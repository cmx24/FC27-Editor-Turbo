// Native tests: Players > Names (every name the game shows for a player, ui_names.cpp). Included by test_main.cpp, run
// from test_ui before the playtest fixes; kept in its own file, away from the preset import cases.
#pragma once
#include "ui/ui_names.h"

static void ui_cases_names_tab(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    run_case("UI: Players > Names: the four shown names, saved in place or by Lua, shirt and common name rules, length check, Restore", [&] {
        const auto npos = std::string::npos;
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.request_tab = 0;
        ui.frames(3);
        const Table* pt = app.db.table("players");
        const Table* et = app.db.table("editedplayernames");
        CHECK(pt && et, "players and editedplayernames tables");
        if (!pt || !et) return;
        const uint64_t prec = app.db.find(*pt, "playerid", 3003);
        const uint64_t erec = app.db.find(*et, "playerid", 3003);
        auto put = [&](const char* f, const char* v) { return app.db.set(*et, erec, *et->field(f), Value::of_str(v)); };
        auto row = [&](uint64_t rec, const char* f) {
            Value v;
            return app.db.get(*et, rec, *et->field(f), v) ? v.to_string() : std::string("?");
        };
        auto ids = [&](int64_t first, int64_t last, int64_t common, int64_t jersey) {
            return app.db.set_int(*pt, prec, "firstnameid", first) && app.db.set_int(*pt, prec, "lastnameid", last) &&
                   app.db.set_int(*pt, prec, "commonnameid", common) && app.db.set_int(*pt, prec, "playerjerseynameid", jersey);
        };
        // 3003 as the world made him: ids Generic Generic, no common name id; his row Bo Yu, common name Bobo, shirt BOBO
        CHECK(prec && erec && ids(20, 20, 0, 20) && put("firstname", "Bo") && put("surname", "Yu") && put("commonname", "Bobo") &&
                  put("playerjerseyname", "BOBO"),
              "3003 set up");
        CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
        CHECK(ui.click("3003", "##plist"), "row 3003");
        CHECK(ui.click("Names", "##pedit"), "Names tab");
        ui.frames(1);
        const NamesTabState& st = names_tab_state();
        CHECK(st.playerid == 3003 && st.has_row && st.first == "Bo" && st.last == "Yu" && st.common == "Bobo" && st.jersey == "BOBO",
              "the four names the game shows: " + st.first + " / " + st.last + " / " + st.common + " / " + st.jersey);
        CHECK(ui.find("##nmfirst") && ui.find("##nmlast") && ui.find("##nmcommon") && ui.find("##nmjersey") && ui.find("Save names##nm") &&
                  ui.find("Restore database names##nm") && ui.find("##nmidsearch"),
              "four boxes, Save, Restore and the name id search");
        CHECK(st.import_hint.empty(), "no preset-import hint for a normal row");

        // a player with a row: edited in place, only the field that changed, with an undo step
        const size_t undo0 = app.undo_count(3003);
        CHECK(ui.type_into(ui.find("##nmlast"), "Yuno"), "type a last name");
        CHECK(ui.click("Save names##nm"), "save");
        CHECK(!app.busy() && app.lua_queue.empty(), "nothing sent to Lua");
        CHECK(row(erec, "surname") == "Yuno" && row(erec, "firstname") == "Bo" && row(erec, "commonname") == "Bobo" &&
                  row(erec, "playerjerseyname") == "BOBO",
              "only the last name written in place: " + row(erec, "surname"));
        CHECK(app.undo_count(3003) == undo0 + 1, "one undo step");

        // an empty common name stays empty (never invented): the Players list shows first + last at once
        CHECK(ui.type_into(ui.find("##nmcommon"), ""), "common name emptied");
        CHECK(ui.click("Save names##nm"), "save");
        CHECK(row(erec, "commonname").empty(), "common name written empty: " + row(erec, "commonname"));
        const PlayerRow* pr = app.model.player(3003);
        CHECK(pr && pr->name == "Bo Yuno", "the Players list follows: " + (pr ? pr->name : std::string("?")));
        ui.frames(1);
        CHECK(st.common.empty() && st.common_note.empty(), "the box stays empty (no common name id either)");

        // an empty shirt name takes the last name (never blank); Undo puts it back
        CHECK(ui.type_into(ui.find("##nmjersey"), ""), "shirt name emptied");
        CHECK(ui.click("Save names##nm"), "save");
        CHECK(row(erec, "playerjerseyname") == "Yuno", "shirt name = the last name: " + row(erec, "playerjerseyname"));
        ui.frames(1);
        CHECK(st.jersey == "Yuno", "box refilled: " + st.jersey);
        CHECK(app.undo(3003) && row(erec, "playerjerseyname") == "BOBO", "Undo puts the shirt name back");
        ui.frames(1);
        CHECK(st.jersey == "BOBO", "box follows the undo: " + st.jersey);

        // an over-long name is refused before any write (the test table holds 23 bytes per name)
        CHECK(ui.type_into(ui.find("##nmfirst"), "Bartholomew Maximilian Jr"), "25 bytes typed");
        CHECK(ui.type_into(ui.find("##nmlast"), "Long"), "and another change");
        CHECK(ui.click("Save names##nm"), "save");
        CHECK(st.error.find("First name is too long: 25 bytes, at most 23") == 0, "refused inline: " + st.error);
        CHECK(row(erec, "firstname") == "Bo" && row(erec, "surname") == "Yuno", "nothing written, not even the valid field");

        // Restore database names: the name ids' texts, in place; no common name for commonnameid 0; the shirt name from
        // playerjerseynameid (Kane)
        CHECK(ids(20, 20, 0, 17), "shirt name id 17 (Kane)");
        CHECK(ui.click("Restore database names##nm"), "restore");
        CHECK(row(erec, "firstname") == "Generic" && row(erec, "surname") == "Generic" && row(erec, "commonname").empty() &&
                  row(erec, "playerjerseyname") == "Kane",
              "restored: " + row(erec, "firstname") + " / " + row(erec, "surname") + " / '" + row(erec, "commonname") + "' / " +
                  row(erec, "playerjerseyname"));
        CHECK(app.db.find(*et, "playerid", 3003) == erec, "the row is kept (never deleted)");
        ui.frames(1);
        CHECK(st.error.empty() && st.first == "Generic" && st.jersey == "Kane", "boxes refilled, the refusal gone");

        // a row damaged by the 1.2.0 preset import (first, surname and shirt empty, the full name as common name): a hint,
        // and Restore fixes it (shirt name = the surname when the shirt name id has no text)
        CHECK(ids(18, 19, 0, 0) && put("firstname", "") && put("surname", "") && put("commonname", "Ben White") && put("playerjerseyname", ""),
              "a damaged row");
        ui.frames(2);
        CHECK(st.import_hint.find("preset import") != npos && st.import_hint.find("Restore database names") != npos, "hint: " + st.import_hint);
        CHECK(ui.click("Restore database names##nm"), "restore");
        CHECK(row(erec, "firstname") == "Ben" && row(erec, "surname") == "White" && row(erec, "commonname").empty() &&
                  row(erec, "playerjerseyname") == "White",
              "fixed in place: " + row(erec, "firstname") + " / " + row(erec, "surname") + " / '" + row(erec, "commonname") + "' / " +
                  row(erec, "playerjerseyname"));
        ui.frames(1);
        CHECK(st.import_hint.empty(), "the hint is gone");
        pr = app.model.player(3003);
        CHECK(pr && pr->name == "Ben White", "the Players list follows: " + (pr ? pr->name : std::string("?")));

        // a player without a row (1006, Ben White): set_display_name to Turbo's Lua side with the room check, all four
        // names, the empty shirt name filled with the last name; nothing inserted by the window
        CHECK(app.db.find(*et, "playerid", 1006) == 0, "1006 has no row");
        CHECK(ui.click("1006", "##plist") && ui.click("Names", "##pedit"), "row 1006");
        ui.frames(1);
        CHECK(!st.has_row && st.first == "Ben" && st.last == "White" && st.common.empty() && st.jersey == "White",
              "his names from the name ids: " + st.first + " / " + st.last + " / '" + st.common + "' / " + st.jersey);
        CHECK(ui.type_into(ui.find("##nmfirst"), "Benjamin") && ui.type_into(ui.find("##nmjersey"), ""), "first name typed, shirt emptied");
        CHECK(ui.click("Save names##nm"), "save");
        CHECK(app.busy(), "one command sent");
        {
            json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
            const json acts = cmd.is_discarded() ? json::array() : cmd["overrides"]["actions"];
            CHECK(!cmd.is_discarded() && cmd["module"] == "callnames" && acts.size() == 1, "callnames command: " + acts.dump());
            if (acts.size() == 1) {
                const json& a = acts[0];
                uint32_t used = 0, cap = 0;
                CHECK(app.db.rows_in_use(*et, used, cap), "row count");
                CHECK(a["action"] == "set_display_name" && a["playerid"] == 1006 && a["firstname"] == "Benjamin" && a["surname"] == "White" &&
                          a["commonname"] == "" && a["playerjerseyname"] == "White" && a.value("room", false) &&
                          a.value("capacity", 0u) == cap,
                      "the row with all four names and the room check: " + a.dump());
            }
        }
        CHECK(app.db.find(*et, "playerid", 1006) == 0, "the window inserts nothing");
        CHECK(ui.click("Cancel"), "cancel");
        // a full editedplayernames table: refused, nothing sent
        uint16_t ep_written = 0, ep_cap = 0;
        CHECK(mem.rd(et->header + 0x7C, ep_written) && mem.rd(et->header + 0x78, ep_cap), "header counts");
        CHECK(mem.wr(et->header + 0x78, ep_written) && mem.wr(et->header + 0x7A, ep_written), "editedplayernames made full");
        app.toasts.clear();
        CHECK(ui.click("Save names##nm"), "save on a full table");
        CHECK(!app.busy() && st.error.find("table is full") != npos && ui.toast_contains("never adds a row to a full table"),
              "refused, nothing sent: " + st.error);
        CHECK(mem.wr(et->header + 0x78, ep_cap) && mem.wr(et->header + 0x7A, ep_cap), "capacity restored");

        // back as the world made him
        CHECK(ids(20, 20, 0, 20) && put("firstname", "Bo") && put("surname", "Yu") && put("commonname", "Bobo") && put("playerjerseyname", "BOBO"),
              "3003 restored");
        app.model.refresh_player_name(3003);
    });
}
