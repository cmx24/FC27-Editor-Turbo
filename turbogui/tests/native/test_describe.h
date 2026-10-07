// Native tests: codes to descriptions (docs/TURBO_2_0_PLAN.md section 6): core/field_labels.h describe(), core/user_text.h,
// the pickers, the edit log / undo toast wording, the collapsed Technical details on Status, and the UI walk that fails when a
// tooltip or a line of text says "code N". Included by test_main.cpp after the Ui class (so test_describe_core() runs from
// main with the core tests and describe_ui_cases() runs inside test_ui).
#pragma once
#include "core/field_labels.h"
#include <cfloat>
#include <algorithm>
#include "core/user_text.h"

// ---------------------------------------------------------------- core
static void test_describe_core() {
    using namespace turbo;
    run_case("describe(): names where the game has them, 'Unnamed N' / '... (no name)' where it has not, numbers stay numbers", [] {
        using labels::describe;
        CHECK(describe("players", "role1", 14) == "CDM Holding+", "role 14: " + describe("players", "role1", 14));
        CHECK(describe("players", "bodytypecode", 5) == "Tall and Normal", "body type 5");
        CHECK(describe("players", "bodytypecode", 12) == "Player-specific body model 12", "a body type with no name: " + describe("players", "bodytypecode", 12));
        CHECK(describe("manager", "bodytypecode", 10) == "Player-specific body model 10", "code 10 has no entry");
        CHECK(describe("players", "role3", 9999) == "Unnamed 9999", "an unnamed role");
        CHECK(describe("players", "skillmoves", 3) == "4 stars", "skill moves 3 is 4 stars");
        CHECK(describe("players", "skillmoves", 9) == "Unnamed 9", "an enum value past its labels");
        CHECK(describe("players", "headclasscode", 0) == "Real face (own head model)" && describe("players", "headclasscode", 1) == "Generic head",
              "head class reads as a real face or a generic head");
        CHECK(describe("players", "shoetypecode", 1432) == "Boots style 1432 (no name)", "boots: " + describe("players", "shoetypecode", 1432));
        CHECK(describe("players", "shoecolorcode1", 4) == "Boot colour 4 (no name)", "boot colour: " + describe("players", "shoecolorcode1", 4));
        CHECK(describe("players", "headtypecode", 20801) == "Head style 20801 (no name)", "head: " + describe("players", "headtypecode", 20801));
        CHECK(describe("players", "overallrating", 87) == "87" && describe("players", "height", 181) == "181", "plain numbers stay numbers");
        CHECK(describe("manager", "personalityid", 3) == "Personality type 3", "manager personality has no names to give: " + describe("manager", "personalityid", 3));
        const std::string hair = describe("players", "hairtypecode", 100);
        CHECK(!hair.empty() && hair.find('#') == std::string::npos && hair.find("code") == std::string::npos, "hair: " + hair);
        for (const char* f : {"role1", "bodytypecode", "shoetypecode", "hairtypecode", "facialhairtypecode", "headclasscode", "eyebrowcode", "accessorycode2"})
            for (int64_t v : {-1, 0, 3, 77, 123456}) {
                const std::string d = describe("players", f, v);
                bool leak = false;
                for (size_t i = 0; (i = d.find("code", i)) != std::string::npos; ++i)
                    if (i + 5 < d.size() && d[i + 4] == ' ' && std::isdigit(static_cast<unsigned char>(d[i + 5]))) leak = true;
                CHECK(!leak && !d.empty(), std::string(f) + " " + std::to_string(v) + ": " + d);
            }
    });
    run_case("field titles: the label table, the word splitter for appearance fields, a plain capital as the last resort", [] {
        using labels::field_title;
        CHECK(field_title("overallrating") == "Overall" && field_title("nationality") == "Nationality", "table entries");
        CHECK(field_title("headtypecode") == "Head type" && field_title("eyebrowcode") == "Eyebrows", "appearance entries");
        CHECK(labels::humanize("shoecolorcode2") == "Shoe colour 2" && labels::humanize("hairlinecode") == "Hair line", "split into words: " + labels::humanize("hairlinecode"));
        CHECK(labels::humanize("tattoohead") == "Tattoo head" && labels::humanize("tattoorightforearm") == "Tattoo right forearm", "tattoos");
        CHECK(labels::humanize("zzzunknown") == "" && field_title("zzzunknown") == "Zzzunknown", "unsplittable: capital only");
        CHECK(labels::describe_range("overallrating", 0, 99) == "Overall (0-99)", "range tooltip text");
        CHECK(labels::describe_change("overallrating", "84", "87", "Bukayo Saka") == "Overall changed 84 -> 87 (Bukayo Saka)", "change text");
        CHECK(labels::describe_change("role1", "CDM Holding+", "ST Poacher+") == "Role 1 changed CDM Holding+ -> ST Poacher+" ||
                  labels::describe_change("role1", "a", "b").find("changed a -> b") != std::string::npos,
              "change without a subject");
        // every title the GUI knows has a real word in it, never a bare field name
        int raw = 0;
        for (const auto& kv : labels::title_table())
            if (kv.second == kv.first) ++raw;
        CHECK(raw == 0, "no title repeats its field name");
    });
    run_case("user text: queued-command verbs, match switch sentences, difficulty names", [] {
        using namespace usertext;
        CHECK(pending_text("Transfer") == "Moving the player" && pending_text("Keep shown name (3 players)") == "Keeping the shown name" &&
                  pending_text("Mass actions: contracts") == "Applying the team actions" && pending_text("Something else") == "Something else",
              "verbs, an unknown label as it is");
        CHECK(match_var_set("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", 30) == "Injury frequency (your team) set to 30; applies at kick-off",
              "injury frequency: " + match_var_set("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", 30));
        CHECK(match_var_set("NEVER_INJURE", 1) == "Injuries off set to on; applies at kick-off" && match_var_set("NEVER_INJURE", 0).find("set to off") != std::string::npos,
              "a switch reads on / off");
        CHECK(match_var_clear("OVERRIDE/WEATHER") == "Weather: the game decides again", "clear");
        CHECK(match_var_set("OVERRIDE_MATCH_DIFFICULTY", 0).find("set to Beginner") != std::string::npos &&
                  match_var_set("OVERRIDE_MATCH_DIFFICULTY", 5).find("set to Legendary") != std::string::npos &&
                  match_var_set("OVERRIDE_MATCH_DIFFICULTY", 2).find("level 2 of 5") != std::string::npos,
              "difficulty: only the two verified ends are named");
        // weather and time of day stay numbers: their names are not verified
        CHECK(match_var_set("OVERRIDE/WEATHER", 3) == "Weather set to 3; applies when the next match is set up", "weather: " + match_var_set("OVERRIDE/WEATHER", 3));
        CHECK(match_var_outcome(true, false, "NEVER_INJURE", 1, "NEVER_INJURE = 1") == "Injuries off set to on; applies at kick-off", "ok outcome");
        const std::string bad = match_var_outcome(false, false, "NEVER_INJURE", 1, "NEVER_INJURE: the variable store is busy");
        CHECK(bad == "Injuries off: the variable store is busy", "a refusal names the switch, not the variable: " + bad);
        CHECK(health_line(5, 5, 3, 0, true, true).find("Everything Turbo needs") == 0 &&
                  health_line(3, 5, 3, 1, true, true).find("Needs attention: 2 of 5 game code spots not found, 1 hook switched off") == 0 &&
                  health_line(5, 5, 3, 0, false, false).find("dispatcher") != std::string::npos,
              "health line");
    });
}

