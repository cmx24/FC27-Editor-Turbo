// Native tests (5.0 T7 window rect; Cassio 10-26 14:33: "window resizing not being saved"). Brief turbo_dev/PITWALL/briefs/5.0-T7-window-rect.md,
// verdict turbo_dev/PITWALL/reviews/5.0-window-rect-cause.md, evidence turbo_dev/evidence/2026-10-26/GUI50/T7/RESULTS.md.
// Replays the "gui" block of Cassio's gui_settings.json, verbatim, on his 2560x1600 game screen. The read-only copy is
// turbo_dev/evidence/2026-10-26/GUI50/T7/gui_settings.cassio.json, md5 c992b71423882f1707a43e41e0fb842e: gui.window {17, 5, 2560, 1600,
// maximized true}, ui_scale 1.2 (UI scale 1.8 on that screen). The other top-level blocks of his file (edit unlock, competitions, callnames,
// ...) are not window settings and drive other features of the App, so they are left out.
//   W1  the maximized panel's bottom-right corner dragged to 1480x900: it resizes, rests, is saved, and a reload places 1480x900.
//       FAILS on 93adb6d0 (the source of the live 0041F6BE and of E70BAAF4): a maximized panel was NoMove | NoResize.
//   W2  Restore on his file: the panel fits the screen, its bottom-right corner (the resize grip) on it. FAILS on e03008ed (D3).
//   W3  a stored rectangle the start cannot use (640x480, under 800x600) + a later save of any setting: the file keeps maximized false and the
//       rectangle; a 1400x900 rectangle that does not fit a 1280x720 screen is placed again when 2560x1600 comes back. FAILS on e03008ed (D1).
//   W4  the window log lines (placed / saved / shown) carry the rectangle, the game screen and the usable verdict with its reason, reach both
//       the in-app Log and the turbo_gui.log callback, and none is written per frame. FAILS on e03008ed (no window log line existed).
//   run(): Restore never returns to a rectangle that covers the screen and moves one that sticks out back on (D3, pure). FAILS on e03008ed.
// Uses only the App / harness API that 93adb6d0 and e03008ed already had, so the same file runs on both for the fail-old runs.
// Included by test_main.cpp after test_window_settings.h; ui_cases runs after resize_ui_test::ui_cases, run() after window_settings_test::run().
#pragma once
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ui/gui_prefs.h"

namespace gui50_window_rect_test {

using nlohmann::json;
using namespace turbo;

// gui_settings.cassio.json "gui", verbatim
static const char* const kCassioGui = R"({
    "advanced_mode": false,
    "preload_images": true,
    "rail_collapsed": false,
    "row_density": "normal",
    "sections_open": {
      "diag.bodythumbs": true,
      "diag.details": true,
      "diag.hooks": true,
      "diagnostics.log": false,
      "settings.link": true,
      "settings.log": false,
      "settings.pictures": true,
      "settings.preferences": true
    },
    "toggle_key": 107,
    "toggle_mods": 0,
    "ui_scale": 1.2000000476837158,
    "version": "3.0.0",
    "window": {
      "h": 1600.0,
      "maximized": true,
      "w": 2560.0,
      "x": 17.0,
      "y": 5.0
    }
})";

static bool near(float a, float b, float tol = 4.0f) { return std::fabs(a - b) <= tol; }

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static ImGuiWindow* panel() { return ImGui::FindWindowByName("FC 27 LE Turbo"); }

// press at `from`, move to `to` in steps (one frame each), release; the mouse is parked mid-screen at the end
static void drag(Ui& ui, ImVec2 from, ImVec2 to, int steps = 14) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(from.x, from.y);
    ui.frames(3);
    io.AddMouseButtonEvent(0, true);
    ui.frames(3);
    for (int i = 1; i <= steps; ++i) {
        io.AddMousePosEvent(from.x + (to.x - from.x) * float(i) / float(steps), from.y + (to.y - from.y) * float(i) / float(steps));
        ui.frames(1);
    }
    ui.frames(2);
    io.AddMouseButtonEvent(0, false);
    ui.frames(3);
    io.AddMousePosEvent(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ui.frames(2);
}

static float num(const json& o, const char* k) {
    return o.is_object() && o.contains(k) && o[k].is_number() ? o[k].get<float>() : -1.0f;
}
static bool flag(const json& o, const char* k, bool dflt) { return o.is_object() && o.contains(k) && o[k].is_boolean() ? o[k].get<bool>() : dflt; }

