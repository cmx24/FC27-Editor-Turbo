// Native tests (2.16 gui/page-career, brief 2.16-gui-pages, plan step 10): the Career pages of ui/ui_career_page.cpp (declared in
// ui/ui_career_pages.h): Transfers & money, Manager jobs, My squad and Managers & staff. Included by test_main.cpp after
// test_asset_app.h; page_career_test::ui_cases runs inside the UI block (a real App on the simulated game). The pages are not
// registered with the shell yet (the navigation re-cut does that), so each one is drawn by function through the Ui hook g_ui_extra,
// in a window of its own ("##careerpage_test") that is tall enough to show a whole page.
//
// What is proven here: every card and its button is there; no page shows an old-tab control (Probe, Bulk edit, Count ...); every
// action the Undo button cannot bring back ASKS first (Cancel sends nothing) and the confirmed request carries the same Lua module
// and the same override keys the old panels send; the daily-job switches write the same gui_settings keys as the old panels; the
// "Not tested yet" tag sits on the right cards. The words and ids are guarded by the "gui labels" cases (the word gate is 0 for this
// file). Cases are named "Career pages: ..." so TURBO_TEST_ONLY="career pages" runs them alone.
#pragma once
#include "ui/ui_career_pages.h"
#include "ui/ui_shell.h"

namespace page_career_test {

using namespace turbo;

static void (*g_page_fn)(App&) = nullptr;
static void draw_extra(App& app) {
    if (!g_page_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // focusing the page window every frame would close the question (a modal popup) the page has just opened
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##careerpage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_page_fn(app);
    ImGui::End();
}
static void show(Ui& ui, void (*fn)(App&)) {
    g_page_fn = fn;
    g_ui_extra = draw_extra;
    ui.frames(4);
}
static void hide(Ui& ui) {
    g_page_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}

// Make capabilities of the simulated game available (the test world has no game calls): rewrite bridge_state.json the way the
// job offer case of test_main.cpp does, each rewrite with a different time stamp so the bridge reads it again
static void write_state(App& app, Ui& ui, const fs::path& le, json st) {
    static int bumps = 0;
    const fs::path state_file = le / "turbo_output" / "bridge_state.json";
    st["seq"] = st.value("seq", 0LL) + 1;
    {
        std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
        f << st.dump();
    }
    fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(300 + 2 * ++bumps));
    app.next_poll = 0.0;
    ui.frames(3);
}
static void lift(App& app, Ui& ui, const fs::path& le, const std::vector<std::string>& keys) {
    json st = read_json(le / "turbo_output" / "bridge_state.json");
    if (st.contains("unavailable") && st["unavailable"].is_object())
        for (const std::string& k : keys) st["unavailable"].erase(k);
    write_state(app, ui, le, st);
}

// A command as the checks read it: every key they look at is there, so a missing one reads as false / 0, never as an exception
static json norm(json j) {
    if (!j.is_object()) j = json::object();
    if (!j.contains("overrides") || !j["overrides"].is_object()) j["overrides"] = json::object();
    if (!j["overrides"].contains("scope") || !j["overrides"]["scope"].is_object()) j["overrides"]["scope"] = json::object();
    if (!j["overrides"].contains("actions") || !j["overrides"]["actions"].is_array()) j["overrides"]["actions"] = json::array();
    return j;
}
// The command the last click sent to the game side
static json sent(SimMemory& mem, uint64_t kMb) { return norm(json::parse(mem.read_cstr(kMb + kMbCmd, kMbTextSize), nullptr, false)); }

static bool has_item(Ui& ui, const std::string& label, const char* win) { return ui.find(label, win) != nullptr; }

// Ask first: click `button` on the page, Cancel sends nothing, click again and confirm with `ok`; returns the command sent
static json ask_and_confirm(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, const std::string& button, const char* page_win, const char* popup,
                            const std::string& ok, std::string& why) {
    why.clear();
    app.mailbox->cancel();
    if (!ui.click(button, page_win)) {
        why = "button not found: " + button;
        return norm(json::object());
    }
    ui.frames(3);
    if (!ui.find("Cancel", popup)) {
        why = "no question after " + button + " (in_cm=" + std::to_string(app.bridge.state().in_cm) + " busy=" + std::to_string(app.busy()) + " pending_label=" + app.pending_label + " lua_alive=" + std::to_string(app.lua_alive()) + ")";
        for (const auto& kv : g_items) if (kv.second.frame == g_frame && kv.second.window.find("careerask") != std::string::npos) why += " [popup item: " + kv.second.label + " in " + kv.second.window + "]";
        return norm(json::object());
    }
    ui.click("Cancel", popup);
    ui.frames(3);
    if (app.mailbox->pending()) {
        why = "Cancel still sent a request for " + button;
        return norm(json::object());
    }
    ui.click(button, page_win);
    ui.frames(3);
    if (!ui.click(ok, popup)) {
        why = "no confirm button " + ok;
        return norm(json::object());
    }
    ui.frames(3);
    if (!app.mailbox->pending()) {
        why = "confirmed but nothing was sent";
        return norm(json::object());
    }
    json j = sent(mem, kMb);
    app.mailbox->cancel();
    ui.frames(2);
    return j;
}

static void ui_cases(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, const fs::path& le) {
    const ImVec2 old_display = ImGui::GetIO().DisplaySize;
    const json original_state = read_json(le / "turbo_output" / "bridge_state.json");
    const json original_auto = app.gui_settings.contains("auto") ? app.gui_settings["auto"] : json();
    ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 3400.0f);  // a whole page fits: nothing is scrolled out of reach
    ui.frames(3);
    app.mailbox->cancel();
    lift(app, ui, le, {"transfer_budget", "transfer_bans", "job_offer", "manager_rules", "form_morale", "development", "reveal", "squad_role"});
    const int64_t mine = app.bridge.state().user_team;
    std::string why;

