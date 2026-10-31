// Native tests (2026-10-16, GUI revamp contract, GUI_REVAMP_SPEC.md): the shell contract of ui/ui_shell.h and the palette of
// ui/ui_theme.h. Included by test_main.cpp after test_ux_resize.h; test_shell() runs the pure cases, test_shell_app(app)
// (inside the UI block, a real App) the page registry cases that need an App&.
// Track T1 (2026-10-16): the tab bar / request_tab / go() / Settings page cases (test_shell_ui) and the sections_open round trip.
#pragma once
#include <cmath>
#include <fstream>
#include <set>
#include <string>

#include "ui/gui_prefs.h"
#include "ui/ui_home.h"
#include "ui/ui_shell.h"
#include "ui/ui_theme.h"

namespace {

using turbo::shell::Area;
using turbo::shell::Page;

static std::vector<Page> shell_all_pages() {
    std::vector<Page> v;
    for (int i = 1; i < turbo::shell::kPageCount; ++i) v.push_back(static_cast<Page>(i));
    return v;
}

// The page strip of a long area (Career had nine pages) takes a second row: the cases that walk the whole old Turbo
// Tools panel in one go ask for a taller window while they run
struct ShellTallWindow {
    ImVec2 old;
    Ui& ui;
    ShellTallWindow(Ui& u, float extra) : old(0, 0), ui(u) {
        if (ImGuiWindow* w = ImGui::FindWindowByName("FC 27 LE Turbo")) {
            old = w->Size;
            ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(old.x, old.y + extra));
            ui.frames(2);
        }
    }
    ~ShellTallWindow() {
        if (old.x > 0) {
            ImGui::SetWindowSize("FC 27 LE Turbo", old);
            ui.frames(2);
        }
    }
};

static int g_shell_calls = 0;
static void shell_fake_page(turbo::App&) { ++g_shell_calls; }
static void shell_other_page(turbo::App&) { g_shell_calls += 100; }

