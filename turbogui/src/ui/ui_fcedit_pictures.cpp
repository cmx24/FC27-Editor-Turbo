#include "ui_fcedit_pictures.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <vector>

#include "app.h"

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

std::string Index::picture(const std::string& field, int64_t id) const {
    const auto f = fields.find(field);
    if (f == fields.end()) return {};
    const auto p = f->second.find(id);
    return p == f->second.end() ? std::string() : p->second;
}

Index build_index(const fs::path& root) {
    const auto t0 = std::chrono::steady_clock::now();
    Index ix;
    ix.root = root.u8string();
    const fs::path assets = root / "assets";
    std::error_code ec;
    ix.found = !root.empty() && fs::is_directory(assets, ec);
    if (ix.found) {
        // the folders in resolution order: assets\27\<field>, 26, 25, 24, 17, then assets\<field>
        std::vector<fs::path> levels;
        for (const char* v : kVersions) levels.push_back(assets / v);
        levels.push_back(assets);
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
    ix.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return ix;
}

fs::path folder(App& app) {
    State& s = st();
    if (s.overridden) return s.override_dir.empty() ? fs::path() : fs::u8path(s.override_dir);
    return app.prefs.fc_editor_dir.empty() ? fs::path(kDefaultDir) : fs::u8path(app.prefs.fc_editor_dir);
}

const Index& index(App& app) {
    State& s = st();
    const fs::path dir = folder(app);
    if (!s.built || s.built_for != dir.u8string()) {
        s.index = build_index(dir);
        s.built = true;
        s.built_for = dir.u8string();
        char buf[160];
        std::snprintf(buf, sizeof(buf), "fc editor pictures: %zu fields, %zu pictures, %.0f ms", s.index.fields.size(), s.index.pictures, s.index.ms);
        const std::string line = std::string(buf) + (s.index.found ? "" : " (no assets folder in " + s.index.root + ")");
        app.log(line);
        if (app.log_hook) app.log_hook(line);
    }
    return s.index;
}

std::string picture(App& app, const std::string& field, int64_t id) { return index(app).picture(field, id); }

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
