// FC 27 LE Turbo GUI - your club's live tactic, tactics channel E (see ui_live_tactic.h, core/team_sheet.h).
//
// Frame-loop rules as in ui_tactics.cpp: never throws (an inline error instead), file I/O only on a click, at the first look (the undo
// file) and every 2 s (the switch files), the live record is read at most once a second while the card is drawn. The kick-off check is
// the service's (core/team_sheet.h kickoff_tick: Turbo's game_tick listener, this card's tick only as a fallback), not the card's.
#include "ui_live_tactic.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

#include "app.h"
#include "core/tactics_db.h"
#include "core/tactics_status.h"
#include "core/team_sheet.h"
#include "imgui.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;
using namespace tsheet;

// WP-B2 branch (c) of gate X3 (docs/TACTICS_OVERHAUL_DECISION.md 4.3: Edit Tactic loads from a UI-side cache, no live write reaches it): the
// card carries this badge. OFF until the coordinator's X3 result names branch (c); branch (a) merges 2.4/channel-e3 instead, (b) adds a copy
// target (JobChecks::copy_targets).
constexpr bool kEditTacticAfterReload = false;
constexpr const char* kEditTacticAfterReloadBadge = "Edit Tactic shows these values after the next career load";

struct Lt {
    // the read-out
    bool have = false;
    LiveTactic lt;
    std::string read_err;
    double next_read = -1.0;
    // the editor (filled from the read-out of a record; kept while the same record is shown)
    std::string editor_key;
    int line_height = 1, build_up = 0, approach = 0, slot = 0;
    std::array<int, kSlots> role_id{}, focus{};
    // switch files (every 2 s)
    Switches sw;
    double next_switch = -1.0;
    // the undo file
    UndoRecord undo;
    bool have_undo = false, undo_loaded = false;
    std::string undo_err;
    // the job in flight
    bool pending = false, pending_undo = false;
    double pending_since = -1.0;
    UndoRecord prev;
    bool prev_have = false, merged = false;
    std::vector<std::string> undo_skipped;
    // lines
    std::string last, mirror_line, error;
    std::vector<std::string> overwritten;
    // the read-back after a write, and when the window is shown again
    double recheck_at = -1.0;
    bool was_visible = false;
    // after Team Management was open: re-read twice (the game's Yes copies its screen into the record and autosaves first); the prompt
    bool tm_was = false;
    bool tm_watch = false;         // Team Management closed after a write: every hub read checks for the game's copy of its screen
    double next_watch_read = 0.0;  // wall seconds (wall_now): the tick reads once a second while watching
    std::string replaced;  // "the game replaced your tactic (LH 50 / Balanced / Balanced). Re-apply?" ("" = none)
    std::string other_note;  // "the active tactic is another one now (...)": said, no Re-apply
    std::string prompt_session;  // the Lua session and career load the prompt was made in: another one drops it (never re-applied there)
    long long prompt_gen = -1;
    // the confirmation
    bool confirm_request = false;
    std::vector<std::string> confirm_lines;
    bool details_open = false;  // 2.5: the "More" node (roles per slot, the live record, details)
};
Lt g;
LiveTacticState g_state;
long long g_result_seq = 0;  // never reset: counts the jobs whose result came back
LiveJobResult g_result;
bool g_force_details = false;  // tests: the "More" node stays open (live_tactic_set_details_open); survives live_tactic_reset
std::function<double()> g_wall;  // tests: the wall clock (live_tactic_test_wall_clock)

// Wall time in seconds. Not Turbo's QPC / steady clocks: in game they ran 3x wall time on 2026-10-13 (IMPL_tactic.md)
double wall_now() {
    if (g_wall) return g_wall();
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void check_replaced(App& app);  // read_now's last step while watching

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }
fs::path undo_path(App& app) { return out_dir(app) / undo_file(); }

std::string stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    return b;
}

void log_line(App& app, const std::string& s) {
    app.log(s);
    if (app.log_hook) app.log_hook(s);
}

void refresh_switches(App& app, bool now) {
    if (!now && app.now < g.next_switch) return;
    g.sw = tsheet::read_switches(out_dir(app));
    g.next_switch = app.now + 2.0;
}

Context ctx_of(App& app) {
    const BridgeState& st = app.bridge.state();
    Context c;
    c.load_gen = st.load_gen;
    c.user = int(st.user_team);
    c.managers = st.managers;
    c.in_cm = st.loaded && st.in_cm;
    c.allow_find = g.sw.opt_in && g.sw.blocking().empty() && !g.sw.find_off;
    c.phase = st.loaded ? st.match_phase : std::string();
    // G2 as the UI sees it now (the locate check aside): the game-thread jobs refuse when it no longer holds
    GateInput in;
    in.bridge = st.loaded;
    in.in_cm = st.in_cm;
    in.match_phase = st.match_phase;
    in.team_mgmt = st.team_mgmt;
    in.save_age = st.save_t > 0 ? (long long)(std::time(nullptr)) - st.save_t : -1;
    in.load_gen = st.load_gen;
    in.located_load_gen = st.load_gen;
    const Gates gs = evaluate(in);
    c.hub = gs.list.size() > 1 && gs.list[1].pass();
    return c;
}

void load_undo_once(App& app) {
    if (g.undo_loaded) return;
    g.undo_loaded = true;
    std::string err;
    UndoRecord u;
    if (load_undo(undo_path(app), u, &err)) {
        g.undo = u;
        g.have_undo = true;
        g.undo_err.clear();
    } else {
        g.have_undo = false;
        std::error_code ec;
        g.undo_err = fs::exists(undo_path(app), ec) ? err : "";
    }
}

bool save_record(App& app, const UndoRecord& u, std::string* err = nullptr) {
    std::string e;
    if (!save_undo(undo_path(app), u, &e)) {
        log_line(app, "channel E: " + undo_path(app).string() + " not written: " + e);
        if (err) *err = e;
        return false;
    }
    g.undo = u;
    g.have_undo = true;
    return true;
}

std::string editor_key_of(const LiveTactic& lt) {
    std::string k = std::to_string(lt.at.user) + "/" + std::to_string(lt.at.record_index) + "/" + std::to_string(lt.formation) + "/" +
                    std::to_string(lt.source_formation) + "/" + std::to_string(lt.sheet_id);
    for (const SlotView& v : lt.slots) k += "/" + std::to_string(v.pid);
    return k;
}

void fill_editor(const LiveTactic& lt) {
    g.line_height = lt.line_height;
    g.build_up = lt.build_up;
    g.approach = lt.approach;
    for (int s = 0; s < kSlots; ++s) {
        g.role_id[size_t(s)] = lt.slots[size_t(s)].role_id;
        g.focus[size_t(s)] = lt.slots[size_t(s)].focus;
    }
    g.editor_key = editor_key_of(lt);
}

// One read of the live record (any reason it cannot be read is kept for the card)
bool read_now(App& app) {
    if (!app.team_sheet) {
        g.have = false;
        g.read_err = "this Turbo has no channel E service";
        return false;
    }
    LiveTactic lt;
    std::string why;
    const bool ok = app.team_sheet->read(ctx_of(app), lt, why);
    g.have = ok;
    g.read_err = ok ? "" : why;
    if (ok) {
        g.lt = lt;
        if (g.editor_key != editor_key_of(lt)) fill_editor(lt);
        g.overwritten.clear();
        if (g.have_undo && g.undo.state == "written") {
            // report 4.5 within the career load of the write; after a reload the record was rebuilt from the save
            const BridgeState& st = app.bridge.state();
            const bool same_load = g.undo.session == st.session && g.undo.load_gen == st.load_gen;
            const std::vector<std::string> rv = reverted(lt, g.undo);
            if (same_load)
                for (const std::string& r : rv) g.overwritten.push_back("Edit Tactic (or the game) overwrote the change: " + r);
            else if (!rv.empty())
                g.overwritten.push_back("Turbo's write of " + g.undo.when + " was in an earlier career load; the live tactic does not hold it now (" + rv.front() +
                                        (rv.size() > 1 ? ", ..." : "") + "): no in-game save after the write?");
        }
        check_replaced(app);
    }
    return ok;
}

