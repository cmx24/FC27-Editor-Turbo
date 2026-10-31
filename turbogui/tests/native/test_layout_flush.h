// Native tests (5.0c E6-layout; Cassio 10-30 rejecting TEST build C07F2C87: "the window size and the column widths are not saved").
// Brief turbo_dev/PITWALL/briefs/5.0c/E6-layout.md. Every case name contains "e6-layout" (TURBO_TEST_ONLY=e6-layout runs exactly these).
// What is pinned (each FAILS on d197d851, the 5.0b/integration base, and passes with the E6 change):
//   A  a pane divider is written to gui.splits the frame after the mouse is released (the base waited 0.4 s of rest, and a pane that is
//      not drawn - the panel hidden - cannot commit anything), so hiding right after a drag keeps the width
//   B  with NO gui.splits entry (the live gui_settings.json starts with none) the width the layout file holds for the pane wins; the base
//      forced the default (ImGuiCond_Always) on the first frame and stamped it over the saved width
//   C  the layout store saves when a drag ends (mouse released), not only on ImGui's 1 s timer, so closing the game right after loses nothing
//   D  a window change still inside its debounce is handed over when the panel is hidden (WindowTracker::take_pending)
//   E  the same on the real App: the main panel resized, then hidden inside the debounce, is written to gui_settings.json gui.window
//   F  Settings > Layout tells what the window is now and what is remembered (source scan: the page registry is cleared by later cases)
//   G  (guard, passes on both) a UI size change still re-applies the stored width
//   H  (guard, passes on both) a page with no stored width starts at its own default, not at the previous page's width in the same pane
//   L1 (fix round) the real panel's corner dragged and released, then only 0.03 s of frames: gui_settings.json already holds the new size
//      (the base waited 0.6 s, so a game killed right after a resize lost it)
//   M1 (fix round) a dialog resized by the person reopens at that size (modal_size.h; the base reopened every picker at its fixed default)
//   N1 (fix round) the visual picker and the body type gallery use it, and App::draw commits on the release edge (source scan)
// C and D call APIs the base does not have through a SFINAE shim, so this file compiles on both trees and the fail-old run FAILS on
// the assertion, not on the compiler.
// Included by test_main.cpp after test_layout_store.h (uses its World / MemSink / SplitGuard); ui_cases runs after gui50_window_rect_test::ui_cases.
#pragma once
#include <cmath>
#include <string>
#include <type_traits>
#include <utility>

#include "test_layout_store.h"

#if __has_include("ui/modal_size.h")
#include "ui/modal_size.h"
#define TURBO_HAVE_MODAL_SIZE 1
#endif

