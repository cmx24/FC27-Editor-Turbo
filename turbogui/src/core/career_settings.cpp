// FC 27 LE Turbo GUI - career settings unlock (see career_settings.h)
#include "career_settings.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#include "legacy.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

namespace turbo {
namespace csu {

const std::vector<std::string>& keep_locked() {
    static const std::vector<std::string> k = {"CAREER_COMPETITION", "CAREER_CURRENCY", "CAREER_DEEPER_SIMULATION",
                                               "CAREER_FINANCIAL_TAKEOVER", "CAREER_YOUTH_ACADEMY"};
    return k;
}

std::string content_hash(const std::string& bytes) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char b[20];
    std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(h));
    return b;
}

static std::string strip_bom(const std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF)
        return s.substr(3);
    return s;
}

static bool parse(const std::string& text, ojson& out, std::string& err) {
    try {
        out = ojson::parse(strip_bom(text));
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

static bool is_keep(const std::string& name) {
    const auto& k = keep_locked();
    return std::find(k.begin(), k.end(), name) != k.end();
}

// Removes "alwaysLocked": true below `n` except on the keep-list
static void unlock_walk(ojson& n, RecipeReport& rep) {
    if (n.is_object()) {
        auto al = n.find("alwaysLocked");
        if (al != n.end() && al->is_boolean() && al->get<bool>()) {
            auto nm = n.find("name");
            const std::string name = (nm != n.end() && nm->is_string()) ? nm->get<std::string>() : std::string();
            if (!name.empty() && is_keep(name)) {
                rep.kept_locked.push_back(name);
            } else if (!name.empty()) {
                n.erase("alwaysLocked");
                rep.unlocked.push_back(name);
            }
        }
        for (auto& kv : n.items())
            if (kv.value().is_structured()) unlock_walk(kv.value(), rep);
    } else if (n.is_array()) {
        for (auto& x : n) unlock_walk(x, rep);
    }
}

static void count_names(const ojson& n, std::map<std::string, int>& out) {
    if (n.is_object()) {
        auto nm = n.find("name");
        if (nm != n.end() && nm->is_string()) ++out[nm->get<std::string>()];
        for (const auto& kv : n.items())
            if (kv.value().is_structured()) count_names(kv.value(), out);
    } else if (n.is_array()) {
        for (const auto& x : n) count_names(x, out);
    }
}

// First object named `name` that has a "settings" array, anywhere below n
static const ojson* find_category(const ojson& n, const std::string& name) {
    if (n.is_object()) {
        auto nm = n.find("name");
        auto st = n.find("settings");
        if (nm != n.end() && nm->is_string() && nm->get<std::string>() == name && st != n.end() && st->is_array()) return &n;
        for (const auto& kv : n.items())
            if (kv.value().is_structured())
                if (const ojson* f = find_category(kv.value(), name)) return f;
    } else if (n.is_array()) {
        for (const auto& x : n)
            if (const ojson* f = find_category(x, name)) return f;
    }
    return nullptr;
}

static ojson* hub_context(ojson& root) {
    if (!root.is_object()) return nullptr;
    auto c = root.find("contexts");
    if (c == root.end() || !c->is_array()) return nullptr;
    for (auto& ctx : *c)
        if (ctx.is_object() && ctx.value("name", std::string()) == kHubContext) return &ctx;
    return nullptr;
}

bool apply_recipe(const std::string& original, const std::string& setup, const RecipeOptions& opt, std::string& out, RecipeReport& rep) {
    rep = RecipeReport();
    out.clear();
    ojson root;
    std::string err;
    if (!parse(original, root, err)) {
        rep.error = "the career settings file does not parse: " + err;
        return false;
    }
    std::map<std::string, int> names_before;
    count_names(root, names_before);
    ojson* hub = hub_context(root);
    if (!hub) {
        rep.error = std::string("the career settings file has no ") + kHubContext + " context";
        return false;
    }
    auto cats = hub->find("categories");
    if (cats == hub->end() || !cats->is_array() || cats->empty()) {
        rep.error = std::string(kHubContext) + " has no categories";
        return false;
    }
    unlock_walk(*hub, rep);
    if (rep.unlocked.empty()) {
        rep.error = std::string("no locked setting to unlock in ") + kHubContext + " (already unlocked, or not the game's file)";
        return false;
    }
    std::map<std::string, int> names_added;
    if (opt.squad_settings) {
        if (find_category(*hub, kSquadCategory)) {
            rep.squad_note = "the hub already has the Squad settings";
        } else {
            ojson sroot;
            const ojson* squad = nullptr;
            if (setup.empty())
                rep.squad_note = "Career_Setup.json is not available";
            else if (!parse(setup, sroot, err))
                rep.squad_note = "Career_Setup.json does not parse: " + err;
            else if (!(squad = find_category(sroot, kSquadCategory)))
                rep.squad_note = "Career_Setup.json has no CAREER_SQUAD category";
            if (squad) {
                ojson copy = *squad;
                RecipeReport ignore;
                unlock_walk(copy, ignore);
                count_names(copy, names_added);
                // where the setup screen has it: before transfers and scouting, else after training, else last
                size_t at = cats->size();
                for (size_t i = 0; i < cats->size(); ++i) {
                    const std::string n = (*cats)[i].is_object() ? (*cats)[i].value("name", std::string()) : std::string();
                    if (n == "CAREER_TRANSFERS_SCOUTING") {
                        at = i;
                        break;
                    }
                    if (n == "CAREER_TRAINING") at = i + 1;
                }
                cats->insert(cats->begin() + static_cast<std::ptrdiff_t>(at), copy);
                rep.squad_added = true;
            }
        }
    }
    // validation: round trip, the same contexts, every name of the original (plus the squad copy) and no other
    out = root.dump(2);
    ojson back;
    if (!parse(out, back, err) || !hub_context(back)) {
        rep.error = "the result does not parse back: " + err;
        out.clear();
        return false;
    }
    std::map<std::string, int> names_after, expect = names_before;
    count_names(back, names_after);
    for (const auto& kv : names_added) expect[kv.first] += kv.second;
    if (names_after != expect || back["contexts"].size() != root["contexts"].size()) {
        rep.error = "the result does not hold the same settings as the game's file";
        out.clear();
        return false;
    }
    if (out.size() > kMaxOutput) {
        rep.error = "the result is larger than 256 KB";
        out.clear();
        return false;
    }
    rep.ok = true;
    return true;
}

// ---------------------------------------------------------------- Store
static fs::path under(const fs::path& base, const std::string& rel) {
    fs::path p = base;
    size_t start = 0;
    while (start < rel.size()) {
        size_t e = rel.find('/', start);
        if (e == std::string::npos) e = rel.size();
        if (e > start) p /= rel.substr(start, e - start);
        start = e + 1;
    }
    return p;
}

static bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool write_file(const fs::path& p, const std::string& data, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.u8string();
            return false;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) {
            if (err) *err = "writing " + tmp.u8string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + p.u8string();
            return false;
        }
    }
    return true;
}

