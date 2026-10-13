// FC 27 LE Turbo GUI - Competitions > Gameplay tuning (see ui_tuning.h, core/tuning.h).
//
// Frame-loop rules as in the other tactics views: never throws (an inline error instead); file I/O only on a click, at the first look
// (the undo file, the keep file, Turbo's own presets) and every 2 s (the switch files); the knobs are read at most once a second while the
// view is drawn (and every 2 s by the tick while kept values wait for a career's hub).
#include "ui_tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>

#include "app.h"
#include "core/tuning.h"
#include "file_picker.h"
#include "imgui.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;
using namespace tuning;

struct Tv {
    // the knobs as last read
    std::map<std::string, KnobLive> live;
    bool have = false;
    double next_read = -1.0;
    // the sliders: the value of each knob; a knob the user moved (or a preset set) is "touched" and keeps its value, the others follow
    // the game's value
    std::map<std::string, double> edit;
    std::set<std::string> touched;
    // switch files (every 2 s)
    Switches sw;
    double next_switch = -1.0;
    // the undo file
    UndoRecord undo;
    bool have_undo = false, undo_loaded = false;
    std::string undo_err;
    // the kept values
    KeepStore keep;
    bool keep_loaded = false;
    std::string keep_err, reapplied_key;
    double next_reapply = -1.0;
    // presets
    std::vector<Preset> presets;
    std::vector<std::string> preset_errors;
    bool presets_loaded = false;
    int preset_sel = -1;
    char preset_name[64] = "";
    FilePicker export_fp, import_fp;
    // the job in flight
    bool pending = false;
    PlanKind pending_kind = PlanKind::Apply;
    double pending_since = -1.0;
    UndoRecord prev;
    bool prev_have = false;
    std::vector<std::string> skipped;
    // lines
    std::string last, preset_line, error;
    // the confirmation
    bool confirm_request = false;
    std::vector<std::string> confirm_lines;
};
Tv g;
TuningViewState g_state;

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }
fs::path undo_path(App& app) { return out_dir(app) / undo_file(); }
fs::path keep_path(App& app) { return out_dir(app) / keep_file(); }
fs::path preset_dir(App& app) { return out_dir(app) / presets_dir(); }

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
    g.sw = read_switches(out_dir(app));
    g.next_switch = app.now + 2.0;
}

Context ctx_of(App& app) {
    const BridgeState& st = app.bridge.state();
    Context c;
    c.session = st.session;
    c.load_gen = st.load_gen;
    c.phase = st.match_phase;
    c.in_cm = st.loaded && st.in_cm;
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
        log_line(app, "gameplay tuning: " + undo_path(app).string() + " not written: " + e);
        if (err) *err = e;
        return false;
    }
    g.undo = u;
    g.have_undo = true;
    return true;
}

void load_keep_once(App& app) {
    if (g.keep_loaded) return;
    g.keep_loaded = true;
    std::string err;
    KeepStore k;
    if (load_keep(keep_path(app), k, &err)) {
        g.keep = k;
        g.keep_err.clear();
    } else {
        g.keep = KeepStore();
        std::error_code ec;
        g.keep_err = fs::exists(keep_path(app), ec) ? err : "";
    }
}

bool save_keep_store(App& app) {
    std::string err;
    g.keep.when = stamp();
    if (!save_keep(keep_path(app), g.keep, &err)) {
        g.keep_err = err;
        log_line(app, "gameplay tuning: turbo_output\\" + std::string(keep_file()) + " not written: " + err);
        return false;
    }
    g.keep_err.clear();
    return true;
}

void load_presets_now(App& app) {
    std::string err;
    const int n = ensure_builtin_presets(preset_dir(app), &err);
    if (n > 0) log_line(app, "gameplay tuning: " + std::to_string(n) + " of Turbo's own presets written to turbo_output\\" + presets_dir());
    g.preset_errors.clear();
    g.presets = load_presets(preset_dir(app), &g.preset_errors);
    if (g.preset_sel >= int(g.presets.size())) g.preset_sel = -1;
    g.presets_loaded = true;
}

std::shared_ptr<const Registry> registry_of(App& app) {
    if (!app.tuning) return std::make_shared<Registry>();
    return app.tuning->registry();
}

// One read of every knob; untouched sliders follow the game's values
void read_now(App& app) {
    g.live.clear();
    g.have = false;
    if (!app.tuning) return;
    for (const KnobLive& l : app.tuning->read()) g.live[l.id] = l;
    g.have = true;
    for (const auto& kv : g.live) {
        if (!kv.second.located) {
            g.edit.erase(kv.first);
            g.touched.erase(kv.first);
            continue;
        }
        if (!g.touched.count(kv.first) || !g.edit.count(kv.first)) g.edit[kv.first] = kv.second.value;
    }
}

// The game default of one copy of a knob: FC 27's own value from the knobs file, else the first snapshot of this game session, else
// (neither known) false
bool copy_default(const Knob& k, const std::map<std::string, double>& defaults, const std::string& copy, double& v) {
    if (k.game_value(copy, v)) {
        v = representable(k, v);
        return true;
    }
    auto d = defaults.find(copy_key(k.id, copy));
    if (d == defaults.end()) return false;
    v = d->second;
    return true;
}

// The game default of a knob (its first copy, "base": a knob with several database copies shows and keeps its first one): FC 27's own
// value, else the first snapshot of this game session, else (Turbo never wrote it) the value it holds now
bool base_of(App& app, const std::map<std::string, double>& defaults, const std::string& id, double& v) {
    auto l = g.live.find(id);
    if (l == g.live.end() || !l->second.located) return false;
    const std::string first = l->second.copies.empty() ? std::string() : l->second.copies.front().variant;
    const auto reg = app.tuning ? app.tuning->registry() : nullptr;
    const Knob* k = reg ? reg->knob(id) : nullptr;
    if (k && copy_default(*k, defaults, first, v)) return true;
    v = l->second.value;
    return true;
}

