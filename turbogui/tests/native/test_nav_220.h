// Native tests (2.20 IA wiring, brief 2.20-IA-wiring chunk 1): the navigation after the empty "Faces & looks" page went away.
// Included by test_main.cpp after test_page_looks.h; nav220_test::run() runs with the pure shell cases (the start-up registrations of every
// page .cpp are still in place), nav220_test::ui_cases(app, ui) inside the UI block. Cases are named "Navigation 2.20: ..." so
// TURBO_TEST_ONLY="navigation 2.20" runs them alone.
//
// What is proven here: the two placeholder pages (Faces & hair, Body, tattoos & gear) are not in any page strip and the area is no longer
// called Faces & looks; the Looks studio carries its new name, Appearance Editor, on the area tab and on its page, and no label is still
// "Looks studio"; every page sits in exactly one strip and draws something (a registered function or a legacy panel); a page that is
// registered in both registries is on the list below (the new page wins; Settings > Use the old pages brings the old one back), so a
// double registration nobody looked at fails here; the tab opens the Appearance Editor and the page strip of Players still reaches the
// pages the placeholders pointed at (Edit a player has the Appearance and Miniface tabs).
#pragma once
#include <set>
#include <string>

#include "ui/ui_player_profile.h"
#include "ui/ui_shell.h"
#include "ui/ui_tactics_page.h"