// While watching (Team Management was closed after Turbo's write in this career load): every good read in the hub decides the prompt. The
// game's Yes may copy Edit Tactic's screen into the record at any time after Lua's flag cleared, so there is no deadline: the watch lasts
// until the user acts (Re-apply, Dismiss, Apply, Undo) or the career is reloaded. Never writes.
void check_replaced(App& app) {
    if (!g.tm_watch || !g.have || !g.have_undo || g.undo.state != "written") return;
    const BridgeState& st = app.bridge.state();
    if (!same_career(g.undo, st.session, st.load_gen).empty() || st.session != g.prompt_session || st.load_gen != g.prompt_gen) return;
    const std::string p = replaced_prompt(g.lt, g.undo);
    const std::string note = other_tactic_note(g.lt, g.undo);
    if (note != g.other_note) {
        g.other_note = note;
        if (!note.empty()) log_line(app, "channel E: after Team Management: " + note);
    }
    if (!p.empty() && p != g.replaced) {
        g.replaced = p;
        log_line(app, "channel E: after Team Management: " + p + " (waiting for the user's click)");
        app.notify("Turbo: " + p + " (Teams > Tactics > My team)", true);
    } else if (p.empty() && !g.replaced.empty()) {
        g.replaced.clear();
        log_line(app, "channel E: after Team Management: Turbo's values are in place again (prompt dropped)");
    }
}

Request request_from_editor() {
    Request r;
    r.line_height = g.line_height;
    r.build_up = g.build_up;
    r.approach = g.approach;
    for (int s = 0; s < kSlots; ++s) r.slots.push_back({s, g.role_id[size_t(s)], g.focus[size_t(s)]});
    return r;
}

GateInput gate_input(App& app, const std::string& plan_why, bool undo) {
    const BridgeState& st = app.bridge.state();
    GateInput in;
    in.sw = g.sw;
    in.bridge = st.loaded;
    in.in_cm = st.in_cm;
    in.match_phase = st.match_phase;
    in.team_mgmt = st.team_mgmt;
    in.save_age = st.save_t > 0 ? (long long)(std::time(nullptr)) - st.save_t : -1;
    in.load_gen = st.load_gen;
    in.located_load_gen = app.team_sheet ? app.team_sheet->located_load_gen() : -1;
    in.anchors = app.team_sheet ? app.team_sheet->anchors_status() : "this Turbo has no channel E service";
    in.locate = g.have ? "" : (g.read_err.empty() ? "not read yet" : g.read_err);
    in.g5 = g.have ? g.lt.g5 : "";
    in.plan = plan_why;
    in.session_off = app.team_sheet ? app.team_sheet->session_off() : "";
    in.undo = undo;
    return in;
}

std::string plan_line(const FieldWrite& w, const LiveTactic& lt) {
    if (w.copy) return "";
    const bool kept = w.old_value == w.new_value;
    switch (w.kind) {
        case FieldKind::Approach:
            return kept ? "Defensive Approach " + approach_text(w.old_value) + " (kept: written with the other two)"
                        : "Defensive Approach " + approach_text(w.old_value) + " -> " + approach_text(w.new_value);
        case FieldKind::LineHeight:
            return kept ? "Line Height " + std::to_string(w.old_value) + " (kept: written with the other two)"
                        : "Line Height " + std::to_string(w.old_value) + " -> " + std::to_string(w.new_value) + " (" + line_band_text(w.new_value) + ")";
        case FieldKind::BuildUp:
            return kept ? "Build-Up Style " + build_up_text(w.old_value) + " (kept: written with the other two)"
                        : "Build-Up Style " + build_up_text(w.old_value) + " -> " + build_up_text(w.new_value);
        case FieldKind::Role: {
            const SlotView& v = lt.slots[size_t(std::clamp(w.slot, 0, kSlots - 1))];
            return "slot " + std::to_string(w.slot) + " (" + position_name(v.position) + ") role word " + std::to_string(w.old_value) + " -> " +
                   std::to_string(w.new_value) + " = group " + std::to_string(role_group(w.new_value)) + " / role " + std::to_string(role_id_of(w.new_value)) +
                   " / focus " + std::to_string(focus_of(w.new_value)) + " (word, role id and focus together)";
        }
        case FieldKind::RoleId:
        case FieldKind::Focus: return "";
    }
    return "";
}

std::vector<std::string> plan_lines(const Plan& p, const LiveTactic& lt) {
    std::vector<std::string> out;
    for (const FieldWrite& w : p.writes) {
        std::string l = plan_line(w, lt);
        if (!l.empty()) out.push_back(l);
    }
    if (!out.empty()) out.push_back("then the sheet's dirty flag = 1 (as the game's UpdateSheet does); all of it read back in the same job");
    return out;
}

// The card's safety line (findings 2: Yes copies Edit Tactic's screen into the team sheet and autosaves)
const char* safety_text() {
    return "If you open Edit Tactic and answer Yes to 'save your changes to your team sheet', the game commits its screen values and AUTOSAVES "
           "your career.";
}

// ---------------------------------------------------------------------------------------------------------------- the cm_mentalities row
// 2.4 (decision 3.2): the row is written in the SAME game-thread job as the live record (core/team_sheet.h DbRowWrite). Here, on the render
// thread, Turbo's database layer finds the row and copies the table's layout into the plan; the job re-checks it, writes, reads back and
// rolls back with the live record. Kill switches channel_e_db_off.txt / tactics_db_off.txt leave it out (the live record is still written).

// The cm_mentalities row of a club's live tactic: teamid == club, activetactic == 1, sourceformationid == the record's (teamsheetid == the
// sheet's when the save has that field). Exactly one row, else none (why says it)
uint64_t mirror_row(App& app, int team, int32_t source_formation, int32_t sheet_id, const Table*& t, std::string& why) {
    t = app.db.table("cm_mentalities");
    if (!t) {
        why = "this save has no cm_mentalities table";
        return 0;
    }
    if (!t->field("artificialkey") || !t->field("activetactic") || !t->field("sourceformationid")) {
        why = "cm_mentalities lacks artificialkey / activetactic / sourceformationid: the row cannot be told apart";
        return 0;
    }
    const TeamRows rows = team_rows(app.db, *t, team);
    if (rows.key_missing) {
        why = "cm_mentalities has no teamid field";
        return 0;
    }
    std::vector<uint64_t> hit;
    for (uint64_t rec : rows.recs) {
        if (app.db.get_int(*t, rec, "activetactic", -1) != 1) continue;
        if (app.db.get_int(*t, rec, "sourceformationid", -2) != source_formation) continue;
        if (t->field("teamsheetid") && app.db.get_int(*t, rec, "teamsheetid", -2) != sheet_id) continue;
        hit.push_back(rec);
    }
    if (hit.size() != 1) {
        why = std::to_string(hit.size()) + " cm_mentalities rows of club " + std::to_string(team) + " are the active tactic with sourceformationid " +
              std::to_string(source_formation) + " (exactly 1 needed)";
        return 0;
    }
    return hit[0];
}

// One int field of a row (false when the table has no such field or it is not an int)
bool row_int(App& app, const Table& t, uint64_t rec, const std::string& name, int64_t& v) {
    const Field* f = t.field(name);
    if (!f) return false;
    Value x;
    if (!app.db.get(t, rec, *f, x) || x.type != FieldType::Int) return false;
    v = x.i;
    return true;
}

