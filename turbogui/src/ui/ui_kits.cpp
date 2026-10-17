// FC 27 LE Turbo GUI - Teams > Kits: import a FIFA Kit Creator kit zip (see ui_kits.h)
#include "ui_kits.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>

#include "app.h"
#include "core/tactic_profiles.h"
#include "core/reapply.h"
#include "file_picker.h"
#include "imgui.h"
#include "nlohmann/json.hpp"
#include "table_registry.h"
#include "ui_club_page.h"
#include "ui_identity.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;
using kits::KitPackage;
using kits::Slot;

static const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);
static const ImVec4 kGood(0.55f, 0.95f, 0.55f, 1.0f);

static std::string now_text() {
    char when[32];
    std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::localtime(&t);  // render thread only
    if (!tmv || std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", tmv) == 0) std::snprintf(when, sizeof(when), "%lld", static_cast<long long>(t));
    return when;
}

static std::string clock_text(uint64_t ms) {
    if (!ms) return "-";
    const time_t t = time_t(ms / 1000);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[40];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, int(ms % 1000));
    return b;
}

static std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

static bool read_all(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// write_text_atomic: core/tactic_profiles.h (tmp file + rename)

fs::path kit_import_start_folder(App& app) {
    std::error_code ec;
    const fs::path mods = "C:\\FC_Tools\\My Mods";
    if (fs::is_directory(mods, ec)) return mods;
    return app.bridge.root();
}

// ================================================================ the club's kit rows
uint64_t kit_row(App& app, int64_t teamid, Slot s, int64_t* teamkitid) {
    if (teamkitid) *teamkitid = -1;
    const Table* kt = app.db.table("teamkits");
    if (!kt || s == Slot::Unknown) return 0;
    const Field* tf = kt->field("teamtechid");
    const Field* ty = kt->field("teamkittypetechid");
    const Field* kf = kt->field("teamkitid");
    if (!tf || !ty) return 0;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *kt)) return 0;
    uint64_t best = 0;
    int64_t best_id = -1;
    for (uint32_t idx : snap.valid) {
        if (snap.get_int(idx, *tf) != teamid || snap.get_int(idx, *ty) != kits::slot_type(s)) continue;
        const int64_t id = kf ? snap.get_int(idx, *kf) : -1;
        if (!best || (id >= 0 && (best_id < 0 || id < best_id))) {
            best = snap.addr(idx);
            best_id = id;
        }
    }
    if (teamkitid) *teamkitid = best_id;
    return best;
}

LegacyImages::State minikit_state(App& app, int64_t teamid, Slot s) {
    if (s == Slot::Unknown) return LegacyImages::State::Invalid;
    return app.legacy.locate(kits::minikit_path(teamid, s), nullptr, false);
}

// ================================================================ import record (turbo_output\kits\<id>\<slot>\import.json)
struct ImportRecord {
    int64_t teamid = 0;
    Slot slot = Slot::Unknown;
    int64_t teamkitid = -1;
    std::string team, source, when;
    struct Db {
        std::string field;
        int64_t before = 0, after = 0;
    };
    std::vector<Db> db;
    std::vector<std::pair<std::string, json>> kept_before;  // field -> kept value before the import (null = not kept)
    bool minikit = false;            // a custom minikit was written
    bool minikit_had_custom = false; // there was a custom minikit before the first import
    std::string minikit_backup;      // its copy (kit_backups), when there was one
    const Db* field(const std::string& f) const {
        for (const Db& d : db)
            if (d.field == f) return &d;
        return nullptr;
    }
    const json* kept(const std::string& f) const {
        for (const auto& k : kept_before)
            if (k.first == f) return &k.second;
        return nullptr;
    }
};

static fs::path record_path(App& app, int64_t teamid, Slot s) { return kits::slot_dir(app.bridge.root(), teamid, s) / "import.json"; }

static bool load_record(App& app, int64_t teamid, Slot s, ImportRecord& r) {
    std::string text;
    if (!read_all(record_path(app, teamid, s), text)) return false;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("turbo_kit_import", 0) != 1) return false;
    r = ImportRecord{};
    r.teamid = j.value("teamid", int64_t(0));
    r.slot = kits::slot_of_type(j.value("teamkittypetechid", int64_t(-1)));
    r.teamkitid = j.value("teamkitid", int64_t(-1));
    r.team = j.value("team", std::string());
    r.source = j.value("source", std::string());
    r.when = j.value("when", std::string());
    if (r.teamid != teamid || r.slot != s) return false;
    if (auto it = j.find("db"); it != j.end() && it->is_array())
        for (const json& e : *it)
            if (e.is_object() && e.contains("field") && e["field"].is_string() && kit_kept_field(e["field"].get<std::string>()) &&
                e.contains("before") && e["before"].is_number_integer() && e.contains("after") && e["after"].is_number_integer())
                r.db.push_back({e["field"].get<std::string>(), e["before"].get<int64_t>(), e["after"].get<int64_t>()});
    if (auto it = j.find("kept_before"); it != j.end() && it->is_object())
        for (auto k = it->begin(); k != it->end(); ++k)
            if (kit_kept_field(k.key()) && (k.value().is_null() || k.value().is_number_integer())) r.kept_before.emplace_back(k.key(), k.value());
    if (auto it = j.find("minikit"); it != j.end() && it->is_object()) {
        r.minikit = it->value("written", false);
        r.minikit_had_custom = it->value("had_custom", false);
        r.minikit_backup = it->value("backup", std::string());
    }
    return true;
}

static bool save_record(App& app, const ImportRecord& r, std::string* err) {
    json db = json::array();
    for (const auto& d : r.db) db.push_back({{"field", d.field}, {"before", d.before}, {"after", d.after}});
    json kept = json::object();
    for (const auto& k : r.kept_before) kept[k.first] = k.second;
    json j = {{"turbo_kit_import", 1},
              {"note", "What Teams > Kits replaced when it imported this kit (Undo puts it back): the teamkits values before the "
                       "import, the kept-edit values before it (null = none), the custom minikit before it."},
              {"teamid", r.teamid}, {"team", r.team}, {"slot", kits::slot_folder(r.slot)}, {"teamkittypetechid", kits::slot_type(r.slot)},
              {"teamkitid", r.teamkitid}, {"source", r.source}, {"when", r.when}, {"db", db}, {"kept_before", kept},
              {"minikit", {{"path", kits::minikit_path(r.teamid, r.slot)}, {"written", r.minikit}, {"had_custom", r.minikit_had_custom},
                           {"backup", r.minikit_backup}}}};
    return write_text_atomic(record_path(app, r.teamid, r.slot), j.dump(2, ' ', false, json::error_handler_t::replace) + "\n", err);
}

bool has_kit_import(App& app, int64_t teamid, Slot s) {
    std::error_code ec;
    return s != Slot::Unknown && fs::exists(record_path(app, teamid, s), ec);
}

// The kept-edit value of a field now (null = not kept), for the exact kit entry an edit made here goes to (2.15: the
// row of this career's scope; restore_kept_kit_field puts it back in that same row)
static json kept_now(App& app, int64_t teamid, int64_t type, int64_t kitid, const std::string& field) {
    const KitEdit* k = app.reapply.kit_in(app.reapply.edit_scope(), teamid, type, kitid);
    if (!k) return json();
    auto f = k->fields.find(field);
    return f == k->fields.end() ? json() : json(f->second);
}

