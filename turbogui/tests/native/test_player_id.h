// Native tests (2.3): the Player ID box of Players > Create player (core/player_id.h, ui_presets.cpp): the verdict for a typed
// id (free / used in a per-player table / out of range / not a number / player career / kill switch), what the game files
// hold for it (synthetic catalog tests/fixtures/asset_catalog_v1_small.json: 3D head, callname, miniface, generic head,
// stale catalog, hidden assets off), the "Use a hidden player's id" list, the scan of the GUI world, and the UI: typing,
// the picker, the overrides the Create button sends and what Turbo's real Lua side does with them (gui_world.lua commands).
// Included by test_main.cpp after test_asset_app.h. No real player name anywhere: the fixture's names are synthetic.
#pragma once
#include <chrono>
#include <thread>

#include "core/asset_catalog.h"
#include "core/player_id.h"
#include "ui/ui_presets.h"

static void test_player_id_ui(App& app, Ui& ui, SimMemory& mem, uint64_t kMb, const fs::path& le) {
    namespace pid = turbo::player_id;
    const std::string fixture = "tests/fixtures/asset_catalog_v1_small.json";
    const std::string key = "0000AAAA-0000BBBB";  // the fixture's build
    auto has = [](const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; };

    run_case("player id: the verdict for a typed id (free, used in any per-player table, out of range, not a number, player career, kill switch, notes)", [&] {
        pid::Use u;
        u.read = true;
        u.ids["players"] = {1001, 1002};
        u.ids["teamplayerlinks"] = {1001};
        u.ids["playerloans"] = {300201};
        u.ids["career_playerawards"] = {300202};
        u.ids["playernamemap"] = {300300};
        pid::Verdict v = pid::check("", u, nullptr, "", false);
        CHECK(v.ok && v.automatic && v.id == 0, "empty box = automatic");
        v = pid::check("  300100 ", u, nullptr, "", false);
        CHECK(v.ok && !v.automatic && v.id == 300100 && v.reason.empty(), "a free id (blanks around it are fine)");
        CHECK(has(v.assets_line, "not checked"), "no catalog: the game files are not checked: " + v.assets_line);
        v = pid::check("1001", u, nullptr, "", false);
        CHECK(!v.ok && has(v.reason, "player id 1001 is in use: players, teamplayerlinks have rows") && v.owners.size() == 2, "used: " + v.reason);
        v = pid::check("300201", u, nullptr, "", false);
        CHECK(!v.ok && has(v.reason, "playerloans has a row"), "a loan row of an id no players row has: " + v.reason);
        v = pid::check("300202", u, nullptr, "", false);
        CHECK(!v.ok && has(v.reason, "career_playerawards has a row"), "a career table: " + v.reason);
        for (const char* t : {"0", "460000", "999999", "1234567890"}) {
            v = pid::check(t, u, nullptr, "", false);
            CHECK(!v.ok && has(v.reason, "out of range: 1 to 459,999 (460,000 and up is the game's own range"), std::string(t) + ": " + v.reason);
        }
        CHECK(pid::check("459999", u, nullptr, "", false).ok && pid::check("1", u, nullptr, "", false).ok, "both ends of the range");
        for (const char* t : {"-5", "12a", "1.5", "1e5"}) {
            v = pid::check(t, u, nullptr, "", false);
            CHECK(!v.ok && has(v.reason, "is not a whole number"), std::string(t) + ": " + v.reason);
        }
        v = pid::check("31399", u, nullptr, "", false);
        CHECK(!v.ok && has(v.reason, "player-career player's id"), "player career: " + v.reason);
        v = pid::check("300100", u, nullptr, "", true);
        CHECK(!v.ok && has(v.reason, "create_player_typed_id_off.txt"), "kill switch: " + v.reason);
        CHECK(pid::check("", u, nullptr, "", true).ok, "kill switch: automatic still works");
        v = pid::check("300300", u, nullptr, "", false);
        CHECK(v.ok && v.notes.size() == 1 && has(v.notes[0], "playernamemap has a row"), "playernamemap: a note, not a refusal");
        u.read = false;
        CHECK(!pid::check("300100", u, nullptr, "", false).ok, "nothing read yet: refused");
    });

    run_case("callnames: the audio-service check asks the generic ids of every language bank (union over languages and teams)", [&] {
        assets::Catalog cat;
        std::string err;
        CHECK(cat.load_file(fixture, key, &err), "fixture: " + err);
        const std::unordered_set<int64_t> u = turbo::catalog_generic_union(cat);
        // eng_us dr {990001, 990002} + gm {} + ita_it {990001}: the union, each id once
        CHECK(u.size() == 2 && u.count(990001) && u.count(990002), "union of eng_us (two teams) and ita_it: " + std::to_string(u.size()));
        assets::Catalog none;
        CHECK(turbo::catalog_generic_union(none).empty(), "no catalog loaded: no ids");
    });

    run_case("player id: what the game files hold for the id (head + callname + miniface, head only, other language, generic, stale, hidden off)", [&] {
        assets::Catalog cat;
        std::string err;
        CHECK(cat.load_file(fixture, key, &err) && cat.current(), "fixture loaded as current: " + err);
        pid::Use u;
        u.read = true;
        u.ids["players"] = {1001};
        pid::Verdict v = pid::check("90001", u, &cat, "eng_us", false);
        CHECK(v.ok && v.give_head && v.assets.head && v.assets.callname() && v.assets.miniface && v.assets.head_var == 0, "90001: head, callname, miniface");
        CHECK(v.assets_line ==
                  "This id has a 3D head and a callname (and a miniface) in the game files: the new player will use them. Name in the catalog: Test Head 90001.",
              "90001 line: " + v.assets_line);
        v = pid::check("90004", u, &cat, "eng_us", false);
        CHECK(v.ok && v.give_head && v.assets.head_var == 2 && v.assets.call_langs == std::vector<std::string>{"ita_it"} && !v.assets.call_loaded,
              "90004: head (variation 2), callname in ita_it only");
        bool lang_note = false;
        for (const auto& n : v.notes) lang_note = lang_note || has(n, "recorded in ita_it only; the loaded commentary is eng_us");
        CHECK(lang_note, "the other language is said");
        v = pid::check("90004", u, &cat, "ita_it", false);
        CHECK(v.assets.call_loaded, "ita_it loaded: his recording is heard");
        v = pid::check("90002", u, &cat, "ita_it", false);
        CHECK(v.assets.call_high_only, "90002 in ita_it: high-intensity only");
        v = pid::check("90100", u, &cat, "eng_us", false);
        CHECK(!v.ok && has(v.reason, "generic head"), "a generic head's id: " + v.reason);
        v = pid::check("90005", u, &cat, "eng_us", false);
        CHECK(v.ok && !v.give_head && has(v.assets_line, "hold no 3D head or callname") && has(v.assets_line, "miniface p90005"),
              "miniface only: " + v.assets_line);
        v = pid::check("12345", u, &cat, "eng_us", false);
        CHECK(v.ok && !v.give_head && !v.assets.any() && has(v.assets_line, "template's head"), "nothing in the game files: " + v.assets_line);
        u.heads = {90002};
        v = pid::check("90002", u, &cat, "eng_us", false);
        bool worn = false;
        for (const auto& n : v.notes) worn = worn || has(n, "also worn");
        CHECK(v.ok && v.give_head && v.assets.head_worn && worn, "a head another row wears: allowed, said");
        // a catalog of another game build: not checked, the head is not given
        assets::Catalog stale;
        CHECK(stale.load_file(fixture, "FFFF0000-00000001", &err) && stale.state() == assets::State::Stale, "stale catalog");
        v = pid::check("90001", u, &stale, "eng_us", false);
        CHECK(v.ok && !v.give_head && has(v.assets_line, "another game build") && has(v.assets_line, "it lists a 3D head and a callname"),
              "stale: " + v.assets_line);
        // asset_hidden_off.txt: a hidden head's id is refused, the list is empty
        const fs::path out = g_out / "player_id_hidden_off";
        std::error_code ec;
        fs::remove_all(out, ec);
        fs::create_directories(assets::catalog_dir(out), ec);
        fs::copy_file(fixture, assets::catalog_file(out, key), fs::copy_options::overwrite_existing, ec);
        std::ofstream(assets::hidden_off_path(out)) << "";
        assets::Catalog off;
        CHECK(off.load_dir(out, key, &err) && off.current() && off.hidden_off(), "hidden off: " + err);
        u.heads.clear();
        v = pid::check("90001", u, &off, "eng_us", false);
        CHECK(!v.ok && has(v.reason, "asset_hidden_off.txt"), "hidden off: " + v.reason);
        CHECK(pid::hidden_ids(&off, u, "eng_us").empty(), "hidden off: no list");
    });

    run_case("player id: the hidden player list (hidden heads and own callnames, ids in use left out, filters, search)", [&] {
        assets::Catalog cat;
        std::string err;
        CHECK(cat.load_file(fixture, key, &err), "fixture: " + err);
        pid::Use u;
        u.read = true;
        u.ids["players"] = {1001, 90003};  // 90003 is a player of this save: never offered
        u.heads = {90002};                 // a row wears head 90002: its head is not hidden
        auto ids_of = [](const std::vector<const pid::Hidden*>& v) {
            std::vector<int64_t> o;
            for (auto* h : v) o.push_back(h->id);
            return o;
        };
        const std::vector<pid::Hidden> all = pid::hidden_ids(&cat, u, "eng_us");
        std::vector<int64_t> ids;
        for (const auto& h : all) ids.push_back(h.id);
        CHECK(ids == (std::vector<int64_t>{90001, 90002, 90004}), fmt("eng_us list: %zu ids", ids.size()));
        CHECK(all.size() == 3 && all[0].head && all[0].callname && all[0].miniface && all[0].name == "Test Head 90001", "90001: head, callname, miniface, name");
        CHECK(all.size() == 3 && !all[1].head && all[1].callname, "90002: callname only (its head is worn)");
        CHECK(all.size() == 3 && all[2].head && !all[2].callname, "90004: head only (its recording is ita_it)");
        CHECK(ids_of(pid::pick(all, pid::Want::Head, "")) == (std::vector<int64_t>{90001, 90004}), "filter 3D head");
        CHECK(ids_of(pid::pick(all, pid::Want::Callname, "")) == (std::vector<int64_t>{90001, 90002}), "filter callname");
        CHECK(ids_of(pid::pick(all, pid::Want::Both, "")) == (std::vector<int64_t>{90001}), "filter both");
        CHECK(ids_of(pid::pick(all, pid::Want::Any, "test HEAD 90002")) == (std::vector<int64_t>{90002}), "search by name, any letter case");
        CHECK(ids_of(pid::pick(all, pid::Want::Any, "04")) == (std::vector<int64_t>{90004}), "search by id");
        const std::vector<pid::Hidden> any_lang = pid::hidden_ids(&cat, u, "");
        CHECK(any_lang.size() == 3 && any_lang[2].id == 90004 && any_lang[2].callname, "language unknown: every bank counts");
        CHECK(pid::hidden_ids(nullptr, u, "eng_us").empty(), "no catalog: no list");
    });

    run_case("player id: the scan of the loaded database (owner tables, playernamemap, heads, absent tables)", [&] {
        app.request_tab = 0;
        ui.frames(3);  // connected (the case may run alone: TURBO_TEST_ONLY)
        const pid::Use u = pid::scan(app.db);
        CHECK(u.read, "read");
        CHECK(u.has("players", 1001) && u.has("players", 4001) && !u.has("players", 9999), "players: valid rows only (9999 is a deleted record)");
        CHECK(u.has("teamplayerlinks", 1001) && u.has("editedplayernames", 3002) && u.has("transfers", 2001), "links, edited names, transfers");
        CHECK(u.has("playernamemap", 1003), "playernamemap (a note table)");
        CHECK(u.heads.count(1001) && u.heads.count(7501), "heads of players and managers");
        CHECK(std::find(u.absent.begin(), u.absent.end(), "playerloans") != u.absent.end(), "tables the world lacks are listed as not checked");
        CHECK(!pid::check("1001", u, nullptr, "", false).ok && pid::check("5555", u, nullptr, "", false).ok, "1001 used, 5555 free");
    });

    run_case("UI: Create player > Player ID: verdicts while typing, kill switch, the id and the game files' head in the command, Lua runs them", [&] {
        if (app.busy()) ui.click("Cancel");
        app.request_tab = 0;
        ui.frames(3);
        // earlier cases leave the Turbo window small and the player list wide: the editor's button row (Export... Create
        // player...) is clipped then. The window is made large for this case and put back at its end.
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
            CHECK(!cmd.is_discarded(), "valid JSON");
            captured.push_back({{"label", label}, {"cmd", cmd}});
            CHECK(ui.click("Cancel"), "Cancel after '" + label + "'");
            return cmd;
        };
        // the template: a player of both worlds (the GUI world here, Turbo's Lua test world for the commands); earlier cases
        // may leave the list filtered or scrolled, so he is selected directly when his row is not in view
        int64_t tpl = 0;
        for (int64_t c : {1002, 1001, 1003})
            if (!tpl && app.model.player(c)) tpl = c;
        CHECK(tpl != 0, "a template player");
        auto scroll_top = [&](const char* part) {
            for (ImGuiWindow* w : GImGui->Windows)
                if (std::string(w->Name).find(part) != std::string::npos) ImGui::SetScrollY(w, 0.0f);
            ui.frames(2);
        };
        auto open = [&]() {
            if (ui.find("Cancel##pcreate", "##pcreate")) ui.click("Cancel##pcreate", "##pcreate");
            app.request_tab = 0;
            ui.frames(2);
            if (!ui.click(std::to_string(tpl), "##plist")) app.sel_player = tpl;
            ui.frames(2);
            scroll_top("##pedit");
            CHECK(app.sel_player == tpl, "template selected");
            CHECK(ui.click("Create player...", "##pedit"), "create dialog");
            ui.frames(2);
            CHECK(ui.find("##playerid", "##pcreate") != nullptr, "Player ID box shown");
            CHECK(ui.type_into(ui.find("##first", "##pcreate"), "Neo"), "first name");
            CHECK(ui.type_into(ui.find("##last", "##pcreate"), "Typed"), "surname");
        };
        auto type_id = [&](const std::string& t) {
            CHECK(ui.type_into(ui.find("##playerid", "##pcreate"), t), "typed '" + t + "'");
            ui.frames(1);
            return create_id_state().verdict;
        };
        open();
        pid::Verdict v = create_id_state().verdict;
        CHECK(v.ok && v.automatic, "an empty box is automatic");
        v = type_id("1001");
        CHECK(!v.ok && has(v.reason, "in use: players, teamplayerlinks"), "1001: " + v.reason);
        ui.click("Create player", "##pcreate");
        CHECK(!app.busy(), "a used id: the button is disabled, nothing sent");
        v = type_id("470000");
        CHECK(!v.ok && has(v.reason, "out of range"), "470000: " + v.reason);
        v = type_id("31399");
        CHECK(!v.ok && has(v.reason, "player-career"), "31399: " + v.reason);
        v = type_id("5555");
        CHECK(v.ok && v.id == 5555 && has(v.assets_line, "not checked"), "5555 free, no catalog: " + v.assets_line);
        CHECK(ui.click("Create player", "##pcreate"), "Create player");
        json cmd = take("Create player (id 5555)");
        CHECK(cmd["overrides"]["playerid"] == 5555 && !cmd["overrides"].contains("id_assets") && cmd["overrides"]["source"]["playerid"] == tpl,
              "the command carries the typed id: " + cmd.dump());
        const json& sent = create_id_state().sent;
        CHECK(sent.is_object() && sent.contains("playerid") && sent["playerid"] == 5555, "sent state");

        // the kill switch: read when the dialog opens; the box is disabled and a typed id refused
        const fs::path kill = le / "turbo_output" / pid::typed_off_name();
        std::ofstream(kill) << "";
        open();
        ui.frames(1);
        v = create_id_state().verdict;
        CHECK(create_id_state().typed_off && create_id_state().text == "5555" && !v.ok && has(v.reason, "switched off"),
              "kill switch: the box (still 5555) is refused: " + v.reason);
        ui.click("Create player", "##pcreate");
        CHECK(!app.busy(), "kill switch: nothing sent");
        std::error_code ec;
        fs::remove(kill, ec);

        // a current catalog: the game files' assets of the id, the hidden player list
        const fs::path out = le / "turbo_output";
        fs::create_directories(assets::catalog_dir(out), ec);
        fs::copy_file(fixture, assets::catalog_file(out, key), fs::copy_options::overwrite_existing, ec);
        auto saved_hook = app.hook_report;
        app.hook_report = [&]() {
            HookReport r;
            r.build = key;
            return r;
        };
        auto load = [&](const char* why) {
            for (int i = 0; i < 400 && !app.asset_reload(why); ++i) {
                ui.frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            for (int i = 0; i < 400 && app.asset_loading(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ui.frames(2);
        };
        load("test: player id catalog");
        CHECK(app.asset_catalog()->current(), "catalog current: " + app.asset_load_note);
        open();
        v = type_id("90001");
        CHECK(v.ok && v.give_head && has(v.assets_line, "This id has a 3D head and a callname (and a miniface) in the game files: the new player will use them"),
              "90001: " + v.assets_line);
        v = type_id("90100");
        CHECK(!v.ok && has(v.reason, "generic head"), "90100: " + v.reason);
        CHECK(ui.click("Use a hidden player's id...", "##pcreate"), "picker");
        ui.frames(2);
        CHECK(create_id_state().hidden == 4, fmt("4 hidden ids with a head or a callname (%zu)", create_id_state().hidden));
        CHECK(ui.type_into(ui.find("##pidsearch", "##pidpick"), "90004"), "search 90004");
        ui.frames(1);
        CHECK(create_id_state().shown == 1, fmt("one shown (%zu)", create_id_state().shown));
        CHECK(ui.click("Use##pid90004", "##pidpick"), "Use 90004");
        ui.frames(2);
        CHECK(create_id_state().text == "90004" && ui.find("##pidsearch", "##pidpick") == nullptr, "the box holds 90004, the picker closed");
        v = create_id_state().verdict;
        CHECK(v.ok && v.give_head && v.assets.head_var == 2, "90004: its head (variation 2) will be given");
        CHECK(ui.click("Create player", "##pcreate"), "Create player 90004");
        cmd = take("Create player (id 90004)");
        const json ia = cmd["overrides"].is_object() && cmd["overrides"].contains("id_assets") ? cmd["overrides"]["id_assets"] : json();
        CHECK(cmd["overrides"]["playerid"] == 90004 && ia.is_object() && ia["head3d"] == true && ia["headvariation"] == 2 && ia["miniface"] == false,
              "id_assets: " + cmd.dump());

        // both commands through Turbo's real Lua side: the free id is created; the head is refused there because the Lua
        // test world has no asset gate (lua_gate.txt) of this GUI session, so nothing is written
        std::ofstream(g_out / "gui_commands.json") << captured.dump(1);
        CHECK(run_lua("commands") == 0, "gui_world.lua commands");
        json results = read_json(g_out / "gui_commands_out.json");
        CHECK(results.size() == 2, "two results");
        if (results.size() == 2) {
            const std::string t0 = results[0]["text"], t1 = results[1]["text"];
            CHECK(results[0]["ok"].get<bool>() && has(t0, "new player 5555") && has(t0, "player id typed"), "5555 created by Lua: " + t0);
            CHECK(!results[1]["ok"].get<bool>() && has(t1, "the 3D head of player id 90004 cannot be checked now") && has(t1, "nothing was written"),
                  "90004 without a gate: refused before any write: " + t1);
        }

        // back to how the other cases found it: empty box, no catalog
        open();
        type_id("");
        CHECK(create_id_state().verdict.automatic, "box emptied");
        ui.click("Cancel##pcreate", "##pcreate");
        fs::remove(assets::catalog_file(out, key), ec);
        load("test: player id catalog removed");
        CHECK(!app.asset_catalog()->loaded(), "catalog gone");
        app.hook_report = saved_hook;
        if (mw) {
            ImGui::SetWindowPos(mw, saved_pos);
            ImGui::SetWindowSize(mw, saved_size);
        }
        ui.frames(3);
    });
}
