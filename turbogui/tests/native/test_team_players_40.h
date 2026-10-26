// Native tests (4.0 S8, brief 4.0-gui-pages "S8 - Team > Players + Transfers"): the two Team pages in the page template, the "My team
// preset" button on Team > Players, and the one-player contract actions the player card hosts (q3).
// Included by test_main.cpp after test_page_team.h; s8_cases is called at the end of page_team_test::ui_cases (the same UI block, the
// same simulated game, the same helpers). Cases are named "Team-pages 4.0: ..." so TURBO_TEST_ONLY="team-pages 4.0" runs them alone.
//
// What is proven here:
//   A. both pages construct page::Frame with the ids and titles of the brief (team_players / Players, team_transfers / Transfers):
//      no Apply, the bar has Undo and Back;
//   B. the preset button is on Team > Players, reachable from every tab; it asks first (Cancel sends nothing); "Apply it" sends the
//      old command (team_mass, the four Lua actions, role_save false, the club's id); the page's one status line says it was sent;
//      on another club the button exists and does nothing; the preset still has exactly Cassio's five things, and the two growth
//      boxes (Extra XP, Does not decline) default OFF;
//   C. draw_player_contract_actions (q3) shows the contract fields and the extension, transfer and release controls of the picked
//      player and the extension writes the end year;
//   D. a contract extension on the Transfers page says "Done: Extended ..." in the template's status line.
// The functions the 4.0 tip adds are declared weak (the precedent of squad_role_section): on the old code they are null, the
// assertions fail, and the test builds and runs there (that is the fail-old run of the G2).
#pragma once
#include "core/team_preset.h"
#include "ui/ui_page.h"
#include "ui/ui_team_pages.h"

namespace turbo {
void draw_player_contract_actions(App& app) TURBO_WEAK;  // ui_team_players.cpp (4.0 S8, q3)
const char* team_preset_drawn_text(int which) TURBO_WEAK;  // ui_team_page40.h (4.0 S8 fix 1: the texts the preset card drew)
}

