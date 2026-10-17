// FC 27 LE Turbo GUI - edits FC 27 forgets at every career load, written again by Turbo.
//
// FC 27 reloads teamkits and playernamemap from its base data whenever a career loads (core/reapply.h has the
// measurement). Teams > Colours (kit colours) and Players > Callname (player-specific callnames) keep what they wrote in
// turbo_output\reapply_edits.json; when Turbo connects to a newly loaded career (App::refresh, once per Lua session +
// load_gen: a career loaded, entered or left; not when the manager changes club, not on a Refresh) every kept entry is
// written again:
//   kits       the kit row the colours were set on (teamtechid, teamkittypetechid, teamkitid; the single row of that type
//              when the kit id is not found and no other kit of that type is kept), each colour channel through
//              Database::set (range-checked, table and record checked alive; no undo step, like any kit edit); a value
//              already in place is not written again; only the colour channels (kit_colour_field) and, since 2.3, the
//              settings a kit import wrote (Teams > Kits: kit_setting_field) are ever written;
//   callnames  through write_player_callname, the code the Callname tab uses (row edited in place; else a row taken over
//              from which no player hears a callname; never an added row: the room check would be read while the game
//              reloads the table, and Live Editor's insert crashes the game on a full table). Never for a player with
//              his own recording (the master list or the game's audio service: Callnames::own_recording): the game says
//              that recording, never a player-specific callname, so the row would only be an extra write.
//   players    (1.2.5) the Players editor's edits of attributes, overall, potential, work rates, skill moves, weak foot
//              (App::player_field_written: sliders, Overall +1/-1, a typed overall, archetypes, Undo, All fields, the
//              Database tab; in a career only). Each kept field holds the value written and the value it replaced; at a
//              career load it is written again (Database::set, range-checked, no undo step) ONLY while it holds the
//              replaced value: the game put the old value back (your players' development plans outrank the players
//              table, and every save is a new CmMgrC<timestamp> file, so an edit made before Turbo synced the plan came
//              back in every later save). A field holding anything else (the game's own growth or decline since, a
//              player of another career) is left alone. Written fields of a player of your club also go into his
//              development plan (App::queue_plan_sync). On / off: "keep_players" in the store (Turbo Tools > Kept
//              player edits), which also lists every kept field with Forget.
// Not written, without an error: a player with his own recording, a player or kit not in this career. One summary line
// goes to the GUI log, turbo_gui.log and the Colours / Callname tabs; a toast only when something was written (an
// error toast when a write failed). Kill switch: turbo_output\reapply_off.txt (nothing is written; the store is kept).
//
// 2.15 scopes (core/reapply.h header): at every career load Turbo first reads the career's identity (update_career_identity:
// the user's manager row + career_calendar.startdate; one "career identity:" line in turbo_gui.log), then writes only the
// rows that apply there - not assigned (rows of an older Turbo), global, and this career's - merged per field, this
// career's over global over not assigned. Rows of another career, or with a scope this Turbo does not know, are left
// alone (one GUI log line each, counted at the end of the summary line). New edits made in an identified career belong to
// it; Career tools > Changes & undo moves a club's kit rows, a callname or a player's edits to Global or This career.
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "ui_career_page.h"
#include "ui_identity.h"
#include "ui_theme.h"

namespace turbo {

namespace fs = std::filesystem;

static const ImVec4 kWarnColour(1.0f, 0.6f, 0.3f, 1.0f);

static std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// "teamcolorprimr" -> "teamcolorprim": the colour a channel field belongs to (Teams > Colours writes r, g and b together)
static std::string colour_of(const std::string& field) {
    const char last = field.empty() ? '\0' : field.back();
    return field.size() > 1 && (last == 'r' || last == 'g' || last == 'b') ? field.substr(0, field.size() - 1) : field;
}

void App::load_reapply() {
    const fs::path p = reapply_store_path(bridge.root());
    std::string err;
    size_t dropped = 0;
    reapply_unreadable_ = false;
    reapply_error.clear();
    if (!load_reapply_store(p, reapply, &err, &dropped)) {
        reapply_unreadable_ = true;
        reapply_error = err + ": no kept edits loaded (the file is set aside as " + reapply_unreadable_path(p).filename().string() +
                        " at the next kit colour or callname edit)";
        log("re-apply: " + reapply_error);
        return;
    }
    if (dropped) log("re-apply: " + plural(dropped, "malformed entry", "malformed entries") + " in " + p.filename().string() + " ignored");
    if (!reapply.empty())
        log("re-apply: " + plural(reapply.kits.size(), "kit", "kits") + " and " + plural(reapply.callnames.size(), "player callname", "player callnames") +
            " kept in " + p.string());
}

// 2.15: the careers rows are bound to keep a name the UI can show: this career's label, once a row is bound to it
void App::note_career_label() {
    if (reapply.career.empty() || career_label.empty()) return;
    const std::string scope = reapply.edit_scope();
    bool used = false;
    for (const auto& kv : reapply.kits) used = used || kv.second.scope == scope;
    for (const auto& kv : reapply.callnames) used = used || kv.second.scope == scope;
    for (const auto& kv : reapply.players) used = used || kv.second.scope == scope;
    if (!used) return;
    ReapplyStore::CareerInfo& c = reapply.careers[reapply.career];
    if (c.label != career_label) {
        c.label = career_label;
        c.seen = time_stamp();
    }
}

bool App::save_reapply() {
    const fs::path p = reapply_store_path(bridge.root());
    std::string err;
    note_career_label();
    if (!save_reapply_store(p, reapply, &err, reapply_unreadable_)) {
        reapply_error = "kept edits not saved: " + err;
        notify("Re-apply: " + reapply_error, true);
        return false;
    }
    if (reapply_unreadable_) log("re-apply: the unreadable store was set aside as " + reapply_unreadable_path(p).string());
    reapply_unreadable_ = false;
    reapply_error.clear();
    return true;
}

void App::remember_kit_colour(const Table& t, uint64_t rec, const std::string& prefix, const uint8_t rgb[3]) {
    // teams.teamcolor1..3 and the stadium colours are kept by the career save; only teamkits is reloaded
    if (t.name != "teamkits" || !kit_colour_field(prefix + "r")) return;
    const int64_t tid = db.get_int(t, rec, "teamtechid", -1);
    if (tid <= 0) return;
    const int64_t type = db.get_int(t, rec, "teamkittypetechid", 0);
    const int64_t kitid = db.get_int(t, rec, "teamkitid", -1);
    const std::string when = time_stamp();
    static const char ch[3] = {'r', 'g', 'b'};
    for (int c = 0; c < 3; ++c) reapply.set_kit_field(tid, type, kitid, model.team_name(tid), when, prefix + ch[c], rgb[c]);
    save_reapply();
}

bool App::remember_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field, int64_t value, bool save) {
    if (teamtechid <= 0 || kittype < 0 || !kit_kept_field(field)) return false;
    reapply.set_kit_field(teamtechid, kittype, teamkitid, model.team_name(teamtechid), time_stamp(), field, value);
    return save ? save_reapply() : true;
}

