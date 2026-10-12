// FC 27 LE Turbo GUI - game asset catalog (asset_catalog.h).
// Loads catalog_<buildkey>.json (schema v1, docs/asset-catalog.md) into sorted id vectors and hash maps once, then only
// reads them: proofs, preview paths, head extras, kits, callname banks, names, the write gate, "hidden" against the
// loaded save (DbUse), the Lua gate file and the write log. Platform independent (no ImGui, no Windows calls).
#include "asset_catalog.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"

namespace turbo::assets {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
constexpr const char* kFamilyKeys[kFamilyCount] = {
    "head3d", "miniface", "youthface", "staffface", "legendface", "hair", "facialhair", "boots", "gkglove",
    "tattoo", "accessory", "eyebrow", "skinsurface", "crest", "kit", "ball", "callnames",
};
bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !ec;
}
}  // namespace

const char* family_key(Family f) {
    const size_t i = static_cast<size_t>(f);
    return i < kFamilyCount ? kFamilyKeys[i] : "";
}

bool family_from_key(const std::string& key, Family& out) {
    for (size_t i = 0; i < kFamilyCount; ++i)
        if (key == kFamilyKeys[i]) {
            out = static_cast<Family>(i);
            return true;
        }
    return false;
}

bool family_has_models(Family f) {
    switch (f) {
        case Family::Hair:
        case Family::Facialhair:
        case Family::Boots:
        case Family::Gkglove:
        case Family::Tattoo:
        case Family::Accessory:
        case Family::Ball:
            return true;
        default:
            return false;
    }
}

const char* call_kind_key(CallKind k) {
    switch (k) {
        case CallKind::Generic: return "generic";
        case CallKind::Simple: return "simple";
        case CallKind::Link: return "link";
        case CallKind::High: return "high";
        default: return "";
    }
}

std::string name_source_label(const std::string& source) {
    if (source.empty()) return "";
    if (source.rfind("master:", 0) == 0) return "name from: " + source.substr(7) + " master";
    if (source == "workbook") return "name from: FC 26 workbook";
    if (source == "extra_names") return "name from: extra player names";
    if (source == "slug") return "name from: game file name";
    if (source == "kit_slug") return "name from: kit folder name";
    if (source == "managers") return "name from: managers list";
    return "name from: " + source;
}

std::string fallback_name(Family f, int64_t id) {
    const char* what = "Asset";
    switch (f) {
        case Family::Head3d: what = "Head"; break;
        case Family::Miniface: what = "Miniface"; break;
        case Family::Youthface: what = "Youth face"; break;
        case Family::Staffface: what = "Staff face"; break;
        case Family::Legendface: what = "Legend picture"; break;
        case Family::Hair: what = "Hair"; break;
        case Family::Facialhair: what = "Facial hair"; break;
        case Family::Boots: what = "Boots"; break;
        case Family::Gkglove: what = "GK gloves"; break;
        case Family::Tattoo: what = "Tattoo"; break;
        case Family::Accessory: what = "Accessory"; break;
        case Family::Eyebrow: what = "Eyebrows"; break;
        case Family::Skinsurface: what = "Skin surface"; break;
        case Family::Crest:
        case Family::Kit: what = "Team"; break;
        case Family::Ball: what = "Ball"; break;
        case Family::Callnames: what = "Callname"; break;
        default: break;
    }
    return std::string(what) + " #" + std::to_string(id);
}

const char* state_name(State s) {
    switch (s) {
        case State::NotLoaded: return "not loaded";
        case State::Off: return "off";
        case State::Current: return "current";
        case State::Stale: return "stale";
        case State::Error: return "error";
    }
    return "";
}

Switches read_switches(const fs::path& turbo_output) {
    Switches s;
    s.catalog_off = file_exists(catalog_off_path(turbo_output));
    s.hidden_off = file_exists(hidden_off_path(turbo_output));
    return s;
}