// The per-copy game defaults of one knob (the row's Reset): "" + map by copy_key, else why there is nothing to reset
std::string reset_defaults(const Knob& k, const std::map<std::string, double>& defaults, std::map<std::string, double>& out) {
    out.clear();
    auto l = g.live.find(k.id);
    if (l == g.live.end() || !l->second.located) return "not located";
    bool differs = false;
    for (const CopyAt& c : l->second.copies) {
        double v = 0;
        if (!copy_default(k, defaults, c.variant, v)) return "the game's own value of its " + (c.variant.empty() ? std::string("table") : c.variant + " copy") + " is not known";
        out[copy_key(k.id, c.variant)] = v;
        differs = differs || !same_value(k.type, c.value, v);
    }
    return differs ? "" : "it holds the game's own value";
}

// The touched sliders that differ from the game's value
std::map<std::string, double> wanted(const Registry& reg) {
    std::map<std::string, double> w;
    for (const std::string& id : g.touched) {
        const Knob* k = reg.knob(id);
        auto e = g.edit.find(id);
        auto l = g.live.find(id);
        if (!k || e == g.edit.end() || l == g.live.end() || !l->second.located) continue;
        bool differs = !same_value(k->type, representable(*k, e->second), l->second.value);
        for (const CopyAt& c : l->second.copies) differs = differs || !same_value(k->type, representable(*k, e->second), c.value);
        if (differs) w[id] = e->second;
    }
    return w;
}

GateInput gate_input(App& app, const Registry& reg, const Plan* plan, const std::string& locate, const std::string& values, bool restore) {
    const BridgeState& st = app.bridge.state();
    GateInput in;
    in.sw = g.sw;
    in.bridge = st.loaded;
    in.in_cm = st.in_cm;
    in.match_phase = st.match_phase;
    in.save_age = st.save_t > 0 ? (long long)(std::time(nullptr)) - st.save_t : -1;
    in.load_gen = st.load_gen;
    in.registry = app.tuning ? app.tuning->registry_status() : "this Turbo has no gameplay tuning service";
    in.locate = locate;
    in.values = values;
    in.session_off = app.tuning ? app.tuning->session_off() : "";
    in.restore = restore;
    if (plan)
        for (const Write& w : plan->writes)
            if (const Knob* k = reg.knob(w.id)) in.knobs.push_back(k);
    return in;
}

// The Apply plan of the sliders with its G4 / G5 lines
struct ApplyView {
    Plan plan;
    std::string locate, values;
};
ApplyView apply_view(const Registry& reg) {
    ApplyView v;
    const std::map<std::string, double> w = wanted(reg);
    for (const auto& kv : w) {
        auto l = g.live.find(kv.first);
        if (l == g.live.end() || !l->second.located) {
            const Knob* k = reg.knob(kv.first);
            v.locate = (k ? k->label : kv.first) + ": " + (l == g.live.end() ? std::string("not located on this build") : l->second.why);
            return v;
        }
    }
    v.values = make_plan(reg, g.live, w, PlanKind::Apply, v.plan);
    if (v.values.empty() && v.plan.writes.empty()) v.values = "nothing changed";
    return v;
}

std::vector<std::string> plan_lines(const Registry& reg, const Plan& p) {
    std::vector<std::string> out;
    for (const Write& w : p.writes) {
        const Knob* k = reg.knob(w.id);
        if (!k) continue;
        const char* when = k->applies == Applies::Live ? "meant to apply at once" : k->applies == Applies::NextMatchSetup ? "meant for the next match setup"
                                                                                                                         : "when it applies is not known yet";
        out.push_back(k->label + (w.copy.empty() ? std::string() : " [" + w.copy + "]") + ": " + format_value(*k, w.old_value) + " -> " +
                      format_value(*k, w.new_value) + " (" + when + ")");
    }
    return out;
}

void after_job_follow(const std::vector<Write>& ws) {
    for (const Write& w : ws) g.touched.erase(w.id);  // the sliders follow the game's value again
}

// ---------------------------------------------------------------------------------------------------------------- the actions
bool queue(App& app, const Plan& plan_in, const std::string& what) {
    Plan plan = plan_in;
    const BridgeState& st = app.bridge.state();
    plan.load_gen = st.load_gen;
    plan.session = st.session;
    std::string why;
    if (!app.tuning->submit(ctx_of(app), plan, why)) {
        app.notify(what + ": " + why, true);
        g.last = what + " not queued: " + why;
        return false;
    }
    g.pending = true;
    g.pending_kind = plan.kind;
    g.pending_since = app.now;
    g.last = std::string(plan_kind_name(plan.kind)) + " queued for the game thread ...";
    return true;
}

// Apply and the re-apply at a career load: the undo file first (it holds the values before the write before anything is written)
bool submit_write(App& app, const Plan& plan, const std::string& what) {
    const BridgeState& st = app.bridge.state();
    const auto reg = registry_of(app);
    UndoRecord rec;
    rec.build = reg->build;
    rec.session = st.session;
    rec.load_gen = st.load_gen;
    rec.knobs = plan.writes;
    g.prev = g.undo;
    g.prev_have = g.have_undo;
    if (g.have_undo) rec = merge_undo(g.undo, rec);
    rec.when = stamp();
    rec.state = "pending";
    rec.note.clear();
    std::string err;
    if (!save_record(app, rec, &err)) {
        app.notify(what + ": turbo_output\\" + std::string(undo_file()) + " could not be written (" + err + ")", true);
        return false;
    }
    if (!queue(app, plan, what)) {
        if (g.prev_have) save_record(app, g.prev);
        else {
            rec.state = "aborted";
            rec.note = g.last;
            save_record(app, rec);
        }
        return false;
    }
    return true;
}