static void test_shell() {
    run_case("shell: areas and pages (labels, one area per page, strips in order, defaults)", [&] {
        using namespace turbo::shell;
        CHECK(kAreaCount == 8, "10-28: Home and seven areas (Game settings added); 2026-10-31: the Lab is gone");
        const char* labels[] = {"Home", "Players", "Looks", "Club", "Tactics", "Game settings", "Career", "Turbo settings"};
        for (int a = 0; a < kAreaCount; ++a) CHECK(std::string(area_label(static_cast<Area>(a))) == labels[a], std::string("area label ") + labels[a]);
        CHECK(std::string(area_label(Area::Count)).empty(), "no label for Count");
        CHECK(static_cast<int>(Area::Home) == 0 && pages_of(Area::Home).size() == 1, "Home is first and has one page");
        const Page defaults[] = {Page::Home, Page::PlayerEdit, Page::LooksStudio, Page::Kits, Page::TacticPresets, Page::GameplayPresets, Page::Standings, Page::Settings};
        std::set<int> seen;
        for (int a = 0; a < kAreaCount; ++a) {
            const auto& strip = pages_of(static_cast<Area>(a));
            CHECK(!strip.empty(), std::string("page strip not empty: ") + labels[a]);
            CHECK(default_page(static_cast<Area>(a)) == defaults[a], std::string("default page of ") + labels[a]);
            for (Page p : strip) {
                CHECK(area_of(p) == static_cast<Area>(a), std::string("page sits in its area: ") + page_label(p));
                CHECK(seen.insert(static_cast<int>(p)).second, std::string("page in exactly one strip: ") + page_label(p));
            }
        }
        std::set<std::string> names;
        for (Page p : shell_all_pages()) {
            CHECK(seen.count(static_cast<int>(p)) == 1, std::string("every Page is in a strip: ") + std::to_string(static_cast<int>(p)));
            CHECK(std::string(page_label(p)).size() > 0, "page has a label");
            CHECK(place_of(p).valid() && place_of(p).area == area_of(p) && place_of(p).page == p, "place_of");
            const std::string key = std::string(area_label(area_of(p))) + "/" + page_label(p);
            CHECK(names.insert(key).second, "labels unique inside an area: " + key);
        }
        CHECK(!place_of(Page::None).valid() && !place_of(Page::Count).valid() && area_of(Page::None) == Area::Count, "None/Count are invalid");
        CHECK(default_page(Area::Count) == Page::None && pages_of(Area::Count).empty(), "Count has no pages");
        CHECK(std::string(page_label(Page::None)).empty(), "no label for None");
        // 3.0 plain-English page names (brief 3.0-shell A.2)
        struct L { Page p; const char* label; };
        const L names30[] = {{Page::Home, "Home"}, {Page::PlayerEdit, "Player"}, {Page::CreatePlayer, "Create & clone"}, {Page::BulkEdit, "Many players at once"},
                             {Page::Staff, "Staff"}, {Page::LooksStudio, "Appearance Editor"}, {Page::Kits, "Kits"},
                             {Page::Identity, "Name, colours & crest"}, {Page::TeamPlayers, "Squad"}, {Page::TeamTransfers, "Transfers"},
                             {Page::StadiumExtras, "Stadium & extras"}, {Page::TacticPresets, "Presets"}, {Page::TeamSheet, "Team sheet & bench"},
                             {Page::GameplayPresets, "Game settings & CPU sliders"}, {Page::Standings, "Standings"}, {Page::Transfers, "Money & transfers"},
                             {Page::JobOffer, "Manager jobs"}, {Page::CMTracker, "Season pack"}, {Page::ChangesUndo, "Changes & undo"}};
        for (const L& l : names30) CHECK(std::string(page_label(l.p)) == l.label, std::string("3.0 label: ") + l.label);
        CHECK(sizeof(names30) / sizeof(names30[0]) == static_cast<size_t>(kPageCount - 1), "every page is in the 3.0 label table");
        for (Page p : shell_all_pages()) CHECK(std::string(page_label(p)).find("(Lab)") == std::string::npos, "no page says (Lab)");
        // sentence case: no capitalised second word (except proper names and the single acronym CMTracker)
        for (Page p : shell_all_pages()) {
            const std::string l = page_label(p);
            if (l == "CMTracker" || l == "Appearance Editor") continue;  // proper names (Cassio named the Appearance Editor, 2.20)
            const size_t sp = l.find(' ');
            if (sp != std::string::npos && sp + 1 < l.size()) CHECK(!(l[sp + 1] >= 'A' && l[sp + 1] <= 'Z'), "sentence case: " + l);
        }
    });

    run_case("shell: area_for maps the eight legacy tab indices (request_tab) to their new home", [&] {
        using namespace turbo::shell;
        struct Row { int tab; Area a; Page p; };
        // 2026-10-31 (5.0c menus step 6): the Teams tab's page ("My squad (old page)") and the Database tab's page went with the Lab:
        // those two indices give an invalid Place now (ui_shell.h area_for)
        const Row rows[] = {
            {turbo::TabPlayers, Area::Squad, Page::PlayerEdit},
            {turbo::TabManagers, Area::Squad, Page::Staff},
            {turbo::TabCompetitions, Area::Career, Page::Standings},
            {turbo::TabTools, Area::Career, Page::Transfers},
            {turbo::TabStatus, Area::Settings, Page::Settings},
            {turbo::TabGameSettings, Area::GameSettings, Page::GameplayPresets},  // 10-28 findability: its own left-menu area
        };
        CHECK(turbo::TabCount == 8 && sizeof(rows) / sizeof(rows[0]) == 6, "eight legacy tabs, six with a page left");
        for (const Row& r : rows) {
            const Place pl = area_for(r.tab);
            CHECK(pl.valid() && pl.area == r.a && pl.page == r.p, "legacy tab " + std::to_string(r.tab) + " -> " + page_label(r.p));
            CHECK(legacy_tab_of(r.p) >= 0, "inverse exists for " + std::string(page_label(r.p)));
        }
        CHECK(!area_for(turbo::TabTeams).valid() && !area_for(turbo::TabDatabase).valid(), "Teams and Database: no page any more");
        CHECK(legacy_tab_of(Page::PlayerEdit) == 0 && legacy_tab_of(Page::Standings) == 3 &&
                  legacy_tab_of(Page::Settings) == 6 && legacy_tab_of(Page::GameplayPresets) == 7 && legacy_tab_of(Page::Kits) == 1 &&
                  legacy_tab_of(Page::Transfers) == 5 && legacy_tab_of(Page::Staff) == 2 && legacy_tab_of(Page::None) == -1,
              "legacy_tab_of: where each panel lived");
        for (Page p : shell_all_pages()) {
            const int t = legacy_tab_of(p);
            // 2.16 step 4: the pages that are new in the re-cut navigation never lived in a legacy tab
            const bool is_new = p == Page::Home || p == Page::CreatePlayer || p == Page::LooksStudio || p == Page::TeamSheet ||
                                p == Page::BulkEdit || p == Page::TeamPlayers || p == Page::TeamTransfers;  // 2.20 pages
            CHECK(is_new ? t == -1 : (t >= 0 && t < turbo::TabCount), std::string("every older page has a legacy tab, the new ones none: ") + page_label(p));
        }
        for (int bad : {-1, 8, 99, -100}) CHECK(!area_for(bad).valid(), "out of range -> invalid: " + std::to_string(bad));
    });

    run_case("shell: every area and page the shell knows is visible, None / Count never; Gameplay presets is the one mixed page", [&] {
        using namespace turbo::shell;
        for (int a = 0; a < kAreaCount; ++a) CHECK(area_visible(static_cast<Area>(a)), std::string("area visible: ") + area_label(static_cast<Area>(a)));
        CHECK(!area_visible(Area::Count), "Count is never visible");
        for (Page p : shell_all_pages()) CHECK(page_visible(p), std::string("page visible: ") + page_label(p));
        CHECK(!page_visible(Page::None) && !page_visible(Page::Count), "None/Count never visible");
        CHECK(page_effect_unmeasured(Page::GameplayPresets), "Gameplay presets: one muted sentence (write proven, effect on play not measured)");
        CHECK(!page_effect_unmeasured(Page::TacticPresets) && !page_effect_unmeasured(Page::Kits), "proven pages carry nothing");
    });

    run_case("shell: gui.sections_open round trip (defaults, set / get, other keys kept, junk ignored)", [&] {
        using turbo::GuiPrefs;
        GuiPrefs p;
        CHECK(p.sections_open.empty() && p.section_open("x.a", true) && !p.section_open("x.a", false), "unset: the page's default");
        CHECK(p.set_section_open("settings.log", true) && !p.set_section_open("settings.log", true), "set reports a change once");
        CHECK(p.set_section_open("settings.preferences", false) && !p.section_open("settings.preferences", true), "closed beats the default");
        nlohmann::json j = nlohmann::json::object();
        j["gui"]["ui_scale"] = 1.2;
        p.save(j);
        CHECK(j["gui"]["ui_scale"] == 1.2 && j["gui"]["sections_open"]["settings.log"] == true &&
                  j["gui"]["sections_open"]["settings.preferences"] == false,
              "saved next to the other keys: " + j["gui"].dump());
        const GuiPrefs q = GuiPrefs::load(j);
        CHECK(q.sections_open.size() == 2 && q.section_open("settings.log", false) && !q.section_open("settings.preferences", true),
              "loaded back");
        nlohmann::json bad = nlohmann::json::parse("{\"gui\":{\"sections_open\":{\"a\":true,\"b\":\"yes\",\"c\":3}}}");
        const GuiPrefs r = GuiPrefs::load(bad);
        CHECK(r.sections_open.size() == 1 && r.section_open("a", false), "only booleans are taken");
        CHECK(GuiPrefs::load(nlohmann::json::parse("{\"gui\":{\"sections_open\":[1]}}")).sections_open.empty() &&
                  GuiPrefs::load(nlohmann::json::object()).sections_open.empty(),
              "wrong type / missing: empty");
    });

    run_case("shell: theme palette and grid constants (spec 2.2)", [&] {
        using namespace turbo::theme;
        const ImVec4 cs[] = {accent(), ok(), warn(), err(), muted()};
        for (const ImVec4& c : cs) CHECK(c.w == 1.0f && c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0 && c.z <= 1, "opaque, in range");
        CHECK(err().x == 1.0f && err().y == 0.5f && err().z == 0.3f, "error keeps the existing orange-red ImVec4(1, 0.5, 0.3, 1)");
        CHECK(ok().y > ok().x && ok().y > ok().z, "ok is green");
        CHECK(warn().x > 0.9f && warn().y > 0.6f && warn().z < 0.4f, "warn is amber");
        CHECK(accent().z > accent().x && accent().z > accent().y, "accent is blue");
        CHECK(muted().x == 0.70f && muted().y == 0.72f && muted().z == 0.76f, "muted equals the style TextDisabled");
        auto differs = [](const ImVec4& a, const ImVec4& b) { return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) > 0.2f; };
        CHECK(differs(ok(), warn()) && differs(warn(), err()) && differs(ok(), err()) && differs(accent(), ok()), "status colours are distinguishable");
        CHECK(kGrid == 8.0f && kSectionPad == 12.0f && kFieldRowHeight == 26.0f && kSelectWidth == 300.0f && kFieldLabelWidth == 160.0f, "spacing constants");
        CHECK(kFontBody == 1.0f && kFontTitle == 1.25f && kFontSmall == 0.85f, "type scale");
        CHECK(kRailWidth < kSelectWidth && kNarrowWindow == 900.0f, "rail narrower than the select panel");
        CHECK(std::fmod(kSelectWidth, 4.0f) == 0.0f && std::fmod(kGrid, 8.0f) == 0.0f, "grid sizes");
    });
}

