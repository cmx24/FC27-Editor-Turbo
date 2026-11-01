#include "ui_fcedit_pictures.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <system_error>
#include <vector>

#include "app.h"
#include "core/gear_thumbs.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;

namespace turbo {
namespace fcedit {

namespace {

struct Best {  // one entry while the index is built: the path and how far down the resolution order it was found
    std::string path;
    int rank = 0;  // folder order * 2, + 1 for a jpg (a png of the same folder wins)
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Every picture of one field folder into `best` (the first folder in the order wins: a later one fills only the ids still missing)
void list_folder(const fs::path& dir, int order, std::unordered_map<int64_t, Best>& best) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec), end;
    while (!ec && it != end) {
        const fs::path& f = it->path();
        const std::string ext = lower(f.extension().u8string());
        const bool png = ext == ".png", jpg = ext == ".jpg" || ext == ".jpeg";
        if (png || jpg) {
            const int64_t id = id_of_name(f.stem().u8string());
            if (id >= 0) {
                const int rank = order * 2 + (jpg ? 1 : 0);
                auto hit = best.find(id);
                if (hit == best.end()) best.emplace(id, Best{f.u8string(), rank});
                else if (rank < hit->second.rank) hit->second = Best{f.u8string(), rank};
            }
        }
        it.increment(ec);
    }
}

// The field folders under `dir` (every sub-folder whose name is not a game-version number)
std::vector<std::string> field_folders(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::directory_iterator it(dir, ec), end;
    while (!ec && it != end) {
        if (it->is_directory(ec)) {
            const std::string name = it->path().filename().u8string();
            if (!all_digits(name)) out.push_back(name);
        }
        it.increment(ec);
    }
    return out;
}

struct State {
    bool built = false;
    std::string pack_for;      // the pack folder the index was built with (u8)
    uint64_t pack_stamp = 0;   // the pack folders' write times + entry counts when it was built
    double pack_checked = -1e9;
    bool overridden = false;
    std::string override_dir;
    std::string built_for;  // the folder the index was built from (u8): a changed setting rebuilds it
    Index index;
};

State& st() {
    static State s;
    return s;
}

}  // namespace

int64_t id_of_name(const std::string& stem) {
    size_t n = 0;
    while (n < stem.size() && std::isdigit(static_cast<unsigned char>(stem[n]))) ++n;
    if (n == 0 || n > 12) return -1;
    if (n < stem.size() && stem[n] != ' ') return -1;  // "123.png" or "10 Lionel Messi.png", not "12abc.png"
    return std::stoll(stem.substr(0, n));
}

std::string folder_field(const std::string& field) {
    if (field.size() == 14 && field.compare(0, 13, "accessorycode") == 0 && field[13] >= '1' && field[13] <= '4') return "accessorycode";
    if (field == "ballid" || field == "defaultballid" || field == "balltypecode") return "ballid";
    if (field == "stadiumid") return "stadiumid";
    if (field == "stadiummowpattern_code" || field == "mowpattern" || field == "mowpattern_code" || field == "mowpatterncode") return "stadiummowpattern_code";
    if (field == "stadiumgoalnetstyle" || field == "goalnetstyle") return "stadiumgoalnetstyle";
    if (field == "stadiumgoalnetpattern" || field == "goalnetpattern") return "stadiumgoalnetpattern";
    if (field == "pitchcolor") return "pitchcolor";
    if (field == "pitchwear") return "pitchwear";
    if (field == "jerseycollargeometrytype" || field == "jerseycollar" || field == "collar") return "jerseycollargeometrytype";
    if (field == "jerseyfont" || field == "kitfont" || field == "jerseyfonttype") return "jerseyfont";
    if (field == "kitnumber" || field == "kitnumbers") return "kitnumber";
    if (field == "outfitid" || field == "manageroutfit") return "outfitid";
    if (field == "heads_staff" || field == "managerheads") return "heads_staff";
    if (field == "genericmanagerheads") return "genericmanagerheads";
    return field;
}
bool Index::has(const std::string& field) const { return fields.count(folder_field(field)) != 0; }
std::string Index::picture(const std::string& field, int64_t id) const {
    const auto f = fields.find(folder_field(field));
    if (f == fields.end()) return {};
    const auto p = f->second.find(id);
    return p == f->second.end() ? std::string() : p->second;
}
std::string Index::capture_picture(const std::string& field, int64_t id) const {
    const auto f = capture_fields.find(folder_field(field));
    if (f == capture_fields.end()) return {};
    const auto p = f->second.find(id);
    return p == f->second.end() ? std::string() : p->second;
}

std::string Index::realface_picture(const std::string& field, int64_t id) const {
    const auto f = realface_fields.find(folder_field(field));
    if (f == realface_fields.end()) return {};
    const auto p = f->second.find(id);
    return p == f->second.end() ? std::string() : p->second;
}

Index build_index(const fs::path& root, const fs::path& pack) {
    const auto t0 = std::chrono::steady_clock::now();
    Index ix;
    ix.root = root.u8string();
    const fs::path assets = root / "assets";
    std::error_code ec;
    ix.found = !root.empty() && fs::is_directory(assets, ec);
    const bool have_pack = !pack.empty() && fs::is_directory(pack / "assets", ec);
    if (ix.found || have_pack) {
        // the folders in resolution order: Turbo's pack (pictures\assets\<field>), then assets\27\<field>, 26, 25, 24, 17, then assets\<field>
        std::vector<fs::path> levels;
        if (have_pack) levels.push_back(pack / "assets");
        if (ix.found) {
            for (const char* v : kVersions) levels.push_back(assets / v);
            levels.push_back(assets);
        }
        std::map<std::string, std::unordered_map<int64_t, Best>> best;
        for (size_t order = 0; order < levels.size(); ++order)
            for (const std::string& field : field_folders(levels[order])) list_folder(levels[order] / field, static_cast<int>(order), best[field]);
        for (auto& kv : best) {
            if (kv.second.empty()) continue;
            std::unordered_map<int64_t, std::string>& m = ix.fields[kv.first];
            for (auto& e : kv.second) m.emplace(e.first, std::move(e.second.path));
            ix.pictures += m.size();
        }
    }
    if (!pack.empty()) {  // TX-102 R2-P: the in-game captures (raw folder first, then the generator's manifest)
        const fs::path raw = pack / "raw";
        if (fs::is_directory(raw, ec)) {
            for (const std::string& field : field_folders(raw)) {
                std::unordered_map<int64_t, Best> best;
                list_folder(raw / field, 0, best);
                for (auto& e : best) ix.capture_fields[field].emplace(e.first, std::move(e.second.path));
            }
        }
        // 1.0.3 RF: the real-face store (renders on a scanned head): its own list, never a generic capture
        const fs::path rf = pack / "realface";
        if (fs::is_directory(rf, ec)) {
            for (const std::string& field : field_folders(rf)) {
                std::unordered_map<int64_t, Best> best;
                list_folder(rf / field, 0, best);
                for (auto& e : best) ix.realface_fields[field].emplace(e.first, std::move(e.second.path));
            }
        }
        // ... then the legacy test folder of the same renders, <pack>\realface_test\<field>_<id>.png (1.0.2's real_face runs): a store
        // picture of the same id wins
        const fs::path rft = pack / "realface_test";
        if (fs::is_directory(rft, ec)) {
            for (fs::directory_iterator it(rft, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (!it->is_regular_file(e2)) continue;
                const fs::path fp = it->path();
                std::string ext = fp.extension().u8string();
                for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ext != ".png") continue;
                const std::string stem = fp.stem().u8string();
                const size_t us = stem.rfind('_');
                if (us == std::string::npos || us == 0) continue;
                const int64_t id = id_of_name(stem.substr(us + 1));
                if (id < 0) continue;
                ix.realface_fields[folder_field(stem.substr(0, us))].emplace(id, fp.u8string());  // emplace: the store's file wins
            }
        }
        std::ifstream mf(pack / "gear_thumbs" / "manifest.json", std::ios::binary);
        if (mf) {
            const nlohmann::json j = nlohmann::json::parse(mf, nullptr, false);
            // a real_face run's manifest lists renders on scanned heads: never a generic tile (1.0.3 RF)
            if (j.is_object() && !j.value("real_face", false) && j.contains("items") && j["items"].is_array())
                for (const auto& it : j["items"]) {
                    if (!it.is_object() || it.value("state", std::string()) != "done" || !it.contains("id") || !it["id"].is_number_integer()) continue;
                    const std::string field = it.value("field", std::string()), file = it.value("file", std::string());
                    if (field.empty() || file.empty() || file.find("realface") != std::string::npos || !fs::is_regular_file(fs::u8path(file), ec)) continue;
                    ix.capture_fields[folder_field(field)].emplace(it["id"].get<int64_t>(), file);  // the raw copy, when listed, wins
                }
        }
    }
    ix.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return ix;
}

fs::path pack_folder(App& app) { return app.turbo_output() / "pictures"; }

namespace {
// write times + entry counts of pack\assets and its field folders: changes when the generator keeps a picture (checked every 5 s)
uint64_t pack_stamp(const fs::path& pack) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    std::error_code ec;
    const fs::path a = pack / "assets";
    fs::directory_iterator it(a, ec), end;
    while (!ec && it != end) {
        if (it->is_directory(ec)) {
            std::error_code e2;
            mix(static_cast<uint64_t>(fs::last_write_time(it->path(), e2).time_since_epoch().count()));
            mix(std::hash<std::string>{}(it->path().filename().u8string()));
        }
        it.increment(ec);
    }
    return h;
}
}  // namespace

