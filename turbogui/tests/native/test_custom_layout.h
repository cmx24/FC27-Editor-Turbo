// Native tests: Customize layout (Turbo X 2.0; Cassio 2026-11-01: "only add ability to resize, scale, show/hide all the main menus, frames and
// etc. i want to be able to customize the layout / visualization"). ui/gui_prefs.h CustomLayout + SplitStore::folded, ui/ui_custom_layout.*,
// the shell in ui/app.cpp (top bar, left menu, page tabs, content), ui/ui_split.h (folded panes).
//   run():       the saved model alone: defaults, round trip, clamps, a hand-edited file, the rules that keep a way back
//   ui_cases():  the real App and its pages: Ctrl+L and the Layout button open the window; each main menu hides and comes back; top-bar items,
//                left-menu areas and page tabs hide one by one (the current page's tab stays, Turbo settings never hides); the "Menus" button
//                when both main menus are hidden; the left menu's width (slider and drag handle); every region's text size; the list pane
//                folds to a ">" strip and back; Reset to default; everything is written to gui_settings.json
// Included by test_main.cpp after test_layout_flush.h; ui_cases runs before test_shell_app clears the page registry, run() with the pure tests.
#pragma once
#include <cmath>
#include <fstream>
#include <string>

#include "imgui_internal.h"
#include "ui/gui_prefs.h"
#include "ui/ui_custom_layout.h"
#include "ui/ui_split.h"

