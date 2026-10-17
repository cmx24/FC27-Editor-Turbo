// Native tests (2026-10-17, P213 triage, turbo_dev/research_20261017/P213_TRIAGE.md): the 2.13 shell keeps every proven panel on the
// path a user walks (area tab, then the page strip), every page button is on screen, no page is a dead end, and no page's text names
// a place the revamp removed. 2026-10-16 the installed 2.13.0 build was reported to have broken the live league table edit: the live
// table had moved to Tactics & Gameplay > Standings, the database copy's hint still named the gone "Live standings (game)" view,
// Career tools > Transfers / Job offer said only "not part of this build", the page strip ran off the right edge of a window narrower
// than it (about 1450 px for Career tools at UI size 1), and the native cases reached the live table through request_tab alone.
// Included by test_main.cpp after test_club_page.h; p213_test::ui_cases runs inside the UI block after test_shell_ui.
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
    const bool was_advanced = app.prefs.advanced_mode;
    const int64_t was_team = app.sel_team;

    run_case("P213: the proven panels are where a user clicks them (Simple view): area tab, then the page strip", [&] {
        app.prefs.advanced_mode = false;
        app.sel_team = 7;  // Everton: the club pages and the club panel show a club
        app.go(Page::PlayerEdit);
        ui.frames(3);
        // the live league table (proven 2.2): Tactics & Gameplay > Standings draws the live view (no FCE here: it says so)
        CHECK(ui.open_page("Tactics & Gameplay", "Standings") && app.place() == shell::place_of(Page::Standings), "Tactics & Gameplay > Standings");
        CHECK(ui.find("Try again") != nullptr || ui.find("Reload") != nullptr, "the live standings view is drawn there");
        // the database copy (the old tab's other table) names the live table's page and opens it
        CHECK(ui.open_page("Career tools", "Maintenance") && ui.find("Recalculate points") != nullptr, "Career tools > Maintenance: the database copy");
        CHECK(logged_frame(ui).find("Tactics & Gameplay > Standings") != std::string::npos, "its hint names the live table's page");
        CHECK(ui.click("Open the live table (Tactics & Gameplay > Standings)##dbcopylive"), "the link to the live table");
        ui.frames(2);
        CHECK(app.place() == shell::place_of(Page::Standings) && (ui.find("Try again") || ui.find("Reload")), "the link lands on the live view");
        // Job offer: until the Squad track exports its panel, the page says where it is and opens it
        CHECK(ui.open_page("Career tools", "Job offer"), "Career tools > Job offer");
        CHECK(ui.click("Open Squad & Players > Squad##squadgo"), "Job offer: the way to the club panel");
        ui.frames(2);
        CHECK(app.place() == shell::place_of(Page::Squad) && ui.find("Job offer") != nullptr && ui.find("Mass actions") != nullptr,
              "Job offer: Squad & Players > Squad, the club panel with its Job offer / Transfers / Mass actions tabs");
        // Transfers: until then the old Turbo Tools panel (integration 721bf59), not a dead end
        CHECK(ui.open_page("Career tools", "Transfers") && ui.find("Your squad") != nullptr, "Career tools > Transfers: the old Turbo Tools panel");
        // the other proven panels, each by its own primary control
        CHECK(ui.open_page("Squad & Players", "Player edit") && ui.find("##psearch") != nullptr, "Squad & Players > Player edit: the players list");
        CHECK(ui.open_page("Club & Kits", "Kits") && ui.find("Apply kit") != nullptr, "Club & Kits > Kits");
        CHECK(ui.open_page("Tactics & Gameplay", "Tactic presets") && ui.find("Apply preset##tpload") != nullptr, "Tactics & Gameplay > Tactic presets");
        CHECK(ui.open_page("Tactics & Gameplay", "Gameplay presets") && ui.find("Apply preset##gpload") != nullptr, "Tactics & Gameplay > Gameplay presets");
        CHECK(ui.open_page("Career tools", "Changes & undo") && ui.find("Kept player edits (every career load)") != nullptr, "Career tools > Changes & undo");
        CHECK(ui.open_page("Career tools", "CMTracker") && ui.find("Season pack...##page") != nullptr, "Career tools > CMTracker: the season pack");
        CHECK(ui.click("Settings", "FC 27 LE Turbo", "##shell") && ui.find("Preferences") != nullptr, "Settings (one page, no strip)");
    });

    run_case("P213: every page button is on screen; no page is a dead end or names a place the revamp removed (Simple and Advanced views)", [&] {
        for (bool adv : {false, true}) {
            app.prefs.advanced_mode = adv;
            ui.frames(2);
            for (int a = 0; a < shell::kAreaCount; ++a) {
                const Area area = static_cast<Area>(a);
                if (!shell::area_visible(area, adv)) continue;
                const auto& strip = shell::pages_of(area);
                app.go(area, shell::default_page(area));
                ui.frames(2);
                for (Page p : strip) {
                    const std::string where = std::string(adv ? "Advanced view, " : "Simple view, ") + shell::area_label(area) + " > " + shell::page_label(p);
                    // the window is the default 1180 px wide here: a long strip wraps instead of running off the right edge
                    if (strip.size() > 1) CHECK(ui.find(shell::page_label(p), "FC 27 LE Turbo", "##pages") != nullptr, where + ": its button is on screen");
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

    app.prefs.advanced_mode = was_advanced;
    app.sel_team = was_team;
    app.go(Page::PlayerEdit);
    ui.frames(3);
}

}  // namespace p213_test
