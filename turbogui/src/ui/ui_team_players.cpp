// FC 27 LE Turbo GUI - Team > Players (brief 2.20-IA-pages, track "team"; see ui_team_pages.h). Cassio 2026-10-19 10:56: "everything about
// contracts, morale etc that only applies to career should be in a players tab of the team menu."
//
// Three tabs for the selected club (App::sel_team, the user's club when none is chosen):
//   Squad list    2.22: the sortable squad table of 2.14 (squad_table, ui_teams.cpp) for any club: Jersey, Player, Pos, OVR, Age, Line-up slot.
//   Player        a list of the club's players and, for the one picked: Contract, Form and morale, Squad role, Growth.
//   Whole squad   the "My team" preset (core/team_preset, track S2) and the cards of Players > My squad (draw_career_squad_cards): form and
//                 fitness, squad roles, contracts, growth and the squad tools. These exist for the user's club only.
// "Extra XP development" and "Does not decline" are drawn by those cards and by the growth tool; their default is OFF (S2).
//
// Every button sends the request the old panels send (squad_role, reveal, development, form_morale ...) or writes the field through the
// same editors; a request the Undo button cannot bring back asks first. Morale and the squad roles exist in the game for the user's club
// only: for another club those controls are greyed out with the reason. Turbo can set the morale of the whole squad but cannot read or
// set one player's morale, so the Form and morale card says so.
//
// 4.0 S8: the page is drawn inside the page template (ui_page.h: title, help, body, ONE status line "Done: ...", the bar Undo / Back).
//   - The "My team preset" button is on the page itself, above the tabs, so it is reachable from every tab (it was inside the Whole squad
//     tab). Same function call (team_preset_apply), same question first. The five things it sets are listed from the preset's own field
//     list (core/team_preset.h fields()), so the page cannot say anything else than what the preset does.
//   - draw_player_contract_actions (q3): one player's contract and transfer actions for that player's card. It calls the existing code:
//     the Contract fields below and the extension and move controls of ui_team_transfers.cpp.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app.h"
#include "core/team_preset.h"
#include "imgui.h"
#include "ui_career_pages.h"
#include "ui_page.h"
#include "ui_team_common.h"
#include "ui_team_page40.h"
#include "ui_team_pages.h"
#include "ui_x2_state.h"

namespace turbo {

using nlohmann::json;
using namespace teampage;

namespace {

TeamPageState g_state;

bool is_own_club(App& app, int64_t club) {
    const BridgeState& st = app.bridge.state();
    return st.in_cm && st.user_team > 0 && club == st.user_team;
}

// The year his contract ends, 0 = none known
int64_t contract_year(App& app, const PlayerRow& p) {
    const Table* t = app.db.table("players");
    return t && t->has("contractvaliduntil") ? app.db.get_int(*t, p.rec, "contractvaliduntil", 0) : 0;
}

// ---------------------------------------------------------------- the list
void player_list(App& app, const std::vector<const PlayerRow*>& squad) {
    static char search[64] = "";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##teamplq", "player name", search, sizeof(search));
    const std::string q = fold_search(search);
    std::vector<const PlayerRow*> rows;
    rows.reserve(squad.size());
    for (const PlayerRow* p : squad)
        if (fold_find(p->name_fold, p->name, q)) rows.push_back(p);
    g_state.listed = rows.size();
    ImGui::BeginChild("##team_pl_rows", ImVec2(0, 0));
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(rows.size()));
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const PlayerRow& p = *rows[static_cast<size_t>(r)];
            const std::string label = p.name + "##tpl" + std::to_string(p.playerid);
            if (ImGui::Selectable(label.c_str(), app.sel_player == p.playerid)) app.sel_player = p.playerid;
            ImGui::SameLine(S(170.0f));
            const int64_t until = contract_year(app, p);
            theme::muted_text("%s  %d%s%s", position_name(p.position), p.overall, until > 0 ? "  until " : "", until > 0 ? std::to_string(until).c_str() : "");
        }
    }
    if (rows.empty()) theme::muted_text("No player matches.");
    ImGui::EndChild();
}

