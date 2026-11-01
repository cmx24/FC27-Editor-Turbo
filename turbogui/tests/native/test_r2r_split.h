// Native tests (Turbo X 1.0.2 lane R2-R, resize permanence; owner: "needs to happen"). Every case name contains "r2-r" and "split", so
// TURBO_TEST_ONLY="r2-r" runs this file and test_r2r_dialog.h alone. Each case builds its own ImGui context (layout_store_test::World, the
// null backend) and draws one host window holding a "##select" pane (split::pane_x) under an ID scope per page (in the App the pane's
// window name carries the page's ID stack, so each page has its own ImGui window).
// What is pinned (each FAILS on e797310, the Turbo X 1.0.1 candidate, and passes with the R2-R change):
//   (a) the host window made narrower clamps the pane (70 % of the panel): the clamp is NOT written over the stored width, and the pane
//       gets its stored width back when the window grows again (the base committed ANY size change, so the clamp became the new width)
//   (b) a "##select" dragged on page A: page B (never dragged) opens at that width, not the default; after a settings reload (gui.splits
//       saved and loaded into a fresh store and ImGui context) A and B both open at it (the base forced the default on every page with no
//       key of its own: ~30 pages, 3 stored keys)
//   (b2) two sub-tabs that share one page key (two ImGui windows, one gui.splits key) do not overwrite each other's width
// Included by test_main.cpp after test_layout_flush.h (uses layout_store_test's World / SplitGuard).
#pragma once
#include <string>

#include "test_layout_store.h"

namespace r2r_split_test {

using namespace turbo;
using layout_store_test::near_f;

#ifdef TURBO_HAVE_LAYOUT_STORE

// One host window `host_w` wide; the pane "##select" (default 300, min 180, max 70 %) under the ID scope `scope`
struct Host {
    float host_w = 1000.0f;
    std::string scope = "PageA";
    float seen = -1.0f;
    void frame() {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ++g_frame;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(host_w, 400.0f));
        ImGui::Begin("##r2rhost", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushID(scope.c_str());
        split::pane_x("##select", 300.0f);
        seen = ImGui::GetWindowWidth();
        ImGui::TextUnformatted("rows");
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::End();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) frame();
    }
    // the active "##select" pane of this scope
    ImGuiWindow* pane() const {
        const std::string part = std::string("/") + "##select_";
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
            if (w->Active && (w->Flags & ImGuiWindowFlags_ChildWindow) && w->Name && std::strstr(w->Name, part.c_str())) return w;
        return nullptr;
    }
    // press on the pane's right border, move by dx in steps (one frame each), release
    void drag_border(float dx, int steps = 14) {
        ImGuiWindow* p = pane();
        if (!p) return;
        const ImVec2 from(p->Pos.x + p->Size.x + 1.5f, p->Pos.y + p->Size.y * 0.5f);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(from.x, from.y);
        frames(3);
        io.AddMouseButtonEvent(0, true);
        frames(3);
        for (int i = 1; i <= steps; ++i) {
            io.AddMousePosEvent(from.x + dx * float(i) / float(steps), from.y);
            frames(1);
        }
        frames(2);
        io.AddMouseButtonEvent(0, false);
        frames(3);
        io.AddMousePosEvent(990.0f, 390.0f);
        frames(2);
    }
};

#endif  // TURBO_HAVE_LAYOUT_STORE

static void run_a() {
#ifdef TURBO_HAVE_LAYOUT_STORE
    run_case("r2-r split (a): a pane clamped by a narrower window keeps its stored width and gets it back when the window grows", [&] {
        layout_store_test::SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        st.set("PageA/##select", 600.0f);
        st.dirty = false;
        split::bind(&st);
        split::set_page("PageA");
        split::reset_runtime_for_tests();
        layout_store_test::World w;
        Host h;
        h.frames(6);
        CHECK(near_f(h.seen, 600.0f), fmt("page A at its stored 600: %.1f", h.seen));
        h.host_w = 600.0f;  // the main window shrinks: the pane is clamped to 70 % of the panel
        h.frames(10);
        CHECK(h.seen < 450.0f, fmt("clamped by the narrower window: %.1f", h.seen));
        CHECK(near_f(st.get("PageA/##select", 0.0f), 600.0f) && !st.dirty,
              fmt("the clamp is not written over the stored width (the base stored the clamp): stored %.1f dirty=%d", st.get("PageA/##select", -1.0f), st.dirty ? 1 : 0));
        h.host_w = 1000.0f;  // restored
        h.frames(10);
        CHECK(near_f(h.seen, 600.0f), fmt("the window grows again: the pane is back at 600 (the base kept the clamp): %.1f", h.seen));
        h.drag_border(50.0f);  // a real drag still commits
        CHECK(near_f(h.seen, 650.0f) && near_f(st.get("PageA/##select", 0.0f), 650.0f),
              fmt("a drag of 50 px is stored once released: shown %.1f stored %.1f", h.seen, st.get("PageA/##select", -1.0f)));
    });
#endif
}

