// Native tests (2026-10-19, brief 2.20-IA-pages, track "profile"): Players > Edit player > Profile and Positions, attributes & PlayStyles
// (ui/ui_player_profile.cpp). Included by test_main.cpp after test_page_player.h; profile_page_test::ui_cases runs inside the UI block, a real App.
// The pages are not registered with the shell yet (the wiring owner does that), so each is drawn directly through the Ui hook g_ui_extra (a
// separate window "##profilepage_test" after the app's own frame), as the Player page test does.
#pragma once
#include "ui/ui_names.h"
#include "ui/ui_player_profile.h"
#include "ui/ui_shell.h"

namespace profile_page_test {

using namespace turbo;

enum class Which { Profile, Ability, ProfileEditor, AbilityEditor };
static Which g_which = Which::Profile;

static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();  // a modal keeps the focus it needs
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##profilepage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        switch (g_which) {
            case Which::Profile: draw_player_profile(app); break;
            case Which::Ability: draw_player_ability(app); break;
            case Which::ProfileEditor: draw_player_profile_editor(app); break;
            case Which::AbilityEditor: draw_player_ability_editor(app); break;
        }
    }
    ImGui::End();
}
static void show(Ui& ui, Which w) {
    g_which = w;
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_ui_extra = nullptr;
    ui.frames(2);
}
// 3.0.1: a widget that is on the page but out of view (the 3.0 Ability page is taller than the window: the Player type & tier card and the Roles tile grids
// come before Attributes and PlayStyles, ui_player_profile.cpp:370-388) is reached by scrolling, as a user does. The editor window keeps its scroll between
// cases, so a widget can be above the view as well as below it. The scroll is set on the window that holds the widget (the wheel can be taken by a child
// window under the mouse). A widget that exists once per field set (every PlayStyle set has its own All and bit boxes) is reached at its topmost copy, the
// first set (trait1); `scope` is the PushID string of a widget that sits directly under the field's PushID (the All / None buttons).
// null when the widget is not on the page at all.
static const ItemRec* reach(Ui& ui, const char* id, const char* W, const char* scope = "") {
    for (int step = 0; step < 4; ++step) {
        const ItemRec* top = nullptr;  // the topmost copy on the page, in view or not
        for (const auto& kv : g_items) {
            const ItemRec& r = kv.second;
            if (r.frame != g_frame || !label_matches(r, id) || r.rect.GetWidth() <= 0 || r.rect.GetHeight() <= 0) continue;
            if (r.window.find(W) == std::string::npos) continue;
            if (scope[0] && ImHashStr(scope, 0, r.seed2) != r.seed) continue;
            if (!top || r.rect.Min.y < top->rect.Min.y) top = &r;  // as Ui::find: the first PlayStyle set (trait1) is above the other three
        }
        if (!top) return nullptr;
        if (top->clip.Contains(top->rect.GetCenter())) return ui.find(id, W, scope);  // in view: find answers with the topmost copy in view, which is this one
        ImGuiWindow* w = ImGui::FindWindowByName(top->window.c_str());
        if (!w) return nullptr;
        ImGui::SetScrollY(w, w->Scroll.y + (top->rect.GetCenter().y - w->InnerClipRect.GetCenter().y));
        ui.frames(3);
    }
    return nullptr;
}
// The user scrolls back to the top of the editor (its header with Undo, and the Player type & tier card with Overall +1 / -1, are there)
static void to_top(Ui& ui) {
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows)
        if (w->Name && std::strstr(w->Name, "##profilepage_test/##editor_")) ImGui::SetScrollY(w, 0.0f);
    ui.frames(3);
}
// Open or close a section by its stable id (a collapsing header toggles on a click)
static void toggle(Ui& ui, const char* id, const char* W) {
    ui.click(id, W);
    ui.frames(2);
}

