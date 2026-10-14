// FC 27 LE Turbo GUI - Competitions > Gameplay tuning (see ui_tuning.h, core/tuning.h).
//
// Frame-loop rules as in the other tactics views: never throws (an inline error instead); file I/O only on a click, at the first look
// (the undo file, Turbo's own presets) and every 2 s (the switch files, the profile file); the knobs are read at most once a second while
// the view is drawn, and when an automatic moment is due; the locate is advanced once a second by the tick.
#include "ui_tuning.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
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
    // the sliders (proven knobs): the value of each knob; a knob the user moved (or a preset set) is "touched" and keeps its value, the
    // others follow the game's value
    std::map<std::string, double> edit;
    std::set<std::string> touched;
    // switch files (every 2 s)
    Switches sw;
    double next_switch = -1.0;
    // the undo file
    UndoRecord undo;
    bool have_undo = false, undo_loaded = false;
    std::string undo_err;
    // the profile (every 2 s; rev bumps when the file's text changes)
    tuning::Profile prof;
    bool prof_have = false;
    std::string prof_text, prof_err;
    long long prof_rev = 0;
    double next_profile = -1.0;
    // the locate and the automatic moments
    LocateStatus locate;
    double next_locate = -1.0;
    MomentTracker moments;
    bool view_seen = false;  // the tab was drawn in this game session (the locate runs for it)
    std::string moment_line;
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
    std::string pending_moment;
    bool pending_restore_all = false;  // the job is Restore FC 27 defaults (the profile keeps its values, switched off)
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
std::function<double()> g_wall_clock;  // tests: a clock driven by hand (tuning_set_wall_clock)

// The wall clock of the moments' prematch window: system_clock (the system time, unaffected by the game's QueryPerformanceCounter
// scaling that makes App::now run ~3x wall time in game), unless a test drives it
double wall_seconds() {
    if (g_wall_clock) return g_wall_clock();
    return double(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) / 1000.0;
}

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }
fs::path undo_path(App& app) { return out_dir(app) / undo_file(); }
fs::path profile_path(App& app) { return out_dir(app) / profile_file(); }
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

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : "; ") + x;
    return s;
}

void refresh_switches(App& app, bool now) {
    if (!now && app.now < g.next_switch) return;
    g.sw = read_switches(out_dir(app));
    g.sw.session = app.tuning && app.tuning->session_opt_in();
    g.next_switch = app.now + 2.0;
}

// The profile file again (every 2 s, or now): a changed text is parsed (rev + 1); an absent file = a profile that is off
void refresh_profile(App& app, bool now) {
    if (!now && app.now < g.next_profile) return;
    g.next_profile = app.now + 2.0;
    std::string text;
    {
        std::error_code ec;
        const uintmax_t size = fs::exists(profile_path(app), ec) ? fs::file_size(profile_path(app), ec) : 0;
        if (!ec && size > kMaxProfileBytes) {
            text = "(too big: " + std::to_string(size) + " bytes)";  // not parsed: reported once below, the profile is off
        } else {
            std::ifstream f(profile_path(app), std::ios::binary);
            if (f) {
                std::stringstream ss;
                ss << f.rdbuf();
                text = ss.str();
            }
        }
    }
    if (g.prof_have && text == g.prof_text) return;
    const bool first = !g.prof_have;
    g.prof_have = true;
    g.prof_text = text;
    ++g.prof_rev;
    tuning::Profile p;
    std::string err;
    if (text.empty()) {
        g.prof = tuning::Profile();
        g.prof_err.clear();
    } else if (tuning::Profile::parse(text, p, &err)) {
        g.prof = p;
        g.prof_err.clear();
    } else {
        g.prof = tuning::Profile();  // a file that does not parse is off (nothing applied)
        g.prof_err = err;
        log_line(app, "gameplay tuning: turbo_output\\" + std::string(profile_file()) + " is not used: " + err);
    }
    if (!first) log_line(app, std::string("gameplay tuning: the profile changed (") + (g.prof.on ? "on" : "off") + ", " + g.prof.preset + ", " +
                                  std::to_string(g.prof.values.size()) + " value(s))");
}

