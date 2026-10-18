// FC 27 LE Turbo GUI - miniface from the game's 3D model: platform-independent part (see player_capture.h)
#include "player_capture.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "nlohmann/json.hpp"

namespace turbo {
namespace capture {

// ---------------------------------------------------------------- PlayerDesc
int32_t PlayerDesc::i32(size_t off) const {
    int32_t v = 0;
    if (off + 4 <= kPlayerDescSize) std::memcpy(&v, b + off, 4);
    return v;
}

void PlayerDesc::set_i32(size_t off, int32_t v) {
    if (off + 4 <= kPlayerDescSize) std::memcpy(b + off, &v, 4);
}

// The descriptor the game's own builders write (manager portrait 0x147D94218, pro card 0x1485DF1AC):
//   +00 id        +04 -1        +08 second id  +0C -1   +10 -1   +14..+1F 0
//   +20 -1        +24 1         +28 0          +2C 0    +30..+4B 0
//   +4C -1        +50 1         +54 -1         +58 -1   +5C..+63 0
//   +64 byte flag (pro card 1, manager 0)      +65..+67 0      +68 byte flag (manager id 9999)   +69..+6B 0
PlayerDesc default_desc(int32_t id, int32_t second_id, bool flag64, bool flag68) {
    PlayerDesc d;
    d.set_i32(0x00, id);
    d.set_i32(0x04, -1);
    d.set_i32(0x08, second_id);
    d.set_i32(0x0C, -1);
    d.set_i32(0x10, -1);
    d.set_i32(0x20, -1);
    d.set_i32(0x24, 1);
    d.set_i32(0x4C, -1);
    d.set_i32(0x50, 1);
    d.set_i32(0x54, -1);
    d.set_i32(0x58, -1);
    d.set_flag64(flag64);
    d.set_flag68(flag68);
    return d;
}

std::string describe_desc(const PlayerDesc& d) {
    std::string s;
    char buf[48];
    for (size_t off = 0; off + 4 <= kPlayerDescSize; off += 4) {
        int32_t v = d.i32(off);
        if (v == 0) continue;
        std::snprintf(buf, sizeof(buf), "%s+%02zX=%d", s.empty() ? "" : " ", off, v);
        s += buf;
    }
    if (s.empty()) s = "(all zero)";
    return s;
}

std::string hex_bytes(const uint8_t* p, size_t n) {
    static const char* h = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        if (i) s += (i % 16 == 0) ? '|' : ' ';
        s += h[p[i] >> 4];
        s += h[p[i] & 15];
    }
    return s;
}

// ---------------------------------------------------------------- cameras / plan
const std::vector<Camera>& cameras() {
    static const std::vector<Camera> k = {
        {"Portrait (mode 0, extra 0: cards and manager heads)", 0, 0},
        {"Large slice (mode 1, extra 0)", 1, 0},
        {"Avatar outfit (mode 1, extra 3)", 1, 3},
        {"Mode 0, extra 3", 0, 3},
    };
    return k;
}

Plan plan_request(const Request& r, const Template* t) {
    Plan p;
    const bool from_template = r.use_template && t && t->learned;
    // the game's manager-head builder 0x147D94218 (docs/re/manager_rules.md section 6): +0x64 = 0 for a staff head,
    // +0x68 = (id == 9999, the user's created avatar), second id = the manager's team id
    const bool avatar = r.manager && r.id == kUserAvatarHeadId;
    if (from_template) {
        p.desc = t->desc;
        p.desc.set_id(r.id);
        if (r.second_id >= 0) p.desc.set_second_id(r.second_id);
        // the template may come from another kind of capture (live: the hub's portrait of the user's avatar, +0x68 = 1,
        // which made the game draw that avatar's head for every id): the flags always follow this request
        p.desc.set_flag64(!r.manager);
        p.desc.set_flag68(avatar);
        p.note = "learned descriptor";
    } else {
        p.desc = default_desc(r.id, r.second_id, !r.manager, avatar);
        p.note = "default descriptor";
    }
    const auto& cams = cameras();
    if (r.camera == kCameraLearned && t && t->learned) {
        p.mode = t->mode;
        p.extra = t->extra;
        p.note += ", learned mode/extra";
    } else {
        size_t ci = (r.camera >= 0 && static_cast<size_t>(r.camera) < cams.size()) ? static_cast<size_t>(r.camera) : 0;
        p.mode = cams[ci].mode;
        p.extra = cams[ci].extra;
    }
    if (r.mode_override >= 0) p.mode = r.mode_override;
    if (r.extra_override >= 0) p.extra = r.extra_override;
    return p;
}

std::vector<BatchEntry> request_entries(const Request& r) {
    if (!r.batch.empty()) return r.batch;
    BatchEntry e;
    e.id = r.id;
    e.second_id = r.second_id;
    e.manager = r.manager;
    e.label = r.label;
    return {e};
}

std::vector<Plan> plan_batch(const Request& r, const Template* t) {
    std::vector<Plan> out;
    for (const BatchEntry& e : request_entries(r)) {
        Request one = r;
        one.batch.clear();
        one.id = e.id;
        one.second_id = e.second_id;
        one.manager = e.manager;
        one.label = e.label;
        out.push_back(plan_request(one, t));
    }
    return out;
}

bool check_request(const Request& r, size_t max_entries, std::string* err) {
    const std::vector<BatchEntry> es = request_entries(r);
    if (max_entries < 1) max_entries = 1;
    if (es.size() > max_entries || es.size() > kMaxPlayersPerRequest) {
        if (err) *err = "too many heads in one request (" + std::to_string(es.size()) + ", at most " + std::to_string(max_entries) + ")";
        return false;
    }
    for (size_t i = 0; i < es.size(); ++i) {
        if (es[i].id <= 0) {
            if (err) *err = "no id to render";
            return false;
        }
        for (size_t j = 0; j < i; ++j)
            if (es[j].id == es[i].id) {
                if (err) *err = "id " + std::to_string(es[i].id) + " twice in one request";
                return false;
            }
    }
    return true;
}

std::string request_label(const Request& r) {
    if (r.batch.empty()) return r.label.empty() ? ("id " + std::to_string(r.id)) : r.label;
    std::string s = std::to_string(r.batch.size()) + (r.batch.size() == 1 ? " head: " : " heads: ");
    for (size_t i = 0; i < r.batch.size(); ++i) {
        if (i) s += ", ";
        s += r.batch[i].label.empty() ? ("id " + std::to_string(r.batch[i].id)) : r.batch[i].label;
    }
    return s;
}

// ---------------------------------------------------------------- BatchBook
BatchBook::BatchBook(std::vector<BatchEntry> entries)
    : entries_(std::move(entries)), st_(entries_.size(), St::Waiting), err_(entries_.size()) {}

int BatchBook::add_picture(int32_t id, std::vector<uint8_t> bytes) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id != id || st_[i] != St::Waiting) continue;
        st_[i] = St::Picture;
        ++received_;
        Picture p;
        p.entry = i;
        p.bytes = std::move(bytes);
        pics_.push_back(std::move(p));
        return static_cast<int>(i);
    }
    return -1;
}

