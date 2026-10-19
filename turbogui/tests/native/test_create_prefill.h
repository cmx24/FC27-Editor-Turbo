// Native tests (2.19 create-prefill): Players > Create player opens with every box read from the template's database row and a
// Create click sends only what the user changed (core/create_form.h, ui_presets.cpp create_dialog). run_core(): the form's diff
// and the database match of an outside player on synthetic data; run_ui(): the dialog on the GUI world's player 1002 (every box
// equals his row; no edit = an empty "set" and no names; one edit = that one field; a name edit = the four shown names; an
// impossible date = nothing sent), and the commands through Turbo's real Lua side (gui_world.lua commands).
// Included by test_main.cpp after test_player_id.h. Players are referred to by id only.
#pragma once

#include "core/create_form.h"
#include "ui/ui_names.h"
#include "ui/ui_presets.h"

namespace create_prefill_test {

namespace cf = turbo::create_form;

static bool same_form(const cf::Form& a, const cf::Form& b) {
    return a.template_pid == b.template_pid && a.names == b.names && a.has_birthdate == b.has_birthdate && cf::same_date(a.birth, b.birth) &&
           a.ints == b.ints;
}

static void run_core() {
    run_case("create prefill: the diff sends only changed fields; names as a set; an emptied common name is cleared; an impossible date is refused", [&] {
        cf::Form o;
        o.template_pid = 1002;
        o.names = {"Ann", "Bee", "Cee", "BEE"};
        o.has_birthdate = true;
        o.birth = turbo::GameDate{1993, 11, 23};
        o.ints = {{"overallrating", 70}, {"potential", 80}, {"preferredposition1", 25}, {"preferredposition2", -1}, {"height", 180}, {"acceleration", 66}};
        cf::Overrides d = cf::diff(o, o);
        CHECK(d.set.empty() && d.names.empty() && d.clear_names.empty() && d.error.empty(), "no edit: nothing to send");
        json cmd = cf::command(o, d, 4);
        CHECK(cmd.size() == 3 && cmd["set"].empty() && cmd["source"]["playerid"] == 1002 && cmd["teamid"] == 4 && !cmd.contains("names"),
              "no edit: source, team and an empty set only: " + cmd.dump());
        cf::Form c = o;
        c.ints["potential"] = 85;
        d = cf::diff(o, c);
        CHECK(d.set == json({{"potential", 85}}) && d.names.empty(), "one field: " + d.set.dump());
        c = o;
        c.birth.day = 24;
        d = cf::diff(o, c);
        CHECK(d.set.size() == 1 && d.set["birthdate"] == turbo::gregorian_days_from_date(turbo::GameDate{1993, 11, 24}), "birth date by the day: " + d.set.dump());
        c.birth = turbo::GameDate{1993, 2, 30};
        d = cf::diff(o, c);
        CHECK(!d.error.empty() && d.set.empty(), "1993-02-30 refused: " + d.error);
        c = o;
        c.names.last = "Zed";
        d = cf::diff(o, c);
        CHECK(d.set.empty() && d.names == json({{"firstname", "Ann"}, {"surname", "Zed"}, {"commonname", "Cee"}, {"playerjerseyname", "BEE"}}),
              "a name: the four shown names: " + d.names.dump());
        c.names.common.clear();
        d = cf::diff(o, c);
        CHECK(!d.names.contains("commonname") && d.clear_names == std::vector<std::string>{"commonname"}, "an emptied common name is cleared");
        c = o;
        turbo::overall::Attrs arch{};
        arch.fill(50);
        d = cf::diff(o, c, &arch);
        CHECK(d.set == json({{"acceleration", 50}}), "an archetype: its attributes the table has, as previewed: " + d.set.dump());
    });

    run_case("create prefill: an outside player is a database player by id, else by exactly one name + birth date", [&] {
        std::vector<cf::DbPlayer> rows = {{1001, "Ann Bee", 150000}, {1002, "Cee", 150001}, {1003, "Dee Eff", 150002}, {1004, "Dee Eff", 150002}};
        CHECK(cf::match(rows, 1002, "anything", 0) == 1002, "same id");
        CHECK(cf::match(rows, 999999, " Ann Bee ", 150000) == 1001, "name + birth date");
        CHECK(cf::match(rows, 999999, "Ann Bee", 150001) == 0, "another birth date: none");
        CHECK(cf::match(rows, 999999, "ann bee", 150000) == 0, "the exact name only");
        CHECK(cf::match(rows, 999999, "Dee Eff", 150002) == 0, "two players with that name and date: none");
        CHECK(cf::match(rows, 0, "", 0) == 0, "nothing to match");
        CHECK(turbo::birthdate_days("1993-11-23") == turbo::gregorian_days_from_date(turbo::GameDate{1993, 11, 23}) && turbo::birthdate_days("150000") == 150000 &&
                  turbo::birthdate_days("1993-02-30") == 0 && turbo::birthdate_days("") == 0,
              "birth dates as files write them");
    });
}

static void run_ui(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    run_case("UI: Create player opens with every box from the template's row; Create sends only the boxes changed; Lua runs them", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.request_tab = 0;
        ui.frames(3);
        const Table* pt = app.db.table("players");
        CHECK(pt != nullptr, "players table");
        if (!pt) return;
        const int64_t tpl = 1002;
        // the editor's button row is clipped in a small window (as in test_player_id.h): large for this case, put back at its end
        ImGuiWindow* mw = ImGui::FindWindowByName("FC 27 LE Turbo");
        const ImVec2 saved_pos = mw ? mw->Pos : ImVec2(0, 0), saved_size = mw ? mw->SizeFull : ImVec2(0, 0);
        if (mw) {
            ImGui::SetWindowPos(mw, ImVec2(20, 20));
            ImGui::SetWindowSize(mw, ImVec2(1600, 1000));
        }
        ui.frames(3);
        json captured = json::array();
        auto take = [&](const std::string& label) {
            CHECK(app.busy(), "a command queued by '" + label + "'");
            if (!app.busy()) return json();
            json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
            CHECK(!cmd.is_discarded() && cmd["module"] == "create_player", "create_player command");
            captured.push_back({{"label", label}, {"cmd", cmd}});
            CHECK(ui.click("Cancel"), "Cancel after '" + label + "'");
            return cmd.is_discarded() ? json() : cmd["overrides"];
        };
        auto open = [&]() {
            if (ui.find("Cancel##pcreate", "##pcreate")) ui.click("Cancel##pcreate", "##pcreate");
            ui.frames(2);
            app.request_tab = 0;
            ui.frames(2);
            if (!ui.click(std::to_string(tpl), "##plist")) app.sel_player = tpl;
            ui.frames(2);
            for (ImGuiWindow* w : GImGui->Windows)
                if (std::string(w->Name).find("##pedit") != std::string::npos) ImGui::SetScrollY(w, 0.0f);
            ui.frames(2);
            CHECK(app.sel_player == tpl, "template selected");
            CHECK(ui.click("Create player...", "##pedit"), "create dialog");
            ui.frames(2);
        };
        open();
        const turbo::CreatePrefillState& st = turbo::create_prefill_state();
        const uint64_t rec = app.db.find(*pt, "playerid", tpl);
        const turbo::PlayerRow* row = app.model.player(tpl);
        CHECK(rec != 0 && row != nullptr && st.playerid == tpl, "the dialog is on the template");
        if (!row) return;
        // every box = his row: the profile fields, positions and the attributes this table has, his exact birth date, his names
        const std::vector<std::string> fields = cf::shown_fields(*pt);
        int equal = 0;
        for (const std::string& f : fields) {
            const bool same = st.orig.has(f) && st.orig.get(f) == app.db.get_int(*pt, rec, f, 0) && st.cur.get(f) == st.orig.get(f);
            CHECK(same, "box " + f + " = his row");
            equal += same;
        }
        CHECK(equal == static_cast<int>(fields.size()) && fields.size() >= 12, fmt("%d of %zu boxes equal his row", equal, fields.size()));
        const turbo::GameDate born = turbo::date_from_gregorian_days(app.db.get_int(*pt, rec, "birthdate", 0));
        CHECK(st.orig.has_birthdate && cf::same_date(st.orig.birth, born), fmt("birth date %04d-%02d-%02d", born.year, born.month, born.day));
        const turbo::ShownName sn = turbo::shown_name(app, *pt, *row);
        CHECK(st.orig.names == cf::Names({sn.first, sn.last, sn.common, sn.jersey}) && !(sn.first.empty() && sn.last.empty() && sn.common.empty()),
              "names as the game shows them: " + sn.first + " / " + sn.last + " / " + sn.common + " / " + sn.jersey);
        CHECK(same_form(st.orig, st.cur) && st.pending.empty(), "nothing changed yet");

        // 1. Create without an edit: only the source, the club and an empty set
        CHECK(ui.click("Create player", "##pcreate"), "Create (no edit)");
        json o = take("Create player (no edit)");
        CHECK(o.is_object() && o["set"].is_object() && o["set"].empty() && !o.contains("names") && !o.contains("clear_names") &&
                  o["source"]["playerid"] == tpl && o.contains("teamid"),
              "no edit: " + o.dump());

        // 2. one field: only that one
        open();
        CHECK(same_form(st.orig, st.cur), "reopened: a fresh snapshot");
        const int64_t pot = st.orig.get("potential");
        const int64_t want = pot == 90 ? 91 : 90;
        CHECK(ui.type_into(ui.find("##potential", "##pcreate"), std::to_string(want)), "potential");
        CHECK(ui.click("Create player", "##pcreate"), "Create (potential)");
        o = take("Create player (potential)");
        CHECK(o["set"] == json({{"potential", want}}) && !o.contains("names"), "one field: " + o.dump());

        // 3. a reopen forgets the last edits (his row again)
        open();
        CHECK(st.cur.get("potential") == pot, "the potential box is his again");

        // 4. birth date by the day; an impossible date sends nothing
        const int new_day = born.day == 28 ? 27 : (born.day % 28) + 1;
        CHECK(ui.type_into(ui.find("##bday", "##pcreate"), std::to_string(new_day)), "birth day");
        CHECK(ui.click("Create player", "##pcreate"), "Create (birth day)");
        o = take("Create player (birth day)");
        CHECK(o["set"].size() == 1 && o["set"]["birthdate"] == turbo::gregorian_days_from_date(turbo::GameDate{born.year, born.month, new_day}),
              "the birth date only: " + o.dump());
        open();
        CHECK(ui.type_into(ui.find("##bmonth", "##pcreate"), "2") && ui.type_into(ui.find("##bday", "##pcreate"), "30"), "30 February");
        ui.click("Create player", "##pcreate");
        CHECK(!app.busy(), "an impossible date: Create disabled, nothing sent");

        // 5. a name: the four shown names, the shirt following the new surname; no players field
        open();
        CHECK(ui.type_into(ui.find("##last", "##pcreate"), "Zed"), "surname");
        CHECK(st.cur.names.jersey == "Zed", "the jersey name box follows the new surname: " + st.cur.names.jersey);
        CHECK(ui.click("Create player", "##pcreate"), "Create (surname)");
        o = take("Create player (surname)");
        CHECK(o["set"].empty() && o.contains("names") && o["names"]["surname"] == "Zed" && o["names"]["playerjerseyname"] == "Zed" &&
                  (sn.first.empty() || o["names"]["firstname"] == sn.first),
              "names: " + o.dump());

        // 6. an attribute box: that attribute only
        open();
        if (st.orig.has("acceleration")) {
            ui.click("His attributes (edit any)###cattrs", "##pcreate");
            ui.frames(1);
            const int64_t acc = st.orig.get("acceleration");
            const int64_t acc_want = acc == 77 ? 78 : 77;
            CHECK(ui.type_into(ui.find("##ca_acceleration", "##pcreate"), std::to_string(acc_want)), "acceleration box");
            ui.click("His attributes (edit any)###cattrs", "##pcreate");  // closed: the dialog fits the screen again (the edit is kept)
            ui.frames(2);
            CHECK(st.cur.get("acceleration") == acc_want, "the edit is kept with the attributes closed");
            CHECK(ui.click("Create player", "##pcreate"), "Create (acceleration)");
            o = take("Create player (acceleration)");
            CHECK(o["set"] == json({{"acceleration", acc_want}}), "one attribute: " + o.dump());
            captured.erase(captured.size() - 1);  // the Lua command world has no attribute columns: this one is checked in the GUI only
            open();
        }
        CHECK(ui.click("Cancel##pcreate", "##pcreate"), "close the dialog");

        // through Turbo's real Lua side: every command creates the player
        std::ofstream(g_out / "gui_commands.json") << captured.dump(1);
        CHECK(run_lua("commands") == 0, "gui_world.lua commands");
        json results = read_json(g_out / "gui_commands_out.json");
        CHECK(results.size() == captured.size(), fmt("a result for every command (%zu of %zu)", results.size(), captured.size()));
        for (size_t i = 0; i < results.size(); ++i) {
            const std::string t = results[i].value("text", std::string());
            CHECK(results[i].value("ok", false) && t.find("new player") != std::string::npos, captured[i]["label"].get<std::string>() + ": " + t);
        }
        if (mw) {
            ImGui::SetWindowPos(mw, saved_pos);
            ImGui::SetWindowSize(mw, saved_size);
        }
        ui.frames(3);
    });
}

}  // namespace create_prefill_test
