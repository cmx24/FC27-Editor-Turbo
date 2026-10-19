// FC 27 LE Turbo GUI - career faces (see career_faces.h)
#include "career_faces.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>

#include "nlohmann/json.hpp"
#include "reapply.h"

namespace turbo {
namespace career_faces {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kImgAssets = "data/ui/imgAssets/";

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string hex64(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(v));
    return b;
}

bool read_bytes(const fs::path& f, std::string& out) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return static_cast<bool>(in) || in.eof();
}

bool regular(const fs::path& f) {
    std::error_code ec;
    return fs::is_regular_file(f, ec);
}

bool write_atomic(const fs::path& p, const std::string& data, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {  // Windows: a rename over an existing file can fail; replace in two steps
        ec.clear();
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + p.string();
            return false;
        }
    }
    return true;
}

// A free name next to `want` (want, want.1, want.2, ...)
fs::path free_name(const fs::path& want) {
    if (!fs::exists(want)) return want;
    for (int i = 1; i < 10000; ++i) {
        fs::path c = want;
        c += "." + std::to_string(i);
        if (!fs::exists(c)) return c;
    }
    return want;
}

struct Entry {
    std::string rel;
    std::string fnv;   // hash of the bytes Turbo put in mods\legacy
    uint64_t size = 0;
    std::string base;  // the file it replaced, kept here (u8 path); "" = there was none
};
struct Manifest {
    std::string career;
    std::vector<Entry> entries;
};

// false = unreadable (never acted on); a missing file is an empty record
bool load_manifest(const Paths& p, Manifest& m, std::string* err) {
    m = Manifest{};
    const fs::path f = manifest_file(p);
    if (!regular(f)) return true;
    std::string text;
    if (!read_bytes(f, text)) {
        if (err) *err = "cannot read " + f.string();
        return false;
    }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("entries") || !j["entries"].is_array()) {
        if (err) *err = f.string() + " is not a readable safety record";
        return false;
    }
    m.career = j.value("career", std::string());
    for (const auto& e : j["entries"]) {
        if (!e.is_object()) continue;
        Entry x;
        x.rel = e.value("rel", std::string());
        x.fnv = e.value("fnv", std::string());
        x.size = e.value("size", uint64_t(0));
        x.base = e.value("base", std::string());
        if (!is_face_path(x.rel) || x.fnv.empty()) {
            if (err) *err = f.string() + ": an entry is not a face picture record";
            return false;
        }
        m.entries.push_back(x);
    }
    return true;
}

bool save_manifest(const Paths& p, const Manifest& m, std::string* err) {
    const fs::path f = manifest_file(p);
    if (m.entries.empty()) {  // nothing of Turbo's in mods\legacy: no record at all
        std::error_code ec;
        fs::remove(f, ec);
        if (regular(f)) {
            if (err) *err = "cannot remove " + f.string();
            return false;
        }
        return true;
    }
    json j = json::object();
    j["version"] = 1;
    j["career"] = m.career;
    j["note"] = "Turbo career faces: the face pictures of this career copied into mods\\legacy; put back at the career's exit and at Turbo start";
    j["entries"] = json::array();
    for (const auto& e : m.entries)
        j["entries"].push_back({{"rel", e.rel}, {"fnv", e.fnv}, {"size", e.size}, {"base", e.base}});
    return write_atomic(f, j.dump(1), err);
}

std::string hash_of(const std::string& bytes) { return hex64(fnv1a64(bytes)); }

// The career's wanted pictures: rel -> bytes
std::map<std::string, std::string> wanted(const Paths& p, const std::string& career, std::vector<std::string>& errors) {
    std::map<std::string, std::string> out;
    if (!is_career_id(career)) return out;
    const fs::path root = careers_dir(p) / career;
    for (const char* sub : {"heads", "heads_staff"}) {
        const fs::path d = root / sub;
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string name = it->path().filename().u8string();
            const std::string rel = std::string(kImgAssets) + sub + "/" + name;
            if (!is_face_path(rel)) continue;  // .tmp and anything else
            std::string bytes;
            if (!read_bytes(it->path(), bytes) || bytes.empty()) {
                errors.push_back("cannot read the career's copy " + it->path().string());
                continue;
            }
            out[rel] = bytes;
        }
    }
    return out;
}

}  // namespace