// The row's identity as the job re-checks it: these fields, when the save has them, must still hold what they hold now
void add_row_checks(App& app, DbRowWrite& row, bool slots) {
    std::vector<std::string> names = {"artificialkey", "teamid", "activetactic", "sourceformationid", "teamsheetid", "code"};
    if (slots)
        for (int s = 0; s < kSlots; ++s) {
            names.push_back("playerid" + std::to_string(s));
            names.push_back("position" + std::to_string(s));
        }
    for (const std::string& n : names) {
        const Field* f = row.table.field(n);
        Value v;
        if (f && app.db.get(row.table, row.rec, *f, v) && (v.type == FieldType::Int || v.type == FieldType::String)) row.checks.push_back({n, v});
    }
}

// An Apply: the row of the live tactic with the fields the plan's map-record writes mirror to (plan.rows), or why there is none (plan.rows_note)
void attach_db_row(App& app, Plan& plan, const LiveTactic& live) {
    plan.rows.clear();
    plan.rows_note.clear();
    bool mirrored = false;
    for (const FieldWrite& w : plan.writes) mirrored = mirrored || !mirror_field_name(w).empty();
    if (!mirrored) return;  // nothing of the plan lives in the row
    refresh_switches(app, true);
    const std::string block = g.sw.db_row_block();
    if (!block.empty()) {
        plan.rows_note = block;
        return;
    }
    if (!app.connected()) {
        plan.rows_note = "the career database is not connected";
        return;
    }
    const Table* t = nullptr;
    std::string why;
    const uint64_t rec = mirror_row(app, plan.user, plan.source_formation, plan.sheet_id, t, why);
    if (!rec) {
        plan.rows_note = why;
        return;
    }
    const int64_t key = app.db.get_int(*t, rec, "artificialkey", -1);
    auto get = [&](const std::string& name, int64_t& v) { return row_int(app, *t, rec, name, v); };
    auto get_str = [&](const std::string& name, std::string& v) {
        const Field* f = t->field(name);
        Value x;
        if (!f || !app.db.get(*t, rec, *f, x) || x.type != FieldType::String) return false;
        v = x.s;
        return true;
    };
    why = mirror_row_check(get, live, plan.writes, get_str);
    if (!why.empty()) {
        plan.rows_note = "artificialkey " + std::to_string(key) + ": " + why;
        return;
    }
    DbRowWrite row;
    row.table = *t;
    row.rec = rec;
    row.key = key;
    row.fields = apply_row_fields(plan, get);
    if (row.fields.empty()) {
        plan.rows_note = row.label() + " already holds these values";
        return;
    }
    add_row_checks(app, row, true);
    plan.rows.push_back(std::move(row));
}

// An undo: the row Turbo wrote (the undo file's db_mirror) with the fields that still hold Turbo's values and whose live field this undo puts
// back, or why it is left as it is (plan.rows_note)
void attach_undo_db_row(App& app, Plan& plan, const UndoRecord& u) {
    plan.rows.clear();
    plan.rows_note.clear();
    if (u.mirror.empty() || u.mirror_state != "written") return;
    refresh_switches(app, true);
    const Table* t = app.connected() ? app.db.table(u.mirror_table.empty() ? std::string("cm_mentalities") : u.mirror_table) : nullptr;
    const uint64_t rec = (t && t->field("artificialkey")) ? app.db.find(*t, "artificialkey", u.mirror_row_key) : 0;
    if (!rec) {
        plan.rows_note = "row " + std::to_string(u.mirror_row_key) + " not found";
        return;
    }
    auto get = [&](const std::string& n, int64_t& v) { return row_int(app, *t, rec, n, v); };
    std::string why = undo_row_check(g.sw, get, u);
    if (!why.empty()) {
        plan.rows_note = why;
        return;
    }
    std::vector<std::string> left;
    DbRowWrite row;
    row.table = *t;
    row.rec = rec;
    row.key = u.mirror_row_key;
    row.fields = undo_row_fields(u, plan, get, left, &g.lt);
    if (row.fields.empty()) {
        std::string l;
        for (const auto& x : left) l += (l.empty() ? "" : ", ") + x;
        plan.rows_note = "nothing to put back" + (l.empty() ? std::string() : " (" + l + ")");
        return;
    }
    add_row_checks(app, row, false);
    plan.rows.push_back(std::move(row));
}

// The card's line about the row after a job
std::string rows_line(const ApplyResult& r) {
    if (r.rows.empty()) return r.rows_note.empty() ? std::string() : "cm_mentalities not " + std::string(r.undo ? "put back" : "written") + ": " + r.rows_note;
    std::string s;
    for (const DbRowWrite& row : r.rows) {
        s += (s.empty() ? "" : "; ") + row.label() + ":";
        if (row.fields.empty()) s += " already holds these values";
        for (size_t i = 0; i < row.fields.size(); ++i)
            s += (i ? ", " : " ") + row.fields[i].name + " " + std::to_string(row.fields[i].old_value) + " -> " + std::to_string(row.fields[i].new_value);
    }
    return s + (r.ok ? " (in the same job, read back)" : "");
}

// ---------------------------------------------------------------------------------------------------------------- Apply / Undo
// Apply (the editor's values) and Re-apply (Turbo's last values after the game replaced them): one guarded job, the live record and the
// cm_mentalities row together; the undo file is written first
// Returns true when the job was queued for the game thread; `why_out` gets the reason when not (the notice says it too)
bool apply_request(App& app, const Request& req, bool reapply, std::string* why_out = nullptr) {
    auto svc = app.team_sheet;
    if (why_out) why_out->clear();
    auto refuse = [&](const std::string& why) {
        if (why_out) *why_out = why;
        return false;
    };
    if (!svc) return refuse("this Turbo has no channel E service");
    const char* what = reapply ? "Live tactic not re-applied: " : "Live tactic not written: ";
    refresh_switches(app, true);
    load_undo_once(app);
    if (!read_now(app)) {
        app.notify(what + g.read_err, true);
        return refuse(g.read_err);
    }
    const BridgeState& st = app.bridge.state();
    Plan plan;
    const auto allow = svc->role_allow(st.load_gen);
    std::string pw = make_plan(g.lt, req, plan, allow.get());
    if (pw.empty() && plan.writes.empty()) pw = "nothing changed";
    plan.load_gen = st.load_gen;
    const Gates gs = evaluate(gate_input(app, pw, false));
    if (!gs.pass()) {
        app.notify(what + gs.first(), true);
        return refuse(gs.first());
    }
    attach_db_row(app, plan, g.lt);  // the cm_mentalities row, in the same job
    UndoRecord rec = UndoRecord::from_plan(plan);
    rec.dirty_before = g.lt.dirty;  // the job reports the flag it found; until then the read-out's (a merge keeps the first write's)
    rec.when = stamp();
    rec.session = st.session;
    rec.load_gen = st.load_gen;
    g.prev = g.undo;
    g.prev_have = g.have_undo;
    g.merged = false;
    if (g.have_undo && (g.undo.state == "written" || g.undo.state == "failed")) {
        // merged only within the same career load (merge_undo compares session and load_gen)
        g.merged = same_career(g.undo, st.session, st.load_gen).empty() && same_record(g.undo, g.lt);
        rec = merge_undo(g.undo, rec);
    }
    rec.when = stamp();
    rec.session = st.session;
    rec.load_gen = st.load_gen;
    rec.state = "pending";
    if (!plan.rows.empty()) rec.mirror_state = "pending";
    rec.note = reapply ? "re-apply after the game replaced the tactic" : "";
    std::string err;
    // the undo file first: it holds the values before the write before anything is written
    if (!save_record(app, rec, &err)) {
        app.notify(what + std::string("turbo_output\\") + undo_file() + " could not be written (" + err + ")", true);
        return refuse(std::string("turbo_output\\") + undo_file() + " could not be written (" + err + ")");
    }
    std::string why;
    if (!svc->submit(ctx_of(app), plan, why)) {
        if (g.prev_have) save_record(app, g.prev);
        else {
            rec.state = "aborted";
            rec.note = why;
            save_record(app, rec);
        }
        app.notify(what + why, true);
        g.last = "not queued: " + why;
        return refuse(why);
    }
    g.pending = true;
    g.pending_undo = false;
    g.pending_since = app.now;
    g.last = reapply ? "re-apply queued for the game thread ..." : "queued for the game thread ...";
    if (reapply) {
        g.replaced.clear();  // asked once; a later replacement asks again
        log_line(app, "channel E: re-apply of Turbo's tactic queued (the user's click after the game replaced it)");
    }
    return true;
}