fs::path catalog_dir(const fs::path& turbo_output) { return turbo_output / "asset_catalog"; }
fs::path catalog_file(const fs::path& turbo_output, const std::string& build_key) {
    return catalog_dir(turbo_output) / ("catalog_" + build_key + ".json");
}
fs::path catalog_off_path(const fs::path& turbo_output) { return turbo_output / "asset_catalog_off.txt"; }
fs::path hidden_off_path(const fs::path& turbo_output) { return turbo_output / "asset_hidden_off.txt"; }
fs::path lua_gate_path(const fs::path& turbo_output) { return catalog_dir(turbo_output) / "lua_gate.txt"; }
fs::path write_log_path(const fs::path& turbo_output) { return turbo_output / "asset_writes.log"; }

std::string rebuild_command() {
    return "re_venv\\Scripts\\python.exe -I turbo\\tools\\asset_catalog\\build_asset_catalog.py";
}


namespace {
// Local time as ISO 8601 with the UTC offset ("2026-10-12T10:03:11-04:00")
std::string iso_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[40];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tmv) == 0) return std::to_string(static_cast<long long>(t));
    std::string s = buf;
    if (s.size() >= 5 && (s[s.size() - 5] == '+' || s[s.size() - 5] == '-')) s.insert(s.size() - 2, ":");  // -0400 -> -04:00
    return s;
}
// One log column: no tabs or line breaks inside
std::string log_cell(const std::string& v) {
    std::string s = v;
    for (char& c : s)
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return s;
}
}  // namespace

bool append_write_log(const fs::path& turbo_output, const WriteLogEntry& e, std::string* err) {
    std::error_code ec;
    fs::create_directories(turbo_output, ec);
    std::ofstream f(write_log_path(turbo_output), std::ios::binary | std::ios::app);
    if (!f) {
        if (err) *err = "cannot open " + write_log_path(turbo_output).u8string();
        return false;
    }
    f << iso_now() << '\t' << log_cell(e.target) << '\t' << log_cell(e.field) << '\t' << log_cell(e.old_value) << '\t'
      << log_cell(e.new_value) << '\t' << log_cell(e.catalog_key) << '\n';
    f.flush();
    if (!f) {
        if (err) *err = "cannot write " + write_log_path(turbo_output).u8string();
        return false;
    }
    return true;
}