// ---------------------------------------------------------------- UI walk
// One frame with ImGui's text log on: every line of text drawn (windows, tooltips, button and combo labels) lands in the
// log, with clipping off while it runs. Returns the text of the frame.
static std::string logged_frame(Ui& ui) {
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ++g_frame;
    ImGui::LogToBuffer();
    ui.app.tick(ui.t);
    ui.app.draw();
    std::string text = GImGui->LogEnabled ? std::string(GImGui->LogBuffer.c_str()) : std::string();
    ImGui::LogFinish();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    ui.t += 1.0 / 60.0;
    return text;
}

// Text that may say "code N": the release code number in bug-report text (game_calls.h, player_move.cpp) and nothing else
static const char* const kCodeAllowList[] = {"release code"};

// "code 3" / "Code 12" as a word: returns the context of the first hit, "" when there is none (or it is allow-listed)
static std::string code_leak(const std::string& text) {
    std::string low = text;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (size_t i = 0; (i = low.find("code ", i)) != std::string::npos; i += 5) {
        if (i > 0 && std::isalnum(static_cast<unsigned char>(low[i - 1]))) continue;  // "bodytypecode 5" is a field name, not the word
        if (i + 5 >= low.size() || !std::isdigit(static_cast<unsigned char>(low[i + 5]))) continue;
        const std::string ctx = text.substr(i > 30 ? i - 30 : 0, 70);
        bool allowed = false;
        for (const char* a : kCodeAllowList) allowed = allowed || low.substr(i > 30 ? i - 30 : 0, 70).find(a) != std::string::npos;
        if (!allowed) return ctx;
    }
    return "";
}