void do_apply(App& app) { apply_request(app, request_from_editor(), false); }

// The opt-in file (call_team_sheet_on.txt): the card's checkbox and the tactic presets panel switch it the same way
void set_opt_in(App& app, bool on) {
    std::error_code ec;
    const fs::path p = out_dir(app) / opt_in_file();
    if (on) {
        fs::create_directories(out_dir(app), ec);
        std::ofstream(p, std::ios::binary | std::ios::trunc) << "Turbo channel E on: Turbo may write your club's live tactic (switched on in the Turbo window "
                                                             << stamp() << ")\n";
    } else {
        fs::remove(p, ec);
    }
    refresh_switches(app, true);
    log_line(app, std::string("channel E: ") + (g.sw.opt_in ? "switched on" : "switched off") + " (turbo_output\\" + opt_in_file() + ")");
}

// The Re-apply prompt belongs to the career load (and Lua session) it was made in: a reload drops it, so it never writes there
void drop_stale_prompt(App& app) {
    const BridgeState& st = app.bridge.state();
    if ((g.replaced.empty() && g.other_note.empty()) || (st.session == g.prompt_session && st.load_gen == g.prompt_gen)) return;
    g.replaced.clear();
    g.other_note.clear();
    g.tm_watch = false;
    log_line(app, "channel E: the Re-apply prompt was dropped: the career was (re)loaded");
}

// One click after the game replaced Turbo's tactic: Turbo's last values onto the live record (never automatic)
void do_reapply(App& app) {
    if (!g.have_undo) return;
    if (!read_now(app)) {
        app.notify("Live tactic not re-applied: " + g.read_err, true);
        return;
    }
    // the prompt must still be this career load's, this club's, this tactic record's, and the record must still miss Turbo's values
    const BridgeState& st = app.bridge.state();
    const std::string why = g.prompt_session != st.session || g.prompt_gen != st.load_gen
                                ? std::string("the prompt was made in another career load")
                                : reapply_check(g.lt, g.undo, st.session, st.load_gen);
    if (!why.empty()) {
        g.replaced.clear();
        log_line(app, "channel E: re-apply refused: " + why);
        app.notify("Live tactic not re-applied: " + why, true);
        return;
    }
    std::vector<std::string> skipped;
    const Request req = reapply_request(g.lt, g.undo, skipped);
    if (!skipped.empty()) {
        std::string s;
        for (const auto& x : skipped) s += (s.empty() ? "" : "; ") + x;
        log_line(app, "channel E: re-apply leaves out: " + s);
    }
    apply_request(app, req, true);
}

// Returns true when the undo job was queued; `why_out` gets the reason when not (the notice says it too)
bool do_undo(App& app, std::string* why_out = nullptr) {
    auto svc = app.team_sheet;
    if (why_out) why_out->clear();
    auto refuse = [&](const std::string& why) {
        if (why_out) *why_out = why;
        return false;
    };
    if (!svc) return refuse("this Turbo has no channel E service");
    refresh_switches(app, true);
    load_undo_once(app);
    if (!g.have_undo || (g.undo.state != "written" && g.undo.state != "failed")) {
        const std::string w = "turbo_output\\" + std::string(undo_file()) + " holds no write in place";
        app.notify("Nothing to put back: " + w, true);
        return refuse(w);
    }
    if (!read_now(app)) {
        app.notify("Nothing put back: " + g.read_err, true);
        return refuse(g.read_err);
    }
    Plan plan;
    std::vector<std::string> skipped;
    const BridgeState& st = app.bridge.state();
    const std::string w = make_undo_plan(g.lt, g.undo, st.session, st.load_gen, plan, skipped);
    g.undo_skipped = skipped;
    if (!w.empty()) {
        app.notify("Nothing put back: " + w, true);
        g.last = "undo: " + w;
        return refuse(w);
    }
    const Gates gs = evaluate(gate_input(app, "", true));
    if (!gs.pass()) {
        app.notify("Nothing put back: " + gs.first(), true);
        return refuse(gs.first());
    }
    attach_undo_db_row(app, plan, g.undo);  // the row Turbo wrote, in the same job
    std::string why;
    if (!svc->submit(ctx_of(app), plan, why)) {
        app.notify("Nothing put back: " + why, true);
        return refuse(why);
    }
    g.pending = true;
    g.pending_undo = true;
    g.pending_since = app.now;
    g.last = "undo queued for the game thread ...";
    return true;
}

void handle_result(App& app, const ApplyResult& r) {
    g.pending = false;
    g_result.seq = ++g_result_seq;  // 2.12: the tactic presets panel waits for the job it queued (live_tactic_result)
    g_result.ok = r.ok;
    g_result.written = r.ok && r.written;
    g_result.undo = r.undo;
    g_result.line = r.log_line.empty() ? r.message : r.log_line;
    g.last = r.log_line.empty() ? r.message : r.log_line;
    if (!r.message.empty() && r.log_line.find(r.message) == std::string::npos) g.last += ": " + r.message;
    g.mirror_line = rows_line(r);
    if (r.undo) {
        if (r.ok && r.written) {
            UndoRecord u = g.undo;
            u.state = "restored";
            u.note = g.undo_skipped.empty() ? "" : "partly: " + [&] {
                std::string s;
                for (const auto& x : g.undo_skipped) s += (s.empty() ? "" : "; ") + x;
                return s;
            }();
            add_job_rows(u, r.rows, true);
            if (r.rows.empty() && !r.rows_note.empty() && u.mirror_state == "written") u.mirror_note = "not put back: " + r.rows_note;
            save_record(app, u);
            g.overwritten.clear();
            g.replaced.clear();
            g.tm_watch = false;
            app.notify("Live tactic put back to the values before Turbo's write (read back)" + std::string(u.note.empty() ? "" : ": " + u.note));
        } else {
            UndoRecord u = g.undo;
            u.note = "undo " + r.stage + ": " + r.message;
            if (r.undone) u.state = "written";  // the undo itself was put back: Turbo's values are still in place
            save_record(app, u);
            app.notify("Live tactic not put back: " + r.message, true);
        }
        read_now(app);
        return;
    }
    if (r.ok && r.written) {
        UndoRecord u = g.undo;
        u.state = "written";
        add_job_writes(u, r.writes);  // copy targets the job wrote too (none by default)
        add_job_rows(u, r.rows, false);
        if (r.rows.empty() && u.mirror_state == "pending") u.mirror_state = u.mirror.empty() ? "" : "written";
        if (r.rows.empty() && u.mirror.empty()) {
            u.mirror_state = r.rows_note.empty() ? "" : "skipped";
            u.mirror_note = r.rows_note;
        }
        u.copies = r.copies;
        if (!g.merged) u.dirty_before = r.dirty_before;  // a merged record keeps the flag from before Turbo's first write
        save_record(app, u);
        g.recheck_at = app.now + 5.0;  // report 4.5: re-read 5 s later (an Edit Tactic overwrite shows then)
        fill_editor(r.after);
        g.replaced.clear();
        g.tm_watch = false;  // a new write: the next Team Management visit starts a new watch
        app.notify("Written into your club's live tactic and read back" + std::string(r.copies.empty() ? "" : " (" + r.copies + ")") +
                   (r.rows.empty() ? std::string() : ", its cm_mentalities row too") + ". After kick-off Turbo logs whether the match copied it. " +
                   safety_text());
    } else if (r.ok) {
        if (g.prev_have) save_record(app, g.prev);
        app.notify("Live tactic: " + r.message);
    } else if (r.restore_failed) {
        // putting back FAILED: values of this job may still be in the game. The record (this Apply merged with an earlier write) names every
        // field the job wrote (copies and the row too) with its value from before Turbo's first write, so Undo (allowed after "failed") can
        // put back each one that still holds Turbo's value
        UndoRecord u = g.undo;
        add_job_writes(u, r.writes);
        add_job_rows(u, r.rows, false);
        u.state = "failed";
        u.note = r.stage + ": " + r.message;
        save_record(app, u);
        app.notify("Live tactic not written: " + r.message, true);
    } else {
        // nothing of this Apply is in the game (refused before writing, or every field put back): the earlier record stays
        if (g.prev_have && g.prev.state == "written") {
            UndoRecord u = g.prev;
            u.note = "a later Apply was not written (" + r.stage + "): " + r.message;
            save_record(app, u);
        } else {
            UndoRecord u = g.undo;
            u.state = r.undone ? "undone" : "aborted";
            u.note = r.stage + ": " + r.message;
            if (u.mirror_state == "pending") u.mirror_state = "";
            save_record(app, u);
        }
        app.notify("Live tactic not written: " + r.message, true);
    }
    read_now(app);
}

