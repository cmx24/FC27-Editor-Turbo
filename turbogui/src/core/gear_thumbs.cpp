// FC 27 LE Turbo GUI - Turbo X: the game draws the look items that ship without a picture (see gear_thumbs.h)
#include "gear_thumbs.h"

#include "faces3d.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "bodytype_thumbs.h"   // encode_png, write_file_atomic
#include "hairbeard_thumbs.h"  // invalidate() after a hair / beard picture
#include "nlohmann/json.hpp"

namespace turbo::gear_thumbs {

using json = nlohmann::json;

namespace {

std::atomic<int> g_running{0};
std::atomic<uint64_t> g_generation{0};

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

const char* cam_word(Cam c) { return c == Cam::Head ? "head" : "body"; }

}  // namespace

const std::vector<FieldSpec>& fields() {
    static const std::vector<FieldSpec> v = {
        {"hairtypecode", "hairtypecode", Cam::Head},
        {"facialhairtypecode", "facialhairtypecode", Cam::Head},
        {"shoetypecode", "shoetypecode", Cam::Body},
        {"gkglovetypecode", "gkglovetypecode", Cam::Body},
        {"accessorycode1", "accessorycode", Cam::Body},
        {"tattoofront", "tattoofront", Cam::Body},
        {"tattooback", "tattooback", Cam::Body},
        {"tattooleftarm", "tattooleftarm", Cam::Body},
        {"tattoorightarm", "tattoorightarm", Cam::Body},
        {"tattooleftleg", "tattooleftleg", Cam::Body},
        {"tattoorightleg", "tattoorightleg", Cam::Body},
        {"bodytypecode", "bodytypecode", Cam::Body},
    };
    return v;
}

const FieldSpec* find_field(const std::string& name, int* index) {
    const std::string n = name == "accessorycode" ? std::string("accessorycode1") : name;
    const auto& v = fields();
    for (size_t i = 0; i < v.size(); ++i)
        if (n == v[i].db) {
            if (index) *index = static_cast<int>(i);
            return &v[i];
        }
    return nullptr;
}

int64_t step_code(int field_index, int64_t id) { return (static_cast<int64_t>(field_index) + 1) * kFieldBase + id; }

bool split_code(int64_t code, int& field_index, int64_t& id) {
    if (code < kFieldBase) return false;
    field_index = static_cast<int>(code / kFieldBase) - 1;
    id = code % kFieldBase;
    return field_index >= 0 && static_cast<size_t>(field_index) < fields().size();
}

fs::path pictures_root(const fs::path& t) { return t / "pictures"; }
fs::path assets_dir(const fs::path& t) { return pictures_root(t) / "assets"; }
fs::path picture_file(const fs::path& t, const FieldSpec& f, int64_t id) { return assets_dir(t) / f.folder / (std::to_string(id) + ".png"); }
fs::path run_dir(const fs::path& t) { return pictures_root(t) / "gear_thumbs"; }
fs::path request_file(const fs::path& t) { return run_dir(t) / "request.json"; }
fs::path want_file(const fs::path& t) { return pictures_root(t) / "want.json"; }
fs::path manifest_file(const fs::path& t) { return run_dir(t) / "manifest.json"; }
fs::path own_file(const fs::path& t) { return run_dir(t) / "own.json"; }
fs::path log_file(const fs::path& t) { return run_dir(t) / "run.log"; }
fs::path status_file(const fs::path& t) { return run_dir(t) / "status.txt"; }

uint64_t generation() { return g_generation.load(); }

bool parse_request(const std::string& text, Request& out, std::string* err, std::vector<std::string>* bad) {
    out = Request{};
    try {
        const json j = json::parse(text);
        if (!j.is_object()) {
            if (err) *err = "the request is not an object";
            return false;
        }
        if (j.contains("op") && j["op"] != "gear_thumbs") {
            if (err) *err = "the request is not a gear_thumbs request";
            return false;
        }
        if (j.contains("player") && !j["player"].is_number_integer()) {
            if (err) *err = "\"player\" is not a number";
            return false;
        }
        out.player = j.value("player", 0);
        if (j.contains("players")) {
            if (!j["players"].is_array()) {
                if (err) *err = "\"players\" is not a list";
                return false;
            }
            for (const json& v : j["players"]) {
                if (!v.is_number_integer() || v.get<int64_t>() <= 0 || v.get<int64_t>() > INT32_MAX) {
                    if (err) *err = "\"players\" holds something that is not a player id";
                    return false;
                }
                const int32_t id = static_cast<int32_t>(v.get<int64_t>());
                if (std::find(out.players.begin(), out.players.end(), id) == out.players.end()) out.players.push_back(id);
            }
            if (out.players.size() > capture::kMaxPlayersPerRequest) {
                if (err) *err = "at most " + std::to_string(capture::kMaxPlayersPerRequest) + " players";
                return false;
            }
        }
        out.fresh = j.contains("fresh") && j["fresh"].is_boolean() && j["fresh"].get<bool>();
        out.real_face = j.contains("real_face") && j["real_face"].is_boolean() && j["real_face"].get<bool>();
        if (j.contains("career") && j["career"].is_string()) out.career = j["career"].get<std::string>();
        if (!j.contains("steps") || !j["steps"].is_array()) {
            if (err) *err = "the request has no \"steps\" list";
            return false;
        }
        size_t n = 0;
        for (const json& s : j["steps"]) {
            ++n;
            if (!s.is_object() || !s.contains("field") || !s["field"].is_string() || !s.contains("id") || !s["id"].is_number_integer()) {
                if (bad) bad->push_back("step " + std::to_string(n) + ": needs a text \"field\" and a whole-number \"id\"");
                continue;
            }
            Step st;
            st.field = s["field"].get<std::string>();
            st.id = s["id"].get<int64_t>();
            if (st.id < 0 || st.id >= kFieldBase) {
                if (bad) bad->push_back("step " + std::to_string(n) + ": id " + std::to_string(st.id) + " is out of range");
                continue;
            }
            if (s.contains("camera") && s["camera"].is_string()) {
                const std::string c = s["camera"].get<std::string>();
                if (c == "head") st.cam = Cam::Head, st.cam_given = true;
                else if (c == "body") st.cam = Cam::Body, st.cam_given = true;
                else if (bad) bad->push_back("step " + std::to_string(n) + ": camera \"" + c + "\" is neither head nor body (the field's own is used)");
            }
            out.steps.push_back(st);
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("the request is not readable: ") + e.what();
        return false;
    }
    if (out.player < 0) {
        if (err) *err = "the request names no player";
        return false;
    }
    return true;
}

bool take_request(const fs::path& t, Request& out, std::string* err, std::vector<std::string>* bad) {
    const fs::path p = request_file(t);
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    std::string text;
    const bool read = read_text(p, text);
    fs::path taken = p;
    taken.replace_filename("request.taken.json");
    fs::remove(taken, ec);
    fs::rename(p, taken, ec);
    if (ec) fs::remove(p, ec);  // never take the same file twice
    if (!read) {
        if (err) *err = "request.json could not be read";
        return false;
    }
    return parse_request(text, out, err, bad);
}

bool read_want(const fs::path& t, Request& out, std::string* err, std::vector<std::string>* bad) {
    std::string text;
    if (!read_text(want_file(t), text)) {
        if (err) *err = "no list of missing pictures yet (want.json)";
        return false;
    }
    return parse_request(text, out, err, bad);
}

bool append_log(const fs::path& t, const std::string& stamp, const std::string& line) {
    std::error_code ec;
    fs::create_directories(run_dir(t), ec);
    std::ofstream f(log_file(t), std::ios::binary | std::ios::app);
    if (!f) return false;
    f << stamp << "  " << line << "\n";
    return static_cast<bool>(f);
}

Plan make_plan(const fs::path& t, const Request& rq, const std::function<bool(const FieldSpec&, int64_t, std::string*)>& check) {
    Plan p;
    std::set<int64_t> seen;
    std::error_code ec;
    for (const Step& s : rq.steps) {
        int fi = -1;
        const FieldSpec* f = find_field(s.field, &fi);
        const std::string what = s.field + " " + std::to_string(s.id);
        if (!f) {
            p.rejected.push_back(what + ": unknown field");
            continue;
        }
        const int64_t code = step_code(fi, s.id);
        if (!seen.insert(code).second) continue;
        if (!rq.fresh && fs::exists(picture_file(t, *f, s.id), ec)) {
            ++p.skipped_existing;
            continue;
        }
        std::string why;
        if (check && !check(*f, s.id, &why)) {
            p.rejected.push_back(what + ": " + (why.empty() ? std::string("not accepted") : why));
            continue;
        }
        const Cam cam = s.cam_given ? s.cam : f->cam;
        (cam == Cam::Head ? p.head : p.body).push_back({fi, s.id, cam});
    }
    return p;
}

// ---------------------------------------------------------------- the generator
bool generator_running() { return g_running.load() > 0; }

Generator::~Generator() {
    if (counted_) --g_running;
}

void Generator::note(const std::string& s) {
    const std::string stamp = env_.stamp ? env_.stamp() : std::string();
    append_log(env_.turbo_output, stamp, s);
    if (env_.log) env_.log("gear pictures: " + s);
}

std::string Generator::where(size_t i) const {
    const Item& it = items_[i];
    return "step " + std::to_string(i + 1) + "/" + std::to_string(items_.size()) + " " + fields()[static_cast<size_t>(it.field_index)].db + " " +
           std::to_string(it.id);
}

// one item on one player: every other field that carries a test value on him gets his own back first (one item per picture)
bool Generator::write_item(size_t ref, size_t item, std::string* err) {
    const int32_t pid = refs_[ref].player;
    const FieldSpec& f = fields()[static_cast<size_t>(items_[item].field_index)];
    std::set<std::string>& dirty = dirty_[pid];
    for (auto it = dirty.begin(); it != dirty.end();) {
        if (*it == f.db) {
            ++it;
            continue;
        }
        std::string e;
        const auto own = own_[pid].find(*it);
        if (own != own_[pid].end() && !env_.write_field(pid, *it, own->second, &e)) {
            if (err) *err = "his own " + *it + " could not be put back first: " + e;
            return false;
        }
        it = dirty.erase(it);
    }
    dirty.insert(f.db);  // even a refused write may have left something: the restore writes it back
    std::string e;
    int64_t value = items_[item].id;
    const std::string fdb = f.db;
    if (real_face_ && (fdb == "hairtypecode" || fdb == "facialhairtypecode")) {  // a scanned head: Turbo's live band code
        value = fdb == "hairtypecode" ? faces3d::hair_band_code(items_[item].id) : faces3d::beard_band_code(items_[item].id);
        if (value <= 0) {
            if (err) *err = "not on Turbo's real-face list (faces3d band)";
            return false;
        }
    }
    if (!env_.write_field(pid, f.db, value, &e)) {
        if (err) *err = "could not be written: " + e;
        return false;
    }
    return true;
}

bool Generator::restore_all(std::string* err) {
    std::string bad;
    for (const auto& pl : own_)
        for (const auto& kv : pl.second) {
            std::string e;
            if (!env_.write_field(pl.first, kv.first, kv.second, &e))
                bad += (bad.empty() ? "" : "; ") + std::to_string(pl.first) + " " + kv.first + ": " + e;
        }
    if (bad.empty()) {
        dirty_.clear();
        return true;
    }
    if (err) *err = bad;
    return false;
}

void Generator::item_failed(size_t i, const std::string& why, bool count_attempt) {
    Item& it = items_[i];
    if (count_attempt) ++it.attempts;
    it.error = why;
    if (count_attempt && it.attempts < kMaxAttempts) {
        it.state = "todo";
        note(where(i) + " -> retry (" + why + ")");
    } else if (count_attempt) {
        it.state = "failed";
        note(where(i) + " -> failed: " + why);
    } else {
        it.state = "todo";  // not the item's fault (cancel)
    }
}

void Generator::pick_round() {
    round_.clear();
    round_pictures_ = 0;
    auto pending = [&](Cam c) {
        for (const Item& it : items_)
            if (it.cam == c && it.state == "todo") return true;
        return false;
    };
    if (!pending(cam_)) {
        if (cam_ == Cam::Head && pending(Cam::Body)) {
            cam_ = Cam::Body;
            note("now the body pictures");
        } else {
            begin_restore("every item had its turn");
            return;
        }
    }
    const capture::Status st = env_.svc->status();
    size_t slots = std::min(refs_.size(), static_cast<size_t>(capture::kMaxPlayersPerRequest));
    if (st.max_batch >= 1) slots = std::min(slots, static_cast<size_t>(st.max_batch));
    std::vector<bool> used(items_.size(), false);
    for (size_t r = 0; r < refs_.size() && round_.size() < slots; ++r) {
        const bool scanned = refs_[r].head_class == 0;
        for (size_t i = 0; i < items_.size(); ++i) {
            Item& it = items_[i];
            if (used[i] || it.cam != cam_ || it.state != "todo") continue;
            const std::string db = fields()[static_cast<size_t>(it.field_index)].db;
            if (scanned && !real_face_ && (db == "hairtypecode" || db == "facialhairtypecode")) continue;
            if (real_face_ && !scanned && (db == "hairtypecode" || db == "facialhairtypecode")) continue;
            used[i] = true;
            std::string e;
            if (!write_item(r, i, &e)) {  // the range was checked at start: a refused write is not retried
                ++it.attempts;
                it.state = "failed";
                it.error = e;
                note(where(i) + " -> failed: " + e);
                continue;
            }
            it.state = "pending";
            round_.push_back({r, i, false});
            break;
        }
    }
    if (round_.empty()) return;  // every candidate failed its write this round: the next tick picks again (failed items make progress)
    save_manifest();
    phase_ = Phase::Requesting;
    t_phase_ = now();
}

void Generator::send_round() {
    const capture::Status st = env_.svc->status();
    if (st.busy || !st.available) {
        if (now() - t_phase_ > kStepDeadlineSec) end_round(st.busy ? "the game was busy with another picture" : "the game could not draw" +
                                                                                                                 (st.reason.empty() ? std::string() : ": " + st.reason),
                                                           true);
        return;
    }
    const int camera = cam_ == Cam::Head ? capture::kCameraBody : capture::kCameraBodyFull;
    const Reference& r0 = refs_[round_[0].ref];
    capture::Request rq = capture::body_request(r0.player, r0.team, camera, "gear pictures, " + std::to_string(round_.size()) + " items");
    if (round_.size() > 1) {
        for (const Slot& sl : round_) {
            const Reference& r = refs_[sl.ref];
            capture::BatchEntry e;
            e.id = r.player;
            e.second_id = r.team >= 0 ? r.team : -1;
            e.manager = false;
            e.label = where(sl.item);
            rq.batch.push_back(e);
        }
    }
    // the camera W2228 proved for the full body (mode 0, 256x256), as capture::Sequence sends it
    if (camera == capture::kCameraBodyFull) rq.mode_override = 0;
    std::string err;
    if (!env_.svc->request(rq, &err)) {
        end_round("the picture was refused" + (err.empty() ? std::string() : ": " + err), true);
        return;
    }
    ++requests_;
    phase_ = Phase::Waiting;
    t_phase_ = now();
}

void Generator::poll_round() {
    capture::Result r;
    while (env_.svc->poll(r)) {
        Slot* sl = nullptr;
        for (Slot& s : round_)
            if (!s.answered && refs_[s.ref].player == r.id) {
                sl = &s;
                break;
            }
        if (!sl) {
            note("a picture for id " + std::to_string(r.id) + " arrived that this round does not wait for (dropped)");
            continue;
        }
        sl->answered = true;
        Item& it = items_[sl->item];
        if (!r.ok) {
            item_failed(sl->item, r.error.empty() ? std::string("the game gave no picture") : r.error, true);
            continue;
        }
        const FieldSpec& f = fields()[static_cast<size_t>(it.field_index)];
        const fs::path p = real_face_ ? pictures_root(env_.turbo_output) / "realface_test" / (std::string(f.folder) + "_" + std::to_string(it.id) + ".png")
                                      : picture_file(env_.turbo_output, f, it.id);
        const std::vector<uint8_t> png = bodytype_thumbs::encode_png(r.image);
        std::string e;
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (png.empty() || !bodytype_thumbs::write_file_atomic(p, std::string(png.begin(), png.end()), &e)) {
            item_failed(sl->item, png.empty() ? std::string("the picture could not be encoded") : "the picture could not be kept: " + e, true);
            continue;
        }
        it.state = "done";
        it.file = p.u8string();
        it.w = r.image.w;
        it.h = r.image.h;
        it.error.clear();
        ++round_pictures_;
        ++g_generation;
        if (f.cam == Cam::Head) hairbeard_thumbs::invalidate();
        note(where(sl->item) + " -> saved " + it.file + " " + std::to_string(r.image.w) + "x" + std::to_string(r.image.h));
    }
    bool all = true;
    for (const Slot& s : round_) all = all && s.answered;
    if (all) {
        end_round(std::string(), true);
        return;
    }
    if (now() - t_phase_ > kStepDeadlineSec) {
        env_.svc->cancel();
        end_round("no picture after " + std::to_string(static_cast<int>(kStepDeadlineSec)) + " s (is a menu screen open, not a match?)", true);
    }
}

void Generator::end_round(const std::string& why, bool count_attempt) {
    for (const Slot& s : round_)
        if (!s.answered && items_[s.item].state == "pending") item_failed(s.item, why.empty() ? std::string("no picture") : why, count_attempt);
    const bool empty = round_pictures_ == 0 && !round_.empty();
    round_.clear();
    save_manifest();
    write_status();
    if (!count_attempt) return;
    empty_rounds_ = empty ? empty_rounds_ + 1 : 0;
    if (empty_rounds_ >= kMaxEmptyRounds) {
        aborted_ = true;
        begin_restore(std::to_string(kMaxEmptyRounds) + " rounds in a row without a picture: the game stopped answering");
        return;
    }
    phase_ = Phase::Pick;
}

void Generator::begin_restore(const std::string& why) {
    end_message_ = why;
    phase_ = Phase::Restoring;
}

void Generator::finish() {
    std::string e;
    restored_ = restore_all(&e);
    std::error_code ec;
    if (restored_) fs::remove(own_file(env_.turbo_output), ec);  // clean end: the next run reads their values from the database
    else {
        end_message_ += "; THEIR OWN VALUES WERE NOT ALL PUT BACK: " + e;
        note("their own values were NOT all put back (" + e + "): own.json keeps them for the next run");
    }
    phase_ = Phase::Ended;
    if (counted_) {
        counted_ = false;
        --g_running;
    }
    save_manifest();
    hairbeard_thumbs::invalidate();
    note(std::string(cancelled_ ? "cancelled" : (aborted_ ? "stopped" : "done")) + ": " + std::to_string(done()) + " pictures, " + std::to_string(failed()) +
         " without one, " + std::to_string(requests_) + " requests (" + end_message_ + ")");
    write_status();
}

void Generator::save_manifest() {
    json j{{"players", json::array()}, {"items", json::array()}};
    for (const Reference& r : refs_) j["players"].push_back(r.player);
    for (const Item& it : items_)
        j["items"].push_back({{"field", fields()[static_cast<size_t>(it.field_index)].db}, {"id", it.id}, {"camera", cam_word(it.cam)}, {"state", it.state},
                              {"attempts", it.attempts}, {"error", it.error}, {"file", it.file}, {"w", it.w}, {"h", it.h}});
    std::string e;
    if (!bodytype_thumbs::write_file_atomic(manifest_file(env_.turbo_output), j.dump(1), &e)) note("the manifest could not be written: " + e);
}

// an earlier run's manifest (same players): items that were being drawn when Turbo died count an attempt ("crashed" at the
// limit: never tried again); done items whose picture is gone are drawn again; failed items get a new chance
void Generator::load_manifest() {
    std::string text;
    if (!read_text(manifest_file(env_.turbo_output), text)) return;
    try {
        const json j = json::parse(text);
        std::set<int32_t> mine, theirs;
        for (const Reference& r : refs_) mine.insert(r.player);
        if (j.contains("players") && j["players"].is_array())
            for (const json& v : j["players"])
                if (v.is_number_integer()) theirs.insert(v.get<int32_t>());
        if (mine != theirs || !j.contains("items") || !j["items"].is_array()) return;
        for (const json& m : j["items"]) {
            if (!m.is_object()) continue;
            const std::string field = m.value("field", std::string());
            const int64_t id = m.value("id", static_cast<int64_t>(-1));
            const std::string state = m.value("state", std::string());
            const int attempts = m.value("attempts", 0);
            for (Item& it : items_) {
                if (fields()[static_cast<size_t>(it.field_index)].db != field || it.id != id) continue;
                if (state == "pending" || state == "crashed") {
                    it.attempts = state == "crashed" ? kMaxAttempts : attempts + 1;
                    if (it.attempts >= kMaxAttempts) {
                        it.state = "crashed";
                        it.error = "it was being drawn when Turbo or the game stopped, " + std::to_string(kMaxAttempts) + " times";
                    }
                }
            }
        }
    } catch (const std::exception&) {
    }
}

bool Generator::start(const Request& rq, const std::vector<Reference>& refs, std::string* err) {
    auto refuse = [&](const std::string& why) {
        if (err) *err = why;
        note("not started: " + why);
        return false;
    };
    if (running()) return refuse("a run is already going");
    if (generator_running()) return refuse("another run is already going");
    if (hairbeard_thumbs::generator_running() || bodytype_thumbs::generator_running()) return refuse("another picture run is going");
    if (!env_.svc || !env_.read_field || !env_.write_field || !env_.clock) return refuse("the game's capture service or database is not available here");
    if (refs.empty()) return refuse("pick a reference player first");
    if (refs.size() > capture::kMaxPlayersPerRequest) return refuse("at most " + std::to_string(capture::kMaxPlayersPerRequest) + " reference players");
    std::set<int32_t> ids;
    bool any_generic = false;
    for (const Reference& r : refs) {
        if (r.player <= 0 || !ids.insert(r.player).second) return refuse("the reference players must be distinct player ids");
        any_generic = any_generic || r.head_class != 0;
    }
    if (env_.svc->status().busy) return refuse("another picture is being drawn: wait until it is done");

    Plan plan = make_plan(env_.turbo_output, rq, env_.check);
    if (!any_generic && !rq.real_face) {  // a scanned head carries its own hair and stubble in its textures (hairbeard_thumbs.h)
        std::vector<Planned> keep;
        for (const Planned& s : plan.head) {
            const std::string db = fields()[static_cast<size_t>(s.field_index)].db;
            if (db == "hairtypecode" || db == "facialhairtypecode")
                plan.rejected.push_back(db + " " + std::to_string(s.id) + ": no reference player has a generic head");
            else keep.push_back(s);
        }
        plan.head = keep;
    }
    for (const std::string& r : plan.rejected) note("failed: " + r);
    rejected_ = plan.rejected.size();
    if (plan.skipped_existing) note(std::to_string(plan.skipped_existing) + " items already have a picture (kept; \"fresh\": true draws them again)");
    items_.clear();
    std::set<std::string> touched;
    for (const std::vector<Planned>* v : {&plan.head, &plan.body})
        for (const Planned& s : *v) {
            Item it;
            it.field_index = s.field_index;
            it.id = s.id;
            it.cam = s.cam;
            items_.push_back(it);
            touched.insert(fields()[static_cast<size_t>(s.field_index)].db);
        }
    if (items_.empty()) {
        write_status();
        return refuse("nothing to draw: every requested item has a picture or was refused");
    }
    refs_ = refs;

    // their own values: own.json of an earlier run that did not end cleanly (they may still wear a test item), else the database
    std::error_code ec;
    fs::create_directories(run_dir(env_.turbo_output), ec);
    own_.clear();
    dirty_.clear();
    std::string own_text;
    const bool had_own = !rq.fresh && read_text(own_file(env_.turbo_output), own_text);
    if (had_own) {
        try {
            const json j = json::parse(own_text);
            if (j.contains("players") && j["players"].is_object())
                for (auto p = j["players"].begin(); p != j["players"].end(); ++p) {
                    const int32_t pid = static_cast<int32_t>(std::strtol(p.key().c_str(), nullptr, 10));
                    if (!ids.count(pid) || !p.value().is_object()) continue;
                    for (auto f = p.value().begin(); f != p.value().end(); ++f)
                        if (f.value().is_number_integer()) own_[pid][f.key()] = f.value().get<int64_t>();
                }
        } catch (const std::exception&) {
            own_.clear();
        }
    }
    for (const Reference& r : refs_)
        for (const std::string& f : touched) {
            if (own_[r.player].count(f)) {
                dirty_[r.player].insert(f);  // he may still wear the earlier run's test value: the first write puts it back
                continue;
            }
            int64_t v = -1;
            std::string e;
            if (!env_.read_field(r.player, f, &v, &e) || v < 0) return refuse("player " + std::to_string(r.player) + ": his " + f + " could not be read: " + e);
            own_[r.player][f] = v;
        }
    json oj{{"players", json::object()}};
    for (const auto& pl : own_)
        for (const auto& kv : pl.second) oj["players"][std::to_string(pl.first)][kv.first] = kv.second;
    std::string e;
    if (!bodytype_thumbs::write_file_atomic(own_file(env_.turbo_output), oj.dump(1), &e)) return refuse("their own values could not be recorded: " + e);

    fresh_ = rq.fresh;
    real_face_ = rq.real_face;
    if (!fresh_) load_manifest();
    cancelled_ = aborted_ = restored_ = false;
    requests_ = 0;
    empty_rounds_ = 0;
    end_message_.clear();
    cam_ = Cam::Head;
    size_t nh = 0;
    for (const Item& it : items_) nh += it.cam == Cam::Head;
    std::string who;
    for (const Reference& r : refs_) who += (who.empty() ? "" : ", ") + std::to_string(r.player) + (r.name.empty() ? "" : " " + r.name);
    note("run: " + std::to_string(nh) + " head and " + std::to_string(items_.size() - nh) + " body items, " + std::to_string(refs_.size()) +
         " reference players (" + who + "), up to " + std::to_string(std::min(refs_.size(), static_cast<size_t>(std::max(1, env_.svc->status().max_batch)))) +
         " per request");
    phase_ = Phase::Pick;
    ++g_running;
    counted_ = true;
    save_manifest();
    write_status();
    tick();
    return true;
}

void Generator::tick() {
    if (!running()) return;
    switch (phase_) {
    case Phase::Pick:
        pick_round();
        break;
    case Phase::Requesting:
        send_round();
        break;
    case Phase::Waiting:
        poll_round();
        break;
    default:
        break;
    }
    if (phase_ == Phase::Restoring) finish();
    else write_status();
}

void Generator::cancel() {
    if (!running()) return;
    cancelled_ = true;
    if (phase_ == Phase::Waiting) env_.svc->cancel();
    end_round("stopped by the user", false);
    begin_restore("stopped by the user");
    finish();
}

size_t Generator::done() const {
    size_t n = 0;
    for (const Item& it : items_) n += it.state == "done";
    return n;
}

size_t Generator::failed() const {
    size_t n = rejected_;
    for (const Item& it : items_) n += it.state == "failed" || it.state == "crashed";
    return n;
}

std::string Generator::status() const {
    if (phase_ == Phase::Idle && last_status_.empty()) return "No run yet.";
    std::string line = std::to_string(done()) + " of " + std::to_string(total()) + " pictures made";
    if (failed()) line += ", " + std::to_string(failed()) + " without one";
    if (running()) return std::string("Making pictures (") + cam_word(cam_) + "): " + line + ".";
    return std::string(cancelled_ ? "Stopped: " : "Last run: ") + line + ". " + end_message_;
}

void Generator::write_status() {
    std::ostringstream s;
    s << "state " << (running() ? "running" : (cancelled_ ? "cancelled" : (aborted_ ? "stopped" : "done"))) << "\n";
    s << "camera " << cam_word(cam_) << "\n";
    s << "done " << done() << "\n";
    s << "failed " << failed() << "\n";
    s << "total " << total() << "\n";
    s << "rejected " << rejected_ << "\n";
    s << "players " << refs_.size() << "\n";
    s << "requests " << requests_ << "\n";
    if (!running() && !end_message_.empty()) s << "message " << end_message_ << "\n";
    const std::string text = s.str();
    if (text == last_status_) return;
    last_status_ = text;
    std::string e;
    bodytype_thumbs::write_file_atomic(status_file(env_.turbo_output), text, &e);
}

}  // namespace turbo::gear_thumbs