// ---- pure (window_plan, Restore)
static void run() {
    run_case("gui50 window rect: Restore never returns to a rectangle that covers the screen, and moves one that sticks out back on (D3)", [&] {
        const WindowRect dflt{102, 80, 2124, 1368};
        WindowPrefs p;
        p.maximized = true;
        p.has_rect = true;
        p.rect = WindowRect{17, 5, 2560, 1600};  // Cassio's stored rectangle
        WindowPlan pl = window_plan(p, 2560, 1600, true, dflt);
        CHECK(!pl.maximized && pl.rect == dflt, fmt("his {17,5,2560,1600}: Restore = the first-use size, got {%.0f,%.0f,%.0f,%.0f}", pl.rect.x, pl.rect.y, pl.rect.w, pl.rect.h));
        p.rect = WindowRect{0, 0, 2560, 1600};
        pl = window_plan(p, 2560, 1600, true, dflt);
        CHECK(pl.rect == dflt, "the whole screen exactly: the first-use size");
        p.rect = WindowRect{2000, 1200, 1000, 700};  // usable for Restore (480x320 and up, title strip reachable) but past the right and bottom edges
        pl = window_plan(p, 2560, 1600, true, dflt);
        CHECK(pl.rect == (WindowRect{1560, 900, 1000, 700}), fmt("moved back on: {%.0f,%.0f,%.0f,%.0f}", pl.rect.x, pl.rect.y, pl.rect.w, pl.rect.h));
        p.rect = WindowRect{300, 200, 700, 500};
        CHECK(window_plan(p, 1920, 1080, true, dflt).rect == p.rect, "a rectangle on the screen is restored unchanged");
        // the start plan is unchanged: his rectangle passes the start check, so maximized true starts maximized, false starts at 17,5
        p.rect = WindowRect{17, 5, 2560, 1600};
        CHECK(window_rect_usable(p.rect, 2560, 1600), "his rectangle passes the start check on 2560x1600 (hypothesis a disconfirmed)");
        CHECK(window_plan(p, 2560, 1600, false, dflt).maximized, "maximized true: starts maximized");
    });
}