void do_apply(App& app) {
    if (!app.tuning) return;
    refresh_switches(app, true);
    load_undo_once(app);
    read_now(app);
    const auto reg = registry_of(app);
    const ApplyView v = apply_view(*reg);
    const Gates gs = evaluate(gate_input(app, *reg, &v.plan, v.locate, v.values, false));
    if (!gs.pass()) {
        app.notify("Gameplay tuning not written: " + gs.first(), true);
        return;
    }
    submit_write(app, v.plan, "Gameplay tuning not written");
}

void do_undo(App& app) {
    if (!app.tuning) return;
    refresh_switches(app, true);
    load_undo_once(app);
    read_now(app);
    const auto reg = registry_of(app);
    if (!g.have_undo) {
        app.notify("Nothing to put back: turbo_output\\" + std::string(undo_file()) + " holds no write", true);
        return;
    }
    const BridgeState& st = app.bridge.state();
    Plan plan;
    const std::string w = make_undo_plan(*reg, g.live, g.undo, st.session, st.load_gen, plan, g.skipped);
    if (!w.empty()) {
        app.notify("Nothing put back: " + w, true);
        g.last = "undo: " + w;
        return;
    }
    const Gates gs = evaluate(gate_input(app, *reg, &plan, "", "", true));
    if (!gs.pass()) {
        app.notify("Nothing put back: " + gs.first(), true);
        return;
    }
    queue(app, plan, "Nothing put back");
}

// The row's Reset: the knob's every copy back to the game's own value now (a Restore job for that knob: no opt-in, the kill switches
// do not block it, the owner rule holds); a slider edit of the knob is dropped
void do_reset(App& app, const std::string& id) {
    if (!app.tuning) return;
    g.touched.erase(id);
    refresh_switches(app, true);
    read_now(app);
    const auto reg = registry_of(app);
    const Knob* k = reg->knob(id);
    if (!k) return;
    std::map<std::string, double> defs;
    const std::string none = reset_defaults(*k, app.tuning->defaults(), defs);
    if (!none.empty()) {
        g.last = k->label + ": slider back to the game's value (" + none + ")";
        return;
    }
    Plan plan;
    const std::string w = make_restore_plan(*reg, g.live, defs, plan, g.skipped, id);
    if (!w.empty() || plan.writes.empty()) {
        app.notify(k->label + " not reset: " + (w.empty() ? std::string("nothing to put back") : w) + (g.skipped.empty() ? "" : " (" + g.skipped.front() + ")"),
                   true);
        return;
    }
    const Gates gs = evaluate(gate_input(app, *reg, &plan, "", "", true));
    if (!gs.pass()) {
        app.notify(k->label + " not reset: " + gs.first(), true);
        return;
    }
    queue(app, plan, k->label + " not reset");
}

void do_restore(App& app) {
    if (!app.tuning) return;
    refresh_switches(app, true);
    read_now(app);
    const auto reg = registry_of(app);
    Plan plan;
    const std::string w = make_restore_plan(*reg, g.live, app.tuning->defaults(), plan, g.skipped);
    if (!w.empty() || plan.writes.empty()) {
        app.notify("Game defaults: " + (w.empty() ? std::string("every knob Turbo wrote already holds its game default") : w), !w.empty());
        return;
    }
    const Gates gs = evaluate(gate_input(app, *reg, &plan, "", "", true));
    if (!gs.pass()) {
        app.notify("Game defaults not restored: " + gs.first(), true);
        return;
    }
    queue(app, plan, "Game defaults not restored");
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : "; ") + x;
    return s;
}

void handle_result(App& app, const ApplyResult& r) {
    g.pending = false;
    g.last = r.log_line.empty() ? r.message : r.log_line;
    const auto reg = registry_of(app);
    const BridgeState& st = app.bridge.state();
    if (r.kind == PlanKind::Undo || r.kind == PlanKind::Restore) {
        if (r.ok && r.written) {
            if (g.have_undo && same_career(g.undo, st.session, st.load_gen).empty() && (g.undo.state == "written" || g.undo.state == "failed")) {
                UndoRecord u = g.undo;
                // what is still Turbo's after this job (a Restore may have put back only some of the knobs of the record)
                std::vector<Write> left;
                for (const Write& k : u.knobs) {
                    bool back = false;
                    for (const Write& w : r.writes) back = back || w.id == k.id;
                    if (!back) left.push_back(k);
                }
                if (r.kind == PlanKind::Undo || left.empty()) {
                    u.state = "restored";
                    u.note = (r.kind == PlanKind::Restore ? "Restore game defaults" : "") +
                             std::string(g.skipped.empty() ? "" : (r.kind == PlanKind::Restore ? "; " : "") + std::string("partly: ") + joined(g.skipped));
                } else {
                    u.knobs = left;
                    u.note = "Restore game defaults put back " + std::to_string(r.writes.size()) + " knob(s)";
                }
                save_record(app, u);
            }
            if (g.keep.on) {
                for (const Write& w : r.writes) g.keep.values.erase(w.id);
                save_keep_store(app);
            }
            after_job_follow(r.writes);
            app.notify(std::string(r.kind == PlanKind::Undo ? "Gameplay tuning put back to the values before Turbo's write" : "Game defaults restored") +
                       " (read back)" + (g.skipped.empty() ? "" : ": " + joined(g.skipped)));
        } else if (r.ok) {
            app.notify("Gameplay tuning: " + r.message);
        } else {
            if (g.have_undo && r.kind == PlanKind::Undo) {
                UndoRecord u = g.undo;
                u.note = "undo " + r.stage + ": " + r.message;
                save_record(app, u);
            }
            app.notify("Gameplay tuning not put back: " + r.message, true);
        }
        read_now(app);
        return;
    }
    // Apply / re-apply
    if (r.ok && r.written) {
        // the record holds the values Apply was pressed for, which the job found in place before writing (a merged record keeps the
        // values from before Turbo's first write in this career load)
        UndoRecord u = g.undo;
        u.state = "written";
        save_record(app, u);
        if (g.keep.on && r.kind == PlanKind::Apply) {
            g.keep.build = reg->build;
            for (const Write& w : r.writes) g.keep.values[w.id] = w.new_value;
            save_keep_store(app);
        }
        after_job_follow(r.writes);
        // no knob's effect on a match has been measured: say only what was proven (written + read back)
        app.notify(std::string(r.kind == PlanKind::Reapply ? "Kept gameplay tuning written again" : "Gameplay tuning written") +
                   " to the engine table and read back; whether the match uses it is not measured yet");
    } else if (r.ok) {
        if (g.prev_have) save_record(app, g.prev);
        if (r.kind == PlanKind::Apply) app.notify("Gameplay tuning: " + r.message);
    } else {
        if (g.prev_have && g.prev.state == "written") {
            UndoRecord u = g.prev;
            u.note = "a later write was not made (" + r.stage + "): " + r.message;
            save_record(app, u);
        } else {
            UndoRecord u = g.undo;
            u.state = r.undone ? "undone" : (r.restore_failed ? "failed" : "aborted");
            u.note = r.stage + ": " + r.message;
            save_record(app, u);
        }
        app.notify("Gameplay tuning not written: " + r.message, true);
    }
    read_now(app);
}