    run_case("Career pages: Transfers & money - the cards, no old-tab clutter, budget and contracts ask first and send the same requests", [&] {
        CHECK(app.connected() && app.bridge.state().in_cm, "the test world is connected and in a career");
        show(ui, draw_career_transfers_money);
        CHECK(!career_page_state().waiting && career_page_state().cards >= 3, fmt("cards drawn: %d", career_page_state().cards));
        CHECK(career_page_state().club == mine, "the page works on my club");
        CHECK(career_page_state().tags >= 1, "a card whose write was never seen in game carries the tag");
        for (const char* l : {"Set budget", "Add to budget", "Extend their contracts"}) CHECK(has_item(ui, l, "##careerpage_test"), std::string("on the page: ") + l);
        for (const char* l : {"Probe report", "Count", "Delete...", "Apply bulk edit", "Export tables", "Season stats", "Reveal my club##rv"})
            CHECK(!has_item(ui, l, "##careerpage_test"), std::string("not on the page: ") + l);
        // the budget: Cancel sends nothing, the confirmed request is the old Set budget / Add to budget request
        json j = ask_and_confirm(app, ui, mem, kMb, "Set budget", "##career_budget", "Set the budget?", "Set budget", why);
        CHECK(why.empty(), "Set budget: " + why);
        CHECK(j.value("op", "") == "run" && j.value("module", "") == "transfer_budget" && j["overrides"].value("mode", "") == "set" &&
                  j["overrides"].value("amount", 0LL) == 50000000LL,
              "Set budget sends transfer_budget mode set, amount 50000000: " + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Add to budget", "##career_budget", "Add to the budget?", "Add to budget", why);
        CHECK(why.empty() && j.value("module", "") == "transfer_budget" && j["overrides"].value("mode", "") == "add", "Add to budget sends mode add: " + why + j.dump());
        // other clubs' contracts: needs no career, asks first, says it cannot be undone
        j = ask_and_confirm(app, ui, mem, kMb, "Extend their contracts", "##career_contracts_all", "Extend all other clubs' contracts?", "Extend contracts", why);
        CHECK(why.empty() && j.value("module", "") == "extend_cpu_contracts" && j["overrides"].value("years", 0) == 5,
              "Extend their contracts sends extend_cpu_contracts, 5 years: " + why + j.dump());
        // an unavailable budget is greyed out, with a plain line instead of a code
        json st = read_json(le / "turbo_output" / "bridge_state.json");
        st["unavailable"]["transfer_budget"] = "No budget call in this build.";
        write_state(app, ui, le, st);
        ui.frames(2);
        app.mailbox->cancel();
        ui.click("Set budget", "##career_budget");
        ui.frames(3);
        CHECK(!ui.find("Cancel", "Set the budget?") && !app.mailbox->pending(), "an unavailable budget cannot be set");
        lift(app, ui, le, {"transfer_budget"});
        hide(ui);
    });

    run_case("Career pages: Manager jobs - job offer, never sack me, move a manager: each asks first and sends the old module", [&] {
        const TeamRow* everton = app.model.team(7);
        CHECK(everton != nullptr && everton->teamid != mine, "Everton (7) is a club of the test world, not mine");
        app.job_offer_team = 0;
        app.job_offer_status.clear();
        show(ui, draw_career_manager_jobs);
        CHECK(!career_page_state().waiting && career_page_state().cards == 3, fmt("three cards: %d", career_page_state().cards));
        // no club picked: the button is greyed out
        app.mailbox->cancel();
        ui.click("Ask for the job", "##career_joboffer");
        ui.frames(3);
        CHECK(!ui.find("Cancel", "for its job?") && !app.mailbox->pending(), "no club picked: nothing happens");
        app.job_offer_team = mine;
        ui.frames(2);
        ui.click("Ask for the job", "##career_joboffer");
        ui.frames(3);
        CHECK(!ui.find("Cancel", "for its job?") && !app.mailbox->pending(), "my own club cannot be asked");
        app.job_offer_team = 7;
        ui.frames(2);
        CHECK(career_page_state().manager_club == 7, "the page works on the club picked");
        json j = ask_and_confirm(app, ui, mem, kMb, "Ask for the job", "##career_joboffer", "for its job?", "Ask for the job", why);
        CHECK(why.empty(), "job offer: " + why);
        CHECK(j.value("module", "") == "job_offer" && j["overrides"].value("teamid", 0) == 7 && j["overrides"].value("confirm", false) &&
                  j["overrides"].value("enabled", false),
              "the job offer carries the club, enabled and confirm: " + j.dump());
        CHECK(app.pending_label.rfind("Job offer from Everton", 0) == 0, "label: " + app.pending_label);
        CHECK(!app.job_offer_status.empty() && app.job_offer_status.find("Open a game screen") != std::string::npos, "status line: " + app.job_offer_status);
        // never sack me: reversible, no question; the second button puts the game's rule back
        app.mailbox->cancel();
        ui.frames(2);
        CHECK(ui.click("Never sack me", "##career_sacking"), "Never sack me");
        ui.frames(3);
        CHECK(app.mailbox->pending(), "sent");
        j = sent(mem, kMb);
        CHECK(j.value("module", "") == "manager_rules" && j["overrides"].value("unsackable", false) && j["overrides"].value("keep", false) &&
                  j["overrides"].value("confirm", false),
              "unsackable on, kept: " + j.dump());
        app.mailbox->cancel();
        ui.frames(2);
        CHECK(ui.click("Allow sacking again", "##career_sacking"), "Allow sacking again");
        ui.frames(3);
        j = sent(mem, kMb);
        CHECK(j.value("module", "") == "manager_rules" && !j["overrides"].value("unsackable", true) && !j["overrides"].value("keep", true),
              "unsackable off, not kept: " + j.dump());
        app.mailbox->cancel();
        ui.frames(2);
        // move a manager: pick one that is not mine and not at Everton
        const auto& mgrs = app.model.managers();
        int pick = -1;
        for (size_t i = 0; i < mgrs.size(); ++i)
            if (mgrs[i].teamid > 0 && mgrs[i].teamid != 7 && mgrs[i].teamid != mine) {
                pick = static_cast<int>(i);
                break;
            }
        CHECK(pick >= 0, "the test world has a manager at another club");
        if (pick >= 0) {
            app.sel_manager = pick;
            app.manager_move_team = 0;
            ui.frames(2);
            ui.click("Move manager", "##career_movemanager");
            ui.frames(3);
            CHECK(!ui.find("Cancel", "Move ") && !app.mailbox->pending(), "no club picked: nothing happens");
            app.manager_move_team = 7;
            ui.frames(2);
            CHECK(career_page_state().move_club == 7, "the page works on the club picked");
            j = ask_and_confirm(app, ui, mem, kMb, "Move manager", "##career_movemanager", "Move ", "Move manager", why);
            CHECK(why.empty(), "move: " + why);
            CHECK(j.value("module", "") == "manager_move" && j["overrides"].value("managerid", 0LL) == mgrs[static_cast<size_t>(pick)].managerid &&
                      j["overrides"].value("teamid", 0) == 7 && j["overrides"].value("confirm", false),
                  "the move carries the manager and the club: " + j.dump());
            j = ask_and_confirm(app, ui, mem, kMb, "Make available", "##career_movemanager", "a free agent?", "Make available", why);
            CHECK(why.empty() && j.value("module", "") == "manager_move" && j["overrides"].value("teamid", -1) == 0,
                  "make available sends teamid 0: " + why + j.dump());
        }
        app.job_offer_team = 0;
        app.job_offer_status.clear();
        app.manager_move_team = 0;
        app.manager_move_status.clear();
        app.manager_rules_status.clear();
        app.sel_manager = -1;
        hide(ui);
    });

    run_case("Career pages: My squad - the five cards, every action asks first, the daily switches write the old settings keys", [&] {
        show(ui, draw_career_my_squad);
        // 2.22: the page opens on the Squad tab (the sortable table, test_squad_table_restore.h); the five cards are the second tab, unchanged
        CHECK(ui.click("###sqtabtools", "##careerpage_test"), "the Career tools tab is there");
        ui.frames(3);
        CHECK(!career_page_state().waiting && career_page_state().cards == 5, fmt("five cards: %d", career_page_state().cards));
        CHECK(career_page_state().tags >= 2, "the cards that were never seen in game carry the tag");
        for (const char* l : {"Apply to my squad", "Set for my squad", "Extend my squad's contracts", "Develop to potential", "Reveal my players", "Run for my squad"})
            CHECK(has_item(ui, l, "##careerpage_test"), std::string("on the page: ") + l);
        for (const char* l : {"Youth player ID", "League ID", "Set youth player##yt", "Position", "Tier", "Reveal league ID##rv"})
            CHECK(!has_item(ui, l, "##careerpage_test"), std::string("no code on the page: ") + l);
        json j = ask_and_confirm(app, ui, mem, kMb, "Apply to my squad", "##career_form", "Set form and fitness?", "Apply", why);
        CHECK(why.empty() && j.value("module", "") == "form_morale" && j["overrides"].contains("form") && j["overrides"].contains("morale") &&
                  j["overrides"].contains("fitness"),
              "form and fitness send form_morale with the three values: " + why + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Set for my squad", "##career_roles", "Set everyone's role?", "Set roles", why);
        CHECK(why.empty() && j.value("module", "") == "squad_role" && j["overrides"].value("role", 0) == 3 && j["overrides"].value("include_loaned_in", false),
              "squad roles send squad_role, role 3 (Rotation), loaned-in included: " + why + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Extend my squad's contracts", "##career_contracts", "Extend your squad's contracts?", "Extend contracts", why);
        CHECK(why.empty() && j.value("module", "") == "extend_user_contracts" && j["overrides"].value("years", 0) == 4,
              "contracts send extend_user_contracts, 4 years: " + why + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Develop to potential", "##career_growth", "Develop everyone to potential?", "Develop them", why);
        CHECK(why.empty() && j.value("module", "") == "development" && j["overrides"].value("mode", "") == "to_potential" && j["overrides"].value("confirm", false) &&
                  j["overrides"]["scope"].value("user_team", false),
              "growth sends development, to_potential, my team, confirm: " + why + j.dump());
        j = ask_and_confirm(app, ui, mem, kMb, "Reveal my players", "##career_growth", "Reveal your players?", "Reveal them", why);
        CHECK(why.empty() && j.value("module", "") == "reveal" && j["overrides"]["scope"].value("user_team", false), "reveal sends reveal, my team: " + why + j.dump());
        // the squad tools: the first tool, Block offers; needs players in the squad
        const bool has_squad = !app.model.links_of_team(mine).empty();
        if (has_squad) {
            app.mass_status.clear();
            j = ask_and_confirm(app, ui, mem, kMb, "Run for my squad", "##career_tools", "Block offers for my players?", "Run it", why);
            CHECK(why.empty() && j.value("module", "") == "team_mass" && j["overrides"].value("teamid", 0LL) == mine && j["overrides"]["actions"].is_array() &&
                      j["overrides"]["actions"].size() == 1 && j["overrides"]["actions"][0] == "block_offers",
                  "squad tools send team_mass for my club with one action: " + why + j.dump());
            CHECK(!app.mass_status.empty(), "the squad tools status line is the old mass_status");
            app.mass_status.clear();
        } else {
            app.mailbox->cancel();
            ui.click("Run for my squad", "##career_tools");
            ui.frames(3);
            CHECK(!app.mailbox->pending(), "no squad: the squad tools do nothing");
        }
        // the daily switches write the same gui_settings keys as the old panels, and switching off restores them
        app.mailbox->cancel();
        const bool was = app.gui_settings["auto"]["form_morale"].value("enabled", false);
        CHECK(ui.click("Keep this every day##careerformkeep", "##career_form"), "switch the daily form job");
        ui.frames(3);
        CHECK(app.gui_settings["auto"]["form_morale"].value("enabled", false) == !was && app.gui_settings["auto"]["form_morale"].contains("form"),
              "auto.form_morale.enabled flips and carries the values");
        app.mailbox->cancel();
        ui.frames(2);
        CHECK(ui.click("Keep this every day##careerformkeep", "##career_form"), "switch it back");
        ui.frames(3);
        CHECK(app.gui_settings["auto"]["form_morale"].value("enabled", false) == was, "back to what it was");
        app.mailbox->cancel();
        ui.frames(2);
        const bool grew = app.gui_settings["auto"]["development"].value("user_team", false);
        CHECK(ui.click("Grow my squad every week##careergrow", "##career_growth"), "switch the weekly growth");
        ui.frames(3);
        CHECK(app.gui_settings["auto"]["development"].value("user_team", false) == !grew && app.gui_settings["auto"]["development"].value("enabled", false) == !grew,
              "auto.development.user_team and enabled flip together");
        app.mailbox->cancel();
        ui.frames(2);
        CHECK(ui.click("Grow my squad every week##careergrow", "##career_growth"), "switch it back");
        ui.frames(3);
        CHECK(app.gui_settings["auto"]["development"].value("user_team", false) == grew, "back to what it was");
        app.mailbox->cancel();
        ui.frames(2);
        hide(ui);
    });

    run_case("Career pages: Managers & staff - the manager list, details, face and scouts; no rules or market left on it", [&] {
        const auto& mgrs = app.model.managers();
        CHECK(!mgrs.empty(), "the test world has managers");
        app.sel_manager = -1;
        show(ui, draw_career_staff);
        CHECK(!career_page_state().waiting, "connected");
        if (!mgrs.empty()) {
            const std::string row = mgrs[0].name + "##careerm0";
            CHECK(ui.click(row, "##career_stafflist"), "the first manager is in the list");
            ui.frames(3);
            CHECK(app.sel_manager == 0, "clicking a manager picks him");
            CHECK(career_page_state().cards >= 3, fmt("details, face and scouts cards: %d", career_page_state().cards));
            CHECK(has_item(ui, "Choose a real face##careerface", "##career_staffedit"), "the face chooser is under Face and picture");
            CHECK(has_item(ui, "Small picture in menus##careerminiface", "##career_staffedit"), "the small picture editor is there");
        }
        for (const char* l : {"Manager rules: job security, unsackable (Manager Career)", "Manager market: move a manager, make one available",
                              "Unsackable ON (keep)", "Move to the picked club", "Make available (free agent)"})
            CHECK(!has_item(ui, l, "##careerpage_test"), std::string("moved to Manager jobs: ") + l);
        app.sel_manager = -1;
        hide(ui);
    });

    run_case("Career pages: a visible label is short and has no code in it (R3, R4)", [&] {
        for (auto fn : {draw_career_transfers_money, draw_career_manager_jobs, draw_career_my_squad}) {
            show(ui, fn);
            if (fn == draw_career_my_squad) {  // 2.22: the cards are on the second tab
                ui.click("###sqtabtools", "##careerpage_test");
                ui.frames(3);
            }
            int checked = 0;
            for (const auto& kv : g_items) {
                const ItemRec& r = kv.second;
                if (r.frame != g_frame || r.window.find("##careerpage_test") == std::string::npos || r.label.empty()) continue;
                if (r.window.find("##career_moves_list") != std::string::npos) continue;  // the moves table is the old read-only panel
                const size_t hash = r.label.find("##");
                const std::string visible = r.label.substr(0, hash == std::string::npos ? r.label.size() : hash);
                if (visible.empty() || r.label.find("##mgr") != std::string::npos) continue;  // a manager row is a name, not a label
                ++checked;
                CHECK(visible.size() <= 30, "label too long (" + std::to_string(visible.size()) + "): " + visible);
                for (const char* bad : {"ID ", "Lua", "hook", "channel", "json", "txt", "md5", "turbo_output", "0x"})
                    CHECK(visible.find(bad) == std::string::npos, std::string("developer word '") + bad + "' in label: " + visible);
            }
            CHECK(checked >= 6, fmt("labels checked: %d", checked));
        }
        hide(ui);
    });

    // put the simulated game back as the other cases expect it
    app.mailbox->cancel();
    if (original_auto.is_null()) app.gui_settings.erase("auto");
    else app.gui_settings["auto"] = original_auto;
    app.save_gui_settings();
    write_state(app, ui, le, original_state);
    app.mailbox->cancel();
    ImGui::GetIO().DisplaySize = old_display;
    ui.frames(3);
}

}  // namespace page_career_test