static int g_stub_calls = 0;
static int64_t g_stub_player = 0;
static void stub_bar(App&, const Table&, const PlayerRow& p) {
    ++g_stub_calls;
    g_stub_player = p.playerid;
    ImGui::TextUnformatted("stub player-type editor");
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    const char* W = "##profilepage_test";
    const int64_t saved_sel = app.sel_player;
    // the real build hooks the full player-type editor up (ui_players.cpp); the cases that need the smaller stand-in unhook it and put it back
    const ArchetypeBarFn real_bar = g_ability_archetype_bar;

    run_case("ProfilePage: the Team > Players link defaults to the squad page the app has today, and the hook starts empty", [&] {
        CHECK(profile_team_players_page() == shell::Page::TeamPlayers, "link target");
        CHECK(g_ability_archetype_bar != nullptr, "the full player-type editor is hooked up");
    });

    run_case("ProfilePage: list, search, sections, names, commentary name, details, club summary read only", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.sel_player = 1001;
        show(ui, Which::Profile);
        // 2.22 (G-ia, CHANGE h): the list is the Classic editor's: search and the filter grid. Turbo X (DESIGN_SPEC_X.md): the grid is behind
        // the "Filters" fold, the chips are on the search line, the rows are people (the Selectable's label is still the player's ID)
        CHECK(ui.find("##psearch", W) != nullptr, "search box");
        CHECK(ui.find("###pl_chip_mine", W) != nullptr && ui.find("###pl_chip_free", W) != nullptr && ui.find("###pl_chip_gk", W) != nullptr, "the chips");
        CHECK(ui.open_filters(W), "the Filters fold opens");
        CHECK(ui.find("Clear##filters", W) != nullptr, "the filter grid (Clear filters)");
        CHECK(ui.find("##fpos", W) != nullptr && ui.find("##fteam", W) != nullptr && ui.find("##fnat", W) != nullptr, "position, club and nationality filters");
        CHECK(ui.click("###pl_filters", W), "fold the filters again");
        ui.frames(2);
        CHECK(ui.find("##fpos", W) == nullptr, "folded: the grid is out of the way");
        const PlayerProfileState& st = player_profile_state();
        CHECK(st.listed > 3, "the list holds players: " + std::to_string(st.listed));
        CHECK(ui.find("1001", W) != nullptr && ui.find("1005", W) != nullptr, "players listed (the rows' ids are the player IDs)");
        // nobody picked: no editor; the pane's empty state offers My club and Create & clone (Turbo X)
        app.sel_player = 0;
        ui.frames(2);
        CHECK(player_profile_state().playerid == 0, "nobody picked: no editor");
        CHECK(ui.find("###pe_empty_club", W) != nullptr && ui.find("###pe_empty_create", W) != nullptr, "the empty state's two actions");
        CHECK(ui.click("1005", W), "click a row");
        ui.frames(2);
        CHECK(ui.find("###pe_empty_club", W) == nullptr, "picked: the empty state is gone");
        ui.frames(2);
        const PlayerRow* pr = app.model.player(1005);
        CHECK(app.sel_player == 1005 && st.playerid == 1005 && pr != nullptr, "the click picked him");
        if (!pr) {
            hide(ui);
            return;
        }
        CHECK(st.headline.find("overall") != std::string::npos && st.headline.find("1005") == std::string::npos, "headline names no ID: " + st.headline);
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details", "###pf_undo"})
            CHECK(ui.find(id, W) != nullptr, std::string("on the page: ") + id);
        CHECK(names_tab_state().playerid == 1005, "the names editor was drawn for him");
        CHECK(st.callname_shown, "the commentary name editor was drawn");
        // the long sections push the club section out of view (a clipped item is not found): close them while it is looked at
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details"}) toggle(ui, id, W);
        CHECK(ui.find("###pf_club", W) != nullptr, "on the page: ###pf_club");
        // the club summary says the club in words and is not editable here
        CHECK(st.club_line.rfind("Club: ", 0) == 0, "club line: " + st.club_line);
        if (!pr->club_name.empty()) CHECK(st.club_line.find(pr->club_name) != std::string::npos, "his club named: " + st.club_line);
        CHECK(st.team_link_shown && ui.find("Open Team > Players###pf_team", W) != nullptr, "the link to Team > Players");
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details"}) toggle(ui, id, W);  // open them again
        // none of the old panel's furniture
        for (const char* gone : {"Profile", "Names", "Attributes", "PlayStyles", "Appearance", "Miniface", "Contract & Clubs", "Callname", "All fields", "Growth",
                                 "Transfer / Loan...", "Terminate loan", "Repair names"})
            CHECK(ui.find(gone, W) == nullptr, std::string("no old tab or button: ") + gone);
        CHECK(ui.find("##fieldsearch", W) == nullptr && ui.find("##ptabs", W) == nullptr, "no field search and no tab bar");
        // no contract or wage box: they are Team > Players
        CHECK(ui.find("Wage", W) == nullptr && ui.find("Contract valid until", W) == nullptr, "contract and wage are not editable here");
        // search narrows the list
        CHECK(ui.type_into(ui.find("##psearch", W), "zzzz-no-such-player"), "type a search");
        ui.frames(2);
        CHECK(st.listed == 0, "nobody matches");
        CHECK(ui.type_into(ui.find("##psearch", W), ""), "clear the search");
        ui.frames(2);
        CHECK(st.listed > 3, "everybody again");
        hide(ui);
    });

    run_case("ProfilePage: Open Team > Players goes to the page set for it; the editor alone draws no list", [&] {
        app.sel_player = 1002;
        app.go(shell::Page::PlayerEdit);
        ui.frames(2);
        show(ui, Which::Profile);
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details"}) toggle(ui, id, W);  // close the long sections: the link is in view
        CHECK(ui.find("Open Team > Players###pf_team", W) != nullptr, "the link is on screen");
        CHECK(ui.click("Open Team > Players###pf_team", W), "click the link");
        ui.frames(3);
        CHECK(app.place().page == profile_team_players_page(), "went to the Team > Players page");
        app.go(shell::Page::PlayerEdit);
        ui.frames(2);
        // the editor alone: the same sections, no list, no search (it is its own scroll area with its own open/closed sections:
        // close the three long ones so the last one is in view)
        show(ui, Which::ProfileEditor);
        CHECK(ui.find("##psearch", W) == nullptr && ui.find("1001", W) == nullptr, "no list in the editor alone");
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details"}) toggle(ui, id, W);
        CHECK(ui.find("###pf_names", W) != nullptr && ui.find("###pf_club", W) != nullptr, "sections of the editor alone");
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details"}) toggle(ui, id, W);  // open them again
        CHECK(player_profile_state().playerid == 1002, "for the picked player");
        hide(ui);
    });

    run_case("AbilityPage: overall, positions, style, roles, attributes and PlayStyles are all there; no old tab", [&] {
        app.sel_player = 1001;
        show(ui, Which::Ability);
        const PlayerAbilityState& st = player_ability_state();
        CHECK(st.playerid == 1001 && st.listed > 3, "the list and the editor");
        CHECK(ui.find("##psearch", W) != nullptr, "the list");
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style", "###ab_attrs", "###ab_undo"})
            CHECK(reach(ui, id, W) != nullptr, std::string("section: ") + id);
        const Table* t = app.db.table("players");
        CHECK(t != nullptr, "players table");
        if (!t) {
            hide(ui);
            return;
        }
        int pos_fields = 0;
        for (int k = 1; k <= 7; ++k) pos_fields += t->field("preferredposition" + std::to_string(k)) != nullptr;
        CHECK(st.position_rows == pos_fields && pos_fields >= 1, "position boxes: " + std::to_string(st.position_rows) + " of " + std::to_string(pos_fields));
        int attr_fields = 0;
        for (int i = 0; i < overall::kAttrCount; ++i) attr_fields += t->has(overall::kAttrNames[i]);
        CHECK(st.attribute_sliders == attr_fields && attr_fields > 0, "attribute sliders: " + std::to_string(st.attribute_sliders) + " of " + std::to_string(attr_fields));
        int bit_boxes = 0;
        for (const char* f : {"trait1", "icontrait1", "trait2", "icontrait2"})
            if (const Field* fl = t->field(f)) bit_boxes += std::min(fl->depth, 62);
        CHECK(st.playstyle_boxes == bit_boxes, "PlayStyle boxes: " + std::to_string(st.playstyle_boxes) + " of " + std::to_string(bit_boxes));
        CHECK(st.archetype_bar_external && !st.stand_in_shown, "hooked: the full player-type editor, no stand-in");
        // the attribute sliders push the PlayStyle section out of view: close the attributes while it is looked at
        toggle(ui, "###ab_attrs", W);
        CHECK(reach(ui, "###ab_styles", W) != nullptr, "section: ###ab_styles");
        toggle(ui, "###ab_attrs", W);
        CHECK(st.headline.find("1001") == std::string::npos, "the headline names no ID");
        for (const char* gone : {"Profile", "Names", "Attributes", "PlayStyles", "Appearance", "Miniface", "Contract & Clubs", "Callname", "All fields", "Growth"})
            CHECK(ui.find(gone, W) == nullptr, std::string("no old tab: ") + gone);
        CHECK(ui.find("##ptabs", W) == nullptr && ui.find("##fieldsearch", W) == nullptr, "no tab bar, no field search");
        hide(ui);
    });

    run_case("AbilityPage: Overall +1 goes through the shared overall write and both Undo buttons take it back", [&] {
        app.sel_player = 1001;
        g_ability_archetype_bar = nullptr;  // the smaller stand-in has the +1 / -1 buttons
        show(ui, Which::Ability);
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style"}) toggle(ui, id, W);  // the attributes come into view
        to_top(ui);  // 3.0.1: Overall +1 / -1 sit in the Player type & tier card at the top of the page; the editor window kept the previous case's scroll
        const Table* t = app.db.table("players");
        const PlayerRow* pr = app.model.player(1001);
        CHECK(t && pr, "player 1001");
        if (!t || !pr) {
            g_ability_archetype_bar = real_bar;
            hide(ui);
            return;
        }
        const int64_t before = app.db.get_int(*t, pr->rec, "overallrating", 0);
        const size_t undo0 = app.undo_count(1001);
        CHECK(ui.find("Overall +1###ab_ovr_up", W) != nullptr && ui.find("Overall -1###ab_ovr_down", W) != nullptr, "Overall +1 / -1");
        CHECK(ui.click("Overall +1###ab_ovr_up", W), "click +1");
        ui.frames(2);
        const int64_t after = app.db.get_int(*t, pr->rec, "overallrating", 0);
        CHECK(after == before + 1, "overall " + std::to_string(before) + " -> " + std::to_string(after));
        CHECK(app.undo_count(1001) > undo0, "an undo step was kept");
        // the big-change Undo undoes the whole move (the attributes with it)
        CHECK(ui.find("###ab_bulkundo", W) != nullptr, "Undo last big change");
        CHECK(ui.click("###ab_bulkundo", W), "click Undo last big change");
        ui.frames(2);
        CHECK(app.db.get_int(*t, pr->rec, "overallrating", 0) == before, "the big-change Undo put the overall back");
        // the single-field Undo of the header
        CHECK(ui.find("###ab_undo", W) != nullptr, "the Undo button is on the page");
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style"}) toggle(ui, id, W);  // open them again
        g_ability_archetype_bar = real_bar;
        hide(ui);
    });

    run_case("AbilityPage: the player-type editor hook replaces the stand-in and gets the picked player", [&] {
        app.sel_player = 1003;
        g_stub_calls = 0;
        g_stub_player = 0;
        g_ability_archetype_bar = stub_bar;
        show(ui, Which::AbilityEditor);
        const PlayerAbilityState& st = player_ability_state();
        CHECK(g_stub_calls >= 1 && g_stub_player == 1003, "the hook was called for the picked player: " + std::to_string(g_stub_calls));
        CHECK(st.archetype_bar_external && !st.stand_in_shown, "the full editor, no stand-in");
        CHECK(ui.find("###ab_bulkundo", W) == nullptr && ui.find("Overall +1###ab_ovr_up", W) == nullptr, "the stand-in's buttons are gone");
        CHECK(ui.find("##psearch", W) == nullptr, "the editor alone draws no list");
        g_ability_archetype_bar = nullptr;
        ui.frames(2);
        CHECK(player_ability_state().stand_in_shown, "unhooked again: the stand-in");
        g_ability_archetype_bar = real_bar;
        hide(ui);
    });

    run_case("AbilityPage: a PlayStyle box writes the field and the header Undo puts it back", [&] {
        app.sel_player = 1002;
        show(ui, Which::Ability);
        const Table* t = app.db.table("players");
        const PlayerRow* pr = app.model.player(1002);
        CHECK(t && pr, "player 1002");
        const Field* f = t ? t->field("trait1") : nullptr;
        if (!t || !pr || !f) {
            hide(ui);
            return;
        }
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style", "###ab_attrs"}) toggle(ui, id, W);  // PlayStyles come into view
        const int64_t before = app.db.get_int(*t, pr->rec, "trait1", 0);
        const size_t undo0 = app.undo_count(1002);
        const ItemRec* box = reach(ui, "###ab_bit0", W);  // the topmost copy: the first of the four PlayStyle sets, trait1 (ui_player_profile.cpp:402)
        CHECK(box != nullptr, "the first PlayStyle box");
        CHECK(ui.click(box), "click it");
        ui.frames(2);
        const int64_t after = app.db.get_int(*t, pr->rec, "trait1", 0);
        CHECK(after != before && ((after ^ before) & 1) == 1, "bit 0 flipped: " + std::to_string(before) + " -> " + std::to_string(after));
        CHECK(app.undo_count(1002) > undo0, "an undo step was kept");
        to_top(ui);  // the header's Undo is above the PlayStyles
        CHECK(ui.click("###ab_undo", W), "click Undo");
        ui.frames(2);
        CHECK(app.db.get_int(*t, pr->rec, "trait1", 0) == before, "Undo put the field back");
        // All / None write the whole field
        const ItemRec* all = reach(ui, "###ab_all", W, "trait1");
        CHECK(all != nullptr, "All");
        if (all && ui.click(all)) {
            ui.frames(2);
            CHECK(app.db.get_int(*t, pr->rec, "trait1", 0) == f->max(), "All sets every bit");
            to_top(ui);  // All scrolled the page down; the header's Undo is at the top
            CHECK(ui.click("###ab_undo", W), "click Undo again");
            ui.frames(2);
            CHECK(app.db.get_int(*t, pr->rec, "trait1", 0) == before, "Undo put the field back again");
        }
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style", "###ab_attrs"}) toggle(ui, id, W);  // open them again
        hide(ui);
    });

    app.sel_player = saved_sel;
    app.go(shell::Page::PlayerEdit);
    app.lua_queue.clear();
}

}  // namespace profile_page_test
