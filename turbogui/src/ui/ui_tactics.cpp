// FC 27 LE Turbo GUI - Teams > Tactics (see ui_tactics.h).
//
// Frame-loop rules (plan section 9): an exception in App::draw disables the overlay for the session, so the tab body is wrapped
// (inline error, never a throw); the pitch model is cached by app.gen, the formation and the slider values (core PreviewCache); the saved
// formation and the opposition profile are read again only when app.gen, the team or an input changed; no file I/O in the frame path (the
// kill switch is looked at every 2 s, and again at once when a write is asked for; preset files are read and written on a click only); the
// pitch is capped at kPitchPrimitiveCap primitives.
#include "ui_tactics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

#include "app.h"
#include "core/opposition.h"
#include "core/sliders.h"
#include "core/tactic_profiles.h"
#include "core/tactics.h"
#include "core/tactics_db.h"
#include "core/tactics_status.h"
#include "file_picker.h"
#include "pitch_view.h"
#include "ui_live_tactic.h"
#include "ui_shell.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;

// ---- panel state
struct OppState {
    bool on = true;        // opposition variety on (off = the "none" mode: an empty profile)
    bool home = true;      // the opponent plays at home
    int salt = 0;          // "Re-roll" bumps it: another seeded wobble for the same fixture
    std::string sig;       // what the profile below was computed for
    bool ready = false;    // a profile was solved
    OppProfile profile;
    SliderSet ghost;       // the opponent's team sliders with the solver's changes (the Opposition scope's pitch)
    std::string opp_name;
    TeamFormation opp_tf;  // the opponent's formation (the selected club)
    struct Live {
        std::string key, target;
        int base = 0, offset = 0, value = 0;
        bool base_from_slider = false;
    };
    std::vector<Live> live;  // what Apply would send
    TacticsTabState::OppCard card;  // what the card shows (kept between frames: the profile is only solved again when an input changed)
};

// The values of the database sliders that write one club's rows (teams: My team) or one player's row (players: Position/Role), put
// aside while another club or player is selected (sync_owners)
struct OwnedValues {
    SliderSet working, applied;
};

struct TacState {
    int scope = 0;  // SliderScope index
    SliderSet working;   // what the sliders hold now (the club and player rows: the selected club's and player's values only)
    SliderSet applied;   // what was last applied (the base for "N changed" and Revert)
    // whose club and player rows `working` / `applied` hold, and the values of every other club and player (sync_owners)
    int64_t team_owner = -1, player_owner = -1;
    std::map<int64_t, OwnedValues> team_values, player_values;
    bool dry_run = false;
    bool show_derived = true, show_heat = false, show_roam = false, show_risk = false;
    int phase = 2;       // Phase index
    int formation_pick = -1;  // -1 = the selected team's own formation, else an index into fallback_formations()
    int selected_slot = -1;   // pitch dot whose position sliders apply
    // the saved formation of the selected team (read again when the team or app.gen changes)
    int64_t form_team = -1;
    int form_gen = -1;
    TeamFormation tf;
    std::string selected_profile;  // id of the preset last loaded
    bool compare = false;
    char preset_name[48] = "";
    char rename_buf[48] = "";
    std::string error;   // inline error of the exception-proof draw
    bool off = false;    // tactics_off.txt, the master switch (looked at every 2 s)
    SwitchState sw;      // every kill switch file present (core/tactics_status.h)
    double next_check = -1.0;
    int live_written = 0;  // game variables this panel set (cleared when Turbo unloads)
    std::vector<std::string> writes_done;  // what the last Apply wrote, one line per DB row (shown until the next Apply)
    bool exp_open = false;                 // 2.5: the "Experimental" section (the preview-only and untested rows) is open
    bool show_legacy = false;              // "show legacy fields"
    // the snapshot store (the value of a database field before Turbo's first write), loaded at the first look at the kill switches (the
    // panel's file I/O slot), saved on every change
    TacticSnapshots snaps;
    bool snaps_loaded = false;
    std::string snaps_error;
    // tactics_off.txt appeared (or was there when Turbo started): the database rows Turbo wrote in this career go back, once a career
    // database is connected (TACTICS_REPORT.txt P0-9)
    bool restore_pending = false;
    // the read-back cache (database rows are read again when the save, the team or the player changed, else every 2 s)
    std::map<std::string, std::string> readback;
    int rb_gen = -1;
    int64_t rb_team = -1, rb_player = -1;
    double rb_next = -1.0;
    // "Apply untested settings?" (asked once per Apply that holds untested or legacy writes)
    bool confirm_request = false;
    std::vector<std::string> confirm_lines;
    PreviewCache preview;
    OppState opp;
    // preset dialogs
    int export_what = 0;  // 0 this preset, 1 my presets of this scope, 2 all my presets
    FilePicker export_fp, import_fp;
    ImportPlan plan;
    std::vector<int> plan_actions;  // ClashAction per plan item
    bool plan_open_request = false;
    bool plan_open = false;
};
TacState g_tac;
bool g_force_exp = false;  // tests: the Experimental section stays open (tactics_set_experimental_open)
TacticsTabState g_state;

const char* const kScopeLabels[4] = {"Match (both teams)", "My team", "Position/Role", "Opposition"};
const SliderScope kScopes[4] = {SliderScope::Match, SliderScope::Team, SliderScope::Position, SliderScope::Opposition};

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }
fs::path off_file(App& app) { return out_dir(app) / kill_switches().front().file; }

ProfileCategory category_of(SliderScope s) {
    switch (s) {
        case SliderScope::Match: return ProfileCategory::Match;
        case SliderScope::Team: return ProfileCategory::Team;
        case SliderScope::Position: return ProfileCategory::Position;
        case SliderScope::Opposition: return ProfileCategory::Opposition;
    }
    return ProfileCategory::Team;
}

