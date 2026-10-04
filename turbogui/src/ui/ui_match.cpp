// FC 27 LE Turbo GUI - Competitions tab, "Match setup" view: the user's next fixtures in the game's competition engine
// (venue swap, another opponent, a fixed result) and the gameplay switches of the next matches (game variables).
// core/match_setup.h + core/fce_standings.h; docs/re/match_setup.md.
#include <algorithm>
#include <cstdio>
#include <map>

#include "app.h"
#include "core/fce_standings.h"
#include "core/match_setup.h"
#include "imgui.h"

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

std::string league_name(App& app, int64_t leagueid) {
    std::string name;
    if (const Table* lg = app.db.table("leagues")) {
        Snapshot ls;
        const Field* nf = lg->field("leaguename");
        if (nf && ls.load(app.db.memory(), *lg))
            for (uint32_t r : ls.valid)
                if (ls.get_int(r, "leagueid", -1) == leagueid) name = ls.get_str(r, *nf);
    }
    return name;
}

// "Serie A" for a fixture of competition node C31; "competition 1118" when the tree does not name it
std::string competition_of(App& app, const fce::Fixture& f) {
    if (f.compobj < g_ms.compobjs.size()) {
        const fce::CompObj& c = g_ms.compobjs[f.compobj];
        const int n = c.comp_number();
        if (n >= 0) {
            std::string name = league_name(app, n);
            if (!name.empty()) return name;
            return "competition " + std::to_string(n);
        }
    }
    return "competition " + std::to_string(f.compobj);
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

// The user's unplayed fixtures from today on, earliest first (at most `n`)
std::vector<const fce::Fixture*> upcoming(uint32_t team, uint32_t today, size_t n) {
    std::vector<const fce::Fixture*> out;
    for (const fce::Fixture& f : g_ms.fixtures) {
        if (f.used != 1 || f.played() || f.home_score >= 0 || f.away_score >= 0 || f.date < today) continue;
        if (team_of(f.home_sid) != team && team_of(f.away_sid) != team) continue;
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
        std::string err = fce::swap_fixture_sides(app.mem, g_ms.loc, f.id);
        if (err.empty()) after_edit(app, "fixture " + std::to_string(f.id) + ": home and away swapped");
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
        ImGui::SetNextItemWidth(S(260.0f));
        if (ImGui::BeginCombo("New opponent", app.model.team_name(team_of(int16_t(g_ms.opponent))).c_str())) {
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
            std::string err = fce::pairing_conflict(g_ms.fixtures, g_ms.rows, f.id, nh, na);
            if (err.empty()) err = fce::set_fixture_teams(app.mem, g_ms.loc, f.id, nh, na);
            if (err.empty())
                after_edit(app, "fixture " + std::to_string(f.id) + ": opponent is now " + app.model.team_name(team_of(int16_t(g_ms.opponent))));
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
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("Home goals", &g_ms.fix_home);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("Away goals", &g_ms.fix_away);
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
                app.notify("Result of fixture " + std::to_string(f.id) + " fixed at " + score + " (played or simulated, the table gets this score)");
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
            app.notify("Fixed result removed: fixture " + std::to_string(f.fixture));
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
    if (ImGui::BeginTable("##gvars", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Switch");
        ImGui::TableSetupColumn("Now");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int i = 0;
        for (const gv::KnownVar& v : gv::known_vars()) {
            ImGui::PushID(i++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(v.label);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s; read %s", v.name, v.what, v.when);
            ImGui::TableNextColumn();
            auto it = lines.find(v.name);
            if (it != lines.end() && it->second.active) ImGui::Text("%d", it->second.value);
            else ImGui::TextDisabled("game decides");
            ImGui::TableNextColumn();
            int& val = g_ms.edit.emplace(v.name, v.max == 1 ? 1 : std::max(int(v.min), 0)).first->second;
            ImGui::SetNextItemWidth(S(90.0f));
            ImGui::InputInt("##val", &val);
            val = std::clamp(val, int(v.min), int(v.max));
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Set")) {
                std::string err;
                if (!app.match_setup->set_var(v.name, val, err)) app.notify(std::string("Match setup: ") + v.name + ": " + err, true);
                else app.match_setup_status = std::string("queued: ") + v.name + " = " + std::to_string(val);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                std::string err;
                if (!app.match_setup->clear_var(v.name, err)) app.notify(std::string("Match setup: ") + v.name + ": " + err, true);
                else app.match_setup_status = std::string("queued: clear ") + v.name;
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

}  // namespace

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
            std::vector<const fce::Fixture*> next = upcoming(user, today, 8);
            if (next.empty()) ImGui::TextDisabled("No unplayed fixture of your club from today on.");
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
}

}  // namespace turbo