// Registry cases: need a real App& for draw_page
static void test_shell_app(turbo::App& app) {
    // The pages the app registered at start-up (Settings, later the other tracks'): the registry case clears them, they are
    // put back afterwards so the cases that follow (and test_shell_ui) see the real pages
    turbo::shell::PageFn saved[turbo::shell::kPageCount] = {};
    for (int i = 1; i < turbo::shell::kPageCount; ++i) saved[i] = turbo::shell::registered_page(static_cast<Page>(i));
    run_case("shell: page registry (register once, draw only registered pages, clear)", [&] {
        using namespace turbo::shell;
        clear_registry_for_tests();
        g_shell_calls = 0;
        CHECK(registered_page(Page::Kits) == nullptr, "nothing registered");
        CHECK(!draw_page(app, Page::Kits) && g_shell_calls == 0, "unregistered page draws nothing");
        CHECK(register_page(Page::Kits, shell_fake_page), "registered");
        CHECK(!register_page(Page::Kits, shell_other_page) && registered_page(Page::Kits) == shell_fake_page, "second registration ignored");
        CHECK(!register_page(Page::None, shell_fake_page) && !register_page(Page::Count, shell_fake_page) && !register_page(Page::Identity, nullptr),
              "None, Count and a null function are refused");
        CHECK(draw_page(app, Page::Kits) && g_shell_calls == 1, "a registered page draws");
        clear_registry_for_tests();
        CHECK(registered_page(Page::Kits) == nullptr && !draw_page(app, Page::Kits), "cleared");
        for (int i = 1; i < kPageCount; ++i)
            if (saved[i]) register_page(static_cast<Page>(i), saved[i]);
        CHECK(registered_page(Page::Settings) == saved[static_cast<int>(Page::Settings)], "start-up registrations restored");
    });
}

