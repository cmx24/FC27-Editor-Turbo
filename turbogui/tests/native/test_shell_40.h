// Native tests (4.0 GUI overhaul, slice S3 shell): the 4.0 menu tree behind the one session flag shell::tree40() (default OFF).
// Included by test_main.cpp after test_ui_page.h; shell40_test::ui_cases(app, ui) runs inside the UI block (a real App, the pages drawn through
// the new-page registry, so before test_shell_app clears the old one). Cases are named "Shell 4.0: ..." so TURBO_TEST_ONLY="shell 4.0" runs
// them alone.
//
// What is guarded:
//   - the 4.0 table is exactly the one in the brief (areas and page labels, written out below, not read back from the code);
//   - with the flag on, every page of every area opens by the clicks a person makes (Ui::open_page: the rail, then the page strip) and
//     App::place() is that page, Tactics > Game sliders included (the Tactics strip skips it only in the old tree, app.cpp: the signed S3 hunk);
//   - there is no Looks area, and no tab, area or page label contains "classic", "old" or "new";
//   - the Player card shows Players / Managers and the four tabs Profile | Positions, attributes & playstyles | Appearance | Player creation,
//     no Callname / Everything (classic) tab; the old values 3 and 4 of player_edit_tab() show Profile;
//   - Home draws five tiles (no Looks) and Club is called Team; with the flag off six tiles and the old names;
//   - a page that uses page::Frame is listed in shell::page_has_frame (its own Back "###pg_back"); the shell's "< Back" ("###nav_back") is not
//     drawn above it, in either tree, while it is drawn above the pages without a frame (a back place exists on every page reached from another);
//   - the checkbox "Preview the 4.0 menus" is on the Lab pages only, turns the tree on and off and lands on the Lab's first page;
//   - with the flag off the old tree is exactly what it was (the existing cases keep passing; the strips are checked again here).
// These cases FAIL on the base without tree40 (ce664c6b); the throwaway run that shows it is in the board row.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "ui/ui_home.h"
#include "ui/ui_shell.h"