Store::Store(LegacyImages& legacy, fs::path le_root) : legacy_(legacy), root_(std::move(le_root)) { read_manifest(); }

fs::path Store::dir() const { return root_ / "turbo_output" / "edit_unlock"; }
fs::path Store::original_file(const std::string& path) const { return under(dir() / "original", path); }
fs::path Store::manifest_file() const { return dir() / "career_settings.json"; }

bool Store::read_manifest() {
    std::string text;
    original_hash_.clear();
    setup_hash_.clear();
    written_hash_.clear();
    written_squad_ = false;
    if (!read_file(manifest_file(), text)) return false;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) return false;
    original_hash_ = j.value("original_hash", std::string());
    setup_hash_ = j.value("setup_hash", std::string());
    written_hash_ = j.value("written_hash", std::string());
    written_squad_ = j.value("squad_settings", false);
    return true;
}

bool Store::write_manifest() {
    nlohmann::json j = {{"recipe_version", kRecipeVersion}, {"path", kPath},          {"original_hash", original_hash_},
                        {"setup_path", kSetupPath},         {"setup_hash", setup_hash_}, {"written_hash", written_hash_},
                        {"squad_settings", written_squad_}};
    return write_file(manifest_file(), j.dump(2), nullptr);
}

bool Store::capture(const std::string& path, std::string* why) {
    std::error_code ec;
    const fs::path keep = original_file(path);
    if (fs::is_regular_file(keep, ec)) return true;
    fs::path f;
    const LegacyImages::State st = legacy_.locate(path, &f, /*custom_first=*/false);
    if (st == LegacyImages::State::Waiting) {
        if (why) *why = "waiting for the game's file (advance a day, or run lua\\scripts\\turbo_images.lua in Live Editor)";
        return false;
    }
    if (st != LegacyImages::State::Game) {
        if (why) *why = "the game has no " + path;
        return false;
    }
    std::string bytes;
    if (!read_file(f, bytes) || bytes.empty()) {
        if (why) *why = "the exported " + path + " cannot be read";
        return false;
    }
    const std::string h = content_hash(bytes);
    // once an override exists Live Editor's export may return it: never take Turbo's own file (or any file that sits in
    // mods\legacy) as the game's original
    std::string custom;
    const fs::path cf = legacy_.custom_file(path);
    if (h == written_hash_ || (!cf.empty() && read_file(cf, custom) && content_hash(custom) == h)) {
        if (why) *why = "the exported " + path + " is the override in mods\\legacy, not the game's original";
        return false;
    }
    if (path == kPath) {
        std::string out;
        RecipeReport rep;
        if (!apply_recipe(bytes, std::string(), RecipeOptions(), out, rep)) {
            if (why) *why = "the exported file is not the game's original: " + rep.error;
            return false;
        }
    } else {
        ojson tmp;
        std::string err;
        if (!parse(bytes, tmp, err)) {
            if (why) *why = "the exported " + path + " does not parse: " + err;
            return false;
        }
    }
    std::string err;
    if (!write_file(keep, bytes, &err)) {
        if (why) *why = err;
        return false;
    }
    (path == kPath ? original_hash_ : setup_hash_) = h;
    write_manifest();
    return true;
}

