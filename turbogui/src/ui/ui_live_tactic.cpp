// FC 27 LE Turbo GUI - your club's live tactic, tactics channel E (see ui_live_tactic.h, core/team_sheet.h).
//
// Frame-loop rules as in ui_tactics.cpp: never throws (an inline error instead), file I/O only on a click, at the first look (the undo
// file) and every 2 s (the switch files), the live record is read at most once a second while the card is drawn.
#include "ui_live_tactic.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "app.h"
#include "core/tactics_db.h"
#include "core/tactics_status.h"
#include "core/team_sheet.h"
#include "imgui.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;
using namespace tsheet;

struct Lt {
    // the read-out
    bool have = false;
    LiveTactic lt;
    std::string read_err;
    double next_read = -1.0;
    // the editor (filled from the read-out of a record; kept while the same record is shown)
    std::string editor_key;
    int build_up = 0, depth = 1, slot = 0;
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
    UndoRecord prev;
    bool prev_have = false, merged = false;
    std::vector<std::string> undo_skipped;
    // lines
    std::string last, mirror_line, error;
    std::vector<std::string> overwritten;
    // the read-back after a write, and when the window is shown again
    double recheck_at = -1.0;
    bool was_visible = false;
    // the kick-off check
    std::string last_phase;
    double kickoff_at = -1.0;
    // the confirmation
    bool confirm_request = false;
    std::vector<std::string> confirm_lines;
};
Lt g;
LiveTacticState g_state;

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
    g.build_up = lt.build_up;
    g.depth = lt.depth;
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
        if (g.have_undo && g.undo.state == "written")
            for (const std::string& r : reverted(lt, g.undo)) g.overwritten.push_back("Edit Tactic (or the game) overwrote the change: " + r);
    }
    return ok;
}

