// Native tests (4.0 GUI overhaul, slice S7b): the Development block (XP multiplier, Bonus XP, No decline) re-homed from Lab > Bulk edit
// (ui_tools.cpp section_bulk_edit) to Players > Bulk edit (ui_bulk_edit.cpp section_actions). Included by test_main.cpp after
// test_profile_40.h; bulkdev40_test::ui_cases runs inside the UI block, a real App, drawing the page through the Ui hook g_ui_extra in a
// window "##bulkdev40_test". GUI only: the page builds the same "development" action as the Lab page and sends it to the same bulk_edit module.
//
// What is guarded:
//   - the three controls are on Bulk edit (the Development box; once it is ticked XP multiplier and Bonus XP; No decline), all OFF by default
//     (core/team_preset.h growth defaults): nothing is sent for a page nobody touched, and ticking only Development sends no_decline=false.
//   - ticking Development puts actions.development = {xp_multiplier, bonus_xp, no_decline} (the ui_tools.cpp keys) into the command the
//     "Send to the picked players" button submits, and nothing else (no fitness / form / morale); ticking No decline flips only that key.
// FAILS on 7e888096 (S7 tip): the page has no Development box.
#pragma once
#include "ui/ui_bulk_edit.h"
#include "ui/ui_page.h"
#include "ui/ui_player_creation.h"
#include "ui/ui_diagnostics_page.h"
#include <algorithm>

