// FC 27 LE Turbo GUI - Competitions > Gameplay tuning (see ui_tuning.h, core/tuning.h).
//
// Frame-loop rules as in the other tactics views: never throws (an inline error instead); file I/O only on a click, at the first look
// (the undo file, Turbo's own presets) and every 2 s (the switch files, the profile file); the knobs are read at most once a second while
// the view is drawn, and when an automatic moment is due; the locate is advanced once a second by the tick.
#include "ui_tuning.h"

#include <algorithm>
#include <cctype>
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
#include "ui_shell.h"
#include "ui_tactics_pages.h"

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
    char rename_buf[64] = "";
    bool rename_request = false, delete_request = false;
    // the knob list's search and filter (2.5)
    char search[64] = "";
    bool only_changed = false;
    // the job in flight
    bool pending = false;
    PlanKind pending_kind = PlanKind::Apply;
    std::string pending_moment;
    bool pending_restore_all = false;  // the job is Restore FC 27 defaults (the profile keeps its values, switched off)
    bool pending_live = false;         // 2.7: the job is "Apply now (live)"
    std::string styles_sig;            // 2.7: which knobs are proven when the Turbo styles were built (rebuilt when it changes)
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

// The same file on a case-insensitive file system too (Windows): "Turbo_compact.json" is Turbo's "turbo_compact.json"
bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::equivalent(a, b, ec) && !ec) return true;
    std::string x = a.filename().string(), y = b.filename().string();
    for (char& c : x) c = char(std::tolower((unsigned char)c));
    for (char& c : y) c = char(std::tolower((unsigned char)c));
    return x == y && a.parent_path() == b.parent_path();
}

std::shared_ptr<const Registry> registry_of(App& app) {
    if (!app.tuning) return std::make_shared<Registry>();
    return app.tuning->registry();
}

// 2.7: the proven knobs of the knobs file, as one string (the Turbo styles are rebuilt when it changes)
std::string proven_signature(const Registry& reg) {
    std::string s;
    for (const Knob& k : reg.knobs)
        if (k.proven() && k.slider) s += k.id + ";";
    return s;
}

