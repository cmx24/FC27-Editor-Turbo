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
        CHECK(kAreaCount == 7, "six areas plus Advanced");
        const char* labels[] = {"Players", "Appearance Editor", "Team", "Tactics", "Career", "Settings", "Advanced"};
        for (int a = 0; a < kAreaCount; ++a) CHECK(std::string(area_label(static_cast<Area>(a))) == labels[a], std::string("area label ") + labels[a]);
        CHECK(std::string(area_label(Area::Count)).empty(), "no label for Count");
        const Page defaults[] = {Page::PlayerEdit, Page::LooksStudio, Page::Kits, Page::TacticPresets, Page::Standings, Page::Settings, Page::AdvDatabase};
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
        const Row rows[] = {
            {turbo::TabPlayers, Area::Squad, Page::PlayerEdit},
            {turbo::TabTeams, Area::Squad, Page::Squad},
            {turbo::TabManagers, Area::Squad, Page::Staff},
            {turbo::TabCompetitions, Area::Career, Page::Standings},
            {turbo::TabDatabase, Area::Advanced, Page::AdvDatabase},
            {turbo::TabTools, Area::Career, Page::Transfers},
            {turbo::TabStatus, Area::Settings, Page::Settings},
            {turbo::TabGameSettings, Area::Tactics, Page::GameplayPresets},
        };
        CHECK(turbo::TabCount == 8 && sizeof(rows) / sizeof(rows[0]) == 8, "eight legacy tabs, all covered");
        for (const Row& r : rows) {
            const Place pl = area_for(r.tab);
            CHECK(pl.valid() && pl.area == r.a && pl.page == r.p, "legacy tab " + std::to_string(r.tab) + " -> " + page_label(r.p));
            CHECK(legacy_tab_of(r.p) >= 0, "inverse exists for " + std::string(page_label(r.p)));
        }
        CHECK(legacy_tab_of(Page::PlayerEdit) == 0 && legacy_tab_of(Page::Standings) == 3 && legacy_tab_of(Page::AdvDatabase) == 4 &&
                  legacy_tab_of(Page::Settings) == 6 && legacy_tab_of(Page::GameplayPresets) == 7 && legacy_tab_of(Page::Kits) == 1 &&
                  legacy_tab_of(Page::Transfers) == 5 && legacy_tab_of(Page::Staff) == 2 && legacy_tab_of(Page::None) == -1,
              "legacy_tab_of: where each panel lived");
        for (Page p : shell_all_pages()) {
            const int t = legacy_tab_of(p);
            // 2.16 step 4: the pages that are new in the re-cut navigation never lived in a legacy tab
            const bool is_new = p == Page::CreatePlayer || p == Page::CommentaryNames || p == Page::LooksStudio || p == Page::TeamSheet ||
                                p == Page::BulkEdit || p == Page::TeamPlayers || p == Page::TeamTransfers;  // 2.20 pages
            CHECK(is_new ? t == -1 : (t >= 0 && t < turbo::TabCount), std::string("every older page has a legacy tab, the new ones none: ") + page_label(p));
        }
        for (int bad : {-1, 8, 99, -100}) CHECK(!area_for(bad).valid(), "out of range -> invalid: " + std::to_string(bad));
        // the legacy hide rule: the tabs Simple hid (Database, Game settings) land in a page that is hidden or flagged accordingly
        CHECK(!page_visible(area_for(turbo::TabDatabase).page, false), "Database stays hidden in Simple");
    });

    run_case("shell: Simple hides exactly the Advanced area, Advanced shows every page", [&] {
        using namespace turbo::shell;
        for (int a = 0; a < kAreaCount; ++a) {
            CHECK(area_visible(static_cast<Area>(a), true), "Advanced shows every area");
            CHECK(area_visible(static_cast<Area>(a), false) == (static_cast<Area>(a) != Area::Advanced), "Simple hides only Advanced");
        }
        CHECK(!area_visible(Area::Count, true) && !area_visible(Area::Count, false), "Count is never visible");
        std::set<int> hidden;
        for (Page p : shell_all_pages()) {
            CHECK(page_visible(p, true), std::string("Advanced shows ") + page_label(p));
            if (!page_visible(p, false)) hidden.insert(static_cast<int>(p));
        }
        std::set<int> want;
        for (Page p : pages_of(Area::Advanced)) want.insert(static_cast<int>(p));
        CHECK(hidden == want, "the hidden set of Simple is the Advanced strip");
        CHECK(!page_visible(Page::None, true) && !page_visible(Page::Count, true), "None/Count never visible");
        // the not-proven set: tactics sliders, match setup, gameplay tuning (all under Advanced); the mixed page is visible in Simple
        const Page np[] = {Page::AdvTactics, Page::AdvMatchSetup, Page::AdvGameplayTuning};
        std::set<int> npset;
        for (Page p : shell_all_pages()) if (page_unproven(p)) npset.insert(static_cast<int>(p));
        CHECK(npset.size() == 3, "exactly three pages carry the amber line");
        for (Page p : np) CHECK(page_unproven(p) && !page_visible(p, false) && page_visible(p, true), std::string("not proven, Advanced only: ") + page_label(p));
        CHECK(!page_unproven(Page::GameplayPresets) && page_effect_unmeasured(Page::GameplayPresets) && page_visible(Page::GameplayPresets, false),
              "Gameplay presets: visible in Simple, one muted sentence, no banner");
        CHECK(!page_unproven(Page::TacticPresets) && !page_unproven(Page::Kits) && !page_effect_unmeasured(Page::Kits), "proven pages carry nothing");
        // consistent with the old tab rule: what the old top-level hiding covered is still hidden or reached via Gameplay presets
        CHECK(!turbo::top_tab_visible(turbo::TabDatabase, false) && !page_visible(Page::AdvDatabase, false), "Database hidden in Simple in both");
    });

    run_case("shell: gui.sections_open round trip (defaults, set / get, other keys kept, junk ignored)", [&] {
        using turbo::GuiPrefs;
        GuiPrefs p;
        CHECK(p.sections_open.empty() && p.section_open("x.a", true) && !p.section_open("x.a", false), "unset: the page's default");
        CHECK(p.set_section_open("settings.log", true) && !p.set_section_open("settings.log", true), "set reports a change once");
        CHECK(p.set_section_open("settings.preferences", false) && !p.section_open("settings.preferences", true), "closed beats the default");
        nlohmann::json j = nlohmann::json::object();
        j["gui"]["ui_scale"] = 1.2;
        p.advanced_mode = true;
        p.save(j);
        CHECK(j["gui"]["ui_scale"] == 1.2 && j["gui"]["sections_open"]["settings.log"] == true &&
                  j["gui"]["sections_open"]["settings.preferences"] == false,
              "saved next to the other keys: " + j["gui"].dump());
        const GuiPrefs q = GuiPrefs::load(j);
        CHECK(q.advanced_mode && q.sections_open.size() == 2 && q.section_open("settings.log", false) && !q.section_open("settings.preferences", true),
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
    run_case("shell: page registry (register once, draw only visible pages, clear)", [&] {
        using namespace turbo::shell;
        clear_registry_for_tests();
        g_shell_calls = 0;
        CHECK(registered_page(Page::Kits) == nullptr, "nothing registered");
        CHECK(!draw_page(app, Page::Kits, true) && g_shell_calls == 0, "unregistered page draws nothing");
        CHECK(register_page(Page::Kits, shell_fake_page), "registered");
        CHECK(!register_page(Page::Kits, shell_other_page) && registered_page(Page::Kits) == shell_fake_page, "second registration ignored");
        CHECK(!register_page(Page::None, shell_fake_page) && !register_page(Page::Count, shell_fake_page) && !register_page(Page::Squad, nullptr),
              "None, Count and a null function are refused");
        CHECK(draw_page(app, Page::Kits, false) && g_shell_calls == 1, "visible page draws in Simple");
        CHECK(register_page(Page::AdvDatabase, shell_fake_page), "advanced page registered");
        CHECK(!draw_page(app, Page::AdvDatabase, false) && g_shell_calls == 1, "Advanced page not drawn in Simple");
        CHECK(draw_page(app, Page::AdvDatabase, true) && g_shell_calls == 2, "Advanced page drawn in Advanced");
        clear_registry_for_tests();
        CHECK(registered_page(Page::Kits) == nullptr && !draw_page(app, Page::Kits, true), "cleared");
        for (int i = 1; i < kPageCount; ++i)
            if (saved[i]) register_page(static_cast<Page>(i), saved[i]);
        CHECK(registered_page(Page::Settings) == saved[static_cast<int>(Page::Settings)], "start-up registrations restored");
    });
}

// UI cases: the area tabs, the page strip, request_tab / go(), the Advanced gate, the Settings page (T1)
static void test_shell_ui(turbo::App& app, Ui& ui) {
    using namespace turbo::shell;
    const bool was_advanced = app.prefs.advanced_mode;
    auto settle = [&](int n = 6) { ui.frames(n); };

    run_case("shell UI: area tabs and the page strip (Simple has no Advanced tab); legacy request_tab lands on area_for", [&] {
        app.prefs.advanced_mode = false;
        app.request_tab = 0;
        settle();
        CHECK(ui.find("Players") && ui.find("Appearance Editor") && ui.find("Team") && ui.find("Tactics") && ui.find("Career") && ui.find("Settings"),
              "the six area tabs");
        CHECK(ui.find("Advanced", "FC 27 LE Turbo") == nullptr, "no Advanced tab in Simple");
        CHECK(app.place() == area_for(0), "request_tab 0 -> Squad / Player edit");
        CHECK(ui.find("Edit a player") && ui.find("Managers & staff"), "the Players page strip");
        for (int t : {1, 2, 3, 5, 6, 7}) {
            app.request_tab = t;
            settle();
            CHECK(app.place() == area_for(t), "request_tab " + std::to_string(t) + " -> " + page_label(area_for(t).page));
        }
        const Place before = app.place();
        app.request_tab = 4;  // Database: hidden in Simple, as before
        settle();
        CHECK(app.place() == before, "request_tab 4 (Database) is ignored in Simple");
        CHECK(app.request_tab == -1, "request consumed");
        app.request_tab = 99;
        settle(2);
        CHECK(app.place() == before && app.request_tab == -1, "an out of range request is dropped");
    });

    run_case("shell UI: go(Area, Page) for every visible page; Advanced pages need the Advanced view", [&] {
        app.prefs.advanced_mode = false;
        for (int a = 0; a < kAreaCount; ++a)
            for (Page p : pages_of(static_cast<Area>(a))) {
                if (!page_visible(p, false)) continue;
                app.go(static_cast<Area>(a), p);
                settle();
                CHECK(app.place().area == static_cast<Area>(a) && app.place().page == p,
                      std::string("go -> ") + area_label(static_cast<Area>(a)) + " / " + page_label(p));
            }
        const Place at = app.place();
        app.go(Area::Advanced, Page::AdvDatabase);
        settle();
        CHECK(app.place() == at, "go to an Advanced page is ignored in Simple");
        app.go(Area::Club, Page::Standings);  // wrong area for the page
        settle();
        CHECK(app.place() == at, "a page in the wrong area is ignored");
        app.prefs.advanced_mode = true;
        settle();
        CHECK(ui.find("Advanced", "FC 27 LE Turbo") != nullptr, "Advanced tab with the toggle");
        for (Page p : pages_of(Area::Advanced)) {
            app.go(p);  // the one-argument form takes the page's own area
            settle();
            CHECK(app.place() == place_of(p), std::string("go -> Advanced / ") + page_label(p));
        }
        app.prefs.advanced_mode = false;
        settle();
        CHECK(app.place().area != Area::Advanced, "turning Advanced off leaves the Advanced area");
    });

    run_case("shell UI: a registered page is drawn instead of the legacy panel; an unregistered one falls back", [&] {
        PageFn saved[kPageCount] = {};
        for (int i = 1; i < kPageCount; ++i) saved[i] = registered_page(static_cast<Page>(i));
        clear_registry_for_tests();
        g_shell_calls = 0;
        app.prefs.advanced_mode = false;
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

    run_case("shell UI: Settings page: Advanced view toggle and collapsing sections are saved in gui_settings.json", [&] {
        auto read_gui = [&]() {
            std::ifstream f(app.turbo_output() / "gui_settings.json", std::ios::binary);
            nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            return j.is_discarded() ? nlohmann::json::object() : j;
        };
        CHECK(registered_page(Page::Settings) != nullptr, "the Settings page is registered");
        app.prefs.advanced_mode = false;
        app.prefs.sections_open.clear();
        app.go(Page::Settings);
        settle();
        CHECK(app.place() == place_of(Page::Settings), "on the Settings page");
        // 2.22: "Look and feel" (open by default) is the first section, so the later ones can start below the visible part: scroll to them as a user would
        CHECK(ui.find_scrolling("Log", "", "") != nullptr && ui.find_scrolling("Preferences", "", "") != nullptr, "sections drawn");
        CHECK(ui.find_scrolling("##rowheight", "", "") != nullptr, "the Row height slider of Look and feel (replaces the List spacing box)");
        CHECK(ui.find_scrolling("Change...##hotkey", "", "status") != nullptr, "the preferences of the old Status tab");
        CHECK(ui.click("###advview", "FC 27 LE Turbo"), "click the toggle");
        ui.frames(3);
        CHECK(app.prefs.advanced_mode && read_gui()["gui"]["advanced_mode"] == true, "Advanced on, saved");
        CHECK(ui.find("Advanced", "FC 27 LE Turbo") != nullptr, "the Advanced tab appeared");
        CHECK(ui.click("###advview", "FC 27 LE Turbo"), "click again");
        ui.frames(3);
        CHECK(!app.prefs.advanced_mode && read_gui()["gui"]["advanced_mode"] == false, "Advanced off, saved");
        CHECK(app.prefs.sections_open.empty(), "drawing alone stores no section state");
        CHECK(ui.click("Log", "FC 27 LE Turbo"), "open the Log section");
        ui.frames(3);
        CHECK(app.prefs.section_open("settings.log", false) && read_gui()["gui"]["sections_open"]["settings.log"] == true, "Log open, saved");
        CHECK(ui.click("Preferences", "FC 27 LE Turbo"), "close Preferences");
        ui.frames(3);
        CHECK(!app.prefs.section_open("settings.preferences", true) && read_gui()["gui"]["sections_open"]["settings.preferences"] == false,
              "Preferences closed, saved");
        CHECK(ui.find("Change...##hotkey", "", "status") == nullptr, "the closed section is not drawn");
        app.prefs.sections_open.clear();
        app.save_gui_settings();
    });

    app.prefs.advanced_mode = was_advanced;
    app.go(Page::PlayerEdit);
    settle();
}

}  // namespace