bool App::restore_kept_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field, const int64_t* value, bool save) {
    return restore_kept_kit_field_in(reapply.edit_scope(), teamtechid, kittype, teamkitid, field, value, save);
}

bool App::restore_kept_kit_field_in(const std::string& scope, int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field,
                                    const int64_t* value, bool save) {
    if (teamtechid <= 0 || kittype < 0 || !kit_kept_field(field)) return false;
    if (value) reapply.set_kit_field_in(scope, teamtechid, kittype, teamkitid, model.team_name(teamtechid), time_stamp(), field, *value);
    else reapply.forget_kit_field_in(scope, teamtechid, kittype, teamkitid, field);
    return save ? save_reapply() : true;
}

bool App::save_kept_edits() { return save_reapply(); }

bool App::remember_player_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from, std::string* why) {
    if (why) why->clear();
    if (playerid <= 0 || commentaryid <= kNoCallname || commentaryid > kCallnameMax) return false;
    // The game says a player's own recording before any callname: such a callname is never written again at a career
    // load (and an older kept one of his goes)
    if (const int own = callnames.own_recording(playerid); own && !callnames.lang_proven) {
        // the own recording is known only in a guessed language: keep the callname, drop nothing (the career-load
        // re-apply checks again once the language is proven)
        log("re-apply: player-specific callname of player " + std::to_string(playerid) + " kept: own recording in " + callnames.lang +
            " not checked (language not proven yet)");
    } else if (own) {
        const bool had = reapply.forget_callname(playerid);
        if (had) save_reapply();
        if (why)
            *why = "Not kept for the next career loads: he has his own recording in " + (callnames.lang.empty() ? std::string("this language") : callnames.lang) +
                   " (" + callnames.own_source(own) + "), which the game says instead" + (had ? " (his older kept callname is dropped too)" : "");
        log("re-apply: player-specific callname of player " + std::to_string(playerid) + " not kept: own recording (" + callnames.own_source(own) + ")");
        return false;
    }
    reapply.set_callname(playerid, commentaryid, player, from, time_stamp());
    save_reapply();
    return true;
}

bool App::forget_kit_edit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) {
    if (!reapply.forget_kit(teamtechid, kittype, teamkitid)) return false;
    log("re-apply: kit " + std::to_string(teamtechid) + " / type " + std::to_string(kittype) + " / kit id " + std::to_string(teamkitid) + " forgotten");
    return save_reapply();
}

bool App::forget_player_callname(int64_t playerid) {
    if (!reapply.forget_callname(playerid)) return false;
    log("re-apply: player-specific callname of player " + std::to_string(playerid) + " forgotten");
    return save_reapply();
}

bool App::forget_player_edit(int64_t playerid, const std::string& field) {
    if (field.empty() ? !reapply.forget_player(playerid) : !reapply.forget_player_field(playerid, field)) return false;
    log("re-apply: kept " + (field.empty() ? std::string("edits") : field) + " of player " + std::to_string(playerid) + " forgotten");
    return save_reapply();
}

bool App::forget_all_player_edits() {
    // 2.15: the rows in effect here (not assigned, global, this career's); another career's rows stay
    std::vector<ReapplyStore::RowKey> keys;
    size_t fields = 0;
    for (const auto& kv : reapply.players)
        if (reapply.rank(kv.first.second) > 0) {
            keys.push_back(kv.first);
            fields += kv.second.fields.size();
        }
    if (keys.empty()) return false;
    log("re-apply: every kept player edit in effect here forgotten (" + plural(fields, "field", "fields") + " in " + plural(keys.size(), "row", "rows") +
        "); rows of other careers are kept");
    for (const auto& k : keys) reapply.players.erase(k);
    return save_reapply();
}

bool App::move_kept_kits(int64_t teamtechid, const std::string& from, const std::string& to) {
    std::vector<ReapplyStore::KitKey> keys;
    for (const auto& kv : reapply.kits)
        if (std::get<0>(kv.first) == teamtechid && std::get<3>(kv.first) == from) keys.push_back(kv.first);
    size_t moved = 0;
    for (const auto& k : keys) moved += reapply.move_kit(k, to);
    if (!moved) return false;
    log("re-apply: " + model.team_name(teamtechid) + " (" + std::to_string(teamtechid) + "): " + plural(moved, "kit row", "kit rows") + " now " +
        reapply.scope_label(to));
    return save_reapply();
}

bool App::move_kept_callname(const ReapplyStore::RowKey& key, const std::string& to) {
    if (!reapply.move_callname(key, to)) return false;
    log("re-apply: player-specific callname of player " + std::to_string(key.first) + " now " + reapply.scope_label(to));
    return save_reapply();
}

bool App::move_kept_player(const ReapplyStore::RowKey& key, const std::string& to) {
    if (!reapply.move_player(key, to)) return false;
    log("re-apply: kept edits of player " + std::to_string(key.first) + " now " + reapply.scope_label(to));
    return save_reapply();
}

bool App::forget_kept_player_field(const ReapplyStore::RowKey& key, const std::string& field) {
    auto it = reapply.players.find(key);
    if (it == reapply.players.end() || !it->second.fields.erase(field)) return false;
    if (it->second.fields.empty()) reapply.players.erase(it);
    log("re-apply: kept " + field + " of player " + std::to_string(key.first) + " (" + reapply.scope_label(key.second) + ") forgotten");
    return save_reapply();
}

bool App::set_keep_players(bool on) {
    if (reapply.keep_players == on) return false;
    reapply.keep_players = on;
    log(std::string("re-apply: player edits ") + (on ? "kept again" : "no longer kept (the kept ones stay in the file, not written)"));
    return save_reapply();
}

std::string App::reapply_key() const {
    // load_gen changes only when a career is loaded, entered or left (Lua bridge.lua write_state); db_gen also changes
    // when the manager changes club or the GUI asks for a refresh, which is not a reload of teamkits / playernamemap
    const auto& st = bridge.state();
    return st.session + "#" + (st.load_gen >= 0 ? "L" + std::to_string(st.load_gen) : "D" + std::to_string(st.db_gen));
}

bool App::reapply_due() const {
    const auto& st = bridge.state();
    return st.loaded && st.in_cm && !reapply.empty() && reapply_key() != reapplied_key_;
}