bool is_career_id(const std::string& id) {
    if (id.size() != 16) return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool is_face_path(const std::string& rel_in) {
    if (rel_in.empty() || rel_in.size() > 400) return false;
    std::string rel = rel_in;
    for (char& c : rel)
        if (c == '\\') c = '/';
    const std::string l = lower(rel);
    std::string rest;
    for (const char* pre : {"data/ui/imgassets/heads/", "data/ui/imgassets/heads_staff/"}) {
        const std::string pr = pre;
        if (l.compare(0, pr.size(), pr) == 0) rest = l.substr(pr.size());
    }
    if (rest.empty() || rest.find('/') != std::string::npos || rest.find(':') != std::string::npos) return false;
    if (rest == "." || rest == ".." || rest.size() <= 4 || rest.compare(rest.size() - 4, 4, ".dds") != 0) return false;
    for (char c : rest)
        if (static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}

fs::path careers_dir(const Paths& p) { return p.turbo_output / "careers"; }
fs::path manifest_file(const Paths& p) { return careers_dir(p) / "active.json"; }

fs::path overlay_file(const Paths& p, const std::string& career, const std::string& rel) {
    std::string r = rel;
    for (char& c : r)
        if (c == '\\') c = '/';
    const std::string pre = kImgAssets;
    if (lower(r).compare(0, pre.size(), lower(pre)) == 0) r = r.substr(pre.size());
    return careers_dir(p) / career / fs::u8path(r);
}

fs::path mods_file(const Paths& p, const std::string& rel) {
    std::string r = rel;
    for (char& c : r)
        if (c == '\\') c = '/';
    return p.mods_legacy / fs::u8path(r);
}

static fs::path undo_copy(const Paths& p, const std::string& career, const fs::path& f) {
    const fs::path dir = careers_dir(p) / career / "_undo";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path to = free_name(dir / f.filename());
    fs::copy_file(f, to, fs::copy_options::overwrite_existing, ec);
    return ec ? fs::path() : to;
}

bool save_overlay(const Paths& p, const std::string& career, const std::string& rel, const std::vector<uint8_t>& bytes,
                  std::string* err, fs::path* undo_backup) {
    if (undo_backup) undo_backup->clear();
    if (!is_career_id(career)) {
        if (err) *err = "no career identified";
        return false;
    }
    if (!is_face_path(rel)) {
        if (err) *err = "not a face picture path: " + rel;
        return false;
    }
    if (bytes.empty()) {
        if (err) *err = "nothing to write";
        return false;
    }
    const fs::path f = overlay_file(p, career, rel);
    if (regular(f)) {
        const fs::path b = undo_copy(p, career, f);
        if (b.empty()) {
            if (err) *err = "cannot keep the career's earlier copy of " + rel;
            return false;
        }
        if (undo_backup) *undo_backup = b;
    }
    return write_atomic(f, std::string(bytes.begin(), bytes.end()), err);
}

bool remove_overlay(const Paths& p, const std::string& career, const std::string& rel, std::string* err, fs::path* undo_backup) {
    if (undo_backup) undo_backup->clear();
    if (!is_career_id(career) || !is_face_path(rel)) {
        if (err) *err = "no career copy for " + rel;
        return false;
    }
    const fs::path f = overlay_file(p, career, rel);
    if (!regular(f)) {
        if (err) *err = "this career has no copy of " + rel;
        return false;
    }
    const fs::path b = undo_copy(p, career, f);
    if (b.empty()) {
        if (err) *err = "cannot keep the career's copy of " + rel + " before removing it";
        return false;
    }
    if (undo_backup) *undo_backup = b;
    std::error_code ec;
    fs::remove(f, ec);
    if (regular(f)) {
        if (err) *err = "cannot remove " + f.string();
        return false;
    }
    return true;
}

bool has_overlay(const Paths& p, const std::string& career, const std::string& rel) {
    return is_career_id(career) && is_face_path(rel) && regular(overlay_file(p, career, rel));
}

bool restore_overlay(const Paths& p, const std::string& career, const std::string& rel, const fs::path& undo_backup, std::string* err) {
    if (!is_career_id(career) || !is_face_path(rel)) {
        if (err) *err = "no career copy for " + rel;
        return false;
    }
    const fs::path f = overlay_file(p, career, rel);
    if (undo_backup.empty()) {
        std::error_code ec;
        fs::remove(f, ec);
        if (regular(f)) {
            if (err) *err = "cannot remove " + f.string();
            return false;
        }
        return true;
    }
    std::string bytes;
    if (!read_bytes(undo_backup, bytes) || bytes.empty()) {
        if (err) *err = "the career's earlier copy " + undo_backup.string() + " is missing or empty";
        return false;
    }
    return write_atomic(f, bytes, err);
}

std::vector<std::string> SyncResult::touched() const {
    std::vector<std::string> t = copied;
    t.insert(t.end(), restored.begin(), restored.end());
    return t;
}

std::string SyncResult::summary() const {
    std::string s = "career " + (career.empty() ? std::string("none") : career) + ": " + std::to_string(copied.size()) + " copied in, " +
                    std::to_string(restored.size()) + " put back";
    if (!left_foreign.empty()) s += ", " + std::to_string(left_foreign.size()) + " left alone (not Turbo's copy any more)";
    if (!errors.empty()) s += ", " + std::to_string(errors.size()) + " error(s): " + errors.front();
    return s;
}

std::string active_career(const Paths& p) {
    Manifest m;
    if (!load_manifest(p, m, nullptr)) return {};
    return m.entries.empty() ? std::string() : m.career;
}

SyncResult sync(const Paths& p, const std::string& career_in) {
    SyncResult r;
    const std::string career = is_career_id(career_in) ? career_in : std::string();
    Manifest m;
    std::string err;
    if (!load_manifest(p, m, &err)) {
        // never act on a record Turbo cannot read: set it aside, touch nothing in mods\legacy
        std::error_code ec;
        const fs::path f = manifest_file(p);
        fs::path bad = f;
        bad += ".bad";
        fs::rename(f, free_name(bad), ec);
        r.errors.push_back(err + " (moved aside to active.json.bad; nothing in mods\\legacy was touched)");
        r.career = "";
        return r;
    }
    const std::map<std::string, std::string> want = wanted(p, career, r.errors);

    // 1. restore every recorded path that is not wanted as it is
    std::vector<Entry> keep;
    for (const Entry& e : m.entries) {
        const fs::path cur = mods_file(p, e.rel);
        std::string now;
        const bool present = regular(cur) && read_bytes(cur, now);
        const bool ours = present && hash_of(now) == e.fnv;
        auto w = want.find(e.rel);
        if (m.career == career && w != want.end() && hash_of(w->second) == e.fnv && ours) {
            keep.push_back(e);
            continue;
        }
        const fs::path base = e.base.empty() ? fs::path() : fs::u8path(e.base);
        bool done = true;
        if (present && !ours) {
            r.left_foreign.push_back(e.rel);  // someone wrote over Turbo's copy: theirs stays (the base backup is kept too)
        } else if (!base.empty()) {
            std::string bytes;
            if (!read_bytes(base, bytes) || bytes.empty()) {
                r.errors.push_back("the earlier " + e.rel + " kept at " + base.string() + " is missing: Turbo's copy left in mods\\legacy");
                done = false;
            } else if (!write_atomic(cur, bytes, &err)) {
                r.errors.push_back("putting back " + e.rel + ": " + err);
                done = false;
            } else {
                std::error_code ec;
                fs::remove(base, ec);
                r.restored.push_back(e.rel);
            }
        } else if (present) {
            std::error_code ec;
            fs::remove(cur, ec);
            if (regular(cur)) {
                r.errors.push_back("cannot remove " + cur.string());
                done = false;
            } else {
                r.restored.push_back(e.rel);
            }
        }
        if (!done) keep.push_back(e);  // still Turbo's: tried again at the next sync
    }
    m.entries = keep;
    if (m.entries.empty()) m.career.clear();
    if (!save_manifest(p, m, &err)) {
        r.errors.push_back("safety record: " + err + " (nothing copied in)");
        r.career = m.career;
        return r;
    }
    // a failed restore of another career's copy: nothing of this career goes in while the record names another one
    if (!m.entries.empty() && m.career != career) {
        r.career = m.career;
        return r;
    }

    // 2. copy the wanted pictures in (the record first: write-ahead)
    for (const auto& [rel, bytes] : want) {
        bool have = false;
        for (const Entry& e : m.entries) have = have || e.rel == rel;
        if (have) continue;
        const fs::path cur = mods_file(p, rel);
        Entry e;
        e.rel = rel;
        e.fnv = hash_of(bytes);
        e.size = bytes.size();
        if (regular(cur)) {
            const fs::path base = free_name(careers_dir(p) / "_base" / fs::u8path(rel));
            std::error_code ec;
            fs::create_directories(base.parent_path(), ec);
            fs::copy_file(cur, base, fs::copy_options::overwrite_existing, ec);
            if (ec || !regular(base)) {
                r.errors.push_back("cannot keep the earlier " + rel + ": not copied in");
                continue;
            }
            e.base = base.u8string();
        }
        m.career = career;
        m.entries.push_back(e);
        if (!save_manifest(p, m, &err)) {
            m.entries.pop_back();
            r.errors.push_back("safety record: " + err + " (" + rel + " not copied in)");
            continue;
        }
        if (!write_atomic(cur, bytes, &err)) {
            r.errors.push_back("copying " + rel + " in: " + err);
            continue;  // the record keeps the entry: the next sync sees mods\legacy is not Turbo's copy and leaves it
        }
        r.copied.push_back(rel);
    }
    if (m.entries.empty()) m.career.clear();
    save_manifest(p, m, nullptr);
    // the career in effect (also when it has no pictures: the log line names it, "career none" only outside a career)
    r.career = m.career.empty() ? career : m.career;
    return r;
}

// ---------------------------------------------------------------- 2.20 faces safety

std::string bytes_hash(const std::string& bytes) { return hash_of(bytes); }

std::string file_hash(const fs::path& f) {
    if (!regular(f)) return {};
    std::string b;
    if (!read_bytes(f, b)) return {};
    return hash_of(b);
}

namespace {
std::string norm_rel(std::string r) {
    for (char& c : r)
        if (c == '\\') c = '/';
    return lower(r);
}

// careers\global_own.json: {"version":1,"entries":{"<rel lower>":"<fnv>"}}; false = unreadable
bool load_global(const Paths& p, std::map<std::string, std::string>& out, std::string* err) {
    out.clear();
    const fs::path f = global_own_file(p);
    if (!regular(f)) return true;
    std::string text;
    if (!read_bytes(f, text)) {
        if (err) *err = "cannot read " + f.string();
        return false;
    }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("entries") || !j["entries"].is_object()) {
        if (err) *err = f.string() + " is not a readable record";
        return false;
    }
    for (auto it = j["entries"].begin(); it != j["entries"].end(); ++it)
        if (it.value().is_string()) out[it.key()] = it.value().get<std::string>();
    return true;
}

bool save_global(const Paths& p, const std::map<std::string, std::string>& m, std::string* err) {
    json j = json::object();
    j["version"] = 1;
    j["note"] = "Turbo's own face pictures written outside a career (FNV-1a 64 of the bytes): only these may be replaced or removed by Turbo";
    j["entries"] = json::object();
    for (const auto& [k, v] : m) j["entries"][k] = v;
    return write_atomic(global_own_file(p), j.dump(1), err);
}
}  // namespace

OwnerInfo owner_of(const Paths& p, const std::string& rel) {
    OwnerInfo o;
    o.hash = file_hash(mods_file(p, rel));
    if (o.hash.empty()) return o;  // Owner::None
    o.owner = Owner::Foreign;
    const std::string key = norm_rel(rel);
    Manifest m;
    if (load_manifest(p, m, nullptr)) {
        for (const Entry& e : m.entries)
            if (norm_rel(e.rel) == key && e.fnv == o.hash) {
                o.owner = Owner::TurboCareer;
                o.base_under = !e.base.empty();
                return o;
            }
    }
    std::map<std::string, std::string> g;
    if (load_global(p, g, nullptr)) {
        auto it = g.find(key);
        if (it != g.end() && it->second == o.hash) o.owner = Owner::TurboGlobal;
    }
    return o;
}

fs::path global_own_file(const Paths& p) { return careers_dir(p) / "global_own.json"; }

bool may_replace_global(const Paths& p, const std::string& rel, std::string* why) {
    if (!is_face_path(rel)) return true;  // not a face picture: not this rule's business
    const OwnerInfo o = owner_of(p, rel);
    if (o.owner == Owner::None || o.owner == Owner::TurboGlobal) return true;
    if (why)
        *why = o.owner == Owner::TurboCareer
                   ? rel + " is a career's picture in mods\\legacy right now (put back when the career is left): not changed outside it"
                   : rel + " was not made by Turbo (the game's donor set, another tool, you, or Turbo before 2.20: no record of it), so Turbo "
                           "leaves it as it is";
    return false;
}

bool record_global(const Paths& p, const std::string& rel, const std::string& bytes, std::string* err) {
    std::map<std::string, std::string> g;
    if (!load_global(p, g, err)) return false;
    g[norm_rel(rel)] = hash_of(bytes);
    return save_global(p, g, err);
}

bool forget_global(const Paths& p, const std::string& rel, std::string* err) {
    std::map<std::string, std::string> g;
    if (!load_global(p, g, err)) return false;
    if (!g.erase(norm_rel(rel))) return true;
    return save_global(p, g, err);
}

MinifaceKind classify_miniface(const MinifaceFacts& f) {
    if (f.file.owner == Owner::Foreign) return MinifaceKind::Real;
    if (f.file.owner == Owner::TurboCareer && f.file.base_under) return MinifaceKind::Real;
    if (f.game == Ships::Yes) return MinifaceKind::Real;
    if (f.game == Ships::Unknown) return MinifaceKind::Unknown;
    return f.file.owner == Owner::None ? MinifaceKind::None : MinifaceKind::TurboOwn;
}

const char* miniface_kind_name(MinifaceKind k) {
    switch (k) {
        case MinifaceKind::None: return "none";
        case MinifaceKind::TurboOwn: return "Turbo's own";
        case MinifaceKind::Real: return "real";
        default: return "unknown";
    }
}

// ---- heads_own.json
std::string squad_scope() { return "_squad"; }
bool is_head_scope(const std::string& scope) { return is_career_id(scope) || scope == squad_scope(); }
fs::path heads_own_file(const Paths& p, const std::string& scope) { return careers_dir(p) / scope / "heads_own.json"; }

namespace {
json own_to_json(const OwnHead& h) {
    json f = json::object();
    for (const auto& [k, v] : h.fields) f[k] = v;
    return {{"name", h.name}, {"at", h.at}, {"fields", f}, {"picture", h.picture}};
}

bool save_heads_own(const Paths& p, const std::string& scope, const HeadsOwn& h, std::string* err) {
    json j = json::object();
    j["version"] = 1;
    j["scope"] = scope;
    j["note"] = "Turbo: each player's / manager's OWN head fields, stored once before Turbo's first head change (the first original "
                "wins); \"His own head\" writes them back";
    j["players"] = json::object();
    j["managers"] = json::object();
    for (const auto& [id, o] : h.players) j["players"][std::to_string(id)] = own_to_json(o);
    for (const auto& [id, o] : h.managers) j["managers"][std::to_string(id)] = own_to_json(o);
    return write_atomic(heads_own_file(p, scope), j.dump(1), err);
}

std::string local_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &tm);
    return b;
}
}  // namespace

