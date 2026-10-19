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

#include "ui/ui_shell.h"

namespace nav220_test {

using turbo::shell::Area;
using turbo::shell::Page;

// Registered in BOTH registries on purpose (the 2.16 page replaces the older page of the same id until the old pages are asked for).
// Wiring chunk 2 adds the pages it replaces; anything else registered twice is a mistake to look at.
static const Page kBoth[] = {Page::Kits,      Page::Identity, Page::TacticPresets, Page::TeamSheet, Page::GameplayPresets,
                             Page::Transfers, Page::JobOffer, Page::ChangesUndo,   Page::AdvExport};

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
        CHECK(area_of(Page::PlayerEdit) == Area::Squad && std::string(page_label(Page::PlayerEdit)) == "Edit a player", "Edit a player is in Players");
    });

    run_case("Navigation 2.20: Looks studio is called Appearance Editor on the tab and on the page", [&] {
        CHECK(std::string(page_label(Page::LooksStudio)) == "Appearance Editor", "page label");
        CHECK(std::string(area_label(Area::Looks)) == "Appearance Editor", "area label");
        CHECK(area_of(Page::LooksStudio) == Area::Looks && page_visible(Page::LooksStudio, false), "it sits in its own area, visible in the Simple view");
        for (int i = 1; i < kPageCount; ++i) CHECK(std::string(page_label(static_cast<Page>(i))) != "Looks studio", "no page still says Looks studio");
        for (int a = 0; a < kAreaCount; ++a) CHECK(std::string(area_label(static_cast<Area>(a))) != "Looks studio", "no area says Looks studio");
        CHECK(registered_page(Page::LooksStudio) != nullptr, "the studio's draw function is registered for it");
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
    run_case("Navigation 2.20: the Appearance Editor tab opens the page; no Faces & looks tab; Players still has Edit a player", [&] {
        app.prefs.advanced_mode = false;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        CHECK(ui.find("Appearance Editor", "FC 27 LE Turbo", "##shell") != nullptr, "the Appearance Editor tab");
        CHECK(ui.find("Faces & looks", "FC 27 LE Turbo", "##shell") == nullptr, "no Faces & looks tab");
        CHECK(ui.find("Faces & hair", "FC 27 LE Turbo") == nullptr && ui.find("Body, tattoos & gear", "FC 27 LE Turbo") == nullptr, "no placeholder buttons");
        CHECK(ui.click("Appearance Editor", "FC 27 LE Turbo", "##shell"), "click the tab");
        ui.frames(4);
        CHECK(app.place() == place_of(Page::LooksStudio), "it opened the Appearance Editor");
        CHECK(ui.find("###navgo") == nullptr, "no signpost button on it");
        CHECK(ui.click("Players", "FC 27 LE Turbo", "##shell"), "back to Players");
        ui.frames(4);
        CHECK(ui.find("Edit a player", "FC 27 LE Turbo", "##pages") != nullptr, "the Players strip still has Edit a player");
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });
}

}  // namespace nav220_test
