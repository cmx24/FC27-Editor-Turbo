// Native tests (Turbo X 1.0.2 lane R2-R(c): dialogs remember their size). Case names contain "r2-r" and "layout".
// What is pinned (each FAILS on e797310 and passes with the R2-R(c) change):
//   (c) the in-overlay file picker (file_picker_modal, the real dialog) resized, closed and reopened keeps its size, and so it does after a
//       settings reload (gui.splits saved and loaded into a fresh store and ImGui context); the base reopened it at 680x500 every time
//   (c2) source scan: every dialog of the lane opens through modal_size::set_next and records its size with modal_size::track (Looks >
//       Managers real face, Choose a real face, the tattoo and gallery pickers, Career Mode tracker, .player file)
// Included by test_main.cpp after test_r2r_split.h.
#pragma once
#include <string>

#include "test_layout_store.h"
#include "ui/file_picker.h"

namespace r2r_dialog_test {

using namespace turbo;
using layout_store_test::near_f;

static void run() {
#ifdef TURBO_HAVE_LAYOUT_STORE
    run_case("r2-r layout (c): the file picker resized, closed and reopened keeps its size, also after a settings reload", [&] {
        layout_store_test::SplitGuard guard;
        g_ui_scale = 1.0f;
        const char* kId = "Choose a file##r2r";
        const fs::path dir = g_out / "r2r_picker";
        std::error_code ec;
        fs::create_directories(dir, ec);
        nlohmann::json gui = nlohmann::json::object();
        bool open_it = true, close_it = false;
        ImVec2 resize_to(0.0f, 0.0f), seen(0.0f, 0.0f);
        FilePicker fp;
        fp.mode = PickMode::Open;
        fp.title = "R2-R";
        fp.start = dir;
        auto frame = [&] {
            ImGui_ImplNull_NewFrame();
            ImGui::NewFrame();
            ++g_frame;
            ImGui::GetIO().DisplaySize = ImVec2(1920.0f, 1080.0f);
            if (close_it) { ImGui::ClosePopupToLevel(0, false); close_it = false; }
            if (open_it) { ImGui::OpenPopup(kId); open_it = false; }
            if (resize_to.x > 0.0f) { ImGui::SetWindowSize(kId, resize_to); resize_to = ImVec2(0.0f, 0.0f); }
            fs::path out;
            file_picker_modal(kId, fp, out);
            if (ImGuiWindow* w = ImGui::FindWindowByName(kId); w && w->Active) seen = w->Size;
            ImGui::Render();
            ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        };
        {
            SplitStore st;
            split::bind(&st);
            layout_store_test::World w;
            ImGui::GetIO().DisplaySize = ImVec2(1920.0f, 1080.0f);
            for (int i = 0; i < 6; ++i) frame();
            CHECK(near_f(seen.x, 680.0f) && near_f(seen.y, 500.0f), fmt("first open at the default 680x500: %.0fx%.0f", seen.x, seen.y));
            resize_to = ImVec2(900.0f, 620.0f);  // the person drags the corner (mouse up)
            for (int i = 0; i < 6; ++i) frame();
            CHECK(near_f(seen.x, 900.0f) && near_f(seen.y, 620.0f), fmt("resized to 900x620: %.0fx%.0f", seen.x, seen.y));
            close_it = true;
            for (int i = 0; i < 4; ++i) frame();
            open_it = true;
            seen = ImVec2(0.0f, 0.0f);
            for (int i = 0; i < 6; ++i) frame();
            CHECK(near_f(seen.x, 900.0f) && near_f(seen.y, 620.0f), fmt("reopened at 900x620 (the base reopened at 680x500): %.0fx%.0f", seen.x, seen.y));
            st.save(gui);
        }
        SplitStore st2;
        st2.load(gui);
        split::bind(&st2);
        layout_store_test::World w2;
        ImGui::GetIO().DisplaySize = ImVec2(1920.0f, 1080.0f);
        open_it = true;
        seen = ImVec2(0.0f, 0.0f);
        for (int i = 0; i < 6; ++i) frame();
        CHECK(near_f(seen.x, 900.0f) && near_f(seen.y, 620.0f), fmt("after a settings reload it opens at 900x620: %.0fx%.0f", seen.x, seen.y));
    });
#endif

    run_case("r2-r layout (c2): every dialog of the lane opens through modal_size::set_next and tracks its size (source scan)", [&] {
        const fs::path root = layout_store_test::turbogui_root() / "src" / "ui";
        struct Site {
            const char* file;
            const char* id;
        };
        const Site sites[] = {
            {"ui_looks_studio.cpp", "mgrface"}, {"ui_faces.cpp", "realface"},     {"ui_faces.cpp", "mgrface"},  {"file_picker.cpp", "filepicker"},
            {"ui_images.cpp", "tattoopick"},   {"ui_images.cpp", "gallerypick"}, {"ui_cmtracker.cpp", "cmtracker"}, {"ui_cmtracker.cpp", "playerfile"},
        };
        for (const Site& s : sites) {
            const std::string src = layout_store_test::slurp(root / s.file);
            CHECK(layout_store_test::has(src, std::string("modal_size::set_next(\"") + s.id + "\""),
                  std::string(s.file) + ": modal_size::set_next(\"" + s.id + "\", ...) before the dialog");
        }
        for (const char* f : {"ui_faces.cpp", "file_picker.cpp", "ui_images.cpp", "ui_cmtracker.cpp"}) {
            const std::string src = layout_store_test::slurp(root / f);
            CHECK(layout_store_test::has(src, "modal_size::track(\""), std::string(f) + ": modal_size::track inside the dialog");
        }
        const std::string faces = layout_store_test::slurp(root / "ui_faces.cpp"), looks = layout_store_test::slurp(root / "ui_looks_studio.cpp");
        CHECK(!layout_store_test::has(faces, "SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing)") &&
                  !layout_store_test::has(looks, "SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing)"),
              "no fixed 880x640 Appearing size left for the face choosers");
    });
}

}  // namespace r2r_dialog_test