std::string fmtf(const char* f, double a, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

std::string stamp() {
    std::time_t tt = std::time(nullptr);
    char buf[32] = "";
    if (const std::tm* tm = std::localtime(&tt)) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
}

// The snapshot store, read once (at the first look at the kill switches, else by the first user action that needs it)
TacticSnapshots& snapshots(App& app) {
    if (!g_tac.snaps_loaded) {
        g_tac.snaps_loaded = true;
        std::string err;
        if (!load_tactic_snapshots(tactics_snapshot_path(app.bridge.root()), g_tac.snaps, &err)) g_tac.snaps_error = err;
    }
    return g_tac.snaps;
}

void persist_snapshots(App& app) {
    std::string err;
    if (!save_tactic_snapshots(tactics_snapshot_path(app.bridge.root()), g_tac.snaps, &err)) app.notify("Tactics snapshot not saved: " + err, true);
}

std::string name_of(const SliderDef& d, int v) {
    const long idx = long(v) - long(d.min);
    if (idx >= 0 && size_t(idx) < d.labels.size() && !d.labels[size_t(idx)].empty()) return d.labels[size_t(idx)];
    return std::to_string(v);
}

std::string signed_text(int v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%+d", v);
    return b;
}

std::vector<int> legal_values(const SliderDef& d) {
    std::vector<int> v;
    if (!d.allowed.empty()) return d.allowed;
    for (int i = d.min; i <= d.max && v.size() < 64; i += std::max(1, d.step)) v.push_back(i);
    return v;
}

bool value_changed(const SliderDef& d) {
    const SliderSet& w = g_tac.working;
    const SliderSet& a = g_tac.applied;
    const bool we = w.is_enabled(d.key), ae = a.is_enabled(d.key);
    if (we != ae) return true;
    return we && w.value_or_default(d.key) != a.value_or_default(d.key);
}

const Profile* loaded_profile(App& app) {
    if (g_tac.selected_profile.empty()) return nullptr;
    return app.tactic_profiles.find(g_tac.selected_profile);
}

// ---------------------------------------------------------------------------------------------------------------- whose rows
// A database value is bound to the club or player it was set for. A teams value (My team) writes the selected club's row
// and a players value (Position/Role) the selected player's row, so when the selection changes the values of the club or player left
// (switched on, last applied) are put aside and the newly selected one's come back. Apply, Reset and switching a row off + Apply then
// only reach the rows of the club or player the values were set for: selecting the opponent (the Opposition scope asks for it) or
// another player never sends your settings into his row, and the row Turbo wrote keeps its applied state for its own Reset.
enum class RowOwner { None, Team, Player };

RowOwner owner_of_target(const std::string& target) {
    const std::string table = target.substr(0, target.find('.'));
    if (table == "teams") return RowOwner::Team;
    if (table == "players") return RowOwner::Player;
    return RowOwner::None;  // a game variable, every referee, a formation slot (not written)
}

RowOwner owner_of(const SliderDef& d) {
    if (d.status != SliderStatus::DB || d.binding.targets.empty()) return RowOwner::None;
    return owner_of_target(d.binding.targets.front());
}

// Moves the values of `o`'s sliders from one set to another (the flag and the value; `from` is left without them)
void move_owned(SliderSet& from, SliderSet& to, RowOwner o) {
    for (const SliderDef& d : slider_registry()) {
        if (owner_of(d) != o) continue;
        auto it = from.values.find(d.key);
        if (it != from.values.end()) to.values[d.key] = it->second;
        if (from.is_enabled(d.key)) to.enabled.insert(d.key);
        from.reset(d.key);
    }
}

void switch_owner(RowOwner o, int64_t& owner, int64_t now, std::map<int64_t, OwnedValues>& aside) {
    if (owner == now) return;
    OwnedValues left;
    move_owned(g_tac.working, left.working, o);
    move_owned(g_tac.applied, left.applied, o);
    if (left.working.empty() && left.applied.empty()) aside.erase(owner);
    else aside[owner] = std::move(left);
    owner = now;
    auto it = aside.find(now);
    if (it == aside.end()) return;
    move_owned(it->second.working, g_tac.working, o);
    move_owned(it->second.applied, g_tac.applied, o);
    aside.erase(it);
}

// Called at the top of every frame of the tab and before every write: the club and player rows hold the selected one's values
void sync_owners(App& app) {
    switch_owner(RowOwner::Team, g_tac.team_owner, app.sel_team, g_tac.team_values);
    switch_owner(RowOwner::Player, g_tac.player_owner, app.sel_player, g_tac.player_values);
}

// Whose row a target writes, for the user: the selected club or player, every referee ("" = a game variable). Cheap (the frame path).
std::string row_owner(App& app, const std::string& target) {
    switch (owner_of_target(target)) {
        case RowOwner::Team: {
            const TeamRow* t = app.model.team(app.sel_team);
            return t ? t->name : "no club selected";
        }
        case RowOwner::Player: {
            const PlayerRow* p = app.model.player(app.sel_player);
            return p ? p->name : "no player selected";
        }
        case RowOwner::None: break;
    }
    if (target.rfind("referee.", 0) == 0) return "every referee";
    return "";
}

std::string for_owner(App& app, const std::string& target) {
    const std::string who = row_owner(app, target);
    return who.empty() ? "" : " for " + who;
}

// ---------------------------------------------------------------------------------------------------------------- formation
// The selected team's saved formation, read when the team or app.gen changes (a cached read, never per frame).
const TeamFormation& team_formation(App& app) {
    if (g_tac.form_team != app.sel_team || g_tac.form_gen != app.gen) {
        if (g_tac.form_team != app.sel_team) {
            g_tac.formation_pick = -1;
            g_tac.selected_slot = -1;
        }
        g_tac.form_team = app.sel_team;
        g_tac.form_gen = app.gen;
        g_tac.tf = read_team_formation(app.db, app.sel_team);
    }
    return g_tac.tf;
}

const Formation& shown_formation(App& app) {
    const TeamFormation& tf = team_formation(app);
    const auto& fb = fallback_formations();
    if (g_tac.formation_pick >= 0 && size_t(g_tac.formation_pick) < fb.size()) return fb[size_t(g_tac.formation_pick)];
    return tf.formation;
}

// ---------------------------------------------------------------------------------------------------------------- writes
// Badge colours: the write path in blue tones (nothing green: "it writes" is not "it works"), what was seen in green / orange / grey
ImVec4 written_colour(WriteKind k) {
    switch (k) {
        case WriteKind::GameVar: return ImVec4(0.55f, 0.75f, 1.0f, 1);
        case WriteKind::DbRow: return ImVec4(0.45f, 0.65f, 0.95f, 1);
        case WriteKind::Preview: return ImVec4(0.7f, 0.7f, 0.7f, 1);
        case WriteKind::TurboOnly: return ImVec4(0.8f, 0.7f, 1.0f, 1);
        case WriteKind::LiveSheet: return ImVec4(0.5f, 0.8f, 0.95f, 1);
    }
    return ImVec4(1, 1, 1, 1);
}

ImVec4 seen_colour(SeenKind k) {
    switch (k) {
        case SeenKind::SeenInMatch: return ImVec4(0.45f, 0.85f, 0.45f, 1);
        case SeenKind::NoEffectSeen: return ImVec4(1.0f, 0.6f, 0.3f, 1);
        case SeenKind::Untested: return ImVec4(0.9f, 0.8f, 0.4f, 1);
        case SeenKind::Legacy: return ImVec4(0.75f, 0.6f, 0.5f, 1);
        case SeenKind::NothingWritten: return ImVec4(0.6f, 0.6f, 0.6f, 1);
    }
    return ImVec4(1, 1, 1, 1);
}

// The channel of a slider's first target (every target of one slider goes through the same channel today)
WriteChannel channel_of_def(const SliderDef& d) {
    if (d.binding.targets.empty()) return WriteChannel::GameVar;
    return channel_of_target(d.binding.targets.front());
}

// The CPU-side game variables an opposition offset is added to, and the Match slider that holds the base (else the registry default)
const char* base_slider_of(const std::string& target) {
    if (target == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI") return "match.injury_frequency_cpu";
    if (target == "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPUAI") return "match.injury_severity_cpu";
    if (target == "OVERRIDE_MATCH_DIFFICULTY") return "match.difficulty";
    return nullptr;
}

int base_for(const SliderSet& s, const std::string& target, bool* from_slider = nullptr) {
    const char* k = base_slider_of(target);
    const SliderDef* d = k ? find_slider(k) : nullptr;
    if (!d) return 0;
    const bool on = s.is_enabled(k);
    if (from_slider) *from_slider = on;
    return on ? s.value_or_default(k) : d->def;
}

int clamp_var(const std::string& target, int v) {
    if (const gv::KnownVar* kv = gv::known(target)) return std::clamp<int>(v, kv->min, kv->max);
    return v;
}

// One write the game would get. An opposition offset is added to its base (the Match slider, else the registry default), never written raw.
struct EffWrite {
    SliderWrite w;
    std::string note;  // "base 50 + offset +8"
};

// The writes of a slider set that this build can send, in registry order, one per target (an opposition offset replaces the Match slider of
// the same variable); `unsent` gets the switched-on settings it cannot send (a formation slot offset needs a slot and a formation row) and
// the dead targets (your saved tactic row: core/tactics_status.h dead_target_reason), which no build writes.
std::vector<EffWrite> effective_writes(const SliderSet& s, std::vector<std::string>* unsent = nullptr) {
    std::vector<EffWrite> out;
    for (const SliderWrite& w : game_writes(s)) {
        if (w.target.find("{n}") != std::string::npos) {
            if (unsent) unsent->push_back(w.target + ": this build does not write formation slots");
            continue;
        }
        if (const char* dead = dead_target_reason(w.target)) {
            if (unsent) unsent->push_back(w.target + ": " + dead);
            continue;
        }
        EffWrite e{w, ""};
        if (w.status == SliderStatus::Live && w.key.rfind("opp.", 0) == 0) {
            const int base = base_for(s, w.target);
            e.w.value = clamp_var(w.target, base + w.value);
            e.note = "base " + std::to_string(base) + " + offset " + signed_text(w.value);
        }
        bool replaced = false;
        for (EffWrite& x : out)
            if (x.w.target == e.w.target) {
                x = e;
                replaced = true;
            }
        if (!replaced) out.push_back(e);
    }
    return out;
}

// One DB write of "table.field" through tactics_db_set (App::edit_field / App::edit, Database::set: range check, table_alive; undo steps for players; never saves).
// Skipped = this build or this save cannot say where to write (not a failure of the write itself); Failed = the write was refused.
enum class DbOutcome { Ok, Skipped, Failed };

// The row(s) a "table.field" target means for the current selection: teams = the selected club, players = the selected player, referee =
// every referee (2.4: the old mentalities table is no write target any more). Each record carries its key in the
// snapshot store, which names the loaded career's club ("club:1|referee:155": core/tactics_status.h snapshot_row). Your saved tactic row
// (cm_mentalities) is never a write target. A failure leaves `outcome` set and `why` said.
struct DbTarget {
    const Table* t = nullptr;
    const Field* f = nullptr;
    std::string table, field;
    std::vector<std::pair<uint64_t, std::string>> recs;  // record, snapshot row key
    std::string label;                                   // club or player name for the user
    std::string what;                                    // what a write will report ("" = the plain "table.field = v")
    std::string why;                                     // "" = resolved
    DbOutcome outcome = DbOutcome::Ok;
};

// The loaded career's club: the snapshot rows are kept per club so a value kept in one career never goes into another
int64_t career_club(App& app) { return app.bridge.state().user_team; }

DbTarget resolve_db(App& app, const std::string& target) {
    DbTarget r;
    auto fail = [&](DbOutcome o, const std::string& why) {
        r.outcome = o;
        r.why = why;
        return r;
    };
    const size_t dot = target.find('.');
    if (dot == std::string::npos) return fail(DbOutcome::Failed, "no field named in " + target);
    r.table = target.substr(0, dot);
    r.field = target.substr(dot + 1);
    if (r.field.find("{n}") != std::string::npos) return fail(DbOutcome::Skipped, "formation slots are not written by this build");
    r.t = app.db.table(r.table);
    if (!r.t) return fail(DbOutcome::Skipped, "this save has no " + r.table + " table");
    r.f = r.t->field(r.field);
    if (!r.f) return fail(DbOutcome::Skipped, r.table + " has no " + r.field + " field in this save");
    const int64_t club = career_club(app);
    if (r.table == "teams") {
        const TeamRow* tr = app.model.team(app.sel_team);
        if (!tr) return fail(DbOutcome::Failed, "select a team first");
        r.label = tr->name;
        r.recs.push_back({tr->rec, snapshot_row(club, "team:" + std::to_string(tr->teamid))});
    } else if (r.table == "players") {
        const PlayerRow* p = app.model.player(app.sel_player);
        if (!p) return fail(DbOutcome::Failed, "select a player first (Position/Role writes the selected player)");
        r.label = p->name;
        r.recs.push_back({p->rec, snapshot_row(club, "player:" + std::to_string(p->playerid))});
    } else if (r.table == "referee") {
        Snapshot snap;  // every referee row (a user action, not the frame path)
        if (snap.load(app.db.memory(), *r.t))
            for (uint32_t i : snap.valid) {
                const uint64_t rec = snap.addr(i);
                r.recs.push_back({rec, snapshot_row(club, "referee:" + std::to_string(app.db.get_int(*r.t, rec, "refereeid", int64_t(i))))});
            }
        if (r.recs.empty()) return fail(DbOutcome::Failed, "the referee table has no rows");
        r.label = std::to_string(r.recs.size()) + " referees";
    } else {
        return fail(DbOutcome::Skipped, "which row of " + r.table + " to write is not resolved in this build");
    }
    return r;
}

// Every database write of the tab and every put-back (Reset, a row switched off + Apply, tactics_off.txt) goes through here. A tactic is
// a match setting, not a player edit: a players field other than the overall goes through App::edit_field, so
// players.preferredposition1 written or put back by the tab never moves the stored overall (App::edit would make it follow,
// ui_players.cpp overall_follow_edit, and the put-back of a position Turbo wrote would move it by the formula's difference between
// the two positions). An A/B run that writes the baseline back, and every way of undoing a write, leave the overall as it was.
bool tactics_db_set(App& app, const Table& t, uint64_t rec, const Field& f, int64_t value) {
    const Value v = Value::of_int(value);
    const bool plain = t.name == "players" && f.name != "overallrating";
    return plain ? app.edit_field(t, rec, f, v) : app.edit(t, rec, f, v);
}

// Before the first write to a row, its value is kept (turbo_output\tactics_snapshot.json) so Reset can put it back
DbOutcome write_db(App& app, const SliderWrite& w, std::string& why, std::string& what) {
    // the club tactic's teams.* fields: gate X2 decides who writes them (core/tactics_status.h teams_tactic_route, the one place)
    if (const char* not_here = teams_tactic_refusal(w.target)) {
        why = not_here;
        return DbOutcome::Skipped;
    }
    DbTarget r = resolve_db(app, w.target);
    if (r.outcome != DbOutcome::Ok) {
        why = r.why;
        return r.outcome;
    }
    TacticSnapshots& snaps = snapshots(app);
    bool kept = false;
    for (const auto& rec : r.recs) {
        if (!app.db.table_alive(*r.t, rec.first)) {
            why = "the database changed (another save loaded?): press Refresh";
            return DbOutcome::Failed;
        }
        Value before;
        if (app.db.get(*r.t, rec.first, *r.f, before) && before.type == FieldType::Int)
            kept = snaps.remember(w.target, rec.second, before.i, stamp(), r.label) || kept;
        if (!tactics_db_set(app, *r.t, rec.first, *r.f, w.value)) {
            if (kept) persist_snapshots(app);
            why = "the game's database refused the value";
            return DbOutcome::Failed;
        }
        kept = snaps.note_written(w.target, rec.second, w.value) || kept;
    }
    if (kept) persist_snapshots(app);
    what = w.target + " = " + std::to_string(w.value) + (r.label.empty() ? "" : " for " + r.label) + r.what;
    return DbOutcome::Ok;
}

// Reset to default for a database slider: every target's row(s) get the value kept before Turbo's first write; rows Turbo never wrote are
// left alone. Returns the rows restored; `why` says what could not be done.
int restore_db(App& app, const SliderDef& d, std::string& why) {
    int restored = 0;
    TacticSnapshots& snaps = snapshots(app);
    bool changed = false;
    for (const std::string& target : d.binding.targets) {
        if (dead_target_reason(target)) continue;
        DbTarget r = resolve_db(app, target);
        if (r.outcome != DbOutcome::Ok) {
            if (why.empty()) why = r.why;
            continue;
        }
        for (const auto& rec : r.recs) {
            const DbSnapshot* s = snaps.find(target, rec.second);
            if (!s) continue;
            if (!app.db.table_alive(*r.t, rec.first)) {
                why = "the database changed (another save loaded?): press Refresh";
                continue;
            }
            Value cur;
            if (app.db.get(*r.t, rec.first, *r.f, cur) && cur.type == FieldType::Int && !TacticSnapshots::may_restore(*s, cur.i)) {
                // another save, the game or the user changed the row since Turbo wrote it: Reset undoes only Turbo's own write
                if (why.empty())
                    why = target + ": the row changed since Turbo wrote it (now " + std::to_string(cur.i) + ", Turbo wrote " + std::to_string(s->written) +
                          "): not restored";
                continue;
            }
            if (tactics_db_set(app, *r.t, rec.first, *r.f, s->value)) {
                ++restored;
                snaps.forget(target, rec.second);
                changed = true;
            } else if (why.empty()) {
                why = "the game's database refused the value kept for " + target;
            }
        }
    }
    if (changed) persist_snapshots(app);
    return restored;
}

// The master kill switch appeared: every row this career's club has in the snapshot store goes back to the value before Turbo's first
// write, but only while the row still holds the value Turbo wrote (core/tactics_status.h restore_verdict). Rows of another career's club,
// rows changed since and rows Turbo never wrote are left as they are (the first two stay kept). Not limited to the selected club or player.
struct RestoreAll {
    int restored = 0, changed_since = 0, other_career = 0, not_found = 0, refused = 0;
};

RestoreAll restore_all_db(App& app) {
    RestoreAll out;
    TacticSnapshots& snaps = snapshots(app);
    const int64_t club = career_club(app);
    std::vector<DbSnapshot> rows;
    for (const auto& kv : snaps.items) rows.push_back(kv.second);
    bool changed = false;
    for (const DbSnapshot& s : rows) {
        SnapshotRow sr;
        const size_t dot = s.target.find('.');
        if (!parse_snapshot_row(s.row, sr) || dot == std::string::npos || dead_target_reason(s.target)) {
            ++out.not_found;
            continue;
        }
        if (sr.club != club) {
            ++out.other_career;
            continue;
        }
        const Table* t = app.db.table(s.target.substr(0, dot));
        const Field* f = t ? t->field(s.target.substr(dot + 1)) : nullptr;
        const char* key = snapshot_key_field(sr.kind);
        const uint64_t rec = (t && f && key[0]) ? app.db.find(*t, key, sr.id) : 0;
        Value cur;
        if (!rec || !app.db.get(*t, rec, *f, cur) || cur.type != FieldType::Int) {
            ++out.not_found;
            continue;
        }
        switch (restore_verdict(s, club, cur.i)) {
            case RestoreVerdict::Restore:
                if (tactics_db_set(app, *t, rec, *f, s.value)) {
                    ++out.restored;
                    snaps.forget(s.target, s.row);
                    changed = true;
                } else {
                    ++out.refused;
                }
                break;
            case RestoreVerdict::NeverWritten:  // Turbo's write never landed: the row holds what it had, nothing to undo
                snaps.forget(s.target, s.row);
                changed = true;
                break;
            case RestoreVerdict::ChangedSince: ++out.changed_since; break;
            case RestoreVerdict::OtherCareer: ++out.other_career; break;
            case RestoreVerdict::NotARow: ++out.not_found; break;
        }
    }
    if (changed) persist_snapshots(app);
    return out;
}

void restore_on_kill_switch(App& app) {
    if (!app.connected()) return;  // stays pending until a career database is there
    g_tac.restore_pending = false;
    const RestoreAll r = restore_all_db(app);
    if (r.restored == 0 && r.changed_since == 0 && r.refused == 0) return;
    std::string msg = "Tactics are off (turbo_output\\tactics_off.txt): " + std::to_string(r.restored) +
                      " database row(s) put back to the value before Turbo's first write";
    if (r.changed_since) msg += ", " + std::to_string(r.changed_since) + " row(s) changed since Turbo wrote them left as they are";
    if (r.refused) msg += ", " + std::to_string(r.refused) + " refused by the game's database";
    app.notify(msg, r.refused > 0);
}

// What the game holds now for a database target, for the row's read-back line
std::string db_readback(App& app, const std::string& target) {
    if (dead_target_reason(target)) return "";
    DbTarget r = resolve_db(app, target);
    if (r.outcome != DbOutcome::Ok) return "no read-back: " + r.why;
    int64_t lo = 0, hi = 0;
    size_t n = 0;
    for (const auto& rec : r.recs) {
        Value v;
        if (!app.db.get(*r.t, rec.first, *r.f, v) || v.type != FieldType::Int) continue;
        if (n == 0) lo = hi = v.i;
        lo = std::min(lo, v.i);
        hi = std::max(hi, v.i);
        ++n;
    }
    if (n == 0) return "no read-back: the row could not be read";
    std::string out = target + " now: " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + ".." + std::to_string(hi) + " (" + std::to_string(n) + " rows)");
    const TacticSnapshots& snaps = snapshots(app);
    if (r.recs.size() == 1) {
        if (const DbSnapshot* s = snaps.find(target, r.recs.front().second)) out += " (before Turbo: " + std::to_string(s->value) + ")";
    } else {
        size_t kept = 0;
        for (const auto& rec : r.recs)
            if (snaps.find(target, rec.second)) ++kept;
        if (kept) out += " (" + std::to_string(kept) + " rows kept before Turbo's first write)";
    }
    return out;
}

// The read-back of a game variable: the host's record of the override (read back from the store after the write)
std::string gamevar_readback(App& app, const SliderDef& d, const std::string& target, const std::map<std::string, msetup::VarLine>& lines) {
    if (!app.match_setup) return "no read-back: this Turbo has no game calls";
    auto it = lines.find(target);
    if (it == lines.end()) return "game variable now: not set, the game decides";
    if (!it->second.active) return "game variable now: cleared, the game decides";
    return "game variable now: " + name_of(d, it->second.value) + " (set by Turbo, read back after the write)";
}

// Reads the kill switch files; the master switch appearing (or being there at the first look) asks for the database rows to go back
void update_switches(App& app) {
    const bool was_off = g_tac.off;
    g_tac.sw = read_switches(out_dir(app));
    g_tac.off = g_tac.sw.all();
    g_tac.next_check = app.now + 2.0;
    if (g_tac.off && !was_off) g_tac.restore_pending = true;
    if (!g_tac.off) g_tac.restore_pending = false;
}

// The kill switches, looked at every 2 s (the frame path does no file I/O otherwise; the snapshot file is read at the first look)
void refresh_off(App& app) {
    if (app.now >= g_tac.next_check) {
        update_switches(app);
        snapshots(app);
    }
    if (g_tac.restore_pending) restore_on_kill_switch(app);
}

// The kill switches are read again at every write: a write never trusts the 2 s cache
bool kill_switch_now(App& app) {
    update_switches(app);
    return g_tac.off;
}

std::string off_text(const std::string& file) { return "off: turbo_output\\" + file + " exists"; }

// The enabled writes nobody has seen work in a played match (untested, no effect seen, legacy): Apply asks about these first, naming the
// club or player whose row each one writes
std::vector<std::string> untested_lines(App& app, const std::vector<EffWrite>& writes) {
    std::vector<std::string> out;
    for (const EffWrite& e : writes) {
        const SliderDef* d = find_slider(e.w.key);
        if (!d) continue;
        const TacticStatus s = tactic_status(*d);
        if (untested_write(s))
            out.push_back(s.label + " (" + e.w.target + " = " + std::to_string(e.w.value) + for_owner(app, e.w.target) + "): " + seen_badge(s));
    }
    return out;
}

// Apply: untested settings ask first (confirmed = the user said "Apply anyway"); every switched-on value is sent (the writes are
// idempotent; the rows' read-back lines show what the game holds); a channel whose kill switch file exists is skipped and named; a slider
// switched off since the last Apply gets its default back (the game variable cleared, the database row restored from the snapshot).
// The club and player rows are the selected club's and player's only (sync_owners): another club's values are never sent.
void apply_changes(App& app, bool confirmed = false) {
    sync_owners(app);
    if (kill_switch_now(app)) {
        app.notify("Tactics are off: turbo_output\\tactics_off.txt exists, nothing was written", true);
        return;
    }
    const std::vector<EffWrite> writes = effective_writes(g_tac.working);
    if (!confirmed) {
        const std::vector<std::string> ask = untested_lines(app, writes);
        if (!ask.empty()) {
            g_tac.confirm_lines = ask;
            g_tac.confirm_request = true;
            return;
        }
    }
    if (g_tac.dry_run) {
        app.notify("Tactics dry run: " + std::to_string(writes.size()) + " write(s) would be sent, nothing was written");
        return;
    }
    g_tac.writes_done.clear();
    int ok = 0, failed = 0, skipped = 0, blocked = 0, restored = 0;
    std::string first_error, first_skip, first_block;
    std::vector<std::string> rows_of;  // whose database rows were written ("Arsenal", "every referee")
    auto fail = [&](const std::string& key, const std::string& why) {
        ++failed;
        if (first_error.empty()) first_error = key + ": " + why;
    };
    // a slider that was applied and is now back to "the game decides": its default again
    for (const SliderDef& d : slider_registry()) {
        if (!writes_game(d) || d.binding.targets.empty()) continue;
        if (!g_tac.applied.is_enabled(d.key) || g_tac.working.is_enabled(d.key)) continue;
        const WriteChannel ch = channel_of_def(d);
        if (g_tac.sw.blocks(ch)) {
            ++blocked;
            if (first_block.empty()) first_block = d.key + ": " + off_text(g_tac.sw.blocking_file(ch));
            continue;
        }
        if (d.status == SliderStatus::Live) {
            if (!app.match_setup) continue;
            std::string why;
            if (!app.match_setup->clear_var(d.binding.targets[0], why)) fail(d.key, why);
        } else {
            std::string why;
            restored += restore_db(app, d, why);
            if (!why.empty() && first_skip.empty()) first_skip = d.key + ": " + why;
        }
    }
    for (const EffWrite& e : writes) {
        const SliderWrite& w = e.w;
        const WriteChannel ch = channel_of_target(w.target);
        if (g_tac.sw.blocks(ch)) {
            ++blocked;
            if (first_block.empty()) first_block = w.key + ": " + off_text(g_tac.sw.blocking_file(ch));
            continue;
        }
        if (w.status == SliderStatus::Live) {
            if (!app.match_setup) {
                fail(w.key, "this Turbo has no game calls");
                continue;
            }
            const gv::KnownVar* kv = gv::known(w.target);
            if (!kv) {
                fail(w.key, w.target + " is not a variable Turbo knows");
                continue;
            }
            std::string why;
            const int32_t v = std::clamp<int32_t>(w.value, kv->min, kv->max);
            if (!app.match_setup->set_var(w.target, v, why)) fail(w.key, why);
            else {
                ++ok;
                ++g_tac.live_written;
            }
        } else if (w.status == SliderStatus::DB) {
            std::string why, what;
            const DbOutcome o = write_db(app, w, why, what);
            if (o == DbOutcome::Ok) {
                ++ok;
                g_tac.writes_done.push_back(what);
                const std::string who = row_owner(app, w.target);
                if (!who.empty() && std::find(rows_of.begin(), rows_of.end(), who) == rows_of.end()) rows_of.push_back(who);
            } else if (o == DbOutcome::Skipped) {
                ++skipped;
                if (first_skip.empty()) first_skip = w.key + ": " + why;
            } else fail(w.key, why);
        }
    }
    if (failed == 0 && blocked == 0) g_tac.applied = g_tac.working;
    std::string msg = "Tactics: " + std::to_string(ok) + " write(s) sent to the game";
    if (!rows_of.empty()) {
        msg += " (database rows of ";
        for (size_t i = 0; i < rows_of.size(); ++i) msg += (i ? ", " : "") + rows_of[i];
        msg += ")";
    }
    if (restored) msg += ", " + std::to_string(restored) + " row(s) restored to the value before Turbo";
    if (skipped) msg += ", " + std::to_string(skipped) + " not written (" + first_skip + ")";
    if (blocked) msg += ", " + std::to_string(blocked) + " blocked by a kill switch (" + first_block + ")";
    if (failed) msg += ", " + std::to_string(failed) + " refused (" + first_error + ")";
    app.notify(msg, failed > 0 || blocked > 0);
}

// Reset to default for one row: back to "the game decides" and the game's default restored at once (a game variable is cleared, a
// database row gets the value kept before Turbo's first write). Honours the kill switches and the dry run. A club or player row is the one
// the row's values belong to (the selected club or player, sync_owners); the values of the others stay as they are.
void reset_row(App& app, const SliderDef& d) {
    sync_owners(app);
    const TacticStatus st = tactic_status(d);
    g_tac.working.reset(d.key);
    if (!writes_game(d) || d.binding.targets.empty()) {
        g_tac.applied.reset(d.key);
        app.notify(st.label + ": the game decides (nothing was ever written for it)");
        return;
    }
    if (kill_switch_now(app)) {
        app.notify(st.label + ": nothing restored, turbo_output\\tactics_off.txt exists", true);
        return;
    }
    const WriteChannel ch = channel_of_def(d);
    if (g_tac.sw.blocks(ch)) {
        app.notify(st.label + ": nothing restored, " + off_text(g_tac.sw.blocking_file(ch)), true);
        return;
    }
    if (g_tac.dry_run) {
        app.notify(st.label + ": dry run, the default would be restored, nothing was written");
        return;
    }
    if (d.status == SliderStatus::Live) {
        if (!app.match_setup) {
            app.notify(st.label + ": this Turbo has no game calls", true);
            return;
        }
        std::string why;
        const std::string& target = d.binding.targets[0];
        if (!app.match_setup->clear_var(target, why)) {
            app.notify(st.label + ": not cleared, " + why, true);
            return;
        }
        g_tac.applied.reset(d.key);
        app.notify(st.label + ": override cleared, the game decides again");
        return;
    }
    std::string why;
    const int n = restore_db(app, d, why);
    g_tac.applied.reset(d.key);
    const std::string who = for_owner(app, d.binding.targets.front());
    if (n > 0) app.notify(st.label + ": " + std::to_string(n) + " row(s) restored to the value before Turbo's first write" + who + (why.empty() ? "" : " (" + why + ")"));
    else if (!why.empty()) app.notify(st.label + ": nothing restored" + who + ", " + why, true);
    else app.notify(st.label + ": nothing to restore" + who + ", Turbo never wrote this row");
}

// ---------------------------------------------------------------------------------------------------------------- the pitch
std::set<std::string> hidden_for(const SliderSet& s, Phase ph, int slot) {
    std::set<std::string> h;
    auto on = [&](const char* k) { return s.is_enabled(k); };
    if (!on("team.line_depth")) h.insert("def_line");
    if (!on("team.engagement_height")) h.insert("press_line");
    if (!on("team.line_length")) h.insert("front_line");
    const bool wa = on("team.width_att"), wd = on("team.width_def");
    const bool band = ph == Phase::WithBall ? wa : ph == Phase::WithoutBall ? wd : (wa || wd);
    if (!band) h.insert("width_band");
    if (!(on("team.forward_runs") || (slot >= 0 && on("pos.forward_runs")))) h.insert("arrows");
    return h;
}

// ---------------------------------------------------------------------------------------------------------------- opposition
struct TeamInfo {
    TeamFacts facts;
    std::string name;
    bool known = false;  // a squad was found
};

TeamInfo gather(App& app, int64_t teamid, const Formation& f) {
    TeamInfo t;
    t.name = app.model.team_name(teamid);
    std::vector<int> ovr;
    double age_sum = 0;
    int age_n = 0;
    for (const LinkRow& l : app.model.links_of_team(teamid)) {
        const PlayerRow* p = app.model.player(l.playerid);
        if (!p || p->overall <= 0) continue;
        ovr.push_back(p->overall);
        if (p->age > 0) {
            age_sum += p->age;
            ++age_n;
        }
    }
    std::sort(ovr.begin(), ovr.end(), std::greater<int>());
    if (ovr.size() > 14) ovr.resize(14);
    if (!ovr.empty()) {
        double s = 0;
        for (int v : ovr) s += v;
        t.facts.ovr = s / double(ovr.size());
        t.known = true;
    } else if (const TeamRow* tr = app.model.team(teamid); tr && tr->overall > 0) {
        t.facts.ovr = tr->overall;  // no squad list: the club's own rating
        t.known = true;
    }
    if (age_n > 0) t.facts.age = age_sum / age_n;
    int d = 0, m = 0, a = 0, wide = 0;
    for (size_t i = 1; i < f.slots.size(); ++i) {
        const FormationSlot& s = f.slots[i];
        if (s.group == PosGroup::Def) ++d;
        else if (s.group == PosGroup::Mid) ++m;
        else if (s.group == PosGroup::Att) ++a;
        if (s.label == "LW" || s.label == "RW" || s.label == "LWB" || s.label == "RWB") ++wide;
    }
    t.facts.n_def = d;
    t.facts.n_mid = m;
    t.facts.n_att = a;
    t.facts.n_wide = wide;
    return t;
}

// The solver's inputs and profile, read again only when something they depend on changed
void refresh_opp(App& app) {
    OppState& o = g_tac.opp;
    const int64_t user = app.bridge.state().user_team, opp_id = app.sel_team;
    SliderSet eff;  // only the opp.* sliders that are switched on count: "the game decides" keeps the solver's default
    for (const SliderDef* d : sliders_in_scope(SliderScope::Opposition))
        if (d->status == SliderStatus::Local && g_tac.working.is_enabled(d->key)) eff.set(d->key, g_tac.working.value_or_default(d->key), true);
    const GameDate date = app.today();
    std::string sig = std::to_string(app.gen) + "|" + std::to_string(user) + "|" + std::to_string(opp_id) + "|" + (o.home ? "h" : "a") + "|" +
                      std::to_string(o.salt) + "|" + (o.on ? "on" : "off") + "|" + std::to_string(date.as_int()) + "|" +
                      std::to_string(app.bridge.state().load_gen) + "|" + std::to_string(app.opp_user_rules.rules.size()) + "|";
    for (const auto& kv : eff.values) sig += kv.first + "=" + std::to_string(kv.second) + ",";
    for (const char* k : {"match.injury_frequency_cpu", "match.injury_severity_cpu", "match.difficulty"})
        sig += std::string(k) + (g_tac.working.is_enabled(k) ? std::to_string(g_tac.working.value_or_default(k)) : "-") + ",";
    if (sig == o.sig) return;
    o.sig = sig;
    o.ready = false;
    o.profile = OppProfile();
    o.ghost = SliderSet();
    o.live.clear();
    TacticsTabState::OppCard& st = o.card;
    st = TacticsTabState::OppCard();
    st.shown = true;
    const TeamRow* opp_row = app.model.team(opp_id);
    if (!opp_row) {
        st.note = "Select the opponent in the club list on the left.";
        return;
    }
    o.opp_name = opp_row->name;
    if (user <= 0 || !app.model.team(user)) {
        st.note = "Your own club is not known (load a career): there is nobody to compare the opponent with.";
        return;
    }
    if (user == opp_id) {
        st.note = "This is your own club. Select the opponent in the club list on the left.";
        return;
    }
    o.opp_tf = read_team_formation(app.db, opp_id);
    const TeamFormation user_tf = read_team_formation(app.db, user);
    const TeamInfo ui = gather(app, user, user_tf.formation), oi = gather(app, opp_id, o.opp_tf.formation);
    if (!ui.known || !oi.known) {
        st.note = std::string("No squad rating for ") + (!ui.known ? ui.name : oi.name) + ": the solver needs players in the save.";
        return;
    }
    Fixture fx;
    fx.user = ui.facts;
    fx.opp = oi.facts;
    fx.opp_home = o.home;
    FixtureId id;
    id.career = std::max<long long>(0, app.bridge.state().load_gen);
    id.season = date.month >= 7 ? date.year : date.year - 1;
    id.matchday = gregorian_days_from_date(date);
    id.opp_teamid = opp_id;
    id.salt = o.salt;
    OppParams params = params_from_sliders(eff);
    params.enabled = o.on;
    const RuleSet rules = merge_rules(builtin_rules(), app.opp_user_rules);
    o.profile = solve_opposition(fx, id, params, rules);
    o.ready = true;

    st.active = o.profile.active;
    st.seed = o.profile.seed;
    st.ratio = o.profile.ratio;
    st.user_ovr = ui.facts.ovr;
    st.opp_ovr = oi.facts.ovr;
    st.n_def = oi.facts.n_def;
    st.n_mid = oi.facts.n_mid;
    st.n_att = oi.facts.n_att;
    st.n_wide = oi.facts.n_wide;
    st.fixture_line = "You: " + ui.name + " (squad rating " + fmtf("%.0f", ui.facts.ovr) + ") against " + oi.name + " (squad rating " +
                      fmtf("%.0f", oi.facts.ovr) + "): strength ratio " + fmtf("%.2f", o.profile.ratio) + ", " + oi.name +
                      (o.home ? " at home" : " away");
    st.facts_line = "Read from your save: the best 14 players' rating, their average age and the formation shape (" +
                    std::to_string(oi.facts.n_def) + "-" + std::to_string(oi.facts.n_mid) + "-" + std::to_string(oi.facts.n_att) +
                    "). Not read yet, neutral values used: pace, passing, reputation, form, mentality.";
    char sb[40];
    std::snprintf(sb, sizeof(sb), "Seed %016llx", static_cast<unsigned long long>(o.profile.seed));
    st.seed_text = sb;
    for (size_t i = 0; i < kFamilyCount; ++i)
        if (o.profile.blend[i] >= 0.005) st.blend.push_back(std::string(family_label(i)) + " " + std::to_string(int(std::lround(o.profile.blend[i] * 100.0))) + "%");
    for (const auto& kv : o.profile.team_deltas) {
        const SliderDef* d = find_slider(kv.first);
        if (!d) continue;
        st.changes.push_back(d->label + " " + signed_text(kv.second) + "  (preview only, not sent to the game)");
        o.ghost.set(kv.first, d->def + kv.second, true);
    }
    for (const auto& kv : o.profile.live_offsets) {
        const SliderDef* d = find_slider(kv.first);
        if (!d || d->binding.targets.empty()) continue;
        OppState::Live l;
        l.key = kv.first;
        l.target = d->binding.targets[0];
        l.offset = kv.second;
        l.base = base_for(g_tac.working, l.target, &l.base_from_slider);
        l.value = clamp_var(l.target, l.base + l.offset);
        o.live.push_back(l);
        st.live.push_back(l.target + " = " + std::to_string(l.value) + "  [game variable]  (base " + std::to_string(l.base) +
                          (l.base_from_slider ? " from your Match setting" : " the middle: Turbo cannot read the game's own number") + " + offset " +
                          signed_text(l.offset) + ")");
    }
    st.explain = explain_text(o.profile);
}

void apply_opp_live(App& app) {
    OppState& o = g_tac.opp;
    if (kill_switch_now(app)) {
        app.notify("Tactics are off: turbo_output\\tactics_off.txt exists, nothing was written", true);
        return;
    }
    if (g_tac.sw.blocks(WriteChannel::GameVar)) {
        app.notify("Opposition: game variables are " + off_text(g_tac.sw.blocking_file(WriteChannel::GameVar)) + ", nothing was written", true);
        return;
    }
    if (!o.ready || o.live.empty()) {
        app.notify("Opposition: no injury or difficulty offset to send");
        return;
    }
    if (g_tac.dry_run) {
        app.notify("Opposition dry run: " + std::to_string(o.live.size()) + " game variable(s) would be set, nothing was written");
        return;
    }
    if (!app.match_setup) {
        app.notify("Opposition: this Turbo has no game calls", true);
        return;
    }
    int ok = 0;
    std::string first_error;
    for (const OppState::Live& l : o.live) {
        std::string why;
        if (!app.match_setup->set_var(l.target, l.value, why)) {
            if (first_error.empty()) first_error = l.key + ": " + why;
        } else {
            ++ok;
            ++g_tac.live_written;
        }
    }
    std::string msg = "Opposition: " + std::to_string(ok) + " game variable(s) set for the match against " + o.opp_name;
    if (!first_error.empty()) msg += ", refused (" + first_error + ")";
    app.notify(msg, !first_error.empty());
}

void hatched_bar(float frac, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const ImU32 col = g_tac.off ? IM_COL32(150, 150, 150, 160) : IM_COL32(120, 190, 255, 200);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), col);
    const float step = S(4.0f);
    const int n = int(w / step), filled = int(std::lround(std::clamp(frac, 0.0f, 1.0f) * float(n)));
    for (int k = 0; k < filled; ++k) dl->AddLine(ImVec2(p.x + step * float(k), p.y + h), ImVec2(p.x + step * float(k) + step, p.y), col);
    ImGui::Dummy(ImVec2(w, h));
}