struct WalkStats {
    int scenes = 0, hovered = 0;
    std::vector<std::string> leaks;
};

static void walk_scan(const std::string& text, const std::string& where, WalkStats& st) {
    const std::string hit = code_leak(text);
    if (!hit.empty()) st.leaks.push_back(where + ": ..." + hit + "...");
}

// The text of the scene as drawn, then of every tooltip: the mouse rests on each visible item in turn
static void walk_scene(Ui& ui, const std::string& where, WalkStats& st, size_t max_items = 160) {
    ui.frames(2);
    walk_scan(logged_frame(ui), where, st);
    ++st.scenes;
    struct Spot {
        ImVec2 c;
        std::string label;
    };
    std::vector<Spot> spots;
    for (const auto& kv : g_items) {
        const ItemRec& r = kv.second;
        if (r.frame != g_frame || r.rect.GetWidth() <= 0 || r.rect.GetHeight() <= 0 || !r.clip.Contains(r.rect.GetCenter())) continue;
        spots.push_back({r.rect.GetCenter(), r.label});
    }
    std::sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.c.y != b.c.y ? a.c.y < b.c.y : a.c.x < b.c.x; });
    const size_t step = spots.size() > max_items ? (spots.size() + max_items - 1) / max_items : 1;
    for (size_t i = 0; i < spots.size(); i += step) {
        ImGui::GetIO().AddMousePosEvent(spots[i].c.x, spots[i].c.y);
        ui.frames(2);
        walk_scan(logged_frame(ui), where + " > hover '" + spots[i].label + "'", st);
        ++st.hovered;
    }
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    ui.frames(2);
}

static std::string hover_text(Ui& ui, const ItemRec* r) {
    if (!r) return "";
    ImGui::GetIO().AddMousePosEvent(r->rect.GetCenter().x, r->rect.GetCenter().y);
    ui.frames(2);
    const std::string t = logged_frame(ui);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    ui.frames(2);
    return t;
}