std::string exe_build_key(const fs::path& exe) {
    std::ifstream f(exe, std::ios::binary);
    if (!f) return "";
    unsigned char h[4096] = {};
    f.read(reinterpret_cast<char*>(h), sizeof(h));
    const size_t n = static_cast<size_t>(f.gcount());
    auto u16 = [&](size_t o) { return static_cast<uint32_t>(h[o] | (h[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(h[o] | (h[o + 1] << 8) | (h[o + 2] << 16) | (static_cast<uint32_t>(h[o + 3]) << 24)); };
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return "";
    const size_t pe = u32(0x3C);
    const size_t opt = pe + 24;  // IMAGE_NT_HEADERS: signature 4 + IMAGE_FILE_HEADER 20
    if (pe < 0x40 || opt + 60 > n || std::memcmp(h + pe, "PE\0\0", 4) != 0) return "";
    const uint32_t magic = u16(opt);
    if (magic != 0x10B && magic != 0x20B) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%08X-%08X", static_cast<unsigned>(u32(pe + 8)), static_cast<unsigned>(u32(opt + 56)));
    return buf;
}

std::string dir_signature(const fs::path& turbo_output) {
    std::vector<std::string> parts;
    std::error_code ec;
    for (fs::directory_iterator it(catalog_dir(turbo_output), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        if (name.size() < 13 || name.rfind("catalog_", 0) != 0 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        std::error_code e2;
        const auto size = it->file_size(e2);
        const auto when = it->last_write_time(e2).time_since_epoch().count();
        parts.push_back(name + ":" + std::to_string(e2 ? 0 : size) + ":" + std::to_string(static_cast<long long>(when)));
    }
    std::sort(parts.begin(), parts.end());
    const Switches sw = read_switches(turbo_output);
    std::string s = std::string("off=") + (sw.catalog_off ? "1" : "0") + ";hidden_off=" + (sw.hidden_off ? "1" : "0");
    for (const auto& p : parts) s += ";" + p;
    return s;
}

const char* scope_label(Scope s) {
    switch (s) {
        case Scope::All: return "All";
        case Scope::InSave: return "In this save";
        case Scope::Hidden: return "Hidden";
    }
    return "";
}

bool scope_shows(Scope s, bool hidden) {
    switch (s) {
        case Scope::All: return true;
        case Scope::InSave: return !hidden;
        case Scope::Hidden: return hidden;
    }
    return true;
}


// ---------------------------------------------------------------- Catalog
namespace {
constexpr size_t kKinds = static_cast<size_t>(CallKind::Count);

struct Bank {
    std::array<std::vector<int64_t>, kKinds> kinds;  // sorted per kind
    std::vector<int64_t> all;                        // sorted union of the kinds
};
struct KitTeam {
    std::string slug;
    std::vector<std::string> kits;
};

bool sorted_has(const std::vector<int64_t>& v, int64_t id) { return std::binary_search(v.begin(), v.end(), id); }
void sort_unique(std::vector<int64_t>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}
std::vector<int64_t> merged(const std::vector<int64_t>& a, const std::vector<int64_t>& b) {
    std::vector<int64_t> out;
    out.reserve(a.size() + b.size());
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

// Parse failures carry the JSON path ("families.hair.model[3]: not an integer id")
struct Bad {
    std::string what;
};
[[noreturn]] void bad(const std::string& what) { throw Bad{what}; }

int64_t id_value(const json& v, const std::string& where) {
    if (v.is_number_unsigned()) {
        const uint64_t u = v.get<uint64_t>();
        if (u > static_cast<uint64_t>(INT64_MAX)) bad(where + ": id out of range");
        return static_cast<int64_t>(u);
    }
    if (!v.is_number_integer()) bad(where + ": not an integer id");
    return v.get<int64_t>();
}
int64_t id_key(const std::string& k, const std::string& where) {
    if (k.empty() || k.size() > 18 || !std::all_of(k.begin(), k.end(), [](char c) { return c >= '0' && c <= '9'; }))
        bad(where + "." + k + ": the key is not a decimal id");
    return std::stoll(k);
}
// An id list; sorted and de-duplicated here, so a hand-edited file still works
std::vector<int64_t> id_list(const json& parent, const char* key, const std::string& where, bool required) {
    std::vector<int64_t> out;
    auto it = parent.find(key);
    if (it == parent.end() || it->is_null()) {
        if (required) bad(where + "." + key + ": missing");
        return out;
    }
    if (!it->is_array()) bad(where + "." + key + ": not a list");
    out.reserve(it->size());
    size_t i = 0;
    for (const auto& v : *it) out.push_back(id_value(v, where + "." + key + "[" + std::to_string(i++) + "]"));
    sort_unique(out);
    return out;
}
const json* object_at(const json& parent, const char* key, const std::string& where) {
    auto it = parent.find(key);
    if (it == parent.end() || !it->is_object()) bad(where + "." + key + ": missing or not an object");
    return &*it;
}
std::string string_at(const json& parent, const char* key) {
    auto it = parent.find(key);
    if (it == parent.end() || !it->is_string()) bad(std::string(key) + ": missing or not a string");
    return it->get<std::string>();
}
}  // namespace

struct Catalog::Impl {
    Status status;
    std::array<std::vector<int64_t>, kFamilyCount> ids;     // every id with any proof, sorted
    std::array<std::vector<int64_t>, kFamilyCount> models;  // gear "model" lists (sorted)
    std::array<std::unordered_map<int64_t, std::string>, kFamilyCount> paths;  // gear "preview", crest "paths"
    std::unordered_map<int64_t, std::vector<int>> head_vars;
    std::unordered_map<int64_t, std::string> head_toc;
    std::vector<int64_t> head_generic;
    std::unordered_map<int64_t, KitTeam> kit_teams;
    std::map<std::string, std::map<std::string, Bank>> banks;  // language -> commentary team -> recordings
    std::vector<int64_t> generic_calls;                       // generic recordings of every bank (sorted)
    std::array<std::unordered_map<int64_t, Name>, 3> names;   // NameKind order

    void clear_data() {
        for (auto& v : ids) v.clear();
        for (auto& v : models) v.clear();
        for (auto& m : paths) m.clear();
        head_vars.clear();
        head_toc.clear();
        head_generic.clear();
        kit_teams.clear();
        banks.clear();
        generic_calls.clear();
        for (auto& m : names) m.clear();
    }

    void parse(const json& j);
};

void Catalog::Impl::parse(const json& j) {
    if (!j.is_object()) bad("the file is not a JSON object");
    const std::string schema = j.contains("schema") && j["schema"].is_string() ? j["schema"].get<std::string>() : "";
    if (schema != kSchema) bad("schema \"" + schema + "\" is not " + kSchema + " (update Turbo or rebuild the catalog)");
    status.build_key = string_at(j, "build_key");
    if (status.build_key.empty()) bad("build_key: empty");
    if (j.contains("game_version") && j["game_version"].is_string()) status.game_version = j["game_version"].get<std::string>();
    if (j.contains("built_at") && j["built_at"].is_string()) status.built_at = j["built_at"].get<std::string>();

    const json& fams = *object_at(j, "families", "");
    std::vector<std::string> unknown;
    for (auto it = fams.begin(); it != fams.end(); ++it) {
        Family f;
        if (!family_from_key(it.key(), f)) {
            unknown.push_back(it.key());  // a newer builder's optional family: ignored, not refused
            continue;
        }
        const json& v = it.value();
        const std::string where = "families." + it.key();
        if (!v.is_object()) bad(where + ": not an object");
        const size_t fi = static_cast<size_t>(f);
        if (family_has_models(f)) {
            models[fi] = id_list(v, "model", where, false);
            std::vector<int64_t> prev;
            if (auto p = v.find("preview"); p != v.end() && !p->is_null()) {
                if (!p->is_object()) bad(where + ".preview: not an object");
                paths[fi].reserve(p->size());
                for (auto e = p->begin(); e != p->end(); ++e) {
                    const int64_t id = id_key(e.key(), where + ".preview");
                    if (!e.value().is_string()) bad(where + ".preview." + e.key() + ": not a path");
                    paths[fi][id] = e.value().get<std::string>();
                    prev.push_back(id);
                }
            }
            sort_unique(prev);
            ids[fi] = merged(models[fi], prev);
        } else if (f == Family::Kit) {
            if (auto t = v.find("teams"); t != v.end() && !t->is_null()) {
                if (!t->is_object()) bad(where + ".teams: not an object");
                for (auto e = t->begin(); e != t->end(); ++e) {
                    const int64_t id = id_key(e.key(), where + ".teams");
                    const json& tv = e.value();
                    if (!tv.is_object()) bad(where + ".teams." + e.key() + ": not an object");
                    KitTeam kt;
                    if (auto s = tv.find("slug"); s != tv.end() && s->is_string()) kt.slug = s->get<std::string>();
                    if (auto k = tv.find("kits"); k != tv.end() && k->is_array())
                        for (const auto& x : *k)
                            if (x.is_string()) kt.kits.push_back(x.get<std::string>());
                    kit_teams[id] = std::move(kt);
                    ids[fi].push_back(id);
                }
                sort_unique(ids[fi]);
            }
        } else if (f == Family::Callnames) {
            std::vector<int64_t> all;
            for (auto l = v.begin(); l != v.end(); ++l) {
                const std::string lw = where + "." + l.key();
                if (!l.value().is_object()) bad(lw + ": not an object");
                const json& teams = *object_at(l.value(), "teams", lw);
                for (auto t = teams.begin(); t != teams.end(); ++t) {
                    const std::string tw = lw + ".teams." + t.key();
                    if (!t.value().is_object()) bad(tw + ": not an object");
                    Bank b;
                    for (size_t k = 0; k < kKinds; ++k) {
                        b.kinds[k] = id_list(t.value(), call_kind_key(static_cast<CallKind>(k)), tw, false);
                        b.all = merged(b.all, b.kinds[k]);
                    }
                    generic_calls = merged(generic_calls, b.kinds[static_cast<size_t>(CallKind::Generic)]);
                    all = merged(all, b.all);
                    banks[l.key()][t.key()] = std::move(b);
                }
            }
            ids[fi] = std::move(all);
        } else {
            ids[fi] = id_list(v, "ids", where, true);
            if (f == Family::Crest) {
                if (auto p = v.find("paths"); p != v.end() && !p->is_null()) {
                    if (!p->is_object()) bad(where + ".paths: not an object");
                    for (auto e = p->begin(); e != p->end(); ++e) {
                        if (!e.value().is_string()) bad(where + ".paths." + e.key() + ": not a path");
                        paths[fi][id_key(e.key(), where + ".paths")] = e.value().get<std::string>();
                    }
                }
            }
            if (f == Family::Head3d) {
                if (auto vv = v.find("vars"); vv != v.end() && vv->is_object()) {
                    head_vars.reserve(vv->size());
                    for (auto e = vv->begin(); e != vv->end(); ++e) {
                        const int64_t id = id_key(e.key(), where + ".vars");
                        if (!e.value().is_array()) bad(where + ".vars." + e.key() + ": not a list");
                        std::vector<int> vs;
                        for (const auto& x : e.value()) vs.push_back(static_cast<int>(id_value(x, where + ".vars." + e.key())));
                        std::sort(vs.begin(), vs.end());
                        vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
                        head_vars[id] = std::move(vs);
                    }
                }
                if (auto tv = v.find("toc"); tv != v.end() && tv->is_object()) {
                    head_toc.reserve(tv->size());
                    for (auto e = tv->begin(); e != tv->end(); ++e)
                        if (e.value().is_string()) head_toc[id_key(e.key(), where + ".toc")] = e.value().get<std::string>();
                }
                head_generic = id_list(v, "generic", where, false);
            }
        }
    }
    if (!unknown.empty()) {
        status.note = "ignored unknown families:";
        for (const auto& u : unknown) status.note += " " + u;
    }

    if (auto nm = j.find("names"); nm != j.end() && nm->is_object()) {
        static const char* kTables[3] = {"player", "team", "commentary"};
        for (size_t t = 0; t < 3; ++t) {
            auto tab = nm->find(kTables[t]);
            if (tab == nm->end() || !tab->is_object()) continue;
            names[t].reserve(tab->size());
            for (auto e = tab->begin(); e != tab->end(); ++e) {
                const json& n = e.value();
                if (!n.is_array() || n.size() < 1 || !n[0].is_string()) bad(std::string("names.") + kTables[t] + "." + e.key() + ": not [name, source]");
                Name name;
                name.text = n[0].get<std::string>();
                if (n.size() > 1 && n[1].is_string()) name.source = n[1].get<std::string>();
                if (!name.text.empty()) names[t][id_key(e.key(), std::string("names.") + kTables[t])] = std::move(name);
            }
        }
    }
    for (size_t i = 0; i < kFamilyCount; ++i) status.counts[i] = ids[i].size();
}

Catalog::Catalog() : d_(std::make_unique<Impl>()) {}
Catalog::~Catalog() = default;

bool Catalog::load_file(const fs::path& file, const std::string& running_key, std::string* err) {
    const auto t0 = std::chrono::steady_clock::now();
    const bool hidden_off = d_->status.hidden_off;  // load_dir read the switches just before
    d_->clear_data();
    d_->status = Status{};
    d_->status.file = file.u8string();
    d_->status.running_key = running_key;
    d_->status.hidden_off = hidden_off;
    auto fail = [&](const std::string& why) {
        d_->clear_data();
        const std::string keep_file = d_->status.file, keep_key = d_->status.running_key;
        d_->status = Status{};
        d_->status.state = State::Error;
        d_->status.file = keep_file;
        d_->status.running_key = keep_key;
        d_->status.hidden_off = hidden_off;
        d_->status.error = why;
        if (err) *err = why;
        return false;
    };
    std::string text;
    {
        std::ifstream f(file, std::ios::binary);
        if (!f) return fail("cannot open " + file.u8string());
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    json j = json::parse(text, nullptr, false);
    text.clear();
    text.shrink_to_fit();
    if (j.is_discarded()) return fail("not valid JSON: " + file.filename().u8string());
    try {
        d_->parse(j);
    } catch (const Bad& b) {
        return fail(file.filename().u8string() + ": " + b.what);
    } catch (const std::exception& ex) {
        return fail(file.filename().u8string() + ": " + ex.what());
    }
    d_->status.state = (!running_key.empty() && running_key == d_->status.build_key) ? State::Current : State::Stale;
    d_->status.load_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

bool Catalog::load_dir(const fs::path& turbo_output, const std::string& running_key, std::string* err) {
    d_->clear_data();
    d_->status = Status{};
    d_->status.running_key = running_key;
    const Switches sw = read_switches(turbo_output);
    d_->status.hidden_off = sw.hidden_off;
    if (sw.catalog_off) {
        d_->status.state = State::Off;
        d_->status.file = catalog_off_path(turbo_output).u8string();
        return false;
    }
    std::error_code ec;
    if (!running_key.empty()) {
        const fs::path exact = catalog_file(turbo_output, running_key);
        if (fs::is_regular_file(exact, ec)) return load_file(exact, running_key, err);
    }
    // No catalog for this build: the newest one, as stale (lists work, hidden ids cannot be written)
    fs::path newest;
    fs::file_time_type newest_t{};
    for (fs::directory_iterator it(catalog_dir(turbo_output), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        if (name.size() < 13 || name.rfind("catalog_", 0) != 0 || name.compare(name.size() - 5, 5, ".json") != 0) continue;
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const auto t = it->last_write_time(e2);
        if (e2) continue;
        if (newest.empty() || t > newest_t || (t == newest_t && name > newest.filename().u8string())) {
            newest = it->path();
            newest_t = t;
        }
    }
    if (newest.empty()) {
        d_->status.state = State::NotLoaded;
        d_->status.error = "no catalog in " + catalog_dir(turbo_output).u8string() + ": build one with " + rebuild_command();
        if (err) *err = d_->status.error;
        return false;
    }
    return load_file(newest, running_key, err);
}

const Status& Catalog::status() const { return d_->status; }
State Catalog::state() const { return d_->status.state; }
bool Catalog::loaded() const { return d_->status.state == State::Current || d_->status.state == State::Stale; }
bool Catalog::current() const { return d_->status.state == State::Current; }
bool Catalog::hidden_off() const { return d_->status.hidden_off; }

std::string Catalog::status_line() const {
    const Status& s = d_->status;
    switch (s.state) {
        case State::Off: return "off (asset_catalog_off.txt)";
        case State::NotLoaded: return s.error.empty() ? "not loaded" : "not loaded - " + s.error;
        case State::Error: return "error - " + s.error;
        case State::Stale:
            return "stale - built for " + s.build_key + ", running " + (s.running_key.empty() ? std::string("an unknown build") : s.running_key) +
                   ": rebuild with " + rebuild_command();
        case State::Current: break;
    }
    std::string line = "current - build " + s.build_key;
    if (s.built_at.size() >= 10) line += ", built " + s.built_at.substr(0, 10);
    struct Shown {
        Family f;
        const char* what;
    };
    static const Shown kShown[] = {{Family::Head3d, "heads"},     {Family::Miniface, "minifaces"}, {Family::Youthface, "youth faces"},
                                   {Family::Staffface, "staff faces"}, {Family::Hair, "hair"},    {Family::Facialhair, "facial hair"},
                                   {Family::Boots, "boots"},      {Family::Gkglove, "GK gloves"}, {Family::Tattoo, "tattoos"},
                                   {Family::Crest, "crests"},     {Family::Callnames, "callnames"}};
    for (const auto& sh : kShown) line += ", " + std::to_string(s.counts[static_cast<size_t>(sh.f)]) + " " + sh.what;
    if (s.hidden_off) line += "; hidden ids off (asset_hidden_off.txt)";
    return line;
}

uint8_t Catalog::proof(Family f, int64_t id) const {
    const size_t fi = static_cast<size_t>(f);
    if (fi >= kFamilyCount || !loaded()) return kProofNone;
    if (family_has_models(f)) {
        uint8_t p = kProofNone;
        if (sorted_has(d_->models[fi], id)) p |= kProofModel;
        if (d_->paths[fi].count(id)) p |= kProofPreview;
        return p;
    }
    if (!sorted_has(d_->ids[fi], id)) return kProofNone;
    switch (f) {
        case Family::Head3d:
        case Family::Eyebrow:
        case Family::Skinsurface:
        case Family::Kit: return kProofModel;
        case Family::Callnames: return kProofRecording;
        default: return kProofPreview;  // minifaces, youth / staff faces, legend pictures, crests
    }
}
bool Catalog::has(Family f, int64_t id) const { return proof(f, id) != kProofNone; }
bool Catalog::has_model(Family f, int64_t id) const { return (proof(f, id) & kProofModel) != 0; }

std::vector<int64_t> Catalog::ids(Family f) const {
    const size_t fi = static_cast<size_t>(f);
    if (fi >= kFamilyCount || !loaded()) return {};
    return d_->ids[fi];
}

std::string Catalog::preview_path(Family f, int64_t id) const {
    const size_t fi = static_cast<size_t>(f);
    if (fi >= kFamilyCount || !loaded()) return "";
    if (auto it = d_->paths[fi].find(id); it != d_->paths[fi].end()) return it->second;
    if (!sorted_has(d_->ids[fi], id)) return "";
    // the picture families without a path table follow one fixed pattern (docs/asset-catalog.md, Families)
    switch (f) {
        case Family::Miniface: return "data/ui/imgAssets/heads/p" + std::to_string(id) + ".dds";
        case Family::Youthface: return "data/ui/imgAssets/youthheads/p" + std::to_string(id) + ".dds";
        case Family::Staffface: return "data/ui/imgAssets/heads_staff/heads_staff_" + std::to_string(id) + ".dds";
        default: return "";
    }
}

std::vector<int> Catalog::head_vars(int64_t headassetid) const {
    if (!loaded()) return {};
    auto it = d_->head_vars.find(headassetid);
    return it == d_->head_vars.end() ? std::vector<int>{} : it->second;
}
std::string Catalog::head_toc(int64_t headassetid) const {
    if (!loaded()) return "";
    auto it = d_->head_toc.find(headassetid);
    return it == d_->head_toc.end() ? std::string() : it->second;
}
bool Catalog::generic_head(int64_t headassetid) const { return loaded() && sorted_has(d_->head_generic, headassetid); }

std::string Catalog::kit_slug(int64_t teamid) const {
    if (!loaded()) return "";
    auto it = d_->kit_teams.find(teamid);
    return it == d_->kit_teams.end() ? std::string() : it->second.slug;
}
std::vector<std::string> Catalog::kits(int64_t teamid) const {
    if (!loaded()) return {};
    auto it = d_->kit_teams.find(teamid);
    return it == d_->kit_teams.end() ? std::vector<std::string>{} : it->second.kits;
}

std::vector<std::string> Catalog::callname_languages() const {
    std::vector<std::string> out;
    if (!loaded()) return out;
    for (const auto& [lang, teams] : d_->banks) out.push_back(lang);
    return out;
}
std::vector<std::string> Catalog::callname_teams(const std::string& lang) const {
    std::vector<std::string> out;
    if (!loaded()) return out;
    if (auto it = d_->banks.find(lang); it != d_->banks.end())
        for (const auto& [team, b] : it->second) out.push_back(team);
    return out;
}
std::vector<int64_t> Catalog::callname_ids(const std::string& lang, const std::string& team, CallKind k) const {
    const size_t ki = static_cast<size_t>(k);
    if (!loaded() || ki >= kKinds) return {};
    auto l = d_->banks.find(lang);
    if (l == d_->banks.end()) return {};
    auto t = l->second.find(team);
    return t == l->second.end() ? std::vector<int64_t>{} : t->second.kinds[ki];
}
bool Catalog::has_recording(const std::string& lang, const std::string& team, int64_t id) const {
    if (!loaded()) return false;
    auto l = d_->banks.find(lang);
    if (l == d_->banks.end()) return false;
    auto t = l->second.find(team);
    return t != l->second.end() && sorted_has(t->second.all, id);
}

Name Catalog::name(NameKind k, int64_t id) const {
    const size_t ki = static_cast<size_t>(k);
    if (!loaded() || ki >= 3) return {};
    auto it = d_->names[ki].find(id);
    return it == d_->names[ki].end() ? Name{} : it->second;
}

bool Catalog::writable(Family f, int64_t id, std::string* why) const {
    auto no = [why](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    const bool gear_code = f == Family::Hair || f == Family::Facialhair || f == Family::Boots || f == Family::Gkglove ||
                           f == Family::Tattoo || f == Family::Accessory;
    if (gear_code && id == 0) return true;  // 0 = none: nothing to prove
    switch (d_->status.state) {
        case State::Off: return no("the asset catalog is switched off (asset_catalog_off.txt)");
        case State::NotLoaded: return no("the asset catalog is not loaded" + (d_->status.error.empty() ? std::string() : ": " + d_->status.error));
        case State::Error: return no("the asset catalog could not be read: " + d_->status.error);
        case State::Stale:
            if (f == Family::Accessory) return true;  // no gate for accessories yet (mapping unknown)
            return no("the asset catalog was built for game build " + d_->status.build_key + ", the game is " +
                      (d_->status.running_key.empty() ? std::string("an unknown build") : d_->status.running_key) + ": rebuild it with " +
                      rebuild_command());
        case State::Current: break;
    }
    if (f == Family::Accessory) return true;
    const uint8_t p = proof(f, id);
    const std::string key = " (catalog " + d_->status.build_key + ")";
    if (p == kProofNone) return no(std::string(fallback_name(f, id)) + " is not in the game files" + key);
    switch (f) {
        case Family::Head3d:
        case Family::Hair:
        case Family::Facialhair:
        case Family::Boots:
        case Family::Gkglove:
        case Family::Tattoo:
        case Family::Ball:
            if (!(p & kProofModel)) return no(fallback_name(f, id) + ": the game has only a preview picture, no 3D model" + key);
            return true;
        default: return true;  // listed cranium presets, pictures in the manifests, kits, recordings
    }
}

bool Catalog::hidden(Family f, int64_t id, const DbUse& use) const {
    const size_t fi = static_cast<size_t>(f);
    if (fi >= kFamilyCount || !loaded() || d_->status.hidden_off) return false;
    return has(f, id) && use.used[fi].count(id) == 0;
}
bool Catalog::offered(Family f, int64_t id, const DbUse& use) const {
    const size_t fi = static_cast<size_t>(f);
    if (fi >= kFamilyCount) return false;
    if (use.used[fi].count(id)) return true;
    return loaded() && !d_->status.hidden_off && has(f, id);
}
bool Catalog::hidden_generic_callname(int64_t commentaryid, const DbUse& use) const {
    if (!loaded() || d_->status.hidden_off) return false;
    return sorted_has(d_->generic_calls, commentaryid) && use.commentary_ids.count(commentaryid) == 0;
}
bool Catalog::not_in_tattoo_table(int64_t tattooid, const DbUse& use) const {
    if (!loaded() || !use.has_tattoo_table || tattooid == 0) return false;
    return has(Family::Tattoo, tattooid) && use.tattoo_table.count(tattooid) == 0;
}

std::string Catalog::lua_gate_text() const {
    if (!current()) return "";
    const auto& heads = d_->ids[static_cast<size_t>(Family::Head3d)];
    std::string s;
    s.reserve(48 + heads.size() * 14);
    s += "#turbo-asset-gate " + std::to_string(kLuaGateVersion) + "\n";
    s += "#build " + d_->status.build_key + "\n";
    for (int64_t id : heads) {
        s += "head3d ";
        s += std::to_string(id);
        s += '\n';
    }
    return s;
}

bool Catalog::sync_lua_gate(const fs::path& turbo_output, std::string* err) const {
    const fs::path gate = lua_gate_path(turbo_output);
    std::error_code ec;
    const std::string text = lua_gate_text();
    if (text.empty()) {  // no current catalog: the gate file must not exist (Lua falls back to today's rules)
        fs::remove(gate, ec);
        if (ec && err) *err = ec.message();
        return !ec;
    }
    fs::create_directories(gate.parent_path(), ec);
    const fs::path tmp = gate.parent_path() / "lua_gate.txt.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.u8string();
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) {
            if (err) *err = "cannot write " + tmp.u8string();
            return false;
        }
    }
    fs::rename(tmp, gate, ec);  // replaces the old gate in one step (Lua never reads half a file)
    if (ec) {
        fs::remove(tmp, ec);
        if (err) *err = "cannot replace " + gate.u8string();
        return false;
    }
    return true;
}

}  // namespace turbo::assets
