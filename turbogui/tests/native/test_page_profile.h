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

    run_case("ProfilePage: the Team > Players link defaults to the squad page the app has today, and the hook starts empty", [&] {
        CHECK(profile_team_players_page() == shell::Page::Squad, "link target");
        CHECK(g_ability_archetype_bar == nullptr, "no player-type editor hooked up in the test build");
    });

    run_case("ProfilePage: list, search, sections, names, commentary name, details, club summary read only", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.sel_player = 1001;
        show(ui, Which::Profile);
        CHECK(ui.find("##pfsearch", W) != nullptr, "search box");
        CHECK(ui.find("Free agents###pf_chip_free", W) != nullptr, "chip: free agents");
        CHECK(ui.find("##pfpos", W) != nullptr, "position filter");
        const PlayerProfileState& st = player_profile_state();
        CHECK(st.listed > 3, "the list holds players: " + std::to_string(st.listed));
        CHECK(ui.find("ID", W) == nullptr && ui.find("Player ID", W) == nullptr, "no ID column or ID label on the page");
        CHECK(ui.find("###pf1001", W) != nullptr && ui.find("###pf1005", W) != nullptr, "players listed by name");
        // nobody picked: no editor
        app.sel_player = 0;
        ui.frames(2);
        CHECK(player_profile_state().playerid == 0, "nobody picked: no editor");
        CHECK(ui.click("###pf1005", W), "click a row");
        ui.frames(2);
        const PlayerRow* pr = app.model.player(1005);
        CHECK(app.sel_player == 1005 && st.playerid == 1005 && pr != nullptr, "the click picked him");
        if (!pr) {
            hide(ui);
            return;
        }
        CHECK(st.headline.find("overall") != std::string::npos && st.headline.find("1005") == std::string::npos, "headline names no ID: " + st.headline);
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details", "###pf_club", "###pf_undo"})
            CHECK(ui.find(id, W) != nullptr, std::string("on the page: ") + id);
        CHECK(names_tab_state().playerid == 1005, "the names editor was drawn for him");
        CHECK(st.callname_shown, "the commentary name editor was drawn");
        // the club summary says the club in words and is not editable here
        CHECK(st.club_line.rfind("Club: ", 0) == 0, "club line: " + st.club_line);
        if (!pr->club_name.empty()) CHECK(st.club_line.find(pr->club_name) != std::string::npos, "his club named: " + st.club_line);
        CHECK(st.team_link_shown && ui.find("Open Team > Players###pf_team", W) != nullptr, "the link to Team > Players");
        // none of the old panel's furniture
        for (const char* gone : {"Profile", "Names", "Attributes", "PlayStyles", "Appearance", "Miniface", "Contract & Clubs", "Callname", "All fields", "Growth",
                                 "Transfer / Loan...", "Terminate loan", "Repair names"})
            CHECK(ui.find(gone, W) == nullptr, std::string("no old tab or button: ") + gone);
        CHECK(ui.find("##fieldsearch", W) == nullptr && ui.find("##ptabs", W) == nullptr, "no field search and no tab bar");
        // no contract or wage box: they are Team > Players
        CHECK(ui.find("Wage", W) == nullptr && ui.find("Contract valid until", W) == nullptr, "contract and wage are not editable here");
        // search narrows the list
        CHECK(ui.type_into(ui.find("##pfsearch", W), "zzzz-no-such-player"), "type a search");
        ui.frames(2);
        CHECK(st.listed == 0, "nobody matches");
        CHECK(ui.type_into(ui.find("##pfsearch", W), ""), "clear the search");
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
        for (const char* id : {"###pf_names", "###pf_callname", "###pf_details"}) toggle(ui, id, W);  // open them again
        app.go(shell::Page::PlayerEdit);
        ui.frames(2);
        // the editor alone: the same sections, no list, no search
        show(ui, Which::ProfileEditor);
        CHECK(ui.find("##pfsearch", W) == nullptr && ui.find("###pf1001", W) == nullptr, "no list in the editor alone");
        CHECK(ui.find("###pf_names", W) != nullptr && ui.find("###pf_club", W) != nullptr, "sections of the editor alone");
        CHECK(player_profile_state().playerid == 1002, "for the picked player");
        hide(ui);
    });

    run_case("AbilityPage: overall, positions, style, roles, attributes and PlayStyles are all there; no old tab", [&] {
        app.sel_player = 1001;
        show(ui, Which::Ability);
        const PlayerAbilityState& st = player_ability_state();
        CHECK(st.playerid == 1001 && st.listed > 3, "the list and the editor");
        CHECK(ui.find("##pfsearch", W) != nullptr, "the list");
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style", "###ab_attrs", "###ab_styles", "###ab_undo"})
            CHECK(ui.find(id, W) != nullptr, std::string("section: ") + id);
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
        CHECK(st.stand_in_shown && !st.archetype_bar_external, "no hook: the smaller stand-in");
        CHECK(st.headline.find("1001") == std::string::npos, "the headline names no ID");
        for (const char* gone : {"Profile", "Names", "Attributes", "PlayStyles", "Appearance", "Miniface", "Contract & Clubs", "Callname", "All fields", "Growth"})
            CHECK(ui.find(gone, W) == nullptr, std::string("no old tab: ") + gone);
        CHECK(ui.find("##ptabs", W) == nullptr && ui.find("##fieldsearch", W) == nullptr, "no tab bar, no field search");
        hide(ui);
    });

    run_case("AbilityPage: Overall +1 goes through the shared overall write and both Undo buttons take it back", [&] {
        app.sel_player = 1001;
        show(ui, Which::Ability);
        for (const char* id : {"###ab_ovr", "###ab_positions", "###ab_style"}) toggle(ui, id, W);  // the attributes come into view
        const Table* t = app.db.table("players");
        const PlayerRow* pr = app.model.player(1001);
        CHECK(t && pr, "player 1001");
        if (!t || !pr) {
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
        CHECK(ui.find("##pfsearch", W) == nullptr, "the editor alone draws no list");
        g_ability_archetype_bar = nullptr;
        ui.frames(2);
        CHECK(player_ability_state().stand_in_shown, "unhooked again: the stand-in");
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
        const ItemRec* box = ui.find("###ab_bit0", W);
        CHECK(box != nullptr, "the first PlayStyle box");
        CHECK(ui.click(box), "click it");
        ui.frames(2);
        const int64_t after = app.db.get_int(*t, pr->rec, "trait1", 0);
        CHECK(after != before && ((after ^ before) & 1) == 1, "bit 0 flipped: " + std::to_string(before) + " -> " + std::to_string(after));
        CHECK(app.undo_count(1002) > undo0, "an undo step was kept");
        CHECK(ui.click("###ab_undo", W), "click Undo");
        ui.frames(2);
        CHECK(app.db.get_int(*t, pr->rec, "trait1", 0) == before, "Undo put the field back");
        // All / None write the whole field
        const ItemRec* all = ui.find("All###ab_all", W);
        CHECK(all != nullptr, "All");
        if (all && ui.click(all)) {
            ui.frames(2);
            CHECK(app.db.get_int(*t, pr->rec, "trait1", 0) == f->max(), "All sets every bit");
            ui.click("###ab_undo", W);
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