namespace shell40_test {

using namespace turbo;
using shell::Area;
using shell::Page;

// Restores every process-wide value a case touches, so the cases after this one (and the rest of the suite) see what they saw before.
struct State {
    App& app;
    Ui& ui;
    bool tree, legacy, adv;
    int tab;
    shell::Place back;
    State(App& a, Ui& u) : app(a), ui(u), tree(shell::tree40()), legacy(a.legacy_pages), adv(a.prefs.advanced_mode), tab(shell::player_edit_tab()), back(shell::back_place()) {}
    ~State() {
        shell::tree40() = tree;
        app.legacy_pages = legacy;
        app.prefs.advanced_mode = adv;
        shell::player_edit_tab() = tab;
        shell::back_place() = back;
        app.go(Page::Home);
        ui.frames(3);
    }
};

struct Row {
    Area area;
    const char* label;
    std::vector<std::pair<Page, const char*>> pages;
};

// The 4.0 tree as the brief writes it (plan section 3 with Cassio's answers): areas in rail order, pages in strip order.
static const std::vector<Row>& tree() {
    static const std::vector<Row> t = {
        {Area::Home, "Home", {{Page::Home, "Home"}}},
        {Area::Squad, "Players", {{Page::PlayerEdit, "Player"}, {Page::BulkEdit, "Bulk edit"}}},
        {Area::Club, "Team", {{Page::Squad, "Squad"}, {Page::TeamPlayers, "Players"}, {Page::TeamTransfers, "Transfers"}, {Page::Kits, "Kits"}, {Page::Identity, "Club & crest"}}},
        {Area::Tactics, "Tactics", {{Page::TacticPresets, "Presets"}, {Page::TeamSheet, "Team sheet & bench"}, {Page::GameplayPresets, "Game sliders"}}},
        {Area::Career, "Career", {{Page::Standings, "Standings"}, {Page::Transfers, "Money"}, {Page::JobOffer, "Manager jobs"}, {Page::CMTracker, "Season pack"}, {Page::ChangesUndo, "Changes & undo"}}},
        {Area::Settings, "Settings", {{Page::Settings, "Settings"}}},
        {Area::Advanced, "Lab", {{Page::AdvDatabase, "Database"}, {Page::AdvRawRows, "Row editor"}, {Page::AdvMaintenance, "Maintenance"}, {Page::AdvMatchSetup, "Match setup"},
                                 {Page::AdvGameplayTuning, "Gameplay tuning"}, {Page::AdvExport, "Data export"}, {Page::AdvDiagnostics, "Diagnostics & logs"}, {Page::AdvKitDiagnostics, "Kit diagnostics"}}},
    };
    return t;
}

static std::string lower_s(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The way a person gets to a page: the rail button, then the page's button in the strip (a single-page area has no strip). Every page of the
// 4.0 tree, Tactics > Game sliders too, is reached this way: nothing here calls go().
static bool reach(App&, Ui& ui, const Row& r, Page, const char* label) {
    if (r.pages.size() == 1) {
        if (!ui.click(r.label, "FC 27 LE Turbo", "##shell")) return false;
        ui.frames(3);
        return true;
    }
    return ui.open_page(r.label, label);
}

static void ui_cases(App& app, Ui& ui) {
    using namespace shell;

    run_case("Shell 4.0: the flag is off by default and the 4.0 table is the one in the brief", [&] {
        CHECK(!tree40(), "tree40() is false when nothing asked for it");
        State st(app, ui);
        tree40() = true;
        for (int a = 0; a < kAreaCount; ++a) {
            const Area ar = static_cast<Area>(a);
            const Row* row = nullptr;
            for (const Row& r : tree())
                if (r.area == ar) row = &r;
            if (!row) {
                CHECK(pages_of(ar).empty(), std::string("no page in the area ") + std::to_string(a) + " (Looks)");
                continue;
            }
            CHECK(std::string(area_label(ar)) == row->label, std::string("area label ") + row->label + ", got " + area_label(ar));
            const auto& strip = pages_of(ar);
            CHECK(strip.size() == row->pages.size(), std::string("page count of ") + row->label + ": " + std::to_string(strip.size()));
            for (size_t i = 0; i < row->pages.size() && i < strip.size(); ++i) {
                CHECK(strip[i] == row->pages[i].first, std::string("page ") + std::to_string(i) + " of " + row->label + " is " + row->pages[i].second);
                CHECK(std::string(page_label(row->pages[i].first)) == row->pages[i].second, std::string("label ") + row->pages[i].second);
                CHECK(area_of(row->pages[i].first) == ar, std::string(row->pages[i].second) + " is in " + row->label);
            }
            CHECK(default_page(ar) == row->pages.front().first, std::string(row->label) + " opens on " + row->pages.front().second);
        }
        // the pages the 4.0 tree does not have (the old table keeps them)
        for (Page gone : {Page::CreatePlayer, Page::Staff, Page::CommentaryNames, Page::LooksStudio, Page::StadiumExtras, Page::AdvBulkEdit, Page::AdvTactics})
            CHECK(area_of(gone) == Area::Count && !page_visible(gone, true), std::string("not in the 4.0 tree: ") + page_label(gone));
        tree40() = false;
        CHECK(std::string(area_label(Area::Club)) == "Club" && std::string(area_label(Area::Looks)) == "Looks", "flag off: the old names are back");
    });

    run_case("Shell 4.0: no Looks area, and no label with classic / old / new in the 4.0 tree", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = true;
        tree40() = true;
        app.go(Page::Home);
        ui.frames(4);
        CHECK(!area_visible(Area::Looks, true) && pages_of(Area::Looks).empty(), "the Looks area has no page and is hidden");
        CHECK(ui.find("Looks", "##rail", "##shell") == nullptr, "no Looks button in the rail");
        for (int a = 0; a < kAreaCount; ++a) {
            const Area ar = static_cast<Area>(a);
            if (!area_visible(ar, true)) continue;
            CHECK(std::string(area_label(ar)) != "Looks", "no area called Looks");
            for (const char* bad : {"classic", "old", "new"}) CHECK(lower_s(area_label(ar)).find(bad) == std::string::npos, std::string("area label '") + area_label(ar) + "' has no '" + bad + "'");
            for (Page p : pages_of(ar))
                for (const char* bad : {"classic", "old", "new"}) CHECK(lower_s(page_label(p)).find(bad) == std::string::npos, std::string("page label '") + page_label(p) + "' has no '" + bad + "'");
        }
    });

    run_case("Shell 4.0: every page of the 4.0 tree opens by its clicks and the place matches; page_has_frame matches the pages that draw the frame's bar", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = true;
        tree40() = true;
        app.go(Page::Home);
        ui.frames(4);
        for (const char* a : {"Home", "Players", "Team", "Tactics", "Career", "Settings", "Lab"})
            CHECK(ui.find(a, "##rail", "##shell") != nullptr, std::string("the rail button ") + a);
        for (const Row& r : tree()) {
            for (const auto& pg : r.pages) {
                const bool ok = reach(app, ui, r, pg.first, pg.second);
                CHECK(ok, std::string("reach ") + r.label + " > " + pg.second);
                CHECK(app.place() == place_of(pg.first), std::string("place after ") + r.label + " > " + pg.second);
                // a page with the frame draws its own Back (###pg_back); the shell's own (###nav_back) is not drawn above it. The check is not vacuous:
                // a back place exists on the framed page (so the shell's Back would be drawn) and the pages without a frame do draw it
                const bool framed = ui.find("###pg_back") != nullptr;
                CHECK(framed == page_has_frame(pg.first), std::string(pg.second) + ": page_has_frame says " + (page_has_frame(pg.first) ? "yes" : "no") + ", the page draws " + (framed ? "a frame bar" : "none"));
                const bool shell_back = ui.find("###nav_back") != nullptr;
                const Place bp = back_place();
                if (page_has_frame(pg.first)) {
                    CHECK(bp.valid() && bp.page != pg.first && page_visible(bp.page, true), std::string(pg.second) + ": a back place exists, so the shell's Back would be drawn but for the frame");
                    CHECK(!shell_back, std::string(pg.second) + ": the shell's '< Back' (###nav_back) is not drawn above a framed page");
                } else if (bp.valid() && bp.page != pg.first && page_visible(bp.page, true)) {
                    CHECK(shell_back, std::string(pg.second) + ": the shell's '< Back' is drawn above a page without a frame");
                }
                // the switch is on the Lab pages, and only there
                const bool sw = ui.find("###tree40_preview") != nullptr;
                CHECK(sw == (r.area == Area::Advanced), std::string(pg.second) + ": the preview switch " + (sw ? "is" : "is not") + " drawn");
            }
        }
    });