static void describe_ui_cases(App& app, Ui& ui) {
    using namespace turbo;
    run_case("UI: the edit log, the undo toast and the pickers speak in words", [&] {
        app.request_tab = 0;
        ui.frames(3);
        app.sel_player = 1001;
        ui.frames(3);
        CHECK(ui.click("Profile", "##pedit"), "Profile tab");
        ui.frames(2);
        const Table* p = app.db.table("players");
        const uint64_t rec = app.db.find(*p, "playerid", 1001);
        CHECK(rec != 0 && p->field("nationality") && p->field("overallrating"), "player 1001 with nationality and overall");
        // the overall: "Overall changed 84 -> 87 (Player)" in the log, the game's own name in brackets
        const int64_t ovr0 = app.db.get_int(*p, rec, "overallrating");
        CHECK(ui.type_into(ui.find("##v", "##prof", "overallrating"), std::to_string(ovr0 == 80 ? 81 : 80)), "type a new overall");
        auto log_has = [&](const std::string& s) {
            for (const auto& l : app.log_lines)
                if (l.find(s) != std::string::npos) return true;
            return false;
        };
        CHECK(log_has("Overall changed " + std::to_string(ovr0) + " -> " + std::to_string(ovr0 == 80 ? 81 : 80) + " (" + app.model.player_name(1001) + ")") &&
                  log_has("[players.overallrating = "),
              "edit log says what changed, with the game's field name after it");
        // the nationality: a searchable list of nation names, not a number box
        CHECK(app.db.get_int(*p, rec, "nationality") == 14, "player starts as nation 14");
        const ItemRec* nat = ui.find("##ref", "##prof", "nationality");
        CHECK(nat != nullptr, "the nationality is a picker, not an integer box");
        CHECK(ui.find("##v", "##prof", "nationality") == nullptr, "no number box for the nationality");
        const std::string tip = hover_text(ui, nat);
        CHECK(tip.find("Nationality: England") != std::string::npos && tip.find("nationality = 14") != std::string::npos, "tooltip: " + tip);
        CHECK(ui.click("##ref", "##prof", "nationality"), "open the nationality list");
        ui.frames(2);
        CHECK(ui.click("France##18", "##Combo"), "pick France");
        CHECK(app.db.get_int(*p, rec, "nationality") == 18, "nationality written");
        CHECK(log_has("Nationality changed England -> France ("), "log names the nations");
        // undo: the toast says what it went back to, in words
        CHECK(app.undo(1001), "undo the nationality");
        CHECK(ui.toast_contains("undone: Nationality back to England"), "undo toast in words");
        CHECK(app.undo(1001), "undo the overall");
        CHECK(ui.toast_contains("undone: Overall back to " + std::to_string(ovr0)), "undo toast of a plain number");
        // a role (a code with a name): the toast and the log use the role's name
        const Field* r1 = p->field("role1");
        if (r1) {
            const int64_t before = app.db.get_int(*p, rec, "role1");
            CHECK(app.edit(*p, rec, *r1, Value::of_int(before == 14 ? 15 : 14)), "edit role 1");
            CHECK(log_has("Role 1 changed " + labels::describe("players", "role1", before) + " -> " +
                          labels::describe("players", "role1", before == 14 ? 15 : 14)),
                  "role change in words");
            CHECK(app.undo(1001) && ui.toast_contains("undone: Role 1 back to " + labels::describe("players", "role1", before)), "role undo toast");
        }
        // the manager's club and nation are pickers too
        app.request_tab = 2;
        ui.frames(3);
        app.sel_manager = 0;
        ui.frames(3);
        ui.click("Details", "##medit");  // the tab bar keeps the tab an earlier case left open
        ui.frames(2);
        CHECK(ui.find("##ref", "##mdet", "teamid") != nullptr && ui.find("##ref", "##mdet", "nationality") != nullptr,
              "manager: club and nation are name pickers");
        const std::string mt = hover_text(ui, ui.find("##ref", "##mdet", "teamid"));
        CHECK(mt.find("Club: ") != std::string::npos && mt.find("teamid = ") != std::string::npos, "manager club tooltip: " + mt);
    });

    run_case("UI: Status keeps the addresses and counters under a collapsed 'Technical details', with a health summary on top", [&] {
        app.request_tab = 6;
        ui.frames(3);
        std::string t = logged_frame(ui);
        CHECK(t.find("Health") != std::string::npos && t.find("Connected to the game's database") != std::string::npos, "health summary: " + t.substr(0, 200));
        CHECK(t.find("DB service") == std::string::npos && t.find("Command channel") == std::string::npos &&
                  t.find("Signatures:") == std::string::npos && t.find("Live Editor folder") == std::string::npos,
              "no service pointers, channel address or hook lines while Technical details is closed");
        CHECK(ui.find("Technical details") != nullptr, "the Technical details header");
        CHECK(ui.click("Technical details"), "open it");
        ui.frames(2);
        t = logged_frame(ui);
        CHECK(t.find("DB service") != std::string::npos && t.find("Command channel") != std::string::npos, "technical lines when open");
        CHECK(ui.click("Technical details"), "close it again");
        ui.frames(2);
    });

    run_case("UI walk: no tooltip or line of text says 'code N' (allow-list: the release code of a bug report)", [&] {
        WalkStats st;
        CHECK(!code_leak("Dark Brown (code 3)").empty() && !code_leak("Unknown (Code 12)").empty() && code_leak("bodytypecode 5 = 3").empty() &&
                  code_leak("release code 4 means").empty() && code_leak("the code of conduct").empty(),
              "the leak detector: flags 'code N', not a field name, the allow-listed release code or the word alone");
        // Players: the list and every editor tab of one player
        app.request_tab = 0;
        ui.frames(3);
        app.sel_player = 1001;
        ui.frames(3);
        walk_scene(ui, "Players list", st);
        for (const char* tab : {"Profile", "Names", "Attributes", "Appearance", "Miniface", "Contract & Clubs", "Callname", "Playstyles", "All fields"}) {
            if (!ui.find(tab, "##pedit")) continue;
            ui.click(tab, "##pedit");
            ui.frames(2);
            walk_scene(ui, std::string("Players > ") + tab, st, 120);
        }
        // Teams
        app.request_tab = 1;
        ui.frames(3);
        app.sel_team = 1;
        ui.frames(3);
        walk_scene(ui, "Teams", st);
        for (const char* tab : {"Overview", "Colours", "Name", "Job offer", "Mass actions", "All fields"}) {
            if (!ui.find(tab, "##ttabs") && !ui.find(tab)) continue;
            ui.click(tab);
            ui.frames(2);
            walk_scene(ui, std::string("Teams > ") + tab, st, 100);
        }
        // Managers
        app.request_tab = 2;
        ui.frames(3);
        app.sel_manager = 0;
        ui.frames(3);
        walk_scene(ui, "Managers", st);
        for (const char* tab : {"Details", "Appearance", "Miniface"}) {
            if (!ui.find(tab, "##mtabs") && !ui.find(tab)) continue;
            ui.click(tab);
            ui.frames(2);
            walk_scene(ui, std::string("Managers > ") + tab, st, 100);
        }
        // Competitions (with its Match setup view), Database, Turbo Tools, Status (Technical details open)
        for (int tab : {3, 4, 5}) {
            app.request_tab = tab;
            ui.frames(3);
            walk_scene(ui, "tab " + std::to_string(tab), st);
        }
        app.request_tab = 3;
        ui.frames(3);
        for (const char* v : {"Match setup", "Live standings"}) {
            if (!ui.find(v)) continue;
            ui.click(v);
            ui.frames(2);
            walk_scene(ui, std::string("Competitions > ") + v, st, 100);
        }
        app.request_tab = 6;
        ui.frames(3);
        if (!ui.find("DB service")) {
            ui.click("Technical details");
            ui.frames(2);
        }
        walk_scene(ui, "Status (Technical details open)", st);
        if (ui.find("Technical details")) {
            ui.click("Technical details");
            ui.frames(2);
        }
        // the toasts and the log lines shown so far
        for (const auto& tt : app.toasts) walk_scan(tt.text, "toast", st);
        for (const auto& l : app.log_lines) walk_scan(l, "log", st);
        CHECK(st.scenes >= 12 && st.hovered > 200, fmt("the walk covered %d scenes, %d hovered items", st.scenes, st.hovered));
        for (const auto& l : st.leaks) CHECK(false, "leak: " + l);
        CHECK(st.leaks.empty(), fmt("%zu leaks", st.leaks.size()));
        app.request_tab = 0;
        ui.frames(3);
    });
}
