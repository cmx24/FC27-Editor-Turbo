// Native tests (4.0 GUI overhaul, brief 4.0-gui-desk.md S0a-code): the dev-service op open_page {area, page}.
// Included by test_main.cpp after test_nav_220.h; devpage_test::ui_cases(app, ui) runs inside the UI block. Cases are named
// "dev service open_page: ..." so TURBO_TEST_ONLY="dev service open_page" runs them alone.
//
// The op is driven through DevService::run, the function the dev service thread calls for every request (win/devtools_win.cpp),
// with no file IO and no game: the env's sleep runs one overlay frame (Ui::frame -> App::draw, whose first call is the overlay's
// half of the op), so the request, the hand-off, App::go, draw_pages and the answer all run as in the game, one thread instead of two.
// Only names that exist at 9a1e888 are used here, so on that commit this file builds and the op answers "unknown op open_page".
//
// What is proven: every page of every area opens by the labels drawn on the rail and the page strip, the answer names the page drawn
// (app.place()); the names are the ones Ui::open_page clicks; unknown area / page and missing fields are
// refused by name and leave the place alone; hidden, the op does not show Turbo, answers pending and the page is there once shown;
// a request no frame answers gives up after 5 s and a later frame does not navigate; a request the overlay TOOK (one frame, shown or
// hidden) but never confirmed is answered ok:false taken:true, never as "page not opened" (the page is open, or opens when shown),
// and the confirmation that comes late is dropped.
#pragma once
#include <memory>
#include <string>

#include "core/devops.h"
#include "ui/ui_shell.h"

