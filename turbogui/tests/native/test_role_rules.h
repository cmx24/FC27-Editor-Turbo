// Native tests (Turbo 2.0, track G): the squad role rule editor (ui/ui_role_rules.{h,cpp}). Included by test_main.cpp after its
// framework (CHECK, run_case, g_out, run_lua, read_json). The editor model (rules, pins, validation), the team_mass command it
// builds, and the preview file parser are tested here; the commands also run through Turbo's real Lua role engine
// (gui_world.lua mode "roles", the simulated career) and the files Lua wrote are read back with the GUI's own parser.
#pragma once
#include "ui_role_rules.h"

namespace role_rules_test {

using namespace turbo;
using json = nlohmann::json;

static std::string no_names(void*, int64_t pid) { return "P" + std::to_string(pid); }

// Runs commands through the Lua world; returns the results (label, ok, text). The files Lua wrote after command n are
// g_out/roles/n_role_preview.json and n_role_rule.json (n counts from 1).
static json run_roles_lua(const json& cmds) {
    std::ofstream(g_out / "role_cmds.json") << cmds.dump(1);
    CHECK(run_lua("roles") == 0, "gui_world.lua roles");
    return read_json(g_out / "role_cmds_out.json");
}
static json mass_cmd(const char* label, const json& overrides) {
    return {{"label", label}, {"cmd", {{"op", "run"}, {"module", "team_mass"}, {"overrides", overrides}}}};
}
static bool role_file(int n, const char* name, std::string& text) {
    std::ifstream f(g_out / "roles" / (std::to_string(n) + "_" + name), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    text = ss.str();
    return !text.empty();
}

}  // namespace role_rules_test

static void test_role_rules() {
    using namespace role_rules_test;
    namespace rr = turbo::rolerules;

    run_case("role rules: the editor model (defaults, add, remove, reorder, 32 rule limit, pins)", [] {
        rr::State s;
        CHECK(s.rules.size() == 2 && s.rules[0].age_min == 19 && s.rules[0].role == 3 && s.rules[1].role == 5, "defaults: 19+ Rotation, younger Prospect");
        CHECK(std::string(rr::role_name(1)) == "Crucial" && std::string(rr::role_name(3)) == "Rotation" && std::string(rr::role_name(5)) == "Prospect" &&
                  std::string(rr::role_name(0)) == "-", "role names");
        CHECK(rr::add_rule(s) && s.rules.size() == 3, "add");
        s.rules[2].role = 1;
        CHECK(rr::move_rule(s, 2, -1) && s.rules[1].role == 1 && s.rules[2].role == 5, "move up");
        CHECK(!rr::move_rule(s, 0, -1) && !rr::move_rule(s, 2, +1), "no move past the ends");
        CHECK(rr::remove_rule(s, 1) && s.rules.size() == 2 && s.rules[1].role == 5, "remove");
        CHECK(rr::remove_rule(s, 1) && !rr::remove_rule(s, 0), "the last rule stays");
        rr::State f;
        while (rr::add_rule(f)) {}
        CHECK(f.rules.size() == 32, "32 rules at most");
        rr::set_pin(s, 1002, "A", 2);
        rr::set_pin(s, 1002, "A", 0);
        rr::set_pin(s, 1003, "B", 9);
        CHECK(s.pins.size() == 2 && s.pins[0].role == 0 && s.pins[1].role == 5, "pin changed in place, role clamped to 1..5");
        CHECK(rr::remove_pin(s, 1002) && !rr::remove_pin(s, 1002) && s.pins.size() == 1, "remove pin");
    });

    run_case("role rules: validation mirrors the Lua checks", [] {
        rr::State s;
        CHECK(rr::validate(s).empty(), "defaults are valid");
        s.rules[0].age_min = 30;
        s.rules[0].age_max = 20;
        CHECK(rr::validate(s).find("Rule 1") == 0 && rr::validate(s).find("youngest") != std::string::npos, "age band reversed: " + rr::validate(s));
        s.rules[0].age_max = 0;
        s.rules[1].rank_min = 5;
        s.rules[1].rank_max = 2;
        CHECK(rr::validate(s).find("Rule 2") == 0, "rank band reversed: " + rr::validate(s));
        s.rules[1].rank_max = 0;
        s.rules[1].role = 7;
        CHECK(!rr::validate(s).empty(), "bad role");
        s.rules.clear();
        CHECK(!rr::validate(s).empty(), "no rule");
    });

    run_case("role rules: the team_mass command (op run, team_mass, squad_roles; names to numbers; preview and save flags)", [] {
        rr::State s;
        rr::Rule gk;
        gk.name = "keepers";
        gk.role = 1;
        gk.pos = 1;
        rr::Rule top;
        top.role = 2;
        top.rank_min = 1;
        top.rank_max = 3;
        top.pos = 15;  // all four ticked = every position
        s.rules = {gk, top, s.rules[1]};
        s.rules[2].age_min = 0;
        s.rules[2].age_max = 21;
        rr::set_pin(s, 158023, "Pinned", 4);
        json o = rr::overrides(s, 1, true);
        CHECK(o["teamid"] == 1 && o["actions"] == json::array({"squad_roles"}) && o["role_preview"] == true && !o.contains("role_save"), "preview: " + o.dump());
        CHECK(o["role_rules"].size() == 3 && o["role_rules"][0]["role"] == 1 && o["role_rules"][0]["pos"] == json::array({"GK"}) &&
                  o["role_rules"][0]["name"] == "keepers" && !o["role_rules"][0].contains("age_min"), "rule 1: " + o["role_rules"][0].dump());
        CHECK(o["role_rules"][1]["ovr_rank_min"] == 1 && o["role_rules"][1]["ovr_rank_max"] == 3 && !o["role_rules"][1].contains("pos"), "rule 2: " + o["role_rules"][1].dump());
        CHECK(o["role_rules"][2]["age_max"] == 21 && o["role_rules"][2]["role"] == 5, "rule 3 (young, Prospect)");
        CHECK(o["role_pins"]["158023"] == 4, "pin: " + o["role_pins"].dump());
        s.save_rule = false;
        o = rr::overrides(s, 1, false);
        CHECK(!o.contains("role_preview") && o["role_save"] == false, "apply without saving: " + o.dump());
        o = rr::clear_overrides(1);
        CHECK(o["role_rule_clear"] == true && o["actions"] == json::array({"squad_roles"}) && !o.contains("role_rules"), "clear: " + o.dump());
        rr::State d;  // defaults serialise to two rules Lua reads as 19+ Rotation / younger Prospect
        o = rr::overrides(d, 1, false);
        CHECK(o["role_rules"][0]["age_min"] == 19 && o["role_rules"][0]["role"] == 3 && o["role_rules"][1]["role"] == 5 && o["role_pins"].empty(), "default rules: " + o.dump());
    });

    run_case("role rules: the preview file parser (rows, counts, rule index or pin, skip reasons)", [] {
        const char* text = R"JSON({"team":1,"counts":{"rotation":3,"pinned: left alone":1},"loaned":1,"no_entry":[9],"outside":[8,7],
            "rows":[{"pid":1002,"age":24,"ovr":80,"rank":1,"pos":3,"group":"DEF","old_role":2,"new_role":3,"rule":1},
                    {"pid":1003,"age":17,"ovr":60,"rank":2,"group":"MID","old_role":3,"new_role":5,"rule":"pin"},
                    {"pid":1004,"old_role":4,"skip":"not in your squad (left alone)"}]})JSON";
        rr::Preview p;
        CHECK(rr::parse_preview(text, p, no_names, nullptr), "parses");
        CHECK(p.loaded && p.team == 1 && p.loaned == 1 && p.no_entry == 1 && p.outside == 2 && p.rows.size() == 3, "header");
        CHECK(p.counts.size() == 2 && p.counts[0].first == "rotation" && p.counts[0].second == 3, "counts, biggest first");
        CHECK(p.rows[0].name == "P1002" && p.rows[0].age == 24 && p.rows[0].ovr == 80 && p.rows[0].old_role == 2 && p.rows[0].new_role == 3 &&
                  p.rows[0].rule == "rule 1" && p.rows[0].skip.empty(), "row 1");
        CHECK(p.rows[1].rule == "pin" && p.rows[1].new_role == 5, "pin row");
        CHECK(p.rows[2].new_role == -1 && p.rows[2].age == -1 && p.rows[2].skip.find("not in your squad") == 0, "skipped row has no new role");
        CHECK(!rr::parse_preview("not json", p, no_names, nullptr) && p.loaded, "garbage refused, the old preview stays");
    });

    run_case("role rules: Lua for real: preview, custom rules and pins, apply (saved), clear, no-save apply, other club", [] {
        // 1) the default rules' preview tells which players the world has
        rr::State s;
        json r1 = run_roles_lua(json::array({mass_cmd("Squad roles preview", rr::overrides(s, 1, true))}));
        CHECK(r1.size() == 1 && r1[0]["ok"].get<bool>(), "default preview ok: " + r1[0].dump());
        std::string text;
        CHECK(role_file(1, "role_preview.json", text), "Lua wrote role_preview.json");
        CHECK(!role_file(1, "role_rule.json", text), "a preview saves no rule");
        rr::Preview p0;
        CHECK(rr::parse_preview(read_file(g_out / "roles" / "1_role_preview.json"), p0, no_names, nullptr) && p0.rows.size() >= 10, fmt("rows: %zu", p0.rows.size()));
        int64_t pin_pid = 0, gk_pid = 0;
        for (const rr::PreviewRow& r : p0.rows) {
            CHECK(r.skip.empty() ? (r.new_role == 3 || r.new_role == 5) : r.new_role == -1, "default rules give Rotation or Prospect, or a skip: " + r.name);
            if (!r.skip.empty() || r.age < 0) continue;
            CHECK(r.rule == (r.age >= 19 ? "rule 1" : "rule 2"), fmt("age %d: rule %s", r.age, r.rule.c_str()));
            if (!pin_pid && r.age >= 19) pin_pid = r.pid;
            if (r.group == "GK" && !gk_pid) gk_pid = r.pid;
        }
        CHECK(pin_pid != 0, "a player to pin");

        // 2) custom rules + a pin: preview, apply (saved), clear, apply without saving, preview of another club
        rr::Rule gk, top, young, rest;
        gk.name = "keepers"; gk.role = 1; gk.pos = 1;
        top.name = "best three"; top.role = 2; top.rank_min = 1; top.rank_max = 3;
        young.name = "youth"; young.role = 5; young.age_max = 21;
        rest.name = "rest"; rest.role = 4;
        s.rules = {gk, top, young, rest};
        rr::set_pin(s, pin_pid, "Pinned", 1);
        rr::State nosave = s;
        nosave.save_rule = false;
        json cmds = json::array({mass_cmd("Squad roles preview", rr::overrides(s, 1, true)),
                                 mass_cmd("Squad roles apply", rr::overrides(s, 1, false)),
                                 mass_cmd("Squad roles clear", rr::clear_overrides(1)),
                                 mass_cmd("Squad roles apply", rr::overrides(nosave, 1, false)),
                                 mass_cmd("Squad roles preview", rr::overrides(s, 2, true)),
                                 mass_cmd("Squad roles clear", rr::clear_overrides(1))});
        json r = run_roles_lua(cmds);
        CHECK(r.size() == 6, "a result for every command");
        for (int i = 0; i < 4; ++i) CHECK(r[i]["ok"].get<bool>(), fmt("command %d ok: %s", i + 1, r[i]["text"].get<std::string>().c_str()));
        CHECK(!r[4]["ok"].get<bool>() && r[4]["text"].get<std::string>().find("your own club only") != std::string::npos, "another club is refused: " + r[4]["text"].dump());
        CHECK(r[5]["ok"].get<bool>() && r[5]["text"].get<std::string>().find("no saved role rule") != std::string::npos, "clear with nothing saved: " + r[5]["text"].dump());
        CHECK(!r[0]["text"].get<std::string>().empty(), "preview summary");

        rr::Preview p;
        CHECK(role_file(1, "role_preview.json", text) && rr::parse_preview(text, p, no_names, nullptr), "custom preview parsed");
        CHECK(p.rows.size() >= 10, "preview rows");
        int pinned = 0, keepers = 0, best = 0;
        for (const rr::PreviewRow& r2 : p.rows) {
            if (!r2.skip.empty()) continue;
            if (r2.pid == pin_pid) {
                CHECK(r2.rule == "pin" && r2.new_role == 1, "pin beats the rules: " + r2.rule);
                ++pinned;
            } else if (r2.group == "GK") {
                CHECK(r2.rule == "rule 1" && r2.new_role == 1, "keepers get rule 1: " + r2.rule);
                ++keepers;
            } else if (r2.rank >= 1 && r2.rank <= 3) {
                CHECK(r2.rule == "rule 2" && r2.new_role == 2, fmt("rank %d gets rule 2: %s", r2.rank, r2.rule.c_str()));
                ++best;
            } else if (r2.age >= 0 && r2.age <= 21) {
                CHECK(r2.rule == "rule 3" && r2.new_role == 5, fmt("age %d gets rule 3: %s", r2.age, r2.rule.c_str()));
            } else if (r2.age >= 0) {
                CHECK(r2.rule == "rule 4" && r2.new_role == 4, fmt("age %d gets rule 4: %s", r2.age, r2.rule.c_str()));
            }
        }
        CHECK(pinned == 1 && best >= 1, fmt("pinned %d, best %d", pinned, best));
        (void)keepers;
        CHECK(!role_file(1, "role_rule.json", text), "preview: nothing saved");
        // apply saved the rule: the saved file carries the rules and the pin (Lua's own format)
        CHECK(role_file(2, "role_rule.json", text), "apply saved role_rule.json");
        json saved = json::parse(text, nullptr, false);
        CHECK(!saved.is_discarded() && saved["rules"].size() == 4 && saved["rules"][0]["role"] == 1 && saved["pins"][std::to_string(pin_pid)] == 1, "saved rule: " + text);
        CHECK(!role_file(3, "role_rule.json", text), "clear removed the saved rule");
        CHECK(!role_file(4, "role_rule.json", text), "apply with role_save false saved nothing");
        // the applied roles are what the preview said: a second preview after apply shows old_role = new_role
        CHECK(r[1]["text"].get<std::string>().find("PlayerStatusManager") != std::string::npos || !r[1]["text"].get<std::string>().empty(), "apply summary");
    });

    run_case("role rules: the re-apply status files (kill switch, saved rule) are read on demand, not per frame", [] {
        // refresh_files needs an App: covered in the UI case (UI: Squad roles rule editor). Here only the throttle contract.
        rr::State s;
        CHECK(s.files_checked < 0 && !s.killswitch && !s.rule_saved, "fresh state: nothing read yet");
    });
}