// The kept values at a newly loaded career: one job at its first hub (opt-in: the keep file's "on"; the usual gates)
void maybe_reapply(App& app) {
    if (!app.tuning || g.pending) return;
    load_keep_once(app);
    if (!g.keep.on || g.keep.values.empty()) return;
    const BridgeState& st = app.bridge.state();
    if (!st.loaded || !st.in_cm || st.load_gen < 0) return;
    const std::string key = st.session + "#" + std::to_string(st.load_gen);
    if (key == g.reapplied_key || app.now < g.next_reapply) return;
    g.next_reapply = app.now + 2.0;
    if (!app.tuning->registry_status().empty()) {
        g.reapplied_key = key;
        log_line(app, "gameplay tuning: kept values not written at this career load: " + app.tuning->registry_status());
        return;
    }
    refresh_switches(app, false);
    load_undo_once(app);
    read_now(app);
    const auto reg = registry_of(app);
    Plan plan;
    std::vector<std::string> skipped;
    const std::string w = make_keep_plan(*reg, g.live, g.keep, plan, skipped);
    if (!w.empty()) {
        g.reapplied_key = key;
        log_line(app, "gameplay tuning: kept values not written at this career load: " + w);
        return;
    }
    if (plan.writes.empty()) {
        g.reapplied_key = key;
        log_line(app, "gameplay tuning: kept values: nothing to write at this career load" + (skipped.empty() ? std::string(" (already in place)") : ": " + joined(skipped)));
        return;
    }
    const Gates gs = evaluate(gate_input(app, *reg, &plan, "", "", false));
    if (!gs.pass()) {
        // the phase (or a save) may still change: wait for the hub; a switch or the knobs file ends the attempt for this load
        if (gs.list.size() > 1 && !gs.list[1].pass() && gs.list[0].pass()) return;
        g.reapplied_key = key;
        log_line(app, "gameplay tuning: kept values not written at this career load: " + gs.first());
        return;
    }
    g.reapplied_key = key;
    if (!skipped.empty()) log_line(app, "gameplay tuning: kept values skipped: " + joined(skipped));
    if (submit_write(app, plan, "Kept gameplay tuning not written")) log_line(app, "gameplay tuning: kept values queued for this career load (" + key + ")");
}

// ---------------------------------------------------------------------------------------------------------------- presets
void load_preset(App& app, const Preset& p) {
    const auto reg = registry_of(app);
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    const PresetValues pv = preset_values(p, *reg, [&](const std::string& id, double& v) { return base_of(app, defs, id, v); });
    for (const auto& kv : pv.values) {
        auto l = g.live.find(kv.first);
        if (l == g.live.end() || !l->second.writable) continue;
        g.edit[kv.first] = kv.second;
        g.touched.insert(kv.first);
    }
    g.preset_line = "Loaded \"" + p.name + "\" into the sliders: " + std::to_string(pv.values.size()) + " knob(s) set" +
                    (pv.skipped.empty() ? "" : ", " + std::to_string(pv.skipped.size()) + " skipped (" + joined(pv.skipped) + ")") + ". Nothing is written until Apply.";
}

void save_preset(App& app) {
    const auto reg = registry_of(app);
    const std::string name = g.preset_name;
    if (name.empty()) {
        app.notify("Type a name for the preset first", true);
        return;
    }
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    std::map<std::string, double> vals;
    for (const auto& kv : g.edit) vals[kv.first] = kv.second;
    Preset p = preset_from_values(name, *reg, vals, [&](const std::string& id, double& v) { return base_of(app, defs, id, v); });
    if (p.changes.empty()) {
        app.notify("Nothing to save: every slider holds the game default", true);
        return;
    }
    const fs::path f = preset_dir(app) / preset_file_name(p);
    for (const Preset& q : g.presets)
        if (q.file == f && q.turbo()) {
            app.notify("That name is one of Turbo's own presets: pick another", true);
            return;
        }
    std::error_code ec;
    fs::create_directories(preset_dir(app), ec);
    std::ofstream o(f, std::ios::binary | std::ios::trunc);
    o << p.to_json();
    if (!o) {
        app.notify("The preset could not be written: " + f.string(), true);
        return;
    }
    o.close();
    g.preset_line = "Saved \"" + p.name + "\" (" + std::to_string(p.changes.size()) + " knob(s)) to turbo_output\\" + presets_dir() + "\\" + f.filename().string();
    log_line(app, "gameplay tuning: " + g.preset_line);
    load_presets_now(app);
}

