// FC 27 LE Turbo GUI - lit pictures of the model-only hair and beard styles (see hairbeard_thumbs.h)
#include "hairbeard_thumbs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

#include "bodytype_thumbs.h"  // encode_png, write_file_atomic (one PNG writer, one atomic writer)
#include "nlohmann/json.hpp"

namespace turbo::hairbeard_thumbs {

using json = nlohmann::json;

namespace {

std::atomic<int> g_running{0};

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::vector<int64_t> clean(const std::vector<int64_t>& ids) {
    std::set<int64_t> s;
    for (int64_t v : ids)
        if (v > 0 && v < kBeardBase) s.insert(v);
    return {s.begin(), s.end()};
}

struct Index {
    std::mutex mu;
    fs::path root;
    std::set<std::string> names;
    std::chrono::steady_clock::time_point at{};
    bool valid = false;
};
Index& index() {
    static Index i;
    return i;
}

void rescan_locked(Index& i) {
    i.names.clear();
    std::error_code ec;
    const fs::path d = folder(i.root);
    if (fs::is_directory(d, ec)) {
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() == ".png") i.names.insert(it->path().filename().u8string());
        }
    }
    i.at = std::chrono::steady_clock::now();
    i.valid = true;
}

}  // namespace

const char* db_field(Field f) { return f == Field::Hair ? "hairtypecode" : "facialhairtypecode"; }
const char* file_word(Field f) { return f == Field::Hair ? "hair" : "beard"; }
bool field_of(const std::string& name, Field& out) {
    if (name == "hairtypecode") out = Field::Hair;
    else if (name == "facialhairtypecode") out = Field::Beard;
    else return false;
    return true;
}

fs::path folder(const fs::path& t) { return t / "cache" / "hairbeard_thumbs"; }
fs::path picture_file(const fs::path& t, Field f, int64_t id) { return folder(t) / (std::string(file_word(f)) + "_" + std::to_string(id) + ".png"); }
fs::path manifest_file(const fs::path& t) { return folder(t) / "manifest.json"; }
fs::path request_file(const fs::path& t) { return folder(t) / "request.json"; }
fs::path log_file(const fs::path& t) { return folder(t) / "run.log"; }

int64_t step_code(Field f, int64_t id) { return f == Field::Beard ? kBeardBase + id : id; }
void split_code(int64_t code, Field& f, int64_t& id) {
    f = code >= kBeardBase ? Field::Beard : Field::Hair;
    id = code >= kBeardBase ? code - kBeardBase : code;
}

void configure(const fs::path& t) {
    Index& i = index();
    std::lock_guard<std::mutex> lk(i.mu);
    if (i.root != t) {
        i.root = t;
        i.valid = false;
    }
}

fs::path thumb(Field f, int64_t id) {
    Index& i = index();
    std::lock_guard<std::mutex> lk(i.mu);
    if (i.root.empty() || id <= 0) return {};
    if (!i.valid || std::chrono::steady_clock::now() - i.at > std::chrono::seconds(5)) rescan_locked(i);
    const std::string name = std::string(file_word(f)) + "_" + std::to_string(id) + ".png";
    return i.names.count(name) ? folder(i.root) / name : fs::path();
}

void invalidate() {
    Index& i = index();
    std::lock_guard<std::mutex> lk(i.mu);
    i.valid = false;
}

std::vector<capture::SequenceStep> plan_steps(const std::vector<int64_t>& hair, const std::vector<int64_t>& beard, size_t max_steps) {
    std::vector<capture::SequenceStep> out;
    for (int64_t id : clean(hair)) out.push_back({step_code(Field::Hair, id), "hair " + std::to_string(id)});
    for (int64_t id : clean(beard)) out.push_back({step_code(Field::Beard, id), "beard " + std::to_string(id)});
    if (max_steps > 0 && out.size() > max_steps) out.resize(max_steps);
    return out;
}

size_t missing_count(const fs::path& t, const std::vector<int64_t>& hair, const std::vector<int64_t>& beard) {
    size_t n = 0;
    std::error_code ec;
    for (int64_t id : clean(hair)) n += !fs::exists(picture_file(t, Field::Hair, id), ec);
    for (int64_t id : clean(beard)) n += !fs::exists(picture_file(t, Field::Beard, id), ec);
    return n;
}

