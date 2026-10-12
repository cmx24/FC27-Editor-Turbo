// FC 27 LE Turbo GUI - your career's scouts (see scouts.h)
#include "scouts.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

#include "tactic_profiles.h"  // read_text_file, write_text_atomic

namespace turbo::scouts {

namespace fs = std::filesystem;
using nlohmann::json;

static const char* const kFormat = "turbo-scouts-backup";
static constexpr int kVersion = 2;  // 2: entries carry "team" (version 1 never left the 2.1 branch)

std::string Scout::name() const {
    std::string n = firstname;
    if (!lastname.empty()) n += (n.empty() ? "" : " ") + lastname;
    return n.empty() ? "Scout " + std::to_string(scoutid) : n;
}

int64_t level_max(const Field& f) { return std::min<int64_t>(f.max(), kMaxLevel); }

int64_t clamp_level(const Field& f, int64_t v) {
    const int64_t lo = std::max<int64_t>(f.min, 0), hi = std::max(lo, level_max(f));
    return std::clamp(v, lo, hi);
}

std::vector<Scout> read(Database& db) {
    std::vector<Scout> out;
    const Table* t = db.table("career_scouts");
    if (!t) return out;
    Snapshot snap;
    if (!snap.load(db.memory(), *t)) return out;
    auto str = [&](uint32_t idx, const char* f) {
        const Field* fl = t->field(f);
        return fl ? snap.get_str(idx, *fl) : std::string();
    };
    for (uint32_t idx : snap.valid) {
        Scout s;
        s.rec = snap.addr(idx);
        s.scoutid = snap.get_int(idx, "scoutid", -1);
        s.firstname = str(idx, "firstname");
        s.lastname = str(idx, "lastname");
        s.experience = snap.get_int(idx, "experience", 0);
        s.knowledge = snap.get_int(idx, "knowledge", 0);
        s.nationality = snap.get_int(idx, "nationality", 0);
        s.regionid = snap.get_int(idx, "regionid", 0);
        s.state = snap.get_int(idx, "state", 0);
        out.push_back(std::move(s));
    }
    return out;
}

fs::path backup_path(const fs::path& out_dir) { return out_dir / "turbo_scouts_backup.json"; }
fs::path kill_switch_path(const fs::path& out_dir) { return out_dir / "scouts_off.txt"; }
bool killed(const fs::path& out_dir) {
    std::error_code ec;
    return fs::exists(kill_switch_path(out_dir), ec);
}

static json entry_of(const Scout& x, int64_t team) {
    return {{"team", team}, {"scoutid", x.scoutid}, {"firstname", x.firstname}, {"lastname", x.lastname},
            {"experience", x.experience}, {"knowledge", x.knowledge}};
}

static json file_of(const json& entries) { return {{"format", kFormat}, {"version", kVersion}, {"scouts", entries}}; }

json backup_json(const std::vector<Scout>& s, int64_t team) {
    json arr = json::array();
    for (const Scout& x : s) arr.push_back(entry_of(x, team));
    return file_of(arr);
}

// Types of one entry (the values are clamped to the game's range when they are written back)
static bool entry_ok(const json& e) {
    if (!e.is_object()) return false;
    for (const char* k : {"team", "scoutid", "experience", "knowledge"})
        if (!e.contains(k) || !e[k].is_number_integer()) return false;
    for (const char* k : {"firstname", "lastname"})
        if (!e.contains(k) || !e[k].is_string()) return false;
    return true;
}

// e passed entry_ok
static bool same_scout(const json& e, int64_t team, const Scout& s) {
    return e["team"].get<int64_t>() == team && e["scoutid"].get<int64_t>() == s.scoutid &&
           e["firstname"].get<std::string>() == s.firstname && e["lastname"].get<std::string>() == s.lastname;
}

bool load_backup(const fs::path& out_dir, json& entries, std::string* err) {
    entries = json::array();
    const fs::path bp = backup_path(out_dir);
    std::error_code ec;
    if (!fs::exists(bp, ec)) return true;
    auto fail = [&](const std::string& m) {
        if (err) *err = bp.filename().string() + " " + m;
        return false;
    };
    std::string text, rerr;
    if (!read_text_file(bp, text, &rerr)) return fail("cannot be read: " + rerr);
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("format") || !j["format"].is_string() ||
        j["format"].get<std::string>() != kFormat || !j.contains("scouts") || !j["scouts"].is_array())
        return fail("is not a Turbo scouts backup");
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int64_t>() != kVersion)
        return fail("has another version of the format");
    size_t n = 0;
    for (const json& e : j["scouts"]) {
        ++n;
        if (!entry_ok(e)) return fail("has a damaged entry (number " + std::to_string(n) + ")");
    }
    entries = j["scouts"];
    return true;
}

int backed_up(const fs::path& out_dir, int64_t team) {
    json entries;
    if (!load_backup(out_dir, entries, nullptr)) return 0;
    int n = 0;
    for (const json& e : entries) n += e["team"].get<int64_t>() == team;
    return n;
}

int ensure_backup(Database& db, const fs::path& out_dir, int64_t team, std::string* msg) {
    json entries;
    std::string err;
    if (!load_backup(out_dir, entries, &err)) {
        if (msg) *msg = "scouts: " + err + "; nothing written (move the file away to start a new backup)";
        return -1;
    }
    int added = 0;
    for (const Scout& s : read(db)) {
        bool have = false;
        for (const json& e : entries) have = have || same_scout(e, team, s);
        if (!have) {
            entries.push_back(entry_of(s, team));
            ++added;
        }
    }
    if (added == 0) return 0;
    if (!write_text_atomic(backup_path(out_dir), file_of(entries).dump(2), &err)) {
        if (msg) *msg = "scouts: the backup could not be written: " + err;
        return -1;
    }
    return added;
}