// 2.15: the loaded career's identity (core/reapply.h header). Read once per career load (reapply_key), before the kept
// edits are written; outside a career there is none (new edits are then not assigned).
void App::update_career_identity() {
    const auto& st = bridge.state();
    reapply.career.clear();
    career_label.clear();
    if (!st.loaded || !st.in_cm) {
        career_identity_line = "no career loaded: new kept edits are not assigned to a career";
        return;
    }
    const int64_t team = st.user_team;
    const std::string club = team > 0 ? model.team_name(team) : std::string("?");
    const Table* mt = db.ready() ? db.table("manager") : nullptr;
    CareerManagerFacts m;
    int rows = 0;
    std::string why, seen;
    Snapshot snap;
    const Field* ftid = mt ? mt->field("teamid") : nullptr;
    if (!mt || !ftid || !snap.load(db.memory(), *mt)) {
        why = "FC 27's manager table is not readable";
    } else if (team <= 0) {
        why = "your club is not known (user team " + std::to_string(team) + ")";
    } else {
        const Field* fmid = mt->field("managerid");
        const Field* ffirst = mt->field("firstname");
        const Field* fsur = mt->field("surname");
        const Field* fcommon = mt->field("commonname");
        const Field* fbirth = mt->field("birthdate");
        const Field* fnat = mt->field("nationality");
        for (uint32_t i : snap.valid) {
            if (snap.get_int(i, *ftid) != team) continue;
            CareerManagerFacts r;
            r.managerid = fmid ? snap.get_int(i, *fmid) : 0;
            r.firstname = ffirst ? snap.get_str(i, *ffirst) : std::string();
            r.surname = fsur ? snap.get_str(i, *fsur) : std::string();
            r.commonname = fcommon ? snap.get_str(i, *fcommon) : std::string();
            r.birthdate = fbirth ? snap.get_int(i, *fbirth) : 0;
            r.nationality = fnat ? snap.get_int(i, *fnat) : 0;
            if (++rows == 1) m = r;
            if (rows <= 3) seen += (seen.empty() ? "" : ", ") + std::to_string(r.managerid) + " " + r.firstname + " " + r.surname;
        }
        if (rows == 0) why = "no manager row of your club (" + club + ", team " + std::to_string(team) + ")";
        else if (rows > 1)
            why = std::to_string(rows) + " manager rows of your club (" + club + ", team " + std::to_string(team) + ": " + seen + (rows > 3 ? ", ..." : "") +
                  "): which one is yours is not known";
    }
    // the career's start date (career_calendar; the same in every save of the career)
    bool has_start = false;
    int64_t start = 0;
    if (const Table* ct = db.ready() ? db.table("career_calendar") : nullptr) {
        Snapshot cs;
        if (const Field* fsd = ct->field("startdate"); fsd && cs.load(db.memory(), *ct))
            for (uint32_t i : cs.valid) {
                const int64_t v = cs.get_int(i, *fsd);
                if (v > 0 && (!has_start || v < start)) {
                    start = v;
                    has_start = true;
                }
            }
    }
    if (!why.empty()) {
        career_identity_line = "not identified: " + why + "; kept edits bound to a career are left alone and new edits are not assigned";
        log("career identity: " + career_identity_line);
        if (log_hook) log_hook("career identity: " + career_identity_line);
        return;
    }
    reapply.career = career_identity_id(m, has_start, start);
    std::string name = m.firstname + (m.firstname.empty() || m.surname.empty() ? "" : " ") + m.surname;
    if (name.empty()) name = m.commonname.empty() ? "manager " + std::to_string(m.managerid) : m.commonname;
    career_label = name + ", " + club;
    career_identity_line = reapply.career + " (" + career_label + ") from " + career_identity_text(m, has_start, start) + " | user team " +
                           std::to_string(team) + ", 1 manager row";
    log("career identity: " + career_identity_line);
    if (log_hook) log_hook("career identity: " + career_identity_line);
    // a career rows are bound to: its name follows its manager's club
    auto it = reapply.careers.find(reapply.career);
    if (it != reapply.careers.end() && it->second.label != career_label) save_reapply();
}

void App::maybe_reapply() {
    const std::string key = reapply_key();
    // the identity first, once per career load (also when nothing is kept: new edits need it)
    if (key != identity_key_) {
        identity_key_ = key;
        update_career_identity();
        team_names_publish();  // the club names in effect in this career (a career's names only in it)
    }
    // only a career database: the tables outside a career are not the ones the edits were made in
    if (!bridge.state().in_cm) return;
    // the same career load again (the Refresh button, the window shown, the manager's new club): what the user changed
    // since then stays as it is
    if (key == reapplied_key_) return;
    reapplied_key_ = key;
    if (!reapply.empty()) reapply_stored_edits();
}