bool save_profile_now(App& app, const std::string& why) {
    std::string err;
    if (!save_profile(profile_path(app), g.prof, &err)) {
        app.notify("The tuning profile was not saved: " + err, true);
        log_line(app, "gameplay tuning: turbo_output\\" + std::string(profile_file()) + " not written: " + err);
        return false;
    }
    log_line(app, "gameplay tuning: profile " + why + " (" + (g.prof.on ? "on" : "off") + ", " + g.prof.preset + ", " +
                      std::to_string(g.prof.values.size()) + " value(s))");
    // the rev follows the file (a change in the hub applies it there); the text is what was written, so the next read sees no change
    g.prof_have = true;
    g.prof_text = g.prof.to_json();
    g.prof_err.clear();
    ++g.prof_rev;
    return true;
}

Context ctx_of(App& app) {
    const BridgeState& st = app.bridge.state();
    Context c;
    c.session = st.session;
    c.load_gen = st.load_gen;
    c.phase = st.match_phase;
    c.in_cm = st.loaded && st.in_cm;
    c.prematch_t = st.prematch_t;
    c.save_t = st.save_t;
    c.bridge = st.loaded;
    c.front_end_t = st.loaded ? st.front_end_t : 0;
    c.team_mgmt = st.loaded && st.team_mgmt;
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

const KnobLive* live_of(const std::string& id) {
    auto it = g.live.find(id);
    return it == g.live.end() ? nullptr : &it->second;
}

// The view's state of a knob: PROVEN (a slider), LOCATED (read-only), UNLOCATED (hidden)
KnobState state_of(const Knob& k) { return knob_state(k, live_of(k.id)); }

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

// The touched sliders of proven knobs that differ from the game's value
std::map<std::string, double> wanted(const Registry& reg) {
    std::map<std::string, double> w;
    for (const std::string& id : g.touched) {
        const Knob* k = reg.knob(id);
        auto e = g.edit.find(id);
        auto l = g.live.find(id);
        if (!k || e == g.edit.end() || l == g.live.end() || !l->second.located || state_of(*k) != KnobState::Proven) continue;
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
    const long long now_s = (long long)(std::time(nullptr));
    in.save_age = st.save_t > 0 ? now_s - st.save_t : -1;
    in.prematch_age = st.prematch_t > 0 ? now_s - st.prematch_t : -1;
    in.team_mgmt = st.loaded && st.team_mgmt;
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
        out.push_back(k->label + (w.copy.empty() ? std::string() : " [" + w.copy + "]") + ": " + format_value(*k, w.old_value) + " -> " +
                      format_value(*k, w.new_value) + (k->proven() ? " (proven in " + k->proof.session + ")" : ""));
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
        if (plan.moment.empty()) app.notify(what + ": " + why, true);
        g.last = what + " not queued: " + why;
        return false;
    }
    g.pending = true;
    g.pending_kind = plan.kind;
    g.pending_moment = plan.moment;
    g.pending_restore_all = false;
    g.pending_since = app.now;
    g.last = std::string(plan_kind_name(plan.kind)) + (plan.moment.empty() ? "" : " (" + plan.moment + ")") + " queued for the game thread ...";
    return true;
}

// Apply and the profile's moments: the undo file first (it holds the values before the write before anything is written)
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
    rec.note = plan.moment.empty() ? std::string() : "the tuning profile (" + plan.moment + ")";
    std::string err;
    if (!save_record(app, rec, &err)) {
        if (plan.moment.empty()) app.notify(what + ": turbo_output\\" + std::string(undo_file()) + " could not be written (" + err + ")", true);
        log_line(app, "gameplay tuning: " + what + ": turbo_output\\" + std::string(undo_file()) + " could not be written (" + err + ")");
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
// do not block it, the owner rule holds); a slider edit of the knob is dropped, the knob leaves the profile
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
    // the knobs tied to it by a "below" rule (the line height bands) are reset with it, so a Reset never leaves two bands overlapping
    bool tied = false;
    for (const Knob& o : reg->knobs) {
        if (o.id == id || (std::find(o.below.begin(), o.below.end(), id) == o.below.end() &&
                           std::find(k->below.begin(), k->below.end(), o.id) == k->below.end()))
            continue;
        std::map<std::string, double> od;
        if (!reset_defaults(o, app.tuning->defaults(), od).empty()) continue;
        g.touched.erase(o.id);
        defs.insert(od.begin(), od.end());
        tied = true;
    }
    Plan plan;
    const std::string w = make_restore_plan(*reg, g.live, defs, plan, g.skipped, tied ? std::string() : id);
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

// Restore FC 27 defaults: the profile off first (the automatic moments stop), then FC 27's own values from the knobs file back into
// every copy of every located knob that holds another value (no opt-in; the kill switches do not block it; the owner rule holds)
void do_restore(App& app) {
    if (!app.tuning) return;
    refresh_switches(app, true);
    refresh_profile(app, true);
    if (g.prof.on) {
        g.prof.on = false;
        save_profile_now(app, "switched off by Restore FC 27 defaults");
    }
    read_now(app);
    const auto reg = registry_of(app);
    Plan plan;
    const std::string w = make_vanilla_plan(*reg, g.live, plan, g.skipped);
    if (!w.empty() || plan.writes.empty()) {
        app.notify("FC 27 defaults: " + (w.empty() ? std::string("every located knob already holds FC 27's own value; the profile is off") : w), !w.empty());
        return;
    }
    const Gates gs = evaluate(gate_input(app, *reg, &plan, "", "", true));
    if (!gs.pass()) {
        app.notify("FC 27 defaults not restored: " + gs.first(), true);
        return;
    }
    if (queue(app, plan, "FC 27 defaults not restored")) g.pending_restore_all = true;
}

void handle_result(App& app, const ApplyResult& r) {
    g.pending = false;
    struct Clear {
        ~Clear() { g.pending_restore_all = false; }
    } clear_after;
    g.last = r.log_line.empty() ? r.message : r.log_line;
    const BridgeState& st = app.bridge.state();
    if (!r.moment.empty()) g.moment_line = (r.lines.empty() ? g.last : r.lines.front()) + " (" + stamp() + ")";
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
                    u.note = (r.kind == PlanKind::Restore ? "Restore FC 27 defaults" : "") +
                             std::string(g.skipped.empty() ? "" : (r.kind == PlanKind::Restore ? "; " : "") + std::string("partly: ") + joined(g.skipped));
                } else {
                    u.knobs = left;
                    u.note = "Restore put back " + std::to_string(r.writes.size()) + " knob(s)";
                }
                save_record(app, u);
            }
            // the knobs put back by Undo or a row's Reset leave the profile (it no longer writes them again); Restore FC 27 defaults
            // switched the profile off and keeps its values (switching it on again writes them again)
            bool changed = false;
            if (!g.pending_restore_all)
                for (const Write& w : r.writes) changed = g.prof.values.erase(w.id) > 0 || changed;
            if (changed) {
                g.prof.preset = "custom";
                save_profile_now(app, "without the knobs put back");
            }
            after_job_follow(r.writes);
            app.notify(std::string(r.kind == PlanKind::Undo ? "Gameplay tuning put back to the values before Turbo's write" : "FC 27's own values restored") +
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
    // Apply / the profile's moments
    if (r.ok && r.written) {
        // the record holds the values Apply was pressed for, which the job found in place before writing (a merged record keeps the
        // values from before Turbo's first write in this career load)
        UndoRecord u = g.undo;
        u.state = "written";
        save_record(app, u);
        if (r.kind == PlanKind::Apply) {
            // what was applied from the view is the profile's value from now on (written again at the next moments when the profile is on)
            for (const Write& w : r.writes) g.prof.values[w.id] = w.new_value;
            g.prof.preset = "custom";
            save_profile_now(app, "updated by Apply");
            app.notify("Gameplay tuning written to the engine table and read back");
        }
        after_job_follow(r.writes);
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
        if (r.kind == PlanKind::Apply) app.notify("Gameplay tuning not written: " + r.message, true);
    }
    read_now(app);
}

// The profile's automatic moments (core/tuning.h MomentTracker): boot, career_load, hub; one job each
void moments_tick(App& app) {
    // M3 (review 2026-10-13): the locate (a heap pass, the first run's name search) runs only for a user who opted in: the profile on, the
    // tab opened in this session, or the session tick; never under a kill switch (a user without a profile pays nothing at launch)
    const bool opted = (g.prof.on && g.sw.blocking_moments().empty()) || g.view_seen || app.tuning->session_opt_in();
    if (opted && g.sw.blocking().empty() && app.now >= g.next_locate) {
        g.next_locate = app.now + 1.0;
        g.locate = app.tuning->locate_status();
    }
    const BridgeState& st = app.bridge.state();
    MomentInput in;
    in.now = wall_seconds();  // a real 5 s window (App::now runs faster than wall time in game)
    in.wall = (long long)(std::time(nullptr));
    in.bridge = st.loaded;
    in.in_cm = st.loaded && st.in_cm;
    in.phase = st.match_phase;
    in.session = st.session;
    in.load_gen = st.load_gen;
    in.prematch_t = st.prematch_t;
    in.front_end_t = st.loaded ? st.front_end_t : 0;
    in.team_mgmt = st.loaded && st.team_mgmt;
    in.profile_on = g.prof.on && g.sw.blocking_moments().empty();
    in.profile_rev = g.prof_rev;
    in.ready = g.locate.ready && app.tuning->registry_status().empty();
    in.busy = g.pending || app.tuning->busy();
    const Moment m = g.moments.next(in);
    if (m == Moment::None) return;
    const std::string mn = moment_name(m);
    g.moments.fired(m, in);
    load_undo_once(app);
    read_now(app);
    const auto reg = registry_of(app);
    Plan plan;
    std::vector<std::string> skipped;
    const std::string w = make_profile_plan(*reg, g.live, g.prof, mn, plan, skipped);
    if (!w.empty() || plan.writes.empty()) {
        g.moment_line = "tuning: profile (" + mn + "): nothing to apply" + (w.empty() ? std::string() : ": " + w) +
                        (skipped.empty() ? std::string() : " (" + joined(skipped) + ")");
        log_line(app, g.moment_line);
        return;
    }
    if (!skipped.empty()) log_line(app, "tuning: profile (" + mn + "): skipped " + joined(skipped));
    if (!submit_write(app, plan, "Tuning profile (" + mn + ")")) {
        g.moment_line = "tuning: profile not applied (" + mn + "): " + g.last;
        log_line(app, g.moment_line);
    }
}

// ---------------------------------------------------------------------------------------------------------------- presets
// A preset sets the sliders of the proven knobs and the profile's values (Turbo's own presets name only FC 27's values and relative
// changes from them); the knobs not proven yet are listed, not set. Nothing is written until Apply or the next automatic moment
void use_preset(App& app, const Preset& p) {
    const auto reg = registry_of(app);
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    auto usable = [](const Knob& k) -> std::string {
        if (state_of(k) == KnobState::Unlocated) return "not located on this build";
        if (!k.proven()) return "located, its effect not proven yet";
        return "";
    };
    const PresetValues pv = preset_values(p, *reg, [&](const std::string& id, double& v) { return base_of(app, defs, id, v); }, usable);
    for (const auto& kv : pv.values) {
        g.edit[kv.first] = kv.second;
        g.touched.insert(kv.first);
    }
    if (!pv.values.empty() || p.fc27) {
        g.prof.values = pv.values;
        g.prof.preset = p.name;
        save_profile_now(app, "set from the preset " + p.name);
    }
    g.preset_line = "\"" + p.name + "\": " + std::to_string(pv.values.size()) + " knob(s) set in the sliders and the profile" +
                    (pv.skipped.empty() ? "" : ", " + std::to_string(pv.skipped.size()) + " not set (" + joined(pv.skipped) + ")") +
                    ". Written by Apply, or by the profile at its next moment when it is on.";
}

void save_preset(App& app) {
    const auto reg = registry_of(app);
    const std::string name = g.preset_name;
    if (name.empty()) {
        app.notify("Type a name for the preset first", true);
        return;
    }
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    std::map<std::string, double> vals = g.prof.values;
    for (const std::string& id : g.touched)
        if (g.edit.count(id)) vals[id] = g.edit[id];
    Preset p = preset_from_values(name, *reg, vals, [&](const std::string& id, double& v) { return base_of(app, defs, id, v); });
    if (p.changes.empty()) {
        app.notify("Nothing to save: the profile and the sliders hold FC 27's own values", true);
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
    ImGui::BeginDisabled(!have || g.pending);
    if (ImGui::Button("Use this preset##tvuse") && have) use_preset(app, g.presets[size_t(g.preset_sel)]);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sets the sliders and the tuning profile from the preset (relative changes start from FC 27's own values). Only the knobs "
                          "proven in a played match are set. Nothing is written until Apply, or the profile's next moment when it is on.");
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
    form_label("Save the profile as", "A preset of your own in turbo_output\\tuning_presets: each knob of the profile (and each moved slider) as a change "
                                      "relative to FC 27's own value");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputText("##tvname", g.preset_name, sizeof(g.preset_name));
    ImGui::SameLine();
    if (ImGui::Button("Save preset##tvsave")) save_preset(app);
    for (const std::string& e : g.preset_errors) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Preset not read: %s", e.c_str());
    if (!g.preset_line.empty()) ImGui::TextWrapped("%s", g.preset_line.c_str());
    g_state.preset_line = g.preset_line;
}

// ---------------------------------------------------------------------------------------------------------------- drawing
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
        ImGui::TextUnformatted("They also become your tuning profile's values. The values before the write go to turbo_output\\tuning_undo.json;");
        ImGui::TextUnformatted("Restore FC 27 defaults puts FC 27's own values back.");
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
    const KnobState st = state_of(k);
    const bool proven = st == KnobState::Proven;
    row.state = knob_state_name(st);
    row.located = "located";
    row.applies = proven ? "proven in " + k.proof.session : "located, effect not proven";
    // FC 27's own value (of the first copy: the knobs file, else the value before Turbo's first write), the value in memory now
    double def = 0;
    const std::string first = !l->second.copies.empty() ? l->second.copies.front().variant : std::string();
    const bool have_def = copy_default(k, defs, first, def);
    row.game = format_value(k, l->second.value);
    if (have_def) row.def = format_value(k, def);
    auto pv = g.prof.values.find(k.id);
    if (pv != g.prof.values.end()) row.profile = format_value(k, pv->second);
    ImGui::PushID(k.id.c_str());
    // the label (tooltip: the table field, the notes, the proof)
    std::string tip = k.collection.empty() ? std::string("table field not known yet") : k.collection + " / " + k.field;
    if (!k.notes.empty()) tip += "\n" + k.notes;
    tip += proven ? "\nProven in a played match: " + k.proof.session + " (" + k.proof.evidence + ")"
                  : std::string("\nLocated and checked in the running game; its effect on a match has not been measured yet, so Turbo shows it read-only.");
    form_label(k.label.c_str(), tip.c_str());
    row.editable = proven && !g.pending;
    if (proven) {
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
        ImGui::TextDisabled("%s", row.game.c_str());
    }
    // the badge
    ImGui::SameLine();
    ImGui::TextColored(proven ? ImVec4(0.45f, 0.85f, 0.45f, 1) : ImVec4(0.75f, 0.75f, 0.55f, 1), "[%s]", row.applies.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
    // the game's own value, the value now, the profile's, Reset
    ImGui::SameLine();
    ImGui::TextDisabled("FC 27: %s | now: %s%s", have_def ? row.def.c_str() : "?", row.game.c_str(), row.profile.empty() ? "" : (" | profile: " + row.profile).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27: the game's own value (Restore FC 27 defaults puts it back). now: what the game holds in memory at this moment. "
                          "profile: the value your tuning profile writes at its moments.");
    if (l->second.copies.size() > 1) {
        // one knob, several database copies (base, simulation = Authentic, ...): every copy is written together
        std::string cs;
        for (const CopyAt& c : l->second.copies) cs += (cs.empty() ? "" : ", ") + c.variant + " " + format_value(k, c.value, false);
        row.copies = cs;
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu copies: %s)", l->second.copies.size(), cs.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The game keeps this table once per gameplay database (base, simulation = Authentic). Turbo writes every copy together, or none.");
    }
    {
        std::map<std::string, double> rd;
        const std::string none = reset_defaults(k, defs, rd);
        row.can_reset = (none.empty() || g.touched.count(k.id)) && !g.pending && l->second.writable;
        ImGui::SameLine();
        ImGui::BeginDisabled(!row.can_reset);
        if (ImGui::SmallButton(("Reset##tvreset_" + k.id).c_str())) do_reset(app, k.id);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", none.empty() ? ("Puts the game's own value (" + row.def + ") back now, in every copy, and takes the knob out of the profile.").c_str()
                                    : g.touched.count(k.id) ? "Drops your slider edit (the knob already holds the game's own value)."
                                                            : ("Nothing to reset: " + none + ".").c_str());
    }
    ImGui::PopID();
    g_state.rows.push_back(row);
}