bool load_heads_own(const Paths& p, const std::string& scope, HeadsOwn& out, std::string* err) {
    out = HeadsOwn{};
    if (!is_head_scope(scope)) {
        if (err) *err = "no career or squad scope for his own head";
        return false;
    }
    const fs::path f = heads_own_file(p, scope);
    if (!regular(f)) return true;
    std::string text;
    if (!read_bytes(f, text)) {
        if (err) *err = "cannot read " + f.string();
        return false;
    }
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = f.string() + " is not readable (left as it is: no head is changed until it is looked at)";
        return false;
    }
    for (const char* part : {"players", "managers"}) {
        if (!j.contains(part)) continue;
        if (!j[part].is_object()) {
            if (err) *err = f.string() + ": \"" + part + "\" is not readable";
            return false;
        }
        for (auto it = j[part].begin(); it != j[part].end(); ++it) {
            char* end = nullptr;
            const long long id = std::strtoll(it.key().c_str(), &end, 10);
            const json& v = it.value();
            if (!end || *end || id <= 0 || !v.is_object() || !v.contains("fields") || !v["fields"].is_object()) {
                if (err) *err = f.string() + ": the entry \"" + it.key() + "\" is not readable";
                return false;
            }
            OwnHead h;
            h.name = v.value("name", std::string());
            h.at = v.value("at", std::string());
            h.picture = v.value("picture", false);
            for (auto fi = v["fields"].begin(); fi != v["fields"].end(); ++fi)
                if (fi.value().is_number_integer()) h.fields[fi.key()] = fi.value().get<int64_t>();
            (std::string(part) == "players" ? out.players : out.managers)[id] = h;
        }
    }
    return true;
}