Request request_from_editor() {
    Request r;
    r.build_up = g.build_up;
    r.depth = g.depth;
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
    switch (w.kind) {
        case FieldKind::BuildUp: return "build-up (buildupplay) " + std::to_string(w.old_value) + " -> " + std::to_string(w.new_value);
        case FieldKind::Depth:
            return "defensive depth " + std::to_string(w.old_value) + " -> " + std::to_string(w.new_value) + " (" + depth_band_text(w.new_value) + ")";
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
    if (!out.empty()) out.push_back("then the sheet's dirty flag = 1 (as the game's UpdateSheet does)");
    return out;
}

// ---------------------------------------------------------------------------------------------------------------- the cm_mentalities mirror
const char* mirror_field(const FieldWrite& w, std::string& buf) {
    switch (w.kind) {
        case FieldKind::BuildUp: return "buildupplay";
        case FieldKind::Depth: return "defensivedepth";
        case FieldKind::Role: buf = "pos" + std::to_string(w.slot) + "role"; return buf.c_str();
        case FieldKind::RoleId:
        case FieldKind::Focus: return nullptr;  // no column: the word carries them
    }
    return nullptr;
}

// The cm_mentalities row of the live record: teamid == your club, activetactic == 1, sourceformationid == the record's (teamsheetid == the
// sheet's when the save has that field). Exactly one row, else none (why says it)
uint64_t mirror_row(App& app, const UndoRecord& u, const Table*& t, std::string& why) {
    t = app.db.table("cm_mentalities");
    if (!t) {
        why = "this save has no cm_mentalities table";
        return 0;
    }
    if (!t->field("artificialkey") || !t->field("activetactic") || !t->field("sourceformationid")) {
        why = "cm_mentalities lacks artificialkey / activetactic / sourceformationid: the row cannot be told apart";
        return 0;
    }
    const TeamRows rows = team_rows(app.db, *t, u.team);
    if (rows.key_missing) {
        why = "cm_mentalities has no teamid field";
        return 0;
    }
    std::vector<uint64_t> hit;
    for (uint64_t rec : rows.recs) {
        if (app.db.get_int(*t, rec, "activetactic", -1) != 1) continue;
        if (app.db.get_int(*t, rec, "sourceformationid", -2) != u.source_formation) continue;
        if (t->field("teamsheetid") && app.db.get_int(*t, rec, "teamsheetid", -2) != u.sheet_id) continue;
        hit.push_back(rec);
    }
    if (hit.size() != 1) {
        why = std::to_string(hit.size()) + " cm_mentalities rows of club " + std::to_string(u.team) + " are the active tactic with sourceformationid " +
              std::to_string(u.source_formation) + " (exactly 1 needed)";
        return 0;
    }
    return hit[0];
}

// After a live write that was read back: the same values into the career database's row of that tactic, read back. The values before
// Turbo's first write are kept in the undo record. Off with tactics_off.txt, tactics_db_off.txt, call_team_sheet_off.txt or
// channel_e_db_mirror_off.txt.
void do_mirror(App& app, UndoRecord& u, const std::vector<FieldWrite>& writes) {
    refresh_switches(app, true);
    auto skip = [&](const std::string& why) {
        u.mirror_state = u.mirror.empty() ? "skipped" : u.mirror_state;
        u.mirror_note = why;
        g.mirror_line = "cm_mentalities not mirrored: " + why + ". The live tactic is written; your next in-game save writes it into the career database.";
        log_line(app, "channel E: " + g.mirror_line);
    };
    if (g.sw.master || g.sw.off || g.sw.db_off || g.sw.mirror_off) {
        skip(std::string("kill switch turbo_output\\") +
             (g.sw.master ? master_switch_file() : g.sw.off ? kill_switch_file() : g.sw.db_off ? db_off_file() : mirror_off_file()) + " is present");
        return;
    }
    if (!app.connected()) {
        skip("the career database is not connected");
        return;
    }
    const Table* t = nullptr;
    std::string why;
    const uint64_t rec = mirror_row(app, u, t, why);
    if (!rec) {
        skip(why);
        return;
    }
    const int64_t key = app.db.get_int(*t, rec, "artificialkey", -1);
    if (u.mirror_row_key >= 0 && key != u.mirror_row_key) {
        skip("the row changed since the last mirror (artificialkey " + std::to_string(key) + ", was " + std::to_string(u.mirror_row_key) + ")");
        return;
    }
    std::vector<std::string> done, missing;
    struct Undo {
        const Field* f;
        int64_t before;
    };
    std::vector<Undo> written;
    std::string bad;
    for (const FieldWrite& w : writes) {
        std::string buf;
        const char* name = mirror_field(w, buf);
        if (!name) continue;
        const Field* f = t->field(name);
        if (!f) {
            missing.push_back(name);
            continue;
        }
        Value before;
        if (!app.db.get(*t, rec, *f, before) || before.type != FieldType::Int) {
            bad = std::string(name) + " is not readable";
            break;
        }
        // the value before Turbo's FIRST mirror of this field is kept
        auto it = std::find_if(u.mirror.begin(), u.mirror.end(), [&](const MirrorField& m) { return m.field == name; });
        if (before.i != w.new_value) {
            if (!app.edit(*t, rec, *f, Value::of_int(w.new_value))) {
                bad = std::string(name) + " = " + std::to_string(w.new_value) + " was refused by the database";
                break;
            }
            if (app.db.get_int(*t, rec, name, INT64_MIN) != w.new_value) {
                written.push_back({f, before.i});
                bad = std::string(name) + " reads back " + std::to_string(app.db.get_int(*t, rec, name, INT64_MIN));
                break;
            }
            written.push_back({f, before.i});
        }
        if (it == u.mirror.end()) u.mirror.push_back({name, before.i, w.new_value});
        else it->new_value = w.new_value;
        done.push_back(std::string(name) + " " + std::to_string(before.i) + " -> " + std::to_string(w.new_value));
    }
    if (!bad.empty()) {
        for (const Undo& x : written) app.edit(*t, rec, *x.f, Value::of_int(x.before));  // put the row back as it was
        u.mirror_state = "failed";
        u.mirror_note = bad;
        g.mirror_line = "cm_mentalities mirror failed (" + bad + "): the row was put back; the live tactic stays written";
        log_line(app, "channel E: " + g.mirror_line);
        return;
    }
    u.mirror_table = "cm_mentalities";
    u.mirror_row_key = key;
    u.mirror_state = "written";
    u.mirror_note = missing.empty() ? "" : "this save has no field " + [&] {
        std::string s;
        for (const auto& m : missing) s += (s.empty() ? "" : ", ") + m;
        return s;
    }();
    std::string list;
    for (const auto& d : done) list += (list.empty() ? "" : ", ") + d;
    g.mirror_line = "cm_mentalities (artificialkey " + std::to_string(key) + ") mirrored and read back: " + (list.empty() ? "nothing to change" : list) +
                    (u.mirror_note.empty() ? "" : " (" + u.mirror_note + ")");
    log_line(app, "channel E: " + g.mirror_line);
}

// Undo of the mirror: fields that still hold Turbo's value get the value from before Turbo's first mirror
void undo_mirror(App& app, UndoRecord& u) {
    if (u.mirror.empty() || u.mirror_state != "written") return;
    const Table* t = app.connected() ? app.db.table("cm_mentalities") : nullptr;
    const uint64_t rec = (t && t->field("artificialkey")) ? app.db.find(*t, "artificialkey", u.mirror_row_key) : 0;
    if (!rec) {
        g.mirror_line = "cm_mentalities row " + std::to_string(u.mirror_row_key) + " not found: its mirror is not put back";
        log_line(app, "channel E: " + g.mirror_line);
        return;
    }
    std::vector<std::string> back, left;
    for (const MirrorField& m : u.mirror) {
        const Field* f = t->field(m.field);
        if (!f) continue;
        const int64_t cur = app.db.get_int(*t, rec, m.field.c_str(), INT64_MIN);
        if (cur != m.new_value) {
            left.push_back(m.field + " is " + std::to_string(cur));
            continue;
        }
        if (app.edit(*t, rec, *f, Value::of_int(m.old_value)) && app.db.get_int(*t, rec, m.field.c_str(), INT64_MIN) == m.old_value)
            back.push_back(m.field + " " + std::to_string(m.new_value) + " -> " + std::to_string(m.old_value));
        else
            left.push_back(m.field + " refused");
    }
    u.mirror_state = "restored";
    std::string a, b;
    for (const auto& x : back) a += (a.empty() ? "" : ", ") + x;
    for (const auto& x : left) b += (b.empty() ? "" : ", ") + x;
    g.mirror_line = "cm_mentalities put back: " + (a.empty() ? "nothing" : a) + (b.empty() ? "" : "; left as they are: " + b);
    log_line(app, "channel E: " + g.mirror_line);
}

// ---------------------------------------------------------------------------------------------------------------- Apply / Undo
void do_apply(App& app) {
    auto svc = app.team_sheet;
    if (!svc) return;
    refresh_switches(app, true);
    load_undo_once(app);
    if (!read_now(app)) {
        app.notify("Live tactic not written: " + g.read_err, true);
        return;
    }
    Plan plan;
    std::string pw = make_plan(g.lt, request_from_editor(), plan);
    if (pw.empty() && plan.writes.empty()) pw = "nothing changed";
    const Gates gs = evaluate(gate_input(app, pw, false));
    if (!gs.pass()) {
        app.notify("Live tactic not written: " + gs.first(), true);
        return;
    }
    const BridgeState& st = app.bridge.state();
    UndoRecord rec = UndoRecord::from_plan(plan);
    rec.when = stamp();
    rec.session = st.session;
    rec.load_gen = st.load_gen;
    g.prev = g.undo;
    g.prev_have = g.have_undo;
    g.merged = false;
    if (g.have_undo && g.undo.state == "written") {
        const UndoRecord m = merge_undo(g.undo, rec);
        g.merged = m.writes.size() != rec.writes.size() || m.dirty_before != rec.dirty_before || same_record(g.undo, g.lt);
        rec = m;
    }
    rec.when = stamp();
    rec.session = st.session;
    rec.load_gen = st.load_gen;
    rec.state = "pending";
    rec.note.clear();
    std::string err;
    // the undo file first: it holds the values before the write before anything is written
    if (!save_record(app, rec, &err)) {
        app.notify("Live tactic not written: turbo_output\\" + std::string(undo_file()) + " could not be written (" + err + ")", true);
        return;
    }
    std::string why;
    if (!svc->submit(ctx_of(app), plan, why)) {
        if (g.prev_have) save_record(app, g.prev);
        else {
            rec.state = "aborted";
            rec.note = why;
            save_record(app, rec);
        }
        app.notify("Live tactic not written: " + why, true);
        g.last = "not queued: " + why;
        return;
    }
    g.pending = true;
    g.pending_undo = false;
    g.last = "queued for the game thread ...";
}

void do_undo(App& app) {
    auto svc = app.team_sheet;
    if (!svc) return;
    refresh_switches(app, true);
    load_undo_once(app);
    if (!g.have_undo || g.undo.state != "written") {
        app.notify("Nothing to put back: turbo_output\\" + std::string(undo_file()) + " holds no write in place", true);
        return;
    }
    if (!read_now(app)) {
        app.notify("Nothing put back: " + g.read_err, true);
        return;
    }
    Plan plan;
    std::vector<std::string> skipped;
    const std::string w = make_undo_plan(g.lt, g.undo, plan, skipped);
    g.undo_skipped = skipped;
    if (!w.empty()) {
        app.notify("Nothing put back: " + w, true);
        g.last = "undo: " + w;
        return;
    }
    const Gates gs = evaluate(gate_input(app, "", true));
    if (!gs.pass()) {
        app.notify("Nothing put back: " + gs.first(), true);
        return;
    }
    std::string why;
    if (!svc->submit(ctx_of(app), plan, why)) {
        app.notify("Nothing put back: " + why, true);
        return;
    }
    g.pending = true;
    g.pending_undo = true;
    g.last = "undo queued for the game thread ...";
}

void handle_result(App& app, const ApplyResult& r) {
    g.pending = false;
    g.last = r.log_line.empty() ? r.message : r.log_line;
    if (!r.message.empty() && r.log_line.find(r.message) == std::string::npos) g.last += ": " + r.message;
    if (r.undo) {
        if (r.ok && r.written) {
            UndoRecord u = g.undo;
            u.state = "restored";
            u.note = g.undo_skipped.empty() ? "" : "partly: " + [&] {
                std::string s;
                for (const auto& x : g.undo_skipped) s += (s.empty() ? "" : "; ") + x;
                return s;
            }();
            undo_mirror(app, u);
            save_record(app, u);
            g.overwritten.clear();
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
        if (!g.merged) u.dirty_before = r.dirty_before;  // a merged record keeps the flag from before Turbo's first write
        do_mirror(app, u, r.writes);
        save_record(app, u);
        g.recheck_at = app.now + 5.0;  // report 4.5: re-read 5 s later (an Edit Tactic overwrite shows then)
        fill_editor(r.after);
        app.notify("Written into your club's live tactic and read back. It reaches the next match set up from the hub; your next in-game save keeps it.");
    } else if (r.ok) {
        if (g.prev_have) save_record(app, g.prev);
        app.notify("Live tactic: " + r.message);
    } else {
        // nothing of this Apply is in the game (refused before writing, or every field put back): the earlier record stays
        if (g.prev_have && g.prev.state == "written") {
            UndoRecord u = g.prev;
            u.note = "a later Apply was not written (" + r.stage + "): " + r.message;
            save_record(app, u);
        } else {
            UndoRecord u = g.undo;
            u.state = r.undone ? "undone" : (r.restore_failed ? "failed" : "aborted");
            u.note = r.stage + ": " + r.message;
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
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1), "Untested: nobody has seen channel E in Edit Tactic or a played match yet.");
        ImGui::TextUnformatted("Team Management (Edit Tactic) must be closed. The values before the write go to turbo_output\\channel_e_undo.json.");
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
        g_state.summary = "Your club's live tactic (read now): \"" + (g.lt.name.empty() ? std::string("?") : g.lt.name) + "\", depth " +
                          std::to_string(g.lt.depth) + ", build-up " + std::to_string(g.lt.build_up);
    } else {
        g_state.summary = "Your club's live tactic: not read (" + g.read_err + ")";
    }
    g_state.summary_shown = true;
    ImGui::TextWrapped("%s", g_state.summary.c_str());
    for (const std::string& o : g.overwritten) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
        ImGui::TextWrapped("%s", o.c_str());
        ImGui::PopStyleColor();
    }
    g_state.overwritten = g.overwritten;

    g_state.card_open = ImGui::CollapsingHeader("Edit the live tactic (channel E)##ltcard");
    if (!g_state.card_open) return;
    ImGui::PushID("ltcard");
    // badges (Phase 0: how it is written / what was seen)
    const TacticStatus bs = live_sheet_status("live.depth", "Live tactic");
    g_state.written = written_badge(bs.written);
    g_state.seen = seen_badge(bs);
    g_state.tag = status_tag(bs);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.95f, 1), "[%s]", g_state.written.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", written_help(bs.written));
    ImGui::SameLine();
    ImGui::TextColored(seen_colour_e(bs.seen), "[%s]", g_state.seen.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", seen_help(bs.seen), bs.note.c_str());
    // the opt-in
    bool on = g.sw.opt_in;
    if (checkbox_right("Channel E on (turbo_output\\call_team_sheet_on.txt)##ltopt", &on)) {
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
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off by default. On creates turbo_output\\call_team_sheet_on.txt; call_team_sheet_off.txt or tactics_off.txt switch every write off.");

    // the plan and the gates
    Plan plan;
    std::string pw;
    if (g.have) {
        pw = make_plan(g.lt, request_from_editor(), plan);
        if (pw.empty() && plan.writes.empty()) pw = "nothing changed";
    }
    g_state.plan_error = pw;
    const Gates gs = evaluate(gate_input(app, pw, false));
    g_state.gates.clear();
    for (const Gate& x : gs.list) g_state.gates.push_back(x.name + ": " + (x.pass() ? "ok" : x.why));
    g_state.status = gs.pass() ? "ready" : gs.first();
    g_state.can_apply = gs.pass() && !g.pending && app.team_sheet != nullptr;
    const Gates ug = evaluate(gate_input(app, "", true));
    g_state.can_undo = ug.pass() && !g.pending && g.have_undo && g.undo.state == "written";
    ImGui::PushStyleColor(ImGuiCol_Text, gs.pass() ? ImVec4(0.55f, 0.85f, 0.55f, 1) : ImVec4(1, 0.7f, 0.3f, 1));
    ImGui::TextWrapped("%s", gs.pass() ? "Ready to write." : ("Not ready: " + g_state.status).c_str());
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
                          "value; the cm_mentalities mirror too.");
    draw_confirm(app);

    // the editor
    g_state.slots.clear();
    if (g.have) {
        const LiveTactic& lt = g.lt;
        ImGui::SeparatorText("New values (raw numbers)");
        input_int("Build-up (0-3)", "##ltbu", &g.build_up, kBuildUpMin, kBuildUpMax,
                  "buildupplay of the active record; what each number means in Edit Tactic is not decoded yet");
        input_int("Defensive depth (1-100)", "##ltdd", &g.depth, kDepthMin, kDepthMax, "defensivedepth of the active record");
        input_int("Slot (0-10)", "##ltslot", &g.slot, 0, kSlots - 1, "the slot whose role id and focus the two boxes below edit");
        g_state.edit_slot = g.slot;
        input_int("Role id (0-63)", "##ltrole", &g.role_id[size_t(g.slot)], 0, kRoleIdMax, "the role inside the slot's position group (raw number)");
        input_int("Focus (0-7)", "##ltfocus", &g.focus[size_t(g.slot)], 0, kFocusMax, "the role's focus (raw number)");
        const SlotView& v = lt.slots[size_t(g.slot)];
        ImGui::TextDisabled("%s", depth_band_text(g.depth).c_str());
        ImGui::TextWrapped("Slot %d: %s, %s, position group %d (kept)", g.slot, position_name(v.position), player_label(app, v.pid).c_str(), v.group());
        if (ImGui::SmallButton("Reset to the live values##ltreset")) fill_editor(lt);
        if (!g_state.plan.empty()) {
            ImGui::TextUnformatted("Apply will write:");
            for (const std::string& l : g_state.plan) ImGui::BulletText("%s", l.c_str());
        } else if (!pw.empty()) {
            ImGui::TextDisabled("Apply: %s", pw.c_str());
        }
        g_state.not_written = "Not written in 2.2: preset " + std::to_string(lt.preset) + " (ids not decoded), formation " + std::to_string(lt.formation) +
                              " (needs all 11 slots: 2.3), takers, names, positions";
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", g_state.not_written.c_str());
        ImGui::PopStyleColor();
    }

    // what happened
    if (!g.last.empty()) ImGui::TextWrapped("Last: %s", g.last.c_str());
    if (!g.mirror_line.empty()) ImGui::TextWrapped("Mirror: %s", g.mirror_line.c_str());
    const std::string ko = app.team_sheet ? app.team_sheet->kickoff_status() : "";
    if (!ko.empty()) ImGui::TextWrapped("%s", ko.c_str());
    if (g.have_undo)
        ImGui::TextWrapped("Undo file: %s, %s%s", g.undo.state.c_str(), g.undo.when.c_str(), g.undo.note.empty() ? "" : (" (" + g.undo.note + ")").c_str());
    else if (!g.undo_err.empty())
        ImGui::TextWrapped("turbo_output\\channel_e_undo.json is not used: %s", g.undo_err.c_str());
    g_state.last = g.last;
    g_state.mirror = g.mirror_line;
    g_state.kickoff = ko;

    // the read-out
    if (g.have) {
        const LiveTactic& lt = g.lt;
        g_state.readout = "\"" + lt.name + "\": formation " + std::to_string(lt.formation) + " (source " + std::to_string(lt.source_formation) + "), depth " +
                          std::to_string(lt.depth) + " " + depth_band_text(lt.depth) + ", build-up " + std::to_string(lt.build_up) + ", preset " +
                          std::to_string(lt.preset) + " | sheet " + std::to_string(lt.sheet_id) + ", mode " + std::to_string(lt.mode) + ", dirty " +
                          std::to_string(lt.dirty) + ", record " + std::to_string(lt.at.record_index) + " of " + std::to_string(lt.at.records);
        ImGui::TextWrapped("Live record: %s", g_state.readout.c_str());
        for (int s = 0; s < kSlots; ++s) {
            const SlotView& v = lt.slots[size_t(s)];
            g_state.slots.push_back("slot " + std::to_string(s) + " " + position_name(v.position) + " " + player_label(app, v.pid) + ": role " +
                                    std::to_string(v.role) + " = group " + std::to_string(v.group()) + " / role " + std::to_string(v.role_id) + " / focus " +
                                    std::to_string(v.focus));
        }
        if (ImGui::TreeNode("Slots (11)##ltslots")) {
            for (const std::string& l : g_state.slots) ImGui::TextWrapped("%s", l.c_str());
            ImGui::TreePop();
        }
    } else {
        g_state.readout.clear();
        ImGui::TextWrapped("Live record not read: %s", g.read_err.c_str());
    }
    ImGui::TextWrapped("Service: %s", g_state.locate.c_str());
    if (ImGui::SmallButton("Locate again##ltreloc") && app.team_sheet) {
        app.team_sheet->relocate();
        g.next_read = 0.0;
    }
    ImGui::TextWrapped(
        "Apply in the career hub with Team Management (Edit Tactic) closed, before the pre-match screen: the next match set up afterwards copies the "
        "record, and your next in-game save keeps it. Turbo re-reads the record 5 s after a write and says so when Edit Tactic overwrote it.");
    ImGui::PopID();
}

}  // namespace