// ================================================================ apply / undo
KitApply apply_kit_import(App& app, int64_t teamid, Slot s, const KitPackage& pk) {
    KitApply res;
    if (!pk.ok) {
        res.line = "not applied: " + pk.error;
        return res;
    }
    if (s == Slot::Unknown) {
        res.line = "not applied: pick the kit slot (home, away, goalkeeper, third)";
        return res;
    }
    const TeamRow* tr = app.model.team(teamid);
    if (!tr) {
        res.line = "not applied: club " + std::to_string(teamid) + " is not in the loaded database";
        return res;
    }
    const std::string mpath = kits::minikit_path(teamid, s);
    fs::path game_mini;
    const LegacyImages::State mst = app.legacy.locate(mpath, &game_mini, false);
    if (mst == LegacyImages::State::Waiting) {
        res.line = "not applied yet: the game's minikit (" + mpath + ") is still loading from the game; try again in a moment";
        return res;
    }
    const int64_t type = kits::slot_type(s);
    ImportRecord old;
    const bool had_record = load_record(app, teamid, s, old);
    ImportRecord rec;
    rec.teamid = teamid;
    rec.slot = s;
    rec.team = tr->name;
    rec.source = path_text(pk.source);
    rec.when = now_text();

    // ---- 1. teamkits: the settings.json fields into the slot's row
    const Table* kt = app.db.table("teamkits");
    int64_t kitid = -1;
    const uint64_t row = kt ? kit_row(app, teamid, s, &kitid) : 0;
    rec.teamkitid = kitid;
    // what a field held and was kept as before this import (a second import keeps the first one's: Undo goes back to the game)
    auto note_before = [&](const std::string& field, int64_t before) {
        const ImportRecord::Db* od = had_record ? old.field(field) : nullptr;
        rec.db.push_back({field, od ? od->before : before, 0});
        const json* ok = had_record ? old.kept(field) : nullptr;
        rec.kept_before.emplace_back(field, ok ? *ok : kept_now(app, teamid, type, kitid, field));
    };
    if (!kt) {
        res.skipped.push_back("every setting: FC 27's teamkits table is not readable");
    } else if (!row) {
        res.skipped.push_back("every setting: the club has no " + std::string(kit_type_name(type)) + " kit row (teamkits type " + std::to_string(type) + ")");
    } else {
        // colours: the three channels of a prefix together, through Teams > Colours' writer (validated, kept)
        std::vector<std::string> done;
        for (const auto& f : pk.settings.fields) {
            const std::string& name = f.first;
            if (kit_setting_field(name)) {
                const Field* fd = kt->field(name);
                if (!fd) {
                    res.skipped.push_back(name + ": not in FC 27's teamkits");
                    continue;
                }
                const std::string verr = Database::validate(*fd, Value::of_int(f.second));
                if (!verr.empty()) {
                    res.skipped.push_back(name + " " + std::to_string(f.second) + ": " + verr);
                    continue;
                }
                const int64_t before = app.db.get_int(*kt, row, name, 0);
                const size_t at = rec.db.size();
                note_before(name, before);
                if (!app.edit(*kt, row, *fd, Value::of_int(f.second))) {
                    rec.db.resize(at);
                    rec.kept_before.resize(at);
                    res.skipped.push_back(name + ": not written");
                    continue;
                }
                rec.db.back().after = f.second;
                app.remember_kit_field(teamid, type, kitid, name, f.second, false);
                ++res.fields;
                continue;
            }
            const std::string prefix = name.substr(0, name.size() - 1);
            if (std::find(done.begin(), done.end(), prefix) != done.end()) continue;
            done.push_back(prefix);
            uint8_t rgb[3] = {0, 0, 0};
            int have = 0;
            for (const auto& g : pk.settings.fields) {
                if (g.first.size() != prefix.size() + 1 || g.first.compare(0, prefix.size(), prefix) != 0) continue;
                const char c = g.first.back();
                const int k = c == 'r' ? 0 : c == 'g' ? 1 : c == 'b' ? 2 : -1;
                if (k >= 0) {
                    rgb[k] = uint8_t(g.second);
                    ++have;
                }
            }
            uint8_t before[3];
            if (have != 3 || !read_colour(app, *kt, row, prefix, before)) {
                res.skipped.push_back(prefix + ": not in FC 27's teamkits");
                continue;
            }
            static const char ch[3] = {'r', 'g', 'b'};
            const size_t at = rec.db.size();
            for (int c = 0; c < 3; ++c) note_before(prefix + ch[c], before[c]);
            std::string msg;
            if (!write_colour(app, *kt, row, prefix, rgb, &msg)) {  // keeps it for every career load (remember_kit_colour)
                rec.db.resize(at);
                rec.kept_before.resize(at);
                res.skipped.push_back(prefix + ": " + msg);
                continue;
            }
            for (int c = 0; c < 3; ++c) rec.db[at + size_t(c)].after = rgb[c];
            res.fields += 3;
        }
        app.save_kept_edits();
    }
    // an earlier import of this slot wrote fields this one does not: they stay in the record, so Undo puts them back too
    if (had_record)
        for (const auto& d : old.db)
            if (!rec.field(d.field)) {
                rec.db.push_back(d);
                const json* k = old.kept(d.field);
                rec.kept_before.emplace_back(d.field, k ? *k : json());
            }

    // ---- 2. the minikit, in the game file's own size / format / mips (the crest writer's way)
    rec.minikit = had_record && old.minikit;
    rec.minikit_had_custom = had_record ? old.minikit_had_custom : !app.legacy.custom_file(mpath).empty();
    rec.minikit_backup = had_record ? old.minikit_backup : std::string();
    if (mst == LegacyImages::State::Game) {
        DdsFormat fmt;
        std::string err;
        std::string text;
        const bool readable = read_all(game_mini, text);
        const std::vector<uint8_t> bytes(text.begin(), text.end());
        if (!readable) {
            res.minikit_note = "the game's minikit file cannot be read";
        } else if (!parse_dds_format(bytes, fmt, &err)) {
            res.minikit_note = "the game's minikit: " + err;
        } else if (fmt.w != fmt.h || pk.minikit.w != pk.minikit.h) {
            res.minikit_note = "the game's minikit is " + std::to_string(fmt.w) + " x " + std::to_string(fmt.h) + ", minikit.png " +
                               std::to_string(pk.minikit.w) + " x " + std::to_string(pk.minikit.h) + ": only square pictures are written";
        } else {
            Rgba img = pk.minikit.w == fmt.w ? pk.minikit : frame_image(pk.minikit, fmt.w, Framing());
            std::vector<uint8_t> dds = encode_dds(img, fmt, &err);
            fs::path backup;
            if (dds.empty()) {
                res.minikit_note = "minikit: " + err;
            } else if (!app.legacy.save_custom(mpath, dds, &err, &backup)) {
                res.minikit_note = "minikit not written: " + err;
            } else {
                res.minikit = true;
                rec.minikit = true;
                // the custom minikit there before the FIRST import (Undo brings it back)
                if (!had_record && !backup.empty()) rec.minikit_backup = path_text(backup);
                app.textures.forget(app.legacy.custom_file(mpath));
            }
        }
    } else if (mst == LegacyImages::State::Missing) {
        res.minikit_note = "the game has no minikit for this kit (" + mpath + "): not written";
    } else if (mst != LegacyImages::State::Custom) {
        res.minikit_note = "minikit path refused";
    }

    std::string err;
    const bool saved = save_record(app, rec, &err);
    res.ok = saved;
    res.line = tr->name + " " + kit_type_name(type) + " kit: " + plural(size_t(res.fields), "setting field", "setting fields") + " written" +
               (res.skipped.empty() ? std::string() : " (" + plural(res.skipped.size(), "setting", "settings") + " not: " + res.skipped[0] + ")") +
               "; minikit " + (res.minikit ? std::string("written") : res.minikit_note.empty() ? std::string("unchanged") : res.minikit_note) +
               (saved ? std::string() : "; the import record was not saved (" + err + "): Undo cannot put this back");
    app.log("kit import: " + res.line + " [" + rec.source + "]");
    for (const auto& sk : res.skipped) app.log("kit import: not written: " + sk);
    return res;
}