bool record_own_head(const Paths& p, const std::string& scope, bool manager, int64_t id, const std::string& name,
                     const std::map<std::string, int64_t>& fields, std::string* err, bool* created) {
    if (created) *created = false;
    if (id <= 0 || fields.empty()) {
        if (err) *err = "nothing to store for his own head";
        return false;
    }
    HeadsOwn h;
    if (!load_heads_own(p, scope, h, err)) return false;
    auto& m = manager ? h.managers : h.players;
    if (m.count(id)) return true;  // the first original wins
    OwnHead o;
    o.name = name;
    o.at = local_now();
    o.fields = fields;
    m[id] = o;
    if (!save_heads_own(p, scope, h, err)) return false;
    if (created) *created = true;
    return true;
}

bool find_own_head(const Paths& p, const std::string& scope, bool manager, int64_t id, OwnHead& out) {
    HeadsOwn h;
    if (!load_heads_own(p, scope, h, nullptr)) return false;
    const auto& m = manager ? h.managers : h.players;
    auto it = m.find(id);
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

bool set_own_picture(const Paths& p, const std::string& scope, bool manager, int64_t id, bool picture, std::string* err) {
    HeadsOwn h;
    if (!load_heads_own(p, scope, h, err)) return false;
    auto& m = manager ? h.managers : h.players;
    auto it = m.find(id);
    if (it == m.end() || it->second.picture == picture) return true;
    it->second.picture = picture;
    return save_heads_own(p, scope, h, err);
}

bool drop_own_head(const Paths& p, const std::string& scope, bool manager, int64_t id, std::string* err) {
    HeadsOwn h;
    if (!load_heads_own(p, scope, h, err)) return false;
    auto& m = manager ? h.managers : h.players;
    if (!m.erase(id)) return true;
    return save_heads_own(p, scope, h, err);
}

}  // namespace career_faces
}  // namespace turbo