std::string App::reapply_stored_edits() {
    size_t colours = 0, settings = 0, kits = 0, players = 0, in_place = 0;
    std::vector<std::string> failed;  // a write that should have happened did not: an error toast
    std::vector<std::string> left;    // not written by design (own recording, not in this career): no toast
    auto fail = [&](const std::string& why) {
        failed.push_back(why);
        log("re-apply: not written: " + why);
    };
    auto leave = [&](const std::string& why) {
        left.push_back(why);
        log("re-apply: left alone: " + why);
    };
    // 2.15: the rows that apply in this career, merged per field (this career's over global over not assigned); the others
    // are left alone and counted (one GUI log line each, never a toast: they come back at every load)
    size_t applied_rows[4] = {0, 0, 0, 0};  // by rank: [1] not assigned, [2] global, [3] this career
    size_t other_career = 0, unknown_scope = 0;
    auto skip_row = [&](const std::string& scope, const std::string& what) {
        if (scope_kind(scope) == ScopeKind::Career) ++other_career;
        else ++unknown_scope;
        log("re-apply: left alone (" + reapply.scope_label(scope) + "): " + what);
    };
    std::error_code ec;
    if (fs::exists(bridge.root() / "turbo_output" / "reapply_off.txt", ec)) {
        const std::string line = "off (turbo_output\\reapply_off.txt): " + plural(reapply.size(), "kept edit", "kept edits") + " not written";
        reapply_status = time_stamp() + ": " + line;
        log("re-apply at career load: " + line);
        if (log_hook) log_hook("re-apply at career load: " + line);
        return line;
    }

    // ---- kits: the kit row the colours were set on; every kept channel written unless it is already in place
    std::map<std::tuple<int64_t, int64_t, int64_t>, KitEdit> mkits;  // (teamtechid, type, teamkitid) -> merged fields
    std::map<std::pair<int64_t, int64_t>, size_t> mkits_of_type;
    {
        std::map<std::tuple<int64_t, int64_t, int64_t>, std::map<std::string, int>> field_rank;
        for (const auto& kv : reapply.kits) {
            const KitEdit& k = kv.second;
            const int r = reapply.rank(k.scope);
            if (r <= 0) {
                skip_row(k.scope, (k.team.empty() ? model.team_name(k.teamtechid) : k.team) + " (" + std::to_string(k.teamtechid) + ") " +
                                      kit_type_name(k.kittype) + " kit" + (k.teamkitid >= 0 ? " " + std::to_string(k.teamkitid) : std::string()));
                continue;
            }
            ++applied_rows[r];
            const auto key = std::make_tuple(k.teamtechid, k.kittype, k.teamkitid);
            KitEdit& m = mkits[key];
            if (m.fields.empty()) {
                m.teamtechid = k.teamtechid;
                m.kittype = k.kittype;
                m.teamkitid = k.teamkitid;
            }
            auto& fr = field_rank[key];
            for (const auto& f : k.fields) {
                auto it = fr.find(f.first);
                if (it == fr.end() || it->second < r) {
                    m.fields[f.first] = f.second;
                    fr[f.first] = r;
                }
            }
        }
        for (const auto& kv : mkits) ++mkits_of_type[{kv.second.teamtechid, kv.second.kittype}];
    }
    if (!mkits.empty()) {
        const Table* kt = db.table("teamkits");
        const Field* ftid = kt ? kt->field("teamtechid") : nullptr;
        const Field* ftype = kt ? kt->field("teamkittypetechid") : nullptr;
        const Field* fkid = kt ? kt->field("teamkitid") : nullptr;
        Snapshot snap;
        if (!kt || !ftid || !snap.load(db.memory(), *kt)) {
            fail(plural(mkits.size(), "kit", "kits") + ": FC 27's teamkits table is not readable");
        } else {
            std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> rows;  // (teamtechid, type) -> snapshot indexes
            for (uint32_t i : snap.valid) rows[{snap.get_int(i, *ftid), ftype ? snap.get_int(i, *ftype) : 0}].push_back(i);
            for (const auto& kv : mkits) {
                const KitEdit& k = kv.second;
                const std::string label = model.team_name(k.teamtechid) + " (" + std::to_string(k.teamtechid) + ") " + kit_type_name(k.kittype) + " kit" +
                                          (k.teamkitid >= 0 ? " " + std::to_string(k.teamkitid) : std::string());
                auto it = rows.find({k.teamtechid, k.kittype});
                if (it == rows.end()) {
                    leave(label + ": not in this career's teamkits");
                    continue;
                }
                // the row with the kit id; else the single row of that type, when no other kit of that type is kept
                // (two kits of one type: never one kit's colours on the other)
                const uint32_t* pick = nullptr;
                if (fkid && k.teamkitid >= 0)
                    for (const uint32_t& i : it->second)
                        if (snap.get_int(i, *fkid) == k.teamkitid) pick = &i;
                if (!pick && it->second.size() == 1 && mkits_of_type[{k.teamtechid, k.kittype}] == 1) pick = &it->second[0];
                if (!pick) {
                    leave(label + ": " + plural(it->second.size(), "kit", "kits") + " of this type in this career and none is kit id " + std::to_string(k.teamkitid));
                    continue;
                }
                const uint64_t rec = snap.addr(*pick);
                std::set<std::string> written;
                size_t written_settings = 0;
                std::string problem;
                for (const auto& f : k.fields) {
                    const Field* fd = kt->field(f.first);
                    // the store only holds colour channels and kit import settings; checked again here, so no other
                    // column is ever written
                    if (!kit_kept_field(f.first) || !fd || fd->type != FieldType::Int) {
                        if (problem.empty()) problem = "no kit " + std::string(kit_setting_field(f.first) ? "setting" : "colour") + " field " + f.first + " in FC 27's teamkits";
                        continue;
                    }
                    if (snap.get_int(*pick, *fd) == f.second) continue;  // already in place
                    std::string err;
                    if (!db.set(*kt, rec, *fd, Value::of_int(f.second), &err)) {
                        if (problem.empty()) problem = f.first + ": " + err;
                        continue;
                    }
                    if (kit_setting_field(f.first)) ++written_settings;
                    else written.insert(colour_of(f.first));
                }
                if (!written.empty() || written_settings) {
                    ++kits;
                    colours += written.size();
                    settings += written_settings;
                    log("re-apply: " + label + ": " + plural(written.size(), "colour", "colours") +
                        (written_settings ? " and " + plural(written_settings, "kit setting", "kit settings") : std::string()) + " written");
                } else if (problem.empty()) {
                    ++in_place;
                }
                if (!problem.empty()) fail(label + ": " + problem);
            }
        }
    }

    // ---- player-specific callnames: the Callname tab's own write (row in place / spare row), never an added row
    std::map<int64_t, const CallnameEdit*> mcallnames;  // playerid -> the row in effect here
    for (const auto& kv : reapply.callnames) {
        const CallnameEdit& c = kv.second;
        const int r = reapply.rank(c.scope);
        if (r <= 0) {
            skip_row(c.scope, "player-specific callname of " + (c.player.empty() ? std::string("player") : c.player) + " (" + std::to_string(c.playerid) + ")");
            continue;
        }
        ++applied_rows[r];
        const CallnameEdit*& slot = mcallnames[c.playerid];
        if (!slot || reapply.rank(slot->scope) < r) slot = &c;
    }
    if (!mcallnames.empty()) {
        // the language and the master list decide who has his own recording: loaded here in case the Callname tab or
        // the watcher's tick have not done it yet
        if (!callnames.refreshed && !game_root.empty()) refresh_callnames();
        if (!db.table("playernamemap")) {
            fail(plural(mcallnames.size(), "player callname", "player callnames") + ": FC 27's playernamemap table is not readable");
        } else if (callnames.lang.empty()) {
            // without a language Turbo cannot tell who has his own recording: nothing written rather than a guess
            fail(plural(mcallnames.size(), "player callname", "player callnames") +
                 ": the commentary language is not known, so own recordings cannot be checked");
        } else if (!callnames.lang_proven) {
            // the language is the packs' guess: who has his own recording is not known either -> nothing written
            fail(plural(mcallnames.size(), "player callname", "player callnames") + ": the commentary language is not proven yet (" +
                 callnames.lang + " is a guess: " + callnames.lang_why +
                 "), so own recordings cannot be checked; nothing written rather than a guess. Open the game's Create Player screen "
                 "or play a match once: Turbo then hears the language and remembers it for the next career loads");
        } else {
            // the index must describe this career's rows before any row is written
            callnames.build_index(db, model, model.names_by_id());
            for (const auto& kv : mcallnames) {
                const CallnameEdit& c = *kv.second;
                const PlayerRow* p = model.player(c.playerid);
                const std::string label = (p ? p->name : (c.player.empty() ? std::string("player") : c.player)) + " (" + std::to_string(c.playerid) + ")";
                if (!p) {
                    leave(label + ": not in this career's players");
                    continue;
                }
                if (const int own = callnames.own_recording(c.playerid)) {
                    leave(label + ": has his own recording in " + callnames.lang + " (" + callnames.own_source(own) +
                          "), which the game says instead of a player-specific callname");
                    continue;
                }
                const PlayerCallnameWrite r =
                    write_player_callname(*this, *p, c.commentaryid, c.from.empty() ? std::string("a kept edit") : c.from, false);
                switch (r.how) {
                    case PlayerCallnameWrite::How::Updated:
                    case PlayerCallnameWrite::How::TookOver:
                        ++players;
                        log("re-apply: " + r.message);
                        break;
                    case PlayerCallnameWrite::How::Unchanged:
                        ++in_place;
                        break;
                    default:  // NeedsRow, Refused (Queued never: no row is added here)
                        fail(r.message);
                        break;
                }
            }
        }
    }

    // ---- player fields (the Players editor's edits): only where the loaded value is the one the edit replaced
    size_t pfields = 0, pplayers = 0;
    std::map<int64_t, PlayerEdit> mplayers;  // playerid -> his fields in effect here
    for (const auto& kv : reapply.players) {
        const PlayerEdit& e = kv.second;
        const int r = reapply.rank(e.scope);
        if (r <= 0) {
            skip_row(e.scope, "kept edits of " + (e.player.empty() ? std::string("player") : e.player) + " (" + std::to_string(e.playerid) + ")");
            continue;
        }
        ++applied_rows[r];
        if (!mplayers.count(e.playerid)) mplayers[e.playerid] = reapply.player_in_effect(e.playerid);
    }
    if (!mplayers.empty() && !reapply.keep_players) {
        leave(plural(mplayers.size(), "player's kept edits", "players' kept edits") + ": keeping is off (Career tools > Changes & undo > Kept player edits)");
    } else if (!mplayers.empty()) {
        const Table* pt = db.table("players");
        if (!pt) {
            fail(plural(mplayers.size(), "player's kept edits", "players' kept edits") + ": FC 27's players table is not readable");
        } else {
            for (const auto& kv : mplayers) {
                const PlayerEdit& e = kv.second;
                const PlayerRow* p = model.player(e.playerid);
                const std::string label = (p ? p->name : (e.player.empty() ? std::string("player") : e.player)) + " (" + std::to_string(e.playerid) + ")";
                if (!p || !db.table_alive(*pt, p->rec)) {
                    leave(label + ": not in this career's players");
                    continue;
                }
                std::vector<std::string> wrote, moved;
                std::string problem;
                for (const auto& f : e.fields) {
                    const Field* fd = pt->field(f.first);
                    // the store only holds kept_player_field; checked again here, so no other column is ever written
                    if (!kept_player_field(f.first) || !fd || fd->type != FieldType::Int) {
                        if (problem.empty()) problem = "no field " + f.first + " in FC 27's players table";
                        continue;
                    }
                    const int64_t loaded = db.get_int(*pt, p->rec, f.first, 0);
                    const KeptField what = kept_field_state(f.second, loaded);
                    if (what == KeptField::InPlace) continue;
                    if (what == KeptField::Moved) {
                        moved.push_back(field_label(f.first) + " " + std::to_string(loaded) + " (kept " + std::to_string(f.second.value) + ")");
                        continue;
                    }
                    std::string err;
                    if (!db.set(*pt, p->rec, *fd, Value::of_int(f.second.value), &err)) {
                        if (problem.empty()) problem = f.first + ": " + err;
                        continue;
                    }
                    wrote.push_back(field_label(f.first) + " " + std::to_string(loaded) + " -> " + std::to_string(f.second.value));
                    queue_plan_sync(e.playerid, f.first);  // his development plan holds it too (your club's players)
                }
                if (!wrote.empty()) {
                    ++pplayers;
                    pfields += wrote.size();
                    model.refresh_player(e.playerid, today());
                    std::string list;
                    for (const auto& w : wrote) list += (list.empty() ? "" : ", ") + w;
                    log("re-apply: " + label + ": " + list);
                } else if (moved.empty() && problem.empty()) {
                    ++in_place;
                }
                if (!moved.empty()) {
                    std::string list;
                    for (size_t i = 0; i < moved.size() && i < 3; ++i) list += (i ? ", " : "") + moved[i];
                    if (moved.size() > 3) list += ", +" + std::to_string(moved.size() - 3) + " more";
                    leave(label + ": " + plural(moved.size(), "field", "fields") + " moved by the game since (" + list + "): not written over");
                }
                if (!problem.empty()) fail(label + ": " + problem);
            }
        }
    }

    if (kits || players || pfields) ++gen;  // cached views (kit list, Callname index, the players list) reload
    std::string line = "re-applied " + plural(colours, "kit colour", "kit colours") +
                       (settings ? " (+ " + plural(settings, "kit setting", "kit settings") + ")" : std::string()) + ", " +
                       plural(players, "player callname", "player callnames");
    // the player part only when player edits are kept (the line stays as it was for everyone else)
    if (!mplayers.empty())
        line += ", " + plural(pfields, "player field", "player fields") + (pplayers ? " (" + plural(pplayers, "player", "players") + ")" : std::string());
    if (in_place) line += " (" + std::to_string(in_place) + " already in place)";
    auto add = [&line](const char* what, const std::vector<std::string>& v) {
        if (v.empty()) return;
        line += "; " + std::to_string(v.size()) + " " + what + ": ";
        for (size_t i = 0; i < v.size() && i < 3; ++i) line += (i ? "; " : "") + v[i];
        if (v.size() > 3) line += "; +" + std::to_string(v.size() - 3) + " more (GUI log)";
    };
    add("not written", failed);
    add("left alone", left);
    // the tabs' status line and the toast: the same text as before 2.15 (the panels lay out around it)
    reapply_status = time_stamp() + ": " + line;
    // A toast when something was written or a write failed; nothing for entries left alone by design (a player with his
    // own recording, a club or player of another career): those would come back at every load
    if (colours || settings || players || pfields || !failed.empty()) notify("Kept edits: " + line, !failed.empty());
    // 2.15: where the rows came from, at the end of the logged line (the parts before it are unchanged)
    line += "; scopes (career " + (reapply.career.empty() ? std::string("not identified") : reapply.career) + "): applied " +
            std::to_string(applied_rows[1]) + " not assigned, " + std::to_string(applied_rows[2]) + " global, " + std::to_string(applied_rows[3]) +
            " this career";
    if (other_career || unknown_scope)
        line += "; left alone " + plural(other_career, "row of another career", "rows of other careers") +
                (unknown_scope ? ", " + plural(unknown_scope, "row with an unknown scope", "rows with an unknown scope") : std::string());
    log("re-apply at career load: " + line);
    if (log_hook) log_hook("re-apply at career load: " + line);
    return line;
}