Store::Status Store::status() {
    Status s;
    read_manifest();
    std::string why, why_setup;
    s.original = capture(kPath, &why);
    s.setup_original = capture(kSetupPath, &why_setup);
    if (!s.original) {
        s.waiting = why.rfind("waiting", 0) == 0;
        s.missing = why.rfind("the game has no", 0) == 0;
    }
    std::string bytes;
    const fs::path cf = legacy_.custom_file(kPath);
    if (!cf.empty() && read_file(cf, bytes)) {
        s.written = !written_hash_.empty() && content_hash(bytes) == written_hash_;
        s.foreign = !s.written;
        s.squad_settings = s.written && written_squad_;
    }
    if (s.foreign)
        s.line = "A career settings file Turbo did not write is in mods\\legacy\\data\\gamesettings: Turbo leaves it alone";
    else if (s.written)
        s.line = std::string("Unlocked (Turbo's file is in mods\\legacy)") + (s.squad_settings ? ", with the Squad settings" : "");
    else if (s.original)
        s.line = "Locked (the game's file). The original is kept in turbo_output\\edit_unlock\\original";
    else
        s.line = "Locked. " + why;
    return s;
}

bool Store::apply(const RecipeOptions& opt, std::string& msg) {
    Status s = status();
    if (s.foreign) {
        msg = s.line + ": remove it first (Turbo does not overwrite another mod's file)";
        return false;
    }
    if (!s.original) {
        msg = "the game's career settings file is not exported yet: " + s.line;
        return false;
    }
    if (opt.squad_settings && !s.setup_original) {
        msg = "the Squad settings need the game's Career_Setup.json, which is not exported yet (advance a day, or run "
              "lua\\scripts\\turbo_images.lua)";
        return false;
    }
    std::string original, setup;
    if (!read_file(original_file(kPath), original)) {
        msg = "the kept original cannot be read";
        return false;
    }
    if (opt.squad_settings) read_file(original_file(kSetupPath), setup);
    std::string out;
    RecipeReport rep;
    if (!apply_recipe(original, setup, opt, out, rep)) {
        msg = rep.error;
        return false;
    }
    if (opt.squad_settings && !rep.squad_added && rep.squad_note.find("already") == std::string::npos) {
        msg = "Squad settings: " + rep.squad_note;
        return false;
    }
    std::string err;
    if (!legacy_.save_custom(kPath, std::vector<uint8_t>(out.begin(), out.end()), &err)) {
        msg = "writing the override failed: " + err;
        return false;
    }
    written_hash_ = content_hash(out);
    written_squad_ = opt.squad_settings && rep.squad_added;
    write_manifest();
    msg = "Career settings unlocked: " + std::to_string(rep.unlocked.size()) + " settings" +
          (rep.squad_added ? " + the Squad settings (edit injuries, edit suspensions, release players)" : "") +
          ". Takes effect the next time the Settings screen opens; restart the game if it does not";
    return true;
}

bool Store::restore(std::string& msg) {
    read_manifest();
    std::string bytes;
    const fs::path cf = legacy_.custom_file(kPath);
    if (cf.empty() || !read_file(cf, bytes)) {
        if (!written_hash_.empty()) {
            written_hash_.clear();
            written_squad_ = false;
            write_manifest();
        }
        msg = "nothing to restore: the game's own career settings are in use";
        return false;
    }
    if (written_hash_.empty() || content_hash(bytes) != written_hash_) {
        msg = "the career settings file in mods\\legacy was not written by Turbo (or was changed since): kept";
        return false;
    }
    std::string err;
    fs::path backup;
    if (!legacy_.remove_custom(kPath, &err, &backup)) {
        msg = "removing the override failed: " + err;
        return false;
    }
    written_hash_.clear();
    written_squad_ = false;
    write_manifest();
    msg = "Career settings locked again (the game's own file); Turbo's file was backed up to " + backup.u8string();
    return true;
}

}  // namespace csu
}  // namespace turbo