namespace layout_flush_test {

using namespace turbo;
using layout_store_test::near_f;

#ifdef TURBO_HAVE_LAYOUT_STORE

// LayoutStore::frame with the mouse-released flag where the tree has it; the 2-argument call where it does not (the base)
template <class S>
auto store_frame(S& s, bool visible, double now, bool released, int) -> decltype(s.frame(visible, now, released), void()) {
    s.frame(visible, now, released);
}
template <class S>
void store_frame(S& s, bool visible, double now, bool, long) {
    s.frame(visible, now);
}
// WindowTracker::take_pending where the tree has it; false where it does not
template <class T>
auto take_pending(T& t, int) -> decltype(t.take_pending()) {
    return t.take_pending();
}
template <class T>
bool take_pending(T&, long) {
    return false;
}

// one World frame with the mouse-released flag given by the caller (World::frame passes none)
static void world_frame_released(layout_store_test::World& w, bool released) {
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ++g_frame;
    store_frame(w.store, true, w.now, released, 0);
    w.demo.draw_table(w.frame_no);
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    ++w.frame_no;
    w.now += 1.0 / 60.0;
}

#endif  // TURBO_HAVE_LAYOUT_STORE

static void run() {
#ifdef TURBO_HAVE_LAYOUT_STORE
    using layout_store_test::Demo;
    using layout_store_test::find_pane;
    using layout_store_test::MemSink;
    using layout_store_test::SplitGuard;
    using layout_store_test::World;
    using layout_store_test::world_drag;

    run_case("e6-layout A: a dragged pane divider is in gui.splits the frame after the mouse is released, and a hide right after keeps it", [&] {
        SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        split::bind(&st);
        split::set_page("LooksStudio");
        split::reset_runtime_for_tests();
        World w;
        w.demo.mode = Demo::Panes;
        w.frames(10);
        ImGuiWindow* p = find_pane("##lslist");
        CHECK(p != nullptr, "the ##lslist pane is drawn");
        if (!p) return;
        const float w0 = p->Size.x, x = p->Pos.x + p->Size.x + 1.5f, y = p->Pos.y + p->Size.y * 0.5f;
        world_drag(w, ImVec2(x, y), ImVec2(x + 100.0f, y));  // ends 5 frames (0.08 s) after the release
        p = find_pane("##lslist");
        CHECK(p != nullptr && near_f(p->Size.x, w0 + 100.0f), fmt("dragged 100 px: %.1f -> %.1f", w0, p ? p->Size.x : -1.0f));
        const std::string key = "LooksStudio/##lslist";
        CHECK(st.has(key) && near_f(st.get(key, 0.0f), w0 + 100.0f),
              fmt("0.08 s after the release the width is stored (the base waited 0.4 s): has=%d value=%.1f", st.has(key) ? 1 : 0, st.get(key, -1.0f)));
        w.frames(60, false);  // the panel hidden: nothing is drawn, nothing could commit it now
        CHECK(st.has(key) && near_f(st.get(key, 0.0f), w0 + 100.0f), "still stored after the panel was hidden");
    });

    run_case("e6-layout B: with no gui.splits entry the width the layout file holds for the pane wins over the default", [&] {
        SplitGuard guard;
        g_ui_scale = 1.0f;
        std::string ini;
        float dragged = 0.0f;
        {
            SplitStore st;
            split::bind(&st);
            split::set_page("LooksStudio");
            split::reset_runtime_for_tests();
            World w;
            w.demo.mode = Demo::Panes;
            w.frames(10);
            ImGuiWindow* p = find_pane("##lspreview");
            CHECK(p != nullptr, "the ##lspreview pane is drawn");
            if (!p) return;
            const float x = p->Pos.x + p->Size.x + 1.5f, y = p->Pos.y + p->Size.y * 0.5f;
            world_drag(w, ImVec2(x, y), ImVec2(x + 120.0f, y));
            w.frames(40);
            p = find_pane("##lspreview");
            dragged = p ? p->Size.x : 0.0f;
            CHECK(p != nullptr && near_f(dragged, 260.0f + 120.0f), fmt("dragged 120 px from the 260 default: %.1f", dragged));
            size_t n = 0;
            const char* d = ImGui::SaveIniSettingsToMemory(&n);
            ini = d ? std::string(d, n) : std::string();
            CHECK(ini.find("[Window][") != std::string::npos, "the layout file holds the window blocks");
        }
        // the next start: a gui_settings.json that has no splits (the live file starts with {}), the layout file loaded before the first frame
        SplitStore st2;
        split::bind(&st2);
        split::reset_runtime_for_tests();
        World w2;
        ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
        w2.demo.mode = Demo::Panes;
        w2.frames(10);
        ImGuiWindow* q = find_pane("##lspreview");
        CHECK(q != nullptr && near_f(q->Size.x, dragged),
              fmt("restart without a stored split: the pane is %.1f again (it was %.1f; the base stamped the 260 default)", q ? q->Size.x : -1.0f, dragged));
    });

    run_case("e6-layout C: the layout store saves when a drag ends (mouse released), before ImGui's own timer", [&] {
        MemSink m;
        World w(30.0f);  // ImGui's timer is 30 s: only the release can have written
        w.store.set_sink(m.sink());
        w.store.load();
        w.frames(30);
        CHECK(!m.has_text, "the timer has not fired yet");
        world_frame_released(w, true);  // the frame the mouse button goes up
        CHECK(m.has_text && layout_store_test::has(m.stored, "Width=123"), "the release wrote the column width (the base wrote nothing until the timer or a hide)");
        const int n = m.writes;
        world_frame_released(w, true);
        world_frame_released(w, true);
        CHECK(m.writes == n, "an unchanged layout is not written again at a later release");
    });

    run_case("e6-layout D: a window change still inside its debounce is handed over when the panel is hidden", [&] {
        WindowTracker t;
        t.applied(1920, 1080);
        const WindowRect a{100, 100, 1200, 800}, b{100, 100, 1100, 800};
        double now = 10.0;
        CHECK(!t.observe(a, now, false), "baseline");
        CHECK(!take_pending(t, 0), "nothing changed: nothing to hand over");
        now += 1.0 / 60.0;
        CHECK(!t.observe(b, now, false) && t.pending_save, "a change waits out the debounce");
        now += 1.0 / 60.0;
        CHECK(take_pending(t, 0), "hidden inside the debounce: the change is handed over (the base has no such call)");
        CHECK(!take_pending(t, 0), "handed over once");
        for (int i = 0; i < 120; ++i) {
            now += 1.0 / 60.0;
            CHECK(!t.observe(b, now, false), "and the debounce does not write it a second time");
        }
    });

    run_case("e6-layout G: a UI size change still re-applies the stored width", [&] {
        SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        st.set("LooksStudio/##lslist", 320.0f);
        split::bind(&st);
        split::set_page("LooksStudio");
        split::reset_runtime_for_tests();
        World w;
        w.demo.mode = Demo::Panes;
        w.frames(10);
        ImGuiWindow* p = find_pane("##lslist");
        CHECK(p != nullptr && near_f(p->Size.x, 320.0f), fmt("the stored 320 at UI size 1.0: %.1f", p ? p->Size.x : -1.0f));
        g_ui_scale = 1.25f;
        w.frames(6);
        p = find_pane("##lslist");
        CHECK(p != nullptr && near_f(p->Size.x, 320.0f * 1.25f, 2.0f), fmt("320 x 1.25 at UI size 1.25: %.1f", p ? p->Size.x : -1.0f));
    });

    run_case("e6-layout H: a page with no stored width starts at its own default, not at the width the previous page left in the same pane", [&] {
        SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        st.set("PageA/##lslist", 420.0f);
        split::bind(&st);
        split::set_page("PageA");
        split::reset_runtime_for_tests();
        World w;
        w.demo.mode = Demo::Panes;
        w.frames(10);
        ImGuiWindow* p = find_pane("##lslist");
        CHECK(p != nullptr && near_f(p->Size.x, 420.0f), fmt("page A at its stored 420: %.1f", p ? p->Size.x : -1.0f));
        split::set_page("PageB");
        w.frames(6);
        p = find_pane("##lslist");
        CHECK(p != nullptr && near_f(p->Size.x, 300.0f), fmt("page B (nothing stored) at the default 300: %.1f", p ? p->Size.x : -1.0f));
        split::set_page("PageA");
        w.frames(6);
        p = find_pane("##lslist");
        CHECK(p != nullptr && near_f(p->Size.x, 420.0f), fmt("back on page A: its 420 again: %.1f", p ? p->Size.x : -1.0f));
    });
    run_case("e6-layout M1: a dialog the person resized reopens at that size, not at its fixed default", [&] {
#ifdef TURBO_HAVE_MODAL_SIZE
        SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        split::bind(&st);
        World w;
        const ImVec2 dflt(500.0f, 400.0f);
        bool open_it = true, close_it = false;
        ImVec2 resize_to(0.0f, 0.0f), seen(0.0f, 0.0f);
        auto frame = [&] {
            ImGui_ImplNull_NewFrame();
            ImGui::NewFrame();
            ++g_frame;
            if (open_it) { ImGui::OpenPopup("###mz"); open_it = false; }
            modal_size::set_next("mz", dflt);
            bool keep = true;
            if (ImGui::BeginPopupModal("Test dialog###mz", &keep, ImGuiWindowFlags_NoSavedSettings)) {
                if (resize_to.x > 0.0f) { ImGui::SetWindowSize(resize_to); resize_to = ImVec2(0.0f, 0.0f); }
                modal_size::track("mz", dflt);
                seen = ImGui::GetWindowSize();
                if (close_it) { ImGui::CloseCurrentPopup(); close_it = false; }
                ImGui::EndPopup();
            }
            ImGui::Render();
            ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        };
        for (int i = 0; i < 6; ++i) frame();
        CHECK(near_f(seen.x, 500.0f) && near_f(seen.y, 400.0f), fmt("first open at the default 500x400: %.0fx%.0f", seen.x, seen.y));
        CHECK(!st.has("modal/mz/w"), "opening at the default stores nothing");
        resize_to = ImVec2(640.0f, 470.0f);  // the person drags the corner
        for (int i = 0; i < 6; ++i) frame();
        CHECK(near_f(seen.x, 640.0f) && near_f(seen.y, 470.0f), fmt("resized to 640x470: %.0fx%.0f", seen.x, seen.y));
        CHECK(st.has("modal/mz/w") && near_f(st.get("modal/mz/w", 0.0f), 640.0f) && near_f(st.get("modal/mz/h", 0.0f), 470.0f),
              "the size is stored (gui.splits modal/mz/w and /h)");
        close_it = true;
        for (int i = 0; i < 6; ++i) frame();
        open_it = true;
        seen = ImVec2(0.0f, 0.0f);
        for (int i = 0; i < 6; ++i) frame();
        CHECK(near_f(seen.x, 640.0f) && near_f(seen.y, 470.0f), fmt("reopened at 640x470 (the base reopened at the default): %.0fx%.0f", seen.x, seen.y));
        close_it = true;
        for (int i = 0; i < 6; ++i) frame();
        g_ui_scale = 1.25f;  // a UI size change (dialog closed) keeps the proportion
        open_it = true;
        for (int i = 0; i < 6; ++i) frame();
        CHECK(near_f(seen.x, 800.0f, 2.0f) && near_f(seen.y, 587.5f, 2.0f), fmt("at UI size 1.25 the stored 640x470 opens at 800x588: %.0fx%.0f", seen.x, seen.y));
#else
        CHECK(false, "ui/modal_size.h does not exist (the base): dialogs reopen at their fixed default size");
#endif
    });

#endif

    run_case("e6-layout F: Settings > Layout shows the window now and what is remembered (source scan)", [&] {
        const std::string src = layout_store_test::slurp(layout_store_test::turbogui_root() / "src" / "ui" / "ui_settings.cpp");
        CHECK(layout_store_test::has(src, "Remembered for the next start"), "ui_settings.cpp draw_layout has the 'Remembered for the next start' read-out");
        const std::string app = layout_store_test::slurp(layout_store_test::turbogui_root() / "src" / "ui" / "app.cpp");
        const size_t hide = app.find("if (!visible) {");
        CHECK(hide != std::string::npos && app.find("take_pending()", hide) != std::string::npos && app.find("splits.take_dirty()", hide) != std::string::npos,
              "app.cpp: the hidden branch of App::draw writes a pending window change and pane width before it returns");
    });

    run_case("e6-layout N1: the pickers remember their size and the panel commits on the release edge (source scan)", [&] {
        const fs::path root = layout_store_test::turbogui_root() / "src" / "ui";
        const std::string vp = layout_store_test::slurp(root / "ui_visual_picker.cpp");
        const std::string bt = layout_store_test::slurp(root / "ui_bodytypes.cpp");
        CHECK(layout_store_test::has(vp, "modal_size::set_next(\"vispicker\"") && layout_store_test::has(vp, "modal_size::track(\"vispicker\""),
              "ui_visual_picker.cpp sets and tracks the picker size");
        // 5.0c integration: E8-body deleted the Body types gallery modal (one picker: the visual picker above), so the size check moves there
        CHECK(!layout_store_test::has(bt, "BeginPopupModal(\"Body types##bt\""),
              "ui_bodytypes.cpp has no second body dialog (E8 RV1): the visual picker, whose size is remembered, is the one Body type window");
        const std::string app = layout_store_test::slurp(root / "app.cpp");
        CHECK(layout_store_test::has(app, "ImGui::IsMouseReleased(0) && wtrack.pending_save"), "app.cpp commits a waiting window change on the release edge");
    });
}

// ---- the real panel (App::draw)
static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    const fs::path file = le / "turbo_output" / "gui_settings.json";
    const std::string keep_file = read_file(file);
    const GuiPrefs keep_prefs = app.prefs;
    const bool keep_visible = app.visible;
    const bool keep_legacy = app.legacy_pages;
    const ImVec2 keep_override = g_display_override;
    app.visible = true;
    app.legacy_pages = false;
    g_display_override = ImVec2(1920.0f, 1080.0f);