// ---------------------------------------------------------------- panels
// 2.15: the start of a row's kept-edits line, short (a Forget button follows on the same line): "every career load" for a
// row not assigned yet (as before 2.15), "every career (global)", "this career"; the full scope is in Career tools > Changes &
// undo and in the line's tooltip
static std::string kept_for(const App& app, const std::string& scope) {
    switch (scope_kind(scope)) {
        case ScopeKind::NotAssigned: return "every career load";
        case ScopeKind::Global: return "every career (global)";
        default: return scope_rank(scope, app.reapply.career) > 0 ? "this career" : app.reapply.scope_label(scope);
    }
}

void reapply_status_line(App& app) {
    if (!app.reapply_error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarnColour, "Kept edits: %s", app.reapply_error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (app.reapply_status.empty()) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Kept edits at the last career load: %s", app.reapply_status.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 reloads kit colours and player-specific callnames from its base data whenever a career loads, and your\n"
                          "players' development plans put their old attributes back. Turbo keeps those edits in\n"
                          "turbo_output\\reapply_edits.json and writes them again when it connects to a newly loaded career (a player field\n"
                          "only while it holds the value the edit replaced). turbo_output\\reapply_off.txt turns this off.");
}

void reapply_kit_line(App& app, const Table& kt, uint64_t rec) {
    const int64_t tid = app.db.get_int(kt, rec, "teamtechid", -1);
    const int64_t type = app.db.get_int(kt, rec, "teamkittypetechid", 0);
    const int64_t kitid = app.db.get_int(kt, rec, "teamkitid", -1);
    const KitEdit* k = app.reapply.kit(tid, type, kitid);
    if (!k) return;
    std::set<std::string> colours;
    size_t settings = 0;
    for (const auto& f : k->fields) {
        if (kit_setting_field(f.first)) ++settings;
        else colours.insert(colour_of(f.first));
    }
    const std::string what = plural(colours.size(), "colour", "colours") + (settings ? " and " + plural(settings, "kit setting", "kit settings") : std::string());
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Kept for %s: %s%s%s", kept_for(app, k->scope).c_str(), what.c_str(), k->when.empty() ? "" : ", last edit ", k->when.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 reloads kit colours from its base data whenever a career loads: Turbo writes these again then.\n"
                          "Forget stops that; the colours shown now stay until the career is loaded again.\nApplies: %s (Career tools > Changes & undo)",
                          app.reapply.scope_label(k->scope).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget##kitkeep")) {
        if (app.forget_kit_edit(tid, type, k->teamkitid))
            app.notify(app.model.team_name(tid) + " " + kit_type_name(type) + " kit: kept colours forgotten (they last until the career is loaded again)");
    }
}

