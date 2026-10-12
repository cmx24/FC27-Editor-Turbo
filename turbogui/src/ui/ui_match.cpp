// FC 27 LE Turbo GUI - Competitions tab, "Match setup" view: the user's next fixtures in the game's competition engine
// (venue swap, another opponent, a fixed result) and the gameplay switches of the next matches (game variables).
// core/match_setup.h + core/fce_standings.h; docs/re/match_setup.md.
#include <algorithm>
#include <cstdio>
#include <map>

#include "app.h"
#include "comp_picker.h"
#include "core/comp_list.h"
#include "core/fce_standings.h"
#include "core/gameplay_sliders.h"
#include "core/match_setup.h"
#include "core/user_text.h"
#include "imgui.h"
#include "ui_tactics.h"  // tactics_off_now: the A/B presets honour turbo_output\tactics_off.txt as well

namespace turbo {

namespace {

struct MatchState {
    fce::Located loc;
    std::string err;
    uint64_t located_ifce = 0;
    int located_gen = -1;
    std::vector<fce::StandingRow> rows;
    std::vector<fce::Fixture> fixtures;
    std::vector<fce::CompObj> compobjs;
    int sel = -1;  // fixture id
    int fix_home = 1, fix_away = 0;
    int opponent = -1;  // standing id picked as the new opponent
    std::map<std::string, int> edit;  // variable -> value in its box
    // channel A: per-side gameplay sliders (core/gameplay_sliders.h) and the A/B presets
    std::map<std::string, int> side_edit;              // variable -> value in its box (50 = neutral until typed)
    std::map<std::string, std::string> read_back;      // variable -> "read back: 100" / why not, from the Read back buttons
    std::string ab_selected;                           // preset id whose plan is shown, "" = none
    int live_written = 0;                              // variables this panel set (cleared again when Turbo unloads)
    bool show_side = false;                            // the 68-row per-side table is open
};

MatchState g_ms;

bool reload(App& app) {
    g_ms.rows.clear();
    g_ms.fixtures.clear();
    g_ms.compobjs.clear();
    if (!g_ms.loc.ok()) return false;
    if (!fce::read_rows(app.mem, g_ms.loc, g_ms.rows) || !fce::read_fixtures(app.mem, g_ms.loc, g_ms.fixtures)) {
        g_ms.err = "the fixtures could not be read";
        g_ms.loc = fce::Located();
        return false;
    }
    if (g_ms.loc.compobj_count) fce::read_compobjs(app.mem, g_ms.loc, g_ms.compobjs);
    return true;
}

void ensure_located(App& app) {
    const uint64_t ifce = app.bridge.state().ifce;
    const bool fresh = g_ms.loc.ok() && g_ms.located_ifce == ifce && g_ms.located_gen == app.gen && fce::validate(app.mem, g_ms.loc, app.game_base);
    if (fresh) return;
    g_ms.located_ifce = ifce;
    g_ms.located_gen = app.gen;
    g_ms.err = fce::locate(app.mem, ifce, app.game_base, g_ms.loc);
    if (g_ms.err.empty()) reload(app);
}

uint32_t team_of(int16_t sid) {
    if (sid < 0 || size_t(sid) >= g_ms.rows.size()) return 0;
    const fce::StandingRow& r = g_ms.rows[size_t(sid)];
    return r.used == 1 ? r.teamid : 0;
}

// The competition of a fixture's node (core/comp_list.h): one entry per competition, whatever the round
comps::Entry competition_entry(App& app, const fce::Fixture& f) {
    const comps::TreeInfo ti = comps::tree_info(g_ms.compobjs, f.compobj);
    comps::Entry e = comps::describe(comp_name_sources(app), ti, f.compobj);
    if (!ti.found) e.name = "competition " + std::to_string(f.compobj);
    e.key = e.parent_key;
    e.stage.clear();
    e.main_stage = true;
    e.user = true;
    return e;
}

// "Serie A" for a fixture of competition node C31, "UEFA Champions League - round of 16" for a cup round
std::string competition_of(App& app, const fce::Fixture& f) {
    const comps::TreeInfo ti = comps::tree_info(g_ms.compobjs, f.compobj);
    if (!ti.found) return "competition " + std::to_string(f.compobj);
    return comps::title(comps::describe(comp_name_sources(app), ti, f.compobj));
}

// A fixed draw is only safe where a draw is a final result: the group of the user's row must be a league stage
bool league_stage(int16_t sid) {
    if (sid < 0 || size_t(sid) >= g_ms.rows.size() || g_ms.compobjs.empty()) return false;
    fce::GroupInfo gi;
    return fce::describe_group(g_ms.compobjs, g_ms.rows[size_t(sid)].compobj, gi) && gi.league_stage();
}

std::string fixture_line(App& app, const fce::Fixture& f) {
    char buf[200];
    const std::string home = app.model.team_name(team_of(f.home_sid)), away = app.model.team_name(team_of(f.away_sid));
    std::snprintf(buf, sizeof(buf), "%02u.%02u.%04u %02u:%02u  %s v %s  (%s)", unsigned(f.date % 100), unsigned(f.date / 100 % 100),
                  unsigned(f.date / 10000), unsigned(f.time / 100), unsigned(f.time % 100), home.c_str(), away.c_str(),
                  competition_of(app, f).c_str());
    return buf;
}

// The user's unplayed fixtures from today on, earliest first (at most `n`; with `filter` only those of the competition
// whose picker key is `comp_key`)
std::vector<const fce::Fixture*> upcoming(App& app, uint32_t team, uint32_t today, size_t n, bool filter = false, int comp_key = 0) {
    std::vector<const fce::Fixture*> out;
    for (const fce::Fixture& f : g_ms.fixtures) {
        if (f.used != 1 || f.played() || f.home_score >= 0 || f.away_score >= 0 || f.date < today) continue;
        if (team_of(f.home_sid) != team && team_of(f.away_sid) != team) continue;
        if (filter && competition_entry(app, f).key != comp_key) continue;
        out.push_back(&f);
    }
    std::sort(out.begin(), out.end(), [](const fce::Fixture* a, const fce::Fixture* b) {
        return a->date != b->date ? a->date < b->date : a->time < b->time;
    });
    if (out.size() > n) out.resize(n);
    return out;
}

void after_edit(App& app, const std::string& what) {
    reload(app);
    app.notify(what + " (the career hub shows it after the next day advance)");
    app.log("match setup: " + what);
}

void draw_selected(App& app, const fce::Fixture& f, uint32_t user) {
    const bool user_home = team_of(f.home_sid) == user;
    const int16_t user_sid = user_home ? f.home_sid : f.away_sid, opp_sid = user_home ? f.away_sid : f.home_sid;
    ImGui::SeparatorText(fixture_line(app, f).c_str());
    ImGui::PushID("##msel");
    // venue
    if (ImGui::Button("Swap home and away")) {
        const std::string was = app.model.team_name(team_of(f.home_sid)) + " v " + app.model.team_name(team_of(f.away_sid));
        std::string err = fce::swap_fixture_sides(app.mem, g_ms.loc, f.id);
        if (err.empty()) after_edit(app, was + ": home and away swapped");
        else app.notify("Swap: " + err, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(you play %s)", user_home ? "at home" : "away");
    // opponent (experimental): another club of the same group, not already playing that day
    std::vector<int16_t> choices;
    if (opp_sid >= 0 && size_t(opp_sid) < g_ms.rows.size()) {
        const uint16_t group = g_ms.rows[size_t(opp_sid)].compobj;
        for (const fce::StandingRow& r : g_ms.rows)
            if (r.used == 1 && r.teamid > 0 && r.compobj == group && int16_t(r.id) != opp_sid && int16_t(r.id) != user_sid) choices.push_back(int16_t(r.id));
    }
    if (!choices.empty()) {
        if (g_ms.opponent < 0 || std::find(choices.begin(), choices.end(), int16_t(g_ms.opponent)) == choices.end()) g_ms.opponent = choices[0];
        form_label("New opponent");
        ImGui::SetNextItemWidth(S(260.0f));
        if (ImGui::BeginCombo("##newopp", app.model.team_name(team_of(int16_t(g_ms.opponent))).c_str())) {
            for (int16_t sid : choices) {
                ImGui::PushID(sid);
                if (ImGui::Selectable(app.model.team_name(team_of(sid)).c_str(), sid == g_ms.opponent)) g_ms.opponent = sid;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Play this opponent")) {
            const int16_t nh = user_home ? user_sid : int16_t(g_ms.opponent), na = user_home ? int16_t(g_ms.opponent) : user_sid;
            const std::string was = app.model.team_name(team_of(f.home_sid)) + " v " + app.model.team_name(team_of(f.away_sid));
            std::string err = fce::pairing_conflict(g_ms.fixtures, g_ms.rows, f.id, nh, na);
            if (err.empty()) err = fce::set_fixture_teams(app.mem, g_ms.loc, f.id, nh, na);
            if (err.empty())
                after_edit(app, was + ": opponent is now " + app.model.team_name(team_of(int16_t(g_ms.opponent))));
            else
                app.notify("New opponent: " + err, true);
        }
        ImGui::TextDisabled("Experimental: the old opponent keeps its other fixtures; the season's pairings are no longer balanced.");
    }
    // fixed result (opt-in: installs the result hooks)
    if (app.match_setup) {
        std::string why;
        const bool ready = app.match_setup->fixing_ready(&why);
        ImGui::BeginDisabled(!ready);
        form_label("Home goals");
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("##homegoals", &g_ms.fix_home);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Away goals");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("##awaygoals", &g_ms.fix_away);
        g_ms.fix_home = std::clamp(g_ms.fix_home, 0, int(mfix::kMaxGoals));
        g_ms.fix_away = std::clamp(g_ms.fix_away, 0, int(mfix::kMaxGoals));
        ImGui::SameLine();
        if (ImGui::Button("Fix this result")) {
            std::string err;
            bool ok = false;
            if (g_ms.fix_home == g_ms.fix_away && !league_stage(user_sid)) err = "a draw can only be fixed in a league match (a cup tie needs a winner)";
            else ok = app.match_setup->fix(f.id, g_ms.fix_home, g_ms.fix_away, fixture_line(app, f), err);
            const std::string score = std::to_string(g_ms.fix_home) + "-" + std::to_string(g_ms.fix_away);
            if (ok) {
                app.notify("Result of " + app.model.team_name(team_of(f.home_sid)) + " v " + app.model.team_name(team_of(f.away_sid)) +
                           " fixed at " + score + " (played or simulated, the table gets this score)");
                app.log("match setup: fixture " + std::to_string(f.id) + " fixed at " + score);
            } else {
                app.notify("Fix result: " + err, true);
            }
        }
        ImGui::EndDisabled();
        if (!ready) ImGui::TextDisabled("Result fixing is off: %s", why.c_str());
    }
    ImGui::PopID();
}

void draw_fixes(App& app) {
    if (!app.match_setup) return;
    std::vector<mfix::Fix> fixes = app.match_setup->fixes();
    if (fixes.empty()) return;
    ImGui::SeparatorText("Fixed results");
    for (const mfix::Fix& f : fixes) {
        ImGui::PushID(int(f.fixture));
        ImGui::Text("%d-%d  %s%s", int(f.home), int(f.away), f.label.empty() ? ("fixture " + std::to_string(f.fixture)).c_str() : f.label.c_str(),
                    f.applied ? "  [applied]" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            app.match_setup->unfix(f.fixture);
            app.notify("Fixed result removed" + (f.label.empty() ? std::string() : ": " + f.label));
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Remove every fixed result")) app.match_setup->unfix_all();
}

void draw_vars(App& app) {
    ImGui::SeparatorText("Gameplay switches for the next matches");
    if (!app.match_setup) {
        ImGui::TextDisabled("Not available: this Turbo has no game calls.");
        return;
    }
    std::string why;
    const bool ready = app.match_setup->vars_ready(&why);
    if (!ready) ImGui::TextDisabled("Off: %s", why.c_str());
    std::map<std::string, msetup::VarLine> lines;
    for (const msetup::VarLine& l : app.match_setup->vars()) lines[l.name] = l;
    ImGui::BeginDisabled(!ready);
    if (ImGui::BeginTable("##gvars", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Switch");
        ImGui::TableSetupColumn("Now");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int i = 0;
        for (const gv::KnownVar* vp : gv::known_proven()) {  // the untested per-side family has its own table below
            const gv::KnownVar& v = *vp;
            ImGui::PushID(i++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.label);
            if (ImGui::IsItemHovered()) {  // what it does first, when the game reads it, then the game's own variable name
                ImGui::BeginTooltip();
                ImGui::Text("%s", v.what);
                ImGui::Text("Applies %s.", v.when);
                ImGui::TextDisabled("%s", v.name);
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();
            const bool difficulty = std::string(v.name) == "OVERRIDE_MATCH_DIFFICULTY";
            auto it = lines.find(v.name);
            if (it != lines.end() && it->second.active) {
                if (difficulty) ImGui::Text("%d (%s)", it->second.value, usertext::difficulty_text(it->second.value).c_str());
                else ImGui::Text("%d", it->second.value);
            } else {
                ImGui::TextDisabled("game decides");
            }
            ImGui::TableNextColumn();
            int& val = g_ms.edit.emplace(v.name, v.max == 1 ? 1 : std::max(int(v.min), 0)).first->second;
            ImGui::SetNextItemWidth(S(90.0f));
            ImGui::InputInt("##val", &val);
            val = std::clamp(val, int(v.min), int(v.max));
            if (difficulty) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", usertext::difficulty_text(val).c_str());
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Set")) {
                std::string err;
                if (!app.match_setup->set_var(v.name, val, err)) app.notify(std::string("Match setup: ") + v.label + ": " + err, true);
                else {
                    app.match_setup_status = usertext::match_var_set(v.name, val);
                    ++g_ms.live_written;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                std::string err;
                if (!app.match_setup->clear_var(v.name, err)) app.notify(std::string("Match setup: ") + v.label + ": " + err, true);
                else app.match_setup_status = usertext::match_var_clear(v.name);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("Clear every switch")) {
        std::string err;
        if (!app.match_setup->clear_all(err)) app.notify("Match setup: " + err, true);
    }
    ImGui::EndDisabled();
    if (!app.match_setup_status.empty()) ImGui::TextDisabled("Last: %s", app.match_setup_status.c_str());
    ImGui::TextWrapped(
        "The switches live in the game's own variable store until the game closes; the game reads them when the next match is set "
        "up. Clear one and the game decides again. Fatigue, offsides, VAR and referee strictness have no such switch in this game "
        "build (docs/re/match_setup.md).");
}

// Read the live table for one variable and remember the outcome for the "Read back" column (read-only, no game call)
void read_back_one(App& app, const std::string& name) {
    int32_t v = 0;
    std::string why;
    const bool ok = app.match_setup->read_back(name, v, why);
    g_ms.read_back[name] = ok ? "read back: " + std::to_string(v) : "no read-back: " + why;
    app.log("match setup: " + name + (ok ? " read back " + std::to_string(v) : ": no read-back (" + why + ")"));
}

// Clear every active override (the A/B "baseline" arm does the same before sending nothing)
void clear_all_overrides(App& app, const char* what) {
    std::string err;
    if (!app.match_setup->clear_all(err)) app.notify(std::string(what) + ": " + err, true);
    else {
        app.notify(std::string(what) + ": every override is being cleared (the game decides again)");
        app.log(std::string("match setup: ") + what + ": clear all overrides");
        g_ms.read_back.clear();
    }
}

// Channel A: one row per slider and side; OFF (cleared) by default, the value box starts at the neutral 50 but nothing is sent until Set
void draw_side_sliders(App& app, bool ready, const std::map<std::string, msetup::VarLine>& lines) {
    ImGui::SeparatorText("Gameplay sliders per side (channel A: accepted by the game, untested in a match)");
    ImGui::TextWrapped(
        "Each slider exists once for your team (_USER) and once for the CPU team (_CPU): 34 sliders, 68 game variables. The game's settings "
        "reset reads them into its slider block; whether that value survives to kick-off is exactly what the A/B test below decides. Every "
        "row is OFF by default (off = the variable is cleared, 50 is never sent as off) and carries the badge \"accepted, untested\": the game "
        "stores the value and reads it back, but no played match has shown an effect yet. EA's slider menu is never touched.");
    ImGui::Checkbox("Show the 68 per-side sliders##side", &g_ms.show_side);
    ImGui::SameLine();
    ImGui::BeginDisabled(!ready);
    if (ImGui::SmallButton("Read back every active override")) {
        int n = 0;
        for (const auto& kv : lines)
            if (kv.second.active) {
                read_back_one(app, kv.first);
                ++n;
            }
        if (n == 0) app.notify("Read back: no override is active");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear all overrides##side")) clear_all_overrides(app, "Clear all overrides");
    ImGui::EndDisabled();
    if (!g_ms.show_side) return;
    ImGui::BeginDisabled(!ready);
    if (ImGui::BeginTable("##sidevars", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                                              ImGuiTableFlags_ScrollY, ImVec2(0, S(380.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Slider");
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Now");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Read back");
        ImGui::TableHeadersRow();
        int i = 0;
        gs::Group last_group = gs::Group::Basic;
        bool first = true;
        for (const gs::Slider& s : gs::family()) {
            if (first || s.group != last_group) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", gs::group_name(s.group));
                last_group = s.group;
                first = false;
            }
            for (gs::Side side : {gs::Side::User, gs::Side::Cpu}) {
                const std::string name = gs::var_name(s, side);
                const gv::KnownVar* kv = gv::known(name);
                if (!kv) continue;
                ImGui::PushID(i++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.label);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", s.what);
                    ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", kv->untested ? "accepted, untested: no played match has shown an effect yet"
                                                                                       : "proven in a played match (injury override)");
                    ImGui::TextDisabled("%s", name.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(gs::side_label(side));
                ImGui::TableNextColumn();
                auto it = lines.find(name);
                if (it != lines.end() && it->second.active) ImGui::Text("%d", it->second.value);
                else ImGui::TextDisabled("off (game decides)");
                ImGui::TableNextColumn();
                int& val = g_ms.side_edit.emplace(name, gs::kNeutral).first->second;
                ImGui::SetNextItemWidth(S(80.0f));
                ImGui::InputInt("##sv", &val);
                val = std::clamp(val, int(kv->min), int(kv->max));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Set")) {
                    std::string err;
                    if (!app.match_setup->set_var(name, val, err)) app.notify(std::string("Match setup: ") + kv->label + ": " + err, true);
                    else {
                        app.match_setup_status = usertext::match_var_set(name, val);
                        ++g_ms.live_written;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear")) {
                    std::string err;
                    if (!app.match_setup->clear_var(name, err)) app.notify(std::string("Match setup: ") + kv->label + ": " + err, true);
                    else app.match_setup_status = usertext::match_var_clear(name);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Read back")) read_back_one(app, name);
                ImGui::TableNextColumn();
                auto rb = g_ms.read_back.find(name);
                if (rb != g_ms.read_back.end()) ImGui::TextUnformatted(rb->second.c_str());
                else ImGui::TextDisabled("-");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
}

// The two one-click A/B presets: pressing one shows exactly what Apply will send; Apply sends it (clear first, then the writes)
void draw_ab(App& app, bool ready, const std::string& not_ready_why) {
    ImGui::SeparatorText("A/B test: does channel A reach the pitch? (one match per arm, same save)");
    ImGui::TextWrapped(
        "Arm A: press the baseline, Apply, play one match, note the CPU's pace, pass accuracy and injuries. Reload the same save. Arm B: "
        "press the CPU handicap extremes, Apply, play the same fixture. Works = CPU forwards visibly slow in replays, CPU pass accuracy in Match "
        "Facts far below arm A, soft goals past their keeper, 2 or more CPU injuries in the first half. Does not work = both matches look alike. "
        "Both arms clear every override first; nothing is sent while turbo_output\\tactics_off.txt or call_gamevar_off.txt exists.");
    for (const gs::Preset& p : gs::presets()) {
        if (ImGui::Button(p.title)) g_ms.ab_selected = p.id;
        ImGui::SameLine();
    }
    ImGui::NewLine();
    const gs::Preset* sel = g_ms.ab_selected.empty() ? nullptr : gs::preset(g_ms.ab_selected);
    if (!sel) return;
    ImGui::TextWrapped("%s", sel->summary);
    ImGui::TextUnformatted("Apply will do, in this order:");
    for (const std::string& line : gs::describe(*sel)) ImGui::BulletText("%s", line.c_str());
    std::vector<gs::Write> planned;
    const bool tactics_off = tactics_off_now(app);
    const std::string gate = gs::plan(*sel, tactics_off, !ready, not_ready_why, planned);
    if (!gate.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Blocked: %s", gate.c_str());
    ImGui::BeginDisabled(!gate.empty());
    if (ImGui::Button("Apply##ab")) {
        // the kill switches are read again at the moment of the write, never from a cache
        std::vector<gs::Write> now;
        const std::string gate2 = gs::plan(*sel, tactics_off_now(app), !app.match_setup->vars_ready(nullptr), "the host refuses game variables", now);
        if (!gate2.empty()) app.notify("A/B preset not applied: " + gate2, true);
        else {
            const gs::ApplyReport r = gs::apply(*app.match_setup, *sel, now);
            g_ms.live_written += r.sent;
            g_ms.read_back.clear();
            std::string msg = std::string(sel->title) + ": " + (r.cleared_all ? "every override cleared" : "clear refused") + ", " +
                              std::to_string(r.sent) + " variable(s) sent";
            if (r.refused) msg += ", " + std::to_string(r.refused) + " refused (" + r.first_error + ")";
            app.notify(msg, r.refused > 0);
            app.log("match setup: A/B preset " + std::string(sel->id) + ": " + msg);
            for (const gs::Write& w : now) app.log("match setup: A/B preset " + std::string(sel->id) + " sends " + w.name + " = " + std::to_string(w.value));
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel##ab")) g_ms.ab_selected.clear();
    ImGui::TextDisabled("Each variable's outcome arrives as a notification (\"ok\" = the game's SetInt stored it and Turbo read it back). The Status tab lists "
                        "the active overrides with their read-back values.");
}

}  // namespace

void match_setup_clear_on_exit(App& app) {
    try {
        if (g_ms.live_written > 0 && app.match_setup) {
            std::string why;
            app.match_setup->clear_all(why);
            g_ms.live_written = 0;
        }
    } catch (...) {
    }
}

void draw_match_setup(App& app) {
    ensure_located(app);
    ImGui::TextWrapped("Your next matches in the game's competition engine, and switches for the matches you play next. A played "
                       "result is edited in the Live standings view: the table follows it.");
    if (!g_ms.loc.ok()) {
        ImGui::TextDisabled("The fixtures are not reachable: %s", g_ms.err.empty() ? "load a career" : g_ms.err.c_str());
        if (ImGui::Button("Try again")) g_ms.located_gen = -1;
    } else {
        if (ImGui::Button("Reload")) reload(app);
        const uint32_t user = uint32_t(std::max<int64_t>(0, app.bridge.state().user_team));
        const GameDate& d = app.bridge.state().date;
        const uint32_t today = d.valid() ? uint32_t(d.as_int()) : 0;
        if (!user) {
            ImGui::TextDisabled("Your club is not known yet (Live Editor publishes it when a career is loaded).");
        } else {
            // the competitions of your next fixtures, to show one of them only (remembered: competitions.match)
            static CompPicker picker("match");
            static std::vector<comps::Entry> comps_of;
            static int built_gen = -1, chosen = -1;
            static size_t built_n = 0;
            const std::vector<const fce::Fixture*> all = upcoming(app, user, today, 10000);
            if (built_gen != app.gen || built_n != all.size()) {
                built_gen = app.gen;
                built_n = all.size();
                const int64_t keep = chosen >= 0 && size_t(chosen) < comps_of.size() ? comps_of[size_t(chosen)].key : -1;
                comps_of.clear();
                std::map<int, size_t> at;
                for (const fce::Fixture* f : all) {
                    comps::Entry e = competition_entry(app, *f);
                    auto it = at.find(e.parent_key);
                    if (it == at.end()) {
                        at[e.parent_key] = comps_of.size();
                        comps_of.push_back(e);
                    }
                }
                chosen = -1;
                for (size_t i = 0; i < comps_of.size(); ++i)
                    if (keep >= 0 && comps_of[i].key == keep) chosen = int(i);
                if (keep < 0) chosen = comp_picker_restore(app, picker, comps_of);
            }
            comp_picker(app, picker, "Competition##msc", comps_of, chosen, S(380.0f), "All competitions");
            const bool filter = chosen >= 0 && size_t(chosen) < comps_of.size();
            std::vector<const fce::Fixture*> next = upcoming(app, user, today, 8, filter, filter ? int(comps_of[size_t(chosen)].key) : 0);
            if (next.empty()) ImGui::TextDisabled("No unplayed fixture of your club from today on%s.", filter ? " in this competition" : "");
            for (const fce::Fixture* f : next) {
                char id[32];
                std::snprintf(id, sizeof(id), "##mf%u", unsigned(f->id));
                if (ImGui::Selectable((fixture_line(app, *f) + id).c_str(), g_ms.sel == int(f->id))) g_ms.sel = int(f->id);
            }
            if (g_ms.sel >= 0 && size_t(g_ms.sel) < g_ms.fixtures.size()) {
                const fce::Fixture& f = g_ms.fixtures[size_t(g_ms.sel)];
                if (f.used == 1 && !f.played() && (team_of(f.home_sid) == user || team_of(f.away_sid) == user)) draw_selected(app, f, user);
            }
        }
        draw_fixes(app);
    }
    draw_vars(app);
    if (app.match_setup) {
        std::string why;
        const bool ready = app.match_setup->vars_ready(&why);
        std::map<std::string, msetup::VarLine> lines;
        for (const msetup::VarLine& l : app.match_setup->vars()) lines[l.name] = l;
        draw_side_sliders(app, ready, lines);
        draw_ab(app, ready, why);
    }
}

}  // namespace turbo