// ---------------------------------------------------------------- the cards of one player
// The contract fields of one player (the Contract card and the player card's contract actions draw the same grid)
void contract_fields(App& app, const Table& t, const PlayerRow& p) {
    field_grid(app, t, p.rec, {"contractvaliduntil", "wage", "releaseclause", "isretiring"}, "##tpcontract", 2);
    if (p.on_loan) {
        theme::muted_text("On loan from %s.", p.loaned_from > 0 ? app.model.team_name(p.loaned_from).c_str() : "another club");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A loaned player keeps the contract of his parent club.");
    }
}

void card_contract(App& app, const Table& t, const PlayerRow& p) {
    ++g_state.cards;
    card_begin("##tp_contract", "Contract");
    contract_fields(app, t, p);
    card_end();
}

void card_form(App& app, int64_t club, const PlayerRow& p) {
    ++g_state.cards;
    card_begin("##tp_form", "Form and morale");
    const Table* lt = app.db.table("teamplayerlinks");
    const Field* ff = lt ? lt->field("form") : nullptr;
    uint64_t link_rec = 0;
    for (const LinkRow& l : app.model.links_of_player(p.playerid))
        if (l.teamid == club) link_rec = l.rec;
    if (lt && ff && link_rec) {
        field_editor(app, *lt, link_rec, *ff, "Form (1 to 5)", S(80.0f));
    } else {
        theme::muted_text("This game's data has no form value for him.");
    }
    hint("Morale: Turbo sets it for the whole squad (tab Whole squad > Squad tools > Make everyone very happy). One player's morale can't be read or set yet.");
    card_end();
}

void role_fallback(App& app, const PlayerRow& p) {
    static int role = 2;  // Rotation, the default role
    ImGui::PushID("tprole");
    form_label("Role", "The game works every role out again at the start of a season.");
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::Combo("##role", &role, role_names(), 5);
    role = std::max(0, std::min(role, 4));
    const Gate g = lua_gate(app, true, "squad_role");
    ImGui::SameLine();
    if (gated_button("Set role###tpsetrole", false, g, "Gives him this role through the game itself. Open the Squad screen again to see it."))
        app.send(run_cmd("squad_role", {{"playerid", p.playerid}, {"role", role + 1}}), "Squad role " + p.name);
    ImGui::SameLine();
    if (gated_button("Read role###tpreadrole", false, g, "Asks the game which role he has now."))
        app.send(run_cmd("squad_role", {{"playerid", p.playerid}, {"mode", "check"}}), "Squad role " + p.name);
    ImGui::PopID();
}

void card_role(App& app, int64_t club, const PlayerRow& p) {
    ++g_state.cards;
    card_begin("##tp_role", "Squad role");
    if (!is_own_club(app, club)) {
        theme::muted_text("The game keeps squad roles for your own club only.");
    } else if (squad_role_section) {
        squad_role_section(app, p);  // the old Players panel's section (needs exposing, see ui_team_pages.h)
    } else {
        role_fallback(app, p);
    }
    card_end();
}

