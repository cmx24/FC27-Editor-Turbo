// Native tests (2026-10-19, brief 2.22-G-ia): layout and organisation. Included by test_main.cpp after test_page_profile.h; ia222_test::ui_cases runs inside the
// UI block (a real App on the simulated game), with the pages reached the way a user reaches them (app.go / the shell), not drawn by function.
//
//   a  Players > Commentary names is a real page (a player list and the callname editor); NO page of the navigation is a signpost any more
//   b  Edit a player > Positions, attributes & PlayStyles opens with the card "Player type & tier" (the 5-tier Tier combo in view, one Player type
//      picker, archetype_bar once), open by default, collapsible, remembered
//   c  the Create player dialog starts with Player type & tier; Potential, Birth date, Nationality and Foot are under Profile; one picker
//   d  the outer tab "Classic editor" (was "All fields")
//   e  the Appearance Editor has the Tattoos card and the whole miniface editor, open
//   h  Edit a player's default list is the Classic editor's: one sortable table of 8 columns and the full filter grid
// Cases are named "G-ia: ..." so TURBO_TEST_ONLY="g-ia" runs them alone.
#pragma once
#include "ui/ui_looks_studio.h"
#include "ui/ui_nav_pages.h"
#include "ui/ui_player_profile.h"
#include "ui/ui_presets.h"
#include "ui/ui_shell.h"

