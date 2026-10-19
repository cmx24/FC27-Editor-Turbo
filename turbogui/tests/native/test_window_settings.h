// Native tests (2.20 S3-window, Cassio 10-19: "why doesn't it just start maximized"): the main panel's place, size and maximized state
// in gui_settings.json gui.window (ui/gui_prefs.h WindowPrefs / WindowTracker, App::draw). window_settings_test::run() has the pure
// cases; window_settings_test::ui_cases(app, ui, le) drives the real panel (Maximize / Restore, a resize that rests, a restart).
// Included by test_main.cpp after test_ux_resize.h.
#pragma once
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "ui/gui_prefs.h"

namespace window_settings_test {

using nlohmann::json;
using namespace turbo;

static bool near(float a, float b, float tol = 1.5f) { return std::fabs(a - b) <= tol; }

static void run() {
    run_case("window: a missing block, no gui object or junk = maximized (the whole viewport)", [&] {
        const char* docs[] = {"{}", "{\"gui\":{}}", "{\"gui\":{\"window\":3}}", "{\"gui\":{\"window\":{}}}", "{\"gui\":{\"window\":{\"x\":\"a\",\"w\":null}}}",
                              "{\"gui\":{\"window\":{\"x\":10,\"y\":10,\"w\":900}}}"};
        for (const char* d : docs) {
            const WindowPrefs p = WindowPrefs::load(json::parse(d));
            CHECK(p.maximized && !p.has_rect, std::string("default for ") + d);
            const WindowPlan pl = window_plan(p, 1920.0f, 1080.0f, false, WindowRect{60, 60, 1180, 760});
            CHECK(pl.maximized && pl.rect == (WindowRect{0, 0, 1920, 1080}), std::string("plan = the whole viewport for ") + d);
        }
        CHECK(WindowPrefs::load(json(5)).maximized && WindowPrefs::load(json::object()).maximized, "not an object / empty");
    });

    run_case("window: the block round trips next to the other keys", [&] {
        WindowPrefs w;
        w.maximized = false;
        w.has_rect = true;
        w.rect = WindowRect{120.4f, 80.0f, 1200.6f, 800.0f};
        json j = json::object();
        j["gui"]["ui_scale"] = 1.2;
        j["asset_scope"]["x"] = "all";
        w.save(j);
        CHECK(j["gui"]["ui_scale"] == 1.2 && j["asset_scope"]["x"] == "all", "other keys kept");
        CHECK(j["gui"]["window"]["x"] == 120 && j["gui"]["window"]["y"] == 80 && j["gui"]["window"]["w"] == 1201 && j["gui"]["window"]["h"] == 800 &&
                  j["gui"]["window"]["maximized"] == false,
              "block: " + j["gui"]["window"].dump());
        const WindowPrefs b = WindowPrefs::load(j);
        CHECK(!b.maximized && b.has_rect && b.rect == (WindowRect{120, 80, 1201, 800}), "loaded back (rounded to pixels)");
        const WindowPlan pl = window_plan(b, 1920.0f, 1080.0f, false, WindowRect{});
        CHECK(!pl.maximized && pl.rect == b.rect, "a usable rectangle is used at start");
        // maximized true keeps the restore rectangle but starts maximized
        WindowPrefs m = b;
        m.maximized = true;
        json k = json::object();
        m.save(k);
        const WindowPrefs mb = WindowPrefs::load(k);
        CHECK(mb.maximized && mb.has_rect && window_plan(mb, 1920.0f, 1080.0f, false, WindowRect{}).maximized, "maximized: starts maximized, rectangle kept for Restore");
        // only the flag known yet (first run, Maximize never left)
        json f = json::object();
        WindowPrefs{}.save(f);
        CHECK(f["gui"]["window"].size() == 1 && f["gui"]["window"]["maximized"] == true, "no rectangle yet: only the flag: " + f["gui"]["window"].dump());
        // GuiPrefs carries it
        GuiPrefs gp;
        gp.window = b;
        json g = json::object();
        gp.save(g);
        CHECK(GuiPrefs::load(g).window.rect == b.rect && !GuiPrefs::load(g).window.maximized, "GuiPrefs save / load carries gui.window");
    });

    run_case("window: off-screen, too small, too big or unreachable rectangles fall back to maximized", [&] {
        const float dw = 1920.0f, dh = 1080.0f;
        struct C { WindowRect r; bool ok; const char* why; };
        const C cases[] = {
            {{100, 100, 1200, 800}, true, "normal"},
            {{0, 0, 1920, 1080}, true, "the whole viewport"},
            {{100, 100, 799, 800}, false, "799 wide"},
            {{100, 100, 1200, 599}, false, "599 high"},
            {{100, 100, 800, 600}, true, "exactly 800x600"},
            {{-1500, 100, 1200, 800}, false, "left of the screen"},
            {{1900, 100, 1200, 800}, false, "right of the screen"},
            {{100, -50, 1200, 800}, false, "title strip above the screen"},
            {{100, 1050, 1200, 800}, false, "below the screen"},
            {{100, 100, 3000, 800}, false, "wider than the viewport (saved on a bigger screen)"},
            {{100, 100, 1200, 2000}, false, "taller than the viewport"},
            {{-300, 100, 1200, 800}, true, "partly off the left edge but reachable"},
        };
        for (const C& c : cases) {
            WindowPrefs p;
            p.maximized = false;
            p.has_rect = true;
            p.rect = c.r;
            const WindowPlan pl = window_plan(p, dw, dh, false, WindowRect{});
            CHECK(pl.maximized == !c.ok, std::string("start plan: ") + c.why);
            CHECK(pl.maximized ? pl.rect == (WindowRect{0, 0, dw, dh}) : pl.rect == c.r, std::string("start rect: ") + c.why);
        }
        WindowPrefs nan;
        nan.maximized = false;
        nan.has_rect = true;
        nan.rect = WindowRect{NAN, 0, 1200, 800};
        CHECK(window_plan(nan, dw, dh, false, WindowRect{}).maximized, "NaN rectangle: maximized");
        // a viewport smaller than 800x600 cannot hold any stored rectangle
        WindowPrefs p;
        p.maximized = false;
        p.has_rect = true;
        p.rect = WindowRect{0, 0, 800, 600};
        CHECK(window_plan(p, 640.0f, 480.0f, false, WindowRect{}).maximized, "a small viewport: maximized");
    });

    run_case("window: Restore uses the stored rectangle (480x320 and up) or the first-use size", [&] {
        const WindowRect dflt{60, 60, 1180, 760};
        WindowPrefs p;
        CHECK(window_plan(p, 1920, 1080, true, dflt).rect == dflt && !window_plan(p, 1920, 1080, true, dflt).maximized, "nothing stored: first-use size, never maximized");
        p.has_rect = true;
        p.rect = WindowRect{300, 200, 700, 500};  // smaller than 800x600: not restored at start, but Restore allows it
        CHECK(window_plan(p, 1920, 1080, true, dflt).rect == p.rect, "700x500 is restored");
        p.rect = WindowRect{300, 200, 300, 200};
        CHECK(window_plan(p, 1920, 1080, true, dflt).rect == dflt, "too small even for the panel: first-use size");
        p.rect = WindowRect{5000, 200, 1000, 700};
        CHECK(window_plan(p, 1920, 1080, true, dflt).rect == dflt, "off-screen: first-use size");
        CHECK(window_is_full(WindowRect{0, 0, 1920, 1080}, 1920, 1080) && window_is_full(WindowRect{0.5f, 0, 1919, 1080}, 1920, 1080) &&
                  !window_is_full(WindowRect{60, 60, 1180, 760}, 1920, 1080),
              "window_is_full");
    });

    run_case("window: the tracker applies on start and viewport change, and saves once after a rest (debounce)", [&] {
        WindowTracker t;
        CHECK(t.need_apply(true, 1920, 1080) && t.need_apply(false, 1920, 1080), "first frame: apply");
        t.applied(1920, 1080);
        CHECK(!t.need_apply(true, 1920, 1080) && !t.need_apply(false, 1920, 1080), "settled");
        CHECK(t.need_apply(true, 1600, 900) && !t.need_apply(false, 1600, 900), "maximized follows a viewport change; a chosen rectangle does not");
        const WindowRect a{100, 100, 1200, 800}, b{100, 100, 1100, 800};
        double now = 10.0;
        CHECK(!t.observe(a, now, false), "first sight = baseline, no save");
        for (int i = 0; i < 120; ++i) { now += 1.0 / 60.0; CHECK(!t.observe(a, now, false), "unchanged: never saved"); }
        // a drag: the rectangle changes every frame with the button down
        int saves = 0;
        WindowRect cur = a;
        for (int i = 0; i < 90; ++i) {
            now += 1.0 / 60.0;
            cur.w -= 1.0f;
            if (t.observe(cur, now, true)) ++saves;
        }
        CHECK(saves == 0, "no save while the mouse is down");
        for (int i = 0; i < 20; ++i) { now += 1.0 / 60.0; if (t.observe(cur, now, false)) ++saves; }
        CHECK(saves == 0, "no save before the rectangle rested for the debounce time");
        for (int i = 0; i < 60; ++i) { now += 1.0 / 60.0; if (t.observe(cur, now, false)) ++saves; }
        CHECK(saves == 1, fmt("exactly one save after the rest: %d", saves));
        for (int i = 0; i < 120; ++i) { now += 1.0 / 60.0; if (t.observe(cur, now, false)) ++saves; }
        CHECK(saves == 1, "and no more while it stays");
        CHECK(!t.observe(b, now, false) && t.pending_save, "a new change waits again");
        // mouse still down after the rest: still waits
        for (int i = 0; i < 90; ++i) { now += 1.0 / 60.0; if (t.observe(b, now, true)) ++saves; }
        CHECK(saves == 1, "held button: no save even long after");
        for (int i = 0; i < 2; ++i) { now += 1.0 / 60.0; if (t.observe(b, now, false)) ++saves; }
        CHECK(saves == 2, "released after a long hold: saved at once");
        t.applied(1920, 1080);
        CHECK(!t.observe(WindowRect{0, 0, 5, 5}, now, false), "after an apply the next sight is a baseline again");
    });
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A real App (the UI block): the panel of App::draw
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    const fs::path file = le / "turbo_output" / "gui_settings.json";
    const std::string keep_file = read_file(file);  // restored at the end (later cases read it)
    const GuiPrefs keep_prefs = app.prefs;
    auto panel = [&]() { return ImGui::FindWindowByName("FC 27 LE Turbo"); };
    auto stored = [&]() {
        json j = json::parse(read_file(file), nullptr, false);
        return j.is_object() && j.contains("gui") && j["gui"].contains("window") ? j["gui"]["window"] : json();
    };

    run_case("window UI: no stored block = the panel covers the whole viewport and offers Restore", [&] {
        std::error_code ec;
        fs::remove(file, ec);
        app.load_gui_settings();
        CHECK(app.prefs.window.maximized && !app.prefs.window.has_rect, "loaded: maximized, no rectangle");
        ui.frames(4);
        ImGuiWindow* w = panel();
        CHECK(w != nullptr, "the panel exists");
        if (!w) return;
        CHECK(near(w->Pos.x, 0) && near(w->Pos.y, 0) && near(w->Size.x, 1920) && near(w->Size.y, 1080), fmt("whole viewport: %.0f,%.0f %.0fx%.0f", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y));
        CHECK((w->Flags & ImGuiWindowFlags_NoMove) && (w->Flags & ImGuiWindowFlags_NoResize), "maximized: no drag, no edge resize");
        CHECK(ui.find("Restore###winmax", "FC 27 LE Turbo") != nullptr || ui.find("###winmax", "FC 27 LE Turbo") != nullptr, "the status line has the Restore button");
        ui.frames(120);
        CHECK(!fs::exists(file), "nothing written while it just sits there");
    });

    run_case("window UI: Restore, resize, rest = one write; Maximize keeps the rectangle", [&] {
        CHECK(ui.click("###winmax", "FC 27 LE Turbo"), "click Restore");
        ui.frames(4);
        ImGuiWindow* w = panel();
        if (!w) { CHECK(false, "panel"); return; }
        CHECK(!app.prefs.window.maximized && w->Size.x < 1900.0f && w->Size.x >= 800.0f, fmt("restored to a window: %.0fx%.0f", w->Size.x, w->Size.y));
        CHECK(!(w->Flags & ImGuiWindowFlags_NoMove) && !(w->Flags & ImGuiWindowFlags_NoResize), "restored: free to move and resize");
        json s = stored();
        CHECK(s.is_object() && s.value("maximized", true) == false && s.contains("w") && s.contains("h"), "Restore wrote the block: " + s.dump());
        // the person resizes and moves it (code stands in for the mouse), then lets go
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1000.0f, 700.0f));
        ImGui::SetWindowPos("FC 27 LE Turbo", ImVec2(200.0f, 120.0f));
        std::error_code ec;
        fs::remove(file, ec);
        ui.frames(10);
        CHECK(!fs::exists(file), "no write while the change is fresh (debounce)");
        ui.frames(70);
        json r = stored();
        CHECK(r.is_object() && r.value("x", 0) == 200 && r.value("y", 0) == 120 && r.value("w", 0) == 1000 && r.value("h", 0) == 700 && r.value("maximized", true) == false,
              "after the rest the rectangle is stored: " + r.dump());
        fs::remove(file, ec);
        ui.frames(200);
        CHECK(!fs::exists(file), "and it is not written again every frame");
        // Maximize
        CHECK(ui.click("###winmax", "FC 27 LE Turbo"), "click Maximize");
        ui.frames(4);
        w = panel();
        CHECK(app.prefs.window.maximized && w && near(w->Size.x, 1920) && near(w->Size.y, 1080) && near(w->Pos.x, 0), "maximized again");
        json m = stored();
        CHECK(m.value("maximized", false) == true && m.value("w", 0) == 1000 && m.value("h", 0) == 700, "flag true, restore rectangle kept: " + m.dump());
    });

    run_case("window UI: a restart restores the stored state; the setting beats the layout file", [&] {
        // restart = read gui_settings.json again; the panel is placed again on the next frame
        app.load_gui_settings();
        CHECK(app.prefs.window.maximized, "stored maximized: starts maximized");
        ui.frames(4);
        ImGuiWindow* w = panel();
        CHECK(w && near(w->Size.x, 1920) && near(w->Size.y, 1080), "maximized after a restart");
        // restored rectangle
        json j = json::parse(read_file(file));
        j["gui"]["window"] = {{"x", 150}, {"y", 90}, {"w", 1100}, {"h", 720}, {"maximized", false}};
        std::ofstream(file.string(), std::ios::binary) << j.dump(2);
        app.load_gui_settings();
        ui.frames(4);
        w = panel();
        CHECK(w && near(w->Pos.x, 150) && near(w->Pos.y, 90) && near(w->Size.x, 1100) && near(w->Size.y, 720),
              w ? fmt("the stored rectangle: %.0f,%.0f %.0fx%.0f", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y) : "panel");
        // the setting wins over the layout file: whatever the ini / earlier session left the panel at, the stored rectangle is put back
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(900.0f, 650.0f));
        ImGui::SetWindowPos("FC 27 LE Turbo", ImVec2(400.0f, 300.0f));
        ui.frames(3);
        app.load_gui_settings();
        ui.frames(4);
        w = panel();
        CHECK(w && near(w->Pos.x, 150) && near(w->Pos.y, 90) && near(w->Size.x, 1100) && near(w->Size.y, 720), "the stored rectangle wins over the panel's own state");
        CHECK(w && !(w->Flags & ImGuiWindowFlags_NoSavedSettings), "the panel keeps its table layouts (a NoSavedSettings panel would drop them)");
        // off-screen / too small -> maximized
        for (const json& bad : {json{{"x", 5000}, {"y", 90}, {"w", 1100}, {"h", 720}, {"maximized", false}},
                                json{{"x", 150}, {"y", 90}, {"w", 640}, {"h", 480}, {"maximized", false}}}) {
            j["gui"]["window"] = bad;
            std::ofstream(file.string(), std::ios::binary) << j.dump(2);
            app.load_gui_settings();
            ui.frames(4);
            w = panel();
            CHECK(w && near(w->Size.x, 1920) && near(w->Size.y, 1080) && app.prefs.window.maximized, "unusable rectangle: maximized: " + bad.dump());
        }
    });

    run_case("window UI: a toast stays above the maximized panel", [&] {
        app.load_gui_settings();
        ui.frames(4);
        CHECK(app.prefs.window.maximized, "maximized");
        app.notify("window test toast");
        ui.frames(3);
        const ItemRec* any = ui.find("###winmax", "FC 27 LE Turbo");
        CHECK(any != nullptr && ui.click("###winmax", "FC 27 LE Turbo") && ui.click("###winmax", "FC 27 LE Turbo"), "click the panel's own button twice (brings the panel to the front)");
        ui.frames(3);
        int ip = -1, it = -1;
        ImGuiContext& g = *GImGui;
        for (int i = 0; i < g.Windows.Size; ++i) {
            if (std::string(g.Windows[i]->Name) == "FC 27 LE Turbo") ip = i;
            if (std::string(g.Windows[i]->Name) == "##turbo_toasts") it = i;
        }
        CHECK(ip >= 0 && it > ip, fmt("toasts (window %d) are above the panel (window %d)", it, ip));
        app.toasts.clear();
        app.load_gui_settings();
        ui.frames(4);
    });

    // leave the world as the later cases expect it
    if (keep_file.empty()) {
        std::error_code ec;
        fs::remove(file, ec);
    } else {
        std::ofstream o(file.string(), std::ios::binary);
        o << keep_file;
    }
    app.load_gui_settings();
    app.prefs = keep_prefs;
    app.prefs.window_rt = WindowTracker();
    ui.frames(3);
}

}  // namespace window_settings_test