void growth_fallback(App& app, const Table& t, const PlayerRow& p) {
    static int points = 1;
    const Gate gr = lua_gate(app, true, "reveal");
    const Gate gd = lua_gate(app, true, "development");
    if (gated_button("Reveal his data", false, gr, "The game marks him fully scouted: his bio shows the true attributes and potential."))
        app.send(run_cmd("reveal", {{"scope", {{"playerid", p.playerid}}}}), "Reveal " + p.name);
    ImGui::SameLine();
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "Develop to potential (%d)###tpdevpot", p.potential);
    if (gated_button(lbl, false, gd, "Every attribute of his group rises until his overall reaches his potential.")) {
        ask("Develop " + p.name + " to his potential?", p.name + " grows to his potential at once." + kCantUndo, "Develop him",
            run_cmd("development", {{"scope", {{"playerid", p.playerid}}}, {"mode", "to_potential"}}), "Develop " + p.name,
            "Asked: develop " + p.name + " to his potential.");
    }
    form_label("Points", "Added to every attribute of his group (-20 to 20).");
    ImGui::SetNextItemWidth(S(100.0f));
    ImGui::InputInt("##tppoints", &points, 1);
    points = std::max(-20, std::min(points, 20));
    ImGui::SameLine();
    if (gated_button("Add points###tpaddpts", false, gd, "Adds the points to every attribute of his group (and to his overall).") && points != 0) {
        ask("Change " + p.name + "'s attributes?", "Every attribute of his group changes by " + std::to_string(points) + "." + kCantUndo, "Change them",
            run_cmd("development", {{"scope", {{"playerid", p.playerid}}}, {"mode", "add"}, {"delta", points}}), "Growth " + p.name,
            "Asked: " + std::to_string(points) + " points for " + p.name + ".");
    }
    if (p.club > 0) {
        ImGui::SameLine();
        if (gated_button("Reveal his club###tprevclub", false, gr, "Every player of his club fully scouted."))
            app.send(run_cmd("reveal", {{"scope", {{"teamid", p.club}}}}), "Reveal " + p.club_name);
    }
    form_label("New potential", "Sets his potential through the game's growth plan (1 to 99).");
    static int potential = 0;
    ImGui::SetNextItemWidth(S(100.0f));
    ImGui::InputInt("##tppotential", &potential, 1);
    potential = std::max(0, std::min(potential, 99));
    ImGui::SameLine();
    if (gated_button("Set potential###tpsetpot", false, gd, "players.potential: the game's growth aims at it.") && potential > 0)
        app.send(run_cmd("development", {{"scope", {{"playerid", p.playerid}}}, {"mode", "none"}, {"potential", potential}}), "Potential " + p.name);
    field_grid(app, t, p.rec, {"potential", "growthprofile"}, "##tpgrowth", 2);
    // weekly forced growth of this one player (the old panel's list in gui_settings: auto.development.players)
    json& dev = app.gui_settings["auto"]["development"];
    if (!dev.is_object()) dev = json::object();
    const json players = dev.contains("players") && dev["players"].is_array() ? dev["players"] : json::array();
    bool listed = std::find(players.begin(), players.end(), json(p.playerid)) != players.end();
    static int weekly = 1;
    static bool no_decline = false;
    form_label("Points per week", "Added every week while he is below his potential (1 to 5).");
    ImGui::SetNextItemWidth(S(120.0f));
    ImGui::SliderInt("##tpweekly", &weekly, 1, 5);
    ImGui::SameLine();
    ImGui::Checkbox("Does not decline###tpnodecline", &no_decline);
    if (ImGui::Checkbox("Grow him every week###tpweeklyon", &listed)) {
        json out = json::array();
        for (const auto& x : players)
            if (x != json(p.playerid)) out.push_back(x);
        if (listed) out.push_back(p.playerid);
        dev["players"] = out;
        dev["weekly"] = weekly;
        dev["no_decline"] = no_decline;
        dev["enabled"] = !out.empty() || dev.value("user_team", false);
        if (app.save_gui_settings()) app.send({{"op", "boot"}}, "Weekly growth");
        else app.notify("Turbo could not save the setting.", true);
    }
}

void card_growth(App& app, const Table& t, const PlayerRow& p) {
    ++g_state.cards;
    card_begin("##tp_growth", "Growth");
    if (career_dev_tab) career_dev_tab(app, p);  // the old Players panel's growth tab (needs exposing, see ui_team_pages.h)
    else growth_fallback(app, t, p);
    card_end();
}

void player_cards(App& app, int64_t club, const PlayerRow& p) {
    const Table* t = app.db.table("players");
    if (!t || !app.db.table_alive(*t, p.rec)) {
        theme::err_text("The game's data changed (another save loaded?). Press Refresh at the top.");
        return;
    }
    g_state.player = p.playerid;
    const int64_t until = contract_year(app, p);
    char head[160];
    std::snprintf(head, sizeof(head), "%s, overall %d%s%s", position_name(p.position), p.overall, until > 0 ? ", contract until " : "",
                  until > 0 ? std::to_string(until).c_str() : "");
    g_state.headline = head;
    ImGui::TextUnformatted(p.name.c_str());
    ImGui::SameLine();
    theme::muted_text("%s", head);
    ImGui::Spacing();
    card_contract(app, *t, p);
    card_form(app, club, p);
    card_role(app, club, p);
    card_growth(app, *t, p);
}

// ---------------------------------------------------------------- the whole squad
// What the preset's contract step does, in plain words. It is NOT "60 months from today": team_mass long_contract writes the contract END
// YEAR contract_end_year(60) = the start year of the current season (July to June) + 5 (moves.lua:210-215, team_mass.lua:145 via :129), a
// fixed June about 49 to 60 months away, and it writes it even when the player's own contract is longer (that one is shortened).
std::string preset_contract_rule(const GameDate& today) {
    if (!today.valid()) return "Contract runs to June, five seasons after this season began (a longer contract is shortened)";
    const int season_start = today.month >= 7 ? today.year : today.year - 1;
    return "Contract runs to June " + std::to_string(season_start + teampreset::kContractMonths / 12) + " (a longer contract is shortened)";
}

