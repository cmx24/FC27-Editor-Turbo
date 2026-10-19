// Native tests (2026-10-19, brief 2.20-IA-pages, track "creation"): Players > Player Creation (ui/ui_player_creation.cpp).
// Included by test_main.cpp after test_page_player.h; page_creation_test::ui_cases runs inside the UI block, a real App. The page is
// drawn through the Ui hook g_ui_extra (a separate window "##playercreation_test" after the app's own frame), as the Player page test
// does: the page is not registered with the shell yet (the wiring owner adds the line), so it is drawn directly.
// Every case name starts with "PlayerCreation:" so that TURBO_TEST_ONLY="PlayerCreation" selects them.
#pragma once
#include "core/create_form.h"
#include "ui/ui_names.h"
#include "ui/ui_player_creation.h"
#include "ui/ui_presets.h"

namespace page_creation_test {

using namespace turbo;

static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();  // a modal keeps the focus it needs
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##playercreation_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        draw_player_creation(app);
    ImGui::End();
}
static void show(Ui& ui) {
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_ui_extra = nullptr;
    ui.frames(2);
}

static void ui_cases(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, const fs::path& le) {
    (void)le;
    const char* W = "##playercreation_test";
    const int64_t saved_sel = app.sel_player;
    const Table* pt = app.db.table("players");

    run_case("PlayerCreation: the page has the list, the actions of one player, Delete and the CMTracker tab; no ID text, no old signpost", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.sel_player = 0;
        show(ui);
        const PlayerCreationState& st = player_creation_state();
        CHECK(ui.find("###pc_tab_player", W) != nullptr && ui.find("###pc_tab_cmt", W) != nullptr, "two tabs");
        CHECK(st.tab == 0, "the player tab is the first");
        CHECK(ui.find("##pcsearch", W) != nullptr && ui.find("###pc_chip_free", W) != nullptr && ui.find("##pcpos", W) != nullptr, "search, chip, position filter");
        CHECK(st.listed > 3, "the list holds players: " + std::to_string(st.listed));
        CHECK(st.playerid == 0 && ui.find("Export...", W) == nullptr && ui.find("###pc_delete", W) == nullptr, "nobody picked: no actions yet");
        CHECK(ui.find("Create like him...", W) != nullptr, "the footer action is there");
        CHECK(ui.click("###pc1005", W), "click a row");
        ui.frames(2);
        CHECK(app.sel_player == 1005 && st.playerid == 1005, "the click picked him");
        const PlayerRow* pr = app.model.player(1005);
        CHECK(pr && st.headline.find("overall") != std::string::npos && st.headline.find(std::to_string(pr->overall)) != std::string::npos,
              "headline: " + st.headline);
        CHECK(st.headline.find("ID") == std::string::npos && st.headline.find("1005") == std::string::npos, "the headline names no ID");
        CHECK(ui.find("ID", W) == nullptr && ui.find("Player ID", W) == nullptr, "no ID label on the page");
        for (const char* l : {"Export...", "Import...", "Clone...", "Create player...", "From CMTracker...", "Repair names...", "###pc_delete"})
            CHECK(ui.find(l, W) != nullptr, std::string("the editor has: ") + l);
        // nothing of the old signpost
        CHECK(ui.find("Open Edit a player", W) == nullptr, "no signpost button");
        // search narrows the list
        CHECK(ui.type_into(ui.find("##pcsearch", W), "zzzz-no-such-player"), "type a search");
        ui.frames(2);
        CHECK(st.listed == 0, "nobody matches");
        CHECK(ui.type_into(ui.find("##pcsearch", W), ""), "clear the search");
        ui.frames(2);
        CHECK(st.listed > 3, "everybody again");
        hide(ui);
    });

    run_case("PlayerCreation: Create player from this page opens the form with every box read from his database row (prefill)", [&] {
        CHECK(pt != nullptr, "players table");
        if (!pt) return;
        if (app.busy()) ui.click("Cancel");
        const int64_t tpl = 1002;
        app.sel_player = tpl;
        show(ui);
        const uint64_t rec = app.db.find(*pt, "playerid", tpl);
        const PlayerRow* row = app.model.player(tpl);
        CHECK(rec != 0 && row != nullptr, "the template exists");
        if (!row) {
            hide(ui);
            return;
        }
        // two routes to the same dialog: the editor's own button, and the footer button of the page
        for (int route = 0; route < 2; ++route) {
            const char* what = route == 0 ? "Create player... (editor)" : "Create like him... (footer)";
            if (ui.find("Cancel##pcreate", "##pcreate")) ui.click("Cancel##pcreate", "##pcreate");
            ui.frames(2);
            app.sel_player = tpl;
            ui.frames(2);
            CHECK(ui.click(route == 0 ? "Create player..." : "Create like him...", W), std::string("click ") + what);
            ui.frames(3);
            const CreatePrefillState& cp = create_prefill_state();
            CHECK(cp.playerid == tpl, std::string(what) + ": the dialog is on the template");
            const std::vector<std::string> fields = create_form::shown_fields(*pt);
            int equal = 0;
            for (const std::string& f : fields) {
                const bool same = cp.orig.has(f) && cp.orig.get(f) == app.db.get_int(*pt, rec, f, 0) && cp.cur.get(f) == cp.orig.get(f);
                equal += same;
                CHECK(same, std::string(what) + ": box " + f + " = his row");
            }
            CHECK(equal == static_cast<int>(fields.size()) && fields.size() >= 12, fmt("%s: %d of %zu boxes equal his row", what, equal, fields.size()));
            const GameDate born = date_from_gregorian_days(app.db.get_int(*pt, rec, "birthdate", 0));
            CHECK(cp.orig.has_birthdate && create_form::same_date(cp.orig.birth, born), std::string(what) + ": his exact birth date");
            const ShownName sn = shown_name(app, *pt, *row);
            CHECK(cp.orig.names == create_form::Names({sn.first, sn.last, sn.common, sn.jersey}), std::string(what) + ": his names as the game shows them");
            CHECK(cp.pending.empty(), std::string(what) + ": nothing changed yet, nothing to send");
            CHECK(ui.click("Cancel##pcreate", "##pcreate"), std::string(what) + ": close the dialog");
            ui.frames(2);
            CHECK(!app.busy(), std::string(what) + ": nothing was sent");
        }
        hide(ui);
    });

    run_case("PlayerCreation: Clone, Import and Export open their dialogs from this page", [&] {
        if (app.busy()) ui.click("Cancel");
        app.sel_player = 1003;
        show(ui);
        struct D { const char* open; const char* cancel; const char* win; };
        const D ds[] = {{"Clone...", "Cancel##pclone", "##pclone"}, {"Import...", "Cancel##pimport", "##pimport"}, {"Export...", "Cancel##pexport", "##pexport"}};
        for (const D& d : ds) {
            CHECK(ui.click(d.open, W), std::string("click ") + d.open);
            ui.frames(3);
            CHECK(ui.find(d.cancel, d.win) != nullptr, std::string("the dialog is open: ") + d.cancel);
            CHECK(ui.click(d.cancel, d.win), std::string("close ") + d.cancel);
            ui.frames(2);
            CHECK(ui.find(d.cancel, d.win) == nullptr && !app.busy(), std::string("closed, nothing sent: ") + d.open);
        }
        hide(ui);
    });

    run_case("PlayerCreation: Delete asks first, says it can't be undone, sends the delete command only on the answer", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        const PlayerCreationState& st = player_creation_state();
        show(ui);
        CHECK(app.bridge.state().in_cm, "the test world is in a career");
        // a player the move rules let go, and (when there is one) a player they refuse
        int64_t allowed = 0, refused = 0;
        std::vector<int64_t> candidates;
        for (const PlayerRow& p : app.model.players()) {
            if (candidates.size() >= 60) break;
            candidates.push_back(p.playerid);
        }
        for (int64_t pid : candidates) {
            app.sel_player = pid;
            ui.frames(2);
            if (st.playerid != pid) continue;
            if (st.delete_why.empty() && !allowed) allowed = pid;
            if (!st.delete_why.empty() && !refused) refused = pid;
            if (allowed && refused) break;
        }
        CHECK(allowed != 0, "a player Delete can run for");
        if (refused) {
            app.sel_player = refused;
            ui.frames(2);
            CHECK(ui.click("###pc_delete", W), "click Delete on a refused player");
            ui.frames(3);
            CHECK(st.confirm_text.empty() && !app.busy(), "refused by the move rules: no question, nothing sent (" + st.delete_why + ")");
        }
        if (!allowed) {
            hide(ui);
            return;
        }
        app.sel_player = allowed;
        ui.frames(2);
        CHECK(ui.click("###pc_delete", W), "click Delete");
        ui.frames(3);
        CHECK(!st.confirm_text.empty(), "the question is on screen");
        CHECK(st.confirm_text.find("Can't be undone") != std::string::npos, "it says it can't be undone: " + st.confirm_text);
        CHECK(!app.busy(), "nothing sent before the answer");
        CHECK(ui.click("###pc_delno"), "Cancel");
        ui.frames(3);
        CHECK(!app.busy() && st.confirm_text.empty(), "cancelled: nothing sent, question gone");
        CHECK(ui.click("###pc_delete", W), "click Delete again");
        ui.frames(3);
        CHECK(ui.click("###pc_delgo"), "answer Delete");
        ui.frames(2);
        CHECK(app.busy(), "the command is queued");
        json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
        CHECK(!cmd.is_discarded() && cmd.value("module", "") == "player_moves" && cmd.contains("overrides") && cmd["overrides"].contains("actions") &&
                  cmd["overrides"]["actions"].size() == 1,
              "a player_moves command: " + (cmd.is_discarded() ? std::string("unreadable") : cmd.dump()));
        if (!cmd.is_discarded() && cmd.contains("overrides") && cmd["overrides"].contains("actions") && cmd["overrides"]["actions"].size() == 1) {
            const json& a = cmd["overrides"]["actions"][0];
            CHECK(a.value("action", "") == "delete" && a.value("playerid", int64_t(0)) == allowed && a.value("confirm", false), "delete him, confirmed: " + a.dump());
        }
        ui.click("Cancel");  // the command is not run: the world's Lua side is not asked
        ui.frames(2);
        hide(ui);
    });

    run_case("PlayerCreation: the CMTracker library is the second tab", [&] {
        if (app.busy()) ui.click("Cancel");
        app.sel_player = 1001;
        show(ui);
        const PlayerCreationState& st = player_creation_state();
        CHECK(ui.click("###pc_tab_cmt", W), "click the CMTracker tab");
        ui.frames(3);
        CHECK(st.tab == 1, "the tab is open");
        CHECK(ui.find("Season pack...##page", W) != nullptr, "the library page is drawn (Season pack...)");
        CHECK(ui.find("Create player", W) != nullptr, "the library's Create player action");
        CHECK(ui.find("###pc_delete", W) == nullptr, "the player actions are not drawn on this tab");
        CHECK(ui.click("###pc_tab_player", W), "back to the player tab");
        ui.frames(3);
        CHECK(st.tab == 0 && ui.find("###pc_delete", W) != nullptr, "the player tab is back");
        hide(ui);
    });

    app.sel_player = saved_sel;
    app.lua_queue.clear();
}

}  // namespace page_creation_test
