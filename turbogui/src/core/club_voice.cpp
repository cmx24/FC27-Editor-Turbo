// FC 27 LE Turbo GUI - club commentary names: the store and the table entries (see club_voice.h)
#include "club_voice.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"
#include "reapply.h"  // scope_rank

namespace turbo {
namespace clubvoice {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string stamp_now() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &tmv);
    return b;
}

std::string str_of(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool entry_of(const json& o, Entry& e) {
    if (!o.is_object()) return false;
    auto id = o.find("teamid");
    if (id == o.end() || !id->is_number_integer()) return false;
    e.teamid = id->get<int64_t>();
    if (e.teamid <= 0 || e.teamid > INT32_MAX) return false;
    auto v = o.find("voice_of");
    if (v == o.end() || !v->is_number_integer()) return false;
    e.voice_of = v->get<int64_t>();
    if (e.voice_of < 0 || e.voice_of > INT32_MAX) return false;
    // an unknown scope is kept as it is (never in effect, never dropped), as in team_names.json
    if (auto sc = o.find("scope"); sc != o.end() && !sc->is_null())
        e.scope = sc->is_string() ? sc->get<std::string>() : sc->dump(-1, ' ', false, json::error_handler_t::replace);
    e.club = str_of(o, "club");
    e.from = str_of(o, "from");
    e.when = str_of(o, "when");
    return true;
}

bool entry_before(const Entry& a, int64_t id, const std::string& scope) { return a.teamid < id || (a.teamid == id && a.scope < scope); }

}  // namespace

// ---------------------------------------------------------------- store
void Store::upsert(const Entry& e) {
    auto it = std::lower_bound(entries.begin(), entries.end(), e, [](const Entry& a, const Entry& b) { return entry_before(a, b.teamid, b.scope); });
    if (it != entries.end() && it->teamid == e.teamid && it->scope == e.scope) *it = e;
    else entries.insert(it, e);
}

bool Store::forget(int64_t teamid, const std::string& scope) {
    for (auto it = entries.begin(); it != entries.end(); ++it)
        if (it->teamid == teamid && it->scope == scope) {
            entries.erase(it);
            return true;
        }
    return false;
}

const Entry* Store::find(int64_t teamid, const std::string& scope) const {
    for (const Entry& e : entries)
        if (e.teamid == teamid && e.scope == scope) return &e;
    return nullptr;
}

const Entry* Store::find_in_effect(int64_t teamid, const std::string& career) const {
    const Entry* best = nullptr;
    int best_rank = 0;
    for (auto it = std::lower_bound(entries.begin(), entries.end(), teamid, [](const Entry& a, int64_t id) { return a.teamid < id; });
         it != entries.end() && it->teamid == teamid; ++it) {
        const int r = scope_rank(it->scope, career);
        if (r > best_rank) {
            best_rank = r;
            best = &*it;
        }
    }
    return best;
}

fs::path store_path(const fs::path& le_root) { return le_root / "turbo_output" / "callnames" / "club_voice.json"; }