void load_presets_now(App& app) {
    std::string err;
    const int n = ensure_builtin_presets(preset_dir(app), &err);
    if (n > 0) log_line(app, "gameplay tuning: " + std::to_string(n) + " of Turbo's own presets written to turbo_output\\" + presets_dir());
    g.preset_errors.clear();
    std::string keep;
    if (g.preset_sel >= 0 && g.preset_sel < int(g.presets.size())) keep = g.presets[size_t(g.preset_sel)].name;
    g.presets = load_presets(preset_dir(app), &g.preset_errors);
    // 2.7 "Turbo mixed styles": in memory, after Turbo's own file presets, holding only the proven knobs of the knobs file
    const auto reg = registry_of(app);
    std::vector<Preset> styles = style_presets(*reg);
    size_t at = 0;
    while (at < g.presets.size() && g.presets[at].turbo()) ++at;
    g.presets.insert(g.presets.begin() + std::ptrdiff_t(at), styles.begin(), styles.end());
    g.styles_sig = proven_signature(*reg);
    g.preset_sel = -1;
    if (!keep.empty())
        for (size_t i = 0; i < g.presets.size(); ++i)
            if (same_name(g.presets[i].name, keep)) g.preset_sel = int(i);
    g.presets_loaded = true;
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

GateInput gate_input(App& app, const Registry& reg, const Plan* plan, const std::string& locate, const std::string& values, bool restore,
                     bool live_apply = false) {
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
    in.live_apply = live_apply;
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
    g.pending_live = plan.live;
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
    rec.boot = plan.moment == "boot";
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

std::string plain_gate(const std::string& s);  // below

// 2.7: the plan of "Apply now (live)": the moved sliders of the knobs that are proven live, all of them or just `only`. "" = a plan that passes
// every gate (G1 switches incl. tuning_live_off.txt, G2 phase, the plan, G4 / G5), else why not in plain words
std::string live_plan(App& app, const Registry& reg, const std::string& only, Plan& out) {
    std::map<std::string, double> w = wanted(reg);
    for (auto it = w.begin(); it != w.end();) {
        const Knob* k = reg.knob(it->first);
        if (!k || !k->live_ok() || (!only.empty() && it->first != only)) it = w.erase(it);
        else ++it;
    }
    if (w.empty()) return "no moved slider of a knob that is proven live";
    const std::string bad = make_plan(reg, g.live, w, PlanKind::Apply, out);
    if (!bad.empty()) return bad;
    if (out.writes.empty()) return "nothing changed";
    out.live = true;
    if (g.pending) return "a write is still running";
    const Gates gs = evaluate(gate_input(app, reg, &out, "", "", false, true));
    if (!gs.pass()) return plain_gate(gs.first());
    return "";
}

void do_apply_live(App& app, const std::string& only) {
    if (!app.tuning) return;
    refresh_switches(app, true);
    load_undo_once(app);
    read_now(app);
    const auto reg = registry_of(app);
    Plan plan;
    const std::string why = live_plan(app, *reg, only, plan);
    if (!why.empty()) {
        app.notify("Gameplay tuning not written live: " + why, true);
        g.last = "live write not made: " + why;
        return;
    }
    submit_write(app, plan, "Gameplay tuning not written live");
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
    const bool was_live = g.pending_live;
    g.pending_live = false;
    struct Clear {
        ~Clear() { g.pending_restore_all = false; }
    } clear_after;
    g.last = r.log_line.empty() ? r.message : r.log_line;
    const BridgeState& st = app.bridge.state();
    if (!r.moment.empty()) {
        g.moment_line = (r.lines.empty() ? g.last : r.lines.front()) + " (" + stamp() + ")";
        // m3: a moment whose job did not run is due again
        if (!r.ok && (r.stage == "late" || r.stage == "cancelled" || r.stage == "G1")) {
            for (Moment m : {Moment::Boot, Moment::CareerLoad, Moment::Hub})
                if (r.moment == moment_name(m)) g.moments.rearm(m);
        }
    }
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
            app.notify(was_live ? "Gameplay tuning written LIVE into the engine table and read back" : "Gameplay tuning written to the engine table and read back");
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
        if (r.kind == PlanKind::Apply) app.notify(std::string(was_live ? "Gameplay tuning not written live: " : "Gameplay tuning not written: ") + r.message, true);
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
    load_undo_once(app);
    read_now(app);
    const auto reg = registry_of(app);
    Plan plan;
    std::vector<std::string> skipped;
    const std::string w = make_profile_plan(*reg, g.live, g.prof, mn, plan, skipped, g.sw.test_unproven);
    // m3 (review 2026-10-13): marked done once queued (or when there is nothing to write); a job that does not run (late, cancelled, a
    // switch that appeared) arms the moment again (handle_result)
    g.moments.fired(m, in);
    if (!w.empty() || plan.writes.empty()) {
        g.moment_line = "tuning: profile (" + mn + "): nothing to apply" + (w.empty() ? std::string() : ": " + w) +
                        (skipped.empty() ? std::string() : " (" + joined(skipped) + ")");
        log_line(app, g.moment_line);
        return;
    }
    if (!skipped.empty()) log_line(app, "tuning: profile (" + mn + "): skipped " + joined(skipped));
    std::string unproven;
    for (const Write& wr : plan.writes)
        if (const Knob* k = reg->knob(wr.id))
            if (!k->proven() && unproven.find(k->label) == std::string::npos) unproven += (unproven.empty() ? "" : ", ") + k->label;
    if (!unproven.empty())
        log_line(app, "tuning: TEST MODE (turbo_output\\" + std::string(test_unproven_file()) + "): the profile writes knobs whose effect is NOT proven: " +
                          unproven);
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
                    (pv.clamped.empty() ? "" : "; brought into the knob's range: " + joined(pv.clamped)) +
                    ". Written by Apply, or by the profile at its next moment when it is on.";
}

void select_preset_named(const std::string& name) {
    g.preset_sel = -1;
    for (size_t i = 0; i < g.presets.size(); ++i)
        if (same_name(g.presets[i].name, name)) g.preset_sel = int(i);
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
        if (q.turbo() && (same_name(q.name, p.name) || same_file(q.file, f))) {
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
    // what was saved is the profile's preset from now on (the active preset)
    if (g.prof.preset != p.name) {
        g.prof.preset = p.name;
        save_profile_now(app, "preset name set by Save");
    }
    load_presets_now(app);
    select_preset_named(p.name);
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
        if (q.turbo() && (same_name(q.name, p.name) || same_file(q.file, to))) {
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

void duplicate_preset(App& app) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    Preset p = g.presets[size_t(g.preset_sel)];
    const std::string source = p.name;
    p.name = unique_preset_name(g.presets, p.name + " copy");
    p.id.clear();      // a new id from the new name
    p.author.clear();  // the copy is yours, also when the source is Turbo's own
    p.note = "Copy of \"" + source + "\"";
    std::string err;
    if (!write_preset_file(preset_dir(app), p, &err)) {
        app.notify("The preset could not be copied: " + err, true);
        return;
    }
    g.preset_line = "Copied to \"" + p.name + "\"";
    log_line(app, "gameplay tuning: " + g.preset_line);
    load_presets_now(app);
    select_preset_named(p.name);
}

void rename_preset(App& app, const std::string& wanted) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    Preset p = g.presets[size_t(g.preset_sel)];
    if (p.turbo()) {
        app.notify("Turbo's own presets cannot be renamed: duplicate one first", true);
        return;
    }
    std::string name = wanted;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) {
        app.notify("Type a name for the preset", true);
        return;
    }
    if (name == p.name) return;
    std::vector<Preset> others;
    for (size_t i = 0; i < g.presets.size(); ++i)
        if (int(i) != g.preset_sel) others.push_back(g.presets[i]);
    if (unique_preset_name(others, name) != name) {
        app.notify("A preset with that name exists: pick another name", true);
        return;
    }
    const Preset old = p;
    p.name = name;
    p.id.clear();
    std::string err;
    if (!write_preset_file(preset_dir(app), p, &err)) {
        app.notify("The preset was not renamed: " + err, true);
        return;
    }
    if (old.file != p.file && !delete_preset_file(old, &err)) log_line(app, "gameplay tuning: the old preset file was not removed: " + err);
    if (same_name(g.prof.preset, old.name)) {
        g.prof.preset = p.name;
        save_profile_now(app, "preset renamed");
    }
    g.preset_line = "Renamed \"" + old.name + "\" to \"" + p.name + "\"";
    log_line(app, "gameplay tuning: " + g.preset_line);
    load_presets_now(app);
    select_preset_named(p.name);
}

void delete_preset(App& app) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    const Preset p = g.presets[size_t(g.preset_sel)];
    std::string err;
    if (!delete_preset_file(p, &err)) {
        app.notify("The preset was not deleted: " + err, true);
        return;
    }
    if (same_name(g.prof.preset, p.name)) {
        g.prof.preset = "custom";  // the values stay in the profile; only the name goes
        save_profile_now(app, "preset deleted");
    }
    g.preset_line = "Deleted \"" + p.name + "\"";
    log_line(app, "gameplay tuning: " + g.preset_line);
    g.preset_sel = -1;
    load_presets_now(app);
}

bool located_now(const std::string& id) {
    auto l = g.live.find(id);
    return l != g.live.end() && l->second.located;
}

// The active preset: the profile's preset name, and whether the values now (the profile's, with the moved sliders) still equal what the preset gives
struct ActivePreset {
    std::string name;    // "" = none (custom values)
    bool known = false;  // the preset's file is there
    bool modified = false;
    std::string status;  // the preset's proof status text
};
ActivePreset active_preset(App& app, const Registry& reg) {
    ActivePreset a;
    if (g.prof.preset.empty() || g.prof.preset == "custom") return a;
    a.name = g.prof.preset;
    const Preset* found = nullptr;
    for (const Preset& p : g.presets)
        if (same_name(p.name, a.name)) found = &p;
    if (!found) return a;
    a.known = true;
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    auto usable = [](const Knob& k) -> std::string {
        if (state_of(k) == KnobState::Unlocated) return "not located on this build";
        if (!k.proven()) return "located, its effect not proven yet";
        return "";
    };
    const PresetValues pv = preset_values(*found, reg, [&](const std::string& id, double& v) { return base_of(app, defs, id, v); }, usable);
    std::map<std::string, double> now = g.prof.values;
    for (const std::string& id : g.touched)
        if (g.edit.count(id)) now[id] = g.edit[id];
    for (const auto& kv : pv.values) {
        auto it = now.find(kv.first);
        const Knob* k = reg.knob(kv.first);
        if (it == now.end() || !k || !same_value(k->type, representable(*k, it->second), kv.second)) a.modified = true;
    }
    for (const auto& kv : now)
        if (!pv.values.count(kv.first)) a.modified = true;
    a.status = preset_status(*found, reg, located_now).text();
    return a;
}

void draw_presets(App& app, const Registry& reg) {
    ImGui::SeparatorText("Presets");
    ImGui::TextDisabled("A preset is a named, complete set of knob values. Use one to fill the sliders and your profile; Apply (or the profile) writes it.");
    if (g.presets_loaded && g.styles_sig != proven_signature(reg)) load_presets_now(app);  // 2.7: a knob was promoted (or the file read again)
    g_state.presets_empty.clear();
    for (const Preset& p : g.presets) {
        const bool empty_style = is_style_preset(p) && p.changes.empty();
        if (empty_style) g_state.presets_empty.push_back(p.name);
        g_state.presets.push_back(p.name + (p.turbo() ? " (Turbo)" : "") + (empty_style ? " - empty until its knobs are proven" : ""));
    }
    const std::string cur = g.preset_sel >= 0 && g.preset_sel < int(g.presets.size()) ? g_state.presets[size_t(g.preset_sel)] : std::string("(pick a preset)");
    ImGui::SetNextItemWidth(S(260.0f));
    if (ImGui::BeginCombo("##tvpreset", cur.c_str(), ImGuiComboFlags_HeightLarge)) {  // 2.7: the Turbo styles make the list long
        for (size_t i = 0; i < g.presets.size(); ++i) {
            const bool grey = is_style_preset(g.presets[i]) && g.presets[i].changes.empty();
            if (grey) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable((g_state.presets[i] + "##tvp" + std::to_string(i)).c_str(), int(i) == g.preset_sel)) g.preset_sel = int(i);
            if (grey) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                const PresetStatus ps = preset_status(g.presets[i], reg, located_now);
                ImGui::SetTooltip("%s%s%s", g.presets[i].note.c_str(), g.presets[i].note.empty() ? "" : "\n", ps.text().c_str());
            }
        }
        ImGui::EndCombo();
    }
    const bool have = g.preset_sel >= 0 && g.preset_sel < int(g.presets.size());
    const bool mine = have && !g.presets[size_t(g.preset_sel)].turbo();
    const bool empty_pick = have && is_style_preset(g.presets[size_t(g.preset_sel)]) && g.presets[size_t(g.preset_sel)].changes.empty();
    ImGui::SameLine();
    ImGui::BeginDisabled(!have || g.pending || empty_pick);
    if (ImGui::Button("Use this preset##tvuse") && have && !empty_pick) use_preset(app, g.presets[size_t(g.preset_sel)]);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", empty_pick ? "This Turbo style holds only knobs that are proven in a played match, and none of its knobs is proven yet: nothing to use."
                                           : "Sets the sliders and the tuning profile from the preset (relative changes start from FC 27's own values). Only the knobs "
                                             "with a measured effect are set. Nothing is written until Apply, or the profile's next moment when it is on.");
    g_state.preset_status.clear();
    if (have) {
        g_state.preset_status = preset_status(g.presets[size_t(g.preset_sel)], reg, located_now).text();
        ImGui::TextDisabled("%s: %s", g.presets[size_t(g.preset_sel)].name.c_str(), g_state.preset_status.c_str());
    }
    // manage
    ImGui::BeginDisabled(!have);
    if (ImGui::SmallButton("Duplicate##tvdup")) duplicate_preset(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!mine);
    if (ImGui::SmallButton("Rename...##tvren")) {
        std::snprintf(g.rename_buf, sizeof(g.rename_buf), "%s", have ? g.presets[size_t(g.preset_sel)].name.c_str() : "");
        g.rename_request = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete...##tvdel")) g.delete_request = true;
    ImGui::EndDisabled();
    if (have && !mine && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Turbo's own presets cannot be renamed or deleted: duplicate one first.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::SmallButton("Export...##tvexp")) {
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
    if (ImGui::SmallButton("Import...##tvimp")) {
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
    // the two small dialogs
    if (g.rename_request) {
        ImGui::OpenPopup("Rename preset##tvrenamepop");
        g.rename_request = false;
    }
    if (ImGui::BeginPopupModal("Rename preset##tvrenamepop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(S(240.0f));
        ImGui::InputText("##tvrenamebox", g.rename_buf, sizeof(g.rename_buf));
        if (ImGui::Button("Rename##tvrenameok")) {
            rename_preset(app, g.rename_buf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##tvrenameno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g.delete_request) {
        ImGui::OpenPopup("Delete preset?##tvdeletepop");
        g.delete_request = false;
    }
    if (ImGui::BeginPopupModal("Delete preset?##tvdeletepop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"? The file is removed; your profile keeps its values.", have ? g.presets[size_t(g.preset_sel)].name.c_str() : "?");
        if (ImGui::Button("Delete##tvdeleteok")) {
            delete_preset(app);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep it##tvdeleteno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    form_label("Save the profile as", "A preset of your own in turbo_output\\tuning_presets: each knob of the profile (and each moved slider) as a change "
                                      "relative to FC 27's own value. The same name replaces your earlier preset of that name.");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputText("##tvname", g.preset_name, sizeof(g.preset_name));
    ImGui::SameLine();
    if (ImGui::Button("Save preset##tvsave")) save_preset(app);
    for (const std::string& e : g.preset_errors) ImGui::TextDisabled("A preset file was skipped: %s", e.c_str());
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
        ImGui::TextUnformatted("Turbo writes these values into the game's tuning tables (FC 27 reads them for every club; their effect on a match is");
        ImGui::TextUnformatted("measured knob by knob in Turbo's proof sessions):");
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

std::string lower_of(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// The text before the first ": " of a gate line is its code ("G1 switches: off: tick ..."): the person reads what follows
std::string plain_gate(const std::string& s) {
    const size_t c = s.find(": ");
    if (s.size() > 2 && s[0] == 'G' && std::isdigit((unsigned char)s[1]) && c != std::string::npos) return s.substr(c + 2);
    return s;
}

// The knob differs from FC 27 NOW: the game holds another value in memory, or a slider was moved to another value (an edit not written yet).
// The profile's values are NOT counted here: a profile that is off writes nothing (profile_differs below)
bool knob_changed(const Knob& k, const std::map<std::string, double>& defs) {
    auto l = g.live.find(k.id);
    if (l == g.live.end() || !l->second.located) return false;
    const std::string first = !l->second.copies.empty() ? l->second.copies.front().variant : std::string();
    double def = 0;
    if (copy_default(k, defs, first, def) && !same_value(k.type, l->second.value, def)) return true;
    auto e = g.edit.find(k.id);
    return g.touched.count(k.id) && e != g.edit.end() && !same_value(k.type, representable(k, e->second), l->second.value);
}

// The profile names a value for the knob that differs from FC 27's own (whether or not the profile is on, whether or not it was written)
bool profile_differs(const Knob& k, const std::map<std::string, double>& defs) {
    auto pv = g.prof.values.find(k.id);
    if (pv == g.prof.values.end()) return false;
    auto l = g.live.find(k.id);
    if (l == g.live.end() || !l->second.located) return false;
    const std::string first = !l->second.copies.empty() ? l->second.copies.front().variant : std::string();
    double def = 0;
    if (!copy_default(k, defs, first, def)) return true;
    return !same_value(k.type, representable(k, pv->second), def);
}

// The search box matches the label, the id, the group, the category and the table field of the knob, case-insensitively; several words must
// all match (some field). The collection name and the notes are NOT searched: every knob of a table shares its name ("...linelengthwidth"
// holds the length knobs too), so searching them kept the rows a person typing "width" did not mean (T27-F1)
bool knob_matches(const Knob& k, const std::string& needle) {
    std::istringstream words(needle);
    std::string w;
    while (words >> w) {
        bool hit = false;
        for (const std::string* f : {&k.label, &k.id, &k.group, &k.category, &k.field})
            if (lower_of(*f).find(w) != std::string::npos) {
                hit = true;
                break;
            }
        if (!hit) return false;
    }
    return true;
}

ImVec4 badge_colour(Badge b) {
    switch (b) {
        case Badge::EffectMeasured: return ImVec4(0.45f, 0.85f, 0.45f, 1);
        case Badge::EngineConfirmed: return ImVec4(0.45f, 0.75f, 0.9f, 1);
        case Badge::LocatedNotProven: return ImVec4(0.75f, 0.75f, 0.55f, 1);
    }
    return ImVec4(1, 1, 1, 1);
}

void draw_row(App& app, const Knob& k, const std::map<std::string, double>& defs) {
    TuningViewState::Row row;
    row.id = k.id;
    row.label = k.label;
    row.group = k.group;
    auto l = g.live.find(k.id);
    const KnobState st = state_of(k);
    const bool proven = st == KnobState::Proven;
    const Badge badge = knob_badge(k);
    row.state = knob_state_name(st);
    row.located = "located";
    row.badge = badge_name(badge);
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
    if (proven) tip += "\nEffect measured in a played match: " + k.proof.session + " (" + k.proof.evidence + ")";
    else if (badge == Badge::EngineConfirmed)
        tip += "\nThe engine read was confirmed in game (" + k.engine_check.session + ", " + k.engine_check.evidence +
               "); its effect on a match has not been measured yet, so Turbo shows it read-only.";
    else tip += "\nLocated and checked in the running game; its effect on a match has not been measured yet, so Turbo shows it read-only.";
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
    ImGui::TextColored(badge_colour(badge), "[%s]", row.badge.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
    // 2.7: when a written value reaches the match (a slider row only): live = the engine reads it while the match runs ("Apply now"), else
    // it is read when the match is built (write it in the hub before kick-off)
    if (proven) {
        row.live = k.live_ok();
        row.latched = !row.live;
        row.timing = k.timing_label();
        ImGui::SameLine();
        ImGui::TextColored(row.live ? ImVec4(0.45f, 0.85f, 0.45f, 1) : k.timing() == Knob::Timing::NextMatch ? ImVec4(0.9f, 0.75f, 0.4f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1),
                           "[applies: %s]", row.timing.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", row.live ? "The engine reads this value while the match runs: \"Apply now\" writes it into the running match (the owner rule, a read-back "
                                               "and a rollback on a mismatch apply; turbo_output\tuning_live_off.txt stops it). The tuning profile writes it again at the "
                                               "next hub."
                                             : "The engine reads this value once, when the match is built (kick-off). Write it in the career hub (Apply) before kick-off; "
                                               "changing it during a match does nothing until the next kick-off, so Turbo does not offer a live write.");
    }
    // FC 27's own value, the value now when it differs, the profile's, Reset
    ImGui::SameLine();
    std::string info = std::string("FC 27: ") + (have_def ? row.def : std::string("?"));
    if (proven || !have_def || row.game != row.def) info += " | now: " + row.game;  // 2.7: a slider row always reads back what the game holds
    if (proven) row.readback = "in the game now: " + row.game;
    if (!row.profile.empty()) info += std::string(" | profile") + (g.prof.on ? "" : " (off, not written)") + ": " + row.profile;
    ImGui::TextDisabled("%s", info.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27: the game's own value (Restore FC 27 defaults puts it back). now: what the game holds in memory at this moment "
                          "(shown when it differs). profile: the value your tuning profile writes at its moments.");
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
    if (row.live) {
        // hotkey-free: a button on the page, for a knob proven live only; the plan passes every gate or the button is greyed with the reason
        Plan pl;
        const auto regp = registry_of(app);
        row.apply_now_why = row.changed ? live_plan(app, *regp, k.id, pl) : std::string("move the slider first");
        row.can_apply_now = row.apply_now_why.empty();
        ImGui::SameLine();
        ImGui::BeginDisabled(!row.can_apply_now);
        if (ImGui::SmallButton(("Apply now##tvnow_" + k.id).c_str())) do_apply_live(app, k.id);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", row.can_apply_now ? "Writes this knob now, also during a match; the value is read back and put back if it does not read as written."
                                                      : ("Cannot write now: " + row.apply_now_why).c_str());
    }
    ImGui::PopID();
    g_state.rows.push_back(row);
}

// How far a candidate is from working, in words (a candidate is never verified, but the text follows the knob's own fields)
std::string candidate_status(const Knob& k) {
    if (k.proven()) return "effect measured";
    if (k.engine_check.given()) return "engine read confirmed, effect not proven";
    if (k.verified) return "located, effect not proven";
    return "not verified in game, no engine check, no proof";
}

// UI only (2.6): the catalogue's unverified candidates, collapsed, read-only, never written. Rows are recorded in the state whether or not the
// header is open
void draw_candidates(const Registry& reg, const std::string& needle) {
    std::vector<const Knob*> all;
    for (const Knob& k : reg.knobs)
        if (k.candidate && state_of(k) == KnobState::Unlocated && knob_matches(k, needle)) all.push_back(&k);
    std::stable_sort(all.begin(), all.end(), [](const Knob* a, const Knob* b) {
        const int ra = a->rank > 0 ? a->rank : 1 << 30, rb = b->rank > 0 ? b->rank : 1 << 30;
        return ra != rb ? ra < rb : a->label < b->label;
    });
    g_state.candidates_shown = int(all.size());
    for (const Knob* k : all) {
        TuningViewState::Candidate c;
        c.id = k->id;
        c.label = k->label;
        c.group = k->group;
        c.field = k->field;
        c.category = k->category.empty() ? k->group : k->category;
        c.rank = k->rank;
        c.impact = k->impact;
        c.status = candidate_status(*k);
        g_state.candidate_rows.push_back(c);
    }
    if (g_state.candidates == 0) return;
    if (!needle.empty() && all.empty()) return;
    const std::string head = "Experimental candidates (" + std::to_string(all.size()) + ", read-only, never written)###tvcand";
    g_state.candidates_open = false;
    if (!ImGui::CollapsingHeader(head.c_str())) return;
    g_state.candidates_open = true;
    ImGui::TextWrapped(
        "The engine tables the offline inventory lists as candidates, ranked by the expected size of their effect on the pitch (1 = largest). None is "
        "verified in the running game and none has a measured effect: Turbo reads and writes nothing for them. This is the catalogue, so you can see "
        "what may come.");
    std::vector<std::string> cats;
    for (const auto& c : g_state.candidate_rows)
        if (std::find(cats.begin(), cats.end(), c.category) == cats.end()) cats.push_back(c.category);
    for (const std::string& cat : cats) {
        int n = 0;
        for (const auto& c : g_state.candidate_rows) n += c.category == cat ? 1 : 0;
        if (!needle.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        if (!ImGui::TreeNode((cat + " (" + std::to_string(n) + ")###tvcc_" + cat).c_str())) continue;
        for (const auto& c : g_state.candidate_rows) {
            if (c.category != cat) continue;
            const std::string line = (c.rank > 0 ? "#" + std::to_string(c.rank) + " " : std::string()) + c.label + "  (" + c.group +
                                     (c.field.empty() ? std::string() : " / " + c.field) + ")  " + (c.impact.empty() ? "impact not rated" : c.impact + " impact") +
                                     "  -  " + c.status;
            ImGui::BulletText("%s", line.c_str());
        }
        ImGui::TreePop();
    }
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
        "The game engine's gameplay tuning tables (shape, pressing, marking, runs, line depth; FC 27 reads them for every club in every match). "
        "A knob gets a slider only after its effect was measured in a played match; the others are shown read-only with their proof status.");
    ImGui::TextWrapped(
        "The game keeps these tables in memory only (not in the career save). Your tuning profile writes its values again at every "
        "career load and each time you enter the hub, while it is on.");
    g_state.session_note = true;
    if (reg_why.empty()) {
        const int ph = reg->placeholders();
        g_state.registry_line = "turbo\\tuning\\" + fs::path(reg->source).filename().string() + ": " + std::to_string(reg->knobs.size()) + " knobs (" +
                                std::to_string(ph) + " not confirmed) for game build " + reg->build;
        int nc = 0;
        for (const Knob& k : reg->knobs) nc += k.candidate ? 1 : 0;
        if (nc > 0) g_state.registry_line = "turbo\\tuning\\" + fs::path(reg->source).filename().string() + ": " + std::to_string(reg->knobs.size() - nc) + " knobs (" +
                                            std::to_string(ph - nc) + " not confirmed), " + std::to_string(nc) + " candidates of the engine-table inventory (not offered) for game build " + reg->build;
        ImGui::TextDisabled("%s", g_state.registry_line.c_str());
    } else {
        g_state.registry_line = reg_why;
        ImGui::TextWrapped("Gameplay tuning is not available: %s", reg_why.c_str());
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
        else if (k.candidate) ++g_state.candidates;
        else ++g_state.unlocated;
    }
    if (reg_why.empty())
        ImGui::TextWrapped("%d with a measured effect (sliders), %d located and checked but their effect not proven yet (read-only), %d not yet working on "
                           "this build (listed at the end).",
                           g_state.proven, g_state.located, g_state.unlocated);

    // the active preset and the tuning profile
    ImGui::SeparatorText("Active preset and profile");
    {
        const ActivePreset ap = active_preset(app, *reg);
        g_state.active_preset = ap.name;
        g_state.active_modified = ap.modified;
        if (ap.name.empty()) {
            g_state.active_line = "Active preset: none (your own values)";
        } else {
            g_state.active_line = "Active preset: " + ap.name + (ap.known ? (ap.modified ? " (values changed since)" : "") : " (its file is gone)");
        }
        g_state.active_line += g.prof.on ? " | applied automatically at every career load and in the hub" : " | not applied automatically (profile off)";
        ImGui::TextWrapped("%s", g_state.active_line.c_str());
        if (!ap.status.empty()) ImGui::TextDisabled("%s", ap.status.c_str());
    }
    bool pon = g.prof.on;
    if (checkbox_right("Apply my tuning profile automatically (every career load, the hub)##tvprof", &pon)) {
        g.prof.on = pon;
        save_profile_now(app, pon ? "switched on" : "switched off");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off by default. On: turbo_output\\tuning_profile.json is applied at every career load and each time you enter the hub "
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
        if (!g.moment_line.empty()) ImGui::TextDisabled("Last automatic write: %s", g.moment_line.c_str());
        g_state.test_mode = g.sw.test_unproven;
        if (g.sw.test_unproven)
            ImGui::TextColored(ImVec4(1, 0.35f, 0.3f, 1),
                               "TEST MODE: turbo_output\\%s makes the profile write knobs whose effect is not proven (the coordinator's proof sessions "
                               "only; delete it to stop).",
                               test_unproven_file());
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

    draw_presets(app, *reg);

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

    // what Apply writes, the actions (above the knobs: always in view)
    const std::map<std::string, double> defs = app.tuning ? app.tuning->defaults() : std::map<std::string, double>();
    g_state.plan = plan_lines(*reg, av.plan);
    ImGui::SeparatorText("Changes");
    ImGui::PushStyleColor(ImGuiCol_Text, gs.pass() ? ImVec4(0.55f, 0.85f, 0.55f, 1) : ImVec4(0.9f, 0.8f, 0.5f, 1));
    ImGui::TextWrapped("%s", gs.pass() ? "Ready to write." : ("Cannot write yet: " + plain_gate(g_state.status)).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        std::string tip;
        for (const auto& x : g_state.gates) tip += x + "\n";
        ImGui::SetTooltip("%s", tip.c_str());
    }
    if (!g_state.plan.empty()) {
        ImGui::TextUnformatted("Not written yet (Apply will write):");
        for (const std::string& l : g_state.plan) ImGui::BulletText("%s", l.c_str());
    } else if (!g_state.plan_error.empty()) {
        ImGui::TextDisabled("Nothing to apply: %s", g_state.plan_error.c_str());
    }
    // the knobs that hold another value than FC 27 NOW (in the game, or a slider edit not written yet); the profile's values are listed apart
    g_state.differs.clear();
    g_state.profile_differs.clear();
    for (const Knob& k : reg->knobs) {
        auto l = g.live.find(k.id);
        if (l == g.live.end() || !l->second.located) continue;
        double def = 0;
        const std::string first = !l->second.copies.empty() ? l->second.copies.front().variant : std::string();
        const bool have_def = copy_default(k, defs, first, def);
        if (knob_changed(k, defs)) {
            auto e = g.edit.find(k.id);
            const bool game_differs = have_def && !same_value(k.type, l->second.value, def);
            const bool edit_differs = g.touched.count(k.id) && e != g.edit.end() && !same_value(k.type, representable(k, e->second), l->second.value);
            std::string t = k.label + ": ";
            if (game_differs) t += "the game holds " + format_value(k, l->second.value) + " (FC 27 " + format_value(k, def) + ")";
            if (edit_differs) t += std::string(game_differs ? "; " : "") + "slider at " + format_value(k, e->second) + ", not written yet";
            g_state.differs.push_back(t);
        }
        if (profile_differs(k, defs)) {
            auto pv = g.prof.values.find(k.id);
            g_state.profile_differs.push_back(k.label + ": profile " + format_value(k, pv->second) + (have_def ? " (FC 27 " + format_value(k, def) + ")" : std::string()) +
                                              ", the game holds " + format_value(k, l->second.value));
        }
    }
    if (!g_state.differs.empty() &&
        ImGui::TreeNode(("Different from FC 27 in the game now or in the sliders: " + std::to_string(g_state.differs.size()) + " knob(s)###tvdiffs").c_str())) {
        for (const std::string& l : g_state.differs) ImGui::BulletText("%s", l.c_str());
        ImGui::TreePop();
    }
    if (!g_state.profile_differs.empty()) {
        g_state.profile_differs_line = "Your profile holds " + std::to_string(g_state.profile_differs.size()) + " value(s) different from FC 27 (" +
                                       (g.prof.on ? "the profile is on: they are written at the next career load and hub" : "the profile is off: nothing of it is written to the game") + ")";
        if (ImGui::TreeNode((g_state.profile_differs_line + "###tvprofdiffs").c_str())) {
            for (const std::string& l : g_state.profile_differs) ImGui::BulletText("%s", l.c_str());
            ImGui::TreePop();
        }
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
    // 2.7: "Apply now (live)": only when the knobs file has a knob that is proven live (with none, the page is exactly 2.6.0's)
    g_state.live_shown = false;
    g_state.live_plan.clear();
    for (const Knob& k : reg->knobs) g_state.live_shown = g_state.live_shown || (state_of(k) == KnobState::Proven && k.live_ok());
    if (g_state.live_shown) {
        Plan lp;
        g_state.live_why = live_plan(app, *reg, "", lp);
        g_state.can_apply_live = g_state.live_why.empty() && app.tuning != nullptr;
        if (g_state.can_apply_live) g_state.live_plan = plan_lines(*reg, lp);
        ImGui::SameLine();
        ImGui::BeginDisabled(!g_state.can_apply_live);
        if (ImGui::Button("Apply now (live)##tvlive")) do_apply_live(app, "");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", g_state.can_apply_live ? "Writes the moved sliders of the knobs proven live now, also during a match (read back; rolled back on a mismatch)."
                                                          : ("Cannot write live now: " + g_state.live_why).c_str());
        ImGui::TextDisabled("Apply now (live) writes only knobs marked [applies: now (live)]; the others are read when the match starts, so use Apply in the hub before "
                            "kick-off. turbo_output\%s stops every write during a match.", live_off_file());
    }
    draw_confirm(app);

    // the knob list: search, filter, one collapsible category each (proven and located rows; the unlocated ones are listed at the end)
    ImGui::SeparatorText("Knobs");
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##tvsearch", "search knobs (name, group, field)", g.search, sizeof(g.search));
    ImGui::SameLine();
    checkbox_right("Only changed##tvonlychanged", &g.only_changed, -1.0f);
    const std::string needle = lower_of(g.search);
    g_state.search = g.search;
    g_state.only_changed = g.only_changed;
    int total_shown = 0, total_listed = 0;
    for (const Knob& k : reg->knobs) total_listed += state_of(k) != KnobState::Unlocated ? 1 : 0;
    auto shown = [&](const Knob& k) { return state_of(k) != KnobState::Unlocated && knob_matches(k, needle) && (!g.only_changed || knob_changed(k, defs)); };
    g_state.groups.clear();
    const bool narrowed = !needle.empty() || g.only_changed;
    for (const std::string& grp : reg->groups()) {
        int n_all = 0, n_shown = 0, n_proven = 0, n_changed = 0;
        for (const Knob& k : reg->knobs) {
            if (k.group != grp || state_of(k) == KnobState::Unlocated) continue;
            ++n_all;
            n_proven += state_of(k) == KnobState::Proven ? 1 : 0;
            n_changed += knob_changed(k, defs) ? 1 : 0;
            n_shown += shown(k) ? 1 : 0;
        }
        if (!n_all || !n_shown) continue;
        total_shown += n_shown;
        g_state.groups.push_back(grp);
        // a short list opens whole; a long catalogue opens the categories with a slider or a change, the rest stay closed (the search opens all it finds)
        if (narrowed) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        else ImGui::SetNextItemOpen(total_listed <= 24 || n_proven > 0 || n_changed > 0, ImGuiCond_Once);
        const std::string head = grp + " (" + std::to_string(n_shown) + (n_changed ? ", " + std::to_string(n_changed) + " changed" : std::string()) + ")###tvg_" + grp;
        if (!ImGui::CollapsingHeader(head.c_str())) {
            for (const Knob& k : reg->knobs)
                if (k.group == grp && shown(k)) {
                    TuningViewState::Row r;
                    r.id = k.id;
                    r.label = k.label;
                    r.group = grp;
                    r.state = knob_state_name(state_of(k));
                    r.badge = badge_name(knob_badge(k));
                    g_state.rows.push_back(r);  // listed (collapsed)
                }
            continue;
        }
        form_label_width(S(260.0f));
        for (const Knob& k : reg->knobs)
            if (k.group == grp && shown(k)) draw_row(app, k, defs);
        form_label_width(0.0f);
    }
    g_state.shown = total_shown;
    if (narrowed && total_shown == 0) ImGui::TextDisabled("No knob matches.");
    for (const Knob& k : reg->knobs)
        if (state_of(k) == KnobState::Unlocated && !k.candidate) g_state.not_working.push_back(k.label);
    if (!g_state.not_working.empty()) {
        if (ImGui::CollapsingHeader(("Not yet working on this build (" + std::to_string(g_state.not_working.size()) + ")##tvnotyet").c_str())) {
            ImGui::TextWrapped("These knobs are not located in the running game yet, so Turbo shows and writes nothing for them: %s.", joined(g_state.not_working).c_str());
            for (const Knob& k : reg->knobs) {
                if (state_of(k) != KnobState::Unlocated || k.candidate) continue;
                const KnobLive* l = live_of(k.id);
                ImGui::BulletText("%s", k.label.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", l && !l->why.empty() ? l->why.c_str() : "not read yet");
            }
        }
    }
    draw_candidates(*reg, needle);
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
    if (!so.empty()) ImGui::TextWrapped("Gameplay tuning is off for this game session: %s", so.c_str());
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

// 2.13: Advanced > Gameplay tuning (was Competitions > Gameplay tuning). The amber line is the only addition; drawn only in the Advanced view
void draw_tuning_page(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    draw_unproven_line("docs/GUI_AUDIT.md");
    ImGui::BeginChild("##tuning");
    draw_tuning(app);
    ImGui::EndChild();
}
TURBO_REGISTER_PAGE(shell::Page::AdvGameplayTuning, draw_tuning_page);

}  // namespace turbo