static void run_b() {
#ifdef TURBO_HAVE_LAYOUT_STORE
    run_case("r2-r split (b): ##select dragged on page A opens at that width on page B (never dragged), and both keep it after a settings reload", [&] {
        layout_store_test::SplitGuard guard;
        g_ui_scale = 1.0f;
        nlohmann::json gui = nlohmann::json::object();
        {
            SplitStore st;
            split::bind(&st);
            split::set_page("PageA");
            split::reset_runtime_for_tests();
            layout_store_test::World w;
            Host h;
            h.frames(6);
            CHECK(near_f(h.seen, 300.0f), fmt("page A starts at the default 300: %.1f", h.seen));
            h.drag_border(150.0f);
            CHECK(near_f(h.seen, 450.0f) && near_f(st.get("PageA/##select", 0.0f), 450.0f), fmt("dragged to 450 and stored: %.1f", st.get("PageA/##select", -1.0f)));
            split::set_page("PageB");
            h.scope = "PageB";
            h.frames(6);
            CHECK(near_f(h.seen, 450.0f), fmt("page B (never dragged) opens at A's 450 (the base forced the default 300): %.1f", h.seen));
            CHECK(!st.has("PageB/##select"), "opening page B writes nothing for page B");
            split::set_page("PageA");
            h.scope = "PageA";
            h.frames(6);
            CHECK(near_f(h.seen, 450.0f), fmt("back on page A: 450: %.1f", h.seen));
            st.save(gui);
        }
        // a settings reload: the saved gui.splits into a fresh store, fresh run-time state, a fresh ImGui context
        SplitStore st2;
        st2.load(gui);
        split::bind(&st2);
        split::set_page("PageB");
        split::reset_runtime_for_tests();
        layout_store_test::World w2;
        Host h2;
        h2.scope = "PageB";
        h2.frames(6);
        CHECK(near_f(h2.seen, 450.0f), fmt("after the reload page B opens at 450: %.1f", h2.seen));
        split::set_page("PageA");
        h2.scope = "PageA";
        h2.frames(6);
        CHECK(near_f(h2.seen, 450.0f), fmt("after the reload page A opens at 450: %.1f", h2.seen));
        // B dragged on its own: B keeps its width, A keeps its own, a third page never dragged takes the last drag
        split::set_page("PageB");
        h2.scope = "PageB";
        h2.frames(6);
        h2.drag_border(-70.0f);
        CHECK(near_f(h2.seen, 380.0f) && near_f(st2.get("PageB/##select", 0.0f), 380.0f), fmt("page B dragged to 380: %.1f", st2.get("PageB/##select", -1.0f)));
        split::set_page("PageA");
        h2.scope = "PageA";
        h2.frames(6);
        CHECK(near_f(h2.seen, 450.0f), fmt("page A keeps its own 450: %.1f", h2.seen));
        split::set_page("PageC");
        h2.scope = "PageC";
        h2.frames(6);
        CHECK(near_f(h2.seen, 380.0f), fmt("page C (never dragged) opens at the last dragged 380: %.1f", h2.seen));
    });

    run_case("r2-r split (b2): two sub-tabs sharing one page key do not overwrite each other's width", [&] {
        layout_store_test::SplitGuard guard;
        g_ui_scale = 1.0f;
        SplitStore st;
        split::bind(&st);
        split::set_page("PageA");
        split::reset_runtime_for_tests();
        layout_store_test::World w;
        Host h;
        h.scope = "tab1";
        h.frames(6);
        h.scope = "tab2";  // same page key, another ImGui window
        h.frames(6);
        h.drag_border(120.0f);
        CHECK(near_f(st.get("PageA/##select", 0.0f), 420.0f), fmt("sub-tab 2 dragged to 420 and stored: %.1f", st.get("PageA/##select", -1.0f)));
        h.scope = "tab1";  // its window still holds the 300 it had
        h.frames(8);
        CHECK(near_f(st.get("PageA/##select", 0.0f), 420.0f),
              fmt("showing sub-tab 1 does not write its old 300 over the stored 420 (the base did): %.1f", st.get("PageA/##select", -1.0f)));
        CHECK(near_f(h.seen, 420.0f), fmt("sub-tab 1 shows the page's 420: %.1f", h.seen));
    });
#endif
}

}  // namespace r2r_split_test