// ---- the staging jobs (worker thread; never touch App)
struct StageTask {
    fs::path zip, dir;
    kits::StageInfo info;
};
struct StageJob {
    std::vector<StageTask> tasks;
    std::vector<kits::TexSpec> spec;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::atomic<int> finished{0};
    std::mutex m;
    std::vector<kits::StageResult> results;  // m
};
struct LoadJob {
    std::vector<fs::path> files;
    std::vector<kits::TexSpec> spec;
    std::atomic<bool> done{false};
    std::atomic<int> finished{0};
    std::mutex m;
    std::vector<KitPackage> result;  // m
    std::string error;
};

struct KitPanel {
    struct Item {
        KitPackage pk;
        Slot slot = Slot::Unknown;
        std::string status;  // "" = from pk
        uint64_t version = 0;
    };
    std::vector<Item> items;
    std::shared_ptr<LoadJob> load;
    std::shared_ptr<StageJob> stage;
    std::vector<std::pair<int64_t, Slot>> staging;  // slots the running job writes
    std::string line;
    uint64_t versions = 1;
    const std::vector<kits::TexSpec>* spec = nullptr;  // nullptr = fc27_kit_spec()
    FilePicker fp_zip, fp_folder;
    std::string source_label;  // the zip or folder shown
};
static KitPanel g_panel;

static const std::vector<kits::TexSpec>& panel_spec() { return g_panel.spec ? *g_panel.spec : kits::fc27_kit_spec(); }

void kit_panel_spec(const std::vector<kits::TexSpec>* spec) { g_panel.spec = spec; }

static bool staging_slot(int64_t teamid, Slot s) {
    for (const auto& p : g_panel.staging)
        if (p.first == teamid && p.second == s) return true;
    return false;
}

bool undo_kit_import(App& app, int64_t teamid, Slot s, std::string* msg) {
    auto fail = [&](const std::string& why) {
        if (msg) *msg = why;
        return false;
    };
    if (staging_slot(teamid, s)) return fail("the kit's textures are still being converted: try again in a moment");
    ImportRecord r;
    if (!load_record(app, teamid, s, r)) return fail("nothing imported for this kit slot");
    const int64_t type = kits::slot_type(s);
    size_t back = 0, same = 0;
    std::vector<std::string> moved, failed;
    // ---- teamkits values (only where the row still holds what the import wrote)
    if (!r.db.empty()) {
        const Table* kt = app.db.table("teamkits");
        uint64_t row = 0;
        if (kt) {
            int64_t id = -1;
            row = kit_row(app, teamid, s, &id);
            if (row && r.teamkitid >= 0 && id != r.teamkitid) {
                // the row with the recorded kit id, when the slot has several
                Snapshot snap;
                const Field* tf = kt->field("teamtechid");
                const Field* kf = kt->field("teamkitid");
                row = 0;
                if (tf && kf && snap.load(app.db.memory(), *kt))
                    for (uint32_t i : snap.valid)
                        if (snap.get_int(i, *tf) == teamid && snap.get_int(i, *kf) == r.teamkitid) row = snap.addr(i);
            }
        }
        if (!kt || !row) {
            moved.push_back("the kit row is not in this career's teamkits");
        } else {
            for (const auto& d : r.db) {
                const Field* fd = kt->field(d.field);
                if (!fd) continue;
                const int64_t cur = app.db.get_int(*kt, row, d.field, 0);
                if (cur == d.before) {
                    ++same;
                    continue;
                }
                if (cur != d.after) {
                    moved.push_back(d.field + " " + std::to_string(cur));
                    continue;
                }
                if (app.edit(*kt, row, *fd, Value::of_int(d.before))) ++back;
                else failed.push_back(d.field);
            }
        }
    }
    // ---- the kept edits as they were before the import
    for (const auto& k : r.kept_before) {
        int64_t v = 0;
        const bool kept = k.second.is_number_integer();
        if (kept) v = k.second.get<int64_t>();
        app.restore_kept_kit_field(teamid, type, r.teamkitid, k.first, kept ? &v : nullptr, false);
    }
    if (!r.kept_before.empty()) app.save_kept_edits();
    // ---- the minikit as it was
    std::string mini = "minikit unchanged";
    const std::string mpath = kits::minikit_path(teamid, s);
    if (r.minikit) {
        std::string err;
        std::string bytes;
        if (r.minikit_had_custom && !r.minikit_backup.empty() && read_all(text_path(r.minikit_backup), bytes)) {
            if (app.legacy.save_custom(mpath, std::vector<uint8_t>(bytes.begin(), bytes.end()), &err)) mini = "the earlier custom minikit is back";
            else failed.push_back("minikit: " + err);
        } else if (!app.legacy.custom_file(mpath).empty()) {
            if (app.legacy.remove_custom(mpath, &err)) mini = "the game's minikit is back";
            else failed.push_back("minikit: " + err);
        }
        app.textures.forget(app.legacy.custom_file(mpath));
    }
    // ---- the 3D swap of this kit goes first (the table no longer reads the staged files), then the staged textures and the record
    if (app.kit_swap) app.kit_swap->deactivate(int(teamid), int(type));
    std::error_code ec;
    const fs::path dir = kits::slot_dir(app.bridge.root(), teamid, s);
    size_t removed = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && fs::remove(it->path(), e2)) ++removed;
    }
    fs::remove_all(dir / ".staging", ec);
    fs::remove(dir, ec);  // only when empty
    std::string line = r.team + " " + kit_type_name(type) + " kit: import undone: " + plural(back, "field", "fields") + " back" +
                       (same ? " (" + std::to_string(same) + " already)" : std::string()) + "; " + mini + "; staged files removed (" +
                       std::to_string(removed) + ")";
    if (!moved.empty()) {
        line += "; left alone (changed since the import): " + moved[0];
        if (moved.size() > 1) line += " +" + std::to_string(moved.size() - 1);
    }
    if (!failed.empty()) line += "; not written: " + failed[0];
    app.log("kit import: " + line);
    if (msg) *msg = line;
    return failed.empty();
}