namespace custom_layout_test {

using namespace turbo;

static void run() {
    run_case("custom layout: defaults, save / load round trip, clamps", [] {
        CustomLayout d;
        CHECK(d.is_default() && d.top_bar && d.rail && d.page_tabs && d.rail_width == CustomLayout::kRailDefaultW, "defaults: everything shown, 176 px");
        CustomLayout c;
        c.top_bar = false;
        c.page_tabs = false;
        c.rail_width = 260.0f;
        c.scale_top_bar = 0.8f;
        c.scale_rail = 1.25f;
        c.scale_tabs = 0.9f;
        c.scale_content = 1.4f;
        c.hidden_top = {"refresh", "profiles"};
        c.hidden_areas = {"Tactics"};
        c.hidden_pages = {"Staff"};
        nlohmann::json g = nlohmann::json::object();
        c.save(g);
        const CustomLayout r = CustomLayout::load(g);
        CHECK(!r.top_bar && r.rail && !r.page_tabs && r.rail_width == 260.0f, "menus and width come back");
        CHECK(std::fabs(r.scale_top_bar - 0.8f) < 1e-4f && std::fabs(r.scale_rail - 1.25f) < 1e-4f && std::fabs(r.scale_tabs - 0.9f) < 1e-4f &&
                  std::fabs(r.scale_content - 1.4f) < 1e-4f,
              "the four text sizes come back");
        CHECK(r.hidden_top == c.hidden_top && r.hidden_areas == c.hidden_areas && r.hidden_pages == c.hidden_pages, "the hidden items come back");
        CHECK(!r.top_item("refresh") && r.top_item("search") && !r.area_shown("Tactics") && r.area_shown("Home") && !r.page_shown("Staff"),
              "queries");
        nlohmann::json bad = {{"custom_layout",
                               {{"rail_width", 5000}, {"scale_rail", 9.0}, {"scale_content", 0.1}, {"top_bar", "no"}, {"hidden_top", "refresh"},
                                {"hidden_areas", {"Turbo settings", 7, "Club"}}}}};
        const CustomLayout b = CustomLayout::load(bad);
        CHECK(b.rail_width == CustomLayout::kRailMaxW && b.scale_rail == CustomLayout::kScaleMax && b.scale_content == CustomLayout::kScaleMin,
              "out-of-range numbers are clamped");
        CHECK(b.top_bar && b.hidden_top.empty(), "a value of the wrong type keeps the default");
        CHECK(b.area_shown(CustomLayout::kSettingsArea) && !b.area_shown("Club") && b.hidden_areas.size() == 1,
              "a hand-edited file cannot hide Turbo settings; a non-string entry is skipped");
        CustomLayout s;
        CHECK(!s.set_area_hidden(CustomLayout::kSettingsArea, true) && s.area_shown(CustomLayout::kSettingsArea), "Turbo settings never hides");
        CHECK(s.set_area_hidden("Looks", true) && !s.set_area_hidden("Looks", true) && s.set_area_hidden("Looks", false), "a change reports true once");
        CHECK(CustomLayout::clamp_rail(std::nanf("")) == CustomLayout::kRailDefaultW && CustomLayout::clamp_scale(std::nanf("")) == 1.0f, "NaN -> default");
        CHECK(CustomLayout::load(nlohmann::json::array()).is_default() && CustomLayout::load(nlohmann::json::object()).is_default(), "no object: defaults");
    });

    run_case("custom layout: GuiPrefs keeps it with the rest of gui_settings.json; folded panes round trip", [] {
        nlohmann::json file = {{"gui", {{"toggle_key", 119}, {"rail_collapsed", true}}}, {"callnames", {{"bank_team", "dr"}}}};
        GuiPrefs p = GuiPrefs::load(file);
        CHECK(p.custom.is_default() && p.rail_collapsed, "an older file: default layout, its other choices kept");
        p.custom.rail = false;
        p.custom.scale_content = 1.2f;
        p.splits.set_folded("##select", true);
        CHECK(p.splits.dirty && p.splits.is_folded("##select"), "folding marks the store dirty");
        p.save(file);
        CHECK(file["gui"]["toggle_key"] == 119 && file["callnames"]["bank_team"] == "dr", "other keys untouched");
        CHECK(file["gui"]["custom_layout"]["rail"] == false && file["gui"]["folded_panes"] == nlohmann::json::array({"##select"}), "written");
        const GuiPrefs q = GuiPrefs::load(file);
        CHECK(!q.custom.rail && std::fabs(q.custom.scale_content - 1.2f) < 1e-4f && q.splits.is_folded("##select") && !q.splits.dirty,
              "read back on the next start");
        SplitStore st;
        st.set_folded("##lslist", true);
        st.dirty = false;
        st.set_folded("##lslist", true);
        CHECK(!st.dirty, "folding a folded pane again changes nothing");
        CHECK(split::foldable("##select") && split::foldable("##lspreview") && !split::foldable("##lscards") && !split::foldable("##editor"),
              "only panes left of a divider fold");
    });
}

static void ui_cases(App& app, Ui& ui) {
    using namespace turbo::shell;
    const GuiPrefs keep_prefs = app.prefs;
    const bool keep_visible = app.visible;
    app.visible = true;
    app.prefs.custom = CustomLayout();
    app.prefs.splits.folded.clear();
    app.prefs.rail_collapsed = false;
    custom_layout::window_open() = false;
    auto settle = [&](int n = 4) { ui.frames(n); };
    auto saved = [&]() {
        std::ifstream f(app.turbo_output() / "gui_settings.json", std::ios::binary);
        nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        return j.is_discarded() ? nlohmann::json::object() : j;
    };

    run_case("customize layout UI: Ctrl+L and the Layout button open the window; the top bar hides and comes back", [&] {
        app.go(Page::Home);
        settle();
        CHECK(ui.find("###top_layout") != nullptr && ui.find("###statusrefresh") != nullptr, "default: the top bar with Layout and Refresh");
        ui.key(ImGuiKey_L, true);
        settle(2);
        CHECK(custom_layout::window_open() && ui.find("###cl_top_bar", "Customize layout") != nullptr, "Ctrl+L opened Customize layout");
        ui.key(ImGuiKey_L, true);
        settle(2);
        CHECK(!custom_layout::window_open() && ui.find("###cl_top_bar") == nullptr, "Ctrl+L again closed it");
        CHECK(ui.click("###top_layout"), "click Layout on the top bar");
        settle(2);
        CHECK(custom_layout::window_open(), "the Layout button opened it");
        CHECK(ui.click("###cl_top_bar", "Customize layout"), "untick Top bar");
        settle(3);
        CHECK(!app.prefs.custom.top_bar && ui.find("###statusrefresh") == nullptr && ui.find("###top_layout") == nullptr && ui.find("###changes") == nullptr,
              "the top bar is gone");
        CHECK(saved()["gui"]["custom_layout"]["top_bar"] == false, "saved: gui.custom_layout.top_bar false");
        CHECK(ui.find("Home", "##rail", "##shell") != nullptr, "the left menu still shows");
        CHECK(ui.click("###cl_top_bar", "Customize layout"), "tick Top bar");
        settle(3);
        CHECK(app.prefs.custom.top_bar && ui.find("###statusrefresh") != nullptr, "the top bar is back");
    });

    run_case("customize layout UI: top-bar buttons hide one by one; Layout always stays", [&] {
        CHECK(ui.click("###cl_sec_top", "Customize layout"), "open Top bar buttons");
        settle(2);
        CHECK(ui.click("###cl_top_refresh", "Customize layout") && ui.click("###cl_top_changes", "Customize layout"), "untick Refresh and Changes");
        settle(3);
        CHECK(ui.find("###statusrefresh") == nullptr && ui.find("###changes") == nullptr && ui.find("###top_pause_gifs") != nullptr &&
                  ui.find("###top_layout") != nullptr,
              "Refresh and Changes gone, Pause animations and Layout stay");
        CHECK(saved()["gui"]["custom_layout"]["hidden_top"] == nlohmann::json::array({"changes", "refresh"}), "saved: hidden_top");
        CHECK(ui.find("###cl_top_layout") == nullptr, "there is no switch for the Layout button");
        CHECK(ui.click("###cl_top_refresh", "Customize layout") && ui.click("###cl_top_changes", "Customize layout"), "tick them again");
        settle(3);
        CHECK(ui.find("###statusrefresh") != nullptr && ui.find("###changes") != nullptr, "back");
        CHECK(ui.click("###cl_sec_top", "Customize layout"), "close the section");
        settle(2);
    });

    run_case("customize layout UI: left-menu areas hide (not Turbo settings); a hidden area still opens from a link", [&] {
        CHECK(ui.click("###cl_sec_areas", "Customize layout"), "open Left menu areas");
        settle(2);
        const std::string tac = "###cl_area_" + std::to_string(static_cast<int>(Area::Tactics));
        const std::string set = "###cl_area_" + std::to_string(static_cast<int>(Area::Settings));
        CHECK(ui.click(tac, "Customize layout"), "untick Tactics");
        settle(3);
        CHECK(ui.find("Tactics", "##rail", "##shell") == nullptr && ui.find("Club", "##rail", "##shell") != nullptr, "Tactics left the left menu");
        ui.click(set, "Customize layout");  // its box is disabled: the click does nothing
        settle(3);
        CHECK(app.prefs.custom.hidden_areas.count(CustomLayout::kSettingsArea) == 0 && ui.find("Turbo settings", "##rail", "##shell") != nullptr,
              "Turbo settings stays (not in the hidden list, still in the left menu)");
        CHECK(saved()["gui"]["custom_layout"]["hidden_areas"] == nlohmann::json::array({"Tactics"}), "saved: hidden_areas = [Tactics]");
        app.go(default_page(Area::Tactics));
        settle(3);
        CHECK(app.place().area == Area::Tactics, "a hidden area still opens through go() (links, search)");
        CHECK(ui.click(tac, "Customize layout"), "tick Tactics again");
        settle(3);
        CHECK(ui.find("Tactics", "##rail", "##shell") != nullptr, "Tactics is back");
        CHECK(ui.click("###cl_sec_areas", "Customize layout"), "close the section");
        settle(2);
    });

    run_case("customize layout UI: page tabs hide one by one (the current page's tab stays) and as a whole", [&] {
        app.go(Page::PlayerEdit);
        settle(3);
        CHECK(ui.find("Staff", "##content", "##pages") != nullptr, "Players strip has Staff");
        CHECK(CustomLayout::set_in(app.prefs.custom.hidden_pages, page_key(Page::Staff), true), "hide the Staff tab");
        CHECK(CustomLayout::set_in(app.prefs.custom.hidden_pages, page_key(Page::PlayerEdit), true), "hide the Player tab (the current page)");
        settle(3);
        CHECK(ui.find("Staff", "##content", "##pages") == nullptr, "the Staff tab is gone");
        CHECK(ui.find("Player", "##content", "##pages") != nullptr, "the current page's tab stays");
        app.prefs.custom.hidden_pages.clear();
        CHECK(ui.click("###cl_page_tabs", "Customize layout"), "untick Page tabs");
        settle(3);
        CHECK(saved()["gui"]["custom_layout"]["page_tabs"] == false, "saved: page_tabs false");
        CHECK(ui.find("Staff", "##content", "##pages") == nullptr && ui.find("Player", "##content", "##pages") == nullptr, "no page tabs");
        CHECK(app.place() == place_of(Page::PlayerEdit), "the page itself still draws");
        CHECK(ui.click("###cl_page_tabs", "Customize layout"), "tick Page tabs");
        settle(3);
        CHECK(ui.find("Staff", "##content", "##pages") != nullptr, "tabs back");
    });

    run_case("customize layout UI: the left menu hides; with the top bar hidden too, the Menus button stays and opens the window", [&] {
        CHECK(ui.click("###cl_rail", "Customize layout"), "untick Left menu");
        settle(3);
        CHECK(ui.find("Home", "##rail", "##shell") == nullptr && ui.find("###top_layout") != nullptr && ui.find("###show_menus") == nullptr,
              "no left menu; the top bar's Layout stays; no Menus button needed");
        CHECK(ui.click("###cl_top_bar", "Customize layout"), "untick Top bar");
        settle(3);
        CHECK(ui.find("###show_menus") != nullptr, "both hidden: the Menus button shows");
        custom_layout::window_open() = false;
        settle(2);
        CHECK(ui.click("###show_menus"), "click Menus");
        settle(2);
        CHECK(custom_layout::window_open(), "it opened Customize layout");
        CHECK(ui.click("###cl_rail", "Customize layout") && ui.click("###cl_top_bar", "Customize layout"), "tick both again");
        settle(3);
        CHECK(ui.find("Home", "##rail", "##shell") != nullptr && ui.find("###statusrefresh") != nullptr && ui.find("###show_menus") == nullptr, "both back");
    });

    run_case("customize layout UI: the left menu's width (setting and drag handle), saved", [&] {
        app.prefs.custom.rail_width = 300.0f;
        settle(3);
        ImGuiWindow* rw = nullptr;
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
            if (std::string(w->Name).find("/##rail_") != std::string::npos && std::string(w->Name).find("FC 27 LE Turbo") == 0) rw = w;
        CHECK(rw && std::fabs(rw->Size.x - S(300.0f)) < 1.5f, fmt("the left menu is 300 px wide (%.1f at UI size %.2f)", rw ? rw->Size.x : -1.0f, g_ui_scale));
        const ItemRec* grip = ui.find("##railgrip");
        CHECK(grip != nullptr, "the drag handle is there");
        if (grip) {
            ImGuiIO& io = ImGui::GetIO();
            const ImVec2 c = grip->rect.GetCenter();
            io.AddMousePosEvent(c.x, c.y);
            ui.frame();
            io.AddMouseButtonEvent(0, true);
            ui.frame();
            for (int i = 1; i <= 8; ++i) {
                io.AddMousePosEvent(c.x - 10.0f * i, c.y);
                ui.frame();
            }
            io.AddMouseButtonEvent(0, false);
            settle(2);
            const float want = 300.0f - 80.0f / (g_ui_scale > 0.0f ? g_ui_scale : 1.0f);
            CHECK(std::fabs(app.prefs.custom.rail_width - want) < 1.5f, fmt("dragged 80 px left: %.1f (want %.1f)", app.prefs.custom.rail_width, want));
            const nlohmann::json j = saved();
            CHECK(j["gui"]["custom_layout"]["rail_width"].is_number() &&
                      std::fabs(j["gui"]["custom_layout"]["rail_width"].get<double>() - std::round(want)) < 1.5,
                  "the width was saved when the mouse was released");
        }
        app.prefs.custom.rail_width = 50.0f;
        settle(2);
        CHECK(rw && std::fabs(rw->Size.x - S(CustomLayout::kRailMinW)) < 1.5f, "a too-small width is clamped to the minimum");
        app.prefs.custom.rail_width = CustomLayout::kRailDefaultW;
        settle(2);
    });

    run_case("customize layout UI: each region's text size (top bar, left menu, tabs, content) and nothing leaks out of its region", [&] {
        app.go(Page::PlayerEdit);
        settle(3);
        auto h = [&](const char* label, const char* win, const char* scope) {
            const ItemRec* r = ui.find(label, win, scope);
            return r ? r->rect.GetHeight() : -1.0f;
        };
        const float top0 = h("###statusrefresh", "", ""), rail0 = h("Home", "##rail", "##shell"), tab0 = h("Staff", "##content", "##pages");
        CHECK(top0 > 0 && rail0 > 0 && tab0 > 0, "baseline heights");
        const float main_before = ImGui::GetStyle().FontScaleMain;
        app.prefs.custom.scale_top_bar = 1.5f;
        app.prefs.custom.scale_rail = 1.5f;
        app.prefs.custom.scale_tabs = 1.5f;
        app.prefs.custom.scale_content = 1.5f;
        settle(3);
        CHECK(h("###statusrefresh", "", "") > top0 * 1.25f, fmt("the top bar grew: %.1f -> %.1f", top0, h("###statusrefresh", "", "")));
        CHECK(h("Home", "##rail", "##shell") > rail0 * 1.1f, fmt("the left menu grew: %.1f -> %.1f", rail0, h("Home", "##rail", "##shell")));
        CHECK(h("Staff", "##content", "##pages") > tab0 * 1.1f, fmt("the tabs grew: %.1f -> %.1f", tab0, h("Staff", "##content", "##pages")));
        CHECK(ImGui::GetStyle().FontScaleMain == main_before, "the global scale is restored after every region");
        app.prefs.custom.scale_top_bar = 1.0f;
        app.prefs.custom.scale_rail = 1.0f;
        app.prefs.custom.scale_tabs = 1.0f;
        settle(3);
        CHECK(std::fabs(h("###statusrefresh", "", "") - top0) < 0.5f && std::fabs(h("Home", "##rail", "##shell") - rail0) < 0.5f &&
                  std::fabs(h("Staff", "##content", "##pages") - tab0) < 0.5f,
              "only the content is still scaled: the other regions are back to their size");
        {   // the content region: a page's own nested windows and PushFont(nullptr, base * x) are scaled too
            float inside = 0.0f, nested = 0.0f, outside = 0.0f;
            ui.extra_draw = [&] {
                ImGui::Begin("##cl_probe");
                outside = ImGui::GetFontSize();
                {
                    custom_layout::RegionScale r(1.5f);
                    inside = ImGui::GetFontSize();
                    ImGui::BeginChild("##cl_probe_child", ImVec2(50, 50));
                    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.85f);
                    nested = ImGui::GetFontSize();
                    ImGui::PopFont();
                    ImGui::EndChild();
                }
                const float after = ImGui::GetFontSize();
                if (std::fabs(after - outside) > 0.01f) outside = -1.0f;
                ImGui::End();
            };
            ui.frames(2);
            ui.extra_draw = nullptr;
            CHECK(outside > 0.0f, "the size is back after the scope");
            CHECK(std::fabs(inside - std::round(outside * 1.5f)) <= 1.0f, fmt("1.5x inside the scope: %.1f vs %.1f", inside, outside));
            CHECK(nested > outside, fmt("a nested child's own smaller font is still scaled: %.1f > %.1f", nested, outside));
        }
        app.prefs.custom.scale_content = 1.0f;
        settle(2);
    });