    run_case("e6-layout E: the main panel resized and then hidden inside the debounce is written to gui.window", [&] {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        {
            std::ofstream o(file.string(), std::ios::binary | std::ios::trunc);
            o << "{\"gui\":{\"window\":{\"x\":150,\"y\":90,\"w\":900,\"h\":650,\"maximized\":false}}}";  // above kWindowMinW x kWindowMinH, else it starts maximized
        }
        app.visible = true;
        app.load_gui_settings();
        ui.frames(6);
        ImGuiWindow* w = ImGui::FindWindowByName("FC 27 LE Turbo");
        CHECK(w != nullptr && !app.prefs.window.maximized && std::fabs(w->Size.x - 900.0f) < 3.0f, fmt("starts at the stored 900 wide: %.0f", w ? w->Size.x : -1.0f));
        if (!w) return;
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(777.0f, 515.0f));
        ui.frames(3);  // the change is seen, the 0.6 s debounce has not run
        fs::remove(file, ec);
        app.visible = false;  // the toggle key
        ui.frames(3);
        json j = json::parse(read_file(file), nullptr, false);
        const bool ok = j.is_object() && j.contains("gui") && j["gui"].contains("window");
        const float sw = ok ? j["gui"]["window"].value("w", 0.0f) : -1.0f;
        CHECK(ok && std::fabs(sw - 777.0f) < 4.0f, fmt("gui_settings.json holds the 777 wide panel written at the hide: %s w=%.0f", ok ? "window block" : "no window block", sw));
        app.visible = true;
        ui.frames(3);
    });

    run_case("e6-layout E2: a grab on the panel corner is logged once when it ends (the H1 discriminator)", [&] {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        {
            std::ofstream o(file.string(), std::ios::binary | std::ios::trunc);
            o << "{\"gui\":{\"window\":{\"x\":150,\"y\":90,\"w\":900,\"h\":650,\"maximized\":false}}}";
        }
        app.visible = true;
        app.load_gui_settings();
        ui.frames(6);
        std::vector<std::string> hooked;
        const auto keep_hook = app.log_hook;
        app.log_hook = [&](const std::string& l) { hooked.push_back(l); };
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 from(150.0f + 900.0f - 3.0f, 90.0f + 650.0f - 3.0f), to(from.x + 200.0f, from.y + 100.0f);
        io.AddMousePosEvent(from.x, from.y);
        ui.frames(3);
        io.AddMouseButtonEvent(0, true);
        ui.frames(3);
        for (int i = 1; i <= 10; ++i) {
            io.AddMousePosEvent(from.x + (to.x - from.x) * float(i) / 10.0f, from.y + (to.y - from.y) * float(i) / 10.0f);
            ui.frames(1);
        }
        ui.frames(2);
        io.AddMouseButtonEvent(0, false);
        ui.frames(3);
        io.AddMousePosEvent(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ui.frames(2);
        int n = 0;
        std::string line;
        for (const std::string& l : hooked)
            if (l.compare(0, 15, "window gesture:") == 0) { ++n; line = l; }
        app.log_hook = keep_hook;
        CHECK(n == 1, fmt("one gesture line for one corner drag: %d", n));
        CHECK(line.find("resize 900x650 at 150,90 -> 1100x750 at 150,90") != std::string::npos && line.find("mouse down after release: no") != std::string::npos &&
                  line.find("not maximized") != std::string::npos,
              "the line names the grab, both rectangles and the verdict: '" + line + "'");
        ui.frames(70);
        app.visible = true;
    });

    run_case("e6-layout L1: the panel corner dragged and released is in gui_settings.json 0.03 s later (a kill inside the debounce keeps it)", [&] {
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        {
            std::ofstream o(file.string(), std::ios::binary | std::ios::trunc);
            o << "{\"gui\":{\"window\":{\"x\":150,\"y\":90,\"w\":900,\"h\":650,\"maximized\":false}}}";
        }
        app.visible = true;
        app.load_gui_settings();
        ui.frames(70);  // settled: the apply is done, nothing waits
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 from(150.0f + 900.0f - 3.0f, 90.0f + 650.0f - 3.0f), to(from.x + 200.0f, from.y + 100.0f);
        io.AddMousePosEvent(from.x, from.y);
        ui.frames(3);
        io.AddMouseButtonEvent(0, true);
        ui.frames(3);
        for (int i = 1; i <= 10; ++i) {
            io.AddMousePosEvent(from.x + (to.x - from.x) * float(i) / 10.0f, from.y + (to.y - from.y) * float(i) / 10.0f);
            ui.frames(1);
        }
        ui.frames(2);
        fs::remove(file, ec);  // whatever was written during the drag does not count
        io.AddMouseButtonEvent(0, false);
        ui.frames(2);  // 0.03 s after the release: far inside the 0.6 s debounce
        json j = json::parse(read_file(file), nullptr, false);
        const bool ok = j.is_object() && j.contains("gui") && j["gui"].contains("window");
        const float sw = ok ? j["gui"]["window"].value("w", 0.0f) : -1.0f, sh = ok ? j["gui"]["window"].value("h", 0.0f) : -1.0f;
        CHECK(ok && std::fabs(sw - 1100.0f) < 4.0f && std::fabs(sh - 750.0f) < 4.0f,
              fmt("0.03 s after the release gui_settings.json holds 1100x750 (the base waited 0.6 s): %s w=%.0f h=%.0f", ok ? "window block" : "no window block", sw, sh));
        io.AddMousePosEvent(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ui.frames(70);
        app.visible = true;
    });

    std::error_code ec;
    {
        std::ofstream o(file.string(), std::ios::binary | std::ios::trunc);
        o << keep_file;
    }
    app.prefs = keep_prefs;
    app.visible = keep_visible;
    app.legacy_pages = keep_legacy;
    g_display_override = keep_override;
    app.load_gui_settings();
    ui.frames(4);
    (void)ec;
}

}  // namespace layout_flush_test