void import_preset(App& app, const fs::path& from) {
    std::ifstream f(from, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Preset p;
    std::string err;
    if (!f || !Preset::parse(text, p, &err)) {
        app.notify("Not imported: " + (err.empty() ? std::string("cannot read the file") : err), true);
        return;
    }
    if (p.turbo()) p.author = "imported";  // only Turbo writes Turbo's own presets
    const fs::path to = preset_dir(app) / preset_file_name(p);
    for (const Preset& q : g.presets)
        if (q.file == to && q.turbo()) {
            app.notify("Not imported: its name is one of Turbo's own presets", true);
            return;
        }
    std::error_code ec;
    fs::create_directories(preset_dir(app), ec);
    const bool replaced = fs::exists(to, ec);
    std::ofstream o(to, std::ios::binary | std::ios::trunc);
    o << p.to_json();
    if (!o) {
        app.notify("Not imported: cannot write " + to.string(), true);
        return;
    }
    o.close();
    g.preset_line = std::string(replaced ? "Imported (replaced) \"" : "Imported \"") + p.name + "\" into turbo_output\\" + presets_dir();
    log_line(app, "gameplay tuning: " + g.preset_line + " from " + from.string());
    load_presets_now(app);
}

void export_preset(App& app, const fs::path& to) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    const Preset& p = g.presets[size_t(g.preset_sel)];
    std::ofstream o(to, std::ios::binary | std::ios::trunc);
    o << p.to_json();
    if (!o) {
        app.notify("Not exported: cannot write " + to.string(), true);
        return;
    }
    g.preset_line = "Exported \"" + p.name + "\" to " + to.string();
    log_line(app, "gameplay tuning: " + g.preset_line);
}

void draw_presets(App& app) {
    ImGui::SeparatorText("Presets");
    for (const Preset& p : g.presets) g_state.presets.push_back(p.name + (p.turbo() ? " (Turbo)" : ""));
    const std::string cur = g.preset_sel >= 0 && g.preset_sel < int(g.presets.size()) ? g_state.presets[size_t(g.preset_sel)] : std::string("(pick a preset)");
    ImGui::SetNextItemWidth(S(260.0f));
    if (ImGui::BeginCombo("##tvpreset", cur.c_str())) {
        for (size_t i = 0; i < g.presets.size(); ++i) {
            if (ImGui::Selectable((g_state.presets[i] + "##tvp" + std::to_string(i)).c_str(), int(i) == g.preset_sel)) g.preset_sel = int(i);
            if (ImGui::IsItemHovered() && !g.presets[i].note.empty()) ImGui::SetTooltip("%s", g.presets[i].note.c_str());
        }
        ImGui::EndCombo();
    }
    const bool have = g.preset_sel >= 0 && g.preset_sel < int(g.presets.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Load into the sliders##tvload") && have) load_preset(app, g.presets[size_t(g.preset_sel)]);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sets the sliders from the preset (relative changes start from each knob's game default). Nothing is written until Apply.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Export...##tvexp")) {
        FilePicker& fp = g.export_fp;
        fp.mode = PickMode::Save;
        fp.title = "Export the preset to";
        fp.exts = {".json"};
        fp.key = "tuning.export";
        fp.start = out_dir(app);
        if (have) std::snprintf(fp.name, sizeof(fp.name), "%s", preset_file_name(g.presets[size_t(g.preset_sel)]).c_str());
        ImGui::OpenPopup("##tvexppick");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Import...##tvimp")) {
        FilePicker& fp = g.import_fp;
        fp.mode = PickMode::Open;
        fp.title = "Choose a Turbo gameplay tuning preset";
        fp.exts = {".json"};
        fp.key = "tuning.import";
        fp.start = out_dir(app);
        ImGui::OpenPopup("##tvimppick");
    }
    fs::path chosen;
    if (file_picker_modal("##tvexppick", g.export_fp, chosen)) export_preset(app, chosen);
    if (file_picker_modal("##tvimppick", g.import_fp, chosen)) import_preset(app, chosen);
    form_label("Save the sliders as", "A preset of your own in turbo_output\\tuning_presets: each moved knob as a change relative to its game default");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputText("##tvname", g.preset_name, sizeof(g.preset_name));
    ImGui::SameLine();
    if (ImGui::Button("Save preset##tvsave")) save_preset(app);
    for (const std::string& e : g.preset_errors) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Preset not read: %s", e.c_str());
    if (!g.preset_line.empty()) ImGui::TextWrapped("%s", g.preset_line.c_str());
    g_state.preset_line = g.preset_line;
}

// ---------------------------------------------------------------------------------------------------------------- drawing
const char* applies_badge(Applies a) {
    switch (a) {
        case Applies::Live: return "meant live, not measured";
        case Applies::NextMatchSetup: return "meant for next match, not measured";
        case Applies::Unknown: return "applies: not known yet";
    }
    return "applies: not known yet";
}