namespace page_team_test {

using namespace turbo;

static void page_player_card(App& app) {
    if (draw_player_contract_actions) draw_player_contract_actions(app);
    else ImGui::TextDisabled("draw_player_contract_actions is not built in");
}

static bool bar_has(const page::Record* r, const char* id) {
    if (!r) return false;
    for (const std::string& b : r->bar)
        if (b == id) return true;
    return false;
}

static void s8_cases(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, int64_t mine) {
    std::string why;
    const char* kPreset = "Apply the My team preset";

    run_case("Team-pages 4.0: both pages are drawn in the page template (ids team_players / team_transfers, no Apply, Undo and Back)", [&] {
        app.sel_team = mine;
        app.sel_player = 1005;
        team_move_inputs() = TeamMoveInputs{};
        team_move_inputs().player = 1005;
        page::reset_for_tests();
        show(ui, draw_team_players);
        const page::Record* r = page::last_record("team_players");
        CHECK(r != nullptr, "Team > Players constructs a page::Frame with the id team_players");
        if (r) {
            CHECK(r->title == "Players", "the title is Players: " + r->title);
            CHECK(!r->has_apply, "Team > Players has no Apply (its buttons act at once or ask first)");
            CHECK(bar_has(r, "###pg_undo") && bar_has(r, "###pg_back"), "the bar has Undo and Back");
        }
        CHECK(has_item(ui, "###pg_back", "##teampage_test"), "the Back button of the template is on the page");
        hide(ui);
        show(ui, draw_team_transfers);
        r = page::last_record("team_transfers");
        CHECK(r != nullptr, "Team > Transfers constructs a page::Frame with the id team_transfers");
        if (r) {
            CHECK(r->title == "Transfers", "the title is Transfers: " + r->title);
            CHECK(!r->has_apply, "Team > Transfers has no Apply");
            CHECK(bar_has(r, "###pg_undo") && bar_has(r, "###pg_back"), "the bar has Undo and Back");
        }
        CHECK(team_transfers_state().cards == 6, fmt("the six cards of Transfers are still there: %d", team_transfers_state().cards));
        hide(ui);
    });

    run_case("Team-pages 4.0: the My team preset is one button on Team > Players, on every tab; it asks first and sends the five-thing command", [&] {
        // Cassio's five things, from the preset's own list; the two growth boxes stay off
        const auto& f = teampreset::fields();
        CHECK(f.size() == 5, fmt("the preset has five fields: %zu", f.size()));
        const std::vector<std::string> want = {"jersey_fit", "contract", "squad_role", "morale", "block_offers"};
        for (size_t i = 0; i < f.size() && i < want.size(); ++i)
            CHECK(want[i] == f[i].id, std::string("preset field ") + std::to_string(i) + " is " + want[i] + ": " + f[i].id);
        CHECK(!teampreset::kExtraXpDefault && !teampreset::kNoDeclineDefault, "Extra XP development and Does not decline default OFF");

        app.sel_team = mine;
        app.sel_player = 1005;
        show(ui, draw_team_players);
        for (const char* tab : {"Squad list###tptabsquadlist", "Player###tptabplayer", "Whole squad###tptabsquad", "All clubs###tptabclubs"}) {
            CHECK(ui.click(tab, "##teampage_test"), std::string("the tab ") + tab);
            ui.frames(4);
            CHECK(has_item(ui, kPreset, "##teampage_test"), std::string("the preset button is on the tab ") + tab);
            CHECK(count_items("##tp_preset") >= 1, std::string("the preset card is on the tab ") + tab);
        }
        CHECK(ui.click("Squad list###tptabsquadlist", "##teampage_test"), "back to the first tab");
        ui.frames(4);

        // every jersey value of my club, to put the world back
        const Table* pt = app.db.table("players");
        std::vector<std::pair<int64_t, int64_t>> fit_before;
        if (pt && pt->has("jerseyfit"))
            for (const PlayerRow& p : app.model.players())
                if (p.club == mine) fit_before.push_back({p.playerid, app.db.get_int(*pt, p.rec, "jerseyfit", -1)});
        CHECK(!fit_before.empty(), "my club has players with a jersey fit value");

        json j = ask_and_confirm(app, ui, mem, kMb, kPreset, "##teampage_test", "Apply it", why);
        CHECK(why.empty(), "the preset asks first and sends after the confirm: " + why);
        CHECK(j.value("module", "") == "team_mass", "module team_mass: " + j.dump());
        CHECK(j["overrides"].value("teamid", 0LL) == mine, "for my club: " + j["overrides"].dump());
        CHECK(j["overrides"].contains("actions") && j["overrides"]["actions"] == json::array({"long_contract", "squad_roles", "morale", "block_offers"}),
              "exactly the four Lua actions: " + j["overrides"].dump());
        CHECK(j["overrides"].value("role_save", true) == false, "no saved role rule");
        const std::string st = page::status_text("team_players");
        CHECK(st.rfind("Done: sent to the game: the My team preset", 0) == 0, "the page's one status line says it: " + st);

        // put the jersey fit of my club back (one undo step per player the preset changed)
        int restored = 0;
        if (pt)
            for (const auto& pb : fit_before) {
                const PlayerRow* p = app.model.player(pb.first);
                if (p && app.db.get_int(*pt, p->rec, "jerseyfit", -1) != pb.second && app.undo(pb.first)) ++restored;
            }
        ui.frames(2);
        int still = 0;
        if (pt)
            for (const auto& pb : fit_before) {
                const PlayerRow* p = app.model.player(pb.first);
                if (p && app.db.get_int(*pt, p->rec, "jerseyfit", -1) != pb.second) ++still;
            }
        CHECK(still == 0, fmt("the world is back as it was: %d players differ after %d undo steps", still, restored));
        app.mailbox->cancel();

        // another club: the button is there and does nothing (the preset is for the user's club only)
        app.sel_team = 7;
        ui.frames(4);
        CHECK(has_item(ui, kPreset, "##teampage_test"), "the button is shown for another club too");
        CHECK(does_nothing(app, ui, kPreset, "##teampage_test"), "another club: no question, nothing sent");
        app.sel_team = mine;
        ui.frames(3);
        hide(ui);
    });

    run_case("Team-pages 4.0: the My team preset card says what its contract step does - an end year in June, not '60 months' (S8 fix 1, D3)", [&] {
        // team_mass long_contract writes contractvaliduntil = season start year + 5 (moves.lua:210-215, team_mass.lua:145), even when the
        // player's own contract is longer. The card must say that, from the texts it handed to the draw calls on the last frame.
        CHECK(team_preset_drawn_text != nullptr, "ui_team_players.cpp exports team_preset_drawn_text (the texts of the preset card)");
        app.sel_team = mine;
        app.sel_player = 1005;
        show(ui, draw_team_players);
        ui.frames(2);
        const std::string hint = team_preset_drawn_text ? team_preset_drawn_text(0) : std::string();
        const GameDate today = app.today();
        const std::string want_rule =
            today.valid() ? "June " + std::to_string((today.month >= 7 ? today.year : today.year - 1) + 5) : std::string("June,");
        CHECK(!hint.empty(), "the preset hint was drawn");
        CHECK(hint.find("60 months") == std::string::npos && hint.find("60-month") == std::string::npos && hint.find("by 60") == std::string::npos,
              "the hint does not promise 60 months: " + hint);
        CHECK(hint.find("Contract runs to " + want_rule) != std::string::npos, "the hint says the end-year rule (" + want_rule + "): " + hint);
        CHECK(hint.find("a longer contract is shortened") != std::string::npos, "the hint says a longer contract is shortened: " + hint);
        // the question the button opens says the same
        const std::string ask = team_preset_drawn_text ? team_preset_drawn_text(1) : std::string();
        CHECK(!ask.empty() && ask.find("60-month") == std::string::npos && ask.find("60 months") == std::string::npos, "the question does not promise 60 months: " + ask);
        CHECK(ask.find("contract runs to " + want_rule) != std::string::npos && ask.find("a longer contract is shortened") != std::string::npos,
              "the question says the end-year rule: " + ask);
        // the other four things are still listed
        for (const char* t : {"Tight shirts", "Squad roles", "Very happy morale", "Offers for your players blocked"})
            CHECK(hint.find(t) != std::string::npos, std::string("the hint still lists: ") + t);
        hide(ui);
    });

    run_case("Team-pages 4.0: draw_player_contract_actions - the contract fields, the extension, the transfer and release controls of the picked player", [&] {
        CHECK(draw_player_contract_actions != nullptr, "ui_team_players.cpp exports draw_player_contract_actions (q3)");
        app.sel_team = mine;
        app.sel_player = 1005;
        team_move_inputs() = TeamMoveInputs{};
        team_move_inputs().player = 1005;
        show(ui, page_player_card);
        CHECK(has_item(ui, "Extend###tpext1005", "##teampage_test"), "Extend is there for the picked player");
        CHECK(has_item(ui, "Transfer###tpmvtransfer", "##teampage_test"), "Transfer is there");
        CHECK(has_item(ui, "Release###tpmvrelease", "##teampage_test"), "Release is there");
        {   // the field editors draw their input as "##v" under PushID(field name), the title as text
            int editors = 0;
            for (const auto& kv : g_items)
                if (kv.second.frame == g_frame && kv.second.window.find("##teampage_test") != std::string::npos && kv.second.label == "##v") ++editors;
            CHECK(editors >= 2, fmt("the contract fields (end year, wage, release clause, retiring) are there: %d editors", editors));
        }
        const Table* pt = app.db.table("players");
        const PlayerRow* ten = app.model.player(1005);
        CHECK(pt != nullptr && ten != nullptr, "the world has player 1005");
        if (pt && ten) {
            const int64_t before = app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0);
            team_move_inputs().extend_pick = 0;  // 1 year
            ui.frames(2);
            if (ui.click("Extend###tpext1005", "##teampage_test")) {
                ui.frames(3);
                const int64_t after = app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0);
                CHECK(after == before + 1, fmt("Extend raises the end year by 1: %lld -> %lld", static_cast<long long>(before), static_cast<long long>(after)));
                app.mailbox->cancel();
                CHECK(app.undo(1005), "the Undo button has a step for him");
                ui.frames(2);
                CHECK(app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0) == before, "Undo brought the end year back");
            } else {
                CHECK(false, "Extend can be clicked");
            }
        }
        // a player with no club has no extension and no move
        hide(ui);
        app.sel_player = 1005;
    });

    run_case("Team-pages 4.0: a contract extension on Team > Transfers says Done in the template's one status line", [&] {
        app.sel_team = mine;
        app.sel_player = 1005;
        TeamMoveInputs& in = team_move_inputs();
        in = TeamMoveInputs{};
        in.player = 1005;
        in.extend_pick = 0;  // 1 year
        page::reset_for_tests();
        show(ui, draw_team_transfers);
        scroll_bottom(ui, true);
        const Table* pt = app.db.table("players");
        const PlayerRow* ten = app.model.player(1005);
        if (pt && ten) {
            const int64_t before = app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0);
            CHECK(ui.click("Extend###tpext1005", "##tt_extend_"), "Extend, 1 year");
            ui.frames(3);
            const std::string st = page::status_text("team_transfers");
            CHECK(st.rfind("Done: Extended ", 0) == 0, "the status line reads Done: Extended ...: " + st);
            CHECK(st.find(std::to_string(before + 1)) != std::string::npos, "it names the new end year " + std::to_string(before + 1) + ": " + st);
            app.mailbox->cancel();
            CHECK(app.undo(1005), "the Undo button has a step for him");
            ui.frames(2);
            CHECK(app.db.get_int(*pt, ten->rec, "contractvaliduntil", 0) == before, "Undo brought the end year back");
        }
        in = TeamMoveInputs{};
        hide(ui);
    });

    (void)mem;
    (void)kMb;
}

}  // namespace page_team_test