bool parse_request(const std::string& text, StartRequest& out, std::string* err) {
    out = StartRequest{};
    try {
        const json j = json::parse(text);
        if (!j.is_object()) {
            if (err) *err = "the request is not an object";
            return false;
        }
        if (j.contains("op") && j["op"] != "hairbeard_thumbs") {
            if (err) *err = "the request is not a hairbeard_thumbs request";
            return false;
        }
        out.player = j.value("player", 0);
        const int64_t mx = j.value("max", static_cast<int64_t>(0));
        out.max_steps = mx > 0 ? static_cast<size_t>(mx) : 0;
        out.fresh = j.value("fresh", false);
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

bool take_request(const fs::path& t, StartRequest& out, std::string* err) {
    const fs::path p = request_file(t);
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    std::string text;
    const bool read = read_text(p, text);
    fs::path taken = p;
    taken.replace_filename("request.taken.json");
    fs::remove(taken, ec);
    fs::rename(p, taken, ec);
    if (!read) {
        if (err) *err = "request.json could not be read";
        return false;
    }
    return parse_request(text, out, err);
}

bool append_log(const fs::path& t, const std::string& stamp, const std::string& line) {
    std::error_code ec;
    fs::create_directories(folder(t), ec);
    std::ofstream f(log_file(t), std::ios::binary | std::ios::app);
    if (!f) return false;
    f << stamp << "  " << line << "\n";
    return static_cast<bool>(f);
}

bool generator_running() { return g_running.load() > 0; }

Generator::~Generator() {
    if (counted_) --g_running;
}

void Generator::note(const std::string& s) {
    const std::string stamp = env_.stamp ? env_.stamp() : std::string();
    append_log(env_.turbo_output, stamp, s);
    if (env_.log) env_.log("hair/beard pictures: " + s);
}

capture::StepResult Generator::write_step(int64_t code, std::string* err) {
    Field f;
    int64_t id = 0;
    split_code(code, f, id);
    // one field at a time: while beards are stepped the hair is his own again (and the reverse), so each picture shows one style
    std::string e;
    if (f == Field::Beard && own_hair_ >= 0 && !env_.write_code(ref_.player, Field::Hair, own_hair_, &e)) {
        if (err) *err = "his own hair could not be put back before the beards: " + e;
        return capture::StepResult::Failed;
    }
    if (!env_.write_code(ref_.player, f, id, &e)) {
        if (err) *err = std::string(file_word(f)) + " " + std::to_string(id) + " could not be written: " + e;
        return capture::StepResult::Failed;
    }
    return capture::StepResult::Done;
}

capture::StepResult Generator::restore_step(std::string* err) {
    std::string e1, e2;
    const bool h = own_hair_ < 0 || env_.write_code(ref_.player, Field::Hair, own_hair_, &e1);
    const bool b = own_beard_ < 0 || env_.write_code(ref_.player, Field::Beard, own_beard_, &e2);
    if (h && b) return capture::StepResult::Done;
    if (err) *err = "his own hair / beard could not be written back: " + e1 + " " + e2;
    return capture::StepResult::Failed;
}

bool Generator::start(const std::vector<int64_t>& hair, const std::vector<int64_t>& beard, const Reference& ref, const StartRequest& rq,
                      std::string* err) {
    auto refuse = [&](const std::string& why) {
        if (err) *err = why;
        note("not started: " + why);
        return false;
    };
    if (running()) return refuse("a run is already going");
    if (generator_running()) return refuse("another run is already going");
    if (!env_.svc || !env_.read_code || !env_.write_code || !env_.clock) return refuse("the game's capture service or database is not available here");
    if (ref.player <= 0) return refuse("pick a reference player first");
    if (ref.head_class == 0) return refuse("the reference player must have a generic head (a scanned head keeps its own hair in its textures)");
    const capture::Status st = env_.svc->status();
    if (st.busy) return refuse("another picture is being drawn: wait until it is done");
    std::string e;
    if (!env_.read_code(ref.player, Field::Hair, &own_hair_, &e) || own_hair_ < 0) return refuse("his hair could not be read: " + e);
    if (!env_.read_code(ref.player, Field::Beard, &own_beard_, &e) || own_beard_ < 0) return refuse("his beard could not be read: " + e);
    plan_ = plan_steps(hair, beard, rq.max_steps);
    if (plan_.empty()) return refuse("no hair or beard style without a picture (is the asset catalogue loaded?)");
    ref_ = ref;

    std::string old_text;
    bool resume = false;
    if (!rq.fresh && read_text(manifest_file(env_.turbo_output), old_text)) {
        try {
            const json j = json::parse(old_text);
            resume = j.value("player", 0) == ref.player;
        } catch (const std::exception&) {
            resume = false;
        }
    }
    std::error_code ec;
    fs::create_directories(folder(env_.turbo_output), ec);
    // his own look lives in own.json: after a crash he still wears a test style, so a resumed run takes it from there, not the DB
    const fs::path own = folder(env_.turbo_output) / "own.json";
    std::string own_text;
    if (resume && read_text(own, own_text)) {
        try {
            const json j = json::parse(own_text);
            if (j.value("player", 0) == ref.player) {
                own_hair_ = j.value("hair", own_hair_);
                own_beard_ = j.value("beard", own_beard_);
            }
        } catch (const std::exception&) {
        }
    } else {
        json j{{"player", ref.player}, {"hair", own_hair_}, {"beard", own_beard_}};
        if (!bodytype_thumbs::write_file_atomic(own, j.dump(1), &e)) return refuse("his own hair and beard could not be recorded: " + e);
    }

    capture::SequenceOptions so;
    so.player = ref.player;
    so.team = ref.team;
    so.camera = capture::kCameraBody;  // head and shoulders, mode 1 (hair and beard are proven to reach a capture: assets-and-runtime.md:189)
    so.original = own_hair_;
    so.step_timeout = kStepDeadlineSec;
    capture::SequenceHooks h;
    h.write = [this](int64_t code, bool, std::string* er) { return write_step(code, er); };
    h.restore = [this](bool, std::string* er) { return restore_step(er); };
    h.save = [this](int64_t code, const capture::Result& r, std::string* file, std::string* er) {
        Field f;
        int64_t id = 0;
        split_code(code, f, id);
        const fs::path p = picture_file(env_.turbo_output, f, id);
        const std::vector<uint8_t> png = bodytype_thumbs::encode_png(r.image);
        if (png.empty()) {
            if (er) *er = "the picture could not be encoded";
            return false;
        }
        const bool ok = bodytype_thumbs::write_file_atomic(p, std::string(png.begin(), png.end()), er);
        if (ok && file) *file = p.filename().u8string();
        invalidate();
        note(std::string(file_word(f)) + " " + std::to_string(id) + (ok ? ": picture kept" : ": the picture could not be kept"));
        return ok;
    };
    h.write_manifest = [this](const std::string& text, std::string* er) {
        return bodytype_thumbs::write_file_atomic(manifest_file(env_.turbo_output), text, er);
    };
    h.clock = env_.clock;
    h.stamp = env_.stamp;
    seq_ = std::make_unique<capture::Sequence>(*env_.svc, so, plan_, h);
    if (resume && !seq_->resume(old_text, &e)) {
        seq_.reset();
        return refuse("the earlier run could not be continued: " + e);
    }
    note(std::string(resume ? "continuing" : "starting") + ": " + std::to_string(plan_.size()) + " styles on player " + std::to_string(ref.player) +
         (ref.name.empty() ? "" : " (" + ref.name + ")") + ", his own hair " + std::to_string(own_hair_) + ", beard " + std::to_string(own_beard_));
    if (!seq_->start(&e)) {
        seq_.reset();
        return refuse(e);
    }
    ++g_running;
    counted_ = true;
    tick();
    return true;
}

void Generator::tick() {
    if (!seq_) return;
    if (seq_->running()) seq_->tick();
    if (!seq_->running() && counted_) {
        counted_ = false;
        --g_running;
        invalidate();
        note("ended: " + seq_->message());
    }
}

void Generator::cancel() {
    if (!running()) return;
    seq_->cancel();
    tick();
}

std::string Generator::status() const {
    if (!seq_) return "No run yet.";
    const capture::Sequence& s = *seq_;
    std::string line = std::to_string(s.count("done")) + " of " + std::to_string(s.total()) + " pictures made";
    const size_t f = s.count("failed") + s.count("crashed");
    if (f) line += ", " + std::to_string(f) + " without a picture";
    return (s.running() ? "Making hair and beard pictures: " : "Last run: ") + line + ". " + s.message();
}

}  // namespace turbo::hairbeard_thumbs