// ---- the real panel (App::draw)
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    const fs::path file = le / "turbo_output" / "gui_settings.json";
    const std::string keep_file = read_file(file);  // restored at the end (later cases read it)
    const GuiPrefs keep_prefs = app.prefs;
    const float keep_scale = app.ui_scale_user;
    const int keep_vk = app.toggle_vk, keep_mods = app.toggle_mods;
    const bool keep_legacy = app.legacy_pages, keep_card = app.player_card_view, keep_visible = app.visible;
    const ImVec2 keep_ds = ImGui::GetIO().DisplaySize;
    const std::function<void(const std::string&)> keep_hook = app.log_hook;
    std::vector<std::string> hooked;  // what reached the turbo_gui.log callback
    app.log_hook = [&hooked, keep_hook](const std::string& s) {
        hooked.push_back(s);
        if (keep_hook) keep_hook(s);
    };
    app.visible = true;

    // his "gui" block (window_block null) or that block with another gui.window, on a game screen of `screen`; then the start frames
    auto load_his = [&](const json& window_block, ImVec2 screen) {
        json g = json::parse(kCassioGui);
        if (!window_block.is_null()) g["window"] = window_block;
        json doc = json::object();
        doc["gui"] = g;
        ImGui::GetIO().DisplaySize = screen;
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        {
            std::ofstream o(file.string(), std::ios::binary);
            o << doc.dump(2);
        }
        app.load_gui_settings();
        ui.frames(4);
    };
    auto stored = [&]() {
        json j = json::parse(read_file(file), nullptr, false);
        return j.is_object() && j.contains("gui") && j["gui"].is_object() && j["gui"].contains("window") ? j["gui"]["window"] : json();
    };
    auto last_line = [&](const std::string& prefix) {
        std::string r;
        for (const std::string& l : hooked)
            if (l.compare(0, prefix.size(), prefix) == 0) r = l;
        return r;
    };
    auto rect_of = [](ImGuiWindow* w) {
        return w ? fmt("%.0f,%.0f %.0fx%.0f", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y) : std::string("no panel");
    };
    const json small_rect = json{{"x", 150}, {"y", 90}, {"w", 640}, {"h", 480}, {"maximized", false}};
    const json big_rect = json{{"x", 150}, {"y", 90}, {"w", 1400}, {"h", 900}, {"maximized", false}};

    run_case("gui50 window rect W1: his maximized panel, the corner dragged to 1480x900, rests, is saved; a reload places 1480x900", [&] {
        load_his(json(), ImVec2(2560.0f, 1600.0f));
        ImGuiWindow* w = panel();
        CHECK(w && app.prefs.window.maximized && near(w->Pos.x, 0) && near(w->Pos.y, 0) && near(w->Size.x, 2560) && near(w->Size.y, 1600),
              "starts maximized on 2560x1600: " + rect_of(w));
        if (!w) return;
        drag(ui, ImVec2(2557.0f, 1597.0f), ImVec2(1477.0f, 897.0f));
        w = panel();
        CHECK(w && near(w->Size.x, 1480) && near(w->Size.y, 900), "the corner drag resized the maximized panel: " + rect_of(w) + " (93adb6d0: NoResize while maximized)");
        CHECK(!app.prefs.window.maximized, "the drag left maximized");
        ui.frames(70);  // rests longer than WindowTracker::kDebounce
        const json s = stored();
        CHECK(!flag(s, "maximized", true) && near(num(s, "w"), 1480) && near(num(s, "h"), 900), "saved once it rested: " + (s.is_null() ? std::string("no window block") : s.dump()));
        app.load_gui_settings();  // the next start
        ui.frames(4);
        w = panel();
        CHECK(w && !app.prefs.window.maximized && near(w->Size.x, 1480, 2.0f) && near(w->Size.y, 900, 2.0f), "reloaded and applied: " + rect_of(w));
    });

    run_case("gui50 window rect W2: Restore on his file (17,5 2560x1600) gives a panel that fits the screen, its bottom-right corner on it", [&] {
        load_his(json(), ImVec2(2560.0f, 1600.0f));
        const ItemRec* b = ui.find("###winmax", "FC 27 LE Turbo");
        CHECK(b != nullptr, "the Restore button is drawn");
        if (b)
            std::printf("gui50 window rect G3: the Restore button (###winmax, app.cpp title strip) on 2560x1600 at UI size 1.2: centre %.0f,%.0f, rect %.0f,%.0f-%.0f,%.0f\n",
                        b->rect.GetCenter().x, b->rect.GetCenter().y, b->rect.Min.x, b->rect.Min.y, b->rect.Max.x, b->rect.Max.y);
        CHECK(ui.click("###winmax", "FC 27 LE Turbo"), "click Restore");
        ui.frames(4);
        ImGuiWindow* w = panel();
        if (!w) {
            CHECK(false, "the panel exists");
            return;
        }
        CHECK(!app.prefs.window.maximized, "restored: not maximized");
        CHECK(w->Pos.x + w->Size.x <= 2561.0f && w->Pos.y + w->Size.y <= 1601.0f, "the bottom-right corner is on the screen: " + rect_of(w));
        CHECK(w->Size.x < 2550.0f || w->Size.y < 1590.0f, "Restore visibly changed the panel: " + rect_of(w));
        const float sc = g_ui_scale;
        const float ew = std::min(1180.0f * sc, 2560.0f * 0.9f), eh = std::min(760.0f * sc, 1600.0f * 0.88f);
        CHECK(near(w->Size.x, ew) && near(w->Size.y, eh), fmt("the first-use size %.0fx%.0f at UI scale %.2f: ", ew, eh, sc) + rect_of(w));
        std::printf("gui50 window rect G3: after Restore the panel is %s (UI scale %.2f)\n", rect_of(w).c_str(), sc);
        ui.frames(4);
        const json s = stored();
        CHECK(!flag(s, "maximized", true) && near(num(s, "w"), w->Size.x) && near(num(s, "h"), w->Size.y), "Restore is saved: " + (s.is_null() ? std::string("no window block") : s.dump()));
    });

    run_case("gui50 window rect W3: a stored rectangle the start cannot use keeps maximized false through a save; a screen that comes back places it again", [&] {
        load_his(small_rect, ImVec2(2560.0f, 1600.0f));
        ImGuiWindow* w = panel();
        CHECK(w && app.prefs.window.maximized && near(w->Size.x, 2560) && near(w->Size.y, 1600), "640x480 is under 800x600: the start falls back to the whole screen: " + rect_of(w));
        CHECK(app.save_gui_settings(), "another setting saves the file");
        json s = stored();
        CHECK(!flag(s, "maximized", true) && near(num(s, "w"), 640, 0.5f) && near(num(s, "h"), 480, 0.5f) && near(num(s, "x"), 150, 0.5f),
              "the stored choice is not overwritten: " + (s.is_null() ? std::string("no window block") : s.dump()));
        // a smaller game resolution, then the old one back
        load_his(big_rect, ImVec2(1280.0f, 720.0f));
        w = panel();
        CHECK(w && near(w->Size.x, 1280) && near(w->Size.y, 720), "on 1280x720 the 1400x900 rectangle does not fit: the whole screen: " + rect_of(w));
        CHECK(app.save_gui_settings(), "save on the small screen");
        s = stored();
        CHECK(!flag(s, "maximized", true) && near(num(s, "w"), 1400, 0.5f), "kept on the small screen: " + (s.is_null() ? std::string("no window block") : s.dump()));
        ImGui::GetIO().DisplaySize = ImVec2(2560.0f, 1600.0f);
        ui.frames(4);
        w = panel();
        CHECK(w && !app.prefs.window.maximized && near(w->Size.x, 1400, 2.0f) && near(w->Size.y, 900, 2.0f) && near(w->Pos.x, 150, 2.0f) && near(w->Pos.y, 90, 2.0f),
              "2560x1600 came back: the stored 1400x900 at 150,90 is placed: " + rect_of(w));
    });

    run_case("gui50 window rect W4: the window log lines (placed, saved, shown) carry the rectangle, the game screen and the verdict; none per frame", [&] {
        hooked.clear();
        load_his(json(), ImVec2(2560.0f, 1600.0f));
        const std::string placed = last_line("window placed:");
        CHECK(placed.find("2560x1600 at 0,0, maximized;") != std::string::npos && placed.find("game screen 2560x1600") != std::string::npos &&
                  placed.find("stored 2560x1600 at 17,5, maximized yes") != std::string::npos && placed.find("stored rectangle usable") != std::string::npos,
              "placed: '" + placed + "'");
        bool in_log = false;
        for (const std::string& l : app.log_lines) in_log = in_log || (!placed.empty() && l == placed);
        CHECK(in_log, "the same line is in the in-app Log");
        auto window_lines = [&]() {
            int c = 0;
            for (const std::string& l : hooked) c += l.compare(0, 7, "window ") == 0 ? 1 : 0;
            return c;
        };
        const int n = window_lines();
        ui.frames(60);
        CHECK(window_lines() == n, fmt("no window line per frame: %d -> %d", n, window_lines()));
        app.visible = false;  // the overlay hidden (the toggle key), then shown again
        ui.frames(3);
        app.visible = true;
        ui.frames(3);
        const std::string shown = last_line("window shown:");
        CHECK(shown.find("2560x1600 at 0,0, maximized;") != std::string::npos, "shown again: '" + shown + "'");
        // a stored rectangle the start rejects names its reason
        hooked.clear();
        load_his(small_rect, ImVec2(2560.0f, 1600.0f));
        const std::string rej = last_line("window placed:");
        CHECK(rej.find("stored 640x480 at 150,90, maximized no") != std::string::npos && rej.find("not usable: smaller than the minimum") != std::string::npos,
              "rejected: '" + rej + "'");
        // a resize that rests
        hooked.clear();
        load_his(json(), ImVec2(2560.0f, 1600.0f));
        drag(ui, ImVec2(2557.0f, 1597.0f), ImVec2(1477.0f, 897.0f));
        ui.frames(70);
        const std::string saved = last_line("window saved:");
        CHECK(saved.find("1480x900 at 0,0, not maximized") != std::string::npos && saved.find("stored 1480x900 at 0,0, maximized no") != std::string::npos,
              "saved: '" + saved + "'");
        int saves = 0;
        for (const std::string& l : hooked) saves += l.compare(0, 13, "window saved:") == 0 ? 1 : 0;
        CHECK(saves == 1, fmt("one save line for one resize: %d", saves));
    });

    // leave the world as the later cases expect it
    app.log_hook = keep_hook;
    ImGui::GetIO().DisplaySize = keep_ds;
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
    app.ui_scale_user = keep_scale;
    app.toggle_vk = keep_vk;
    app.toggle_mods = keep_mods;
    app.legacy_pages = keep_legacy;
    app.player_card_view = keep_card;
    app.visible = keep_visible;
    ui.frames(3);
}

}  // namespace gui50_window_rect_test