void draw_confirm(App& app) {
    if (g.confirm_request) {
        ImGui::OpenPopup("Apply gameplay tuning?##tvconfirm");
        g.confirm_request = false;
    }
    g_state.confirm_open = false;
    if (ImGui::BeginPopupModal("Apply gameplay tuning?##tvconfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        g_state.confirm_open = true;
        g_state.confirm_lines = g.confirm_lines;
        ImGui::TextUnformatted("Turbo writes these values into the game's tuning tables (every club, every match):");
        for (const std::string& l : g.confirm_lines) ImGui::BulletText("%s", l.c_str());
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1), "Untested: no knob has been seen in a played match yet.");
        ImGui::TextUnformatted("The values before the write go to turbo_output\\tuning_undo.json; Restore game defaults puts every knob back.");
        if (ImGui::Button("Apply##tvyes")) {
            ImGui::CloseCurrentPopup();
            do_apply(app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##tvno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_row(App& app, const Knob& k, const std::map<std::string, double>& defs) {
    TuningViewState::Row row;
    row.id = k.id;
    row.label = k.label;
    row.group = k.group;
    auto l = g.live.find(k.id);
    const bool located = l != g.live.end() && l->second.located;
    const bool writable = located && l->second.writable;
    row.located = located ? "located" : "not located";
    row.applies = applies_badge(k.applies);
    row.why = located ? "" : (l == g.live.end() ? std::string("not read yet") : l->second.why);
    // FC 27's own value (of the first copy: the knobs file, else the value before Turbo's first write), the value in memory now
    double def = 0;
    const std::string first = located && !l->second.copies.empty() ? l->second.copies.front().variant : std::string();
    const bool have_def = copy_default(k, defs, first, def);
    if (located) row.game = format_value(k, l->second.value);
    if (have_def) row.def = format_value(k, def);
    ImGui::PushID(k.id.c_str());
    // the label (tooltip: the table field and the notes)
    std::string tip = k.collection.empty() ? std::string("table field not known yet") : k.collection + " / " + k.field;
    if (!k.notes.empty()) tip += "\n" + k.notes;
    form_label(k.label.c_str(), tip.c_str());
    // the slider and the number box
    row.editable = writable && !g.pending;
    if (writable) {
        double& v = g.edit[k.id];
        double before = v;
        ImGui::BeginDisabled(g.pending);
        if (k.type == ValueType::Bool) {
            bool b = v != 0;
            if (ImGui::Checkbox(("##tvsw_" + k.id).c_str(), &b)) v = b ? 1 : 0;
        } else {
            ImGui::SetNextItemWidth(S(180.0f));
            double lo = k.min, hi = k.max;
            const char* f = k.type == ValueType::F32 ? "%.3f" : "%.0f";
            ImGui::SliderScalar(("##tvs_" + k.id).c_str(), ImGuiDataType_Double, &v, &lo, &hi, f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(80.0f));
            ImGui::InputDouble(("##tvbox_" + k.id).c_str(), &v, 0.0, 0.0, f);
            v = std::min(std::max(v, k.min), k.max);
            if (k.type != ValueType::F32) v = std::round(v);
        }
        ImGui::EndDisabled();
        if (v != before) g.touched.insert(k.id);
        if (!k.unit.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", k.unit.c_str());
        }
        row.turbo = format_value(k, v);
        row.changed = !same_value(k.type, representable(k, v), l->second.value);
    } else {
        ImGui::TextDisabled("%s", located ? "read-only (no value range in the knobs file)" : "Not located on this build: shown only, never written");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", row.why.empty() ? "no value range" : row.why.c_str());
    }
    // the badges
    ImGui::SameLine();
    ImGui::TextColored(located ? ImVec4(0.45f, 0.85f, 0.45f, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1), "[%s]", row.located.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", located ? ("at the address the locator found now; type " + std::string(type_name(k.type))).c_str() : row.why.c_str());
    ImGui::SameLine();
    ImGui::TextColored(k.applies == Applies::Live ? ImVec4(0.5f, 0.8f, 0.95f, 1) : ImVec4(0.9f, 0.8f, 0.4f, 1), "[%s]", row.applies.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", k.applies == Applies::Live ? "meant to be read while a match is played; a write is checked by reading it back, whether the match uses it is not measured yet"
                                : k.applies == Applies::NextMatchSetup ? "meant to be read when the game sets a match up; a write is checked by reading it back, whether the match uses it is not measured yet"
                                                                       : "when the game reads this table is not known yet");
    // the game's own value, the value now, Reset
    if (located) {
        ImGui::SameLine();
        ImGui::TextDisabled("FC 27: %s | now: %s", have_def ? row.def.c_str() : "?", row.game.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("FC 27: the game's own value (Reset puts it back). now: what the game holds in memory at this moment.");
        if (l->second.copies.size() > 1) {
            // one knob, several database copies (base, simulation = Authentic, ...): every copy is written together
            std::string cs;
            for (const CopyAt& c : l->second.copies) cs += (cs.empty() ? "" : ", ") + c.variant + " " + format_value(k, c.value, false);
            row.copies = cs;
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu copies: %s)", l->second.copies.size(), cs.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The game keeps this table once per gameplay database (base, simulation = Authentic). Turbo writes every copy "
                                  "together, or none.");
        }
        if (writable) {
            std::map<std::string, double> rd;
            const std::string none = reset_defaults(k, defs, rd);
            row.can_reset = (none.empty() || g.touched.count(k.id)) && !g.pending;
            ImGui::SameLine();
            ImGui::BeginDisabled(!row.can_reset);
            if (ImGui::SmallButton(("Reset##tvreset_" + k.id).c_str())) do_reset(app, k.id);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", none.empty() ? ("Puts the game's own value (" + row.def + ") back now, in every copy (no Apply needed).").c_str()
                                        : g.touched.count(k.id) ? "Drops your slider edit (the knob already holds the game's own value)."
                                                                : ("Nothing to reset: " + none + ".").c_str());
        }
    } else if (have_def) {
        ImGui::SameLine();
        ImGui::TextDisabled("FC 27: %s", row.def.c_str());
    }
    ImGui::PopID();
    g_state.rows.push_back(row);
}

void draw_body(App& app) {
    g_state.drawn = true;
    refresh_switches(app, false);
    load_undo_once(app);
    load_keep_once(app);
    if (!g.presets_loaded) load_presets_now(app);
    if (app.now >= g.next_read) {
        g.next_read = app.now + 1.0;
        read_now(app);
    }
    const auto reg = registry_of(app);
    const std::string reg_why = app.tuning ? app.tuning->registry_status() : "this Turbo has no gameplay tuning service";
    g_state.opt_in = g.sw.opt_in;
    g_state.busy = g.pending;

    ImGui::TextWrapped(
        "The game engine's gameplay tuning tables: how teams hold their shape, press, mark and make runs in every match (every club, both teams).");
    ImGui::TextWrapped(
        "Values last for this game session only: the game does not keep these tables in the career save, so they are back to FC 27's own values "
        "the next time the game starts. Turbo writes them again at a career load only if you switch on \"Write these values again at every career "
        "load\" below.");
    g_state.session_note = true;
    if (reg_why.empty()) {
        const int ph = reg->placeholders();
        g_state.registry_line = "turbo\\tuning\\" + fs::path(reg->source).filename().string() + ": " + std::to_string(reg->knobs.size()) + " knobs (" +
                                std::to_string(ph) + " not confirmed) for game build " + reg->build;
        ImGui::TextDisabled("%s", g_state.registry_line.c_str());
        int located = 0;
        for (const auto& kv : g.live) located += kv.second.located ? 1 : 0;
        if (ph > 0 && located == 0)
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1),
                               "Not located on this build: none of these knobs has been confirmed in the running game yet. They are shown with "
                               "FC 27's own values; Turbo writes nothing until a live check confirms them on this game build.");
        else if (ph > 0)
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1), "%d knob(s) not confirmed on this build: shown only, never written.", ph);
    } else {
        g_state.registry_line = reg_why;
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", reg_why.c_str());
    }
    g_state.anchors_line = app.tuning ? app.tuning->anchors_status() : "";
    if (!g_state.anchors_line.empty() && reg_why.empty()) ImGui::TextDisabled("Locators: %s", g_state.anchors_line.c_str());
    if (app.tuning && ImGui::SmallButton("Read the knobs file again##tvreload")) {
        std::string why;
        app.tuning->reload(why);
        g.next_read = 0.0;
        app.notify(why.empty() ? "Knobs file read again" : "Knobs file: " + why, !why.empty());
    }

    // the opt-in
    bool on = g.sw.opt_in;
    if (checkbox_right("Gameplay tuning on (turbo_output\\tuning_on.txt)##tvopt", &on)) {
        std::error_code ec;
        const fs::path p = out_dir(app) / opt_in_file();
        if (on) {
            fs::create_directories(out_dir(app), ec);
            std::ofstream(p, std::ios::binary | std::ios::trunc) << "Turbo gameplay tuning on: Turbo may write the game's tuning tables (switched on in the Turbo window "
                                                                  << stamp() << ")\n";
        } else {
            fs::remove(p, ec);
        }
        refresh_switches(app, true);
        log_line(app, std::string("gameplay tuning: ") + (g.sw.opt_in ? "switched on" : "switched off") + " (turbo_output\\" + opt_in_file() + ")");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off by default. On creates turbo_output\\tuning_on.txt; tuning_off.txt, call_attrib_off.txt or tactics_off.txt switch "
                          "every write off (Undo, Reset and Restore game defaults still work: they put the game's own values back).");

    // the plans and the gates
    const ApplyView av = apply_view(*reg);
    g_state.plan_error = av.locate.empty() ? av.values : av.locate;
    const Gates gs = evaluate(gate_input(app, *reg, &av.plan, av.locate, av.values, false));
    g_state.gates.clear();
    for (const Gate& x : gs.list) g_state.gates.push_back(x.name + ": " + (x.pass() ? "ok" : x.why));
    g_state.status = gs.pass() ? "ready" : gs.first();
    g_state.can_apply = gs.pass() && !g.pending && app.tuning != nullptr;
    const BridgeState& st = app.bridge.state();
    {
        Plan up;
        std::vector<std::string> sk;
        std::string w = !g.have_undo ? std::string("no Apply to put back") : make_undo_plan(*reg, g.live, g.undo, st.session, st.load_gen, up, sk);
        if (w.empty()) {
            const Gates ug = evaluate(gate_input(app, *reg, &up, "", "", true));
            if (!ug.pass()) w = ug.first();
        }
        g_state.undo_why = w;
        g_state.can_undo = w.empty() && !g.pending && app.tuning != nullptr;
    }
    {
        Plan rp;
        std::vector<std::string> sk;
        std::string w = app.tuning ? make_restore_plan(*reg, g.live, app.tuning->defaults(), rp, sk) : "no service";
        if (w.empty() && rp.writes.empty()) w = "every knob Turbo wrote holds its game default";
        if (w.empty()) {
            const Gates rg = evaluate(gate_input(app, *reg, &rp, "", "", true));
            if (!rg.pass()) w = rg.first();
        }
        g_state.restore_why = w;
        g_state.can_restore = w.empty() && !g.pending && app.tuning != nullptr;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, gs.pass() ? ImVec4(0.55f, 0.85f, 0.55f, 1) : ImVec4(1, 0.7f, 0.3f, 1));
    ImGui::TextWrapped("%s", gs.pass() ? "Ready to write." : ("Not ready: " + g_state.status).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        std::string tip;
        for (const auto& x : g_state.gates) tip += x + "\n";
        ImGui::SetTooltip("%s", tip.c_str());
    }

    // what Apply writes, the actions (above the knobs: always in view)
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    g_state.plan = plan_lines(*reg, av.plan);
    ImGui::SeparatorText("Apply");
    if (!g_state.plan.empty()) {
        ImGui::TextUnformatted("Apply will write:");
        for (const std::string& l : g_state.plan) ImGui::BulletText("%s", l.c_str());
    } else if (!g_state.plan_error.empty()) {
        ImGui::TextDisabled("Apply: %s", g_state.plan_error.c_str());
    }
    ImGui::BeginDisabled(!g_state.can_apply);
    if (ImGui::Button("Apply##tvapply")) {
        g.confirm_lines = g_state.plan;
        g.confirm_request = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", g_state.can_apply ? "Writes every moved knob in one go on the game thread, reads them back, puts every knob back on a mismatch."
                                                  : g_state.status.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_state.can_undo);
    if (ImGui::Button("Undo last Apply##tvundo")) do_undo(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", g_state.can_undo ? "Puts back the values from before Turbo's first write in this career load (turbo_output\\tuning_undo.json), "
                                                   "only knobs that still hold Turbo's value."
                                                 : g_state.undo_why.c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_state.can_restore);
    if (ImGui::Button("Restore game defaults##tvrestore")) do_restore(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", g_state.can_restore ? "Every knob Turbo wrote in this game session back to the value it had before Turbo's first write."
                                                    : g_state.restore_why.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Discard edits##tvdiscard")) {
        g.touched.clear();
        read_now(app);
    }
    draw_confirm(app);

    // the kept values
    bool keep = g.keep.on;
    if (checkbox_right("Write these values again at every career load (turbo_output\\tuning_keep.json)##tvkeep", &keep)) {
        g.keep.on = keep;
        if (keep) {
            g.keep.build = reg->build;
            g.keep.values.clear();
            for (const Knob& k : reg->knobs) {
                auto l = g.live.find(k.id);
                double base = 0;
                if (l != g.live.end() && l->second.located && base_of(app, defs, k.id, base) && !same_value(k.type, l->second.value, base))
                    g.keep.values[k.id] = l->second.value;
            }
        }
        save_keep_store(app);
        log_line(app, std::string("gameplay tuning: keeping ") + (keep ? "on (" + std::to_string(g.keep.values.size()) + " values)" : "off"));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The values Turbo wrote are written again at the first hub of every career load (one job, the same gates and checks). "
                          "Off by default: without it the values last until the game is closed.");
    g_state.keep_on = g.keep.on;
    g_state.keep_line = g.keep.on ? std::to_string(g.keep.values.size()) + " value(s) kept" + (g.reapplied_key.empty() ? "" : "; last career load: " + g.reapplied_key)
                                  : std::string("not kept");
    if (!g.keep_err.empty()) g_state.keep_line += " (turbo_output\\" + std::string(keep_file()) + ": " + g.keep_err + ")";
    ImGui::TextDisabled("Kept: %s", g_state.keep_line.c_str());

    draw_presets(app);

    // the knobs, one group each
    g_state.groups = reg->groups();
    for (const std::string& grp : g_state.groups) {
        if (!ImGui::CollapsingHeader((grp + "##tvg").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const Knob& k : reg->knobs)
                if (k.group == grp) {
                    TuningViewState::Row r;
                    r.id = k.id;
                    r.label = k.label;
                    r.group = grp;
                    g_state.rows.push_back(r);  // listed (collapsed)
                }
            continue;
        }
        form_label_width(S(260.0f));
        for (const Knob& k : reg->knobs)
            if (k.group == grp) draw_row(app, k, defs);
        form_label_width(0.0f);
    }
    if (reg->knobs.empty()) ImGui::TextDisabled("No knobs: %s", reg_why.empty() ? "the knobs file lists none" : reg_why.c_str());

    // track T2: the measurement (coming)
    ImGui::SeparatorText("Measure (coming)");
    g_state.measure_enabled = false;
    g_state.measure_label = "Measure 60 s of a match (coming)";
    ImGui::BeginDisabled(true);
    ImGui::Button("Measure 60 s of a match (coming)##tvmeasure");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Coming: Turbo measures 60 s of a match before and after Apply and compares the line height, team width and length, pressing and runs.");

    // what happened
    if (!g.last.empty()) ImGui::TextWrapped("Last: %s", g.last.c_str());
    g_state.last = g.last;
    g_state.undo_state = g.have_undo ? g.undo.state : "";
    if (g.have_undo)
        ImGui::TextWrapped("Undo file: %s, %s%s", g.undo.state.c_str(), g.undo.when.c_str(), g.undo.note.empty() ? "" : (" (" + g.undo.note + ")").c_str());
    else if (!g.undo_err.empty())
        ImGui::TextWrapped("turbo_output\\%s is not used: %s", undo_file(), g.undo_err.c_str());
    const std::string so = app.tuning ? app.tuning->session_off() : "";
    if (!so.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Off for this session: %s", so.c_str());
}

}  // namespace

