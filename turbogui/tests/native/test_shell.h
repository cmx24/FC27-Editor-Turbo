// Native tests (2026-10-16, GUI revamp contract, GUI_REVAMP_SPEC.md): the shell contract of ui/ui_shell.h and the palette of
// ui/ui_theme.h. Included by test_main.cpp after test_ux_resize.h; test_shell() runs the pure cases, test_shell_app(app)
// (inside the UI block, a real App) the page registry cases that need an App&.
// Track T1 extends this file with the tab bar / request_tab cases.
#pragma once
#include <cmath>
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

static int g_shell_calls = 0;
static void shell_fake_page(turbo::App&) { ++g_shell_calls; }
static void shell_other_page(turbo::App&) { g_shell_calls += 100; }

static void test_shell() {
    run_case("shell: areas and pages (labels, one area per page, strips in order, defaults)", [&] {
        using namespace turbo::shell;
        CHECK(kAreaCount == 6, "five areas plus Advanced");
        const char* labels[] = {"Squad & Players", "Club & Kits", "Tactics & Gameplay", "Career tools", "Settings", "Advanced"};
        for (int a = 0; a < kAreaCount; ++a) CHECK(std::string(area_label(static_cast<Area>(a))) == labels[a], std::string("area label ") + labels[a]);
        CHECK(std::string(area_label(Area::Count)).empty(), "no label for Count");
        const Page defaults[] = {Page::PlayerEdit, Page::Kits, Page::TacticPresets, Page::Transfers, Page::Settings, Page::AdvDatabase};
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
            if (l == "CMTracker") continue;
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
            {turbo::TabCompetitions, Area::Tactics, Page::Standings},
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
                  legacy_tab_of(Page::Scouting) == 5 && legacy_tab_of(Page::Staff) == 2 && legacy_tab_of(Page::None) == -1,
              "legacy_tab_of: where each panel lived");
        for (Page p : shell_all_pages()) {
            const int t = legacy_tab_of(p);
            CHECK(t >= 0 && t < turbo::TabCount, std::string("every page has a legacy tab: ") + page_label(p));
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
    });
}

}  // namespace
