// Native tests: the 2.0 UX filter work (branch 2.0/ux-filters). core/range_filter.h (two-sided ranges, 0 = no bound), the
// game's visible position list and the descriptors of internal codes (core/model.h), and widgets.cpp checkbox_right (label
// left, box right, the ID of ImGui::Checkbox(label), the whole row clickable). Included by test_main.cpp after the Ui class;
// test_ux_filters_core() runs from main with the core tests and makes its own short-lived ImGui context for the widget
// case (before test_ui makes the real one). The Players list filter grid itself is driven in test_ui ("UI: Players list
// filters").
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/range_filter.h"

static void test_ux_filters_core() {
    using namespace turbo;
    run_case("range filter: 0 = no bound, from / to inclusive, unknown values fail a set bound", [] {
        CHECK(range_passes(0, 0, 5) && range_passes(0, 0, -3, false), "unbounded passes everything, even an unknown value");
        CHECK(range_passes(20, 0, 20) && !range_passes(20, 0, 19), "from is inclusive");
        CHECK(range_passes(0, 30, 30) && !range_passes(0, 30, 31), "to is inclusive");
        CHECK(range_passes(20, 30, 25) && range_passes(20, 30, 20) && range_passes(20, 30, 30) && !range_passes(20, 30, 19) &&
                  !range_passes(20, 30, 31),
              "both bounds");
        CHECK(!range_passes(0, 30, 25, false) && !range_passes(20, 0, 25, false) && !range_passes(20, 30, 25, false),
              "an unknown value (no birth date) fails any set bound, as the old Max age did");
        IntRange r;
        CHECK(!r.active() && range_passes(r, 1), "a fresh range is inactive");
        r.to = 25;
        CHECK(r.active() && range_passes(r, 25) && !range_passes(r, 26) && !range_passes(r, 30, false), "IntRange to only");
        r.from = 20;
        CHECK(range_passes(r, 20) && !range_passes(r, 19), "IntRange from and to");
        r.clear();
        CHECK(!r.active(), "clear()");
    });
    run_case("range filter: normalize keeps 0 <= from <= to <= hi, the side just edited wins, 0 stays 0", [] {
        int f = 30, t = 20;
        range_normalize(f, t, 99, false);
        CHECK(f == 30 && t == 30, fmt("from raised above to: to follows (%d-%d)", f, t));
        f = 30, t = 20;
        range_normalize(f, t, 99, true);
        CHECK(f == 20 && t == 20, fmt("to lowered below from: from follows (%d-%d)", f, t));
        f = -5, t = 150;
        range_normalize(f, t, 99);
        CHECK(f == 0 && t == 99, fmt("clamped to 0..hi (%d-%d)", f, t));
        f = 0, t = 25;
        range_normalize(f, t, 99);
        CHECK(f == 0 && t == 25, "from 0 = no bound is left alone");
        f = 40, t = 0;
        range_normalize(f, t, 99, true);
        CHECK(f == 40 && t == 0, "to 0 = no bound is not forced up to from");
        IntRange r{30, 20};
        range_normalize(r, 99);
        CHECK(r.from == 30 && r.to == 30, "IntRange overload");
        CHECK(range_text("Age", 20, 30) == "Age 20-30" && range_text("Age", 20, 0) == "Age from 20" && range_text("Age", 0, 30) == "Age up to 30" &&
                  range_text("Age", 0, 0).empty(),
              "range text");
    });
    run_case("positions: the game's twelve visible positions in role order, 16 internal codes, descriptors never a bare number", [] {
        const std::vector<int>& v = visible_positions();
        CHECK(v.size() == 12, fmt("12 visible positions, got %zu", v.size()));
        const std::vector<std::string> want = {"GK", "RB", "LB", "CB", "CDM", "CM", "RM", "LM", "CAM", "RW", "LW", "ST"};
        for (size_t i = 0; i < want.size() && i < v.size(); ++i)
            CHECK(position_name(v[i]) == want[i] && position_visible(v[i]), want[i] + " at " + std::to_string(i) + ": " + position_name(v[i]));
        int internal = 0;
        for (const char* n : {"SW", "RWB", "LWB", "RCB", "LCB", "RDM", "LDM", "RCM", "LCM", "RAM", "LAM", "RF", "CF", "LF", "RS", "LS"}) {
            int code = -1;
            for (int p = 0; p < position_count(); ++p)
                if (std::string(position_name(p)) == n) code = p;
            CHECK(code >= 0 && !position_visible(code), std::string(n) + " is an internal code");
            ++internal;
        }
        CHECK(internal + static_cast<int>(v.size()) == position_count(), fmt("12 + 16 = %d codes", position_count()));
        CHECK(!position_visible(-1) && !position_visible(position_count()), "none / out of range are not visible");
        CHECK(position_descriptor(5) == "CB" && position_descriptor(0) == "GK" && position_descriptor(25) == "ST", "visible: the name alone");
        CHECK(position_descriptor(4) == "RCB (internal, plays as CB)", "RCB: " + position_descriptor(4));
        CHECK(position_descriptor(9) == "RDM (internal, plays as CDM)" && position_descriptor(13) == "RCM (internal, plays as CM)" &&
                  position_descriptor(19) == "LAM (internal, plays as CAM)" && position_descriptor(26) == "LS (internal, plays as ST)",
              "the paired codes play as their visible position");
        CHECK(position_descriptor(1) == "SW (internal)" && position_descriptor(21) == "CF (internal)" && position_descriptor(2) == "RWB (internal)",
              "unpaired internal: " + position_descriptor(1));
        CHECK(position_descriptor(-1) == "None", "none");
        CHECK(position_descriptor(40) == "Unknown position 40", "outside the table: " + position_descriptor(40));
        for (int p = -2; p < 64; ++p) {
            const std::string d = position_descriptor(p);
            const bool bare = !d.empty() && std::all_of(d.begin(), d.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
            CHECK(!d.empty() && !bare && d != "?", fmt("never a bare number or '?': %d -> %s", p, d.c_str()));
        }
    });
    run_case("checkbox_right: the ID of ImGui::Checkbox(label), label left / box right, a click on the label toggles", [] {
        IMGUI_CHECKVERSION();
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui_ImplNull_Init();
        bool v = false, plain = false;
        ImGuiID want = 0, got = 0, want_plain = 0;
        ImVec2 rmin, rmax;
        float frame_h = 0.0f, label_w = 0.0f;
        auto frame = [&] {
            ImGui_ImplNull_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(500, 300));
            ImGui::Begin("##uxtest", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
            form_label_width(150.0f);
            want = ImGui::GetID("Retiring");  // what ImGui::Checkbox("Retiring") hashes
            label_w = ImGui::CalcTextSize("Retiring").x;
            frame_h = ImGui::GetFrameHeight();
            checkbox_right("Retiring", &v);
            got = ImGui::GetItemID();
            rmin = ImGui::GetItemRectMin();
            rmax = ImGui::GetItemRectMax();
            // the stock widget under another ID, for the geometry comparison
            ImGui::Checkbox("Retiring##plain", &plain);
            want_plain = ImGui::GetItemID();
            form_label_width(0.0f);
            ImGui::End();
            ImGui::Render();
            ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        };
        frame();
        CHECK(got != 0 && got == want, fmt("same ID as ImGui::Checkbox(label): %u vs %u", got, want));
        CHECK(got != want_plain, "a different label is a different ID (the hash is of the label)");
        CHECK(rmax.x - rmin.x >= 150.0f && rmax.x - rmin.x >= label_w + frame_h, fmt("row spans label column + box: %.0f px", rmax.x - rmin.x));
        // a click on the LABEL (left part of the row) toggles: the whole row is the target
        io.AddMousePosEvent(rmin.x + 4.0f, (rmin.y + rmax.y) * 0.5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        CHECK(v, "click on the label toggled it on");
        // and a click on the BOX (right end) toggles back
        io.AddMousePosEvent(rmax.x - frame_h * 0.5f, (rmin.y + rmax.y) * 0.5f);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        CHECK(!v, "click on the box toggled it off");
        ImGui_ImplNull_Shutdown();
        ImGui::DestroyContext(ctx);
    });
}