    run_case("Shell 4.0: the Players page is a Players / Managers switch; the card has the four tabs and no Callname / classic tab", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = false;
        tree40() = true;
        player_edit_tab() = 0;
        app.go(Page::PlayerEdit);
        ui.frames(5);
        CHECK(app.place() == place_of(Page::PlayerEdit), "on the Players page");
        CHECK(ui.find("Players###pe_who_players") != nullptr && ui.find("Managers###pe_who_managers") != nullptr, "the Players / Managers switch");
        CHECK(ui.find("Profile###pe40_profile") != nullptr, "tab Profile");
        CHECK(ui.find("Positions, attributes & playstyles###pe40_ability") != nullptr, "tab Positions, attributes & playstyles");
        CHECK(ui.find("Appearance###pe40_appearance") != nullptr, "tab Appearance");
        CHECK(ui.find("Player creation###pe40_creation") != nullptr, "tab Player creation");
        CHECK(ui.find("###pe_callname") == nullptr && ui.find("###pe_allfields") == nullptr, "no Callname tab and no Everything (classic) tab");
        CHECK(ui.find("Looks###pe_appearance") == nullptr, "the Looks tab is called Appearance");
        // Player creation is reachable by the person and by code (value 5); the old values 3 and 4 show Profile
        CHECK(ui.click("###pe40_creation"), "click Player creation");
        ui.frames(4);
        CHECK(player_edit_tab() == 5, "the page followed the click: tab 5");
        player_edit_tab() = 2;
        ui.frames(4);
        CHECK(player_edit_tab() == 2, "code can select Appearance");
        player_edit_tab() = 3;
        ui.frames(4);
        CHECK(player_edit_tab() == 0, "the old Callname value shows Profile");
        player_edit_tab() = 4;
        ui.frames(4);
        CHECK(player_edit_tab() == 0, "the old classic value shows Profile");
        // Managers: the existing managers page (draw_managers); the card is gone while it shows
        CHECK(ui.click("###pe_who_managers"), "click Managers");
        ui.frames(4);
        CHECK(ui.find("###pe40_profile") == nullptr, "the player card is not drawn on the Managers tab");
        CHECK(ui.click("###pe_who_players"), "click Players");
        ui.frames(4);
        CHECK(ui.find("Profile###pe40_profile") != nullptr, "the player card is back");
    });

    run_case("Shell 4.0: Home has five tiles (no Looks) and Team; the old Home has six", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = false;
        tree40() = true;
        app.go(Page::Home);
        ui.frames(5);
        CHECK(home_state().tiles == 5, "5 tiles with the 4.0 tree: " + std::to_string(home_state().tiles));
        CHECK(ui.find("###home_tile_2") == nullptr, "no Looks tile");
        CHECK(ui.find("###home_tile_3") != nullptr, "the Team tile");
        CHECK(ui.click("###home_tile_3"), "click the Team tile");
        ui.frames(3);
        CHECK(app.place() == place_of(Page::Squad) && app.place().area == Area::Club, "it opens Team > Squad");
        tree40() = false;
        app.go(Page::Home);
        ui.frames(5);
        CHECK(home_state().tiles == 6 && ui.find("###home_tile_2") != nullptr, "flag off: six tiles with Looks");
    });

    run_case("Shell 4.0: Team > Squad is Page::Squad in the Team area; the old tree keeps My squad (old page) in the Lab", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = true;
        tree40() = true;
        app.go(Page::Squad);
        ui.frames(4);
        CHECK(app.place().area == Area::Club && app.place().page == Page::Squad, "go(Squad) lands in Team");
        tree40() = false;
        app.go(Page::Squad);
        ui.frames(4);
        CHECK(app.place().area == Area::Advanced && app.place().page == Page::Squad, "flag off: Squad is in the Lab again");
        CHECK(std::string(page_label(Page::Squad)) == "My squad (old page)", "flag off: its old label");
    });

    run_case("Shell 4.0: the preview switch turns the tree on and off from the Lab and lands on the Lab's first page", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = true;
        tree40() = false;
        app.go(Page::AdvMaintenance);
        ui.frames(4);
        CHECK(app.place() == place_of(Page::AdvMaintenance), "on Lab > Maintenance (old tree)");
        const ItemRec* sw = ui.find("###tree40_preview");
        CHECK(sw != nullptr, "the switch is drawn above a Lab page");
        CHECK(ui.click("###tree40_preview"), "click the switch");
        ui.frames(4);
        CHECK(tree40(), "the 4.0 tree is on");
        CHECK(app.place() == place_of(Page::AdvDatabase), "it opened the Lab's first page");
        CHECK(ui.find("Team", "##rail", "##shell") != nullptr && ui.find("Looks", "##rail", "##shell") == nullptr, "the rail follows: Team, no Looks");
        CHECK(ui.click("###tree40_preview"), "click the switch again");
        ui.frames(4);
        CHECK(!tree40(), "the old tree is back");
        CHECK(ui.find("Club", "##rail", "##shell") != nullptr && ui.find("Looks", "##rail", "##shell") != nullptr, "the rail follows: Club and Looks again");
        CHECK(ui.find("###tree40_preview") != nullptr, "and the switch is still where it was");
    });

    run_case("Shell 4.0: with the flag off the old tree is unchanged", [&] {
        State st(app, ui);
        app.legacy_pages = false;
        app.prefs.advanced_mode = true;
        tree40() = false;
        struct Old {
            const char* area;
            const char* label;
            Page page;
        };
        static const Old old_tree[] = {
            {"Players", "Player", Page::PlayerEdit},       {"Players", "Create & clone", Page::CreatePlayer}, {"Players", "Many players at once", Page::BulkEdit},
            {"Players", "Staff", Page::Staff},             {"Players", "Callnames", Page::CommentaryNames},   {"Club", "Kits", Page::Kits},
            {"Club", "Name, colours & crest", Page::Identity}, {"Club", "Squad", Page::TeamPlayers},          {"Club", "Transfers", Page::TeamTransfers},
            {"Club", "Stadium & extras", Page::StadiumExtras}, {"Tactics", "Presets", Page::TacticPresets},   {"Tactics", "Team sheet & bench", Page::TeamSheet},
            {"Career", "Standings", Page::Standings},      {"Career", "Money & transfers", Page::Transfers},  {"Career", "Manager jobs", Page::JobOffer},
            {"Career", "Season pack", Page::CMTracker},    {"Career", "Changes & undo", Page::ChangesUndo},   {"Lab", "Database", Page::AdvDatabase},
            {"Lab", "Row editor", Page::AdvRawRows},       {"Lab", "Bulk edit", Page::AdvBulkEdit},           {"Lab", "Maintenance", Page::AdvMaintenance},
            {"Lab", "Tactics (experimental)", Page::AdvTactics}, {"Lab", "My squad (old page)", Page::Squad},    {"Lab", "Diagnostics & logs", Page::AdvDiagnostics},
        };
        app.go(Page::Home);
        ui.frames(4);
        for (const Old& o : old_tree) {
            CHECK(ui.open_page(o.area, o.label), std::string("open ") + o.area + " > " + o.label);
            CHECK(app.place() == place_of(o.page), std::string("place after ") + o.area + " > " + o.label);
        }
        CHECK(ui.click("Looks", "FC 27 LE Turbo", "##shell"), "the Looks area");
        ui.frames(3);
        CHECK(app.place() == place_of(Page::LooksStudio), "Looks > Appearance Editor");
        CHECK(pages_of(Area::Squad).size() == 5 && pages_of(Area::Club).size() == 5 && pages_of(Area::Advanced).size() == 11, "the strips have their old length");
        // the old Tactics strip still skips Game sliders (3.0.1 GUI30K); the 4.0 strip lists it
        CHECK(ui.open_page("Tactics", "Presets") && ui.find("Game sliders", "FC 27 LE Turbo", "##pages") == nullptr, "flag off: no Game sliders button on the Tactics strip");
        // the shell's Back is not drawn above a framed page in the old tree either (Data export draws the frame's own Back); it is above the others
        CHECK(ui.open_page("Lab", "Maintenance") && back_place().valid(), "Lab > Maintenance with a back place");
        CHECK(ui.find("###nav_back") != nullptr && ui.find("###pg_back") == nullptr, "flag off: the shell's Back above a page without a frame");
        CHECK(ui.open_page("Lab", "Data export") && back_place().valid() && back_place().page == Page::AdvMaintenance, "Lab > Data export, the back place is Maintenance");
        CHECK(ui.find("###pg_back") != nullptr && ui.find("###nav_back") == nullptr, "flag off: the frame's Back only above Data export");
        // the Player card keeps its five tabs
        player_edit_tab() = 0;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        CHECK(ui.find("Callname###pe_callname") != nullptr && ui.find("Everything (classic)###pe_allfields") != nullptr && ui.find("###pe_who_managers") == nullptr, "the old card tabs, no Players / Managers switch");
    });
}

}  // namespace shell40_test
