// FC 27 LE Turbo GUI - your career's scouts (see scouts.h)
#include "scouts.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "tactic_profiles.h"  // read_text_file, write_text_atomic

namespace turbo::scouts {

namespace fs = std::filesystem;
using nlohmann::json;

std::string Scout::name() const {
    std::string n = firstname;
    if (!lastname.empty()) n += (n.empty() ? "" : " ") + lastname;
    return n.empty() ? "Scout " + std::to_string(scoutid) : n;
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

json backup_json(const std::vector<Scout>& s) {
    json arr = json::array();
    for (const Scout& x : s)
        arr.push_back({{"scoutid", x.scoutid}, {"firstname", x.firstname}, {"lastname", x.lastname},
                       {"experience", x.experience}, {"knowledge", x.knowledge}});
    return {{"format", "turbo-scouts-backup"}, {"version", 1}, {"scouts", arr}};
}

int make_best(Database& db, int level, const fs::path& out_dir, std::string* msg) {
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
    std::error_code ec;
    const fs::path bp = backup_path(out_dir);
    if (!fs::exists(bp, ec)) {
        std::string err;
        if (!write_text_atomic(bp, backup_json(all).dump(2), &err)) return fail("scouts: the backup could not be written: " + err);
    }
    int changed = 0;
    std::string err;
    for (const Scout& s : all) {
        if (!db.table_alive(*t, s.rec)) return fail("scouts: the database changed (another save loaded?). Press Refresh.");
        const int64_t e = std::clamp<int64_t>(level, fe->min, fe->max()), k = std::clamp<int64_t>(level, fk->min, fk->max());
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
        *msg = "Scouts: " + std::to_string(changed) + " of " + std::to_string(all.size()) + " set to level " + std::to_string(level) +
               " (originals in " + bp.filename().string() + ")";
    return changed;
}

int restore(Database& db, const fs::path& out_dir, std::string* msg) {
    auto fail = [&](const std::string& m) {
        if (msg) *msg = m;
        return 0;
    };
    if (killed(out_dir)) return fail("scouts: turned off (" + kill_switch_path(out_dir).filename().string() + " exists)");
    const fs::path bp = backup_path(out_dir);
    std::string text, err;
    if (!read_text_file(bp, text, &err)) return fail("scouts: no backup to restore (" + bp.filename().string() + ")");
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != "turbo-scouts-backup" || !j.contains("scouts") ||
        !j["scouts"].is_array())
        return fail("scouts: " + bp.filename().string() + " is not a Turbo scouts backup");
    const Table* t = db.table("career_scouts");
    if (!t) return fail("scouts: this database has no career_scouts table (load a career)");
    const std::vector<Scout> all = read(db);
    int restored = 0, unmatched = 0;
    for (const json& b : j["scouts"]) {
        if (!b.is_object()) {
            ++unmatched;
            continue;
        }
        const int64_t id = b.value("scoutid", int64_t(-1));
        const std::string fn = b.value("firstname", ""), ln = b.value("lastname", "");
        const Scout* s = nullptr;
        for (const Scout& x : all)
            if (x.scoutid == id && x.firstname == fn && x.lastname == ln) s = &x;
        if (!s || !db.table_alive(*t, s->rec)) {
            ++unmatched;
            continue;
        }
        if (!db.set_int(*t, s->rec, "experience", b.value("experience", s->experience), &err) ||
            !db.set_int(*t, s->rec, "knowledge", b.value("knowledge", s->knowledge), &err))
            return fail("scouts: " + err);
        ++restored;
    }
    std::error_code ec;
    if (unmatched == 0) fs::remove(bp, ec);
    if (msg)
        *msg = "Scouts: " + std::to_string(restored) + " restored" +
               (unmatched ? ", " + std::to_string(unmatched) + " not found in this career (backup kept)" : std::string());
    return restored;
}

}  // namespace turbo::scouts