int BatchBook::add_error(int32_t id, const std::string& why) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id != id || st_[i] != St::Waiting) continue;
        st_[i] = St::Error;
        err_[i] = why;
        return static_cast<int>(i);
    }
    return -1;
}

bool BatchBook::take_picture(Picture& out) {
    if (pics_.empty()) return false;
    out = std::move(pics_.front());
    pics_.erase(pics_.begin());
    st_[out.entry] = St::Handed;
    return true;
}

bool BatchBook::take_failed(bool over, size_t& entry, std::string& why) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (st_[i] == St::Error || (over && st_[i] == St::Waiting)) {
            entry = i;
            why = err_[i];
            st_[i] = St::Handed;
            return true;
        }
    }
    return false;
}

bool BatchBook::all_handed() const {
    for (St s : st_)
        if (s != St::Handed) return false;
    return true;
}

void BatchBook::drop_pictures() {
    for (const Picture& p : pics_) st_[p.entry] = St::Handed;
    pics_.clear();
}

Settle settle_request(bool all_handed, bool finished, bool failed, bool ran, double since_ran_s, double timeout_s) {
    if (!all_handed) return Settle::Running;  // poll() still has Results to hand out (and ends the request itself)
    if (finished || failed) return Settle::Over;
    if (ran && since_ran_s > timeout_s) return Settle::TimedOut;
    return Settle::Running;
}