std::string store_json(const Store& s) {
    json list = json::array();
    for (const Entry& e : s.entries) {
        json o = json::object();
        o["teamid"] = e.teamid;
        o["voice_of"] = e.voice_of;
        if (!e.scope.empty()) o["scope"] = e.scope;
        o["club"] = e.club;
        o["from"] = e.from;
        o["when"] = e.when;
        list.push_back(o);
    }
    json j = json::object();
    j["turbo_club_voice"] = 1;
    j["clubs"] = list;
    return j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool parse_store_json(const std::string& text, Store& out, std::string* err) {
    out = Store{};
    size_t bad = 0;
    try {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        auto ver = j.find("turbo_club_voice");
        if (ver == j.end() || !ver->is_number_integer() || ver->get<int64_t>() != 1) {
            if (err) *err = "not a Turbo club commentary file (no \"turbo_club_voice\": 1)";
            return false;
        }
        auto list = j.find("clubs");
        if (list != j.end() && !list->is_null()) {
            if (!list->is_array()) {
                if (err) *err = "\"clubs\" is not a list";
                return false;
            }
            for (const json& o : *list) {
                Entry e;
                if (!entry_of(o, e)) {
                    ++bad;
                    continue;
                }
                out.upsert(e);  // one per club and scope: the last one wins
            }
        }
    } catch (const std::exception& e) {
        out = Store{};
        if (err) *err = std::string("cannot read the file: ") + e.what();
        return false;
    }
    if (bad && err) *err = std::to_string(bad) + (bad == 1 ? " bad entry dropped" : " bad entries dropped");
    return true;
}

bool Store::load(const fs::path& file, std::string* err) {
    entries.clear();
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + file.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    f.close();
    std::string why;
    Store read;
    if (parse_store_json(ss.str(), read, &why)) {
        entries = std::move(read.entries);
        if (!why.empty() && err) *err = file.filename().string() + ": " + why;
        return true;
    }
    // a bad file is set aside (never overwritten by the next save) and the store starts empty
    const std::string stamp = stamp_now();
    fs::path aside = file;
    aside += ".bad-" + stamp;
    for (int i = 2; fs::exists(aside, ec) && i < 1000; ++i) {
        aside = file;
        aside += ".bad-" + stamp + "_" + std::to_string(i);
    }
    ec.clear();
    fs::rename(file, aside, ec);
    if (ec) {
        if (err) *err = file.filename().string() + ": " + why + "; cannot set it aside: " + ec.message();
        return false;
    }
    if (err) *err = file.filename().string() + ": " + why + "; set aside as " + aside.filename().string();
    return true;
}

bool Store::save(const fs::path& file, std::string* err) const {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << store_json(*this);
        f.close();  // a failed flush must not replace the good file
        if (!f) {
            fs::remove(tmp, ec);
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    ec.clear();
    fs::rename(tmp, file, ec);
    if (ec) {
        // Windows: a rename over a file another program holds open can fail; replace in two steps
        ec.clear();
        fs::remove(file, ec);
        ec.clear();
        fs::rename(tmp, file, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + file.string();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- table
std::vector<voice::Table::Team> build_teams(const Store& s, const std::string& career) {
    std::vector<voice::Table::Team> out;
    for (size_t i = 0; i < s.entries.size(); ++i) {
        const Entry& e = s.entries[i];
        if (i > 0 && s.entries[i - 1].teamid == e.teamid) continue;  // entries are sorted: one look per club
        const Entry* best = s.find_in_effect(e.teamid, career);
        if (!best || best->voice_of == best->teamid) continue;
        out.push_back({static_cast<int32_t>(best->teamid), static_cast<int32_t>(best->voice_of)});
    }
    return out;  // sorted by teamid (the store's order)
}

fs::path recorded_path(const fs::path& le_root, const std::string& lang) { return le_root / "turbo" / "callnames" / "clubs" / (lang + ".json"); }

bool load_recorded(const fs::path& file, std::vector<int32_t>& ids, std::string* err) {
    ids.clear();
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + file.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    auto list = j.is_object() ? j.find("ids") : j.end();
    if (j.is_discarded() || !j.is_object() || list == j.end() || !list->is_array()) {
        if (err) *err = file.filename().string() + ": no \"ids\" list";
        return false;
    }
    for (const json& v : *list)
        if (v.is_number_integer() && v.get<int64_t>() > 0 && v.get<int64_t>() <= INT32_MAX) ids.push_back(static_cast<int32_t>(v.get<int64_t>()));
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return true;
}

std::string donor_label(int64_t id, const std::string& row_name, const std::string& fallback_name) {
    if (id == 0) return "No club name";
    if (!row_name.empty()) return row_name;
    if (!fallback_name.empty()) return fallback_name;
    return "Club " + std::to_string(id) + " (not in this database)";
}

bool is_special_id(int64_t id) { return id == 999997 || id == 9999990; }

float donor_list_height(float line_h, float pad_y, int rows) { return static_cast<float>(rows) * line_h + 2.0f * pad_y; }

}  // namespace clubvoice
}  // namespace turbo