// ================================================================ panel
bool kit_panel_busy() {
    return (g_panel.load && !g_panel.load->done.load()) || (g_panel.stage && !g_panel.stage->done.load());
}
size_t kit_panel_count() { return g_panel.items.size(); }
const KitPackage* kit_panel_kit(size_t i) { return i < g_panel.items.size() ? &g_panel.items[i].pk : nullptr; }
Slot kit_panel_slot(size_t i) { return i < g_panel.items.size() ? g_panel.items[i].slot : Slot::Unknown; }
void kit_panel_set_slot(size_t i, Slot s) {
    if (i < g_panel.items.size()) {
        g_panel.items[i].slot = s;
        g_panel.items[i].status.clear();
    }
}
std::string kit_panel_line() { return g_panel.line; }

// "ready", the refusal, "slot taken by <file>"
static std::string item_status(size_t i) {
    const KitPanel::Item& it = g_panel.items[i];
    if (!it.status.empty()) return it.status;
    if (!it.pk.ok) return "refused: " + it.pk.error;
    if (it.slot == Slot::Unknown) return "pick the slot (the file name does not say)";
    for (size_t k = 0; k < i; ++k)
        if (g_panel.items[k].slot == it.slot && g_panel.items[k].pk.ok)
            return std::string("slot taken by ") + g_panel.items[k].pk.source.filename().string() + ": pick another";
    return "ready";
}
std::string kit_panel_status(size_t i) { return i < g_panel.items.size() ? item_status(i) : std::string(); }

bool kit_panel_open(App& app, const fs::path& zip_or_folder) {
    if (kit_panel_busy()) return false;
    std::error_code ec;
    std::vector<fs::path> files;
    if (fs::is_directory(zip_or_folder, ec)) {
        files = kits::team_folder_zips(zip_or_folder);
        if (files.empty()) {
            g_panel.line = "no kit zips in " + path_text(zip_or_folder) + " (nor one folder down)";
            return false;
        }
    } else {
        files.push_back(zip_or_folder);
    }
    auto job = std::make_shared<LoadJob>();
    job->files = files;
    job->spec = panel_spec();
    g_panel.items.clear();
    g_panel.load = job;
    g_panel.source_label = path_text(zip_or_folder);
    g_panel.line = "reading " + plural(files.size(), "kit zip", "kit zips") + "...";
    app.log("kit import: reading " + g_panel.source_label);
    std::thread([job] {
        for (const auto& f : job->files) {
            KitPackage pk = kits::load_kit_zip(f, job->spec);
            {
                std::lock_guard<std::mutex> lk(job->m);
                job->result.push_back(std::move(pk));
            }
            ++job->finished;
        }
        job->done = true;
    }).detach();
    return true;
}

static void poll_jobs(App& app) {
    KitPanel& p = g_panel;
    if (p.load && p.load->done.load()) {
        std::vector<KitPackage> got;
        {
            std::lock_guard<std::mutex> lk(p.load->m);
            got = std::move(p.load->result);
        }
        p.load.reset();
        size_t ok = 0;
        for (auto& pk : got) {
            KitPanel::Item it;
            it.slot = pk.slot;
            it.version = ++p.versions;
            ok += pk.ok;
            app.log("kit import: " + pk.source.filename().string() + ": " + (pk.ok ? std::string("checked, ready") : "refused: " + pk.error));
            it.pk = std::move(pk);
            p.items.push_back(std::move(it));
        }
        p.line = plural(p.items.size(), "kit zip", "kit zips") + " read: " + std::to_string(ok) + " ready" +
                 (ok < p.items.size() ? ", " + std::to_string(p.items.size() - ok) + " refused" : std::string());
    }
    if (p.stage && p.stage->done.load()) {
        std::vector<kits::StageResult> rs;
        {
            std::lock_guard<std::mutex> lk(p.stage->m);
            rs = p.stage->results;
        }
        size_t ok = 0, files = 0;
        uint64_t bytes = 0;
        std::string first_error;
        for (const auto& r : rs) {
            if (r.ok) {
                ++ok;
                files += r.files.size();
                bytes += r.bytes;
            } else if (first_error.empty()) {
                first_error = r.error;
            }
            app.log("kit import: textures " + path_text(r.dir) + ": " + (r.ok ? plural(r.files.size(), "file", "files") + " staged" : "not staged: " + r.error));
        }
        char mb[32];
        std::snprintf(mb, sizeof(mb), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
        p.line = "textures staged for " + plural(ok, "kit", "kits") + " (" + std::to_string(files) + " files, " + mb +
                 ") in turbo_output\\kits; turn on 3D kit on the pitch under Kits imported for this club to use them in matches" +
                 (first_error.empty() ? std::string() : "; not staged: " + first_error);
        if (ok && app.kit_swap && !app.kit_swap->status().slots.empty()) app.kit_swap->rebuild();  // new staged textures of an active kit
        app.notify("Kit import: " + p.line, !first_error.empty());
        p.stage.reset();
        p.staging.clear();
    }
}

// Apply the panel's items `which` to the club: parts 1-2 now, the textures on one worker thread
static void apply_items(App& app, int64_t teamid, const std::vector<size_t>& which) {
    auto job = std::make_shared<StageJob>();
    job->spec = panel_spec();
    std::string summary;
    bool any_error = false;
    const auto cat = app.asset_catalog();
    const std::string slug = cat ? cat->kit_slug(teamid) : std::string();
    for (size_t i : which) {
        KitPanel::Item& it = g_panel.items[i];
        if (item_status(i) != "ready") continue;
        KitApply r = apply_kit_import(app, teamid, it.slot, it.pk);
        summary += (summary.empty() ? "" : " | ") + r.line;
        any_error = any_error || !r.ok;
        if (!r.ok && r.line.rfind("not applied", 0) == 0) {
            it.status = r.line;
            continue;
        }
        it.status = std::string("applied to ") + app.model.team_name(teamid) + " (" + kits::slot_label(it.slot) + ")";
        kits::StageInfo info;
        info.teamid = teamid;
        info.team = app.model.team_name(teamid);
        info.slot = it.slot;
        info.slug = slug;
        info.when = now_text();
        job->tasks.push_back({it.pk.source, kits::slot_dir(app.bridge.root(), teamid, it.slot), info});
        g_panel.staging.emplace_back(teamid, it.slot);
    }
    if (!summary.empty()) app.notify("Kit import: " + summary, any_error);
    g_panel.line = summary.empty() ? "nothing to apply" : summary;
    if (job->tasks.empty()) return;
    g_panel.line += " | converting the textures of " + plural(job->tasks.size(), "kit", "kits") + "...";
    g_panel.stage = job;
    std::thread([job] {
        for (const auto& t : job->tasks) {
            kits::StageResult r = kits::stage_kit_textures(t.zip, t.dir, t.info, job->spec, &job->cancel);
            {
                std::lock_guard<std::mutex> lk(job->m);
                job->results.push_back(std::move(r));
            }
            ++job->finished;
        }
        job->done = true;
    }).detach();
}

static void picture(App& app, const std::string& key, uint64_t version, const Rgba& img, float side, const char* empty_text) {
    if (img.empty()) {
        ImGui::Dummy(ImVec2(side, side));
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Border));
        if (empty_text && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", empty_text);
        return;
    }
    TextureCache::Pic pic = app.textures.pixels(key, version, img);
    if (pic.tex && pic.w > 0) {
        const float s = side / float(std::max(pic.w, pic.h));
        ImGui::Image(pic.tex->GetTexRef(), ImVec2(pic.w * s, pic.h * s));
    } else {
        ImGui::Dummy(ImVec2(side, side));
    }
}