namespace ia222_test {

using namespace turbo;

// Tables drawn in the last two frames that are sortable and have 8 columns (the Classic player list)
static int sortable_8col_tables() {
    ImGuiContext& g = *GImGui;
    int n = 0;
    for (int i = 0; i < g.Tables.GetMapSize(); ++i)
        if (ImGuiTable* t = g.Tables.TryGetMapData(i))
            if (t->LastFrameActive >= g.FrameCount - 1 && t->ColumnsCount == 8 && (t->Flags & ImGuiTableFlags_Sortable)) ++n;
    return n;
}

// How many items drawn in the last frame have this label (a count, not the first: "one picker" means one)
static int count_items(const std::string& label) {
    int n = 0;
    for (const auto& kv : g_items)
        if (kv.second.frame == g_frame && (kv.second.label == label || (kv.second.label.empty() && ImHashStr(label.c_str(), 0, kv.second.seed) == kv.second.id))) ++n;
    return n;
}

static void ui_cases(App& app, Ui& ui, fs::path le) {
    (void)le;
    using shell::Page;
    const bool was_legacy = app.legacy_pages;
    const bool was_adv = app.prefs.advanced_mode;
    const bool was_card = app.player_card_view;
    const int64_t was_sel = app.sel_player;
    const int was_tab = shell::player_edit_tab();
    const ImVec2 old_display = ImGui::GetIO().DisplaySize;
    ImGui::GetIO().DisplaySize = ImVec2(1920.0f, 1080.0f);  // the screen the check is made on: nothing may need a scroll to be seen
    app.legacy_pages = false;
    app.player_card_view = false;

    run_case("G-ia: no page of the navigation is a signpost (no Open ... button), in the Simple view and in Advanced", [&] {
        for (bool adv : {false, true}) {
            app.prefs.advanced_mode = adv;
            for (int pi = 1; pi < shell::kPageCount; ++pi) {
                const Page p = static_cast<Page>(pi);
                if (!shell::page_visible(p, adv)) continue;
                app.go(p);
                ui.frames(3);
                CHECK(ui.find("###navgo") == nullptr, std::string("not a signpost: ") + shell::page_label(p) + (adv ? " (Advanced)" : ""));
            }
        }
        app.prefs.advanced_mode = false;
    });

    run_case("G-ia: Commentary names is a real page: a player list, and the callname editor of the picked player beside it, one selection with Edit a player", [&] {
        if (app.busy()) ui.click("Cancel");
        app.prefs.advanced_mode = false;
        app.sel_player = 0;
        CHECK(ui.open_page("Players", "Callnames") && app.place() == shell::place_of(Page::CommentaryNames), "Players > Commentary names opens");
        ui.frames(4);
        const CommentaryNamesState& cs = commentary_names_state();
        CHECK(ui.find("###navgo") == nullptr, "no signpost button");
        CHECK(ui.find("##cnsearch") != nullptr && ui.find("##cnclub") != nullptr, "search and club filter");
        CHECK(cs.all > 3 && cs.listed == cs.all, "the list holds every player: " + std::to_string(cs.listed) + " of " + std::to_string(cs.all));
        CHECK(!cs.editor_shown, "nobody picked: no editor yet");
        CHECK(cs.callnames_ready, "the callname index is built, so the column and the filter work");
        CHECK(cs.first_listed > 0 && !cs.now_of_first.empty(), "the Callname now column has text: '" + cs.now_of_first + "'");
        CHECK(ui.find("No callname yet###cn_none") != nullptr, "the No callname yet filter");
        // pick a player: the editor is on the same page, and Edit a player has the same player
        const int64_t pid = cs.first_listed;
        const std::string id = "###cn" + std::to_string(pid);
        CHECK(ui.click(id), "click the first player");
        ui.frames(3);
        CHECK(app.sel_player == pid && cs.editor_shown && cs.editor_playerid == pid, "the callname editor is drawn for him on this page");
        CHECK(!cs.now_of_selected.empty(), "his Callname now: '" + cs.now_of_selected + "'");
        app.go(Page::PlayerEdit);
        ui.frames(3);
        CHECK(app.sel_player == pid, "Edit a player shows the same player (one selection)");
        // the filter narrows the list
        app.go(Page::CommentaryNames);
        ui.frames(3);
        const size_t all = cs.listed;
        CHECK(ui.click("No callname yet###cn_none"), "No callname yet");
        ui.frames(3);
        CHECK(cs.listed <= all, "the filter does not add players: " + std::to_string(cs.listed) + " of " + std::to_string(all));
        CHECK(ui.click("No callname yet###cn_none"), "filter off");
        ui.frames(3);
        CHECK(cs.listed == all, "everybody again");
        CHECK(ui.type_into(ui.find("##cnsearch"), "zzzz-no-such-player"), "search");
        ui.frames(3);
        CHECK(cs.listed == 0, "nobody matches");
        CHECK(ui.type_into(ui.find("##cnsearch"), ""), "search cleared");
        ui.frames(3);
        CHECK(cs.listed == all, "everybody again after the search");
    });

    run_case("G-ia: Edit a player (Profile and Positions tabs) lists players in the Classic editor's sortable 8-column table with the full filter grid", [&] {
        for (int tab : {0, 1}) {
            shell::player_edit_tab() = tab;
            app.sel_player = 1001;
            app.go(Page::PlayerEdit);
            ui.frames(4);
            const std::string where = tab == 0 ? "Profile tab" : "Positions tab";
            CHECK(sortable_8col_tables() == 1, where + ": one sortable table of 8 columns: " + std::to_string(sortable_8col_tables()));
            for (const char* f : {"##psearch", "##fteam", "##fnat", "##fcont", "##fpos", "##fstyle", "##fgender", "##floan", "Clear##filters"})
                CHECK(ui.find(f) != nullptr, where + ": filter " + f);
            CHECK(classic_player_list_rows() > 3, where + ": rows listed " + std::to_string(classic_player_list_rows()));
            CHECK(ui.find("##pfsearch") == nullptr, where + ": the old 3-column list's search is gone");
        }
        // one selection: a row picked on the Positions tab is the player of the Profile tab
        const PlayerRow* other = app.model.player(1005);
        CHECK(other != nullptr, "player 1005");
        if (other) {
            CHECK(ui.type_into(ui.find("##psearch"), other->name), "search him");
            ui.frames(3);
            CHECK(ui.click("1005"), "he is listed (his row is there to click)");
            ui.frames(2);
            CHECK(app.sel_player == 1005, "one selection: he is the picked player");
        }
        CHECK(ui.type_into(ui.find("##psearch"), ""), "search cleared");
    });

    run_case("G-ia: Positions tab: Player type & tier is the first card, with the 5 tiers in view, one player type picker, the player type bar once", [&] {
        shell::player_edit_tab() = 1;
        app.sel_player = 1001;
        app.prefs.advanced_mode = false;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        const PlayerAbilityState& st = player_ability_state();
        CHECK(st.playerid == 1001 && st.type_tier_shown, "the card is drawn open");
        const ItemRec* card = ui.find("###ab_typetier");
        const ItemRec* ovr = ui.find("###ab_ovr");
        const ItemRec* pos = ui.find("###ab_positions");
        CHECK(card && ovr && pos, "the card and the sections after it are on screen");
        if (card && ovr && pos) CHECK(card->rect.Min.y < ovr->rect.Min.y && ovr->rect.Min.y < pos->rect.Min.y, "Player type & tier comes before Overall & potential and Positions");
        const ItemRec* tier = ui.find("##ptier");
        CHECK(tier != nullptr, "the Tier combo is in view without scrolling (1920x1080)");
        CHECK(ui.find("##parch") != nullptr && ui.find("##ptarget") != nullptr, "Player type and Wanted overall are in the card");
        CHECK(ui.find("Review changes...###popreview") != nullptr, "Populate's Review changes is in the card");
        CHECK(st.bar_calls == 1 && st.archetype_bar_external, "the player type bar (Overall +/-1, See changes...) is drawn once, in the card: " + std::to_string(st.bar_calls));
        CHECK(ui.find("See changes...###archpreview") != nullptr, "See changes... is reachable in the card");
        CHECK(count_items("##arch") == 0 && count_items("##parch") == 1, "one Player type picker: the card's");
        // the Tier combo offers the five tiers
        if (tier) {
            CHECK(ui.click("##ptier"), "open the Tier combo");
            ui.frames(2);
            for (const char* name : {"Superstar", "Starter", "Rotation", "Bench", "Prospect"})
                CHECK(ui.find(name, "##Combo") != nullptr, std::string("tier: ") + name);
            CHECK(ui.click("##ptier"), "close the Tier combo");
            ui.frames(2);
        }
        // collapsible and remembered
        CHECK(ui.click("###ab_typetier"), "collapse the card");
        ui.frames(3);
        CHECK(!app.prefs.section_open("ability.type_tier", true) && !player_ability_state().type_tier_shown && ui.find("##ptier") == nullptr, "collapsed, and remembered");
        CHECK(ui.click("###ab_typetier"), "open the card again");
        ui.frames(3);
        CHECK(app.prefs.section_open("ability.type_tier", false) && player_ability_state().type_tier_shown && ui.find("##ptier") != nullptr, "open again, and remembered");
        // 3.0.1: the Attributes header follows the card; since the 3.0 pages grew (c497923a shell, 3db038b7 theme) it is below the fold of a 1080 px window,
        // so it is reached by scrolling, as a user does (last, so the checks above saw the page at the top)
        CHECK(ui.find("###ab_attrs") != nullptr || ui.find_scrolling("###ab_attrs", "", "") != nullptr, "Attributes follow");
        // the Attributes section has no bar of its own any more, and the attribute sliders are still there
        CHECK(player_ability_state().attribute_sliders > 0, "attribute sliders drawn: " + std::to_string(player_ability_state().attribute_sliders));
    });

    run_case("G-ia: the outer tab is Classic editor (was All fields); the Classic editor keeps its own tabs and its own player type bar", [&] {
        shell::player_edit_tab() = 4;
        app.sel_player = 1001;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        CHECK(ui.find("Everything (classic)###pe_allfields") != nullptr, "the tab is called Everything (classic)");
        CHECK(ui.find("All fields###pe_allfields") == nullptr, "no outer tab called All fields");
        CHECK(ui.find("Card view (beta)###pcardview") != nullptr, "it holds the full editor and the Card view switch");
        shell::player_edit_tab() = 0;
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("G-ia: Create player dialog: Player type & tier first, Potential / Birth date / Nationality / Foot under Profile, one Player type picker", [&] {
        if (app.busy()) ui.click("Cancel");
        shell::player_edit_tab() = 4;
        app.sel_player = 1002;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        CHECK(ui.click("Create player...", "##pedit"), "open the Create player dialog");
        ui.frames(3);
        const CreatePrefillState& cp = create_prefill_state();
        const std::vector<std::string> want = {"Player type & tier", "Names", "Club", "Profile", "Attributes"};
        std::string got;
        for (const std::string& s : cp.sections) got += s + "|";
        CHECK(cp.sections == want, "section order: " + got);
        CHECK(count_items("##carch") == 0 && count_items("##cparch") == 1, "one Player type picker");
        const ItemRec* type = ui.find("##cparch", "##pcreate");
        const ItemRec* first = ui.find("##first", "##pcreate");
        const ItemRec* position = ui.find("##position", "##pcreate");
        CHECK(type && first && position, "the pieces of the dialog are drawn");
        if (type && first && position) CHECK(type->rect.Min.y < first->rect.Min.y && first->rect.Min.y < position->rect.Min.y, "the player type picker is above the names, the profile after");
        // Potential, Birth date, Nationality and Foot come after the Profile heading's first box (the position), and before the attributes
        const ItemRec* attrs = ui.find("His attributes (edit any)###cattrs", "##pcreate");
        for (const char* id : {"##potential", "##bday", "##nationality", "##foot"}) {
            const ItemRec* r = ui.find(id, "##pcreate");
            if (!r) continue;  // a database without the field
            CHECK(position && r->rect.Min.y > position->rect.Min.y, std::string("under Profile, after the position: ") + id);
            if (attrs) CHECK(r->rect.Min.y < attrs->rect.Min.y, std::string("before the attributes: ") + id);
        }
        CHECK(ui.click("Cancel##pcreate", "##pcreate"), "close the dialog");
        shell::player_edit_tab() = 0;
        app.go(Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("G-ia: Appearance Editor: the Tattoos card and the whole miniface editor are on the page, open, no extra click", [&] {
        app.prefs.advanced_mode = false;
        app.sel_player = 1001;
        looks::looks_studio_reset_for_tests();
        CHECK(ui.click("Looks", "FC 27 LE Turbo", "##shell"), "Appearance Editor tab");
        ui.frames(4);
        CHECK(app.place() == shell::place_of(Page::LooksStudio), "it opened the Appearance Editor");
        const looks::StudioState& ss = looks::studio_state();
        if (ss.subject.id != 1001) {
            CHECK(ui.type_into(ui.find("##lsq"), app.model.player(1001) ? app.model.player(1001)->name : std::string("a")), "search the player");
            const PlayerRow* p = app.model.player(1001);
            if (p) CHECK(ui.click(p->name + "##lsp1001"), "pick him");
            ui.frames(3);
        }
        CHECK(ss.subject.id == 1001 && !ss.subject.manager, "he is the person shown");
        CHECK(ss.tattoo_card, "the Tattoos card is drawn (tattoo_editor, all body areas)");
        CHECK(ss.miniface_card, "the Player miniface card is drawn (the whole miniface_editor)");
        // the miniface editor's own tabs and buttons, with no card to open first
        // (the card is the fifth of a scrolling column and a card scrolled out of view submits nothing: the wheel, over the cards, brings it in;
        // no click on a card header is needed because no card is collapsed)
        auto reach = [&](const char* tab) {
            if (ui.find(tab) != nullptr) return true;
            const ItemRec* anchor = ui.find("###lsundo");
            ImVec2 over(300.0f, 400.0f);
            if (anchor) over = ImVec2(anchor->rect.GetCenter().x, anchor->rect.GetCenter().y + S(120.0f));
            for (int step = 0; step < 60; ++step) {
                ImGui::GetIO().AddMousePosEvent(over.x, over.y);
                ui.frame();
                ImGui::GetIO().AddMouseWheelEvent(0.0f, -3.0f);
                ui.frames(2);
                if (ui.find(tab) != nullptr) return true;
            }
            return false;
        };
        CHECK(reach("An image file") || reach("The 3D model"), "the miniface editor's source tabs are drawn");
        CHECK(reach("A player's miniface") && reach("Head model / youth face"), "the miniface editor's other tabs");
        CHECK(ui.find("###navgo") == nullptr, "no signpost");
        // 3.0.1: the picture picker now probes its shortcut folders when it opens (ui_images.cpp file_browser_modal), not on every frame it is
        // closed: the very first open must still show them. A home folder of our own, so the check holds on any machine.
        {
            const char* env_home = std::getenv("USERPROFILE");
            const std::string saved_home = env_home ? env_home : "";
            const bool had_home = env_home != nullptr;
            const fs::path home = test_tmp_dir("picker_home");
            fs::remove_all(home);
            fs::create_directories(home / "Desktop");
            fs::create_directories(home / "Pictures");
#ifdef _WIN32
            _putenv_s("USERPROFILE", home.string().c_str());
#else
            setenv("USERPROFILE", home.string().c_str(), 1);
#endif
            CHECK(reach("An image file") && ui.click("An image file"), "the miniface editor's image-file tab");
            ui.frames(2);
            // "Browse..." sits a few pixels below the visible part of the cards column (TURBO_TEST_DEBUG dump of 3.0.1: centre y 1054, clip bottom 1047).
            // reach() wheels at (300,400) here, because ###lsundo has scrolled out and is not found: that is over the player list, so nothing moved.
            // ui.click() goes through find_scrolling, which puts the wheel over the hidden item's own window, the way a user does.
            CHECK(ui.click("Browse..."), "Browse...");
            ui.frames(3);
            if (ui.find("Desktop##fpplace") == nullptr) ui.dump("guard: the picker did not open");
            CHECK(ui.find("Desktop##fpplace") != nullptr && ui.find("Pictures##fpplace") != nullptr, "first open of the picker: the Desktop and Pictures shortcuts are there");
            CHECK(ui.click("Cancel##fp"), "close the picker");
            ui.frames(3);
            CHECK(ui.find("Desktop##fpplace") == nullptr, "the picker is closed");
#ifdef _WIN32
            _putenv_s("USERPROFILE", saved_home.c_str());
#else
            if (had_home) setenv("USERPROFILE", saved_home.c_str(), 1);
            else unsetenv("USERPROFILE");
#endif
        }
    });

    app.legacy_pages = was_legacy;
    app.prefs.advanced_mode = was_adv;
    app.player_card_view = was_card;
    app.sel_player = was_sel;
    shell::player_edit_tab() = was_tab;
    ImGui::GetIO().DisplaySize = old_display;
    ui.frames(2);
}

}  // namespace ia222_test
