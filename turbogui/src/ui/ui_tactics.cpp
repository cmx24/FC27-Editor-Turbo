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
#include "file_picker.h"
#include "pitch_view.h"

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

struct TacState {
    int scope = 0;  // SliderScope index
    SliderSet working;   // what the sliders hold now
    SliderSet applied;   // what was last applied (the base for "N changed" and Revert)
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
    bool off = false;    // tactics_off.txt (looked at every 2 s)
    double next_check = -1.0;
    int live_written = 0;  // game variables this panel set (cleared when Turbo unloads)
    std::vector<std::string> writes_done;  // what the last Apply wrote, one line per DB row (shown until the next Apply)
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
TacticsTabState g_state;

const char* const kScopeLabels[4] = {"Match (both teams)", "My team", "Position/Role", "Opposition"};
const SliderScope kScopes[4] = {SliderScope::Match, SliderScope::Team, SliderScope::Position, SliderScope::Opposition};

fs::path off_file(App& app) { return app.bridge.root() / "turbo_output" / "tactics_off.txt"; }

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

// The kill switch, looked at every 2 s (the frame path does no file I/O otherwise)
void refresh_off(App& app) {
    if (app.now < g_tac.next_check) return;
    g_tac.next_check = app.now + 2.0;
    std::error_code ec;
    g_tac.off = fs::exists(off_file(app), ec);
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
std::string tag_of(const SliderDef& d) {
    switch (d.status) {
        case SliderStatus::Live: return "game variable, applies in played matches";
        case SliderStatus::DB: return "saved data, in-game effect unverified";
        case SliderStatus::Local: return "Turbo only, never written to the game";
        case SliderStatus::Preview:
        case SliderStatus::RE: return "not applied to game";
    }
    return "";
}

ImVec4 badge_colour(SliderStatus s) {
    switch (s) {
        case SliderStatus::Live: return ImVec4(0.45f, 0.85f, 0.45f, 1);
        case SliderStatus::DB: return ImVec4(0.45f, 0.7f, 1.0f, 1);
        case SliderStatus::Local: return ImVec4(0.8f, 0.7f, 1.0f, 1);
        case SliderStatus::Preview: return ImVec4(0.9f, 0.8f, 0.4f, 1);
        case SliderStatus::RE: return ImVec4(0.7f, 0.7f, 0.7f, 1);
    }
    return ImVec4(1, 1, 1, 1);
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
// the same variable); `unsent` gets the switched-on settings it cannot send (a formation slot offset needs a slot and a formation row).
std::vector<EffWrite> effective_writes(const SliderSet& s, std::vector<std::string>* unsent = nullptr) {
    std::vector<EffWrite> out;
    for (const SliderWrite& w : game_writes(s)) {
        if (w.target.find("{n}") != std::string::npos) {
            if (unsent) unsent->push_back(w.target + ": this build does not write formation slots");
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

// One DB write of "table.field" through App::edit (Database::set: range check, table_alive; undo steps for players; never saves).
// Skipped = this build or this save cannot say where to write (not a failure of the write itself); Failed = the write was refused.
enum class DbOutcome { Ok, Skipped, Failed };

DbOutcome write_db(App& app, const SliderWrite& w, std::string& why, std::string& what) {
    const size_t dot = w.target.find('.');
    if (dot == std::string::npos) {
        why = "no field named in " + w.target;
        return DbOutcome::Failed;
    }
    const std::string table = w.target.substr(0, dot), field = w.target.substr(dot + 1);
    if (field.find("{n}") != std::string::npos) {
        why = "formation slots are not written by this build";
        return DbOutcome::Skipped;
    }
    const Table* t = app.db.table(table);
    if (!t) {
        why = "this save has no " + table + " table";
        return DbOutcome::Skipped;
    }
    const Field* f = t->field(field);
    if (!f) {
        why = table + " has no " + field + " field in this save";
        return DbOutcome::Skipped;
    }
    std::vector<uint64_t> recs;
    if (table == "teams") {
        const TeamRow* tr = app.model.team(app.sel_team);
        if (!tr) {
            why = "select a team first";
            return DbOutcome::Failed;
        }
        recs.push_back(tr->rec);
    } else if (table == "players") {
        const PlayerRow* p = app.model.player(app.sel_player);
        if (!p) {
            why = "select a player first (Position/Role writes the selected player)";
            return DbOutcome::Failed;
        }
        recs.push_back(p->rec);
    } else if (table == "referee") {
        Snapshot snap;  // every referee row (a user action, not the frame path)
        if (snap.load(app.db.memory(), *t))
            for (uint32_t i : snap.valid) recs.push_back(snap.addr(i));
        if (recs.empty()) {
            why = "the referee table has no rows";
            return DbOutcome::Failed;
        }
    } else if (table == "mentalities" || table == "cm_mentalities") {
        // the team's own row, found by its teamid: the active tactic when one is flagged, else the first row
        const TeamRow* tr = app.model.team(app.sel_team);
        if (!tr) {
            why = "select a team first";
            return DbOutcome::Failed;
        }
        const TeamRows rows = team_rows(app.db, *t, tr->teamid);
        if (rows.key_missing) {
            why = table + " has no teamid field in this save";
            return DbOutcome::Skipped;
        }
        if (rows.recs.empty()) {
            why = tr->name + " has no row in " + table;
            return DbOutcome::Skipped;
        }
        recs.push_back(rows.recs[rows.chosen]);
        what = table + "." + field + " = " + std::to_string(w.value) + ": row " + std::to_string(rows.chosen + 1) + " of " +
               std::to_string(rows.recs.size()) + " of " + tr->name + (rows.has_active ? " (the active tactic)" : "");
    } else {
        why = "which row of " + table + " to write is not resolved in this build";
        return DbOutcome::Skipped;
    }
    for (uint64_t rec : recs) {
        if (!app.db.table_alive(*t, rec)) {
            why = "the database changed (another save loaded?): press Refresh";
            return DbOutcome::Failed;
        }
        if (!app.edit(*t, rec, *f, Value::of_int(w.value))) {
            why = "the game's database refused the value";
            return DbOutcome::Failed;
        }
    }
    if (what.empty()) what = table + "." + field + " = " + std::to_string(w.value);
    return DbOutcome::Ok;
}

// The kill switch is read again at every write: a write never trusts the 2 s cache
bool kill_switch_now(App& app) {
    std::error_code ec;
    g_tac.off = fs::exists(off_file(app), ec);
    g_tac.next_check = app.now + 2.0;
    return g_tac.off;
}

void apply_changes(App& app) {
    if (kill_switch_now(app)) {
        app.notify("Tactics are off: turbo_output\\tactics_off.txt exists, nothing was written", true);
        return;
    }
    const std::vector<EffWrite> writes = effective_writes(g_tac.working);
    if (g_tac.dry_run) {
        app.notify("Tactics dry run: " + std::to_string(writes.size()) + " write(s) would be sent, nothing was written");
        return;
    }
    g_tac.writes_done.clear();
    int ok = 0, failed = 0, skipped = 0;
    std::string first_error, first_skip;
    auto fail = [&](const std::string& key, const std::string& why) {
        ++failed;
        if (first_error.empty()) first_error = key + ": " + why;
    };
    // a Live slider that was applied and is now back to "the game decides": clear its variable
    for (const SliderDef& d : slider_registry()) {
        if (d.status != SliderStatus::Live || d.binding.targets.empty()) continue;
        if (g_tac.applied.is_enabled(d.key) && !g_tac.working.is_enabled(d.key) && app.match_setup) {
            std::string why;
            if (!app.match_setup->clear_var(d.binding.targets[0], why)) fail(d.key, why);
        }
    }
    for (const EffWrite& e : writes) {
        const SliderWrite& w = e.w;
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
            } else if (o == DbOutcome::Skipped) {
                ++skipped;
                if (first_skip.empty()) first_skip = w.key + ": " + why;
            } else fail(w.key, why);
        }
    }
    if (failed == 0) g_tac.applied = g_tac.working;
    std::string msg = "Tactics: " + std::to_string(ok) + " write(s) sent to the game";
    if (skipped) msg += ", " + std::to_string(skipped) + " not written (" + first_skip + ")";
    if (failed) msg += ", " + std::to_string(failed) + " refused (" + first_error + ")";
    app.notify(msg, failed > 0);
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
        st.live.push_back(l.target + " = " + std::to_string(l.value) + "  [Live]  (base " + std::to_string(l.base) +
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
    ImGui::Checkbox("Opposition variety on##tacoppon", &o.on);
    ImGui::SameLine();
    ImGui::Checkbox("Opponent plays at home##tacopphome", &o.home);
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
    for (const auto& kv : p.sliders.values) {
        if (p.sliders.is_enabled(kv.first)) g_tac.working.set(kv.first, kv.second, true);
        else g_tac.working.reset(kv.first);
    }
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
    app.notify("Preset loaded: " + p.name + " (not applied yet: press Apply)");
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

// ---- a slider row
void draw_row(App& app, const SliderDef& d, const Profile* cmp) {
    SliderSet& w = g_tac.working;
    const bool dim = preview_only(d);
    TacticsTabState::Row row;
    row.key = d.key;
    row.status = status_name(d.status);
    row.tag = tag_of(d);
    row.enabled = w.is_enabled(d.key);
    row.dimmed = dim;
    row.value = w.value_or_default(d.key);
    row.was = was_text(d, cmp);
    g_state.rows.push_back(row);

    ImGui::PushID(d.key.c_str());
    if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);
    bool on = w.is_enabled(d.key);
    if (ImGui::Checkbox("##on", &on)) w.set_enabled(d.key, on);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", on ? "Switched on: this value is used" : "Off: the game decides");
    ImGui::SameLine();
    ImGui::TextUnformatted(d.label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.description.c_str());
    ImGui::SameLine();
    ImGui::TextColored(badge_colour(d.status), "[%s]", status_name(d.status));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", status_badge_help(d.status));
    ImGui::SameLine();
    if (ImGui::SmallButton("o##reset")) w.reset(d.key);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to \"the game decides\"");
    if (!d.technical.empty()) ImGui::TextDisabled("  %s", d.technical.c_str());
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
    if (d.binding.kind == BindingKind::DbField && !d.binding.targets.empty() && d.binding.targets.front().find("{n}") != std::string::npos)
        ImGui::TextDisabled("  this build does not write formation slots yet");
    if (dim) ImGui::PopStyleVar();
    ImGui::PopID();
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
    ImGui::Checkbox("Compare with the loaded preset##taccmp", &g_tac.compare);
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
    ImGui::Checkbox("Schematic layers (dashed)##tacd", &g_tac.show_derived);
    ImGui::Checkbox("Modelled layer (hatched, off by default)##tach", &g_tac.show_heat);
    ImGui::Checkbox("Roaming range (stippled, off by default)##tacr", &g_tac.show_roam);
    ImGui::Checkbox("Exposure hint (hatched, off by default)##tace", &g_tac.show_risk);

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
        note = "Saved data: " + tf.source + ". Its in-game effect is unverified.";
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
    const std::vector<const SliderDef*> visible = sliders_in_scope(sc);
    const size_t prev = count_preview_only(visible);
    g_state.preview_strip = std::to_string(prev) + " of " + std::to_string(visible.size()) + " visible settings are preview-only";
    ImGui::TextDisabled("%s", g_state.preview_strip.c_str());
    // the cards: consecutive groups of the registry order
    std::vector<std::string> groups;
    for (const SliderDef* d : visible)
        if (std::find(groups.begin(), groups.end(), d->group) == groups.end()) groups.push_back(d->group);
    const Profile* cmp = g_tac.compare ? loaded_profile(app) : nullptr;
    // the footer lists what the game will receive: it grows with the list (but never takes more than 60% of the column)
    std::vector<std::string> unsent;
    const std::vector<EffWrite> writes = effective_writes(g_tac.working, &unsent);
    const float lines = float(std::max<size_t>(1, writes.size()) + unsent.size());
    const float footer = std::min(ImGui::GetContentRegionAvail().y * 0.6f,
                                  ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetTextLineHeightWithSpacing() * (lines + 2.0f) + S(10.0f));
    ImGui::BeginChild("##tacsliders", ImVec2(0, -footer));
    for (const std::string& g : groups) {
        ImGui::PushID(g.c_str());
        if (ImGui::CollapsingHeader(g.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            for (const SliderDef* d : visible)
                if (d->group == g) draw_row(app, *d, cmp);
        ImGui::PopID();
    }
    ImGui::EndChild();

    // footer: what the game will receive, N changed, Apply / Revert / Save as preset
    ImGui::SeparatorText("What the game will receive");
    g_state.receive_title = "What the game will receive";
    for (const EffWrite& e : writes) {
        const SliderDef* d = find_slider(e.w.key);
        const bool offset = !e.note.empty();
        const std::string line = e.w.target + " = " + (d && !offset ? name_of(*d, e.w.value) : std::to_string(e.w.value)) + "  [" + status_name(e.w.status) + "]" +
                                 (offset ? "  (" + e.note + ")" : "");
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
    ImGui::Checkbox("Dry run##tacdry", &g_tac.dry_run);
    ImGui::BeginDisabled(g_tac.off);
    if (ImGui::Button("Apply##tac")) apply_changes(app);
    ImGui::EndDisabled();
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
                g_tac.applied = SliderSet();
                app.notify("Game overrides cleared: the game decides again");
            }
        }
    }
}

void draw_body(App& app) {
    g_state = TacticsTabState();
    const std::string shown_error = g_tac.error;  // an error stays shown while it keeps happening, then goes
    g_tac.error.clear();
    g_state.scope = g_tac.scope;
    g_state.dry_run = g_tac.dry_run;
    g_state.selected_profile = g_tac.selected_profile;
    g_state.writes_done = g_tac.writes_done;
    refresh_off(app);
    g_state.off = g_tac.off;
    if (g_tac.off) {
        g_state.off_line = "Tactics are off: turbo_output\\tactics_off.txt exists. Nothing is written and the effect layers are grey.";
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", g_state.off_line.c_str());
    }
    draw_live_banner(app);
    if (!shown_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error: %s", shown_error.c_str());
    const float total = ImGui::GetContentRegionAvail().x;
    const ImGuiChildFlags cf = ImGuiChildFlags_Borders;
    ImGui::BeginChild("##tacleft", ImVec2(total * 0.22f, 0), cf);
    try {
        draw_left(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##taccentre", ImVec2(total * 0.46f, 0), cf);
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