// ---------------------------------------------------------------- delegate
void* delegate_manager(void* to, void* from, int op) {
    if ((op == kMgrCopy || op == kMgrMove) && to && from && to != from) std::memcpy(to, from, sizeof(void*) * 2);
    return nullptr;
}

Delegate make_delegate(void* context, void* invoker) {
    Delegate d;
    d.storage[0] = context;
    d.storage[1] = nullptr;
    d.manager = reinterpret_cast<void*>(&delegate_manager);
    d.invoker = invoker;
    return d;
}

void* delegate_context(const void* storage) {
    void* ctx = nullptr;
    if (storage) std::memcpy(&ctx, storage, sizeof(ctx));
    return ctx;
}

// ---------------------------------------------------------------- picture bytes
int raw_square_side(size_t size) {
    if (size == 0 || size % 4 != 0) return 0;
    size_t px = size / 4;
    auto side = static_cast<size_t>(std::llround(std::sqrt(static_cast<double>(px))));
    for (size_t s = side > 0 ? side - 1 : 0; s <= side + 1; ++s)
        if (s > 0 && s * s == px && s <= static_cast<size_t>(kMaxImageSide)) return static_cast<int>(s);
    return 0;
}

static bool is_png(const uint8_t* p, size_t n) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return n >= 8 && std::memcmp(p, sig, 8) == 0;
}

bool decode_slice(const uint8_t* data, size_t size, Rgba& out, std::string* format, std::string* err) {
    if (!data || size < 16) {
        if (err) *err = "no picture bytes";
        return false;
    }
    std::vector<uint8_t> bytes(data, data + size);
    std::string e;
    if (size >= 4 && std::memcmp(data, "DDS ", 4) == 0) {
        DdsFormat f;
        std::string fe;
        bool known = parse_dds_format(bytes, f, &fe);
        if (decode_dds(bytes, out, &e)) {
            if (format) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "DDS %s %dx%d", known ? f.name() : "?", out.w, out.h);
                *format = buf;
            }
            return true;
        }
        if (err) *err = "DDS: " + e;
        return false;
    }
    if (is_png(data, size) || (size > 2 && data[0] == 'B' && data[1] == 'M') || (size > 3 && data[0] == 0xFF && data[1] == 0xD8)) {
        if (decode_image(bytes, out, &e)) {
            if (format) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s %dx%d", is_png(data, size) ? "PNG" : data[0] == 'B' ? "BMP" : "JPG", out.w, out.h);
                *format = buf;
            }
            return true;
        }
        if (err) *err = e;
        return false;
    }
    // raw 8-bit, 4 channels, square
    if (int side = raw_square_side(size)) {
        out.w = out.h = side;
        out.px.assign(data, data + size);
        // the game's textures are usually BGRA: a plain-background portrait has far more "blue" than "red" in BGRA order
        // when the background is skin-coloured, so keep RGBA (the editor's background removal does not care) but note it
        if (format) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "raw RGBA %dx%d", side, side);
            *format = buf;
        }
        return true;
    }
    // last resort: stb_image sniffing (TGA has no magic)
    if (decode_image(bytes, out, &e)) {
        if (format) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "image %dx%d", out.w, out.h);
            *format = buf;
        }
        return true;
    }
    if (err) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "unknown picture format (%zu bytes, starts %02X %02X %02X %02X)", size, data[0], data[1], data[2], data[3]);
        *err = buf;
    }
    return false;
}