void reapply_callname_line(App& app, const PlayerRow& p) {
    const CallnameEdit* c = app.reapply.callname(p.playerid);
    if (!c) return;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Kept for %s: player-specific callname %lld%s%s%s", kept_for(app, c->scope).c_str(), static_cast<long long>(c->commentaryid),
                        c->from.empty() ? "" : " (from ", c->from.c_str(), c->from.empty() ? "" : ")");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 reloads playernamemap from its base data whenever a career loads: Turbo writes this callname again\n"
                          "then. Forget stops that; the callname stays until the career is loaded again.\nApplies: %s (Career tools > Changes & undo)",
                          app.reapply.scope_label(c->scope).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget##cnkeep")) {
        if (app.forget_player_callname(p.playerid)) app.notify(p.name + ": kept player-specific callname forgotten (it lasts until the career is loaded again)");
    }
    if (const int own = app.callnames.own_recording(p.playerid))
        ImGui::TextColored(kWarnColour, "Not written at career load: he has his own recording (%s), which the game says instead.",
                           app.callnames.own_source(own).c_str());
}

// "Overall 85 (was 80)": a kept field for the Players tab and the list
static std::string kept_text(const std::string& field, const PlayerFieldEdit& e) {
    return field_label(field) + " " + std::to_string(e.value) + " (was " + std::to_string(e.was) + ")";
}

// The field's value in the loaded career ("-" when the player is not in it)
static std::string now_text(App& app, int64_t playerid, const std::string& field) {
    const Table* t = app.db.ready() ? app.db.table("players") : nullptr;
    const PlayerRow* p = t ? app.model.player(playerid) : nullptr;
    if (!p || !t->field(field) || !app.db.table_alive(*t, p->rec)) return "-";
    return std::to_string(app.db.get_int(*t, p->rec, field, 0));
}