// ---------------------------------------------------------------------------------------------------------------- drawing
ImVec4 seen_colour_e(SeenKind k) {
    switch (k) {
        case SeenKind::SeenInMatch: return ImVec4(0.45f, 0.85f, 0.45f, 1);
        case SeenKind::NoEffectSeen: return ImVec4(1.0f, 0.6f, 0.3f, 1);
        case SeenKind::Untested: return ImVec4(0.9f, 0.8f, 0.4f, 1);
        default: return ImVec4(0.6f, 0.6f, 0.6f, 1);
    }
}

std::string player_label(App& app, int32_t pid) {
    const PlayerRow* p = app.model.player(pid);
    if (p && !p->name.empty()) return p->name + " (" + std::to_string(pid) + ")";
    return "player " + std::to_string(pid);
}

// label left, box right (the form rows of the other tabs); no step buttons: one text box (typed into, Enter)
bool input_int(const char* text, const char* id, int* v, int lo, int hi, const char* tip = nullptr) {
    form_label(text, tip);
    ImGui::SetNextItemWidth(S(90.0f));
    const bool changed = ImGui::InputInt(id, v, 0, 0);
    *v = std::clamp(*v, lo, hi);
    return changed;
}

// A row of small buttons in the form's control column that wraps (at the column's left edge) instead of running out of the window
struct ButtonFlow {
    float x0, right;  // the column's left edge (window coordinates) and the window's right edge (screen coordinates)
    bool first = true;
    ButtonFlow() : x0(ImGui::GetCursorPosX()), right(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}
    void next(const std::string& label) {
        if (first) {
            first = false;
            return;
        }
        const float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine();
        if (ImGui::GetCursorScreenPos().x + w > right) {
            ImGui::NewLine();
            ImGui::SetCursorPosX(x0);
        }
    }
};

// "G2 career hub: not in the hub" -> "not in the hub": the person reads what follows the gate's code
std::string plain_gate(const std::string& s) {
    const size_t c = s.find(": ");
    if (s.size() > 2 && s[0] == 'G' && std::isdigit(static_cast<unsigned char>(s[1])) && c != std::string::npos) return s.substr(c + 2);
    return s;
}