// ---------------------------------------------------------------- full-body pictures
Request body_request(int32_t id, int32_t team, int camera, const std::string& label) {
    Request r;
    r.id = id;
    r.second_id = team >= 0 ? team : -1;
    r.manager = false;
    r.camera = (camera == kCameraBody || camera == kCameraBodyKit) ? camera : kCameraBody;
    r.mode_override = -1;
    r.extra_override = -1;
    r.use_template = false;
    r.label = label.empty() ? ("player " + std::to_string(id)) : label;
    return r;
}

// ---------------------------------------------------------------- Sequence
namespace {
using json = nlohmann::json;

constexpr int kManifestVersion = 1;

json entry_json(const ManifestEntry& e) {
    json j = json::object();
    j["label"] = e.label;
    j["state"] = e.state;
    j["attempts"] = e.attempts;
    j["file"] = e.file;
    j["w"] = e.w;
    j["h"] = e.h;
    j["bytes"] = e.bytes;
    j["format"] = e.format;
    j["error"] = e.error;
    j["time"] = e.time;
    return j;
}

bool known_state(const std::string& s) {
    return s == "todo" || s == "pending" || s == "done" || s == "failed" || s == "crashed" || s == "cancelled";
}

// "123" -> 123; false for anything else (the manifest's keys are decimal codes)
bool parse_code(const std::string& k, int64_t& out) {
    if (k.empty() || k.size() > 18) return false;
    size_t i = 0;
    bool neg = false;
    if (k[0] == '-') {
        neg = true;
        i = 1;
        if (k.size() == 1) return false;
    }
    int64_t v = 0;
    for (; i < k.size(); ++i) {
        if (k[i] < '0' || k[i] > '9') return false;
        v = v * 10 + (k[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}
}  // namespace

Sequence::Sequence(CaptureService& svc, SequenceOptions opt, std::vector<SequenceStep> steps, SequenceHooks hooks)
    : svc_(svc), opt_(std::move(opt)), hooks_(std::move(hooks)) {
    if (opt_.camera != kCameraBody && opt_.camera != kCameraBodyKit) opt_.camera = kCameraBody;
    if (opt_.max_attempts < 1) opt_.max_attempts = 1;
    if (opt_.max_failures_in_row < 1) opt_.max_failures_in_row = 1;
    for (const SequenceStep& s : steps) {
        if (entry(s.code)) continue;  // a code twice: the first one counts
        ManifestEntry e;
        e.code = s.code;
        e.label = s.label;
        order_.push_back(entries_.size());
        entries_.push_back(std::move(e));
    }
}

const ManifestEntry* Sequence::entry(int64_t code) const {
    for (const ManifestEntry& e : entries_)
        if (e.code == code) return &e;
    return nullptr;
}

size_t Sequence::count(const std::string& state) const {
    size_t n = 0;
    for (size_t i : order_)
        if (entries_[i].state == state) ++n;
    return n;
}

bool Sequence::running() const { return started_ && !finished_; }

int64_t Sequence::current_code() const {
    if (!running() || cursor_ >= order_.size()) return 0;
    if (phase_ == Phase::Writing || phase_ == Phase::Requesting || phase_ == Phase::Waiting) return entries_[order_[cursor_]].code;
    return 0;
}

std::string Sequence::manifest_text() const {
    json j = json::object();
    j["version"] = kManifestVersion;
    j["player"] = opt_.player;
    j["team"] = opt_.team;
    j["camera"] = opt_.camera;
    const auto& cams = cameras();
    j["mode"] = cams[static_cast<size_t>(opt_.camera)].mode;
    j["extra"] = cams[static_cast<size_t>(opt_.camera)].extra;
    j["original"] = opt_.original;
    j["finished"] = finished_;
    j["restored"] = restored_;
    j["updated"] = now_stamp();
    json codes = json::object();
    for (const ManifestEntry& e : entries_) codes[std::to_string(e.code)] = entry_json(e);
    j["codes"] = std::move(codes);
    return j.dump(1);
}

bool Sequence::parse_manifest(const std::string& text, std::vector<ManifestEntry>& out, std::string* err, int32_t* player, int* camera,
                              int64_t* original) {
    out.clear();
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        if (err) *err = std::string("the manifest is not readable JSON: ") + e.what();
        return false;
    }
    if (!j.is_object() || !j.contains("codes") || !j["codes"].is_object()) {
        if (err) *err = "the manifest has no codes";
        return false;
    }
    try {
        if (j.value("version", 0) != kManifestVersion) {
            if (err) *err = "the manifest has another version (" + std::to_string(j.value("version", 0)) + ")";
            return false;
        }
        if (player) *player = j.value("player", 0);
        if (camera) *camera = j.value("camera", -1);
        if (original) *original = j.value("original", static_cast<int64_t>(-1));
        for (auto it = j["codes"].begin(); it != j["codes"].end(); ++it) {
            ManifestEntry e;
            if (!parse_code(it.key(), e.code) || !it.value().is_object()) {
                if (err) *err = "the manifest has a bad code entry '" + it.key() + "'";
                out.clear();
                return false;
            }
            const json& v = it.value();
            e.label = v.value("label", std::string());
            e.state = v.value("state", std::string("todo"));
            if (!known_state(e.state)) e.state = "failed";
            e.attempts = v.value("attempts", 0);
            if (e.attempts < 0) e.attempts = 0;
            e.file = v.value("file", std::string());
            e.w = v.value("w", 0);
            e.h = v.value("h", 0);
            e.bytes = v.value("bytes", static_cast<size_t>(0));
            e.format = v.value("format", std::string());
            e.error = v.value("error", std::string());
            e.time = v.value("time", std::string());
            out.push_back(std::move(e));
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("the manifest has a field of the wrong type: ") + e.what();
        out.clear();
        return false;
    }
    return true;
}

bool Sequence::resume(const std::string& manifest_text_in, std::string* err) {
    if (started_) {
        if (err) *err = "the run has already started";
        return false;
    }
    std::vector<ManifestEntry> old;
    int32_t player = 0;
    int camera = -1;
    int64_t original = -1;
    if (!parse_manifest(manifest_text_in, old, err, &player, &camera, &original)) return false;
    if (player != opt_.player) {
        if (err) *err = "the manifest is for player " + std::to_string(player) + ", this run for " + std::to_string(opt_.player);
        return false;
    }
    if (camera != opt_.camera) {
        if (err) *err = "the manifest was made with camera " + std::to_string(camera) + ", this run uses " + std::to_string(opt_.camera);
        return false;
    }
    for (ManifestEntry& o : old) {
        // the code that was being drawn when the last run stopped without a word: the game (and Turbo) went down with it
        if (o.state == "pending") {
            o.state = o.attempts >= opt_.max_attempts ? "crashed" : "failed";
            o.error = "the last run stopped while this code was being drawn (the game closed or crashed)";
        }
        bool mine = false;
        for (ManifestEntry& e : entries_) {
            if (e.code != o.code) continue;
            const std::string label = e.label.empty() ? o.label : e.label;
            e = o;
            e.label = label;
            mine = true;
            break;
        }
        if (!mine) entries_.push_back(o);  // a code of another list: kept as it was
    }
    if (original >= 0) opt_.original = original;
    return true;
}

bool Sequence::start(std::string* err) {
    auto refuse = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    if (started_) return refuse("the run has already started");
    if (!hooks_.write || !hooks_.save || !hooks_.restore || !hooks_.write_manifest || !hooks_.clock)
        return refuse("the run is missing a step (write, save, restore, manifest or clock)");
    if (opt_.player <= 0) return refuse("no reference player");
    if (opt_.original < 0) return refuse("his own code is not known (options.original): it is needed to put it back at the end");
    started_ = true;
    failures_in_row_ = 0;
    cursor_ = 0;
    phase_ = Phase::Next;
    message_ = "starting: " + std::to_string(order_.size()) + " code(s) on player " + std::to_string(opt_.player);
    if (!save_manifest()) begin_restore(Phase::Aborted, message_);
    return true;
}

bool Sequence::save_manifest() {
    std::string e;
    if (hooks_.write_manifest(manifest_text(), &e)) return true;
    message_ = "stopped: the manifest could not be written (" + (e.empty() ? std::string("no reason given") : e) + ")";
    return false;
}

// The next code to draw: one not done, not crashed, not failed max_attempts times
void Sequence::next_step() {
    while (cursor_ < order_.size()) {
        const ManifestEntry& e = cur();
        const bool spent = e.attempts >= opt_.max_attempts;
        if (e.state == "done" || e.state == "crashed" || ((e.state == "failed" || e.state == "pending") && spent)) {
            ++cursor_;
            continue;
        }
        break;
    }
    if (cursor_ >= order_.size()) {
        begin_restore(Phase::Finished, "all codes drawn: " + std::to_string(count("done")) + " done, " + std::to_string(count("failed")) +
                                            " failed, " + std::to_string(count("crashed")) + " crashed");
        return;
    }
    ManifestEntry& e = cur();
    ++e.attempts;
    e.state = "pending";  // recorded BEFORE the write: a crash from here on leaves "pending" in the file
    e.error.clear();
    e.time = now_stamp();
    if (!save_manifest()) {
        begin_restore(Phase::Aborted, message_);
        return;
    }
    phase_ = Phase::Writing;
    first_call_ = true;
    t_phase_ = now();
    message_ = "code " + std::to_string(e.code) + (e.label.empty() ? "" : " (" + e.label + ")") + ": writing";
}

void Sequence::step_done() {
    failures_in_row_ = 0;
    ++cursor_;
    phase_ = Phase::Next;
    if (!save_manifest()) begin_restore(Phase::Aborted, message_);
}

void Sequence::step_failed(const std::string& why) {
    ManifestEntry& e = cur();
    e.state = "failed";
    e.error = why;
    e.time = now_stamp();
    message_ = "code " + std::to_string(e.code) + ": " + why;
    ++failures_in_row_;
    ++cursor_;
    phase_ = Phase::Next;
    if (!save_manifest()) {
        begin_restore(Phase::Aborted, message_);
        return;
    }
    if (failures_in_row_ >= opt_.max_failures_in_row)
        begin_restore(Phase::Aborted, "stopped: " + std::to_string(failures_in_row_) + " codes in a row got no picture (last: " + why + ")");
}

void Sequence::begin_restore(Phase end, const std::string& why) {
    end_phase_ = end;
    message_ = why;
    phase_ = Phase::Restoring;
    first_call_ = true;
    t_phase_ = now();
}

void Sequence::end_run() {
    phase_ = end_phase_;
    finished_ = true;
    save_manifest();  // best effort: the run is over either way
}

void Sequence::tick() {
    if (!running()) return;
    std::string err;
    switch (phase_) {
    case Phase::Idle:
    case Phase::Next:
        next_step();
        return;
    case Phase::Writing: {
        const bool first = first_call_;
        first_call_ = false;
        const StepResult r = hooks_.write(cur().code, first, &err);
        if (r == StepResult::Done) {
            phase_ = Phase::Requesting;
            t_phase_ = now();
            message_ = "code " + std::to_string(cur().code) + ": asking the game for the picture";
            tick();  // straight on to the request when the service is free
        } else if (r == StepResult::Failed) {
            step_failed("could not write the code" + (err.empty() ? std::string() : ": " + err));
        } else if (now() - t_phase_ > opt_.write_timeout) {
            step_failed("the write did not finish in " + std::to_string(static_cast<int>(opt_.write_timeout)) + " s");
        }
        return;
    }
    case Phase::Requesting: {
        // wait while another picture is in flight, or while the game cannot draw (no menu renderer, or the controller is still
        // finishing a picture Turbo stopped waiting for): a request then would only fail and spend one of the code's attempts
        const Status st = svc_.status();
        if (st.busy || !st.available) {
            if (now() - t_phase_ > opt_.step_timeout) {
                const std::string secs = std::to_string(static_cast<int>(opt_.step_timeout));
                step_failed(st.busy ? "the game was busy with another picture for " + secs + " s"
                                    : "the game could not draw for " + secs + " s" + (st.reason.empty() ? std::string() : ": " + st.reason));
            }
            return;
        }
        const Request rq = body_request(opt_.player, opt_.team, opt_.camera, cur().label.empty() ? std::string() : "player " + std::to_string(opt_.player) + " as " + cur().label);
        if (!svc_.request(rq, &err)) {
            step_failed("the picture was refused" + (err.empty() ? std::string() : ": " + err));
            return;
        }
        phase_ = Phase::Waiting;
        t_phase_ = now();
        return;
    }
    case Phase::Waiting: {
        Result r;
        if (svc_.poll(r)) {
            if (r.id != opt_.player) {
                message_ = "a picture for id " + std::to_string(r.id) + " arrived while waiting for " + std::to_string(opt_.player) + " (dropped)";
                return;
            }
            if (!r.ok) {
                step_failed(r.error.empty() ? std::string("the game gave no picture") : r.error);
                return;
            }
            std::string file;
            if (!hooks_.save(cur().code, r, &file, &err)) {
                step_failed("could not keep the picture" + (err.empty() ? std::string() : ": " + err));
                return;
            }
            ManifestEntry& e = cur();
            e.state = "done";
            e.file = file;
            e.w = r.image.w;
            e.h = r.image.h;
            e.bytes = r.bytes;
            e.format = r.format;
            e.error.clear();
            e.time = now_stamp();
            message_ = "code " + std::to_string(e.code) + ": done (" + r.format + ")";
            step_done();
            return;
        }
        if (now() - t_phase_ > opt_.step_timeout) {
            svc_.cancel();
            step_failed("no picture after " + std::to_string(static_cast<int>(opt_.step_timeout)) + " s (is a menu screen open, not a match?)");
        }
        return;
    }
    case Phase::Restoring: {
        const bool first = first_call_;
        first_call_ = false;
        const StepResult r = hooks_.restore(first, &err);
        if (r == StepResult::Done) {
            restored_ = true;
            end_run();
        } else if (r == StepResult::Failed) {
            restored_ = false;
            message_ += "; HIS OWN CODE COULD NOT BE PUT BACK" + (err.empty() ? std::string() : ": " + err);
            end_run();
        } else if (now() - t_phase_ > opt_.write_timeout) {
            restored_ = false;
            message_ += "; HIS OWN CODE WAS NOT PUT BACK IN TIME";
            end_run();
        }
        return;
    }
    case Phase::Finished:
    case Phase::Cancelled:
    case Phase::Aborted:
        return;
    }
}

void Sequence::cancel() {
    if (!running() || phase_ == Phase::Restoring) return;
    if (phase_ == Phase::Waiting) svc_.cancel();
    if ((phase_ == Phase::Writing || phase_ == Phase::Requesting || phase_ == Phase::Waiting) && cursor_ < order_.size()) {
        ManifestEntry& e = cur();
        e.state = "cancelled";  // not the code's fault: the attempt does not count
        if (e.attempts > 0) --e.attempts;
        e.error = "stopped by the user";
        e.time = now_stamp();
    }
    begin_restore(Phase::Cancelled, "stopped by the user: " + std::to_string(count("done")) + " of " + std::to_string(order_.size()) + " done");
    save_manifest();
}

}  // namespace capture
}  // namespace turbo
