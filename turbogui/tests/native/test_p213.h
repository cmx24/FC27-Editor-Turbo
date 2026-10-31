// Native tests (2026-10-17, P213 triage, turbo_dev/research_20261017/P213_TRIAGE.md): the 2.13 shell keeps every proven panel on the
// path a user walks (area tab, then the page strip), every page button is on screen, no page is a dead end, and no page's text names
// a place the revamp removed. 2026-10-16 the installed 2.13.0 build was reported to have broken the live league table edit: the live
// table had moved to Tactics & Gameplay > Standings, the database copy's hint still named the gone "Live standings (game)" view,
// Career tools > Transfers / Job offer said only "not part of this build", the page strip ran off the right edge of a window narrower
// than it (about 1450 px for Career tools at UI size 1), and the native cases reached the live table through request_tab alone.
// Included by test_main.cpp after test_club_page.h; p213_test::ui_cases runs inside the UI block after test_shell_ui.
// 2026-10-31 (5.0c menus step 6): the Lab / Advanced view, "Use the old pages" (the old Turbo Tools panel as the Transfers fallback, the
// signposts), the Callnames page, the classic tab and the card view are gone; the cases that switched or asserted them went with them.
#pragma once
#include "ui/ui_shell.h"

namespace p213_test {

using namespace turbo;
using turbo::shell::Area;
using turbo::shell::Page;

// Phrases that name a place the revamp removed outright (the Competitions tab and its views, the Turbo Tools and Status tabs):
// a hint that says one sends the user nowhere. The old Players / Teams / Managers names stay until the Squad track (T2) replaces
// the panels that still carry them.
static const char* const kRemovedPlaces[] = {"Live standings (game)", "Turbo Tools >", "Competitions >", "Status tab"};

static void ui_cases(App& app, Ui& ui) {
    const int64_t was_team = app.sel_team;

    run_case("chrome (2.16 step 2): slim status line and a Changes button; Refresh, the key and the version only on Settings", [&] {
        app.go(Page::PlayerEdit);
        ui.frames(3);
        CHECK(ui.find("###changes", "FC 27 LE Turbo") != nullptr, "the Changes button is in the top bar");
        CHECK(ui.find("Refresh", "FC 27 LE Turbo") == nullptr && ui.find("###advview", "FC 27 LE Turbo") == nullptr &&
                  ui.find("##hkpick", "FC 27 LE Turbo", "bar") == nullptr && ui.find("###reload", "FC 27 LE Turbo") == nullptr,
              "no Refresh, Lab switch or key control in the top bar");
        const std::string txt = logged_frame(ui);
        CHECK(txt.find("Simple view:") == std::string::npos, "no Simple view banner");
        CHECK(txt.find("Connected") != std::string::npos && txt.find("career loaded") != std::string::npos && txt.find(" players |") == std::string::npos &&
                  txt.find(" teams |") == std::string::npos && txt.find(std::string("v") + kGuiVersion) == std::string::npos,
              "the status line says Connected and career loaded, without counts or the version: " + txt.substr(0, 200));
        CHECK(ui.click("###changes", "FC 27 LE Turbo"), "click Changes");
        ui.frames(3);
        CHECK(app.place() == shell::place_of(Page::ChangesUndo), "Changes opens Career tools > Changes & undo");
        app.go(Page::Settings);
        ui.frames(3);
        CHECK(ui.find("###reload") != nullptr && ui.find("##hkpick", "", "status") != nullptr, "Settings has Reload from game and the show/hide key");
        CHECK(ui.find("###advview") == nullptr && ui.find_scrolling("###advview", "", "") == nullptr, "no Show the Lab switch anywhere on Settings (deleted 2026-10-31)");
        CHECK(logged_frame(ui).find(std::string("Turbo ") + kGuiVersion) != std::string::npos, "Settings shows the version");
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("honest view (2.16 step 3): the tags and notes say what is not tested", [&] {
        CHECK(ui.open_page("Career", "Season pack"), "Career tools > CMTracker");
        ui.frames(2);
        CHECK(ui.find("Season pack...##page") != nullptr && logged_frame(ui).find("Not tested yet") != std::string::npos, "the Season pack carries the tag: Not tested yet");
        CHECK(ui.open_page("Tactics", "Presets"), "Tactics & Gameplay > Tactic presets");
        ui.frames(2);
        CHECK(logged_frame(ui).find("Saved to your club's tactic and read back. Its effect in matches is not tested yet.") != std::string::npos,
              "the one-line proof note");
    });

    run_case("navigation (2.16 step 4): the areas; the new pages are real pages; Standings leads Career", [&] {
        app.go(Page::PlayerEdit);
        ui.frames(3);
        for (const char* area : {"Home", "Players", "Looks", "Club", "Tactics", "Game settings", "Career", "Turbo settings"})  // 3.0 rail names (ui_shell.cpp area_label): Appearance Editor is now Looks, Team is Club
            CHECK(ui.find(area, "FC 27 LE Turbo", "##shell") != nullptr, std::string("area tab: ") + area);
        CHECK(ui.find("Lab", "FC 27 LE Turbo", "##shell") == nullptr, "no Lab tab (deleted 2026-10-31)");
        // Team sheet & bench is a real page (its signpost went with ui_nav_pages.cpp)
        CHECK(ui.open_page("Tactics", "Team sheet & bench") && app.place() == shell::place_of(Page::TeamSheet), "Tactics > Team sheet & bench opens");
        CHECK(ui.find("###navgo") == nullptr && ui.find("###pg_back") != nullptr, "Team sheet & bench is a framed page, not a signpost");
        // Career opens on Standings
        CHECK(shell::default_page(shell::Area::Career) == Page::Standings && shell::pages_of(shell::Area::Career).size() == 5, "Career: five pages, Standings first");
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("wiring (2.16): every page registered as a new page draws in its place", [&] {
        int drawn = 0;
        for (int i = 1; i < shell::kPageCount; ++i) {
            const Page p = static_cast<Page>(i);
            if (!shell::registered_new_page(p) || !shell::page_visible(p)) continue;
            app.go(shell::area_of(p), p);
            ui.frames(4);  // a frame that asserts in ImGui throws and fails the case
            CHECK(app.place() == shell::place_of(p), std::string("opens: ") + shell::area_label(shell::area_of(p)) + " > " + shell::page_label(p));
            ++drawn;
        }
        CHECK(drawn >= 10, "the new pages are wired: " + std::to_string(drawn) + " page draws");
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("P213: the proven panels are where a user clicks them: area tab, then the page strip", [&] {
        app.sel_team = 7;  // Everton: the club pages and the club panel show a club
        app.go(Page::PlayerEdit);
        ui.frames(3);
        // the live league table (proven 2.2): Career > Standings draws the live view (no FCE here: it says so)
        CHECK(ui.open_page("Career", "Standings") && app.place() == shell::place_of(Page::Standings), "Career > Standings");
        CHECK(ui.find("Try again") != nullptr || ui.find("Reload") != nullptr, "the live standings view is drawn there");
        // Manager jobs is the career page (ui_career_page.cpp draw_career_manager_jobs); the old pointer panel went with "Use the old pages"
        CHECK(ui.open_page("Career", "Manager jobs") && ui.find("###squadgo") == nullptr, "Career > Manager jobs: the career page, no pointer");
        // the club panel with its Job offer / Transfers / Mass actions tabs: Club > Squad > All clubs (2.22 G-squad, ui_team_players.cpp:302), reached through the
        // strip as a user does
        CHECK(ui.open_page("Club", "Squad") && app.place() == shell::place_of(Page::TeamPlayers), "Club > Squad");
        CHECK(ui.click("All clubs###tptabclubs"), "Club > Squad: the All clubs tab");
        ui.frames(3);
        CHECK(ui.find("Job offer") != nullptr && ui.find("Mass actions") != nullptr,
              "Club > Squad > All clubs: the club panel with its Job offer / Transfers / Mass actions tabs");
        CHECK(ui.click("Squad list###tptabsquadlist"), "Club > Squad: back on the Squad list tab (ImGui keeps the chosen tab; the squad table cases expect to open on it)");
        ui.frames(2);
        // Transfers & money: the career page (draw_career_transfers_money), not a dead end
        CHECK(ui.open_page("Career", "Money & transfers") && app.place() == shell::place_of(Page::Transfers), "Career > Transfers & money opens");
        // the other proven panels, each by its own primary control
        CHECK(ui.open_page("Players", "Player") && ui.find("##psearch") != nullptr, "Squad & Players > Player edit: the players list");
        CHECK(ui.open_page("Club", "Kits") && ui.find("Apply kit") != nullptr, "Club & Kits > Kits");
        CHECK(ui.open_page("Tactics", "Presets") && ui.find("Apply preset##tpload") != nullptr, "Tactics & Gameplay > Tactic presets");
        // 3.0.1 GUI30K: the Tactics strip has no Game sliders button (10-28: Game settings & CPU sliders has its own area)
        CHECK(!ui.open_page("Tactics", "Game sliders") && ui.find("Apply preset##gpload") == nullptr, "Tactics: no Game sliders page in the strip");
        CHECK(ui.open_page("Career", "Changes & undo") && ui.find("###keptedits") != nullptr, "Career tools > Changes & undo");
        CHECK(ui.open_page("Career", "Season pack") && ui.find("Season pack...##page") != nullptr, "Career tools > CMTracker: the season pack");
        CHECK(ui.click("Turbo settings", "FC 27 LE Turbo", "##shell") && ui.find("Look and feel") != nullptr, "Settings (one page, no strip)");
    });

    run_case("P213: every page button is on screen; no page is a dead end or names a place the revamp removed", [&] {
        {
            ui.frames(2);
            for (int a = 0; a < shell::kAreaCount; ++a) {
                const Area area = static_cast<Area>(a);
                if (!shell::area_visible(area)) continue;
                const auto& strip = shell::pages_of(area);
                app.go(area, shell::default_page(area));
                ui.frames(2);
                for (Page p : strip) {
                    const std::string where = std::string(shell::area_label(area)) + " > " + shell::page_label(p);
                    // the window is the default 1180 px wide here: a long strip wraps instead of running off the right edge
                    // 3.0.1 GUI30K: Game sliders is left out of the Tactics strip (10-28: the page is Game settings & CPU sliders, the only page of its area)
                    if (p == Page::GameplayPresets) CHECK(ui.find(shell::page_label(p), "FC 27 LE Turbo", "##pages") == nullptr, where + ": no button in the strip");
                    else if (strip.size() > 1) CHECK(ui.find(shell::page_label(p), "FC 27 LE Turbo", "##pages") != nullptr, where + ": its button is on screen");
                    app.go(area, p);
                    ui.frames(2);
                    const std::string txt = logged_frame(ui);
                    CHECK(app.place() == shell::place_of(p), where + ": opened");
                    CHECK(txt.find("panel is not part of this build") == std::string::npos, where + ": not a dead end");
                    for (const char* gone : kRemovedPlaces)
                        CHECK(txt.find(gone) == std::string::npos, where + ": names a removed place: " + gone);
                }
            }
        }
    });

    app.sel_team = was_team;
    app.go(Page::PlayerEdit);
    ui.frames(3);
}

}  // namespace p213_test