int make_best(Database& db, int level, const fs::path& out_dir, int64_t team, std::string* msg) {
    auto fail = [&](const std::string& m) {
        if (msg) *msg = m;
        return 0;
    };
    if (killed(out_dir)) return fail("scouts: turned off (" + kill_switch_path(out_dir).filename().string() + " exists)");
    const Table* t = db.table("career_scouts");
    if (!t) return fail("scouts: this database has no career_scouts table (load a career)");
    const Field *fe = t->field("experience"), *fk = t->field("knowledge");
    if (!fe || !fk) return fail("scouts: career_scouts has no experience / knowledge field");
    const std::vector<Scout> all = read(db);
    if (all.empty()) return fail("scouts: your career has no scouts yet");
    std::string err;
    if (ensure_backup(db, out_dir, team, &err) < 0) return fail(err);
    const int64_t e = clamp_level(*fe, level), k = clamp_level(*fk, level);
    int changed = 0;
    for (const Scout& s : all) {
        if (!db.table_alive(*t, s.rec)) return fail("scouts: the database changed (another save loaded?). Press Refresh.");
        bool did = false;
        if (s.experience != e) {
            if (!db.set_int(*t, s.rec, "experience", e, &err)) return fail("scouts: " + err);
            did = true;
        }
        if (s.knowledge != k) {
            if (!db.set_int(*t, s.rec, "knowledge", k, &err)) return fail("scouts: " + err);
            did = true;
        }
        changed += did;
    }
    if (msg)
        *msg = "Scouts: " + std::to_string(changed) + " of " + std::to_string(all.size()) + " set to level " + std::to_string(e) +
               " of " + std::to_string(kMaxLevel) + " (originals in " + backup_path(out_dir).filename().string() + ")";
    return changed;
}

bool set_level(Database& db, uint64_t rec, const std::string& field, int64_t v, const fs::path& out_dir, int64_t team,
               std::string* msg) {
    auto fail = [&](const std::string& m) {
        if (msg) *msg = m;
        return false;
    };
    if (field != "experience" && field != "knowledge") return fail("scouts: " + field + " is not a level field");
    if (killed(out_dir)) return fail("scouts: turned off (" + kill_switch_path(out_dir).filename().string() + " exists)");
    const Table* t = db.table("career_scouts");
    const Field* f = t ? t->field(field) : nullptr;
    if (!t || !f) return fail("scouts: this database has no career_scouts." + field);
    if (!db.table_alive(*t, rec)) return fail("scouts: the database changed (another save loaded?). Press Refresh.");
    std::string err;
    if (ensure_backup(db, out_dir, team, &err) < 0) return fail(err);
    if (!db.set_int(*t, rec, field, clamp_level(*f, v), &err)) return fail("scouts: " + err);
    return true;
}

int restore(Database& db, const fs::path& out_dir, int64_t team, std::string* msg) {
    auto fail = [&](const std::string& m) {
        if (msg) *msg = m;
        return 0;
    };
    if (killed(out_dir)) return fail("scouts: turned off (" + kill_switch_path(out_dir).filename().string() + " exists)");
    const fs::path bp = backup_path(out_dir);
    json entries;
    std::string err;
    if (!load_backup(out_dir, entries, &err)) return fail("scouts: " + err + "; nothing restored");
    if (entries.empty()) return fail("scouts: no backup to restore (" + bp.filename().string() + ")");
    const Table* t = db.table("career_scouts");
    if (!t) return fail("scouts: this database has no career_scouts table (load a career)");
    const Field *fe = t->field("experience"), *fk = t->field("knowledge");
    if (!fe || !fk) return fail("scouts: career_scouts has no experience / knowledge field");
    const std::vector<Scout> all = read(db);
    // every entry passed load_backup's checks; the writes are planned before the first one
    std::vector<std::pair<const Scout*, size_t>> plan;
    int unmatched = 0, others = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const json& e = entries[i];
        if (e["team"].get<int64_t>() != team) {
            ++others;
            continue;
        }
        const Scout* s = nullptr;
        for (const Scout& x : all)
            if (same_scout(e, team, x)) s = &x;
        if (!s || !db.table_alive(*t, s->rec)) {
            ++unmatched;
            continue;
        }
        plan.push_back({s, i});
    }
    auto kept = [&]() {
        std::string k;
        if (unmatched) k += ", " + std::to_string(unmatched) + " of this club's not found (kept)";
        if (others) k += ", " + std::to_string(others) + " kept for other careers / clubs";
        return k;
    };
    if (plan.empty()) return fail("scouts: nothing in the backup matches this career's scouts" + kept());
    std::vector<bool> done(entries.size(), false);
    for (const auto& [s, i] : plan) {
        const json& e = entries[i];
        if (!db.set_int(*t, s->rec, "experience", clamp_level(*fe, e["experience"].get<int64_t>()), &err) ||
            !db.set_int(*t, s->rec, "knowledge", clamp_level(*fk, e["knowledge"].get<int64_t>()), &err))
            return fail("scouts: " + err + " (the backup is unchanged)");
        done[i] = true;
    }
    json left = json::array();
    for (size_t i = 0; i < entries.size(); ++i)
        if (!done[i]) left.push_back(entries[i]);
    std::error_code ec;
    std::string tail;
    if (left.empty()) fs::remove(bp, ec);
    else if (!write_text_atomic(bp, file_of(left).dump(2), &err)) tail = " (the backup could not be updated: " + err + ")";
    if (msg) *msg = "Scouts: " + std::to_string(plan.size()) + " restored" + kept() + tail;
    return static_cast<int>(plan.size());
}

}  // namespace turbo::scouts
