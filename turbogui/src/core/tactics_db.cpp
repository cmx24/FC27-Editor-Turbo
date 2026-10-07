// FC 27 LE Turbo GUI - what the Tactics tab reads from the career database (see tactics_db.h)
#include "tactics_db.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "model.h"  // position_name

namespace turbo {

std::string pitch_label_for_position(int code) {
    const std::string n = position_name(code);
    if (n == "?") return "?";
    if (n == "RCB" || n == "LCB") return "CB";
    if (n == "RDM" || n == "LDM") return "CDM";
    if (n == "RCM" || n == "LCM") return "CM";
    if (n == "RAM" || n == "LAM") return "CAM";
    if (n == "RS" || n == "LS") return "ST";
    return n;
}

namespace {

constexpr int kSlots = 11;

std::string num(int64_t v) { return std::to_string(v); }

// The saved offsets of one record: 11 (x, y) pairs. False (with why) when a field is missing or is not a decimal number.
bool read_offsets(const Snapshot& s, uint32_t idx, std::vector<std::pair<float, float>>& out, std::string& why) {
    out.clear();
    for (int i = 0; i < kSlots; ++i) {
        const Field* fx = s.table->field("offset" + num(i) + "x");
        const Field* fy = s.table->field("offset" + num(i) + "y");
        if (!fx || !fy) {
            why = "the table has no offset fields for slot " + num(i);
            return false;
        }
        if (fx->type != FieldType::Float || fy->type != FieldType::Float) {
            why = "the offsets are not decimal numbers in this save";
            return false;
        }
        out.emplace_back(s.get_float(idx, *fx), s.get_float(idx, *fy));
    }
    return true;
}

// Offsets that look like eleven places on a pitch: finite, inside 0..1 (a little slack), and not all the same spot
bool offsets_mean_something(const std::vector<std::pair<float, float>>& o, std::string* why = nullptr) {
    std::set<std::pair<int, int>> spots;
    for (const auto& p : o) {
        if (!std::isfinite(p.first) || !std::isfinite(p.second)) {
            if (why) *why = "an offset is not a number";
            return false;
        }
        if (p.first < -0.05f || p.first > 1.05f || p.second < -0.05f || p.second > 1.05f) {
            if (why) *why = "the offsets are outside 0..1";
            return false;
        }
        spots.insert({int(std::lround(p.first * 100.0f)), int(std::lround(p.second * 100.0f))});
    }
    if (spots.size() < 4) {
        if (why) *why = "the offsets are all zero or all the same";
        return false;
    }
    return true;
}

bool int_field(const Snapshot& s, uint32_t idx, const std::string& name, int64_t& v) {
    const Field* f = s.table->field(name);
    if (!f || f->type != FieldType::Int) return false;
    v = s.get_int(idx, *f);
    return true;
}

// first valid record index whose integer field `name` equals v
bool find_row(const Snapshot& s, const std::string& name, int64_t v, uint32_t& idx) {
    const Field* f = s.table->field(name);
    if (!f || f->type != FieldType::Int) return false;
    for (uint32_t i : s.valid)
        if (s.get_int(i, *f) == v) {
            idx = i;
            return true;
        }
    return false;
}

std::string row_name(const Snapshot& s, uint32_t idx) {
    const Field* f = s.table->field("formationname");
    return f && f->type == FieldType::String ? s.get_str(idx, *f) : std::string();
}

}  // namespace

bool read_formation_by_id(Database& db, int64_t formationid, Formation& out, std::string& source, std::string& why) {
    const Table* ft = db.table("formations");
    if (!ft || !ft->has("formationid")) {
        why = "this save's formations table has no formation ids";
        return false;
    }
    Snapshot snap;
    if (!snap.load(db.memory(), *ft)) {
        why = "the formations table could not be read";
        return false;
    }
    uint32_t idx = 0;
    if (!find_row(snap, "formationid", formationid, idx)) {
        why = "formation " + num(formationid) + " is not in the formations table";
        return false;
    }
    std::vector<FormationSlot> slots(kSlots);
    for (int i = 0; i < kSlots; ++i) {
        const Field* fp = ft->field("position" + num(i));
        if (!fp || fp->type != FieldType::Int) {
            why = "the formations table has no position fields";
            return false;
        }
        const int64_t code = snap.get_int(idx, *fp);
        if (i == 0) {
            if (code != 0) {
                why = "slot 0 of formation " + num(formationid) + " is not the goalkeeper";
                return false;
            }
            slots[0].label = "GK";
        } else {
            slots[size_t(i)].label = pitch_label_for_position(int(code));
        }
    }
    std::vector<std::pair<float, float>> off;
    std::string owhy, offsets_from = "formations";
    bool have = read_offsets(snap, idx, off, owhy) && offsets_mean_something(off, &owhy);
    if (!have) {  // the formations row has no offsets that mean anything: formationoffsets may
        const Table* ot = db.table("formationoffsets");
        Snapshot os;
        uint32_t oi = 0;
        std::vector<std::pair<float, float>> off2;
        std::string why2;
        if (ot && os.load(db.memory(), *ot) && find_row(os, "formationid", formationid, oi) && read_offsets(os, oi, off2, why2) &&
            offsets_mean_something(off2, &why2)) {
            off = off2;
            have = true;
            offsets_from = "formationoffsets";
        } else if (ot && why2.size()) {
            owhy += " (formationoffsets: " + why2 + ")";
        }
    }
    if (!have) {
        why = owhy;
        return false;
    }
    if (off[0].second > 0.5f)  // the goalkeeper marks the own goal line: a y axis that runs the other way is turned round
        for (auto& p : off) p.second = 1.0f - p.second;
    for (int i = 0; i < kSlots; ++i) {
        slots[size_t(i)].x = off[size_t(i)].first;
        slots[size_t(i)].y = off[size_t(i)].second;
    }
    std::string name = row_name(snap, idx);
    if (name.empty()) name = "Formation " + num(formationid);
    Formation f;
    std::string err;
    if (!make_formation(num(formationid), name, std::move(slots), f, &err)) {
        why = err;
        return false;
    }
    out = f;
    source = "saved formation " + num(formationid) + (offsets_from == "formations" ? "" : " (offsets from formationoffsets)");
    return true;
}

TeamFormation read_team_formation(Database& db, int64_t teamid) {
    TeamFormation r;
    r.formation = *find_fallback_formation("4-4-2");
    r.note = "the save's formation could not be found: a built-in shape is shown";
    try {
        const Table* ft = db.table("formations");
        if (!ft) {
            r.note = "this save has no formations table: a built-in shape is shown";
            return r;
        }
        int64_t fid = -1;
        std::string via, team_row_name;
        // 1. the team's style link: teamid -> formationid
        if (const Table* lt = db.table("teamformationteamstylelinks")) {
            Snapshot ls;
            uint32_t li = 0;
            int64_t v = 0;
            if (ls.load(db.memory(), *lt) && find_row(ls, "teamid", teamid, li) && int_field(ls, li, "formationid", v) && v >= 0) {
                fid = v;
                via = "the team's style link";
            }
        }
        // 2. the formation of the team's active tactic
        if (fid < 0) {
            for (const char* tn : {"cm_mentalities", "mentalities"}) {
                const Table* mt = db.table(tn);
                if (!mt || !mt->has("sourceformationid")) continue;
                const TeamRows rows = team_rows(db, *mt, teamid);
                if (rows.recs.empty()) continue;
                const int64_t v = db.get_int(*mt, rows.recs[rows.chosen], "sourceformationid", -1);
                if (v >= 0) {
                    fid = v;
                    via = std::string("the team's tactic in ") + tn;
                    break;
                }
            }
        }
        // 3. a formations row that carries the team's id (also gives the name Turbo's own shapes are matched by)
        {
            Snapshot fs;
            uint32_t fi = 0;
            if (fs.load(db.memory(), *ft) && find_row(fs, "teamid", teamid, fi)) {
                team_row_name = row_name(fs, fi);
                int64_t v = 0;
                if (fid < 0 && int_field(fs, fi, "formationid", v) && v >= 0) {
                    fid = v;
                    via = "the formation row of the team";
                }
            }
        }
        std::string why;
        if (fid >= 0) {
            Formation f;
            std::string source;
            if (read_formation_by_id(db, fid, f, source, why)) {
                r.formation = f;
                r.saved = true;
                r.source = source + " (" + via + ")";
                r.note.clear();
                return r;
            }
            Snapshot fs;
            uint32_t fi = 0;
            if (team_row_name.empty() && fs.load(db.memory(), *ft) && find_row(fs, "formationid", fid, fi)) team_row_name = row_name(fs, fi);
        } else {
            why = "the save does not say which formation this team uses";
        }
        // the name may still pick one of Turbo's own shapes
        if (const Formation* b = team_row_name.empty() ? nullptr : find_fallback_formation(team_row_name)) {
            r.formation = *b;
            r.note = "the save names " + team_row_name + " but its positions could not be read (" + why + "): Turbo's built-in " + team_row_name +
                     " shape is shown";
            return r;
        }
        r.note = why + ": a built-in shape is shown";
    } catch (...) {
        r.note = "the formation could not be read: a built-in shape is shown";
    }
    return r;
}

TeamRows team_rows(Database& db, const Table& t, int64_t teamid) {
    TeamRows out;
    const Field* ft = t.field("teamid");
    if (!ft || ft->type != FieldType::Int) {
        out.key_missing = true;
        return out;
    }
    Snapshot snap;
    if (!snap.load(db.memory(), t)) return out;
    const Field* fa = t.field("activetactic");
    bool found_active = false;
    for (uint32_t idx : snap.valid) {
        if (snap.get_int(idx, *ft) != teamid) continue;
        out.recs.push_back(snap.addr(idx));
        if (!found_active && fa && fa->type == FieldType::Int && snap.get_int(idx, *fa) == 1) {
            found_active = true;
            out.chosen = out.recs.size() - 1;
        }
    }
    out.has_active = found_active;
    return out;
}

}  // namespace turbo