const TuningViewState& tuning_view_state() { return g_state; }

void tuning_reset() {
    g = Tv();
    g_state = TuningViewState();
}

void draw_tuning(App& app) {
    g_state = TuningViewState();
    try {
        draw_body(app);
        if (!g.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Gameplay tuning error: %s", g.error.c_str());
        g_state.error = g.error;
    } catch (const std::exception& e) {
        g.error = e.what();
        g_state.error = g.error;
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Gameplay tuning error: %s", e.what());
    } catch (...) {
        g.error = "unknown error";
        g_state.error = g.error;
    }
}

void tuning_tick(App& app) {
    try {
        if (!app.tuning) return;
        refresh_switches(app, false);
        app.tuning->note_context(ctx_of(app));
        ApplyResult r;
        while (app.tuning->poll(r)) {
            if (!g.pending) {
                log_line(app, "gameplay tuning: result of a job given up on: " + (r.log_line.empty() ? r.message : r.log_line));
                continue;
            }
            load_undo_once(app);
            handle_result(app, r);
        }
        // a job the game thread never ran: the service stops waiting after 10 s, the view too (a late run writes nothing)
        if (g.pending && g.pending_since >= 0 && app.now - g.pending_since > kJobLostUs / 1e6 + 2.0 && !app.tuning->busy()) {
            g.pending = false;
            g.last = std::string(plan_kind_name(g.pending_kind)) + ": the game thread did not run the job (nothing written); try again";
            if (g.pending_kind == PlanKind::Apply || g.pending_kind == PlanKind::Reapply) {
                if (g.prev_have) save_record(app, g.prev);
                else {
                    UndoRecord u = g.undo;
                    u.state = "aborted";
                    u.note = g.last;
                    save_record(app, u);
                }
            }
            log_line(app, "gameplay tuning: " + g.last);
        }
        maybe_reapply(app);
    } catch (const std::exception& e) {
        g.error = e.what();
    } catch (...) {
        g.error = "unknown error";
    }
}

}  // namespace turbo