namespace nav220_test {

using turbo::shell::Area;
using turbo::shell::Page;

// Registered in BOTH registries on purpose (the 2.16 page replaces the older page of the same id until the old pages are asked for).
// Wiring chunk 2: BulkEdit, TeamPlayers and TeamTransfers also have an old-registry signpost (ui_nav_pages.cpp) that answers when Settings > Use the
// old pages is on; anything else registered twice is a mistake to look at.
static const Page kBoth[] = {Page::Kits,      Page::Identity, Page::TacticPresets, Page::TeamSheet, Page::GameplayPresets,
                             Page::Transfers, Page::JobOffer, Page::ChangesUndo,   Page::AdvExport,  Page::CreatePlayer,
                             Page::BulkEdit,  Page::TeamPlayers, Page::TeamTransfers,
                             Page::AdvTactics};  // 3.0.1 GUI30K: Lab > Tactics (experimental) + the offline slider file section (ui_tactics_page.cpp)

static void run() {
    using namespace turbo::shell;
    run_case("Navigation 2.20: the placeholder pages are gone and every control they pointed at is still on a page", [&] {
        std::set<std::string> labels;
        for (int i = 1; i < kPageCount; ++i) labels.insert(page_label(static_cast<Page>(i)));
        CHECK(labels.count("Faces & hair") == 0, "no page called Faces & hair");
        CHECK(labels.count("Body, tattoos & gear") == 0, "no page called Body, tattoos & gear");
        for (int a = 0; a < kAreaCount; ++a) CHECK(std::string(area_label(static_cast<Area>(a))) != "Faces & looks", "no area called Faces & looks");
        const auto& strip = pages_of(Area::Looks);
        CHECK(strip.size() == 1 && strip[0] == Page::LooksStudio && default_page(Area::Looks) == Page::LooksStudio, "the area holds the Appearance Editor and nothing else");
        // what the two signposts pointed at lives on Edit a player (Appearance and Miniface tabs), which is registered
        CHECK(registered_new_page(Page::PlayerEdit) != nullptr && page_visible(Page::PlayerEdit, false), "Edit a player is a page of the Simple view");
        CHECK(area_of(Page::PlayerEdit) == Area::Squad && std::string(page_label(Page::PlayerEdit)) == "Player", "Player (3.0: was Edit a player) is in Players");
    });

    run_case("Navigation 2.20: Looks studio is called Appearance Editor on the page, the area is Looks (3.0)", [&] {
        CHECK(std::string(page_label(Page::LooksStudio)) == "Appearance Editor", "page label");
        CHECK(std::string(area_label(Area::Looks)) == "Looks", "area label (3.0 rail)");
        CHECK(area_of(Page::LooksStudio) == Area::Looks && page_visible(Page::LooksStudio, false), "it sits in its own area, visible in the Simple view");
        for (int i = 1; i < kPageCount; ++i) CHECK(std::string(page_label(static_cast<Page>(i))) != "Looks studio", "no page still says Looks studio");
        for (int a = 0; a < kAreaCount; ++a) CHECK(std::string(area_label(static_cast<Area>(a))) != "Looks studio", "no area says Looks studio");
        CHECK(registered_page(Page::LooksStudio) != nullptr, "the studio's draw function is registered for it");
    });

    run_case("Navigation 2.20: Club area, Players area and the new pages are wired", [&] {
        CHECK(std::string(area_label(Area::Club)) == "Club", "the Kits & club area is called Club (3.0; 2.20 Team)");
        for (int a = 0; a < kAreaCount; ++a)
            CHECK(std::string(area_label(static_cast<Area>(a))) != "Kits & club" && std::string(area_label(static_cast<Area>(a))) != "Team" &&
                      std::string(area_label(static_cast<Area>(a))) != "Advanced",
                  "no area still says Kits & club, Team or Advanced");
        CHECK(std::string(area_label(Area::Advanced)) == "Lab" && std::string(area_label(Area::Home)) == "Home", "3.0: Home and Lab");
        std::set<Page> team(pages_of(Area::Club).begin(), pages_of(Area::Club).end());
        for (Page p : {Page::Identity, Page::Kits, Page::TeamPlayers, Page::TeamTransfers, Page::StadiumExtras})
            CHECK(team.count(p) == 1, std::string("Team has ") + page_label(p));
        CHECK(team.size() == 5, "Club holds exactly Identity, Kits, Squad, Transfers, Stadium");
        CHECK(team.count(Page::TeamSheet) == 0 && area_of(Page::TeamSheet) == Area::Tactics, "Team sheet stays in Tactics");
        CHECK(area_of(Page::Transfers) == Area::Career, "the money page (Transfers & money) stays in Career");
        CHECK(std::string(page_label(Page::TeamPlayers)) == "Squad" && std::string(page_label(Page::TeamTransfers)) == "Transfers", "Club page labels");
        CHECK(area_of(Page::BulkEdit) == Area::Squad && std::string(page_label(Page::BulkEdit)) == "Many players at once", "Players > Many players at once");
        CHECK(area_of(Page::CreatePlayer) == Area::Squad && std::string(page_label(Page::CreatePlayer)) == "Create & clone", "Players > Create & clone");
        CHECK(area_of(Page::AdvBulkEdit) == Area::Advanced, "Lab > Bulk edit stays until the capability check passes");
        for (Page p : {Page::PlayerEdit, Page::CreatePlayer, Page::BulkEdit, Page::TeamPlayers, Page::TeamTransfers})
            CHECK(registered_new_page(p) != nullptr && page_visible(p, false), std::string("new page registered and in the Simple view: ") + page_label(p));
        CHECK(registered_page(Page::LooksStudio) != nullptr && page_visible(Page::LooksStudio, false), "the Appearance Editor is registered and in the Simple view");
        // (profile_team_players_page() is pointed at Team > Players where Edit a player is drawn: checked in the UI case below)
        CHECK(player_edit_tab() >= 0 && player_edit_tab() <= 4, "Player has four tabs (five with 3.0 track B's Callname tab)");
    });

    run_case("Navigation 2.20: every page is in one strip, draws something, and a double registration is on the list", [&] {
        std::set<int> inboth;
        for (Page p : kBoth) inboth.insert(static_cast<int>(p));
        std::set<int> seen, found_both;
        for (int a = 0; a < kAreaCount; ++a)
            for (Page p : pages_of(static_cast<Area>(a))) CHECK(seen.insert(static_cast<int>(p)).second, std::string("one strip only: ") + page_label(p));
        for (int i = 1; i < kPageCount; ++i) {
            const Page p = static_cast<Page>(i);
            CHECK(seen.count(i) == 1, std::string("in a strip: ") + page_label(p));
            const bool oldp = registered_page(p) != nullptr, newp = registered_new_page(p) != nullptr;
            CHECK(oldp || newp || legacy_tab_of(p) >= 0, std::string("draws something (a registered page or a legacy panel): ") + page_label(p));
            if (oldp && newp) found_both.insert(i);
        }
        CHECK(found_both == inboth, "the pages registered twice are exactly the listed ones (" + std::to_string(found_both.size()) + " of " + std::to_string(inboth.size()) + ")");
    });
}

static void ui_cases(turbo::App& app, Ui& ui) {
    using namespace turbo::shell;
    run_case("Navigation 2.20: the Looks rail button opens the Appearance Editor; no Faces & looks; Players still has Player", [&] {
        app.prefs.advanced_mode = false;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        CHECK(ui.find("Looks", "##rail", "##shell") != nullptr, "the Looks rail button");
        CHECK(ui.find("Faces & looks", "FC 27 LE Turbo", "##shell") == nullptr, "no Faces & looks button");
        CHECK(ui.find("Faces & hair", "FC 27 LE Turbo") == nullptr && ui.find("Body, tattoos & gear", "FC 27 LE Turbo") == nullptr, "no placeholder buttons");
        CHECK(ui.click("Looks", "##rail", "##shell"), "click Looks in the rail");
        ui.frames(4);
        CHECK(app.place() == place_of(Page::LooksStudio), "it opened the Appearance Editor");
        CHECK(ui.find("###navgo") == nullptr, "no signpost button on it");
        CHECK(ui.click("Players", "##rail", "##shell"), "back to Players");
        ui.frames(4);
        CHECK(ui.find("Player", "##content", "##pages") != nullptr, "the Players strip still has Player");
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("Navigation 2.20: Player has its tabs; Club has Squad and Transfers; Players has Create & clone and Many players at once", [&] {
        app.prefs.advanced_mode = false;
        const bool was_legacy = app.legacy_pages;
        app.legacy_pages = false;  // the new pages (the cases of the other tracks run with the old ones)
        const int was_tab = player_edit_tab();
        const Page was_link = turbo::profile_team_players_page();
        player_edit_tab() = 0;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        CHECK(ui.find("Profile###pe_profile") != nullptr || ui.find("###pe_profile") != nullptr, "Profile tab");
        CHECK(ui.find("###pe_ability") != nullptr, "Positions, attributes & PlayStyles tab");
        CHECK(ui.find("###pe_appearance") != nullptr, "Appearance tab");
        CHECK(ui.find("###pe_allfields") != nullptr, "Everything (classic) tab (last)");
        CHECK(turbo::profile_team_players_page() == Page::TeamPlayers, "the Profile's Open Team > Players button is pointed at Team > Players");
        CHECK(ui.click("###pe_allfields"), "click Everything (classic)");
        ui.frames(4);
        CHECK(player_edit_tab() >= 3 && ui.find("Card view (beta)###pcardview") != nullptr, "All fields shows the full editor and remembers its tab");
        CHECK(ui.click("###pe_profile"), "back to Profile");
        ui.frames(4);
        CHECK(player_edit_tab() == 0 && ui.find("Card view (beta)###pcardview") == nullptr, "Profile does not show the full editor");
        CHECK(ui.open_page("Club", "Squad") && app.place() == place_of(Page::TeamPlayers), "Club > Squad opens");
        CHECK(ui.open_page("Club", "Transfers") && app.place() == place_of(Page::TeamTransfers), "Club > Transfers opens");
        CHECK(ui.open_page("Players", "Create & clone") && app.place() == place_of(Page::CreatePlayer), "Players > Create & clone opens");
        CHECK(ui.open_page("Players", "Many players at once") && app.place() == place_of(Page::BulkEdit), "Players > Many players at once opens");
        player_edit_tab() = was_tab;
        turbo::profile_team_players_page() = was_link;  // the Profile's own test expects its default
        app.legacy_pages = was_legacy;
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    // 3.0.1 GUI30K (coordinator 16:55): the Settings-file slider route had no effect in game on 2026-10-24 (the game rewrote its Settings
    // file at boot), so the Tactics area shows no slider staging; the list, Prepare / Discard and the steps are in the Lab only, on
    // Tactics (experimental), under "Offline slider file (experimental)" with the label below. Fails on b153e48 (the strip has Game sliders,
    // the Lab page has no such section). Both page sets: the new pages and Settings > Use the old pages.
    run_case("3.0.1 GUI30K: Game sliders moved to the Lab - no slider staging on the Tactics pages; Lab > Tactics (experimental) has the offline slider file", [&] {
        const bool was_legacy = app.legacy_pages, was_adv = app.prefs.advanced_mode;
        const std::string heading = "Offline slider file (experimental)";
        const std::string label =
            "No effect seen in game on 2026-10-24: the game rewrote its Settings file at boot, before the Main Menu (GUI30K). Kept for diagnosis.";
        for (bool legacy : {false, true}) {
            const std::string how = legacy ? " (old pages)" : " (new pages)";
            app.legacy_pages = legacy;
            // the Lab flag off (Settings > Show advanced tools off)
            app.prefs.advanced_mode = false;
            app.go(Page::TacticPresets);
            ui.frames(4);
            CHECK(ui.find("Presets", "##content", "##pages") != nullptr, "the Tactics strip is drawn (Presets)" + how);
            CHECK(ui.find("Game sliders", "##content", "##pages") == nullptr, "Lab off: no Game sliders entry in the Tactics strip" + how);
            for (Page p : pages_of(Area::Tactics)) {
                if (p == Page::GameplayPresets) continue;  // a pointer page now; checked below (new pages)
                app.go(p);
                ui.frames(3);
                CHECK(ui.find("Prepare###gsprepare") == nullptr && ui.find("###gsdiscard") == nullptr,
                      std::string("Lab off: no slider staging on Tactics > ") + page_label(p) + how);
            }
            // the Lab flag on: Lab > Tactics (experimental), below the live-slider block
            app.prefs.advanced_mode = true;
            app.go(Page::TacticPresets);
            ui.frames(3);
            CHECK(ui.find("Game sliders", "##content", "##pages") == nullptr, "Lab on: still no Game sliders entry in the Tactics strip" + how);
            app.go(Page::AdvTactics);
            ui.frames(4);
            const std::string txt = logged_frame(ui);
            CHECK(app.place() == place_of(Page::AdvTactics), "Lab > Tactics (experimental) opened" + how);
            if (!legacy) {
                const turbo::GameSlidersPageState& s = turbo::game_sliders_page_state();
                CHECK(s.lab_heading == heading && s.lab_label == label, "the Lab page drew the section heading and the label verbatim" + how);
                CHECK(txt.find(heading) != std::string::npos && txt.find(label) != std::string::npos, "the heading and the label are on screen" + how);
                CHECK(ui.find("Prepare###gsprepare") != nullptr && ui.find("###gsrefresh") != nullptr, "the list's Refresh and Prepare are in the Lab section" + how);
                CHECK(txt.find("Save one on Tactic presets") != std::string::npos || !s.names.empty(), "the hint or the list is there" + how);
            }
        }
        // the Game sliders page itself (Home and Changes & undo still open it): a pointer to the Lab, no list, no Prepare
        app.legacy_pages = false;
        for (bool adv : {false, true}) {
            app.prefs.advanced_mode = adv;
            app.go(Page::GameplayPresets);
            ui.frames(4);
            const std::string txt = logged_frame(ui);
            CHECK(turbo::game_sliders_page_state().moved_note && ui.find("Prepare###gsprepare") == nullptr && ui.find("###gsel0") == nullptr,
                  std::string("Game sliders is a pointer, no list or Prepare (Lab ") + (adv ? "on)" : "off)"));
            CHECK(txt.find(label) != std::string::npos && ui.find("###gsmoved") != nullptr, "the pointer says why and links on");
        }
        app.legacy_pages = was_legacy;
        app.prefs.advanced_mode = was_adv;
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });
}

}  // namespace nav220_test
