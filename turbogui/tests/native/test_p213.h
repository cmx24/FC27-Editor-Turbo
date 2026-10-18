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

    run_case("chrome (2.16 step 2): slim status line and a Changes button; Refresh, the key, the version and the Advanced switch only on Settings", [&] {
        app.prefs.advanced_mode = false;
        app.go(Page::PlayerEdit);
        ui.frames(3);
        CHECK(ui.find("###changes", "FC 27 LE Turbo") != nullptr, "the Changes button is in the top bar");
        CHECK(ui.find("Refresh", "FC 27 LE Turbo") == nullptr && ui.find("###advview", "FC 27 LE Turbo") == nullptr &&
                  ui.find("##hkpick", "FC 27 LE Turbo", "bar") == nullptr && ui.find("###reload", "FC 27 LE Turbo") == nullptr,
              "no Refresh, Advanced switch or key control in the top bar");
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
        CHECK(ui.find("###reload") != nullptr && ui.find("###advview") != nullptr && ui.find("##hkpick", "", "status") != nullptr,
              "Settings has Reload from game, the Advanced switch and the show/hide key");
        CHECK(logged_frame(ui).find(std::string("Turbo ") + kGuiVersion) != std::string::npos, "Settings shows the version");
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("honest Simple view (2.16 step 3): no Bulk edit / Delete / Exports on the Transfers fallback in Simple; the tags and notes say what is not tested", [&] {
        auto drawn = [&](const char* label) {  // an item submitted in the last frame, on screen or scrolled out of view
            for (const auto& kv : g_items)
                if (kv.second.frame == g_frame && kv.second.label == label) return true;
            return false;
        };
        app.prefs.advanced_mode = false;
        CHECK(ui.open_page("Career", "Transfers & money"), "Career tools > Transfers");
        ui.frames(3);
        CHECK(drawn("Safety") && drawn("Your squad"), "the Transfers fallback still has its own tools");
        CHECK(!drawn("Bulk edit players") && !drawn("Database maintenance") && !drawn("Exports (CSV in turbo_output)"),
              "Simple view: no Bulk edit, no Delete (Database maintenance), no Exports on the Transfers fallback");
        app.prefs.advanced_mode = true;
        ui.frames(3);
        CHECK(drawn("Bulk edit players") && drawn("Database maintenance") && drawn("Exports (CSV in turbo_output)"),
              "Advanced view: the sections are there");
        app.prefs.advanced_mode = false;
        CHECK(ui.open_page("Career", "Season pack"), "Career tools > CMTracker");
        ui.frames(2);
        CHECK(ui.find("Season pack...##page") != nullptr && logged_frame(ui).find("Not tested yet") != std::string::npos, "the Season pack carries the tag: Not tested yet");
        CHECK(ui.open_page("Tactics", "Tactic presets"), "Tactics & Gameplay > Tactic presets");
        ui.frames(2);
        CHECK(logged_frame(ui).find("Saved to your club's tactic and read back. Its effect in matches is not tested yet.") != std::string::npos,
              "the one-line proof note");
    });

    run_case("navigation (2.16 step 4): six areas and Advanced; the new pages are signposts to where the feature is; Standings leads Career; nothing was dropped", [&] {
        auto drawn = [&](const char* label) {  // an item submitted in the last frame, on screen or scrolled out of view
            for (const auto& kv : g_items)
                if (kv.second.frame == g_frame && kv.second.label == label) return true;
            return false;
        };
        app.prefs.advanced_mode = false;
        app.go(Page::PlayerEdit);
        ui.frames(3);
        for (const char* area : {"Players", "Faces & looks", "Kits & club", "Tactics", "Career", "Settings"})
            CHECK(ui.find(area, "FC 27 LE Turbo", "##shell") != nullptr, std::string("area tab: ") + area);
        CHECK(ui.find("Advanced", "FC 27 LE Turbo", "##shell") == nullptr, "no Advanced tab in Simple");
        struct Sign { const char* area; const char* page; Page want; };
        for (const Sign& s : {Sign{"Players", "Create a player", Page::CreatePlayer}, Sign{"Players", "Commentary names", Page::CommentaryNames},
                              Sign{"Faces & looks", "Faces & hair", Page::FacesHair}, Sign{"Faces & looks", "Body, tattoos & gear", Page::BodyGear}}) {
            CHECK(ui.open_page(s.area, s.page) && app.place() == shell::place_of(s.want), std::string("opens: ") + s.area + " > " + s.page);
            CHECK(ui.find("###navgo") != nullptr, std::string("a button that goes to the feature: ") + s.page);
            CHECK(ui.click("###navgo") && (ui.frames(2), app.place() == shell::place_of(Page::PlayerEdit)), std::string("it opens Edit a player: ") + s.page);
        }
        // Team sheet & bench: Simple goes to Settings (where the Advanced switch is), Advanced to the experimental tactics
        CHECK(ui.open_page("Tactics", "Team sheet & bench"), "Tactics > Team sheet & bench");
        CHECK(ui.click("###navgo") && (ui.frames(2), app.place() == shell::place_of(Page::Settings)), "Simple: it opens Settings");
        app.prefs.advanced_mode = true;
        CHECK(ui.open_page("Tactics", "Team sheet & bench"), "Tactics > Team sheet & bench (Advanced)");
        CHECK(ui.click("###navgo") && (ui.frames(2), app.place() == shell::place_of(Page::AdvTactics)), "Advanced: it opens Advanced > Tactics");
        app.prefs.advanced_mode = false;
        // Career opens on Standings; the old Scouting / Budget / Bans pages are sections of Transfers & money
        CHECK(shell::default_page(shell::Area::Career) == Page::Standings && shell::pages_of(shell::Area::Career).size() == 5, "Career: five pages, Standings first");
        CHECK(ui.open_page("Career", "Transfers & money"), "Career > Transfers & money");
        ui.frames(3);
        CHECK(drawn("Scouting, development and youth academy") && drawn("Your club: transfer budget") && drawn("Transfer bans"), "the scouting, budget and bans sections are on Transfers & money");
        // Advanced: Data export and the database copy
        app.prefs.advanced_mode = true;
        ui.frames(3);  // the Advanced tab is drawn from the next frame on
        CHECK(ui.open_page("Advanced", "Data export") && app.place() == shell::place_of(Page::AdvExport), "Advanced > Data export");
        CHECK(ui.open_page("Advanced", "Maintenance") && drawn("Career database copy"), "Advanced > Maintenance has the database copy");
        app.prefs.advanced_mode = false;
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("wiring (2.16): every page registered as a new page draws in its place; legacy_pages brings the old panels back", [&] {
        const bool was_legacy = app.legacy_pages, was_adv = app.prefs.advanced_mode;
        int drawn = 0;
        for (bool adv : {false, true}) {
            app.prefs.advanced_mode = adv;
            ui.frames(3);
            for (int i = 1; i < shell::kPageCount; ++i) {
                const Page p = static_cast<Page>(i);
                if (!shell::registered_new_page(p) || !shell::page_visible(p, adv)) continue;
                for (bool legacy : {false, true}) {
                    app.legacy_pages = legacy;
                    app.go(shell::area_of(p), p);
                    ui.frames(4);  // a frame that asserts in ImGui throws and fails the case
                    CHECK(app.place() == shell::place_of(p), std::string("opens: ") + shell::area_label(shell::area_of(p)) + " > " + shell::page_label(p) +
                                                                 (legacy ? " (old panel)" : " (new page)"));
                    ++drawn;
                }
            }
        }
        CHECK(drawn >= 20, "the new pages are wired: " + std::to_string(drawn) + " page draws");
        app.legacy_pages = was_legacy;
        app.prefs.advanced_mode = was_adv;
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("P213: the proven panels are where a user clicks them (Simple view): area tab, then the page strip", [&] {
        app.prefs.advanced_mode = false;
        app.sel_team = 7;  // Everton: the club pages and the club panel show a club
        app.go(Page::PlayerEdit);
        ui.frames(3);
        // the live league table (proven 2.2): Career > Standings draws the live view (no FCE here: it says so)
        CHECK(ui.open_page("Career", "Standings") && app.place() == shell::place_of(Page::Standings), "Career > Standings");
        CHECK(ui.find("Try again") != nullptr || ui.find("Reload") != nullptr, "the live standings view is drawn there");
        // the database copy (the old tab's other table) names the live table's page and opens it
        app.prefs.advanced_mode = true;  // 2.16 step 4: the database copy is a section of Advanced > Maintenance
        ui.frames(3);
        CHECK(ui.open_page("Advanced", "Maintenance") && ui.find("Recalculate points") != nullptr, "Advanced > Maintenance: the database copy");
        CHECK(logged_frame(ui).find("Career > Standings") != std::string::npos, "its hint names the live table's page");
        CHECK(ui.click("###dbcopylive"), "the link to the live table");
        app.prefs.advanced_mode = false;
        ui.frames(2);
        CHECK(app.place() == shell::place_of(Page::Standings) && (ui.find("Try again") || ui.find("Reload")), "the link lands on the live view");
        // Job offer: until the Squad track exports its panel, the page says where it is and opens it
        CHECK(ui.open_page("Career", "Manager jobs"), "Career > Manager jobs");
        CHECK(ui.click("###squadgo"), "Manager jobs: the way to the club panel");
        ui.frames(2);
        CHECK(app.place() == shell::place_of(Page::Squad) && ui.find("Job offer") != nullptr && ui.find("Mass actions") != nullptr,
              "Manager jobs: Players > My squad, the club panel with its Job offer / Transfers / Mass actions tabs");
        // Transfers: until then the old Turbo Tools panel (integration 721bf59), not a dead end
        CHECK(ui.open_page("Career", "Transfers & money") && ui.find("Your squad") != nullptr, "Career > Transfers & money: the old Turbo Tools panel");
        // the other proven panels, each by its own primary control
        CHECK(ui.open_page("Players", "Edit a player") && ui.find("##psearch") != nullptr, "Squad & Players > Player edit: the players list");
        CHECK(ui.open_page("Kits & club", "Kits") && ui.find("Apply kit") != nullptr, "Club & Kits > Kits");
        CHECK(ui.open_page("Tactics", "Tactic presets") && ui.find("Apply preset##tpload") != nullptr, "Tactics & Gameplay > Tactic presets");
        CHECK(ui.open_page("Tactics", "Game sliders") && ui.find("Apply preset##gpload") != nullptr, "Tactics & Gameplay > Gameplay presets");
        CHECK(ui.open_page("Career", "Changes & undo") && ui.find("###keptedits") != nullptr, "Career tools > Changes & undo");
        CHECK(ui.open_page("Career", "Season pack") && ui.find("Season pack...##page") != nullptr, "Career tools > CMTracker: the season pack");
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