void draw_confirm(App& app) {
    if (g.confirm_request) {
        ImGui::OpenPopup("Apply to the live tactic?##ltconfirm");
        g.confirm_request = false;
    }
    g_state.confirm_open = false;
    if (ImGui::BeginPopupModal("Apply to the live tactic?##ltconfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        g_state.confirm_open = true;
        g_state.confirm_lines = g.confirm_lines;
        ImGui::TextUnformatted("Turbo writes these values into your club's live tactic record (channel E):");
        for (const std::string& l : g.confirm_lines) ImGui::BulletText("%s", l.c_str());
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1),
                           "Not proven in a played match yet: after kick-off Turbo reads the match's own copy and logs whether it holds these values.");
        ImGui::TextUnformatted("Team Management (Edit Tactic) must be closed. The values before the write go to turbo_output\\channel_e_undo.json.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
        ImGui::TextWrapped("%s", safety_text());
        ImGui::PopStyleColor();
        if (ImGui::Button("Apply##ltyes")) {
            ImGui::CloseCurrentPopup();
            do_apply(app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##ltno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_body(App& app) {
    refresh_switches(app, false);
    load_undo_once(app);
    if (app.now >= g.next_read) {
        g.next_read = app.now + 1.0;
        read_now(app);
    }
    g_state.opt_in = g.sw.opt_in;
    g_state.locate = app.team_sheet ? app.team_sheet->locate_status() : "";
    g_state.read_ok = g.have;
    g_state.read_error = g.read_err;
    g_state.undo_state = g.have_undo ? g.undo.state : "";
    g_state.busy = g.pending;

    // the summary line (always, read-only)
    if (g.have) {
        g_state.summary = "Your club's live tactic (read now): \"" + (g.lt.name.empty() ? std::string("?") : g.lt.name) + "\", Defensive Approach " +
                          approach_text(g.lt.approach) + ", Line Height " + std::to_string(g.lt.line_height) + ", Build-Up Style " +
                          build_up_text(g.lt.build_up);
    } else {
        g_state.summary = "Your club's live tactic: not read (" + g.read_err + ")";
    }
    g_state.summary_shown = true;
    ImGui::TextWrapped("%s", g_state.summary.c_str());
    // 2.5: what the next match will use, in one plain sentence
    if (g.have) {
        const bool ours = g.have_undo && g.undo.state == "written" && same_career(g.undo, app.bridge.state().session, app.bridge.state().load_gen).empty();
        g_state.match_status = ours ? "The next match will use these values: Turbo's change is in place."
                                    : "The next match will use these values: the game's own tactic, Turbo has changed nothing in this career load.";
    } else {
        g_state.match_status = "The next match: Turbo cannot read your live tactic yet, so it cannot tell which values the match will use.";
    }
    ImGui::TextDisabled("%s", g_state.match_status.c_str());
    for (const std::string& o : g.overwritten) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
        ImGui::TextWrapped("%s", o.c_str());
        ImGui::PopStyleColor();
    }
    g_state.overwritten = g.overwritten;
    // after Team Management: the game replaced Turbo's values -> one click puts them back (never automatic)
    drop_stale_prompt(app);  // made in another career load: never shown (nor clicked) here
    g_state.replaced = g.replaced;
    g_state.other_note = g.other_note;
    g_state.can_reapply = !g.replaced.empty() && !g.pending && app.team_sheet != nullptr;
    if (!g.other_note.empty()) ImGui::TextDisabled("%s", g.other_note.c_str());
    if (!g.replaced.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
        ImGui::TextWrapped("%s", g.replaced.c_str());
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(!g_state.can_reapply);
        if (ImGui::Button("Re-apply##ltreapply")) do_reapply(app);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Writes Turbo's last values into the live record again (the same guarded job as Apply, with its gates). Turbo never "
                              "does this by itself.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss##ltreplacedno")) {
            g.replaced.clear();
            g.tm_watch = false;  // the user decided: no more asking until the next Team Management visit
        }
    }

    ImGui::SetNextItemOpen(true, ImGuiCond_Once);  // 2.5: the proven controls are the tab's working part: open from the first look
    g_state.card_open = ImGui::CollapsingHeader("Edit the live tactic (channel E)##ltcard");
    if (!g_state.card_open) return;
    ImGui::PushID("ltcard");
    // badges (Phase 0: how it is written / what was seen)
    const TacticStatus bs = live_sheet_status("live.line_height", "Live tactic");
    g_state.written = written_badge(bs.written);
    g_state.seen = seen_badge(bs);
    g_state.tag = status_tag(bs);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.95f, 1), "[%s]", g_state.written.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", written_help(bs.written));
    ImGui::SameLine();
    ImGui::TextColored(seen_colour_e(bs.seen), "[%s]", g_state.seen.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", seen_help(bs.seen), bs.note.c_str());
    // WP-B2 branch (c) of X3 only (Edit Tactic loads from a UI-side cache): the honest badge; off until the coordinator names branch (c)
    g_state.edit_tactic_badge = kEditTacticAfterReload ? kEditTacticAfterReloadBadge : "";
    if (kEditTacticAfterReload) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1), "[%s]", kEditTacticAfterReloadBadge);
    }
    g_state.safety = safety_text();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
    ImGui::TextWrapped("%s", g_state.safety.c_str());
    ImGui::PopStyleColor();
    // the opt-in
    bool on = g.sw.opt_in;
    if (checkbox_right("Channel E on (turbo_output\\call_team_sheet_on.txt)##ltopt", &on)) set_opt_in(app, on);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off by default. On creates turbo_output\\call_team_sheet_on.txt; call_team_sheet_off.txt or tactics_off.txt switch every write off.");

    // the plan and the gates
    Plan plan;
    std::string pw;
    const auto allow = app.team_sheet ? app.team_sheet->role_allow(app.bridge.state().load_gen) : nullptr;
    if (g.have) {
        pw = make_plan(g.lt, request_from_editor(), plan, allow.get());
        if (pw.empty() && plan.writes.empty()) pw = "nothing changed";
    }
    g_state.plan_error = pw;
    const Gates gs = evaluate(gate_input(app, pw, false));
    g_state.gates.clear();
    for (const Gate& x : gs.list) g_state.gates.push_back(x.name + ": " + (x.pass() ? "ok" : x.why));
    g_state.status = gs.pass() ? "ready" : gs.first();
    g_state.can_apply = gs.pass() && !g.pending && app.team_sheet != nullptr;
    const Gates ug = evaluate(gate_input(app, "", true));
    g_state.can_undo = ug.pass() && !g.pending && g.have_undo && (g.undo.state == "written" || g.undo.state == "failed");
    ImGui::PushStyleColor(ImGuiCol_Text, gs.pass() ? ImVec4(0.55f, 0.85f, 0.55f, 1) : ImVec4(1, 0.7f, 0.3f, 1));
    ImGui::TextWrapped("%s", gs.pass() ? "Ready to write." : ("Cannot write yet: " + plain_gate(g_state.status)).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        std::string tip;
        for (const auto& l : g_state.gates) tip += l + "\n";
        ImGui::SetTooltip("%s", tip.c_str());
    }
    // the actions, one per row (the column can be narrow)
    g_state.plan = g.have ? plan_lines(plan, g.lt) : std::vector<std::string>();
    ImGui::BeginDisabled(!g_state.can_apply);
    if (ImGui::Button("Apply to the live tactic##ltapply")) {
        g.confirm_lines = g_state.plan;
        g.confirm_request = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", g_state.can_apply ? "Writes the new values on the game thread, reads them back, puts everything back on a mismatch."
                                                  : g_state.status.c_str());
    ImGui::BeginDisabled(!g_state.can_undo);
    if (ImGui::Button("Undo Turbo's write##ltundo")) do_undo(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Puts back the values from before Turbo's first write (turbo_output\\channel_e_undo.json): only fields that still hold Turbo's "
                          "value, only in the career load of the write; the cm_mentalities row in the same job (unless channel_e_db_off.txt / "
                          "tactics_db_off.txt exist). Undo still works while call_team_sheet_off.txt / tactics_off.txt exist: it is the intended way back.");
    draw_confirm(app);

    // the editor
    g_state.slots.clear();
    g_state.approach_choices.clear();
    g_state.build_up_choices.clear();
    g_state.line_hint.clear();
    if (g.have) {
        const LiveTactic& lt = g.lt;
        ImGui::SeparatorText("New values (Edit Tactic's words)");
        // Defensive Approach: the writable values as buttons; picking one puts in its default Line Height (still editable below, within the
        // approach's band: Turbo writes them band-consistent)
        form_label("Defensive Approach",
                   "record +0xB4, written together with Line Height and Build-Up Style and always in the Line Height's band (as the game's own "
                   "records are). Deep and Balanced were seen in Edit Tactic; High follows the game's band order (other clubs' records hold it). "
                   "Aggressive is not offered: its stored value is not decoded yet. Picking one puts in its default Line Height.");
        ButtonFlow ap_row;
        for (const EnumLabel& e : defensive_approaches()) {
            if (!e.writable) continue;
            g_state.approach_choices.push_back(e.value);
            const std::string id = std::string(e.label) + "##ltap" + std::to_string(e.value);
            ap_row.next(id);
            const bool sel = e.value == g.approach;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(id.c_str()) && !sel) {
                g.approach = e.value;
                if (e.default_line > 0) g.line_height = e.default_line;
            }
            if (sel) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s = %d: %s%s", e.label, e.value, e.source,
                                  e.default_line > 0 ? (std::string("\nDefault Line Height ") + std::to_string(e.default_line) +
                                                        (e.default_line_seen ? " (seen in Edit Tactic)" : " (EA's notes)"))
                                                           .c_str()
                                                     : "");
        }
        if (!defensive_approach(g.approach)) ImGui::TextDisabled("live value %d has no known label: kept unless you pick one", g.approach);
        else if (!approach_writable(g.approach))
            ImGui::TextDisabled("live value %s is not written by Turbo (its stored value is not decoded): kept unless you pick another", approach_text(g.approach).c_str());
        // Line Height: a slider (up to the top of the highest writable band) and a box for the exact number; editing it moves the Defensive
        // Approach into the line's band
        form_label("Line Height", "record +0xA4 (cm_mentalities.defensivedepth), 1..100: Edit Tactic's Line Height slider. Turbo writes up to the top "
                                  "of the highest band whose Defensive Approach it writes; the approach follows the line's band.");
        bool line_edited = false;
        {
            // the slider and the box for the exact number side by side, or the box under the slider when the column is narrow
            const float box = S(60.0f), gap = ImGui::GetStyle().ItemSpacing.x, avail = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
            const bool one_line = avail >= S(60.0f) + gap + box;
            ImGui::SetNextItemWidth(one_line ? std::min(avail - box - gap, S(220.0f)) : std::max(avail, S(60.0f)));
            line_edited = ImGui::SliderInt("##ltlh", &g.line_height, kLineMin, std::max(kLineMin, max_writable_line())) || line_edited;
            if (one_line) ImGui::SameLine();
            else ImGui::SetCursorPosX(x0);
            ImGui::SetNextItemWidth(box);
            line_edited = ImGui::InputInt("##ltlhbox", &g.line_height, 0, 0) || line_edited;
        }
        g.line_height = std::clamp(g.line_height, kLineMin, kLineMax);
        if (line_edited) g.approach = line_band(g.line_height);  // the approach follows the line (band-consistent)
        {
            const EnumLabel* band = defensive_approach(line_band(g.line_height));
            if (line_band(g.line_height) != g.approach) {
                const EnumLabel* ap = defensive_approach(g.approach);
                g_state.line_hint = "Line Height " + std::to_string(g.line_height) + " lies in " + (band ? band->label : "another approach") + "'s band (" +
                                    line_band_text(g.line_height) + "), not " + (ap ? ap->label : approach_text(g.approach).c_str()) +
                                    "'s: Turbo writes the approach band-consistent (pick " + (band ? band->label : "the band's approach") +
                                    ", or a line height in the approach's band)";
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.3f, 1));
                ImGui::TextWrapped("%s", g_state.line_hint.c_str());
                ImGui::PopStyleColor();
            } else {
                g_state.line_hint = line_band_text(g.line_height) + ": " + (band ? band->label : "?") + "'s band";
                ImGui::TextDisabled("%s", g_state.line_hint.c_str());
            }
        }
        // Build-Up Style: the known values as buttons
        form_label("Build-Up Style", "record +0xB0 (cm_mentalities.buildupplay). Short Passing is not offered: its stored value (2 or 3) is not decoded yet");
        ButtonFlow bu_row;
        for (const EnumLabel& e : build_up_styles()) {
            if (!e.writable) continue;
            g_state.build_up_choices.push_back(e.value);
            const std::string id = std::string(e.label) + "##ltbu" + std::to_string(e.value);
            bu_row.next(id);
            const bool sel = e.value == g.build_up;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(id.c_str())) g.build_up = e.value;
            if (sel) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s = %d: %s", e.label, e.value, e.source);
        }
        if (!build_up_style(g.build_up)) ImGui::TextDisabled("live value %d has no known label (Short Passing?): kept unless you pick one", g.build_up);
        if (ImGui::SmallButton("Reset to the live values##ltreset")) fill_editor(lt);
        if (!g_state.plan.empty()) {
            ImGui::TextUnformatted("Apply will write:");
            for (const std::string& l : g_state.plan) ImGui::BulletText("%s", l.c_str());
        } else if (!pw.empty()) {
            ImGui::TextDisabled("Apply: %s", pw.c_str());
        }
        g_state.not_written = "Not written: preset " + std::to_string(lt.preset) + " (ids not decoded), formation " + std::to_string(lt.formation) +
                              " (needs all 11 slots), the tactic code, takers, names, positions";
        g_state.edit_slot = g.slot;
        g_state.allow = app.team_sheet ? app.team_sheet->allow_status() : "";
        g_state.role_choices.clear();
        g_state.focus_choices.clear();
    }

    // what happened
    if (!g.last.empty()) ImGui::TextWrapped("Last: %s", g.last.c_str());
    if (!g.mirror_line.empty()) ImGui::TextWrapped("Database row: %s", g.mirror_line.c_str());
    const std::string ko = app.team_sheet ? app.team_sheet->kickoff_status() : "";
    if (!ko.empty()) ImGui::TextWrapped("%s", ko.c_str());
    g_state.last = g.last;
    g_state.mirror = g.mirror_line;
    g_state.kickoff = ko;

    // the read-out, as state (tests) and, below, behind "More"
    if (g.have) {
        const LiveTactic& lt = g.lt;
        g_state.readout = "\"" + lt.name + "\" code " + (lt.code.empty() ? std::string("?") : lt.code) + ": formation " + std::to_string(lt.formation) +
                          " (source " + std::to_string(lt.source_formation) + "), Defensive Approach " + approach_text(lt.approach) + ", Line Height " +
                          std::to_string(lt.line_height) + " " + line_band_text(lt.line_height) + ", Build-Up Style " + build_up_text(lt.build_up) +
                          ", preset " + std::to_string(lt.preset) + " | sheet " + std::to_string(lt.sheet_id) + ", mode " + std::to_string(lt.mode) +
                          ", dirty " + std::to_string(lt.dirty) + ", record " + std::to_string(lt.at.record_index) + " of " + std::to_string(lt.at.records);
        for (int s = 0; s < kSlots; ++s) {
            const SlotView& v = lt.slots[size_t(s)];
            g_state.slots.push_back("slot " + std::to_string(s) + " " + position_name(v.position) + " " + player_label(app, v.pid) + ": role " +
                                    std::to_string(v.role) + " = group " + std::to_string(v.group()) + " / role " + std::to_string(v.role_id) + " / focus " +
                                    std::to_string(v.focus));
        }
    } else {
        g_state.readout.clear();
    }

    // 2.5: the details behind one collapsed node (roles per slot, the live record, the service): the card's working part is above
    ImGui::SetNextItemOpen(g.details_open || g_force_details, ImGuiCond_Always);
    const bool details_shown = ImGui::TreeNode("More: roles per slot, the live record, details###ltmore");
    if (!g_force_details) g.details_open = details_shown;  // a test that keeps it open does not change what the person left
    g_state.details_open = details_shown;
    if (details_shown) {
        if (g.have) {
            const LiveTactic& lt = g.lt;
            input_int("Slot (0-10)", "##ltslot", &g.slot, 0, kSlots - 1, "the slot whose role id and focus the buttons below set");
            g_state.edit_slot = g.slot;
            const SlotView& v = lt.slots[size_t(g.slot)];
            // role id / focus: only the triples the game itself uses (the role allow-list of every other club's sheets), as buttons
            if (allow && allow->ok) {
                int& rid = g.role_id[size_t(g.slot)];
                int& foc = g.focus[size_t(g.slot)];
                std::vector<int> roles = allow->role_ids(v.group());
                if (std::find(roles.begin(), roles.end(), v.role_id) == roles.end()) roles.insert(roles.begin(), v.role_id);  // the live one stays offered
                form_label("Role id", "the role ids the game uses in this position group (raw numbers: the labels are not decoded)");
                ButtonFlow role_row;
                for (size_t i = 0; i < roles.size(); ++i) {
                    const int x = roles[i];
                    g_state.role_choices.push_back(x);
                    const std::string id = std::to_string(x) + "##ltrole" + std::to_string(x);
                    role_row.next(id);
                    const bool sel = x == rid;
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::SmallButton(id.c_str()) && !sel) {
                        rid = x;
                        const std::vector<int> fs = allow->focuses(v.group(), x);
                        if (x == v.role_id) foc = v.focus;
                        else if (!fs.empty() && std::find(fs.begin(), fs.end(), foc) == fs.end()) foc = fs.front();
                    }
                    if (sel) ImGui::PopStyleColor();
                }
                std::vector<int> focs = allow->focuses(v.group(), rid);
                if (rid == v.role_id && std::find(focs.begin(), focs.end(), v.focus) == focs.end()) focs.insert(focs.begin(), v.focus);
                form_label("Focus", "the focuses the game uses with this role (raw numbers)");
                ButtonFlow foc_row;
                for (size_t i = 0; i < focs.size(); ++i) {
                    const int x = focs[i];
                    g_state.focus_choices.push_back(x);
                    const std::string id = std::to_string(x) + "##ltfoc" + std::to_string(x);
                    foc_row.next(id);
                    const bool sel = x == foc;
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::SmallButton(id.c_str())) foc = x;
                    if (sel) ImGui::PopStyleColor();
                }
                if (focs.empty()) ImGui::TextDisabled("Focus: none seen with role %d", rid);
            } else {
                ImGui::TextDisabled("Role / focus: not editable (%s)", g_state.allow.c_str());
            }
            ImGui::TextWrapped("Slot %d: %s, %s, position group %d (kept)", g.slot, position_name(v.position), player_label(app, v.pid).c_str(), v.group());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", g_state.not_written.c_str());
            ImGui::PopStyleColor();
        }
        if (app.team_sheet) ImGui::TextDisabled("%s", app.team_sheet->allow_status().c_str());
        if (g.have_undo)
            ImGui::TextWrapped("Undo file: %s, %s%s", g.undo.state.c_str(), g.undo.when.c_str(), g.undo.note.empty() ? "" : (" (" + g.undo.note + ")").c_str());
        else if (!g.undo_err.empty())
            ImGui::TextWrapped("turbo_output\\channel_e_undo.json is not used: %s", g.undo_err.c_str());
        if (g.have) {
            ImGui::TextWrapped("Live record (the next match is set up from it; its roles were seen in a match, its Line Height / Build-Up Style / "
                               "Defensive Approach are what the kick-off line checks): %s",
                               g_state.readout.c_str());
            if (ImGui::TreeNode("Slots (11)##ltslots")) {
                for (const std::string& l : g_state.slots) ImGui::TextWrapped("%s", l.c_str());
                ImGui::TreePop();
            }
        } else {
            ImGui::TextWrapped("Live record not read: %s", g.read_err.c_str());
        }
        ImGui::TextWrapped("Service: %s", g_state.locate.c_str());
        if (ImGui::SmallButton("Locate again##ltreloc") && app.team_sheet) {
            app.team_sheet->relocate();
            g.next_read = 0.0;
        }
        ImGui::TextWrapped(
            "Apply in the career hub with Team Management (Edit Tactic) closed, before the pre-match screen: Turbo writes your club's live tactic "
            "record (the record the next match is set up from: its roles were seen in a match; whether its Line Height, Build-Up Style and Defensive "
            "Approach reach the match is what the kick-off line checks) and, when it matches, its cm_mentalities row. Leave Team Management with No "
            "when the game asks to save your team sheet. Turbo never saves. Turbo re-reads the record 5 s after a write and watches it after Team "
            "Management was open, and says so when the game replaced it; after kick-off it reads the match's own copy of the tactic (read-only) "
            "and logs whether it holds these values.");
        ImGui::TreePop();
    }
    ImGui::PopID();
}

}  // namespace

