// Native tests (4.0 GUI overhaul, slice S6): Players > Edit a player > Positions, attributes & playstyles (ui/ui_player_profile.cpp,
// draw_player_ability and the helpers only it calls).
//
// What is guarded (all of it read from the text ImGui drew, not from a trace the page keeps for tests):
//   - the tab draws the Player type & tier card FIRST, then Overall, Positions, Style and roles, Attributes, PlayStyles: every heading is drawn (text log,
//     read at its section), card < Overall < Positions in the text at the top of the tab, and the full six-section order is the screen order of the
//     six section headers in the item registry (each must be present; none is skipped);
//   - a coded field shows its descriptor in words: the "Plays as: ST, also RCB (internal, plays as CB)" line, one "Ticked: ..." line per PlayStyle / Trait set
//     (a box past the name list reads "Unnamed trait N": not reachable in the fixture, whose trait fields are as wide as the name lists), and the Roles line
//     (only when the table has role fields, which the fixture world, gui_world.lua, does not: named as untested here in the S6 report);
//     each set's "Ticked: ..." line is matched exactly, read right under its own heading line;
//   - each of the four set headings (PlayStyles, PlayStyles+, Traits, Traits+) shows its own help with the mouse on the heading, and no help
//     says "Stored as" or names a field (trait1, icontrait1, trait2, icontrait2);
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
        // the full order of the six sections on the page, from the item registry (every section header is listed in every frame, in view or not):
        // each one present (never skipped) and each one below the one before it
        {
            bool found[6] = {false, false, false, false, false, false};
            float y[6] = {0, 0, 0, 0, 0, 0};
            for (int i = 0; i < 6; ++i)
                for (const auto& kv : g_items)
                    if (kv.second.frame == g_frame && label_matches(kv.second, ids[i]) && kv.second.window.find(W) != std::string::npos) {
                        found[i] = true;
                        y[i] = kv.second.rect.Min.y;
                    }
            std::string got;
            for (int i = 0; i < 6; ++i) got += std::string(" ") + ids[i] + (found[i] ? " y " + std::to_string(int(y[i])) : std::string(" MISSING"));
            for (int i = 0; i < 6; ++i) CHECK(found[i], std::string("the section header ") + ids[i] + " is on the page:" + got);
            bool ordered = true;
            for (int i = 1; i < 6; ++i) ordered = ordered && found[i - 1] && found[i] && y[i - 1] < y[i];
            CHECK(ordered, "order on the page: card, Overall, Positions, Style and roles, Attributes, PlayStyles:" + got);
        }
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
            // The heading (a separator text) has no item id, so the registry does not list it; the first tick box below it (###ab_bit0) is the one
            // item of that set with a stable id. The tab is at its top, where the box and the heading above it are in view, and the mouse rests at
            // every height of the editor window down to that box, as a user moving down the page would, until the help shows. (The registry's clip
            // of a table item is the table's own, so the window's inner rectangle is used for the top.)
            profile_page_test::to_top(ui);
            const ItemRec* box = profile_page_test::reach(ui, "###ab_bit0", W);
            // the window the box was drawn in (its own name from the registry), else an editor window that is on screen
            ImGuiWindow* ew = box ? ImGui::FindWindowByName(box->window.c_str()) : nullptr;
            if (ew == nullptr)
                for (ImGuiWindow* w : GImGui->Windows)
                    if (!ew && w->Active && w->Name && std::strstr(w->Name, "##profilepage_test/##editor_")) ew = w;
            // The help must show on the heading itself: below the "PlayStyles and traits" section header (an item with an id, the one IsItemHovered
            // would answer for if the heading were gone) and above the first tick box.
            float sec_bottom = -1.0f, box_top = -1.0f, hit_y = -1.0f;
            for (const auto& kv : g_items)
                if (kv.second.frame == g_frame && label_matches(kv.second, "###ab_styles") && kv.second.window.find(W) != std::string::npos)
                    sec_bottom = kv.second.rect.Max.y;
            if (box && ew) {
                found = true;
                box_top = box->rect.Min.y;
                const float x = box->rect.Min.x + 24.0f, top = ew->InnerClipRect.Min.y + 2.0f;
                int tries = 0, tip_windows = 0;
                float first_tip_y = -1.0f;
                for (float y = top; y < box_top && tries < 600 && !has(tip, "Special skills shown on his card; tick the ones he has"); y += 3.0f, ++tries) {
                    ImGui::GetIO().AddMousePosEvent(x, y);
                    ui.frames(2);
                    tip = page_log();
                    if (hit_y < 0.0f && has(tip, "Special skills shown on his card; tick the ones he has")) hit_y = y;
                    for (ImGuiWindow* w : GImGui->Windows)
                        if (w->Active && w->Name && std::strncmp(w->Name, "##Tooltip_", 10) == 0) {
                            if (first_tip_y < 0.0f) first_tip_y = y;
                            ++tip_windows;
                        }
                }
                diag += " [swept " + std::to_string(tries) + " heights from y " + std::to_string(int(top)) + " at x " + std::to_string(int(x)) +
                        " to the first tick box at y " + std::to_string(int(box->rect.Min.y)) + "; window '" + std::string(ew->Name) + "' (box drawn in '" +
                        box->window + "') inner " + std::to_string(int(ew->InnerClipRect.Min.y)) + ".." + std::to_string(int(ew->InnerClipRect.Max.y)) + " scroll " +
                        std::to_string(int(ew->Scroll.y)) + "/" + std::to_string(int(ew->ScrollMax.y)) + "; tooltip windows seen " + std::to_string(tip_windows) +
                        " first at y " + std::to_string(int(first_tip_y)) + "; hovered window '" +
                        std::string(GImGui->HoveredWindow ? GImGui->HoveredWindow->Name : "none") + "' active id " + std::to_string(GImGui->ActiveId) +
                        " nav cursor " + std::to_string(GImGui->NavCursorVisible) + "; items labelled PlayStyles at y";
                for (const auto& kv : g_items)
                    if (kv.second.frame == g_frame && kv.second.label.find("PlayStyles") != std::string::npos)
                        diag += " " + std::to_string(int(kv.second.rect.Min.y)) + "('" + kv.second.label + "' in " + kv.second.window + ")";
                diag += "]";
            }
            CHECK(found, "the PlayStyles heading is on the page");
            CHECK(has(tip, "Special skills shown on his card; tick the ones he has"), "the heading's help is a tooltip in words:" + diag);
            CHECK(sec_bottom > 0.0f && hit_y > sec_bottom && hit_y < box_top,
                  "the help shows with the mouse on the PlayStyles heading (shown at y " + std::to_string(int(hit_y)) + ", section header bottom y " +
                      std::to_string(int(sec_bottom)) + ", first tick box y " + std::to_string(int(box_top)) + ")");
            CHECK(!has(tip, "Stored as") && !has(tip, "trait1"), "the tooltip names no field");
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            ui.frames(2);
        }

        // Each of the four sets read where a user sees it (review 10-26 P2: the fd3ec1e9 test checked all four helps and the exact words of each set):
        // the set's heading in view, its "Ticked: ..." line matched EXACTLY (the line right under the heading line, so "none ticked" is tied to
        // Traits+), and its help with the mouse on the heading itself: the set's own words, no "Stored as", no field name. The heading (a separator
        // text, no item id) lies between the item above it (the section header for the first set, the None button of the set above for the others,
        // ui_player_profile.cpp playstyle_bits) and the set's first tick box.
        const auto lines_of = [](const std::string& text) {
            std::vector<std::string> out;
            for (size_t at = 0; at <= text.size();) {
                size_t nl = text.find('\n', at);
                if (nl == std::string::npos) nl = text.size();
                const std::string l = text.substr(at, nl - at);
                const size_t b = l.find_first_not_of(" \t\r"), e = l.find_last_not_of(" \t\r");
                out.push_back(b == std::string::npos ? std::string() : l.substr(b, e - b + 1));
                at = nl + 1;
            }
            return out;
        };
        // the words of the "Ticked: " line right under the heading line of `title` (ImGui logs a separator text as "--- <title>",
        // S6_FIX2\try2_pass\f2_pass.log)
        const auto ticked_under = [&](const std::string& text, const std::string& title) -> std::string {
            const std::vector<std::string> ls = lines_of(text);
            for (size_t i = 0; i < ls.size(); ++i)
                if (ls[i] == "--- " + title) return i + 1 < ls.size() && ls[i + 1].compare(0, 8, "Ticked: ") == 0 ? ls[i + 1].substr(8) : "<no Ticked line right under the heading>";
            return "<no heading line '" + title + "'>";
        };
        // the text logged between the heading line of `title` and the next "Ticked: " line: with the mouse on the heading, the tooltip's own lines
        // (empty when nothing is logged there)
        const auto help_under = [&](const std::string& text, const std::string& title) -> std::string {
            const std::vector<std::string> ls = lines_of(text);
            for (size_t i = 0; i < ls.size(); ++i) {
                if (ls[i] != "--- " + title) continue;
                std::string out;
                for (size_t j = i + 1; j < ls.size() && ls[j].compare(0, 8, "Ticked: ") != 0; ++j)
                    if (!ls[j].empty()) out += (out.empty() ? "" : "\n") + ls[j];
                return out;
            }
            return "";
        };
        struct SetView {
            bool anchors = false, in_view = false;
            std::string base, tip, diag;
            float upper = -1.0f, box_top = -1.0f, hit_y = -1.0f;
        };
        const auto set_view = [&](const char* prev_field) -> SetView {
            SetView v;
            ImGui::ClearActiveID();
            if (ImGuiWindow* pw = ImGui::FindWindowByName(W)) ImGui::FocusWindow(pw);
            GImGui->NavCursorVisible = false;
            GImGui->NavHighlightItemUnderNav = false;
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            ui.frames(2);
            const ItemRec* up = prev_field ? profile_page_test::reach(ui, "###ab_none", W, prev_field) : profile_page_test::reach(ui, "###ab_styles", W);
            if (up == nullptr) {
                v.diag = std::string(" [no item above the heading: ") + (prev_field ? std::string("###ab_none of ") + prev_field : std::string("###ab_styles")) + "]";
                return v;
            }
            v.upper = up->rect.Max.y;
            const std::string win = up->window;
            float x = -1.0f;
            for (const auto& kv : g_items) {
                const ItemRec& r = kv.second;
                if (r.frame != g_frame || !label_matches(r, "###ab_bit0") || r.window.find(W) == std::string::npos) continue;
                if (r.rect.Min.y > v.upper && (v.box_top < 0.0f || r.rect.Min.y < v.box_top)) {
                    v.box_top = r.rect.Min.y;
                    x = r.rect.Min.x + 24.0f;
                }
            }
            ImGuiWindow* ew = ImGui::FindWindowByName(win.c_str());
            v.anchors = v.box_top > 0.0f && ew != nullptr;
            v.diag = " [item above bottom y " + std::to_string(int(v.upper)) + ", first tick box y " + std::to_string(int(v.box_top)) + ", window '" + win + "'";
            if (!v.anchors) {
                v.diag += "]";
                return v;
            }
            v.in_view = ew->InnerClipRect.Min.y <= v.upper && v.box_top <= ew->InnerClipRect.Max.y;
            v.diag += " inner " + std::to_string(int(ew->InnerClipRect.Min.y)) + ".." + std::to_string(int(ew->InnerClipRect.Max.y)) + "]";
            v.base = page_log();  // the mouse is away: no tooltip
            for (float y = v.upper + 1.0f; v.in_view && y < v.box_top; y += 2.0f) {
                ImGui::GetIO().AddMousePosEvent(x, y);
                ui.frames(2);
                const std::string log = page_log();
                bool tip_up = false;
                for (ImGuiWindow* w : GImGui->Windows)
                    if (w->Active && w->Name && std::strncmp(w->Name, "##Tooltip_", 10) == 0) tip_up = true;
                if (tip_up) {  // the first height at which a tooltip shows
                    v.hit_y = y;
                    v.tip = log;
                    break;
                }
            }
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            ui.frames(2);
            return v;
        };
        {
            struct SetCase { const char* title; const char* prev; const char* help; const char* words; };
            const SetCase sets[] = {
                {"PlayStyles", nullptr, "Special skills shown on his card; tick the ones he has", "Finesse Shot, Rapid"},
                {"PlayStyles+", "trait1", "The stronger version of a PlayStyle; tick the ones he has", "Power Shot"},
                {"Traits", "icontrait1", "Goalkeeper skills, CPU AI habits and others such as Leadership or Injury Prone", "Super Sub"},
                {"Traits+", "trait2", "The stronger version of a Traits box; tick the ones he has", "none ticked"},
            };
            for (const SetCase& c : sets) {
                const SetView v = set_view(c.prev);
                const std::string title = c.title;
                CHECK(v.anchors && v.in_view, "the " + title + " heading is in view between its neighbours:" + v.diag);
                CHECK(ticked_under(v.base, title) == c.words,
                      title + " in words, exactly '" + c.words + "': '" + ticked_under(v.base, title) + "' (text: ..." + tail(v.base) + ")");
                CHECK(v.hit_y > v.upper && v.hit_y < v.box_top,
                      "a help shows with the mouse on the " + title + " heading (shown at y " + std::to_string(int(v.hit_y)) + ")" + v.diag);
                CHECK(has(v.tip, c.help), "the " + title + " heading's help is its own, in words (text: ..." + tail(v.tip) + ")");
                // the help's own text: the lines the tooltip adds between the heading line and its Ticked line (the page itself says "PlayStyles and
                // traits", so the whole-frame text cannot carry the old check). As strict as the old fd3ec1e9 help check (test_ability_40.h:92): not
                // empty, no "trait" at all (covers trait1, icontrait1, trait2, icontrait2), no "Stored as".
                const std::string own = help_under(v.tip, title);
                CHECK(help_under(v.base, title).empty(), "with the mouse away nothing is logged between the " + title + " heading and its Ticked line: '" +
                                                             help_under(v.base, title) + "'");
                CHECK(!own.empty() && has(own, c.help) && !has(own, "trait") && !has(own, "Stored as"),
                      "the " + title + " help, read under its heading, is its own and names no field: '" + own + "'");
                CHECK(!v.tip.empty() && !has(v.tip, "Stored as") && !has(v.tip, "trait1") && !has(v.tip, "trait2"),
                      "the " + title + " help names no field (text: ..." + tail(v.tip) + ")");
                CHECK(!v.base.empty() && !has(v.base, "Stored as") && !has(v.base, "trait1") && !has(v.base, "trait2"),
                      "the page around the " + title + " heading names no field (text: ..." + tail(v.base) + ")");
            }
        }

        // the descriptor follows an edit by the next frame (a ticked box, in words)
        const Field* f1 = t->field("trait1");
        if (f1) {
            app.edit(*t, pr->rec, *f1, Value::of_int(int64_t(1) << 4));
            ui.frames(2);
            const std::string after = text_at("###ab_bit0");
            CHECK(has(after, "Ticked: Precision Header") && !has(after, "Ticked: Finesse Shot"), "after an edit: ..." + tail(after));
            const SetView v = set_view(nullptr);
            CHECK(ticked_under(v.base, "PlayStyles") == "Precision Header",
                  "after an edit, PlayStyles in words, exactly 'Precision Header': '" + ticked_under(v.base, "PlayStyles") + "'" + v.diag);
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