std::string preset_ask_text(const GameDate& today) {
    std::string rule = preset_contract_rule(today);
    rule[0] = static_cast<char>(rule[0] + ('a' - 'A'));  // "contract runs to June 2031 (...)" inside the sentence
    return "Every player of your club gets tight shirts, a squad role (Rotation, or Prospect for young players below the club's middle "
           "rating), very happy morale and blocked transfer offers. Their " + rule +
           "; players on loan to you keep their own club's contract. Nothing else changes. Part of this can't be undone with the Undo button.";
}

// The last texts preset_strip handed to the draw calls (the tests read them through team_preset_drawn_text, ui_team_page40.h)
std::string g_drawn_hint, g_drawn_ask;

// The "My team" preset, on the page above the tabs: the five things it sets (the preset's own list), the button, and the question first.
// Greyed out with the reason for another club (team_preset_apply refuses it as well). Not counted in g_state.cards (the cards of one player).
void preset_strip(App& app, int64_t club) {
    if (!team_preset_apply) return;
    card_begin("##tp_preset", "My team preset");
    const GameDate today = app.today();
    std::string what;
    for (const teampreset::Field& f : teampreset::fields())
        what += (what.empty() ? "" : "; ") + (std::string(f.id) == "contract" ? preset_contract_rule(today) : std::string(f.title));
    what += ". Extra XP development and Does not decline stay off.";
    const std::string ask = preset_ask_text(today);
    g_drawn_hint = what;
    g_drawn_ask = ask;
    hint(what.c_str());
    Gate g = lua_gate(app, true, "team_mass");
    if (!is_own_club(app, club)) g = {false, "It is for your own club only: press My club above."};
    if (gated_button("Apply the My team preset", true, g, "Sets those five things for the whole squad.")) {
        ask_do("Apply the My team preset?", ask, "Apply it", [club](App& a) { return team_preset_apply(a, club); }, "Asked: the My team preset for your club.");
    }
    card_end();
}

void whole_squad(App& app, int64_t club) {
    if (!is_own_club(app, club)) {
        hint("The whole-squad tools work on your own club: roles, morale, offers and the preset exist in the game for it only. "
             "Press My club above.");
        return;
    }
    draw_career_squad_cards(app);  // ui_career_pages.h: form and fitness, squad roles, contracts, growth, squad tools
}

}  // namespace

const char* team_preset_drawn_text(int which) { return (which == 0 ? g_drawn_hint : g_drawn_ask).c_str(); }

// ---------------------------------------------------------------- one player's contract and transfer actions (q3)
void draw_player_contract_actions(App& app) {
    const PlayerRow* p = app.sel_player > 0 ? app.model.player(app.sel_player) : nullptr;
    const Table* t = app.db.table("players");
    if (!p) {
        theme::muted_text("Pick a player first.");
        return;
    }
    if (!t || !app.db.table_alive(*t, p->rec)) {
        theme::err_text("The game's data changed (another save loaded?). Press Refresh at the top.");
        return;
    }
    ImGui::SeparatorText("Contract");
    contract_fields(app, *t, *p);
    if (p->club <= 0) {
        theme::muted_text("He has no club: a contract extension and a move need one.");
    } else {
        ImGui::SeparatorText("Extend the contract");
        team_player_extend_controls(app, p->club);
        ImGui::SeparatorText("Transfer, loan or release");
        team_player_move_controls(app, p->club);
    }
    confirm_popup(app);  // the question those controls ask first (teampage::confirm_popup)
}