const LiveTacticState& live_tactic_state() { return g_state; }

void live_tactic_set_details_open(bool open) { g_force_details = open; }

void live_tactic_test_wall_clock(std::function<double()> wall) { g_wall = std::move(wall); }

void live_tactic_reset() {
    g = Lt();
    g_state = LiveTacticState();
}

// ---- 2.12: what the tactic presets panel (ui_tactic_presets.cpp) uses: the card's own read, gates and guarded job
bool live_tactic_read(App& app, tsheet::LiveTactic& out, std::string& why) {
    refresh_switches(app, false);
    load_undo_once(app);
    if (!read_now(app)) {
        why = g.read_err;
        return false;
    }
    out = g.lt;
    why.clear();
    return true;
}

std::string live_tactic_gate(App& app, const tsheet::Request& req) {
    refresh_switches(app, false);
    if (!g.have) return g.read_err.empty() ? std::string("not read yet") : g.read_err;
    Plan plan;
    const auto allow = app.team_sheet ? app.team_sheet->role_allow(app.bridge.state().load_gen) : nullptr;
    std::string pw = make_plan(g.lt, req, plan, allow.get());
    if (pw.empty() && plan.writes.empty()) pw = "nothing changed";
    const Gates gs = evaluate(gate_input(app, pw, false));
    return gs.pass() ? std::string() : gs.first();
}