static void swatch_hex(const char* id, const uint8_t rgb[3]) {
    ImGui::ColorButton(id, ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha,
                       ImVec2(S(14.0f), S(14.0f)));
    ImGui::SameLine();
    ImGui::Text("#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
}

// settings.json next to the club's row of that slot
static void settings_table(App& app, int64_t teamid, const KitPanel::Item& it, size_t i) {
    const Table* kt = app.db.table("teamkits");
    int64_t kitid = -1;
    const uint64_t row = kt ? kit_row(app, teamid, it.slot, &kitid) : 0;
    (void)i;  // the item's PushID keeps one table per kit apart
    if (!ImGui::BeginTable("##kitset", 3, tbl::kData | ImGuiTableFlags_SizingFixedFit)) return;
    ImGui::TableSetupColumn("Setting");
    ImGui::TableSetupColumn("In the zip");
    ImGui::TableSetupColumn(row ? "This club now" : "This club now (no row for this slot)");
    ImGui::TableHeadersRow();
    for (const auto& kv : it.pk.settings.shown) {
        ImGui::PushID(kv.first.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kv.first.c_str());
        ImGui::TableNextColumn();
        uint8_t z[3];
        const bool colour = kits::parse_hex_colour(kv.second, z) && !kit_setting_field(kv.first);
        if (colour) swatch_hex("##z", z);
        else ImGui::TextUnformatted(kv.second.c_str());
        ImGui::TableNextColumn();
        if (!row) {
            ImGui::TextDisabled("-");
        } else if (colour) {
            uint8_t now[3];
            if (read_colour(app, *kt, row, kv.first, now)) swatch_hex("##n", now);
            else ImGui::TextDisabled("not in FC 27's teamkits");
        } else if (kt->field(kv.first)) {
            ImGui::Text("%lld", static_cast<long long>(app.db.get_int(*kt, row, kv.first, 0)));
        } else {
            ImGui::TextDisabled("not in FC 27's teamkits");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// ---- 2.13: the 3D kit's status in plain words (one kit folder is shown whole: the zip's, or the game's own)
static const char* kit_words(const std::string& kind) {
    switch (kitswap::kind_slot(kind)) {
        case 0: return "home kit";
        case 1: return "away kit";
        case 2: return "GK kit";
        case 3: return "third kit";
        case 4: return "fourth kit";
        default: return "kit";
    }
}
static std::string garment_of(const std::string& role) {
    if (role.compare(0, 6, "jersey") == 0) return "shirt";
    if (role.compare(0, 6, "shorts") == 0) return "shorts";
    if (role.compare(0, 5, "socks") == 0) return "socks";
    return "texture";
}
// "Aston Villa's GK kit and Team 50's GK kit", "this club's away kit"
static std::string kits_words(const std::vector<kitswap::KitUser>& users, int64_t teamid, const std::function<std::string(int64_t)>& name) {
    std::vector<std::string> parts;
    for (const auto& u : users) {
        const std::string w = (u.team == teamid ? std::string("this club's") : name(u.team) + "'s") + " " + kit_words(u.kind);
        if (std::find(parts.begin(), parts.end(), w) == parts.end()) parts.push_back(w);
    }
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) s += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return s;
}
static std::string verdict_words(const kitswap::FolderVerdict& f, int64_t teamid, const std::function<std::string(int64_t)>& name) {
    std::vector<kitswap::KitUser> others, club;
    for (const auto& u : f.shared) (u.team == teamid ? club : others).push_back(u);
    if (f.swapped) {
        std::string s = "swapped";
        if (!others.empty()) s += " (also changes " + kits_words(others, teamid, name) + ")";
        if (f.details_left) s += "; " + std::to_string(f.details_left) + (f.details_left == 1 ? " detail map stays" : " detail maps stay") + " the game's";
        return s;
    }
    const std::string g = garment_of(f.role);
    const std::string is = "the " + g + (g == "shirt" || g == "texture" ? " is" : " are");
    switch (f.why) {
        case kitswap::LeftBy::OtherClubs:
            if (!others.empty())
                return "not swapped: " + is + " shared with " + kits_words(others, teamid, name) + "; tick 'Also change other clubs' kits...' to swap " + (others.size() == 1 ? "both" : "them all");
            break;
        case kitswap::LeftBy::ThisClub:
            if (!club.empty()) return "not swapped: " + is + " shared with " + kits_words(club, teamid, name) + "; tick 'Also change this club's other kits...' to swap both";
            break;
        case kitswap::LeftBy::ThisClubData:
            if (!f.shared.empty()) return "not swapped: " + is + " made of the same data as " + kits_words(f.shared, teamid, name) + ", which Turbo cannot change on its own";
            break;
        case kitswap::LeftBy::Clash:
            if (!others.empty() || !club.empty())
                return "not swapped: " + is + " made of the same data as " + kits_words(f.shared, teamid, name) + ", which has its own imported kit with another picture";
            break;
        default:
            break;
    }
    return "not swapped (" + g + "): " + f.reason;
}

std::string kit_swap_status_line(const kitswap::Status& st, int64_t teamid, int slot, const std::function<std::string(int64_t)>& name) {
    std::vector<const kitswap::FolderVerdict*> fs;  // the main kit folder first, its alternative versions (loc2_*) after
    for (const auto& f : st.folders)
        if (f.team == teamid && f.slot == slot) fs.push_back(&f);
    if (fs.empty()) return "";
    std::string s = verdict_words(*fs[0], teamid, name);
    for (size_t i = 1; i < fs.size(); ++i) {
        if (fs[i]->swapped == fs[0]->swapped) continue;
        std::string w = verdict_words(*fs[i], teamid, name);
        if (w.compare(0, 13, "not swapped: ") == 0) w = w.substr(13);
        s += "; the game's alternative version of this kit (" + fs[i]->kind + ") " + (fs[i]->swapped ? std::string("is swapped") : "stays the game's: " + w);
    }
    return s;
}

std::string kit_swap_shared_clubs(const kitswap::Status& st, int64_t teamid, int slot, const std::function<std::string(int64_t)>& name) {
    std::vector<kitswap::KitUser> others;
    for (const auto& it : st.items) {
        if (it.team != teamid || it.slot != slot || it.probe) continue;
        for (const auto& u : it.shared)
            if (u.team != teamid && std::none_of(others.begin(), others.end(), [&](const kitswap::KitUser& o) { return o.team == u.team && o.kind == u.kind; })) others.push_back(u);
    }
    return kits_words(others, teamid, name);
}

// 3D kit on the pitch for one imported slot: off / watch only / show (core/kit_swap.h, win/kit_swap_win.cpp)
static void kit_swap_controls(App& app, int64_t teamid, Slot s, bool staged) {
    kitswap::Runtime* rt = app.kit_swap;
    ImGui::Indent();
    if (!rt) {
        ImGui::TextDisabled("3D kit on the pitch: not available in this session (Turbo's game hooks are off)");
        ImGui::Unindent();
        return;
    }
    if (!rt->opted_in()) {  // experimental, off by default: it adds a hook on the game's decompressor, so it needs an explicit opt-in file
        ImGui::TextWrapped("3D kit on the pitch is off, so an imported kit only changes the colours and the minikit, not the kit the players wear in a match.");
        if (ImGui::Button("Turn on 3D kits (restart the game afterwards)##k3on")) {
            // the opt-in is the empty file turbo_output\kits3d_on.txt (nothing is hooked without it); the button only creates it
            std::error_code ec;
            const fs::path f = app.bridge.root() / "turbo_output" / "kits3d_on.txt";
            fs::create_directories(f.parent_path(), ec);
            std::ofstream(f).put('\n');
            const bool ok = fs::exists(f, ec);
            app.notify(ok ? "3D kits are on from the next game start. Delete turbo_output\\kits3d_on.txt to turn them off."
                          : "Could not create turbo_output\\kits3d_on.txt",
                       !ok);
        }
        ImGui::TextDisabled("Experimental: one hook on the game's decompressor; proven in game for Leicester's home kit only. turbo_output\\kits3d_off.txt switches it off at once.");
        ImGui::Unindent();
        return;
    }
    const int slot = kits::slot_type(s);
    kitswap::Mode m = kitswap::Mode::Off;
    const bool active = rt->is_active(int(teamid), slot, &m);
    int sel = !active ? 0 : m == kitswap::Mode::Swap ? 2 : 1;
    bool changed = false;
    if (!staged) ImGui::BeginDisabled();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("3D kit on the pitch:");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Off##kswap", &sel, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Watch only (log what the game loads)##kswap", &sel, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Show it on the pitch##kswap", &sel, 2);
    if (!staged) ImGui::EndDisabled();
    if (changed) {
        std::string err;
        const bool ok = rt->activate(int(teamid), slot, sel == 0 ? kitswap::Mode::Off : sel == 1 ? kitswap::Mode::Observe : kitswap::Mode::Swap, &err);
        app.log(std::string("kit swap: team ") + std::to_string(teamid) + " " + kits::slot_label(s) + ": " + (ok ? kitswap::mode_name(sel == 0 ? kitswap::Mode::Off : sel == 1 ? kitswap::Mode::Observe : kitswap::Mode::Swap) : "refused: " + err));
        if (!ok) app.notify("3D kit: " + err, true);
    }
    const kitswap::Status st = rt->status();
    if (active) {
        int in_table = 0, left = 0, watch = 0;
        uint64_t seen = 0;
        for (const auto& it : st.items)
            if (it.team == teamid && it.slot == slot) {
                if (it.probe) ++watch;
                else if (it.swap) ++in_table;
                else ++left;
                if (!it.probe) seen += it.hits;
            }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(st.state == "error" ? kWarn : kGood, "%s%s", st.state.c_str(), st.killed ? " (kill switch kits3d_off.txt is present: nothing is swapped)" : "");
        ImGui::SameLine();
        if (st.scanning)
            ImGui::TextDisabled("reading the game's kit data (%d of %d bundles; once per game update, then it is kept in turbo_output\\kits\\kit_index.cache)", st.scan_done, st.scan_total);
        else
            ImGui::TextDisabled("%s | %d textures in the table, %d left to the game, %d more watched | blocks of this kit seen in the game so far: %llu | %s | kit index %s",
                                st.message.c_str(), in_table, left, watch, static_cast<unsigned long long>(seen), st.hook ? "hook installed" : "HOOK NOT INSTALLED",
                                st.index_cached ? "from the saved copy" : "scanned this session");
        ImGui::PopTextWrapPos();
        if (ImGui::TreeNode("Textures##kswap")) {
            for (const auto& it : st.items)
                if (it.team == teamid && it.slot == slot) {
                    const std::string leaf = it.res.substr(it.res.rfind('/') == std::string::npos ? 0 : it.res.rfind('/') + 1);
                    const std::string copies = it.copies > 1 ? " (" + std::to_string(it.copies) + " copies in the game)" : "";
                    std::string detail = it.swap || it.probe ? "seen " + std::to_string(it.hits) + " of " + std::to_string(it.blocks) + " blocks" : it.reason;
                    if ((it.swap || it.probe) && it.hits) detail += " | first " + clock_text(it.first_ms) + ", last " + clock_text(it.last_ms);
                    if ((it.swap || it.probe) && !it.reason.empty()) detail += (it.role == "hotspots" ? " | " : " - ") + it.reason;
                    ImGui::TextDisabled("%s %s/%s: %s%s", it.probe ? "[watched]" : it.swap ? "[in table]" : "[game's own]", it.kind.c_str(), leaf.c_str(), detail.c_str(), copies.c_str());
                }
            ImGui::TreePop();
        }
    }
    // what the game has for this club, and what the last match used (all slots of the club)
    const std::set<std::string> have = rt->kinds_of(int(teamid));
    if (!have.empty()) {
        std::string list;
        for (const auto& k : have)
            if (kitswap::kind_slot(k) >= 0) list += (list.empty() ? "" : ", ") + k;
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("3D kit folders the game has for this club: %s", list.c_str());
        bool mine = false;
        for (const auto& k : have) mine = mine || kitswap::kind_slot(k) == slot;
        if (!mine) ImGui::TextColored(kWarn, "The game has no 3D %s kit for this club: switching it on finds nothing to replace.", kits::slot_label(s));
        ImGui::PopTextWrapPos();
    }
    bool any_seen = false;
    for (const auto& k : st.kinds) any_seen = any_seen || (k.team == teamid && k.blocks_seen);
    if (active && ImGui::TreeNode("Kit folders the match loaded##kswap")) {
        for (const auto& k : st.kinds)
            if (k.team == teamid) ImGui::TextDisabled("%s: %d of %d textures, %llu blocks%s", k.kind.c_str(), k.textures_seen, k.textures, static_cast<unsigned long long>(k.blocks_seen),
                                                      k.blocks_seen ? "  <- loaded" : "");
        if (st.kinds.empty()) ImGui::TextDisabled("(nothing registered yet)");
        else if (!any_seen) ImGui::TextDisabled("no match has loaded this club's kits since the game started");
        ImGui::TreePop();
    }
    // per-kit choices (saved)
    bool decals = true, keep_crest = true, share_club = false, share_other = false;
    const bool have_opts = rt->slot_options(int(teamid), slot, &decals, &keep_crest, &share_club, &share_other);
    bool opt_changed = false;
    if (!have_opts) ImGui::BeginDisabled();
    opt_changed |= ImGui::Checkbox("Also use the zip's badge and brand pictures##kswap", &decals);
    if (!decals) ImGui::BeginDisabled();
    ImGui::SameLine();
    opt_changed |= ImGui::Checkbox("Do not use the zip's crest pictures##kswap", &keep_crest);
    if (!decals) ImGui::EndDisabled();
    opt_changed |= ImGui::Checkbox("Also change this club's other kits that share a texture with this one##kswap", &share_club);
    // 2.13: EA reuses some kit pictures between clubs (goalkeeper templates): with this ticked they are replaced too, so the other club's
    // kit shows them as well; unticked, a kit whose shirt, shorts or socks picture is shared stays the game's whole (never half a kit)
    opt_changed |= ImGui::Checkbox("Also change other clubs' kits that share this texture##kswap", &share_other);
    const auto club_name = [&](int64_t id) { return app.model.team_name(id); };
    const std::string clubs = active ? kit_swap_shared_clubs(st, teamid, slot, club_name) : std::string();
    if (!clubs.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(shared with %s)", clubs.c_str());
    }
    if (!have_opts) ImGui::EndDisabled();
    if (opt_changed && have_opts) rt->set_slot_options(int(teamid), slot, decals, keep_crest, share_club, share_other);
    const std::string status = active ? kit_swap_status_line(st, teamid, slot, club_name) : std::string();
    if (!status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(status.compare(0, 7, "swapped") == 0 ? kGood : kWarn, "Status%s: %s", m == kitswap::Mode::Observe ? " (watch only, nothing is replaced)" : "", status.c_str());
        ImGui::PopTextWrapPos();
    }
    kitswap::Options kopt = rt->get_options();
    bool watch_club = kopt.watch_club;
    if (ImGui::Checkbox("Log which of this club's kit folders the match loads (watch only, all slots)##kswap", &watch_club)) {
        kopt.watch_club = watch_club;
        rt->set_options(kopt);
        if (!rt->status().slots.empty()) rt->rebuild();
    }
    bool hot = kopt.apply_hotspots;
    if (ImGui::Checkbox("Also use the zip's name and number placement (hotspots)##kswap", &hot)) {
        kopt.apply_hotspots = hot;
        rt->set_options(kopt);
        if (!rt->status().slots.empty()) rt->rebuild();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Takes effect when the game next loads this kit (the next match load, or after a game restart when the kit was already loaded this session). "
                        "The zip's crest picture is usually blank and its badge is baked into the jersey picture: its hotspot 'team' is then 'not used' (-0.5), and with 'Also use the zip's badge and brand pictures' ticked (default) the game's own chest badge is switched off, so you see ONE badge (the baked one). "
                        "With that option unticked the game's badge is drawn as well and you may see two. "
                        "A kit is never shown half: when its shirt, shorts or socks picture cannot be replaced, the game's whole kit stays. "
                        "A texture the game shares with another club is replaced only with 'Also change other clubs' kits that share this texture' (that club's kit then changes too). "
                        "turbo_output\\kits3d_off.txt switches the feature off at once (it also stops the reading of the game's kit data); "
                        "the log is turbo_output\\kit_swap_log.txt.");
    ImGui::PopTextWrapPos();
    ImGui::Unindent();
}

// The slot selected on the Kits page (2.13)
static Slot g_page_slot = Slot::Home;

// The editor body. in_page false = the legacy Teams > Kits tab (every slot's record in one list, an Apply per kit);
// in_page true = Club & Kits > Kits (the selected slot only; the page's primary button applies, Restore EA kit undoes).
static void kits_body(App& app, int64_t teamid, bool in_page) {
    KitPanel& p = g_panel;
    poll_jobs(app);
    const std::string club = app.model.team_name(teamid);
    const bool busy = kit_panel_busy();

    ImGui::SeparatorText(in_page ? "Source: a FIFA Kit Creator kit (fifakitcreator.com zip)" : "Import a FIFA Kit Creator kit (fifakitcreator.com zip)");
    if (!in_page) {
        ImGui::Text("Target club: %s (ID %lld)", club.c_str(), static_cast<long long>(teamid));
        ImGui::SameLine();
        ImGui::TextDisabled("pick another club in the list on the left");
    }
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Import kit zip...")) {
        p.fp_zip.mode = PickMode::Open;
        p.fp_zip.title = "Choose a kit zip (FIFA Kit Creator download)";
        p.fp_zip.exts = {".zip"};
        p.fp_zip.key = "kit_import";
        p.fp_zip.start = kit_import_start_folder(app);
        p.fp_zip.places = {{"My Mods", fs::path("C:\\FC_Tools\\My Mods")}, {"Live Editor", app.bridge.root()}};
        ImGui::OpenPopup("##kitzip");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import team folder...")) {
        p.fp_folder.mode = PickMode::Folder;
        p.fp_folder.title = "Choose a team folder (its h / a / t / g zips are imported at once)";
        p.fp_folder.exts = {".zip"};
        p.fp_folder.key = "kit_import";
        p.fp_folder.start = kit_import_start_folder(app);
        p.fp_folder.places = {{"My Mods", fs::path("C:\\FC_Tools\\My Mods")}, {"Live Editor", app.bridge.root()}};
        ImGui::OpenPopup("##kitfolder");
    }
    if (busy) ImGui::EndDisabled();
    fs::path chosen;
    if (file_picker_modal("##kitzip", p.fp_zip, chosen)) kit_panel_open(app, chosen);
    if (file_picker_modal("##kitfolder", p.fp_folder, chosen)) kit_panel_open(app, chosen);
    ImGui::TextDisabled("Starts in C:\\FC_Tools\\My Mods. A team folder (eng\\leicester) imports its h / a / t / g zips at once.");
    if (p.load) ImGui::TextUnformatted(("Reading " + std::to_string(p.load->finished.load()) + " of " + std::to_string(p.load->files.size()) + "...").c_str());

    // ---- the kits read
    if (!p.items.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", p.source_label.c_str());
    }
    std::vector<size_t> ready;
    for (size_t i = 0; i < p.items.size(); ++i) {
        KitPanel::Item& it = p.items[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::BeginGroup();
        picture(app, "kit_mini_" + std::to_string(i), it.version, it.pk.minikit, S(72.0f), "no minikit");
        ImGui::SameLine();
        picture(app, "kit_jersey_" + std::to_string(i), it.version, it.pk.thumb, S(72.0f), "no jersey colour picture");
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(it.pk.source.filename().string().c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(150.0f));
        char cid[32];
        std::snprintf(cid, sizeof(cid), "##kitslot%zu", i);
        if (ImGui::BeginCombo(cid, kits::slot_label(it.slot))) {
            for (Slot s : {Slot::Home, Slot::Away, Slot::Goalkeeper, Slot::Third, Slot::Fourth}) {
                if (ImGui::Selectable(kits::slot_label(s), it.slot == s)) {
                    it.slot = s;
                    it.status.clear();
                }
            }
            ImGui::EndCombo();
        }
        const std::string st = item_status(i);
        const bool is_ready = st == "ready";
        LegacyImages::State ms = it.slot == Slot::Unknown ? LegacyImages::State::Invalid : minikit_state(app, teamid, it.slot);
        ImGui::PushTextWrapPos(0.0f);
        if (is_ready) ImGui::TextColored(kGood, "%s", ms == LegacyImages::State::Waiting ? "ready (the game's minikit is still loading)" : "ready");
        else if (st.rfind("applied", 0) == 0) ImGui::TextColored(kGood, "%s", st.c_str());
        else ImGui::TextColored(kWarn, "%s", st.c_str());
        for (const auto& n : it.pk.notes) ImGui::TextDisabled("%s", n.c_str());
        if (it.pk.ok && !it.pk.decals) ImGui::TextDisabled("brand / crest decals: the game's own stay");
        ImGui::PopTextWrapPos();
        if (!in_page) {  // on the page the one primary action, "Apply kit", applies the selected slot's ready kits
            const bool can = is_ready && !busy && ms != LegacyImages::State::Waiting && app.model.team(teamid);
            if (!can) ImGui::BeginDisabled();
            char bid[32];
            std::snprintf(bid, sizeof(bid), "Apply##kit%zu", i);
            if (ImGui::Button(bid)) apply_items(app, teamid, {i});
            if (!can) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", ms == LegacyImages::State::Waiting
                                            ? "Waiting for the game's minikit file, so the new one is written in its own size and format"
                                            : "Writes the settings into this club's kit row (kept for every career load), the minikit, and converts the textures");
        }
        if (is_ready) ready.push_back(i);
        ImGui::EndGroup();
        if (it.pk.ok && ImGui::TreeNode("##kitsettings", "Kit settings (settings.json) and this club's values")) {
            settings_table(app, teamid, it, i);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (ready.size() > 1) {
        ImGui::Separator();
        bool waiting = false;
        for (size_t i : ready) waiting = waiting || minikit_state(app, teamid, p.items[i].slot) == LegacyImages::State::Waiting;
        const bool can = !busy && !waiting && app.model.team(teamid);
        if (!can) ImGui::BeginDisabled();
        const std::string all = "Apply all " + std::to_string(ready.size()) + " kits to " + club + "##kitsall";
        if (ImGui::Button(all.c_str())) apply_items(app, teamid, ready);
        if (!can) ImGui::EndDisabled();
    }
    if (p.stage) {
        ImGui::TextUnformatted(("Converting textures: " + std::to_string(p.stage->finished.load()) + " of " + std::to_string(p.stage->tasks.size()) +
                                " kits done...").c_str());
    }
    if (!p.line.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", p.line.c_str());
        ImGui::PopTextWrapPos();
    }

    // ---- this club's imported kits (Undo)
    ImGui::SeparatorText(in_page ? (std::string(kits::slot_label(g_page_slot)) + " kit: what Turbo imported").c_str() : "Kits imported for this club");
    bool any = false;
    for (Slot s : {Slot::Home, Slot::Away, Slot::Goalkeeper, Slot::Third, Slot::Fourth}) {
        if (in_page && s != g_page_slot) continue;
        ImportRecord r;
        if (!load_record(app, teamid, s, r)) continue;
        any = true;
        ImGui::PushID(kits::slot_folder(s));
        std::error_code ec;
        const fs::path dir = kits::slot_dir(app.bridge.root(), teamid, s);
        const bool staged = fs::exists(dir / "manifest.json", ec);
        size_t dds = 0;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->path().extension() == ".dds") ++dds;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s: %s, from %s; %s; minikit %s; textures %s", kits::slot_label(s), r.when.c_str(), text_path(r.source).filename().string().c_str(),
                    plural(r.db.size(), "field", "fields").c_str(), r.minikit ? "written" : "not written",
                    staging_slot(teamid, s) ? "converting..." : staged ? (std::to_string(dds) + " staged").c_str() : "not staged");
        ImGui::SameLine();
        const bool can = !staging_slot(teamid, s);
        if (!can) ImGui::BeginDisabled();
        if (ImGui::SmallButton(in_page ? "Restore EA kit##kitundo" : "Undo##kitundo")) {
            std::string msg;
            const bool ok = undo_kit_import(app, teamid, s, &msg);
            p.line = msg;
            app.notify("Kit import: " + msg, !ok);
        }
        if (!can) ImGui::EndDisabled();
        kit_swap_controls(app, teamid, s, staged && !staging_slot(teamid, s));
        ImGui::PopID();
    }
    if (!any) ImGui::TextDisabled(in_page ? "Nothing imported for this slot: the game's own (EA) kit is in use." : "none");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Live now: the kit settings (kept for every career load, like Teams > Colours; turbo_output\\reapply_off.txt turns "
                        "the career-load re-apply off) and the minikit (mods\\legacy; menus show it when the screen is drawn again). "
                        "The 3D kit on the pitch: Turbo converts the textures to FC 27's own sizes and formats, keeps them in "
                        "turbo_output\\kits\\<club id>\\<slot>\\ and, with Show it on the pitch on, serves them to the game while it loads the kit.");
    ImGui::PopTextWrapPos();
}

// The Teams > Kits tab (the legacy tab bar; the Club & Kits page below replaces it)
void kits_editor(App& app, int64_t teamid) { kits_body(app, teamid, false); }

// ---------------------------------------------------------------- Club & Kits > Kits (2.13)
std::vector<KitSlotRow> kit_slot_rows(App& app, int64_t teamid) {
    std::vector<KitSlotRow> rows;
    for (Slot s : {Slot::Home, Slot::Away, Slot::Third, Slot::Goalkeeper, Slot::Fourth}) {
        KitSlotRow r;
        r.slot = s;
        r.label = kits::slot_label(s);
        r.custom = has_kit_import(app, teamid, s);
        r.state = r.custom ? "custom *" : "EA";
        rows.push_back(std::move(r));
    }
    return rows;
}
Slot kit_page_slot() { return g_page_slot; }
void kit_page_select(Slot s) {
    if (s != Slot::Unknown) g_page_slot = s;
}

namespace {
struct KitsPageCtx {
    int64_t teamid = 0;
};
void kits_select_panel(App& app, void* user) {
    const KitsPageCtx& c = *static_cast<KitsPageCtx*>(user);
    theme::muted_text("Kit slot");
    for (const KitSlotRow& r : kit_slot_rows(app, c.teamid)) {
        const std::string id = std::string(r.label) + "##kslot";
        if (ImGui::Selectable(id.c_str(), g_page_slot == r.slot)) g_page_slot = r.slot;
        ImGui::SameLine(S(120.0f));
        if (r.custom) theme::ok_text("%s", r.state.c_str());
        else theme::muted_text("%s", r.state.c_str());
    }
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("* imported by Turbo. Restore EA kit puts one slot of this club back; other clubs are never touched.");
    ImGui::PopTextWrapPos();
}
void kits_editor_panel(App& app, void* user) {
    const KitsPageCtx& c = *static_cast<KitsPageCtx*>(user);
    ImGui::Text("%s kit . %s", kits::slot_label(g_page_slot), app.model.team_name(c.teamid).c_str());
    kits_body(app, c.teamid, true);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Colours & numbers (kit colours, number fonts, outline)##kitcolours"))
        club_kit_colours_section(app, c.teamid);
}
}  // namespace

void kits_page(App& app, int64_t teamid) {
    poll_jobs(app);
    KitPanel& p = g_panel;
    // a zip or folder was just read: show the slot of its first kit
    static size_t seen_items = 0;
    if (p.items.size() != seen_items) {
        seen_items = p.items.size();
        if (!p.items.empty()) kit_page_select(p.items.front().slot);
    }
    // the primary action: the ready kits of the selected slot
    std::vector<size_t> which;
    bool waiting = false;
    for (size_t i = 0; i < p.items.size(); ++i) {
        if (p.items[i].slot != g_page_slot || item_status(i) != "ready") continue;
        which.push_back(i);
        waiting = waiting || minikit_state(app, teamid, p.items[i].slot) == LegacyImages::State::Waiting;
    }
    KitsPageCtx ctx;
    ctx.teamid = teamid;
    shell::page::Primary primary;
    primary.label = "Apply kit";
    primary.enabled = !which.empty() && !waiting && !kit_panel_busy() && app.model.team(teamid) != nullptr;
    shell::page::frame(app, kits_select_panel, kits_editor_panel, &ctx, &primary);
    if (primary.clicked && primary.enabled) apply_items(app, teamid, which);
}

}  // namespace turbo