static void draw_pitch_squad_view(App& app, int64_t club) {
    const auto squad = squad_of(app, club);
    if (squad.empty()) {
        theme::muted_text("No players found in this squad.");
        return;
    }
    // Select top 11 players by overall rating
    std::vector<const PlayerRow*> best = squad;
    std::sort(best.begin(), best.end(), [](const PlayerRow* a, const PlayerRow* b) {
        return a->overall > b->overall;
    });
    if (best.size() > 11) best.resize(11);

    // Group into positions: GK, DEF, MID, FWD
    std::vector<const PlayerRow*> gks, defs, mids, fwds;
    for (const PlayerRow* p : best) {
        if (p->positions[0] == 0) gks.push_back(p);
        else if (p->positions[0] >= 1 && p->positions[0] <= 8) defs.push_back(p);
        else if (p->positions[0] >= 9 && p->positions[0] <= 19) mids.push_back(p);
        else fwds.push_back(p);
    }
    if (gks.empty()) {
        for (const PlayerRow* p : squad) {
            if (p->positions[0] == 0) {
                gks.push_back(p);
                if (!best.empty()) best.pop_back();
                break;
            }
        }
    }

    const ImVec2 size(ImGui::GetContentRegionAvail().x, S(380.0f));
    ImGui::InvisibleButton("##pitch_canvas", size);
    if (ImDrawList* dl = ImGui::GetWindowDrawList()) {
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();

        // Pitch grass background: deep broadcast turf
        dl->AddRectFilled(p0, p1, IM_COL32(22, 42, 28, 255), S(8.0f));
        dl->AddRect(p0, p1, IM_COL32(40, 80, 50, 255), S(8.0f), 1.5f);

        // Pitch lines: white with 50% opacity
        const ImU32 line_col = IM_COL32(255, 255, 255, 120);
        const float pad = S(16.0f);
        const ImVec2 pitch_min(p0.x + pad, p0.y + pad);
        const ImVec2 pitch_max(p1.x - pad, p1.y - pad);
        dl->AddRect(pitch_min, pitch_max, line_col, 0.0f, 1.5f);

        // Halfway line and center circle
        const float mid_y = (pitch_min.y + pitch_max.y) * 0.5f;
        dl->AddLine(ImVec2(pitch_min.x, mid_y), ImVec2(pitch_max.x, mid_y), line_col, 1.5f);
        dl->AddCircle(ImVec2((pitch_min.x + pitch_max.x) * 0.5f, mid_y), S(35.0f), line_col, 32, 1.5f);

        // Penalty boxes
        const float pw = pitch_max.x - pitch_min.x;
        const float box_w = pw * 0.45f;
        const float box_h = S(50.0f);
        const float box_x0 = pitch_min.x + (pw - box_w) * 0.5f;
        dl->AddRect(ImVec2(box_x0, pitch_min.y), ImVec2(box_x0 + box_w, pitch_min.y + box_h), line_col, 0.0f, 1.5f);
        dl->AddRect(ImVec2(box_x0, pitch_max.y - box_h), ImVec2(box_x0 + box_w, pitch_max.y), line_col, 0.0f, 1.5f);

        // Helper to draw a player node
        auto draw_player_node = [&](const PlayerRow* p, float nx, float ny) {
            if (!p) return;
            const ImVec2 pos(pitch_min.x + (pitch_max.x - pitch_min.x) * nx,
                             pitch_min.y + (pitch_max.y - pitch_min.y) * ny);
            const float r = S(14.0f);
            dl->AddCircleFilled(pos, r, IM_COL32(18, 24, 38, 230));
            dl->AddCircle(pos, r, IM_COL32(45, 212, 191, 255), 24, 2.0f);

            char ovr_str[8];
            std::snprintf(ovr_str, sizeof(ovr_str), "%d", p->overall);
            const ImVec2 ovr_sz = ImGui::CalcTextSize(ovr_str);
            dl->AddText(ImVec2(pos.x - ovr_sz.x * 0.5f, pos.y - ovr_sz.y * 0.5f), IM_COL32(255, 255, 255, 255), ovr_str);

            const ImVec2 name_sz = ImGui::CalcTextSize(p->name.c_str());
            dl->AddText(ImVec2(pos.x - name_sz.x * 0.5f, pos.y + r + S(2.0f)), IM_COL32(240, 240, 240, 240), p->name.c_str());
        };

        // Draw GK (bottom)
        if (!gks.empty()) draw_player_node(gks.front(), 0.50f, 0.88f);

        // Draw Defenders
        const size_t nd = defs.size();
        for (size_t i = 0; i < nd; ++i) {
            float frac = nd == 1 ? 0.5f : (0.18f + 0.64f * float(i) / float(nd - 1));
            draw_player_node(defs[i], frac, 0.68f);
        }
        // Draw Midfielders
        const size_t nm = mids.size();
        for (size_t i = 0; i < nm; ++i) {
            float frac = nm == 1 ? 0.5f : (0.18f + 0.64f * float(i) / float(nm - 1));
            draw_player_node(mids[i], frac, 0.45f);
        }
        // Draw Forwards (top)
        const size_t nf = fwds.size();
        for (size_t i = 0; i < nf; ++i) {
            float frac = nf == 1 ? 0.5f : (0.22f + 0.56f * float(i) / float(nf - 1));
            draw_player_node(fwds[i], frac, 0.20f);
        }
    }
}