namespace devpage_test {

using turbo::shell::Area;
using turbo::shell::Page;
using turbo::shell::Place;

struct Rig {
    SimMemory mem;  // the old op table refuses an op without memory before it looks at the name: give it some
    turbo::DevEnv env;
    double clock = 0.0;
    int frames = 0;
    int frame_budget = -1;  // frames_run only: at most this many overlay frames per open() (-1: no limit), then the overlay stalls
    std::unique_ptr<turbo::DevService> svc;
    int id = 0;
    Rig(Ui& ui, bool frames_run) {
        env.mem = &mem;
        env.clock = [this]() { return clock; };
        env.sleep = [this, &ui, frames_run](int ms) {
            clock += ms / 1000.0;
            if (frames_run && (frame_budget < 0 || frames < frame_budget)) {
                ui.frame();
                ++frames;
            }
        };
        env.out_dir = g_out;
        svc = std::make_unique<turbo::DevService>(env);
    }
    json open(const std::string& area, const std::string& page) {
        frames = 0;
        return svc->run({{"id", ++id}, {"op", "open_page"}, {"area", area}, {"page", page}});
    }
};

static std::string where(const App& app) {
    const Place p = app.place();
    return std::string(turbo::shell::area_label(p.area)) + " > " + turbo::shell::page_label(p.page);
}

static void ui_cases(App& app, Ui& ui) {
    using namespace turbo::shell;
    const bool vis0 = app.visible;

    run_case("dev service open_page: every page of every area opens by its rail and strip labels, the answer names the page drawn", [&] {
        app.visible = true;
        ui.frames(2);
        Rig rig(ui, true);
        int opened = 0;
        for (int ai = 0; ai < kAreaCount; ++ai) {
            const Area a = static_cast<Area>(ai);
            for (Page p : pages_of(a)) {
                const json r = rig.open(area_label(a), page_label(p));
                const std::string what = std::string(area_label(a)) + " > " + page_label(p) + ": " + r.dump();
                CHECK(r.value("ok", false), "opened: " + what);
                CHECK(app.place() == (Place{a, p}), "app.place() is the page asked for (now " + where(app) + "): " + what);
                CHECK(r.contains("shown") && r["shown"].is_object() && r["shown"].value("area", "") == area_label(a) &&
                          r["shown"].value("page", "") == page_label(p),
                      "the answer names the page drawn: " + what);
                CHECK(r.value("overlay_visible", false) && !r.value("pending", true), "shown, not pending: " + what);
                CHECK(r.value("op", "") == "open_page" && r.value("id", 0) == rig.id, "id and op echoed: " + what);
                CHECK(rig.frames >= 1 && rig.frames <= 3, "answered within 3 overlay frames (" + std::to_string(rig.frames) + "): " + what);
                ++opened;
            }
        }
        CHECK(opened >= 30, "the sweep covered the pages of all areas (" + std::to_string(opened) + ")");
        // ASCII case is ignored; a one-page area (no strip, the click helper cannot reach it) opens too
        json r = rig.open("career", "STANDINGS");
        CHECK(r.value("ok", false) && app.place() == place_of(Page::Standings), "case ignored: " + r.dump());
        r = rig.open("Looks", "Appearance Editor");
        CHECK(r.value("ok", false) && app.place() == place_of(Page::LooksStudio), "one-page area: " + r.dump());
    });

    run_case("dev service open_page: the op and the click helper (Ui::open_page) use the same names", [&] {
        app.visible = true;
        Rig rig(ui, true);
        const struct {
            const char* area;
            const char* page;
        } pairs[] = {{"Career", "Standings"}, {"Club", "Kits"}, {"Players", "Staff"}, {"Tactics", "Team sheet & bench"}};
        for (const auto& pr : pairs) {
            CHECK(rig.open("Home", "Home").value("ok", false), "back to Home first");
            CHECK(ui.open_page(pr.area, pr.page), std::string("clicks reach ") + pr.area + " > " + pr.page);
            const Place clicked = app.place();
            CHECK(rig.open("Home", "Home").value("ok", false) && app.place() == place_of(Page::Home), "Home again");
            const json r = rig.open(pr.area, pr.page);
            CHECK(r.value("ok", false) && app.place() == clicked, std::string("the op lands where the clicks landed: ") + pr.area + " > " + pr.page + " " + r.dump());
        }
    });

    run_case("dev service open_page: unknown area or page, a deleted Lab page, missing fields: refused by name, place kept", [&] {
        app.visible = true;
        Rig rig(ui, true);
        CHECK(rig.open("Career", "Standings").value("ok", false), "start on Career > Standings");
        const Place before = app.place();
        json r = rig.open("Players", "Nope");
        std::string e = r.value("error", "");
        CHECK(!r.value("ok", true) && e.find("unknown page 'Nope'") != std::string::npos && e.find("Players") != std::string::npos &&
                  e.find("Many players at once") != std::string::npos,
              "unknown page named, with the pages of its area: " + r.dump());
        CHECK(app.place() == before, "place kept after the unknown page (" + where(app) + ")");
        r = rig.open("Teams", "Kits");
        e = r.value("error", "");
        CHECK(!r.value("ok", true) && e.find("unknown area 'Teams'") != std::string::npos && e.find("Club") != std::string::npos,
              "unknown area named, with the areas: " + r.dump());
        CHECK(rig.open("Career", "Kits").value("ok", true) == false, "a page of another area is unknown in this one");
        CHECK(app.place() == before, "place kept after the unknown area (" + where(app) + ")");
        // 2026-10-31 (5.0c menus step 6): the Lab is gone, so its name is an unknown area now, like any other
        r = rig.open("Lab", "Database");
        e = r.value("error", "");
        CHECK(!r.value("ok", true) && e.find("unknown area 'Lab'") != std::string::npos, "the deleted Lab is an unknown area: " + r.dump());
        CHECK(app.place() == before, "place kept after the deleted Lab page (" + where(app) + ")");
        r = rig.svc->run({{"op", "open_page"}, {"area", "Career"}});
        CHECK(!r.value("ok", true) && r.value("error", "").find("page") != std::string::npos, "page missing: " + r.dump());
        r = rig.svc->run({{"op", "open_page"}, {"page", "Standings"}, {"area", 3}});
        CHECK(!r.value("ok", true) && r.value("error", "").find("area") != std::string::npos, "area not a string: " + r.dump());
        CHECK(app.place() == before, "place kept after the bad requests (" + where(app) + ")");
    });

    run_case("dev service open_page: Turbo hidden -> not shown by the op, answer pending, the page is there once Turbo is shown", [&] {
        app.visible = true;
        Rig rig(ui, true);
        CHECK(rig.open("Career", "Standings").value("ok", false), "start on Career > Standings");
        app.visible = false;
        const json r = rig.open("Club", "Kits");
        CHECK(r.value("ok", false) && !r.value("overlay_visible", true) && r.value("pending", false) && r.contains("shown") && r["shown"].is_null(),
              "hidden: ok, pending, nothing shown: " + r.dump());
        CHECK(r.contains("requested") && r["requested"].is_object() && r["requested"].value("page", "") == "Kits", "the answer names the page asked for: " + r.dump());
        CHECK(!app.visible, "the op did not show Turbo");
        app.visible = true;  // the lane's show key
        ui.frames(2);
        CHECK(app.place() == place_of(Page::Kits), "shown: Club > Kits (now " + where(app) + ")");
    });

    run_case("dev service open_page: no overlay frame -> gives up after 5 s, a later frame does not navigate", [&] {
        app.visible = true;
        {
            Rig rig(ui, true);
            CHECK(rig.open("Career", "Standings").value("ok", false), "start on Career > Standings");
        }
        Rig stalled(ui, false);  // sleep runs no frame: the game is not drawing
        const json r = stalled.open("Club", "Kits");
        CHECK(!r.value("ok", true) && r.value("error", "").find("no frame in 5 s") != std::string::npos, "timed out by name: " + r.dump());
        CHECK(r.contains("taken") && r["taken"].is_boolean() && !r["taken"].get<bool>(), "nobody took it: taken:false: " + r.dump());
        CHECK(stalled.clock >= 5.0 && stalled.clock < 6.0, "waited about 5 s (" + std::to_string(stalled.clock) + ")");
        ui.frames(3);
        CHECK(app.place() == place_of(Page::Standings), "the abandoned request did not navigate (now " + where(app) + ")");
        Rig rig(ui, true);
        const json r2 = rig.open("Tactics", "Presets");
        CHECK(r2.value("ok", false) && app.place() == place_of(Page::TacticPresets), "the next request works: " + r2.dump());
    });

    run_case("dev service open_page: request taken by one frame, then the overlay stalls -> ok:false taken:true, not 'page not opened'; the late answer is dropped", [&] {
        app.visible = true;
        {
            Rig rig(ui, true);
            CHECK(rig.open("Career", "Standings").value("ok", false), "start on Career > Standings");
        }
        Rig rig(ui, true);
        rig.frame_budget = 1;  // frame 1 takes the request and go() opens the page; frame 2 (the answer) never comes in time
        const json r = rig.open("Club", "Kits");
        CHECK(rig.frames == 1, "exactly one overlay frame ran (" + std::to_string(rig.frames) + ")");
        CHECK(!r.value("ok", true), "ok:false, opening is not confirmed: " + r.dump());
        CHECK(r.contains("taken") && r["taken"].is_boolean() && r["taken"].get<bool>(), "taken:true: " + r.dump());
        CHECK(r.value("error", "").find("page not opened") == std::string::npos, "not the 'page not opened' text: " + r.dump());
        CHECK(r.value("error", "").find("took the request") != std::string::npos && r.value("error", "").find("read the current page") != std::string::npos,
              "the error says the overlay took it and to read the current page: " + r.dump());
        CHECK(rig.clock >= 5.0 && rig.clock < 6.0, "waited about 5 s (" + std::to_string(rig.clock) + ")");
        ui.frames(3);  // the confirmation arrives late: dropped, the page stays open
        CHECK(app.place() == place_of(Page::Kits), "shown case: the page IS open (now " + where(app) + ")");
        Rig next(ui, true);
        const json r2 = next.open("Tactics", "Presets");
        CHECK(r2.value("ok", false) && !r2.contains("taken") && app.place() == place_of(Page::TacticPresets) && r2.contains("requested") &&
                  r2["requested"].value("page", "") == "Presets",
              "the next request gets its own answer, not the dropped one: " + r2.dump());
    });

    run_case("dev service open_page: taken while Turbo is hidden, then no frame -> ok:false taken:true; the page opens once Turbo is shown", [&] {
        app.visible = true;
        {
            Rig rig(ui, true);
            CHECK(rig.open("Career", "Standings").value("ok", false), "start on Career > Standings");
        }
        app.visible = false;
        Rig rig(ui, true);
        rig.frame_budget = 1;
        const json r = rig.open("Club", "Kits");
        CHECK(rig.frames == 1, "exactly one overlay frame ran (" + std::to_string(rig.frames) + ")");
        CHECK(!r.value("ok", true) && r.contains("taken") && r["taken"].is_boolean() && r["taken"].get<bool>(), "ok:false taken:true: " + r.dump());
        CHECK(r.value("error", "").find("page not opened") == std::string::npos, "not the 'page not opened' text: " + r.dump());
        CHECK(!app.visible, "the op did not show Turbo");
        app.visible = true;
        ui.frames(3);
        CHECK(app.place() == place_of(Page::Kits), "hidden case: shown, the page is there (now " + where(app) + ")");
    });

    app.visible = vis0;
    ui.frames(2);
}

}  // namespace devpage_test