fs::path folder(App& app) {
    State& s = st();
    if (s.overridden) return s.override_dir.empty() ? fs::path() : fs::u8path(s.override_dir);
    return app.prefs.fc_editor_dir.empty() ? fs::path(kDefaultDir) : fs::u8path(app.prefs.fc_editor_dir);
}

const Index& index(App& app) {
    State& s = st();
    const fs::path dir = folder(app);
    const fs::path pack = s.overridden ? fs::path() : pack_folder(app);  // tests: their folder only
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    static uint64_t gear_gen = 0;  // the gear generator kept a picture: list the pack again at once
    if (s.built && !pack.empty() && gear_thumbs::generation() != gear_gen) {
        gear_gen = gear_thumbs::generation();
        s.built = false;
    }
    if (s.built && !pack.empty() && now - s.pack_checked >= 5.0) {  // the generator keeps pictures while the game runs
        s.pack_checked = now;
        if (pack_stamp(pack) != s.pack_stamp) s.built = false;
    }
    if (!s.built || s.built_for != dir.u8string() || s.pack_for != pack.u8string()) {
        s.index = build_index(dir, pack);
        s.built = true;
        s.built_for = dir.u8string();
        s.pack_for = pack.u8string();
        s.pack_stamp = pack.empty() ? 0 : pack_stamp(pack);
        s.pack_checked = now;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "fc editor pictures: %zu fields, %zu pictures, %.0f ms", s.index.fields.size(), s.index.pictures, s.index.ms);
        const std::string line = std::string(buf) + (s.index.found ? "" : " (no assets folder in " + s.index.root + ")");
        app.log(line);
        if (app.log_hook) app.log_hook(line);
    }
    return s.index;
}

std::string picture(App& app, const std::string& field, int64_t id) { return index(app).picture(field, id); }

std::string capture_picture(App& app, const std::string& field, int64_t id) { return index(app).capture_picture(field, id); }

std::string realface_picture(App& app, const std::string& field, int64_t id) { return index(app).realface_picture(field, id); }

bool has_field(App& app, const std::string& field) { return index(app).has(field); }

void refresh() { st().built = false; }

void override_dir(const std::string& dir) {
    State& s = st();
    s.overridden = !dir.empty();
    s.override_dir = dir;
    s.built = false;
}

}  // namespace fcedit
}  // namespace turbo