// ---------------------------------------------------------------- the page
void draw_team_players(App& app) {
    g_state = TeamPageState{};
    page::Frame frame(app, {"team_players", "Players", "Contract, form, squad role and growth of one player; the tools for a whole squad.", nullptr});
    PageTail tail(app, "team_players");
    if (!app.connected()) {
        g_state.waiting = true;
        not_connected_hint();
        return;
    }
    const int64_t club = club_header(app, "##teamplclub");
    g_state.club = club;
    if (club <= 0) {
        g_state.waiting = true;
        theme::muted_text("Waiting for your career.");
        return;
    }
    g_state.own_club = is_own_club(app, club);
    preset_strip(app, club);

    // 2.0 Clubs & Managers Studio: Transfer budget bar
    const bool is_user_club = g_state.own_club;
    static int s_budget_amount = 100000000;
    static bool s_budget_inited = false;
    const BridgeState& bst = app.bridge.state();
    if (!s_budget_inited && bst.transfer_budget > 0) {
        s_budget_amount = static_cast<int>(std::min<int64_t>(bst.transfer_budget, 2000000000LL));
        s_budget_inited = true;
    }
    ImGui::AlignTextToFramePadding();
    theme::muted_text("Transfer budget:");
    ImGui::SameLine();
    if (!is_user_club) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(S(150.0f));
    ImGui::InputInt("##tp_budget_input", &s_budget_amount, 1000000, 10000000);
    if (s_budget_amount < 0) s_budget_amount = 0;
    ImGui::SameLine();
    if (ImGui::Button("Set###tp_budget_set")) {
        app.send(teampage::run_cmd("transfer_budget", {{"mode", "set"}, {"amount", static_cast<int64_t>(s_budget_amount)}}), "Set budget");
        page::done("team_players", "Transfer budget updated.");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set transfer budget for this career club");
    if (!is_user_club) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        theme::muted_text("(Career club only)");
    }
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##team_pl_tabs")) {
        // 2.22 G-squad: the sortable squad table of 2.14 for the club picked above (any club), first and selected by default
        if (ImGui::BeginTabItem("Squad list###tptabsquadlist")) {
            const bool pitch = x2::state().squad_pitch_club;
            if (ImGui::SmallButton(pitch ? "Table view###tp_view_table" : "Pitch view###tp_view_pitch")) {
                x2::state().squad_pitch_club = !pitch;
            }
            ImGui::SameLine();
            theme::muted_text("%s", pitch ? "Pitch formation view (read-only Best XI)" : "Sortable squad table");
            if (pitch) {
                draw_pitch_squad_view(app, club);
            } else {
                squad_table(app, club);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Player###tptabplayer")) {
            const std::vector<const PlayerRow*> squad = squad_of(app, club);
            log_page_once(app, "players", "Team page Players: club " + std::to_string(club) + " (" + app.model.team_name(club) + "), " +
                                              std::to_string(squad.size()) + " players");
            ImGui::BeginChild("##team_pl_list", ImVec2(S(320.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
            player_list(app, squad);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##team_pl_cards", ImVec2(0, 0));
            const PlayerRow* picked = nullptr;
            for (const PlayerRow* p : squad)
                if (p->playerid == app.sel_player) picked = p;
            if (!picked && !squad.empty()) {  // nobody of this club is picked yet: the best player, so the cards are never empty
                picked = squad.front();
                app.sel_player = picked->playerid;
            }
            if (picked) player_cards(app, club, *picked);
            else theme::muted_text("Pick a player on the left.");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Whole squad###tptabsquad")) {
            whole_squad(app, club);
            ImGui::EndTabItem();
        }
        // 2.22 G-squad (v2.14 capability): the club browser, Mass actions, Job offer and the team's All fields (draw_teams), for every club
        if (ImGui::BeginTabItem("All clubs###tptabclubs")) {
            draw_teams(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

const TeamPageState& team_players_state() { return g_state; }

}  // namespace turbo