const LiveTacticState& live_tactic_state() { return g_state; }

void live_tactic_reset() {
    g = Lt();
    g_state = LiveTacticState();
}

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
        ApplyResult r;
        while (app.team_sheet->poll(r)) {
            load_undo_once(app);
            handle_result(app, r);
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
                } else if (timed) {
                    log_line(app, "channel E: re-read 5 s after the write: the written values are still in place");
                }
            }
        }
        // report 4.5: once a match is set up after a write, look for the written role words in the match copy (read-only)
        const std::string& phase = app.bridge.state().match_phase;
        if (phase != g.last_phase) {
            if (phase == "match" && g.have_undo && g.undo.state == "written") g.kickoff_at = app.now + 20.0;
            g.last_phase = phase;
        }
        if (g.kickoff_at > 0 && app.now >= g.kickoff_at) {
            g.kickoff_at = -1.0;
            if (read_now(app)) {
                std::array<int32_t, kSlots> roles{}, pids{};
                for (int s = 0; s < kSlots; ++s) {
                    roles[size_t(s)] = g.lt.slots[size_t(s)].role;
                    pids[size_t(s)] = g.lt.slots[size_t(s)].pid;
                }
                app.team_sheet->kickoff_check(g.lt.at.user, roles, pids);
            }
        }
    } catch (const std::exception& e) {
        g.error = e.what();
    } catch (...) {
        g.error = "unknown error";
    }
}

}  // namespace turbo