// UI cases: the area tabs, the page strip, request_tab / go(), the Settings page (T1)
// (2026-10-31, 5.0c menus step 6: the Lab / Advanced view, "Show the Lab" and "Use the old pages" are gone, and with them the cases that
// switched them; what is left runs on the one view.)
static void test_shell_ui(turbo::App& app, Ui& ui) {
    using namespace turbo::shell;
    auto settle = [&](int n = 6) { ui.frames(n); };

    run_case("shell UI: area tabs and the page strip; legacy request_tab lands on area_for", [&] {
        app.request_tab = 0;
        settle();
        for (const char* a : {"Home", "Players", "Looks", "Club", "Tactics", "Game settings", "Career", "Turbo settings"})
            CHECK(ui.find(a, "##rail", "##shell") != nullptr, std::string("the rail button ") + a);
        CHECK(ui.find("Lab", "FC 27 LE Turbo", "##shell") == nullptr, "no Lab in the rail");
        CHECK(app.place() == area_for(0), "request_tab 0 -> Players / Player");
        CHECK(ui.find("Player", "##content", "##pages") && ui.find("Staff", "##content", "##pages"), "the Players page strip, inside the content child");
        {
            const Place at = app.place();
            app.request_tab = 1;  // the Teams tab's page (My squad (old page)) is gone: no place to land on
            settle();
            CHECK(app.place() == at && app.request_tab == -1, "request_tab 1 (Teams: no page any more) is ignored");
        }
        for (int t : {2, 3, 5, 6, 7}) {
            app.request_tab = t;
            settle();
            CHECK(app.place() == area_for(t), "request_tab " + std::to_string(t) + " -> " + page_label(area_for(t).page));
        }
        const Place before = app.place();
        app.request_tab = 4;  // Database: its page went with the Lab
        settle();
        CHECK(app.place() == before, "request_tab 4 (Database: no page any more) is ignored");
        CHECK(app.request_tab == -1, "request consumed");
        app.request_tab = 99;
        settle(2);
        CHECK(app.place() == before && app.request_tab == -1, "an out of range request is dropped");
    });

    run_case("shell UI: go(Area, Page) for every page; a page in the wrong area is ignored", [&] {
        for (int a = 0; a < kAreaCount; ++a)
            for (Page p : pages_of(static_cast<Area>(a))) {
                app.go(static_cast<Area>(a), p);
                settle();
                CHECK(app.place().area == static_cast<Area>(a) && app.place().page == p,
                      std::string("go -> ") + area_label(static_cast<Area>(a)) + " / " + page_label(p));
            }
        const Place at = app.place();
        app.go(Area::Club, Page::Standings);  // wrong area for the page
        settle();
        CHECK(app.place() == at, "a page in the wrong area is ignored");
        app.go(Page::Kits);  // the one-argument form takes the page's own area
        settle();
        CHECK(app.place() == place_of(Page::Kits), "go(page) -> Club / Kits");
    });

    run_case("shell UI: a registered page is drawn instead of the legacy panel; an unregistered one falls back", [&] {
        PageFn saved[kPageCount] = {};
        for (int i = 1; i < kPageCount; ++i) saved[i] = registered_page(static_cast<Page>(i));
        clear_registry_for_tests();
        g_shell_calls = 0;
        app.go(Page::Kits);
        settle();
        CHECK(app.place().page == Page::Kits && g_shell_calls == 0, "unregistered page: opened, the old panel stands in, nothing registered is called");
        CHECK(register_page(Page::Kits, shell_fake_page), "register Kits");
        settle();
        CHECK(g_shell_calls > 0, "registered page drawn");
        const int n = g_shell_calls;
        app.go(Page::Identity);
        settle();
        CHECK(g_shell_calls == n, "not drawn while another page is shown");
        clear_registry_for_tests();
        for (int i = 1; i < kPageCount; ++i)
            if (saved[i]) register_page(static_cast<Page>(i), saved[i]);
    });

    run_case("shell UI: Settings page: collapsing sections are saved in gui_settings.json", [&] {
        auto read_gui = [&]() {
            std::ifstream f(app.turbo_output() / "gui_settings.json", std::ios::binary);
            nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            return j.is_discarded() ? nlohmann::json::object() : j;
        };
        CHECK(registered_page(Page::Settings) != nullptr, "the Settings page is registered");
        app.prefs.sections_open.clear();
        app.go(Page::Settings);
        settle();
        CHECK(app.place() == place_of(Page::Settings), "on the Settings page");
        CHECK(ui.find("Look and feel") != nullptr, "sections drawn (the Log header is clicked below, scrolling to it if it is below the fold)");
        CHECK(ui.find("Change...##hotkey", "", "status") != nullptr && ui.find("##rowheight") != nullptr, "the preferences of the old Status tab (2.22: the Row height slider replaced ###rowdensity)");
        CHECK(ui.find("###advview") == nullptr && ui.find("Lab", "##rail", "##shell") == nullptr, "no Show the Lab switch, no Lab in the rail");
        CHECK(app.prefs.sections_open.empty(), "drawing alone stores no section state");
        CHECK(ui.click("Log", "FC 27 LE Turbo"), "open the Log section");
        ui.frames(3);
        CHECK(app.prefs.section_open("settings.log", false) && read_gui()["gui"]["sections_open"]["settings.log"] == true, "Log open, saved");
        CHECK(ui.click("Look and feel", "FC 27 LE Turbo"), "close Look and feel");
        ui.frames(3);
        CHECK(!app.prefs.section_open("settings.preferences", true) && read_gui()["gui"]["sections_open"]["settings.preferences"] == false,
              "Look and feel closed, saved");
        CHECK(ui.find("##rowheight") == nullptr, "the closed section is not drawn");
        app.prefs.sections_open.clear();
        app.save_gui_settings();
    });

    // ---- 3.0 shell (brief 3.0-shell A.6): Home, the rail
    run_case("shell UI 3.0: Home draws its five sections (status chip, My squad, Presets, Recent changes, tiles) and a tile opens its area", [&] {
        app.go(Page::Home);
        settle();
        CHECK(app.place() == place_of(Page::Home), "on Home");
        CHECK(ui.find("Home", "##rail", "##shell") != nullptr, "Home is in the rail");
        const turbo::HomeState& h = turbo::home_state();
        CHECK(h.sections == 5 && h.status_chip && h.squad && h.presets && h.recent && h.tiles == 7,
              "five sections, seven tiles: sections " + std::to_string(h.sections) + ", tiles " + std::to_string(h.tiles));
        CHECK(h.squad_table == h.in_career, "the squad table exactly when a career is loaded");
        for (const char* id : {"###home_preset_team", "###home_preset_tactics", "###home_preset_season", "###home_changes"})
            CHECK(ui.find(id) != nullptr, std::string("Home button ") + id);
        CHECK(ui.find("##gsearch") != nullptr, "the global search box in the top bar");
        CHECK(ui.click("###home_tile_" + std::to_string(static_cast<int>(Area::Tactics))), "click the Tactics tile");
        settle(2);
        CHECK(app.place() == place_of(Page::TacticPresets), "the tile opened Tactics > Presets");
        app.go(Page::Home);
        settle();
        CHECK(ui.click("###home_changes"), "click See all changes");
        settle(2);
        CHECK(app.place() == place_of(Page::ChangesUndo), "it opened Career > Changes & undo");
        app.go(Page::Home);
        settle();
        CHECK(ui.click("###home_game_settings"), "click Game settings & CPU sliders (first row)");
        settle(2);
        CHECK(app.place() == place_of(Page::GameplayPresets), "it opened Game settings > Game settings & CPU sliders");
    });

    run_case("shell UI 3.0: a rail click changes the area; the narrow rail keeps working and is saved; the search opens a player", [&] {
        app.prefs.rail_collapsed = false;
        // 3.0.1: a rail click opens the page the area showed last (area_page_); earlier cases visit every Tactics page, so this one starts from the first
        app.go(default_page(Area::Tactics));
        settle(2);  // one drawn frame stores the page in area_page_ (app.cpp:1845); go() alone does not
        app.go(Page::Home);
        settle();
        CHECK(ui.click("Tactics", "##rail", "##shell"), "click Tactics in the rail");
        settle(2);
        CHECK(app.place().area == Area::Tactics && app.place().page == default_page(Area::Tactics), "Tactics, its first page");
        CHECK(ui.click("Club", "##rail", "##shell"), "click Club in the rail");
        settle(2);
        CHECK(app.place().area == Area::Club, "Club");
        CHECK(ui.click("Squad", "##content", "##pages"), "the page strip: Squad");
        settle(2);
        CHECK(app.place() == place_of(Page::TeamPlayers), "Club > Squad");
        CHECK(ui.click("Home", "##rail", "##shell") && (settle(2), app.place() == place_of(Page::Home)), "back to Home");
        CHECK(ui.click("Club", "##rail", "##shell") && (settle(2), app.place() == place_of(Page::TeamPlayers)), "Club remembers its page");
        CHECK(ui.open_page("Players", "Staff") && app.place() == place_of(Page::Staff), "Ui::open_page still works on the rail");
        CHECK(ui.click("###railnarrow"), "narrow the rail");
        settle(2);
        CHECK(app.prefs.rail_collapsed && ui.find("Tactics", "##rail", "##shell") == nullptr && ui.find("###rail_Tactics") != nullptr,
              "narrow rail: first letters, stable ids");
        {
            std::ifstream f(app.turbo_output() / "gui_settings.json", std::ios::binary);
            nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            CHECK(!j.is_discarded() && j["gui"]["rail_collapsed"] == true && j["gui"]["version"] == "3.0.0", "saved: gui.rail_collapsed, gui.version");
        }
        CHECK(ui.click("###rail_Tactics"), "click T");
        settle(2);
        CHECK(app.place().area == Area::Tactics, "the narrow rail changes the area");
        CHECK(ui.click("###railwiden") && (settle(2), !app.prefs.rail_collapsed) && ui.find("Tactics", "##rail", "##shell") != nullptr, "wide again");
        // the global search: a player's name (Enter takes the first match) -> Players > Player with him selected
        const turbo::PlayerRow* pick = nullptr;
        for (const turbo::PlayerRow& p : app.model.players())
            if (p.name.size() >= 4) { pick = &p; break; }
        if (pick) {
            std::string q = pick->name;
            for (char& c : q) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');  // case-insensitive
            const int64_t want_id = pick->playerid;
            const std::string want_name = pick->name;
            app.sel_player = 0;
            CHECK(ui.type_into(ui.find("##gsearch"), q), "type into the search");
            settle(2);
            const turbo::PlayerRow* got = app.model.player(app.sel_player);
            CHECK(app.place() == place_of(Page::PlayerEdit) && got != nullptr && (app.sel_player == want_id || got->name == want_name),
                  "the search opened Players > Player with " + want_name);
        }
    });

    // 3.1 N1 (Defect 3, navigation loops): go() keeps the place it leaves; the page shows "< Back to <page>" and one click returns, once
    run_case("shell UI 3.1 N1: go(PlayerEdit), go(LooksStudio) -> back_place() is PlayerEdit, the button returns there, and only once", [&] {
        const int was_tab = player_edit_tab();
        app.go(Page::Home);
        settle(2);
        CHECK(app.place() == place_of(Page::Home), "start on Home");
        app.go(Page::PlayerEdit);
        settle(2);
        CHECK(app.place() == place_of(Page::PlayerEdit) && back_place() == place_of(Page::Home), "Home -> Player: back is Home");
        CHECK(ui.find("###nav_back") != nullptr, "the Player page shows the back button");
        player_edit_tab() = 3;  // the tab the user is working on when he leaves
        settle(2);
        CHECK(player_edit_tab() == 3, "the Player page shows its Callname tab (3)");
        app.go(Page::LooksStudio);
        settle(2);
        CHECK(app.place() == place_of(Page::LooksStudio), "on the Appearance Editor");
        CHECK(back_place() == place_of(Page::PlayerEdit), "back_place() is the Player page");
        CHECK(ui.find("###nav_back") != nullptr, "the Looks page (no page strip) draws the back button");
        CHECK(ui.click("###nav_back"), "click < Back to Player");
        settle(2);
        CHECK(app.place() == place_of(Page::PlayerEdit), "back landed on the Player page");
        CHECK(player_edit_tab() == 3, "Back returns to the same tab it left (Callname, 3), not to the first tab");
        CHECK(!back_place().valid() && ui.find("###nav_back") == nullptr, "going back does not bounce: nothing to go back to now");
        CHECK(!app.back(), "back() with nothing to go back to is refused");
        // a page-strip click and a rail click leave a place too
        app.go(Page::Kits);
        settle(2);
        CHECK(ui.click(page_label(Page::Identity), "##content", "##pages"), "strip click Kits -> Name, colours & crest");
        settle(2);
        CHECK(app.place() == place_of(Page::Identity) && back_place() == place_of(Page::Kits), "a strip click keeps Kits as the back place");
        CHECK(app.back(), "App::back()");
        settle(2);
        CHECK(app.place() == place_of(Page::Kits) && !back_place().valid(), "App::back() lands on Kits, once");
        player_edit_tab() = was_tab;
    });

    run_case("shell UI 3.0: gui.version is written as 3.0.0 and compares as a version", [&] {
        using turbo::GuiPrefs;
        GuiPrefs p;
        nlohmann::json out = nlohmann::json::object();
        p.save(out);
        CHECK(out["gui"]["version"] == "3.0.0", "saved with gui.version 3.0.0");
        CHECK(turbo::gui_version_compare("3.0.0", "3.0") == 0 && turbo::gui_version_compare("2.21.0", "3.0.0") < 0 &&
                  turbo::gui_version_compare("3.10.1", "3.0.0") > 0, "version compare");
    });

    app.go(Page::PlayerEdit);
    settle();
}

}  // namespace
