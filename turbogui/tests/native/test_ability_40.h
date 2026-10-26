// Native tests (4.0 GUI overhaul, slice S6): Players > Edit a player > Positions, attributes & playstyles (ui/ui_player_profile.cpp,
// draw_player_ability and the helpers only it calls).
//
// What is guarded (all of it read from the text ImGui drew, not from a trace the page keeps for tests):
//   - the tab draws the Player type & tier card FIRST, then Overall, Positions, Style and roles, Attributes, PlayStyles (heading order in the frame's text log);
//   - a coded field shows its descriptor in words: the "Plays as: ST, also RCB (internal, plays as CB)" line, one "Ticked: ..." line per PlayStyle / Trait set
//     (a box past the name list reads "Unnamed trait N": not reachable in the fixture, whose trait fields are as wide as the name lists), and the Roles line
//     (only when the table has role fields, which the fixture world, gui_world.lua, does not: named as untested here in the S6 report);
//   - the old stand-in wording is gone ("is added when this page is wired in", "The game works the shown overall", the "Stored as <field>" tooltip);
//   - the two grids of the tab are tables in the registry form (table_registry.h ids "##bits" and "##sl", tbl::kGrid flags).
//
// HOW THIS FILE IS INCLUDED: test_main.cpp holds ONE line for it, `#include "test_ability_40.h"`, INSIDE the UI block (right after the
// profile_page_test::ui_cases call), so the statements below run there with `app`, `ui` and `le` in scope. It is therefore a sequence of
// statements, not a header: no #pragma once, no #include, no namespace and no function definitions (they would land inside a function).
//
// The text of a frame is ImGui's own log (ImGui::LogToBuffer, as logged_frame in test_describe.h and logged_text in test_page_kits.h), taken with the page
// window the profile page test draws through g_ui_extra. Text the window has scrolled out of view may not be drawn, so a line is read after the page has
// been scrolled to it (profile_page_test::reach), as a user does; a tooltip is read with the mouse resting on its heading.
{
    const char* W = "##profilepage_test";
    // one frame of the app and the page with the text log on: every line drawn (and the tooltip of the item under the mouse)
    const auto page_log = [&]() -> std::string {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ++g_frame;
        ImGui::LogToBuffer();
        ui.app.tick(ui.t);
        ui.app.draw();
        if (g_ui_extra) g_ui_extra(ui.app);
        std::string text = GImGui->LogEnabled ? std::string(GImGui->LogBuffer.c_str()) : std::string();
        ImGui::LogFinish();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        ui.t += 1.0 / 60.0;
        return text;
    };
    // the page's text after the page was scrolled so that the widget `id` is in view ("" when the widget is not on the page)
    const auto text_at = [&](const char* id) -> std::string {
        if (profile_page_test::reach(ui, id, W) == nullptr) return "";
        ui.frames(2);
        return page_log();
    };
    const auto has = [](const std::string& text, const std::string& s) { return text.find(s) != std::string::npos; };
    const auto tail = [](const std::string& text) { return text.size() > 1200 ? text.substr(text.size() - 1200) : text; };

    run_case("S6 ability tab: the Player type & tier card is drawn before Positions, and the old stand-in wording is gone", [&] {
        const bool advanced0 = app.prefs.advanced_mode;
        const int64_t sel0 = app.sel_player;
        app.sel_player = 1001;
        app.prefs.advanced_mode = false;
        profile_page_test::show(ui, profile_page_test::Which::Ability);
        // every section heading is drawn (each read after scrolling to its section)
        const char* const heads[] = {"Player type & tier", "Overall and potential", "Positions", "Playing style and roles", "Attributes", "PlayStyles and traits"};
        const char* const ids[] = {"###ab_typetier", "###ab_ovr", "###ab_positions", "###ab_style", "###ab_attrs", "###ab_styles"};
        for (int i = 0; i < 6; ++i) {
            const std::string t = text_at(ids[i]);
            CHECK(has(t, heads[i]), std::string("the heading '") + heads[i] + "' is drawn (text: ..." + tail(t) + ")");
        }
        // at the top of the tab the card's heading comes before Overall, and Overall before Positions
        profile_page_test::to_top(ui);
        const std::string top = page_log();
        const size_t card = top.find("Player type & tier"), ovr = top.find("Overall and potential");
        const size_t pos = ovr == std::string::npos ? std::string::npos : top.find("Positions", ovr);
        CHECK(card != std::string::npos && ovr != std::string::npos && card < ovr, "the card heading is drawn before Overall (text: ..." + tail(top) + ")");
        CHECK(pos == std::string::npos || ovr < pos, "Overall is drawn before Positions");
        CHECK(player_ability_state().archetype_bar_external && player_ability_state().bar_calls == 1, "the player type editor is the card's, drawn once");
        // the Overall line: the new wording, not the old one
        const std::string ovr_text = text_at("###ab_ovr");
        CHECK(has(ovr_text, "Typing an overall moves the attributes that count most for his position."), "the new Overall line is drawn: ..." + tail(ovr_text));
        CHECK(!has(ovr_text, "The game works the shown overall"), "the old Overall line is gone");
        // the stand-in (drawn when the full player-type editor is not hooked up) names no integration step
        const ArchetypeBarFn bar = g_ability_archetype_bar;
        g_ability_archetype_bar = nullptr;
        profile_page_test::show(ui, profile_page_test::Which::Ability);
        profile_page_test::to_top(ui);
        const std::string stand_in = page_log();
        g_ability_archetype_bar = bar;
        CHECK(player_ability_state().stand_in_shown, "the stand-in was drawn (no external editor)");
        CHECK(has(stand_in, "Reshaping to a player type is not available here; Overall +1 / -1 still works."), "the new stand-in line is drawn: ..." + tail(stand_in));
        CHECK(!has(stand_in, "is added when this page is wired in"), "the stand-in line names no integration step");
        profile_page_test::hide(ui);
        app.prefs.advanced_mode = advanced0;
        app.sel_player = sel0;
    });

    run_case("S6 ability tab: coded fields show their descriptors (positions, roles, PlayStyle and Trait boxes)", [&] {
        const bool advanced0 = app.prefs.advanced_mode;
        const int64_t sel0 = app.sel_player;
        app.prefs.advanced_mode = false;
        app.sel_player = 1004;
        const Table* t = app.db.table("players");
        const PlayerRow* pr = app.model.player(1004);
        CHECK(t && pr, "player 1004");
        if (!t || !pr) {
            app.prefs.advanced_mode = advanced0;
            app.sel_player = sel0;
            return;
        }
        const size_t undo0 = app.undo_count(1004);
        const auto put = [&](const char* field, int64_t v) {
            const Field* f = t->field(field);
            if (f) app.edit(*t, pr->rec, *f, Value::of_int(v));
            return f != nullptr;
        };
        // a known state: main position ST, second position RCB (a code no game menu names), roles 42 and 152 (only when the table has the role
        // fields: the GUI fixture world has none), two PlayStyles, one PlayStyle+, Traits bit 13 (Super Sub). A box past the name list
        // ("Unnamed trait N") cannot be built here: the fixture's trait fields are as wide as the name lists.
        const bool has_roles = t->field("role1") != nullptr && t->field("role2") != nullptr;
        const bool set = put("preferredposition1", 25) && put("preferredposition2", 4) && (!has_roles || (put("role1", 42) && put("role2", 152))) &&
                         put("trait1", (int64_t(1) << 0) | (int64_t(1) << 21)) && put("icontrait1", int64_t(1) << 2) && put("trait2", int64_t(1) << 13);
        CHECK(set, "the fixture player's coded fields were set");
        profile_page_test::show(ui, profile_page_test::Which::Ability);

        const std::string pos = text_at("###ab_positions");
        CHECK(has(pos, "Plays as: ST, also RCB (internal, plays as CB)"), "positions in words: ..." + tail(pos));
        const std::string style = text_at("###ab_style");
        if (has_roles)
            CHECK(has(style, "Roles: ") && has(style, labels::describe("players", "role1", 42)) && has(style, "ST Poacher+") && has(style, "LW False Winger++"),
                  "roles in words: ..." + tail(style));
        else
            CHECK(!has(style, "Roles: "), "no role fields in this table: no roles line");

        // the PlayStyle and Trait sets: one "Ticked: ..." line each, above its grid; the first set (PlayStyles, trait1) is read at its first box, the
        // others at the end of the tab
        const std::string s1 = text_at("###ab_bit0");
        CHECK(has(s1, "Ticked: Finesse Shot, Rapid"), "PlayStyles in words: ..." + tail(s1));
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
            if (w->Name && std::strstr(w->Name, "##profilepage_test/##editor_")) ImGui::SetScrollY(w, w->ScrollMax.y);
        ui.frames(3);
        const std::string s_end = page_log();
        CHECK(has(s_end, "Ticked: Power Shot"), "PlayStyles+ in words: ..." + tail(s_end));
        CHECK(has(s_end, "Ticked: Super Sub"), "Traits in words: ..." + tail(s_end));
        CHECK(has(s_end, "Ticked: none ticked"), "a set with nothing ticked says so: ..." + tail(s_end));
        CHECK(!has(s_end, "Unnamed bit"), "no box is called 'Unnamed bit N'");

        // the heading of a set explains it in words on hover; the old tooltip named the stored field
        {
            std::string tip, diag;
            bool found = false;
            // the user has the page in front, nothing is being dragged or typed into (an earlier case may have left the focus elsewhere or the
            // keyboard-navigation cursor on), and the mouse comes from elsewhere on the screen
            ImGui::ClearActiveID();
            if (ImGuiWindow* pw = ImGui::FindWindowByName(W)) ImGui::FocusWindow(pw);
            GImGui->NavCursorVisible = false;
            GImGui->NavHighlightItemUnderNav = false;
            ImGui::GetIO().AddMousePosEvent(4.0f, 4.0f);
            ui.frames(2);
            for (int attempt = 0; attempt < 3 && !has(tip, "Special skills shown on his card; tick the ones he has"); ++attempt) {
                // the heading is found again each time (reach scrolls it into view); the mouse rests on its left end, inside what is visible of it
                const ItemRec* head = profile_page_test::reach(ui, "PlayStyles", W);
                if (!head) break;
                found = true;
                ImRect vis = head->rect;
                vis.ClipWith(head->clip);
                const float x = vis.Min.x + 24.0f, y = vis.GetCenter().y;
                const std::string hlabel = head->label, hwin = head->window;
                ImGui::GetIO().AddMousePosEvent(x, y);
                ui.frames(3);
                tip = page_log();
                diag += " [mouse " + std::to_string(int(x)) + "," + std::to_string(int(y)) + " hovered window '" +
                        std::string(GImGui->HoveredWindow ? GImGui->HoveredWindow->Name : "none") + "' item '" + hlabel + "' in '" + hwin + "' active id " +
                        std::to_string(GImGui->ActiveId) + " nav window '" + std::string(GImGui->NavWindow ? GImGui->NavWindow->Name : "none") +
                        "' nav cursor " + std::to_string(GImGui->NavCursorVisible) + " tooltip windows:";
                for (ImGuiWindow* w : GImGui->Windows)
                    if (w->Name && std::strstr(w->Name, "Tooltip"))
                        diag += std::string(" ") + w->Name + (w->Active ? " active" : " inactive") + (w->Hidden ? " hidden" : "") + (w->SkipItems ? " skipitems" : "");
                diag += "]";
            }
            CHECK(found, "the PlayStyles heading is on the page");
            CHECK(has(tip, "Special skills shown on his card; tick the ones he has"), "the heading's help is a tooltip in words:" + diag);
            CHECK(!has(tip, "Stored as") && !has(tip, "trait1"), "the tooltip names no field");
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            ui.frames(2);
        }

        // the descriptor follows an edit by the next frame (a ticked box, in words)
        const Field* f1 = t->field("trait1");
        if (f1) {
            app.edit(*t, pr->rec, *f1, Value::of_int(int64_t(1) << 4));
            ui.frames(2);
            const std::string after = text_at("###ab_bit0");
            CHECK(has(after, "Ticked: Precision Header") && !has(after, "Ticked: Finesse Shot"), "after an edit: ..." + tail(after));
        }
        profile_page_test::hide(ui);
        while (app.undo_count(1004) > undo0) app.undo(1004);  // the fixture player as it was
        app.prefs.advanced_mode = advanced0;
        app.sel_player = sel0;
    });

    run_case("S6 ability tab: its grids are tables in the registry form (##sl, ##bits) and its helpers have no raw BeginTable", [&] {
        bool sl = false, bits = false;
        for (const tbl::Spec& s : tbl::all()) {
            if (std::string(s.id) == "##sl") sl = (s.flags & tbl::kGrid) == tbl::kGrid;
            if (std::string(s.id) == "##bits") bits = (s.flags & tbl::kGrid) == tbl::kGrid;
        }
        CHECK(sl && bits, "##sl and ##bits are registered with the kGrid flags");
        const fs::path src = fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui" / "ui_player_profile.cpp";
        const std::string text = read_file(src);
        CHECK(!text.empty(), "ui_player_profile.cpp at " + src.string());
        // every BeginTable( of the file names tbl:: in its flags (the registry form), the field_grid tables included (widgets.cpp)
        int calls = 0, raw = 0;
        for (size_t at = text.find("BeginTable("); at != std::string::npos; at = text.find("BeginTable(", at + 1)) {
            ++calls;
            const size_t end = text.find(';', at);
            const std::string call = text.substr(at, end == std::string::npos ? 160 : end - at);
            if (call.find("tbl::") == std::string::npos) ++raw;
        }
        CHECK(calls >= 2 && raw == 0, "BeginTable calls in ui_player_profile.cpp: " + std::to_string(calls) + ", raw: " + std::to_string(raw));
    });
}