bool live_tactic_submit(App& app, const tsheet::Request& req, std::string& why) { return apply_request(app, req, false, &why); }
bool live_tactic_undo(App& app, std::string& why) { return do_undo(app, &why); }
bool live_tactic_undo_ready(App& app) {
    load_undo_once(app);
    const BridgeState& st = app.bridge.state();
    return g.have_undo && (g.undo.state == "written" || g.undo.state == "failed") && same_career(g.undo, st.session, st.load_gen).empty();
}
bool live_tactic_busy() { return g.pending; }
LiveJobResult live_tactic_result() { return g_result; }
bool live_tactic_opt_in(App& app) {
    refresh_switches(app, false);
    return g.sw.opt_in;
}
void live_tactic_set_opt_in(App& app, bool on) { set_opt_in(app, on); }

void draw_live_tactic(App& app) {
    g_state = LiveTacticState();
    try {
        draw_body(app);
        if (!g.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Live tactic error: %s", g.error.c_str());
    } catch (const std::exception& e) {
        g.error = e.what();
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Live tactic error: %s", e.what());
    } catch (...) {
        g.error = "unknown error";
    }
}

void live_tactic_tick(App& app) {
    try {
        if (!app.team_sheet) return;
        refresh_switches(app, false);
        app.team_sheet->note_context(ctx_of(app));  // the jobs compare their career load and the hub with the latest view
        ApplyResult r;
        while (app.team_sheet->poll(r)) {
            if (!g.pending) {
                // the result of a job the card gave up on (below): it ran more than 10 s after Apply, so it was refused as late
                log_line(app, "channel E: result of a job given up on: " + (r.log_line.empty() ? r.message : r.log_line));
                continue;
            }
            load_undo_once(app);
            handle_result(app, r);
        }
        // a job the game thread never ran (the dispatcher's queue dropped it): the service stops waiting after 10 s, the card too. Nothing
        // was written by it (a late run writes nothing): an Apply's undo file goes back to the earlier record, or "aborted"
        if (g.pending && g.pending_since >= 0 && app.now - g.pending_since > kJobLostUs / 1e6 + 2.0 && !app.team_sheet->busy()) {
            g.pending = false;
            g.last = std::string(g.pending_undo ? "undo" : "Apply") + ": the game thread did not run the job (nothing written); try again";
            if (!g.pending_undo) {
                if (g.prev_have) {
                    save_record(app, g.prev);
                } else {
                    UndoRecord u = g.undo;
                    u.state = "aborted";
                    u.note = g.last;
                    save_record(app, u);
                }
            }
            log_line(app, "channel E: " + g.last);
        }
        // after Team Management was open (Lua's team_mgmt flag back to false) following Turbo's write in this career load: watch the live
        // record. Every good read in the hub (once a second of WALL time here, and the card's own reads) decides the prompt (check_replaced);
        // no deadline, since the game's Yes and its copy of Edit Tactic's screen may land at any time. Never acts by itself.
        {
            const BridgeState& st = app.bridge.state();
            drop_stale_prompt(app);  // the prompt belongs to the career load it was made in
            const bool tm = st.loaded && st.team_mgmt;
            if (g.tm_was && !tm) {
                load_undo_once(app);
                if (g.have_undo && g.undo.state == "written" && same_career(g.undo, st.session, st.load_gen).empty()) {
                    g.tm_watch = true;
                    g.next_watch_read = 0.0;
                    g.prompt_session = st.session;
                    g.prompt_gen = st.load_gen;
                    log_line(app, "channel E: Team Management closed: watching your club's live record in the hub for the game's copy of Edit Tactic's "
                                  "screen (a Yes on leaving); the card asks before anything is written");
                }
            }
            g.tm_was = tm;
            if (g.tm_watch && (st.session != g.prompt_session || st.load_gen != g.prompt_gen)) g.tm_watch = false;  // reloaded: the watch is over
            if (g.tm_watch && !tm && st.loaded && st.match_phase == "hub" && wall_now() >= g.next_watch_read) {
                g.next_watch_read = wall_now() + 1.0;
                read_now(app);  // check_replaced runs on the good read
            }
        }
        // report 4.5: re-read 5 s after a write, and when the Turbo window is shown again (the user may have used Edit Tactic meanwhile)
        const bool shown_again = app.visible && !g.was_visible;
        g.was_visible = app.visible;
        if ((g.recheck_at > 0 && app.now >= g.recheck_at) || (shown_again && g.have_undo && g.undo.state == "written")) {
            const bool timed = g.recheck_at > 0 && app.now >= g.recheck_at;
            g.recheck_at = -1.0;
            const std::vector<std::string> before = g.overwritten;
            if (read_now(app) && g.have_undo && g.undo.state == "written") {
                if (!g.overwritten.empty() && (timed || g.overwritten != before)) {
                    std::string s;
                    for (const auto& o : g.overwritten) s += (s.empty() ? "" : "; ") + o;
                    log_line(app, "channel E: re-read " + std::string(timed ? "5 s after the write" : "when the window was shown") + ": " + s);
                } else if (g.overwritten.empty()) {
                    log_line(app, "channel E: re-read " + std::string(timed ? "5 s after the write" : "when the window was shown") +
                                      ": the written values are still in place");
                }
            }
        }
        // the kick-off check (core/team_sheet.h): the game_tick listener runs it; this tick only stands in when the game thread is not hooked
        app.team_sheet->kickoff_tick();
    } catch (const std::exception& e) {
        g.error = e.what();
    } catch (...) {
        g.error = "unknown error";
    }
}

}  // namespace turbo