    run_case("customize layout UI: the list pane folds to a strip and comes back (the > strip, the window)", [&] {
        app.go(Page::PlayerEdit);
        settle(3);
        CHECK(ui.find("###unfold##select") == nullptr, "not folded");
        app.prefs.splits.set_folded("##select", true);
        settle(3);
        CHECK(ui.find("###unfold##select") != nullptr, "folded: the > strip shows");
        CHECK(ui.click("###unfold##select"), "click >");
        settle(3);
        CHECK(!app.prefs.splits.is_folded("##select") && ui.find("###unfold##select") == nullptr, "the list is back");
        CHECK(ui.click("###cl_pane##select", "Customize layout"), "untick the list in Customize layout");
        settle(3);
        CHECK(app.prefs.splits.is_folded("##select") && ui.find("###unfold##select") != nullptr, "folded from the window");
        const nlohmann::json j = saved();
        CHECK(j["gui"]["folded_panes"].is_array() && j["gui"]["folded_panes"].size() == 1 && j["gui"]["folded_panes"][0] == "##select", "saved");
        CHECK(ui.click("###cl_pane##select", "Customize layout"), "tick it again");
        settle(3);
        CHECK(!app.prefs.splits.is_folded("##select"), "shown again");
    });

    run_case("customize layout UI: Reset to default asks first, then puts everything back and saves", [&] {
        app.prefs.custom.top_bar = false;
        app.prefs.custom.scale_content = 1.3f;
        app.prefs.custom.hidden_areas = {"Looks"};
        app.prefs.splits.set_folded("##lslist", true);
        app.prefs.rail_collapsed = true;
        settle(2);
        CHECK(ui.click("###cl_reset", "Customize layout"), "click Reset to default");
        settle(2);
        CHECK(ui.find("###cl_reset_cancel") != nullptr && ui.find("###cl_reset_go") != nullptr, "it asks");
        CHECK(ui.click("###cl_reset_cancel"), "Cancel");
        settle(2);
        CHECK(!app.prefs.custom.top_bar, "Cancel changed nothing");
        CHECK(ui.click("###cl_reset", "Customize layout") && (settle(2), ui.click("###cl_reset_go")), "Reset");
        settle(3);
        CHECK(app.prefs.custom.is_default() && app.prefs.splits.folded.empty() && !app.prefs.rail_collapsed, "back to the default");
        const nlohmann::json j = saved();
        CHECK(j["gui"]["custom_layout"]["top_bar"] == true && j["gui"]["folded_panes"].empty() && j["gui"]["rail_collapsed"] == false, "saved");
        CHECK(ui.click("###cl_close", "Customize layout"), "Close");
        settle(2);
        CHECK(!custom_layout::window_open(), "closed");
    });

    custom_layout::window_open() = false;
    app.prefs = keep_prefs;
    app.visible = keep_visible;
    if (!app.save_gui_settings()) std::printf("  (could not restore gui_settings.json after the custom layout cases)\n");
    ui.frames(3);
}

}  // namespace custom_layout_test