void draw_opp_card(App& app) {
    OppState& o = g_tac.opp;
    refresh_opp(app);
    TacticsTabState::OppCard& st = o.card;
    st.shown = true;
    st.can_apply = o.ready && o.profile.active && !o.live.empty() && !g_tac.off && app.match_setup != nullptr;
    g_state.opp = st;
    ImGui::SeparatorText("Opposition");
    ImGui::TextDisabled("Turbo's own estimate. Nothing below reaches the game until you press Apply, and only injury and difficulty offsets can.");
    checkbox_right("Opposition variety on##tacoppon", &o.on);
    ImGui::SameLine();
    checkbox_right("Opponent plays at home##tacopphome", &o.home);
    if (ImGui::Button("Re-roll##tacopproll")) ++o.salt;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Another seeded wobble for the same fixture. The same fixture and the same settings always give the same result.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!st.can_apply);
    if (ImGui::Button("Apply live offsets to the game##tacoppapply")) apply_opp_live(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sends the injury and difficulty offsets below as game variables for played matches. The style changes are a preview and are never sent.");
    if (!st.note.empty()) {
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", st.note.c_str());
        return;
    }
    ImGui::TextWrapped("%s", st.fixture_line.c_str());
    ImGui::TextDisabled("%s", st.facts_line.c_str());
    if (!o.profile.active) {
        ImGui::TextWrapped("%s", explain_text(o.profile).c_str());
        return;
    }
    ImGui::Text("Style blend (Turbo's estimate, blended and never one hard label):");
    for (size_t i = 0; i < kFamilyCount; ++i) {
        if (o.profile.blend[i] < 0.005) continue;
        hatched_bar(float(o.profile.blend[i]), S(110.0f));
        ImGui::SameLine();
        ImGui::Text("%s %d%%", family_label(i), int(std::lround(o.profile.blend[i] * 100.0)));
    }
    if (st.changes.empty()) ImGui::TextDisabled("The solver changes no team setting for this fixture.");
    else ImGui::Text("What the solver would change (the pitch below shows it; preview only):");
    for (const std::string& c : st.changes) ImGui::BulletText("%s", c.c_str());
    if (st.live.empty()) {
        ImGui::TextDisabled("No injury or difficulty offset: no rule produced one, so there is nothing to send to the game.");
    } else {
        ImGui::Text("What Apply would send to the game:");
        for (const std::string& l : st.live) ImGui::BulletText("%s", l.c_str());
    }
    ImGui::TextDisabled("%s. %zu built-in rules, %zu of yours.", st.seed_text.c_str(), builtin_rules().rules.size(), app.opp_user_rules.rules.size());
    if (ImGui::CollapsingHeader("Explain##tacoppexp")) ImGui::TextWrapped("%s", st.explain.c_str());
    if (!app.opp_rules_error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Your opposition rules file: %s", app.opp_rules_error.c_str());
}

// ---------------------------------------------------------------------------------------------------------------- presets
bool persist_profiles(App& app, const std::string& ok_text) {
    std::string err;
    if (!save_tactic_profiles(tactic_profiles_path(app.bridge.root()), app.tactic_profiles, &err, app.tactic_profiles_unreadable)) {
        app.notify("Preset kept for this session only: " + err, true);
        return false;
    }
    app.tactic_profiles_unreadable = false;
    if (!ok_text.empty()) app.notify(ok_text);
    return true;
}

void load_profile(App& app, const Profile& p) {
    g_tac.selected_profile = p.id;
    const ProfileLoadResult r = load_profile_into(p, g_tac.working);
    if (!p.formation.empty()) {
        const TeamFormation& tf = team_formation(app);
        if (p.formation == tf.formation.name) {
            g_tac.formation_pick = -1;
        } else {
            const auto& fb = fallback_formations();
            for (size_t i = 0; i < fb.size(); ++i)
                if (fb[i].name == p.formation) g_tac.formation_pick = int(i);
        }
    }
    std::string msg = "Preset loaded: " + p.name + " (not applied yet: press Apply)";
    if (!r.held_off.empty())
        msg += "; " + std::to_string(r.held_off.size()) + " untested or legacy setting(s) loaded switched OFF (switch them on yourself): " + r.held_off.front() +
               (r.held_off.size() > 1 ? ", ..." : "");
    app.notify(msg);
}

// Where a profile's settings live: its category's scope (a bundle spans all four)
bool in_profile_scope(const Profile& p, const SliderDef& d) {
    if (p.category == ProfileCategory::Bundle) return true;
    return category_of(d.scope) == p.category;
}

// Compare mode: the loaded preset against the sliders now, "was" being the preset's value
std::vector<std::string> compare_lines(const Profile& p) {
    std::vector<std::string> out;
    for (const SliderChange& c : diff_sliders(p.sliders, g_tac.working)) {
        const SliderDef* d = find_slider(c.key);
        if (!d || !in_profile_scope(p, *d)) continue;
        const std::string was = c.has_a ? name_of(*d, c.a) : "the game decides", now = c.has_b ? name_of(*d, c.b) : "the game decides";
        out.push_back(d->label + ": was " + was + ", now " + now);
    }
    return out;
}

std::string was_text(const SliderDef& d, const Profile* cmp) {
    if (cmp) {
        for (const SliderChange& c : diff_sliders(cmp->sliders, g_tac.working))
            if (c.key == d.key) return c.has_a ? "was " + name_of(d, c.a) : "was: game decides";
        return "";
    }
    if (!value_changed(d)) return "";
    return g_tac.applied.is_enabled(d.key) ? "was " + name_of(d, g_tac.applied.value_or_default(d.key)) : "was: game decides";
}

// The read-back of every target of a slider, from the cache (database rows) or the host's override list (game variables)
std::string readback_of(App& app, const SliderDef& d, const TacticStatus& st, const std::map<std::string, msetup::VarLine>& lines) {
    if (st.written == WriteKind::Preview || st.written == WriteKind::TurboOnly) return "no read-back: nothing is written for this setting";
    std::string out;
    for (const std::string& t : d.binding.targets) {
        if (t.find("{n}") != std::string::npos || dead_target_reason(t)) continue;
        std::string one;
        if (st.written == WriteKind::GameVar) one = gamevar_readback(app, d, t, lines);
        else {
            auto it = g_tac.readback.find(t);  // the cache is filled on first sight of a target (a scope or "show legacy fields" change) and cleared every 2 s
            if (it == g_tac.readback.end()) it = g_tac.readback.emplace(t, db_readback(app, t)).first;
            one = it->second;
        }
        if (one.empty()) continue;
        if (!out.empty()) out += "; ";
        out += one;
    }
    return out.empty() ? "no read-back: nothing is written for this setting" : out;
}

// ---- a slider row
void draw_row(App& app, const SliderDef& d, const Profile* cmp, const std::map<std::string, msetup::VarLine>& lines) {
    SliderSet& w = g_tac.working;
    const TacticStatus st = tactic_status(d);
    const std::string off = writes_game(d) ? g_tac.sw.blocking_file(channel_of_def(d)) : std::string();
    const bool dim = preview_only(d) || !off.empty();
    TacticsTabState::Row row;
    row.key = d.key;
    row.status = status_name(d.status);
    row.label = st.label;
    row.written = written_badge(st.written);
    row.seen = seen_badge(st);
    row.tag = status_tag(st);
    row.readback = readback_of(app, d, st, lines);
    row.channel_off = off;
    row.enabled = w.is_enabled(d.key);
    row.dimmed = dim;
    row.untested = untested_write(st);
    row.legacy = st.legacy_hidden;
    row.value = w.value_or_default(d.key);
    row.was = was_text(d, cmp);
    g_state.rows.push_back(row);

    ImGui::PushID(d.key.c_str());
    if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);
    bool on = w.is_enabled(d.key);
    if (ImGui::Checkbox("##on", &on)) w.set_enabled(d.key, on);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", on ? "Switched on: this value is used" : "Off: the game decides");
    ImGui::SameLine();
    // Reset sits right after the checkbox: a long label ("Role familiarity 1 (Role+ / Role++)") would push a button behind it out of a
    // narrow card column
    if (ImGui::SmallButton("Reset##reset")) reset_row(app, d);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", writes_game(d) ? "Reset to default: back to \"the game decides\" and the game's own value restored at once (the override cleared, the row put back)"
                                               : "Back to \"the game decides\" (nothing is written for this setting)");
    ImGui::SameLine();
    ImGui::TextUnformatted(st.label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.description.c_str());
    // the two badges on their own line, so neither is cut off by the column edge
    ImGui::TextColored(written_colour(st.written), "  [%s]", row.written.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", written_help(st.written));
    ImGui::SameLine();
    ImGui::TextColored(seen_colour(st.seen), "[%s]", row.seen.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", seen_help(st.seen));
    if (!d.technical.empty()) ImGui::TextDisabled("  %s", d.technical.c_str());
    if (!off.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "  %s", off_text(off).c_str());
    int v = w.value_or_default(d.key);
    int nv = v;
    ImGui::SetNextItemWidth(S(150.0f));
    if (d.kind == SliderKind::Toggle) {
        bool b = v != 0;
        if (ImGui::Checkbox("##v", &b)) nv = b ? 1 : 0;
    } else if (d.kind == SliderKind::Enum && (!d.labels.empty() || !d.allowed.empty())) {
        if (ImGui::BeginCombo("##v", on ? name_of(d, v).c_str() : "game decides")) {
            for (int val : legal_values(d))
                if (ImGui::Selectable((name_of(d, val) + "##" + std::to_string(val)).c_str(), on && val == v)) nv = val;
            ImGui::EndCombo();
        }
    } else {
        ImGui::SliderInt("##v", &nv, d.min, d.max, on ? "%d" : "game decides");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(64.0f));
        int typed = v;
        if (ImGui::InputInt("##typed", &typed, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) nv = typed;
    }
    if (nv != v) w.set(d.key, nv, true);
    if (!row.was.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", row.was.c_str());
    }
    ImGui::TextDisabled("  %s", row.tag.c_str());
    ImGui::TextDisabled("  %s", row.readback.c_str());
    if (d.binding.kind == BindingKind::DbField && !d.binding.targets.empty() && d.binding.targets.front().find("{n}") != std::string::npos)
        ImGui::TextDisabled("  this build does not write formation slots yet");
    if (dim) ImGui::PopStyleVar();
    ImGui::PopID();
}

// The database read-backs of a scope, refreshed when the save, the club or the player changed, else every 2 s
void refresh_readback(App& app, const std::vector<const SliderDef*>& visible) {
    if (app.gen == g_tac.rb_gen && app.sel_team == g_tac.rb_team && app.sel_player == g_tac.rb_player && app.now < g_tac.rb_next) return;
    g_tac.rb_gen = app.gen;
    g_tac.rb_team = app.sel_team;
    g_tac.rb_player = app.sel_player;
    g_tac.rb_next = app.now + 2.0;
    g_tac.readback.clear();
    for (const SliderDef* d : visible) {
        if (d->status != SliderStatus::DB) continue;
        for (const std::string& t : d->binding.targets) {
            if (t.find("{n}") != std::string::npos || g_tac.readback.count(t)) continue;
            g_tac.readback[t] = db_readback(app, t);
        }
    }
}

void draw_confirm_dialog(App& app) {
    if (g_tac.confirm_request) {
        g_tac.confirm_request = false;
        ImGui::OpenPopup("Apply untested settings?##tacconfirm");
    }
    if (ImGui::BeginPopupModal("Apply untested settings?##tacconfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        g_state.confirm_open = true;
        g_state.confirm_lines = g_tac.confirm_lines;
        ImGui::TextWrapped("Nobody has seen these settings work in a played match. They are written as asked; whether the game honours them is unknown.");
        for (const std::string& l : g_tac.confirm_lines) ImGui::BulletText("%s", l.c_str());
        if (ImGui::Button("Apply anyway##tacconfirm")) {
            ImGui::CloseCurrentPopup();
            apply_changes(app, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##tacconfirmno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------------------------- dialogs
void draw_rename_dialog(App& app) {
    if (ImGui::BeginPopupModal("Rename preset##tacren", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Profile* p = loaded_profile(app);
        if (!p) {
            ImGui::TextUnformatted("No preset is loaded.");
        } else {
            ImGui::Text("New name for \"%s\"", p->name.c_str());
            ImGui::SetNextItemWidth(S(260.0f));
            const bool enter = ImGui::InputText("##tacrenname", g_tac.rename_buf, sizeof(g_tac.rename_buf), ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Rename##tacrenok") || enter) {
                const std::string nm = g_tac.rename_buf;
                if (nm.empty()) {
                    app.notify("Type a name for the preset first", true);
                } else if (!app.tactic_profiles.rename(p->id, nm)) {
                    app.notify("Not renamed: another preset of this kind already has that name (or the preset cannot be changed)", true);
                } else {
                    persist_profiles(app, "Preset renamed: " + nm);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel##tacrenno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_delete_dialog(App& app) {
    if (ImGui::BeginPopupModal("Delete preset?##tacdel", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const Profile* p = loaded_profile(app);
        if (!p) {
            ImGui::TextUnformatted("No preset is loaded.");
        } else {
            ImGui::Text("Delete the preset \"%s\"?", p->name.c_str());
            ImGui::TextDisabled("This cannot be undone. Nothing is changed in the game.");
            if (ImGui::Button("Delete##tacdelok")) {
                const std::string nm = p->name, id = p->id;
                if (app.tactic_profiles.remove(id)) {
                    g_tac.selected_profile.clear();
                    g_tac.compare = false;
                    persist_profiles(app, "Preset deleted: " + nm);
                } else {
                    app.notify("The preset cannot be deleted", true);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel##tacdelno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Which presets an export covers: this preset, my presets of this scope's kind, or all of mine
std::vector<const Profile*> export_set(App& app, SliderScope sc) {
    std::vector<const Profile*> which;
    if (g_tac.export_what == 0) {
        if (const Profile* p = loaded_profile(app)) which.push_back(p);
    } else {
        for (const Profile& p : app.tactic_profiles.profiles)
            if (g_tac.export_what == 2 || p.category == category_of(sc)) which.push_back(&p);
    }
    return which;
}

void draw_export_dialog(App& app, SliderScope sc) {
    if (ImGui::BeginPopupModal("Export presets##tacexp", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Write presets to a file you can give to someone else (turbo preset file, .json).");
        ImGui::RadioButton("The loaded preset##tacexp0", &g_tac.export_what, 0);
        ImGui::RadioButton("All my presets of this kind##tacexp1", &g_tac.export_what, 1);
        ImGui::RadioButton("All my presets##tacexp2", &g_tac.export_what, 2);
        const std::vector<const Profile*> which = export_set(app, sc);
        ImGui::TextDisabled("%zu preset(s) will be written.", which.size());
        ImGui::BeginDisabled(which.empty());
        if (ImGui::Button("Save as...##tacexpsave")) {
            FilePicker& fp = g_tac.export_fp;
            fp.mode = PickMode::Save;
            fp.title = "Where to save the presets";
            fp.exts = {".json"};
            fp.key = "tactics.export";
            fp.start = app.bridge.dir();
            fp.ask_overwrite = true;
            std::snprintf(fp.name, sizeof(fp.name), "%s", "turbo_tactic_presets.json");
            ImGui::OpenPopup("##tacexppick");
        }
        ImGui::EndDisabled();
        fs::path chosen;
        if (file_picker_modal("##tacexppick", g_tac.export_fp, chosen)) {
            fs::path target = chosen;
            if (target.extension().empty()) target += ".json";
            std::string err;
            if (!write_text_atomic(target, export_profiles_json(which, kGuiVersion), &err)) app.notify("Export failed: " + err, true);
            else app.notify("Exported " + std::to_string(which.size()) + " preset(s) to " + path_text(target));
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Close##tacexpclose")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

const char* clash_text(ClashKind k) {
    switch (k) {
        case ClashKind::None: return "new";
        case ClashKind::SameId: return "same preset already here";
        case ClashKind::SameName: return "a preset with this name is already here";
    }
    return "";
}

void draw_import(App& app) {
    fs::path chosen;
    if (file_picker_modal("##tacimppick", g_tac.import_fp, chosen)) {
        std::string text, err;
        ImportPlan plan;
        if (!read_text_file(chosen, text, &err)) {
            app.notify("Import failed: " + err, true);
        } else if (!plan_import(app.tactic_profiles, text, plan, &err)) {
            app.notify("That is not a Turbo preset file: " + err, true);
        } else {
            g_tac.plan = plan;
            g_tac.plan_actions.assign(plan.items.size(), int(ClashAction::Rename));
            g_tac.plan_open_request = true;
        }
    }
    if (g_tac.plan_open_request) {
        g_tac.plan_open_request = false;
        ImGui::OpenPopup("Import presets##tacimpplan");
    }
    g_tac.plan_open = false;
    if (ImGui::BeginPopupModal("Import presets##tacimpplan", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        g_tac.plan_open = true;
        const ImportPlan& plan = g_tac.plan;
        ImGui::Text("%zu preset(s) in the file", plan.items.size());
        if (app.tactic_profiles.read_only) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "The preset file on this computer was written by a newer Turbo: it is read-only, nothing can be imported.");
        const size_t left_out = plan.report.dropped_profiles;
        if (left_out) ImGui::TextDisabled("%zu entr%s of the file could not be used and %s left out.", left_out, left_out == 1 ? "y" : "ies", left_out == 1 ? "was" : "were");
        for (size_t i = 0; i < plan.items.size(); ++i) {
            const ImportItem& it = plan.items[i];
            ImGui::PushID(int(i));
            ImGui::BulletText("%s (%s): %s", it.incoming.name.c_str(), category_name(it.incoming.category), clash_text(it.clash));
            if (it.clash != ClashKind::None) {
                ImGui::TextDisabled("   %zu setting(s) differ from the one you have", it.diff.size());
                int& act = g_tac.plan_actions[i];
                static const char* const names[3] = {"Keep both (rename the new one)", "Replace mine", "Skip it"};
                ImGui::SetNextItemWidth(S(240.0f));
                if (ImGui::BeginCombo("##tacimpact", names[std::clamp(act, 0, 2)])) {
                    for (int a = 0; a < 3; ++a) {
                        const bool blocked = a == int(ClashAction::Replace) && it.existing_builtin;
                        if (blocked) ImGui::BeginDisabled();
                        if (ImGui::Selectable((std::string(names[a]) + (blocked ? " (built-in, cannot be replaced)" : "") + "##" + std::to_string(a)).c_str(), act == a)) act = a;
                        if (blocked) ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
            }
            ImGui::PopID();
        }
        ImGui::BeginDisabled(app.tactic_profiles.read_only || plan.items.empty());
        if (ImGui::Button("Import##tacimpok")) {
            std::vector<ClashAction> acts;
            for (int a : g_tac.plan_actions) acts.push_back(static_cast<ClashAction>(a));
            const ImportResult r = apply_import(app.tactic_profiles, g_tac.plan, acts);
            persist_profiles(app, "Imported: " + std::to_string(r.added) + " added, " + std::to_string(r.replaced) + " replaced, " +
                                      std::to_string(r.renamed) + " renamed, " + std::to_string(r.skipped) + " skipped");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##tacimpno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------------------------------------- columns
void draw_left(App& app) {
    ImGui::SeparatorText("Scope");
    for (int i = 0; i < 4; ++i) {
        const bool sel = g_tac.scope == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button((std::string(kScopeLabels[i]) + "##scope").c_str(), ImVec2(-1, 0))) g_tac.scope = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::SeparatorText("Presets");
    const SliderScope sc = kScopes[g_tac.scope];
    const std::vector<const Profile*> list = app.tactic_profiles.list(category_of(sc));
    if (list.empty()) ImGui::TextDisabled("No presets in this scope yet.");
    for (const Profile* p : list) {
        g_state.preset_names.push_back(p->name);
        const bool sel = g_tac.selected_profile == p->id;
        const std::string label = p->name + (p->builtin ? " (built-in)" : "") + "##" + p->id;
        if (ImGui::Selectable(label.c_str(), sel)) load_profile(app, *p);
        if (ImGui::IsItemHovered() && !p->note.empty()) ImGui::SetTooltip("%s", p->note.c_str());
    }
    if (!app.tactic_profiles_error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", app.tactic_profiles_error.c_str());

    // what can be done with the loaded preset
    const Profile* lp = loaded_profile(app);
    const bool mine = lp && !lp->builtin && !lp->locked && !app.tactic_profiles.read_only;
    ImGui::BeginDisabled(!mine);
    if (ImGui::SmallButton("Rename##tacren")) {
        std::snprintf(g_tac.rename_buf, sizeof(g_tac.rename_buf), "%s", lp ? lp->name.c_str() : "");
        ImGui::OpenPopup("Rename preset##tacren");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete##tacdel")) ImGui::OpenPopup("Delete preset?##tacdel");
    ImGui::EndDisabled();
    if (lp && !mine && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Built-in presets cannot be renamed or deleted: duplicate one first.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!lp || app.tactic_profiles.read_only);
    if (ImGui::SmallButton("Duplicate##tacdup") && lp) {
        const std::string src = lp->id;  // the store may reallocate while it adds the copy
        const std::string id = app.tactic_profiles.duplicate(src, app.tactic_profiles.unique_name(lp->category, lp->name + " copy"), stamp());
        if (id.empty()) app.notify("The preset could not be duplicated", true);
        else {
            g_tac.selected_profile = id;
            persist_profiles(app, "Preset duplicated");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton("Export...##tacexp")) ImGui::OpenPopup("Export presets##tacexp");
    ImGui::SameLine();
    if (ImGui::SmallButton("Import...##tacimp")) {
        FilePicker& fp = g_tac.import_fp;
        fp.mode = PickMode::Open;
        fp.title = "Choose a Turbo preset file";
        fp.exts = {".json"};
        fp.key = "tactics.import";
        fp.start = app.bridge.dir();
        ImGui::OpenPopup("##tacimppick");
    }
    ImGui::BeginDisabled(!lp);
    checkbox_right("Compare with the loaded preset##taccmp", &g_tac.compare);
    ImGui::EndDisabled();
    if (!lp) g_tac.compare = false;
    g_state.compare = g_tac.compare && lp;
    if (g_state.compare) {
        g_state.compare_lines = compare_lines(*lp);
        if (g_state.compare_lines.empty()) ImGui::TextDisabled("Nothing differs from \"%s\".", lp->name.c_str());
        else ImGui::TextDisabled("%zu setting(s) differ from \"%s\":", g_state.compare_lines.size(), lp->name.c_str());
        for (const std::string& l : g_state.compare_lines) ImGui::TextWrapped("%s", l.c_str());
    }
    draw_rename_dialog(app);
    draw_delete_dialog(app);
    draw_export_dialog(app, sc);
    draw_import(app);
    g_state.import_open = g_tac.plan_open;
    g_state.import_items = g_tac.plan_open ? g_tac.plan.items.size() : 0;
}

void draw_centre(App& app) {
    const bool opp_scope = kScopes[g_tac.scope] == SliderScope::Opposition;
    if (opp_scope) draw_opp_card(app);
    const TeamFormation& tf = team_formation(app);
    const auto& fb = fallback_formations();
    const Formation& shown = opp_scope ? g_tac.opp.opp_tf.formation.slots.empty() ? tf.formation : g_tac.opp.opp_tf.formation : shown_formation(app);
    if (!opp_scope) {
        const std::string own = tf.formation.name + (tf.saved ? " (your save)" : " (built-in shape)");
        const std::string cur = g_tac.formation_pick < 0 ? own : fb[size_t(g_tac.formation_pick)].name + " (built-in shape)";
        ImGui::SetNextItemWidth(S(190.0f));
        if (ImGui::BeginCombo("Formation (preview)##tacform", cur.c_str())) {
            if (ImGui::Selectable((own + "##tacown").c_str(), g_tac.formation_pick < 0)) g_tac.formation_pick = -1;
            for (size_t i = 0; i < fb.size(); ++i)
                if (ImGui::Selectable((fb[i].name + " (built-in shape)##tacfb" + std::to_string(i)).c_str(), int(i) == g_tac.formation_pick)) g_tac.formation_pick = int(i);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previews another formation. Nothing is written: the pitch only shows it.");
    }
    // phase and layers
    static const char* const kPhase[3] = {"With ball##tacph0", "Without ball##tacph1", "Overall##tacph2"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        ImGui::RadioButton(kPhase[i], &g_tac.phase, i);
    }
    checkbox_right("Schematic layers (dashed)##tacd", &g_tac.show_derived);
    checkbox_right("Modelled layer (hatched, off by default)##tach", &g_tac.show_heat);
    checkbox_right("Roaming range (stippled, off by default)##tacr", &g_tac.show_roam);
    checkbox_right("Exposure hint (hatched, off by default)##tace", &g_tac.show_risk);

    PreviewOptions opt;
    opt.phase = static_cast<Phase>(std::clamp(g_tac.phase, 0, 2));
    opt.show_heat = g_tac.show_heat;
    opt.show_roam = g_tac.show_roam;
    opt.show_risk = g_tac.show_risk;
    opt.selected_slot = g_tac.selected_slot < int(shown.slots.size()) ? g_tac.selected_slot : -1;
    opt.effects_greyed = g_tac.off;
    static const SliderSet no_sliders;
    const SliderSet& team_set = opp_scope ? g_tac.opp.ghost : g_tac.working;
    const SliderSet& pos_set = opp_scope ? no_sliders : g_tac.working;
    const PreviewModel& pm = g_tac.preview.get(shown, team_set, pos_set, opt, uint64_t(uint32_t(app.gen)));

    g_state.preview_builds = g_tac.preview.builds();
    PitchDrawOptions po;
    po.show_derived = g_tac.show_derived;
    po.hidden = hidden_for(team_set, opt.phase, opt.selected_slot);
    std::string note;
    if (opp_scope) {
        note = "Ghost view: " + g_tac.opp.opp_name + "'s " + shown.name + " shape with the solver's changes. Turbo's preview, not what the game does.";
    } else if (g_tac.formation_pick >= 0) {
        note = "Preview of Turbo's built-in " + shown.name + " shape: nothing is written.";
    } else if (tf.saved) {
        note = app.sel_team == app.bridge.state().user_team
                   ? "Your saved tactic as of your last save: " + tf.source + ". Change it in the game's Edit Tactic; nothing on this pitch is written."
                   : "Saved data: " + tf.source + " (the club's saved tactic row). Nothing on this pitch is written.";
    } else {
        note = "Built-in shape (Turbo's fallback table), not read from your save: " + tf.note + ".";
    }
    po.dots_note = note;
    const PitchViewResult r = draw_pitch_view("##tacpitch", pm, po, 0.0f);
    g_state.chip = r.chip;
    g_state.pitch_primitives = r.primitives;
    g_state.pitch_capped = r.capped;
    g_state.heat_stride = r.heat_stride;
    g_state.pitch_labels = r.labels;
    g_state.pitch_strip = pm.preview_strip;
    g_state.formation_name = shown.name;
    g_state.formation_saved = opp_scope ? g_tac.opp.opp_tf.saved : (g_tac.formation_pick < 0 && tf.saved);
    g_state.formation_source = g_state.formation_saved ? (opp_scope ? g_tac.opp.opp_tf.source : tf.source) : "";
    g_state.formation_note = note;
    g_state.phase = g_tac.phase;
    if (r.clicked_dot >= 0 && !opp_scope) g_tac.selected_slot = g_tac.selected_slot == r.clicked_dot ? -1 : r.clicked_dot;
    g_state.selected_slot = g_tac.selected_slot;
    ImGui::TextDisabled("%s", pm.preview_strip.c_str());
    if (g_tac.selected_slot >= 0 && g_tac.selected_slot < int(shown.slots.size()) && !opp_scope)
        ImGui::TextDisabled("Selected: %s (slot %d). Position sliders apply to this dot in the preview; click it again to deselect.",
                            shown.slots[size_t(g_tac.selected_slot)].label.c_str(), g_tac.selected_slot);
    if (g_tac.off) ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Effect layers are grey: tactics_off.txt is present.");
    ImGui::TextDisabled("Solid = saved data or Turbo's shape, dashed = schematic (the slider value, not a distance), hatched = Turbo's estimate. Nothing here is the game's own output.");
}

void draw_right(App& app) {
    const SliderScope sc = kScopes[g_tac.scope];
    // tactics channel E: your club's live tactic record, its summary line and the card (ui_live_tactic.cpp), in the My team scope
    if (sc == SliderScope::Team) {
        draw_live_tactic(app);
        ImGui::Separator();
    }
    // 2.5: everything below is preview-only or untested: collapsed. The proven controls (channel E, above) are the tab's working part
    ImGui::SetNextItemOpen(g_tac.exp_open || g_force_exp, ImGuiCond_Always);
    const bool exp_shown = ImGui::CollapsingHeader("Experimental (preview-only and untested settings)###tacexp");
    if (!g_force_exp) g_tac.exp_open = exp_shown;  // a test that keeps it open does not change what the person left
    g_state.experimental_open = exp_shown;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Most of these settings only change Turbo's picture of a tactic, not the game. A setting marked [game variable] or "
                          "[database row] is sent when you press Apply and has not been proven in a played match.");
    if (!exp_shown) {
        if (sc != SliderScope::Team)
            ImGui::TextWrapped("This scope has only experimental settings. Open the section above to see them: most are Turbo's preview, not what the game does.");
        return;
    }
    // legacy fields (EA removed them in FC 25) are drawn only behind "show legacy fields"
    std::vector<const SliderDef*> visible;
    size_t legacy = 0;
    for (const SliderDef* d : sliders_in_scope(sc)) {
        if (legacy_hidden(*d)) {
            ++legacy;
            if (!g_tac.show_legacy) continue;
        }
        visible.push_back(d);
    }
    g_state.legacy_hidden = g_tac.show_legacy ? 0 : legacy;
    g_state.show_legacy = g_tac.show_legacy;
    const size_t prev = count_preview_only(visible);
    g_state.preview_strip = std::to_string(prev) + " of " + std::to_string(visible.size()) + " visible settings are preview-only";
    ImGui::TextDisabled("%s", g_state.preview_strip.c_str());
    if (legacy > 0) {
        checkbox_right("Show legacy fields##taclegacy", &g_tac.show_legacy);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%zu control(s) EA removed in FC 25. Hidden by default; Turbo's preview only, nothing is written to the game.", legacy);
    }
    refresh_readback(app, visible);
    std::map<std::string, msetup::VarLine> lines;  // the host's overrides, read once per frame
    if (app.match_setup)
        for (const msetup::VarLine& l : app.match_setup->vars()) lines[l.name] = l;
    // the cards: consecutive groups of the registry order
    std::vector<std::string> groups;
    for (const SliderDef* d : visible)
        if (std::find(groups.begin(), groups.end(), d->group) == groups.end()) groups.push_back(d->group);
    const Profile* cmp = g_tac.compare ? loaded_profile(app) : nullptr;
    // the footer lists what the game will receive: it grows with the list (but never takes more than 60% of the column)
    std::vector<std::string> unsent;
    const std::vector<EffWrite> writes = effective_writes(g_tac.working, &unsent);
    const float nlines = float(std::max<size_t>(1, writes.size()) + unsent.size());
    const float footer = std::min(ImGui::GetContentRegionAvail().y * 0.6f,
                                  ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetTextLineHeightWithSpacing() * (nlines + 2.0f) + S(10.0f));
    // 2.5: the live tactic card above is open by default, so the rows keep a minimum height and the column scrolls to the footer
    ImGui::BeginChild("##tacsliders", ImVec2(0, std::max(S(220.0f), ImGui::GetContentRegionAvail().y - footer)));
    for (const std::string& g : groups) {
        ImGui::PushID(g.c_str());
        if (ImGui::CollapsingHeader(g.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            for (const SliderDef* d : visible)
                if (d->group == g) draw_row(app, *d, cmp, lines);
        ImGui::PopID();
    }
    ImGui::EndChild();

    // footer: what the game will receive, N changed, Apply / Revert / Save as preset
    ImGui::SeparatorText("What the game will receive");
    g_state.receive_title = "What the game will receive";
    for (const EffWrite& e : writes) {
        const SliderDef* d = find_slider(e.w.key);
        const bool offset = !e.note.empty();
        // a database row names whose row it is (the selected club or player, every referee)
        const std::string line = e.w.target + " = " + (d && !offset ? name_of(*d, e.w.value) : std::to_string(e.w.value)) + for_owner(app, e.w.target) + "  [" +
                                 written_badge(e.w.status == SliderStatus::Live ? WriteKind::GameVar : WriteKind::DbRow) + "]" + (offset ? "  (" + e.note + ")" : "");
        g_state.receive.push_back(line);
        ImGui::TextUnformatted(line.c_str());
    }
    if (writes.empty()) ImGui::TextDisabled("Nothing: every setting is \"the game decides\".");
    for (const std::string& u : unsent) {
        g_state.not_written.push_back(u);
        ImGui::TextDisabled("Not written: %s", u.c_str());
    }
    const int changed = int(diff_sliders(g_tac.working, g_tac.applied).size());
    g_state.changed = changed;
    ImGui::Text("%d changed", changed);
    ImGui::SameLine();
    checkbox_right("Dry run##tacdry", &g_tac.dry_run);
    ImGui::BeginDisabled(g_tac.off);
    if (ImGui::Button("Apply##tac")) apply_changes(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sends every switched-on setting. Untested settings ask first. A setting switched off gets the game's default back.");
    draw_confirm_dialog(app);
    ImGui::SameLine();
    if (ImGui::Button("Revert##tac")) {
        g_tac.working = g_tac.applied;
        app.notify("Tactics reverted to what was last applied");
    }
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::InputTextWithHint("##tacname", "preset name", g_tac.preset_name, sizeof(g_tac.preset_name));
    ImGui::SameLine();
    if (ImGui::Button("Save as preset##tac")) {
        const std::string nm = g_tac.preset_name;
        if (nm.empty()) {
            app.notify("Type a name for the preset first", true);
        } else {
            Profile p;
            p.category = category_of(sc);
            p.name = app.tactic_profiles.unique_name(p.category, nm);
            for (const SliderDef* d : visible)
                if (g_tac.working.has(d->key)) p.sliders.set(d->key, g_tac.working.value_or_default(d->key), g_tac.working.is_enabled(d->key));
            if (sc == SliderScope::Team) p.formation = shown_formation(app).name;
            p.created = stamp();
            const std::string id = app.tactic_profiles.add(p);
            if (id.empty()) {
                app.notify("Preset not saved: the preset file is read-only (written by a newer Turbo)", true);
            } else if (persist_profiles(app, "Preset saved: " + p.name)) {
                g_tac.selected_profile = id;
                g_tac.preset_name[0] = 0;
            }
        }
    }
}

void draw_live_banner(App& app) {
    int active = 0;
    if (app.match_setup) {
        for (const msetup::VarLine& l : app.match_setup->vars())
            if (l.active) ++active;
    }
    if (active > 0) {
        g_state.banner = "Game overrides are active (" + std::to_string(active) + "): they last until the game closes and apply to every match you play.";
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", g_state.banner.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear overrides##tac")) {
            std::string why;
            if (!app.match_setup->clear_all(why)) app.notify("Clear overrides: " + why, true);
            else {
                g_tac.live_written = 0;
                // only the game variables are gone: a database row keeps its applied state, so switching it off + Apply still puts the
                // value before Turbo's first write back
                for (const SliderDef& d : slider_registry())
                    if (d.status == SliderStatus::Live) g_tac.applied.reset(d.key);
                app.notify("Game overrides cleared: the game decides again");
            }
        }
    }
}

void draw_body(App& app) {
    g_state = TacticsTabState();
    sync_owners(app);  // the club and player rows: the selected club's and player's values
    const std::string shown_error = g_tac.error;  // an error stays shown while it keeps happening, then goes
    g_tac.error.clear();
    g_state.scope = g_tac.scope;
    g_state.dry_run = g_tac.dry_run;
    g_state.selected_profile = g_tac.selected_profile;
    g_state.writes_done = g_tac.writes_done;
    refresh_off(app);
    g_state.off = g_tac.off;
    g_state.switches = g_tac.sw.files();
    if (g_tac.off) {
        g_state.off_line = "Tactics are off: turbo_output\\tactics_off.txt exists. Nothing is written and the effect layers are grey.";
        ImGui::TextWrapped("%s", g_state.off_line.c_str());
    } else if (!g_state.switches.empty()) {
        std::string line = "Some tactics channels are off:";
        for (const KillSwitch& k : kill_switches())
            if (g_tac.sw.present.count(k.file)) line += std::string(" turbo_output\\") + k.file + " (" + k.what + ")";
        g_state.off_line = line + ". Their rows are grey and nothing is written for them.";
        ImGui::TextWrapped("%s", g_state.off_line.c_str());
    }
    if (!g_tac.snaps_error.empty())
        ImGui::TextWrapped("turbo_output\\tactics_snapshot.json is not used (%s): Reset cannot put database rows back.", g_tac.snaps_error.c_str());
    draw_live_banner(app);
    if (!shown_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error: %s", shown_error.c_str());
    const float total = ImGui::GetContentRegionAvail().x;
    const ImGuiChildFlags cf = ImGuiChildFlags_Borders;
    ImGui::BeginChild("##tacleft", ImVec2(total * 0.22f, 0), cf | ImGuiChildFlags_ResizeX);
    try {
        draw_left(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##taccentre", ImVec2(total * 0.46f, 0), cf | ImGuiChildFlags_ResizeX);
    try {
        draw_centre(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##tacright", ImVec2(0, 0), cf);
    try {
        draw_right(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    g_state.error = g_tac.error.empty() ? shown_error : g_tac.error;
}

}  // namespace

const TacticsTabState& tactics_tab_state() { return g_state; }

bool tactics_off_now(App& app) {
    std::error_code ec;
    return fs::exists(off_file(app), ec);
}

void draw_tactics(App& app) {
    try {
        draw_body(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error: %s", e.what());
    } catch (...) {
        g_tac.error = "unknown error";
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error");
    }
}

void tactics_set_experimental_open(bool open) { g_force_exp = open; }

void tactics_clear_on_exit(App& app) {
    try {
        if (g_tac.live_written > 0 && app.match_setup) {
            std::string why;
            app.match_setup->clear_all(why);
            g_tac.live_written = 0;
        }
    } catch (...) {
    }
}

}  // namespace turbo