void draw_body(App& app) {
    g_state.drawn = true;
    g.view_seen = true;  // the tab was opened in this session: it locates (M3)
    refresh_switches(app, false);
    refresh_profile(app, false);
    load_undo_once(app);
    if (!g.presets_loaded) load_presets_now(app);
    if (app.now >= g.next_read) {
        g.next_read = app.now + 1.0;
        read_now(app);
    }
    const auto reg = registry_of(app);
    const std::string reg_why = app.tuning ? app.tuning->registry_status() : "this Turbo has no gameplay tuning service";
    g_state.opt_in = g.sw.opted_in();
    g_state.busy = g.pending;

    ImGui::TextWrapped(
        "The game engine's gameplay tuning tables: how teams hold their shape, press, mark and make runs in every match (every club, both teams).");
    ImGui::TextWrapped(
        "The game keeps these tables in memory only (not in the career save). Your tuning profile writes its values again at the main menu, at every "
        "career load and each time you enter the hub, while it is on.");
    g_state.session_note = true;
    if (reg_why.empty()) {
        const int ph = reg->placeholders();
        g_state.registry_line = "turbo\\tuning\\" + fs::path(reg->source).filename().string() + ": " + std::to_string(reg->knobs.size()) + " knobs (" +
                                std::to_string(ph) + " not confirmed) for game build " + reg->build;
        ImGui::TextDisabled("%s", g_state.registry_line.c_str());
    } else {
        g_state.registry_line = reg_why;
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", reg_why.c_str());
    }
    // how the tables were located
    {
        const LocateStatus& ls = g.locate;
        if (!reg_why.empty()) g_state.locate_line.clear();
        else if (!ls.ready) g_state.locate_line = "Locating the tables ... (" + ls.why + ")";
        else if (ls.path == "chain") g_state.locate_line = "Located through the game's attribute manager: " + std::to_string(ls.groups) + " group(s), " + std::to_string(ls.knobs) + " knob(s)";
        else if (ls.path == "search") g_state.locate_line = "Located by searching the game's memory: " + std::to_string(ls.groups) + " group(s), " + std::to_string(ls.knobs) + " knob(s)";
        else g_state.locate_line = std::to_string(ls.groups) + " locator(s) resolved";
        if (!g_state.locate_line.empty()) ImGui::TextDisabled("%s", g_state.locate_line.c_str());
    }
    g_state.anchors_line = app.tuning ? app.tuning->anchors_status() : "";
    if (!g_state.anchors_line.empty() && reg_why.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("Locators: %s", g_state.anchors_line.c_str());
    if (app.tuning && ImGui::SmallButton("Read the knobs file again##tvreload")) {
        std::string why;
        app.tuning->reload(why);
        g.next_read = 0.0;
        g.next_locate = 0.0;
        app.notify(why.empty() ? "Knobs file read again" : "Knobs file: " + why, !why.empty());
    }

    // the knobs' states
    for (const Knob& k : reg->knobs) {
        const KnobState s = state_of(k);
        if (s == KnobState::Proven) ++g_state.proven;
        else if (s == KnobState::Located) ++g_state.located;
        else ++g_state.unlocated;
    }
    if (reg_why.empty())
        ImGui::TextWrapped("%d proven in a played match (sliders), %d located and checked but their effect not proven yet (read-only), %d not yet working on "
                           "this build (listed at the end).",
                           g_state.proven, g_state.located, g_state.unlocated);

    // the tuning profile
    ImGui::SeparatorText("Your tuning profile");
    bool pon = g.prof.on;
    if (checkbox_right("Apply my tuning profile automatically (main menu, every career load, the hub)##tvprof", &pon)) {
        g.prof.on = pon;
        save_profile_now(app, pon ? "switched on" : "switched off");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off by default. On: turbo_output\\tuning_profile.json is applied at the main menu, at every career load and each time you enter the hub "
                          "(never while a match is set up or played). turbo_output\\tuning_profile_off.txt stops these automatic writes; tuning_off.txt, "
                          "call_attrib_off.txt and tactics_off.txt stop every write.");
    g_state.profile_on = g.prof.on;
    {
        std::string vals;
        for (const auto& kv : g.prof.values) {
            const Knob* k = reg->knob(kv.first);
            vals += (vals.empty() ? "" : ", ") + (k ? k->label + " " + format_value(*k, kv.second) : kv.first + " " + std::to_string(kv.second));
        }
        g_state.profile_line = std::string("Profile: ") + (g.prof.on ? "on" : "off") + ", " + g.prof.preset + ", " + std::to_string(g.prof.values.size()) +
                               " value(s)" + (vals.empty() ? std::string() : ": " + vals);
        if (!g.prof_err.empty()) g_state.profile_line += " (turbo_output\\" + std::string(profile_file()) + " is not used: " + g.prof_err + ")";
        if (g.sw.profile_off) g_state.profile_line += " (turbo_output\\" + std::string(profile_off_file()) + ": automatic writes off)";
        ImGui::TextWrapped("%s", g_state.profile_line.c_str());
        g_state.moment_line = g.moment_line;
        if (!g.moment_line.empty()) ImGui::TextDisabled("Last: %s", g.moment_line.c_str());
    }
    bool tick = app.tuning && app.tuning->session_opt_in();
    if (checkbox_right("Write in this game session (Apply, Reset)##tvsess", &tick) && app.tuning) {
        app.tuning->set_session_opt_in(tick);
        refresh_switches(app, true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lets Apply and a row's Reset write the tables in this game session (off at every start). The profile being on allows them too.");
    g_state.session_tick = tick;
    g_state.opt_in = g.sw.opted_in();

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
        std::string w = app.tuning ? make_vanilla_plan(*reg, g.live, rp, sk) : "no service";
        if (w.empty() && rp.writes.empty()) w = "every located knob holds FC 27's own value";
        if (w.empty()) {
            const Gates rg = evaluate(gate_input(app, *reg, &rp, "", "", true));
            if (!rg.pass()) w = rg.first();
        }
        g_state.restore_why = w;
        // switching the profile off is always possible (even when every knob holds FC 27's value)
        g_state.can_restore = (w.empty() || (g.prof.on && w == "every located knob holds FC 27's own value")) && !g.pending && app.tuning != nullptr;
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
        ImGui::SetTooltip("%s", g_state.can_apply ? "Writes every moved knob in one go on the game thread, reads them back, puts every knob back on a mismatch; "
                                                    "the values become your profile's."
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
    if (ImGui::Button("Restore FC 27 defaults##tvrestore")) do_restore(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", g_state.can_restore ? "Switches the profile off and writes FC 27's own values (the knobs file) back into every located knob."
                                                    : g_state.restore_why.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Discard edits##tvdiscard")) {
        g.touched.clear();
        read_now(app);
    }
    draw_confirm(app);

    draw_presets(app);

    // the knobs, one group each: proven and located rows; the unlocated ones are listed at the end
    g_state.groups.clear();
    for (const std::string& grp : reg->groups()) {
        bool any = false;
        for (const Knob& k : reg->knobs) any = any || (k.group == grp && state_of(k) != KnobState::Unlocated);
        if (!any) continue;
        g_state.groups.push_back(grp);
        if (!ImGui::CollapsingHeader((grp + "##tvg").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const Knob& k : reg->knobs)
                if (k.group == grp && state_of(k) != KnobState::Unlocated) {
                    TuningViewState::Row r;
                    r.id = k.id;
                    r.label = k.label;
                    r.group = grp;
                    r.state = knob_state_name(state_of(k));
                    g_state.rows.push_back(r);  // listed (collapsed)
                }
            continue;
        }
        form_label_width(S(260.0f));
        for (const Knob& k : reg->knobs)
            if (k.group == grp && state_of(k) != KnobState::Unlocated) draw_row(app, k, defs);
        form_label_width(0.0f);
    }
    for (const Knob& k : reg->knobs)
        if (state_of(k) == KnobState::Unlocated) g_state.not_working.push_back(k.label);
    if (!g_state.not_working.empty()) {
        if (ImGui::CollapsingHeader(("Not yet working on this build (" + std::to_string(g_state.not_working.size()) + ")##tvnotyet").c_str())) {
            ImGui::TextWrapped("These knobs are not located in the running game yet, so Turbo shows and writes nothing for them: %s.", joined(g_state.not_working).c_str());
            for (const Knob& k : reg->knobs) {
                if (state_of(k) != KnobState::Unlocated) continue;
                const KnobLive* l = live_of(k.id);
                ImGui::BulletText("%s", k.label.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", l && !l->why.empty() ? l->why.c_str() : "not read yet");
            }
        }
    }
    if (reg->knobs.empty()) ImGui::TextDisabled("No knobs: %s", reg_why.empty() ? "the knobs file lists none" : reg_why.c_str());

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

void tuning_set_wall_clock(std::function<double()> clock) { g_wall_clock = std::move(clock); }

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
        refresh_profile(app, false);
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
        moments_tick(app);
    } catch (const std::exception& e) {
        g.error = e.what();
    } catch (...) {
        g.error = "unknown error";
    }
}

}  // namespace turbo
