// Native tests (brief 3.0-shell, track B): Players > Player, the header card above the tabs (ui/ui_player_header.cpp) and the five tabs of
// draw_player_edit (ui/ui_pages_216.cpp). Included by test_main.cpp after test_ia_222.h; player_header_test::ui_cases runs inside the UI block
// (a real App, the page drawn through the shell's new-page registry). Cases are named "Players 3.0: ..." so TURBO_TEST_ONLY="players 3.0" runs
// them alone.
#pragma once
#include <string>

#include "ui/ui_player_header.h"
#include "ui/ui_shell.h"

namespace player_header_test {

using namespace turbo;

static void ui_cases(App& app, Ui& ui) {
    using shell::Page;
    const int64_t was_sel = app.sel_player;
    const int was_tab = shell::player_edit_tab();

    run_case("Players 3.0: header card shows the selected player's name and OVR above the five tabs", [&] {
        if (app.busy()) ui.click("Cancel");
        shell::player_edit_tab() = 0;
        app.sel_player = 1001;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        const PlayerRow* p = app.model.player(1001);
        CHECK(p != nullptr, "fixture player 1001 is in the model");
        const PlayerHeaderState& st = player_header_state();
        CHECK(st.playerid == 1001, "the card is for the selected player: " + std::to_string(st.playerid));
        if (p) {
            CHECK(!st.name.empty() && st.name == p->name, "the card draws his name: '" + st.name + "'");
            CHECK(st.overall == p->overall && st.overall > 0, "the card draws his OVR: " + std::to_string(st.overall));
            CHECK(st.potential == p->potential, "the card draws his POT");
        }
        CHECK(ui.find("Profile###pe_profile") != nullptr, "tab Profile");
        CHECK(ui.find("Attributes & roles###pe_ability") != nullptr, "tab Attributes & roles");
        CHECK(ui.find("Looks###pe_appearance") != nullptr, "tab Looks");
        CHECK(ui.find("Callname###pe_callname") != nullptr, "tab Callname");
        CHECK(ui.find("Everything (classic)###pe_allfields") != nullptr, "tab Everything (classic)");
        CHECK(ui.click("###pe_callname"), "click Callname");
        ui.frames(4);
        CHECK(shell::player_edit_tab() == 3, "Callname is tab 3");
        CHECK(player_header_state().playerid == 1001, "the card stays on every tab");
        app.sel_player = 0;
        ui.frames(3);
        CHECK(player_header_state().playerid == 0, "no player picked: no card");
    });

    shell::player_edit_tab() = was_tab;
    app.sel_player = was_sel;
    app.go(Page::PlayerEdit);
    ui.frames(3);
}

}  // namespace player_header_test