void reapply_player_line(App& app, const PlayerRow& p) {
    // 2.15: his fields in effect here, merged from every row that applies (this career's over global over not assigned)
    const PlayerEdit merged = app.reapply.player_in_effect(p.playerid);
    const PlayerEdit* e = merged.fields.empty() ? nullptr : &merged;
    if (e) {
        std::string list;
        size_t shown = 0;
        for (const auto& f : e->fields) {
            if (shown++ == 4) {
                list += ", +" + std::to_string(e->fields.size() - 4) + " more";
                break;
            }
            list += (list.empty() ? "" : ", ") + kept_text(f.first, f.second);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - S(90.0f));
        if (app.reapply.keep_players) ImGui::TextDisabled("Kept for %s: %s", kept_for(app, e->scope).c_str(), list.c_str());
        else ImGui::TextColored(kWarnColour, "Kept, but keeping is off (Career tools > Changes & undo > Kept player edits): %s", list.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted("Written again whenever a career loads (whatever the save is called), but only while the field holds\n"
                                   "the value the edit replaced: the game put the old value back. A value the game moved since (its own\n"
                                   "growth or decline) is left alone.");
            if (ImGui::BeginTable("##keptplayer", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Field");
                ImGui::TableSetupColumn("Kept");
                ImGui::TableSetupColumn("Was");
                ImGui::TableSetupColumn("Now");
                ImGui::TableHeadersRow();
                for (const auto& f : e->fields) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(field_label(f.first).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", static_cast<long long>(f.second.value));
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", static_cast<long long>(f.second.was));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(now_text(app, p.playerid, f.first).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Forget##playerkeep")) {
            if (app.forget_player_edit(p.playerid))
                app.notify(p.name + ": kept edits forgotten (the values stay until the career is loaded again)");
        }
    } else if (!app.reapply.keep_players) {
        ImGui::TextDisabled("Player edits are not kept for career loads (Career tools > Changes & undo > Kept player edits).");
    }
    // his edits on their way into his development plan (your club's players), and the last answer
    if (app.plan_sync_waiting() || !app.plan_sync_status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (app.plan_sync_waiting())
            ImGui::TextDisabled("Development plan: %zu player(s)' edits waiting to be sent", app.plan_sync_waiting());
        else
            ImGui::TextDisabled("Development plan, last update: %s", app.plan_sync_status.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Your club's players have a development plan that outranks the players table when a career loads.\n"
                              "Attributes, work rates, skill moves and weak foot edited here go into his plan too, so the save keeps them.");
    }
}

// Career tools > Changes & undo (GUI revamp 2.13): the one "Undo (n)" number of the shell's status bar is the size of the re-apply log
size_t changes_undo_count(const App& app) { return app.reapply.size(); }

void changes_undo_summary(App& app) {
    const size_t n = changes_undo_count(app);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Undo");
    ImGui::SameLine();
    ImGui::TextColored(n ? theme::accent() : theme::muted(), "(%zu)", n);
    ImGui::SameLine();
    theme::muted_text("kept kit edits %zu, kept callnames %zu, players with kept edits %zu", app.reapply.kits.size(), app.reapply.callnames.size(),
                      app.reapply.players.size());
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("Every edit FC 27 forgets at a career load is kept here and written again at the next load. Forget stops that for one edit; "
                      "the value you see now stays until the career is loaded again. Kit edits are forgotten from Club & Kits, callnames from the "
                      "player page; kept player edits are listed below.");
    ImGui::PopTextWrapPos();
}

void kept_player_edits_section(App& app) {
    if (!ImGui::CollapsingHeader("Kept player edits (every career load)###keptedits")) return;
    ImGui::PushID("keptplayers");
    bool on = app.reapply.keep_players;
    if (ImGui::Checkbox("Keep my player edits for every career load", &on)) app.set_keep_players(on);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Attributes, overall, potential, work rates, skill moves and weak foot you edit in the Players tab (in a career) are "
                        "kept in turbo_output\\reapply_edits.json. Whenever a career loads, whatever the save file is called, a kept "
                        "field is written again if it holds the value your edit replaced (the game put the old value back); a value "
                        "the game moved since (its own growth or decline) is left alone. Your club's players also get the value in "
                        "their development plan.");
    ImGui::PopTextWrapPos();
    reapply_status_line(app);
    if (app.reapply.players.empty()) {
        ImGui::TextDisabled("No player edits kept.");
        ImGui::PopID();
        return;
    }
    ImGui::Text("%s of %s kept", plural(app.reapply.player_fields(), "field", "fields").c_str(),
                plural(app.reapply.players.size(), "player", "players").c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget all...##keptall")) ImGui::OpenPopup("##keptforgetall");
    if (ImGui::BeginPopup("##keptforgetall")) {
        ImGui::Text("Forget every kept player edit (%s)?", plural(app.reapply.player_fields(), "field", "fields").c_str());
        ImGui::TextDisabled("The values stay as they are now; they are just no longer written again at a career load.");
        if (ImGui::Button("Forget all##keptforgetall")) {
            if (app.forget_all_player_edits()) app.notify("Every kept player edit forgotten");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##keptforgetall")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ReapplyStore::RowKey forget_key{0, std::string()};
    std::string forget_field;
    const float h = std::min(S(320.0f), ImGui::GetTextLineHeightWithSpacing() * (2.5f + static_cast<float>(app.reapply.player_fields())));
    if (ImGui::BeginTable("##keptlist", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                          ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable, ImVec2(0, h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, S(130.0f));
        ImGui::TableSetupColumn("Kept", ImGuiTableColumnFlags_WidthFixed, S(44.0f));
        ImGui::TableSetupColumn("Was", ImGuiTableColumnFlags_WidthFixed, S(44.0f));
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, S(44.0f));
        ImGui::TableSetupColumn("Applies", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(64.0f));
        ImGui::TableHeadersRow();
        for (const auto& kv : app.reapply.players) {
            const PlayerEdit& e = kv.second;
            const bool here = app.reapply.rank(e.scope) > 0;
            const PlayerRow* p = app.db.ready() ? app.model.player(e.playerid) : nullptr;
            const std::string who = (p ? p->name : (e.player.empty() ? std::string("player") : e.player)) + " (" + std::to_string(e.playerid) + ")";
            for (const auto& f : e.fields) {
                ImGui::PushID(static_cast<int>(e.playerid & 0x7FFFFFFF));
                ImGui::PushID(e.scope.c_str());
                ImGui::PushID(f.first.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(who.c_str());
                if (ImGui::IsItemHovered() && !e.when.empty()) ImGui::SetTooltip("last edit %s", e.when.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(field_label(f.first).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(f.second.value));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(f.second.was));
                ImGui::TableNextColumn();
                const std::string now = here ? now_text(app, e.playerid, f.first) : std::string("-");
                ImGui::TextUnformatted(now.c_str());
                if (ImGui::IsItemHovered()) {
                    if (!here) ImGui::SetTooltip("Bound to another career: not written in this one");
                    else if (now == "-") ImGui::SetTooltip("Not in the loaded career (or not connected)");
                    else if (now == std::to_string(f.second.value)) ImGui::SetTooltip("In place");
                    else if (now == std::to_string(f.second.was)) ImGui::SetTooltip("The old value: written again at the next career load");
                    else ImGui::SetTooltip("Moved by the game since (growth or decline): left alone");
                }
                ImGui::TableNextColumn();
                const std::string where = app.reapply.scope_label(e.scope);
                ImGui::TextUnformatted(where.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", where.c_str());
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Forget")) {
                    forget_key = kv.first;
                    forget_field = f.first;
                }
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    // after the loop: the map is not changed while it is drawn
    if (forget_key.first && app.forget_kept_player_field(forget_key, forget_field))
        app.notify(field_label(forget_field) + " of player " + std::to_string(forget_key.first) + ": no longer kept");
    ImGui::PopID();
}

// 2.15 (Changes & undo): where each kept row applies - a club's kit rows, a callname, a player's edits - with Global / This
// career. Rows of an older Turbo are "not assigned yet" and keep being written in every career until moved.
void kept_edit_scopes_section(App& app) {
    if (!ImGui::CollapsingHeader("Where kept edits apply (careers)")) return;
    ImGui::PushID("keptscopes");
    const bool identified = !app.reapply.career.empty();
    const std::string here = identified ? app.reapply.edit_scope() : std::string();
    ImGui::PushTextWrapPos(0.0f);
    if (identified) ImGui::Text("This career: %s", app.career_label.c_str());
    else ImGui::TextColored(kWarnColour, "This career: %s", app.career_identity_line.empty() ? "not identified yet" : app.career_identity_line.c_str());
    if (ImGui::IsItemHovered() && !app.career_identity_line.empty()) ImGui::SetTooltip("career identity: %s", app.career_identity_line.c_str());
    ImGui::TextDisabled("Global rows are written at every career load; This career rows only when this career loads (whatever the save is "
                        "called). Rows kept by an older Turbo are not assigned yet and are still written in every career. New edits made in "
                        "a career belong to it.");
    ImGui::PopTextWrapPos();
    if (app.reapply.empty() && app.team_names.entries.empty()) {
        ImGui::TextDisabled("No kept edits.");
        ImGui::PopID();
        return;
    }
    // what a click asks for, done after the table (the maps are not changed while they are drawn)
    enum class What { None, Kits, Callname, Player, TeamName };
    What act = What::None;
    int64_t act_team = 0;
    std::string act_from, act_to;
    ReapplyStore::RowKey act_key{0, std::string()};
    auto scope_buttons = [&](const std::string& scope, What what, int64_t team, const ReapplyStore::RowKey& key) {
        ImGui::BeginDisabled(scope == kScopeGlobal);
        if (ImGui::SmallButton("Global")) {
            act = what;
            act_team = team;
            act_key = key;
            act_from = scope;
            act_to = kScopeGlobal;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!identified || scope == here);
        if (ImGui::SmallButton("This career")) {
            act = what;
            act_team = team;
            act_key = key;
            act_from = scope;
            act_to = here;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", identified ? ("Only when this career loads: " + app.career_label).c_str() : "No career identified: load a career first");
    };
    // a club's kit rows of one scope: one line
    struct ClubKits {
        int64_t team = 0;
        std::string scope, name;
        size_t rows = 0;
        std::set<std::string> types;
    };
    std::map<std::pair<int64_t, std::string>, ClubKits> clubs;
    for (const auto& kv : app.reapply.kits) {
        const KitEdit& k = kv.second;
        ClubKits& c = clubs[{k.teamtechid, k.scope}];
        c.team = k.teamtechid;
        c.scope = k.scope;
        if (c.name.empty()) c.name = k.team.empty() ? app.model.team_name(k.teamtechid) : k.team;
        ++c.rows;
        c.types.insert(kit_type_name(k.kittype));
    }
    const float h = std::min(S(360.0f), ImGui::GetTextLineHeightWithSpacing() * (2.5f + static_cast<float>(clubs.size() + app.reapply.callnames.size() +
                                                                                                           app.reapply.players.size() + app.team_names.entries.size())));
    if (ImGui::BeginTable("##scopelist", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                            ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable, ImVec2(0, h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Kept", ImGuiTableColumnFlags_WidthFixed, S(90.0f));
        ImGui::TableSetupColumn("Club / player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Applies", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(170.0f));
        ImGui::TableHeadersRow();
        auto applies = [&](const std::string& scope) {
            const std::string where = app.reapply.scope_label(scope);
            if (app.reapply.rank(scope) > 0) ImGui::TextUnformatted(where.c_str());
            else ImGui::TextColored(theme::muted(), "%s", where.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", app.reapply.rank(scope) > 0 ? "written when this career loads" : "not written in this career");
        };
        for (const auto& kv : clubs) {
            const ClubKits& c = kv.second;
            ImGui::PushID(("k" + std::to_string(c.team) + "|" + c.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Kits");
            ImGui::TableNextColumn();
            std::string types;
            for (const auto& t : c.types) types += (types.empty() ? "" : ", ") + t;
            ImGui::Text("%s (%lld): %s (%s)", c.name.c_str(), static_cast<long long>(c.team), plural(c.rows, "kit row", "kit rows").c_str(), types.c_str());
            ImGui::TableNextColumn();
            applies(c.scope);
            ImGui::TableNextColumn();
            scope_buttons(c.scope, What::Kits, c.team, ReapplyStore::RowKey{0, std::string()});
            ImGui::PopID();
        }
        // club names (turbo_output\team_names.json, shown by the live hook): the same scopes
        for (const auto& e : app.team_names.entries) {
            ImGui::PushID(("n" + std::to_string(e.teamid) + "|" + e.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Club name");
            ImGui::TableNextColumn();
            ImGui::Text("%s (%lld)", e.text[tnames::Full].c_str(), static_cast<long long>(e.teamid));
            ImGui::TableNextColumn();
            applies(e.scope);
            ImGui::TableNextColumn();
            scope_buttons(e.scope, What::TeamName, e.teamid, ReapplyStore::RowKey{0, std::string()});
            ImGui::PopID();
        }
        for (const auto& kv : app.reapply.callnames) {
            const CallnameEdit& cn = kv.second;
            ImGui::PushID(("c" + std::to_string(cn.playerid) + "|" + cn.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Callname");
            ImGui::TableNextColumn();
            ImGui::Text("%s (%lld): %lld", cn.player.empty() ? "player" : cn.player.c_str(), static_cast<long long>(cn.playerid),
                        static_cast<long long>(cn.commentaryid));
            ImGui::TableNextColumn();
            applies(cn.scope);
            ImGui::TableNextColumn();
            scope_buttons(cn.scope, What::Callname, 0, kv.first);
            ImGui::PopID();
        }
        for (const auto& kv : app.reapply.players) {
            const PlayerEdit& e = kv.second;
            ImGui::PushID(("p" + std::to_string(e.playerid) + "|" + e.scope).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Player edits");
            ImGui::TableNextColumn();
            ImGui::Text("%s (%lld): %s", e.player.empty() ? "player" : e.player.c_str(), static_cast<long long>(e.playerid),
                        plural(e.fields.size(), "field", "fields").c_str());
            ImGui::TableNextColumn();
            applies(e.scope);
            ImGui::TableNextColumn();
            scope_buttons(e.scope, What::Player, 0, kv.first);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    bool done = false;
    switch (act) {
        case What::Kits: done = app.move_kept_kits(act_team, act_from, act_to); break;
        case What::Callname: done = app.move_kept_callname(act_key, act_to); break;
        case What::Player: done = app.move_kept_player(act_key, act_to); break;
        case What::TeamName: done = app.move_team_name(act_team, act_from, act_to); break;
        default: break;
    }
    if (done) app.notify("Kept edits: now " + app.reapply.scope_label(act_to));
    ImGui::PopID();
}

}  // namespace turbo
