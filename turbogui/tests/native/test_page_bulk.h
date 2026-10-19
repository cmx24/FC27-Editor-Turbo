// Native tests (2.20, brief 2.20-IA-pages, track "bulk"): Players > Bulk edit (ui/ui_bulk_edit.cpp). Included by test_main.cpp after
// test_page_advanced.h; page_bulk_test::ui_cases runs inside the UI block, a real App over the synthetic database. The page is not
// registered with the shell (the wiring owner does that), so it is drawn through the Ui hook g_ui_extra: a separate window
// "##bulkpage_test" drawn after the app's own frame, as test_page_advanced.h does.
//
// What is guarded: the value rules (clamp, change by, PlayStyle give / take away); the field list offers every Int field of the
// players table with a plain title and no player ID or name id; the clicks (Add field -> a field -> its row); and the Tester's W3
// check done offline: Jersey fit on two players, read back, Undo puts both back; a refused value leaves the player alone.
#pragma once
#include "ui/ui_bulk_edit.h"

namespace page_bulk_test {

using namespace turbo;

static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##bulkpage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) draw_bulk_edit(app);
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
static int64_t read_field(App& app, int64_t pid, const char* field) {
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(pid);
    return t && p ? app.db.get_int(*t, p->rec, field, -999) : -999;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    const char* W = "##bulkpage_test";

    run_case("Bulk edit: the value rules (set clamps, change by clamps, PlayStyle give / take away) (pure)", [&] {
        bulkedit::Row r;
        r.field = "x";
        r.mode = bulkedit::Mode::Set;
        r.value = 500;
        CHECK(bulkedit::new_value(r, 10, 0, 99) == 99, "set above the range: the top");
        r.value = -4;
        CHECK(bulkedit::new_value(r, 10, 0, 99) == 0, "set below the range: the bottom");
        r.mode = bulkedit::Mode::Add;
        r.delta = 5;
        CHECK(bulkedit::new_value(r, 96, 0, 99) == 99 && bulkedit::new_value(r, 50, 0, 99) == 55, "change by +5 stays inside the range");
        r.delta = -5;
        CHECK(bulkedit::new_value(r, 3, 0, 99) == 0 && bulkedit::new_value(r, 50, 0, 99) == 45, "change by -5 stays inside the range");
        r.mode = bulkedit::Mode::Give;
        r.bit = 3;
        CHECK(bulkedit::new_value(r, 0, 0, (1 << 30) - 1) == 8, "give: the bit is set");
        CHECK(bulkedit::new_value(r, 8, 0, (1 << 30) - 1) == 8, "give twice: no change");
        r.mode = bulkedit::Mode::Take;
        CHECK(bulkedit::new_value(r, 9, 0, (1 << 30) - 1) == 1, "take away: only that bit goes");
    });

    run_case("Bulk edit: the field list offers the editor's fields with plain titles, no player ID and no name ids", [&] {
        bulkedit::reset();
        const std::vector<std::string> list = bulkedit::field_list(app);
        CHECK(list.size() > 40, "a long list: " + std::to_string(list.size()));
        auto has = [&](const char* n) { return std::find(list.begin(), list.end(), n) != list.end(); };
        CHECK(has("jerseyfit") && has("overallrating") && has("potential") && has("preferredposition1") && has("contractvaliduntil"), "the common fields are offered");
        CHECK(!has("playerid") && !has("firstnameid") && !has("lastnameid") && !has("commonnameid") && !has("playerjerseynameid"), "no player ID and no name id");
        CHECK(!bulkedit::field_offered("playerid") && bulkedit::field_offered("jerseyfit"), "field_offered agrees");
        CHECK(bulkedit::field_title_of("jerseyfit") == "Jersey fit", "plain title: " + bulkedit::field_title_of("jerseyfit"));
        size_t raw = 0;
        for (const std::string& n : list) {
            const std::string title = bulkedit::field_title_of(n);
            if (title.empty()) CHECK(false, "a field with no title: " + n);
            if (title == n) ++raw;  // a title equal to its code name
        }
        CHECK(raw == 0 || raw < list.size() / 20, "almost every title is a plain name, not the code: " + std::to_string(raw) + " raw");
        CHECK(bulkedit::field_group("jerseyfit") == "Looks" && bulkedit::field_group("overallrating") == "Rating", "grouped for the list");
        CHECK(!bulkedit::add_field(app, "playerid") && !bulkedit::add_field(app, "no_such_field"), "an excluded or unknown field is refused");
    });

    run_case("Bulk edit: the page draws; Add field lists the fields; a click adds the row with its picker", [&] {
        if (app.busy()) ui.click("Cancel");
        bulkedit::reset();
        show(ui);
        CHECK(ui.find("##bk_search", W) != nullptr, "search box");
        CHECK(ui.find("Add field###bk_addfield", W) != nullptr, "Add field");
        CHECK(ui.find("Apply to 0 players###bk_apply", W) == nullptr || bulkedit::state().selected > 0, "Apply names the count");
        CHECK(bulkedit::state().matched > 3 && bulkedit::state().selected == bulkedit::state().matched, "the filters match every player at first: " + std::to_string(bulkedit::state().matched));
        CHECK(!bulkedit::state().why_not.empty(), "Apply is greyed out before a field is added: " + bulkedit::state().why_not);
        CHECK(ui.click("Add field###bk_addfield", W), "click Add field");
        ui.frames(3);
        CHECK(ui.type_into(ui.find("##bk_fieldfilter"), "jersey"), "type in the field search");
        ui.frames(2);
        const ItemRec* item = ui.find("Jersey fit##jerseyfit");
        CHECK(item != nullptr, "the list shows Jersey fit by its plain name");
        if (item) {
            ui.click(item);
            ui.frames(3);
        }
        CHECK(bulkedit::row_of("jerseyfit") != nullptr, "the row was added");
        CHECK(bulkedit::state().rows == 1, "one chosen field");
        CHECK(ui.find("##bk_code", W) != nullptr, "Jersey fit has a name list picker");
        CHECK(ui.find("Remove###bk_remove", W) != nullptr, "Remove");
        CHECK(ui.click("Remove###bk_remove", W), "click Remove");
        ui.frames(3);
        CHECK(bulkedit::row_of("jerseyfit") == nullptr, "the row is gone");
        hide(ui);
        bulkedit::reset();
    });

    run_case("Bulk edit: Jersey fit on two players, read back, Undo puts both back (the W3 check offline)", [&] {
        if (app.busy()) ui.click("Cancel");
        bulkedit::reset();
        const int64_t a = 1001, b = 1005;
        const int64_t a0 = read_field(app, a, "jerseyfit"), b0 = read_field(app, b, "jerseyfit");
        CHECK(a0 != -999 && b0 != -999, "the fixture players hold a jersey fit");
        int64_t v = 0;
        while (v == a0 || v == b0) ++v;
        const int64_t other = 1002 + (a == 1002 ? 1 : 0);
        const int64_t other0 = read_field(app, other, "jerseyfit");
        CHECK(bulkedit::add_field(app, "jerseyfit"), "Add field");
        bulkedit::Row* r = bulkedit::row_of("jerseyfit");
        CHECK(r != nullptr, "row");
        if (!r) return;
        r->value = v;
        bulkedit::pick_only(app, {a, b});
        CHECK(bulkedit::picked_ids().size() == 2, "two players picked");
        // the preview names the change in words
        show(ui);
        bool shows = false;
        for (const std::string& l : bulkedit::state().preview) shows = shows || (l.find("Jersey fit") != std::string::npos);
        CHECK(shows, "the preview names the field");
        CHECK(bulkedit::state().why_not.empty(), "Apply can run: " + bulkedit::state().why_not);
        CHECK(ui.find("Apply to 2 players###bk_apply", W) != nullptr, "the Apply button says 2 players");
        hide(ui);
        CHECK(bulkedit::start_apply(app), "Apply starts");
        bulkedit::run_to_end(app);
        CHECK(read_field(app, a, "jerseyfit") == v && read_field(app, b, "jerseyfit") == v, "both players read back the new value");
        CHECK(read_field(app, other, "jerseyfit") == other0, "a player who was not picked is untouched");
        {   // the log line the live check reads: "<Name>: Jersey fit <before> -> <after>  [players.jerseyfit = <value>]"
            int n = 0;
            for (const std::string& l : app.log_lines)
                if (l.find("[players.jerseyfit = " + std::to_string(v) + "]") != std::string::npos && l.find("Jersey fit") != std::string::npos) ++n;
            CHECK(n >= 2, "the log has one line per player: " + std::to_string(n));
        }
        CHECK(bulkedit::state().result.find("Changed 2 players") != std::string::npos, "the result line: " + bulkedit::state().result);
        CHECK(bulkedit::state().undo_players == 2 && bulkedit::state().undo_what == "Jersey fit", "Undo knows what it puts back");
        CHECK(bulkedit::start_undo(app), "Undo starts");
        bulkedit::run_to_end(app);
        CHECK(read_field(app, a, "jerseyfit") == a0 && read_field(app, b, "jerseyfit") == b0, "Undo put both back");
        CHECK(!bulkedit::start_undo(app), "nothing left to undo");
        // applying the value they already hold changes nobody
        r = bulkedit::row_of("jerseyfit");
        r->value = a0;
        bulkedit::pick_only(app, {a});
        CHECK(bulkedit::start_apply(app), "apply the held value");
        bulkedit::run_to_end(app);
        CHECK(bulkedit::state().result.find("Changed 0 players") != std::string::npos, "an unchanged value changes nobody: " + bulkedit::state().result);
        bulkedit::reset();
    });

    run_case("Bulk edit: change by on potential, and the overall follows an attribute", [&] {
        if (app.busy()) ui.click("Cancel");
        bulkedit::reset();
        const int64_t a = 1001;
        const int64_t p0 = read_field(app, a, "potential");
        CHECK(bulkedit::add_field(app, "potential"), "Add field");
        bulkedit::Row* r = bulkedit::row_of("potential");
        CHECK(r != nullptr, "row");
        if (!r) return;
        r->mode = bulkedit::Mode::Add;
        r->delta = p0 >= 90 ? -3 : 3;
        bulkedit::pick_only(app, {a});
        CHECK(bulkedit::start_apply(app), "start");
        bulkedit::run_to_end(app);
        CHECK(read_field(app, a, "potential") == p0 + r->delta, "potential moved by the change");
        CHECK(bulkedit::start_undo(app), "undo starts");
        bulkedit::run_to_end(app);
        CHECK(read_field(app, a, "potential") == p0, "potential is back");
        // an attribute: the overall follows (the editor's own rule), and Undo restores both
        const int64_t o0 = read_field(app, a, "overallrating"), s0 = read_field(app, a, "shortpassing");
        bulkedit::remove_field("potential");
        CHECK(bulkedit::add_field(app, "shortpassing"), "Add field: an attribute");
        r = bulkedit::row_of("shortpassing");
        if (!r) return;
        r->mode = bulkedit::Mode::Set;
        r->value = s0 > 60 ? 35 : 90;
        bulkedit::pick_only(app, {a});
        CHECK(bulkedit::start_apply(app), "start");
        bulkedit::run_to_end(app);
        CHECK(read_field(app, a, "shortpassing") == r->value, "the attribute is written");
        CHECK(bulkedit::start_undo(app), "undo starts");
        bulkedit::run_to_end(app);
        CHECK(read_field(app, a, "shortpassing") == s0 && read_field(app, a, "overallrating") == o0, "attribute and overall are back");
        bulkedit::reset();
    });

    run_case("Bulk edit: Apply stays greyed out for no player, a nation not chosen, and a big group without the tick", [&] {
        if (app.busy()) ui.click("Cancel");
        bulkedit::reset();
        show(ui);
        CHECK(!bulkedit::state().why_not.empty(), "no field: greyed out");
        bulkedit::add_field(app, "jerseyfit");
        bulkedit::pick_only(app, {});
        ui.frames(2);
        CHECK(bulkedit::state().why_not.find("no player") != std::string::npos, "nobody picked: " + bulkedit::state().why_not);
        CHECK(!bulkedit::start_apply(app), "Apply refuses to start");
        bulkedit::remove_field("jerseyfit");
        bulkedit::add_field(app, "nationality");
        bulkedit::select_all_matching(app);
        ui.frames(2);
        CHECK(bulkedit::state().why_not.find("nationality") == std::string::npos && bulkedit::state().why_not.find("Nationality") != std::string::npos,
              "a nation still to choose: " + bulkedit::state().why_not);
        hide(ui);
        bulkedit::reset();
    });

    run_case("Bulk edit: the My team preset block and the career actions block say why they are greyed out", [&] {
        if (app.busy()) ui.click("Cancel");
        bulkedit::reset();
        show(ui);
        ui.click("###bk_preset_head", W);
        ui.click("###bk_actions_head", W);
        ui.frames(3);
        const bulkedit::State& st = bulkedit::state();
        CHECK(!st.preset_line.empty(), "the preset block states its state: " + st.preset_line);
        CHECK(st.preset_enabled == st.preset_line.empty() || st.preset_enabled == (st.preset_line == "ready"), "enabled <=> ready");
        CHECK(ui.find("My team preset###bk_preset", W) != nullptr, "the preset button is on the page");
        CHECK(ui.find("Send to the picked players###bk_actions_go", W) != nullptr, "the career actions button is on the page");
        CHECK(!st.actions_why.empty(), "nothing ticked: Send is greyed out: " + st.actions_why);
        hide(ui);
        bulkedit::reset();
    });
}

}  // namespace page_bulk_test