namespace bulkdev40_test {

using namespace turbo;

static void (*g_page)(App&) = draw_bulk_edit;  // the page the hook window draws
static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##bulkdev40_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        g_page(app);
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

// The newest "sent: {...}" line of the in-app log as JSON (discarded when there is none)
static json last_sent(App& app) {
    for (auto it = app.log_lines.rbegin(); it != app.log_lines.rend(); ++it)
        if (it->rfind("sent: ", 0) == 0) return json::parse(it->substr(6), nullptr, false);
    return json(json::value_t::discarded);
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    const char* W = "##bulkdev40_test";

    run_case("S7b Bulk edit: the Development block (XP multiplier, Bonus XP, No decline) is on the page, OFF by default, and sends the ui_tools development action", [&] {
        if (app.busy()) app.mailbox->cancel();
        app.lua_queue.clear();
        bulkedit::reset();
        bulkedit::pick_only(app, {1001, 1005});
        app.log_lines.clear();
        show(ui);
        ui.click("###bk_actions_head", W);
        ui.frames(3);
        {   // fix round 2: the collapsed header names Development, so a user can see the block is inside it
            const auto* head = ui.find("###bk_actions_head", W);
            CHECK(head != nullptr && head->label.find("development") != std::string::npos,
                  "the Bulk edit actions header names development: " + (head ? head->label : std::string("header not drawn")));
        }
        CHECK(ui.find("Development###bk_dev_on", W) != nullptr, "the Development box is on Bulk edit");
        // OFF by default: the three controls only open with the box, and nothing is ready to send
        CHECK(ui.find("##bk_xpmult", W) == nullptr && ui.find("##bk_bonusxp", W) == nullptr && ui.find("###bk_nodecline", W) == nullptr,
              "Development is off: XP multiplier, Bonus XP and No decline are not open");
        CHECK(!bulkedit::state().actions_why.empty(), "nothing ticked: Send is greyed out: " + bulkedit::state().actions_why);
        CHECK(last_sent(app).is_discarded(), "nothing was sent by drawing the page");

        // tick Development: the three controls appear
        CHECK(ui.click("Development###bk_dev_on", W), "click the Development box");
        ui.frames(3);
        CHECK(ui.find("##bk_xpmult", W) != nullptr, "XP multiplier");
        CHECK(ui.find("##bk_bonusxp", W) != nullptr, "Bonus XP");
        CHECK(ui.find("No decline###bk_nodecline", W) != nullptr, "No decline");
        CHECK(bulkedit::state().actions_why.find("tick what to set") == std::string::npos, "Development counts as a ticked action: " + bulkedit::state().actions_why);
        CHECK(bulkedit::state().actions_why.empty(), "Send can run (career, players picked): " + bulkedit::state().actions_why);

        // Send: the command is the bulk_edit module with the ui_tools "development" keys, No decline OFF
        CHECK(ui.click("Send to the picked players###bk_actions_go", W), "click Send");
        ui.frames(2);
        json cmd = last_sent(app);
        CHECK(!cmd.is_discarded(), "a command was sent");
        if (!cmd.is_discarded()) {
            CHECK(cmd.value("op", "") == "run" && cmd.value("module", "") == "bulk_edit", "module bulk_edit: " + cmd.dump());
            const json& a = cmd["overrides"]["actions"];
            CHECK(a.size() == 1 && a.contains("development"), "only the development action: " + a.dump());
            if (a.contains("development")) {
                const json& d = a["development"];
                CHECK(d.size() == 3 && d.contains("xp_multiplier") && d.contains("bonus_xp") && d.contains("no_decline"),
                      "the ui_tools.cpp keys: " + d.dump());
                CHECK(d.value("xp_multiplier", -1.0) == 2.0 && d.value("bonus_xp", -1) == 0, "the ui_tools values (x2.0, +0): " + d.dump());
                CHECK(d.value("no_decline", true) == false, "No decline is OFF by default: " + d.dump());
            }
            CHECK(cmd["overrides"]["scope"]["playerids"].size() == 2, "the two picked players: " + cmd["overrides"]["scope"].dump());
        }
        CHECK(app.pending_label.find("development") != std::string::npos, "the pending label names it: " + app.pending_label);
        app.mailbox->cancel();

        // tick No decline: only that key flips
        app.log_lines.clear();
        CHECK(ui.click("No decline###bk_nodecline", W), "click No decline");
        ui.frames(3);
        CHECK(ui.click("Send to the picked players###bk_actions_go", W), "click Send again");
        ui.frames(2);
        cmd = last_sent(app);
        CHECK(!cmd.is_discarded() && cmd["overrides"]["actions"]["development"].value("no_decline", false) == true,
              "No decline ticked: " + (cmd.is_discarded() ? std::string("nothing sent") : cmd.dump()));
        app.mailbox->cancel();

        // a reset puts every box back OFF
        hide(ui);
        bulkedit::reset();
        app.log_lines.clear();
        show(ui);
        ui.click("###bk_actions_head", W);
        ui.frames(3);
        CHECK(ui.find("##bk_xpmult", W) == nullptr && ui.find("###bk_nodecline", W) == nullptr, "after reset Development is off again");
        hide(ui);
        bulkedit::reset();
    });

    // Review round 1 (D2, D3): the send gate of section_actions (ui_bulk_edit.cpp) and the pending label.
    //   (a) form_morale unavailable, only Development ticked: the form_morale reason does not apply, Send stays enabled and sends.
    //       (With Fitness ticked the same reason greys Send out: the gate still guards fitness, form and morale.)
    //   (b) development_xp unavailable: Development ticked greys Send out with the "Development: <reason>" text; nothing is sent.
    //   (c) the label names only the ticked boxes.
    // FAILS on a tip whose gate tests form_morale unconditionally ((a) Send is greyed out) and on a tip whose label is the fixed
    // "fitness, form, morale[, development]" text ((c)). On 7e888096 (no Development block) it fails at "the Development box is on Bulk edit".
    run_case("S7b Bulk edit: the send gate follows the ticked boxes (form_morale / development_xp) and the label names only what is ticked", [&] {
        if (app.busy()) app.mailbox->cancel();
        app.lua_queue.clear();
        const fs::path state_file = le / "turbo_output" / "bridge_state.json";
        const json original = read_json(state_file);
        static int bumps = 0;
        auto write_unavailable = [&](const json& unavailable) {
            json st = original;
            st["unavailable"] = unavailable;
            st["seq"] = st.value("seq", 0LL) + 1000 + ++bumps;
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(900 + 2 * bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        json base_unavailable = original.contains("unavailable") && original["unavailable"].is_object() ? original["unavailable"] : json::object();
        base_unavailable.erase("form_morale");
        base_unavailable.erase("development_xp");
        auto fresh_page = [&] {
            hide(ui);
            bulkedit::reset();
            bulkedit::pick_only(app, {1001, 1005});
            app.log_lines.clear();
            show(ui);
            if (!ui.find("Development###bk_dev_on", W)) {
                ui.click("###bk_actions_head", W);
                ui.frames(3);
            }
        };

        // (a) form_morale unavailable, Development only
        json u = base_unavailable;
        u["form_morale"] = "needs the form and morale natives";
        write_unavailable(u);
        CHECK(app.bridge.state().unavailable_reason("form_morale") != nullptr, "form_morale is unavailable in this state");
        CHECK(app.bridge.state().unavailable_reason("development_xp") == nullptr, "development_xp is available in this state");
        fresh_page();
        CHECK(ui.find("Development###bk_dev_on", W) != nullptr, "the Development box is on Bulk edit");
        CHECK(ui.click("Development###bk_dev_on", W), "tick Development");
        ui.frames(3);
        CHECK(bulkedit::state().actions_why.empty(), "(a) form_morale unavailable, Development only: Send is enabled: " + bulkedit::state().actions_why);
        CHECK(ui.click("Send to the picked players###bk_actions_go", W), "(a) click Send");
        ui.frames(2);
        json cmd = last_sent(app);
        CHECK(!cmd.is_discarded() && cmd["overrides"]["actions"].contains("development"),
              "(a) the development action went out: " + (cmd.is_discarded() ? std::string("nothing sent") : cmd.dump()));
        CHECK(app.pending_label == "Bulk edit: development", "(c) a Development-only send is labelled 'Bulk edit: development': '" + app.pending_label + "'");
        app.mailbox->cancel();
        // the same reason still guards Fitness
        CHECK(ui.click("Fitness###bk_fit_on", W), "tick Fitness too");
        ui.frames(3);
        CHECK(bulkedit::state().actions_why == "needs the form and morale natives", "(a) Fitness ticked: the form_morale reason greys Send out: '" + bulkedit::state().actions_why + "'");

        // (b) development_xp unavailable, form_morale available
        u = base_unavailable;
        u["development_xp"] = "needs the development natives";
        write_unavailable(u);
        CHECK(app.bridge.state().unavailable_reason("form_morale") == nullptr && app.bridge.state().unavailable_reason("development_xp") != nullptr,
              "development_xp is the only unavailable one");
        fresh_page();
        CHECK(ui.click("Development###bk_dev_on", W), "tick Development");
        ui.frames(3);
        CHECK(bulkedit::state().actions_why == "Development: needs the development natives",
              "(b) development_xp unavailable: Send is greyed out with the Development reason: '" + bulkedit::state().actions_why + "'");
        app.log_lines.clear();
        ui.click("Send to the picked players###bk_actions_go", W);
        ui.frames(2);
        CHECK(last_sent(app).is_discarded(), "(b) the greyed-out Send sends nothing");
        // without Development the same state sends fitness
        CHECK(ui.click("Development###bk_dev_on", W), "untick Development");
        CHECK(ui.click("Fitness###bk_fit_on", W), "tick Fitness");
        ui.frames(3);
        CHECK(bulkedit::state().actions_why.empty(), "(b) Fitness only: Send is enabled: " + bulkedit::state().actions_why);
        CHECK(ui.click("Send to the picked players###bk_actions_go", W), "click Send");
        ui.frames(2);
        CHECK(app.pending_label == "Bulk edit: fitness", "(c) a Fitness-only send is labelled 'Bulk edit: fitness': '" + app.pending_label + "'");
        app.mailbox->cancel();

        // (c) several boxes: the ticked ones in a fixed order
        write_unavailable(base_unavailable);
        fresh_page();
        CHECK(ui.click("Morale###bk_morale_on", W), "tick Morale");
        CHECK(ui.click("Fitness###bk_fit_on", W), "tick Fitness");
        CHECK(ui.click("Development###bk_dev_on", W), "tick Development");
        ui.frames(3);
        CHECK(bulkedit::state().actions_why.empty(), "all available: Send is enabled: " + bulkedit::state().actions_why);
        CHECK(ui.click("Send to the picked players###bk_actions_go", W), "click Send");
        ui.frames(2);
        CHECK(app.pending_label == "Bulk edit: fitness, morale, development", "(c) the label lists the three ticked boxes: '" + app.pending_label + "'");
        app.mailbox->cancel();

        // put the state file back the way it was
        hide(ui);
        bulkedit::reset();
        write_unavailable(original.contains("unavailable") && original["unavailable"].is_object() ? original["unavailable"] : json::object());
    });

    // Commit 2 (S7b): the bar Undo is off on Bulk edit and Player creation (each page::Spec sets show_undo=false), and every other
    // Frame page keeps it (Data export, the S1 worked example, is the witness). Bulk edit has its own ###bk_undo that restores the whole run;
    // the bar Undo took back only the newest Changes row (one player's edits; changes::poll folds the per-field steps into one row per player).
    // FAILS on 7e888096 (S7 tip): both pages draw ###pg_undo.
    run_case("S7b page::Frame: no bar Undo on Bulk edit and Player creation, the bar Undo stays on Data export", [&] {
        if (app.busy()) app.mailbox->cancel();
        app.lua_queue.clear();
        app.sel_player = 0;
        bulkedit::reset();
        struct PageCase { const char* id; void (*fn)(App&); bool undo; };
        for (const PageCase& pc : {PageCase{"bulk_edit", draw_bulk_edit, false}, PageCase{"player_creation", draw_player_creation, false},
                                   PageCase{"data_export", draw_data_export_page, true}}) {
            page::reset_for_tests();
            g_page = pc.fn;
            show(ui);
            const page::Record* r = page::last_record(pc.id);
            const std::string who = pc.id;
            CHECK(r != nullptr, who + ": the page drew a page::Frame");
            if (r) {
                const bool in_bar = std::find(r->bar.begin(), r->bar.end(), "###pg_undo") != r->bar.end();
                CHECK(in_bar == pc.undo, who + ": ###pg_undo in the bar = " + (pc.undo ? "yes" : "no"));
                CHECK(!r->bar.empty() && r->bar.back() == "###pg_back", who + ": Back is still the last button");
                if (!pc.undo) CHECK(!r->undo_enabled, who + ": the Record says the bar Undo is off");
            }
            CHECK((ui.find("###pg_undo", "##bulkdev40_test") != nullptr) == pc.undo, who + ": the Undo button is " + (pc.undo ? "drawn" : "not drawn"));
            CHECK(ui.find("###pg_back", "##bulkdev40_test") != nullptr, who + ": Back is drawn");
            hide(ui);
        }
        // Bulk edit keeps its own Undo of the whole run
        g_page = draw_bulk_edit;
        show(ui);
        CHECK(ui.find("###bk_undo", "##bulkdev40_test") != nullptr, "Bulk edit keeps its own Undo (###bk_undo)");
        hide(ui);
        g_page = draw_bulk_edit;
        bulkedit::reset();
    });
}

}  // namespace bulkdev40_test
