// Native tests (2.20 S2-team-defaults): the "My team" preset (core/team_preset.h), the growth defaults and the Kits & Club descriptors.
//   run_core(): the five-field constant (a sixth field fails), the squad role rule (Prospect = under 23 AND below the club median,
//   Rotation = every other player; the over-23-below-median case), the team_mass command shape (explicit action list, never "all",
//   role_save false), the growth defaults OFF with and without a saved gui_settings value.
//   run_ui(): team_preset_apply on the GUI world's user club: jersey fit tight on every player of the club and no other player field
//   changed, exactly one team_mass command with the four game-side actions, refusal for another club.
//   descriptor cases: every code the Kits & Club page draws has a descriptor (kind != Unnamed), see descriptor_cases().
// Included by test_main.cpp after test_create_prefill.h. Players are referred to by id only.
#pragma once

#include "core/field_labels.h"
#include "core/team_preset.h"
#include "ui/ui_identity.h"

namespace team_preset_test {

namespace tp = turbo::teampreset;

static void run_core() {
    run_case("team preset: the field list is exactly the five fields (a sixth field fails this test)", [&] {
        const auto& f = tp::fields();
        CHECK(f.size() == 5 && tp::kFieldCount == 5, fmt("%zu fields", f.size()));
        const char* want_ids[5] = {"jersey_fit", "contract", "squad_role", "morale", "block_offers"};
        const char* want_actions[5] = {"", "long_contract", "squad_roles", "morale", "block_offers"};
        for (size_t i = 0; i < 5 && i < f.size(); ++i) {
            CHECK(std::string(f[i].id) == want_ids[i], std::string("field ") + std::to_string(i) + " is " + f[i].id);
            CHECK(std::string(f[i].action) == want_actions[i], std::string("action of ") + f[i].id + " is '" + f[i].action + "'");
            CHECK(std::string(f[i].title).size() > 3, std::string("title of ") + f[i].id);
        }
        CHECK(tp::kJerseyFitTight == 1 && tp::kContractMonths == 60 && tp::kRoleRotation == 3 && tp::kRoleProspect == 5 && tp::kProspectBelowAge == 23,
              "the preset's constants: tight 1, 60 months, Rotation 3, Prospect 5, under 23");
        // the game-side fields the window does not write itself are exactly the team_mass actions of the list
        json ov = tp::team_mass_overrides(7, {});
        CHECK(ov["actions"] == json::array({"long_contract", "squad_roles", "morale", "block_offers"}), "actions: " + ov["actions"].dump());
    });

    run_case("team preset: squad role rule - Prospect = under 23 AND below the club median, Rotation = everyone else", [&] {
        // 7 players: ovr 60 61 62 70 71 80 90 -> median 70
        const std::vector<int> o = {71, 60, 90, 62, 70, 80, 61};
        CHECK(tp::median_ovr(o) == 70.0, fmt("odd squad median %.1f", tp::median_ovr(o)));
        CHECK(tp::median_ovr({60, 70, 80, 90}) == 75.0, "even squad: the mean of the two middle ones");
        CHECK(tp::median_ovr({}) == 0.0 && tp::median_ovr({77}) == 77.0, "no player 0, one player himself");
        const double med = 70.0;
        CHECK(tp::squad_role_for(19, 60, med) == tp::kRoleProspect, "19 years, below the median: Prospect");
        CHECK(tp::squad_role_for(22, 69, med) == tp::kRoleProspect, "22 years, one below the median: Prospect");
        CHECK(tp::squad_role_for(23, 69, med) == tp::kRoleRotation, "23 years, below the median: Rotation (the over-23 case)");
        CHECK(tp::squad_role_for(31, 40, med) == tp::kRoleRotation, "31 years, far below the median: Rotation");
        CHECK(tp::squad_role_for(19, 70, med) == tp::kRoleRotation, "19 years exactly at the median: Rotation (below is strict)");
        CHECK(tp::squad_role_for(18, 95, med) == tp::kRoleRotation, "18 years above the median: Rotation");
        CHECK(tp::squad_role_for(28, 80, med) == tp::kRoleRotation, "28 years above the median: Rotation");
        CHECK(tp::squad_role_for(-1, 60, med) == tp::kRoleLeaveAlone, "unknown age below the median: pinned 0 (left alone)");
        CHECK(tp::squad_role_for(-1, 70, med) == tp::kRoleRotation, "unknown age at the median is still Rotation (the age does not matter)");
        std::vector<tp::SquadPlayer> sq = {{1, 71, 30}, {2, 60, 19}, {3, 90, 21}, {4, 62, 24}, {5, 70, 20}, {6, 80, 33}, {7, 61, 22}};
        json pins = tp::role_pins(sq);
        CHECK(pins.size() == 7, "one pin per player");
        CHECK(pins["1"] == 3 && pins["2"] == 5 && pins["3"] == 3 && pins["4"] == 3 && pins["5"] == 3 && pins["6"] == 3 && pins["7"] == 5,
              "pins: " + pins.dump());  // 4 is 24 and below the median: Rotation; 5 is at the median: Rotation
        for (auto it = pins.begin(); it != pins.end(); ++it) CHECK(it.value() == 3 || it.value() == 5, "only Rotation or Prospect for known ages");
        json ov = tp::team_mass_overrides(7, sq);
        CHECK(ov["teamid"] == 7 && ov["role_pins"] == pins && ov["role_save"] == false && !ov.contains("role_rules") && !ov.contains("role_preview"),
              "overrides: " + ov.dump());
        for (const auto& a : ov["actions"]) CHECK(a != "all" && a != "form" && a != "match_xp" && a != "dev_bonus", "no growth or form action: " + a.dump());
    });

    run_case("team preset: Extra XP development and Does not decline are OFF by default, saved gui_settings cannot turn them on", [&] {
        CHECK(!tp::kExtraXpDefault && !tp::kNoDeclineDefault, "the constants are false");
        CHECK(!tp::growth_default_extra_xp(json::object()) && !tp::growth_default_no_decline(json::object()), "no settings: off");
        CHECK(!tp::growth_default_extra_xp(json()) && !tp::growth_default_no_decline(json()), "null settings: off");
        const json saved = {{"no_decline", true}, {"extra_xp", true}, {"use_dev", true}, {"squad_no_decline", true}, {"dev_bonus", true}};
        CHECK(!tp::growth_default_extra_xp(saved) && !tp::growth_default_no_decline(saved), "an older Turbo's saved true values are ignored");
    });
}

// ---- Kits & club descriptors (brief goal c): every code the page draws has a descriptor over its whole schema range
static bool schema_field_range(const json& schema, const std::string& table, const std::string& field, int64_t& lo, int64_t& hi) {
    if (!schema.contains("tables") || !schema["tables"].contains(table)) return false;
    for (const auto& f : schema["tables"][table]["fields"]) {
        if (f.value("name", "") != field) continue;
        lo = f.value("min", 0LL);
        hi = f.contains("max") ? f["max"].get<int64_t>() : lo + ((int64_t(1) << f.value("depth", 1)) - 1);
        return true;
    }
    return false;
}

static bool says_code_number(const std::string& d) {
    std::string low = d;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (size_t i = 0; (i = low.find("code ", i)) != std::string::npos; i += 5)
        if (i + 5 < low.size() && std::isdigit(static_cast<unsigned char>(low[i + 5]))) return true;
    return false;
}

static bool all_digits(const std::string& d) {
    return !d.empty() && std::all_of(d.begin(), d.end(), [](unsigned char c) { return std::isdigit(c) || c == '-'; });
}

static void run_descriptors() {
    namespace lb = turbo::labels;
    run_case("Kits & club: the page's code fields all have a descriptor over their whole schema range (kind is never Unnamed), the code on hover only", [&] {
        const char* tests_dir = std::getenv("TURBO_TESTS");
        const std::string schema_path = std::string(tests_dir ? tests_dir : "../turbo/tests") + "/../le27/fc27_db_schema.json";
        json schema;
        {
            std::ifstream in(schema_path);
            CHECK(in.good(), "the game DB schema is readable: " + schema_path);
            if (in.good()) in >> schema;
        }
        struct PF {
            std::string table, field;
        };
        std::vector<PF> page;
        for (const std::string& f : lb::page_stadium_fields()) page.push_back({"teams", f});
        for (const std::string& f : lb::page_kit_template_fields()) page.push_back({"teamkits", f});
        page.push_back({"teamkits", lb::page_kit_type_field()});
        CHECK(page.size() == 10, fmt("%zu page code fields (6 stadium, 3 templates, the kit type)", page.size()));
        int found = 0;
        for (const PF& pf : page) {
            int64_t lo = 0, hi = 0;
            const bool known = schema_field_range(schema, pf.table, pf.field, lo, hi);
            CHECK(known, pf.table + "." + pf.field + " is in the game schema");
            if (!known) continue;
            ++found;
            CHECK(lb::is_code_field(pf.field), pf.field + " is drawn as a combo of descriptors, not a bare number");
            const std::string title = lb::field_title(pf.field);
            CHECK(!title.empty() && title.find(pf.field) == std::string::npos && title.find(' ') != std::string::npos, pf.field + " has a plain title: " + title);
            int bad = 0;
            std::string first;
            for (int64_t v = lo; v <= hi; ++v) {
                const lb::Label l = lb::code_label(pf.field, v, title);
                const std::string d = lb::describe(pf.table, pf.field, v);
                if (l.kind == lb::Kind::Unnamed || l.text.empty() || all_digits(l.text) || says_code_number(l.text) || d != l.text) {
                    if (!bad) first = std::to_string(v) + " -> '" + l.text + "' / '" + d + "'";
                    ++bad;
                }
            }
            CHECK(bad == 0, fmt("%s.%s [%lld..%lld]: %d without a descriptor, first %s", pf.table.c_str(), pf.field.c_str(), static_cast<long long>(lo),
                                static_cast<long long>(hi), bad, first.c_str()));
        }
        CHECK(found == 10, fmt("%d of 10 fields found in the schema", found));
        // the percentages stay numbers (they are not codes) and have titles
        for (const char* pct : {"teamcolorprimpercent", "teamcolorsecpercent", "teamcolortertpercent"})
            CHECK(!lb::is_code_field(pct) && lb::field_title(pct).find("(%)") != std::string::npos, std::string(pct) + " is a percentage with a title");
    });

    run_case("Kits & club: kit types are named (Live Editor 0..3, the game's kit folders 4..9), one table for the page header and the combos", [&] {
        namespace lb = turbo::labels;
        const char* want[10] = {"Home", "Away", "Goalkeeper", "Third", "Fourth", "Referee", "Training home", "Training away", "Training home bibs", "Training away bibs"};
        for (int t = 0; t < 10; ++t) {
            const lb::Label l = lb::code_label("teamkittypetechid", t, "Kit type");
            CHECK(l.text == want[t] && l.kind == (t < 4 ? lb::Kind::Named : lb::Kind::Described), fmt("kit type %d: '%s'", t, l.text.c_str()));
            CHECK(std::string(turbo::kit_type_name(t)) == want[t], fmt("the page header says '%s' for type %d", turbo::kit_type_name(t), t));
        }
        CHECK(lb::code_label("teamkittypetechid", 14, "Kit type").text == "Kit type #14" && std::string(turbo::kit_type_name(14)) == "Kit", "a type beyond the game's ten");
        const std::vector<int64_t> offered = lb::offered_codes("teamkittypetechid", {}, 0);
        CHECK(offered.size() == 10, fmt("the combo offers the ten kit types (%zu)", offered.size()));
        // the crowd's skin tones use the players' ten-step scale (the base file holds 20, 40, 70, 90)
        CHECK(lb::code_label("crowdskintonecode", 20, "").text == "Caucasian 2" && lb::code_label("crowdskintonecode", 40, "").text == "Latin Asian 1" &&
                  lb::code_label("crowdskintonecode", 70, "").text == "Latin Asian 4" && lb::code_label("crowdskintonecode", 90, "").text == "African 2" &&
                  lb::code_label("crowdskintonecode", 55, "").kind == lb::Kind::Described,
              "crowd skin tone");
        CHECK(lb::describe("players", "skintonecode", 30) == "Caucasian 3", "the players' skin tones are unchanged");
        // item catalogues read "<what> #N" (the number is part of the descriptor, as for boots); the first and last value of each page field
        CHECK(lb::describe("teams", "stadiumgoalnetstyle", 63) == "Goal net style #63" && lb::describe("teamkits", "jerseytemplateindex", 0) == "Shirt template #0" &&
                  lb::describe("teams", "pitchcolor", 10) == "Pitch colour #10",
              "item descriptors");
    });
}

// UI part: needs the GUI world (user club 1) with the players.jerseyfit field (gui_world.lua).
static void run_ui(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    run_case("UI: the My team preset writes tight shirts to the whole club and nothing else, then sends one team_mass command", [&] {
        (void)ui;
        if (app.busy()) app.mailbox->cancel();
        app.lua_queue.clear();
        const Table* pt = app.db.table("players");
        CHECK(pt != nullptr && pt->field("jerseyfit") != nullptr, "players.jerseyfit exists in the test world");
        if (!pt || !pt->field("jerseyfit")) return;
        const int64_t club = app.bridge.state().user_team;
        CHECK(club == 1, fmt("the user's club is %lld", static_cast<long long>(club)));
        // before: every int field of every player
        std::map<std::pair<int64_t, std::string>, int64_t> before;
        std::set<int64_t> mine;
        for (const turbo::LinkRow& l : app.model.links_of_team(club)) mine.insert(l.playerid);
        CHECK(mine.size() >= 3, fmt("%zu players in the club", mine.size()));
        std::vector<int64_t> all;
        for (const turbo::PlayerRow& p : app.model.players()) all.push_back(p.playerid);
        auto snap = [&](std::map<std::pair<int64_t, std::string>, int64_t>& out) {
            out.clear();
            for (int64_t pid : all) {
                const uint64_t rec = app.db.find(*pt, "playerid", pid);
                if (!rec) continue;
                for (const turbo::Field& f : pt->fields)
                    if (f.type == turbo::FieldType::Int) out[{pid, f.name}] = app.db.get_int(*pt, rec, f.name, -999);
            }
        };
        snap(before);
        CHECK(before.size() > 100, fmt("%zu values read", before.size()));
        int not_tight = 0;
        for (int64_t pid : mine) not_tight += before[{pid, "jerseyfit"}] != 1;
        CHECK(not_tight >= 2, fmt("%d players of the club start not tight", not_tight));  // the world: 0 for all but one (2)

        // another club: refused, nothing written, nothing sent
        CHECK(!turbo::team_preset_apply(app, 2), "another club's id is refused");
        CHECK(!app.mailbox->pending(), "nothing sent for another club");
        std::map<std::pair<int64_t, std::string>, int64_t> mid;
        snap(mid);
        CHECK(mid == before, "refusal wrote nothing");

        std::vector<std::string> lines;
        auto saved_hook = app.log_hook;
        app.log_hook = [&](const std::string& s) { lines.push_back(s); };
        const bool applied = turbo::team_preset_apply(app, club);
        app.log_hook = saved_hook;
        CHECK(applied, "the preset is applied to the user's club");
        ui.frames(2);
        CHECK(app.mailbox->pending(), "a command was queued");
        json cmd = json::parse(mem.read_cstr(kMb + kMbCmd, kMbTextSize), nullptr, false);
        CHECK(!cmd.is_discarded() && cmd.value("op", "") == "run" && cmd.value("module", "") == "team_mass", "module team_mass: " + cmd.dump());
        if (!cmd.is_discarded()) {
            const json& o = cmd["overrides"];
            CHECK(o["teamid"] == club && o["actions"] == json::array({"long_contract", "squad_roles", "morale", "block_offers"}) && o["role_save"] == false,
                  "overrides: " + o.dump());
            CHECK(o["role_pins"].size() == mine.size(), "one role pin per player of the club");
            // the pins follow the rule on the test world's own ovr and age
            std::vector<tp::SquadPlayer> sq;
            for (int64_t pid : mine)
                if (const turbo::PlayerRow* p = app.model.player(pid)) sq.push_back({pid, p->overall, p->age});
            CHECK(o["role_pins"] == tp::role_pins(sq), "the pins are the rule applied to the club: " + o["role_pins"].dump());
            bool prospect = false, rotation = false;
            for (auto it = o["role_pins"].begin(); it != o["role_pins"].end(); ++it) {
                prospect |= it.value() == tp::kRoleProspect;
                rotation |= it.value() == tp::kRoleRotation;
            }
            CHECK(rotation, "the club has Rotation players");
            (void)prospect;
        }
        CHECK(app.pending_label == tp::label(), "label: " + app.pending_label);

        // after: tight for the whole club; every other value of every player unchanged
        std::map<std::pair<int64_t, std::string>, int64_t> after;
        snap(after);
        int wrong = 0, changed_other = 0;
        for (const auto& kv : after) {
            const bool is_fit = kv.first.second == "jerseyfit";
            if (is_fit && mine.count(kv.first.first)) wrong += kv.second != 1;
            else if (kv.second != before[kv.first]) ++changed_other;
        }
        CHECK(wrong == 0, fmt("%d players of the club are not tight", wrong));
        CHECK(changed_other == 0, fmt("%d other values changed (must be 0: players outside the club, and every other field)", changed_other));
        bool saw_summary = false, saw_sent = false;
        int fit_lines = 0;
        for (const std::string& l : lines) {  // what turbo_gui.log gets (log_hook)
            saw_summary |= l.find("My team preset: jersey fit tight on " + std::to_string(mine.size()) + " of " + std::to_string(mine.size()) + " players (" +
                                  std::to_string(not_tight) + " changed, 0 not written)") != std::string::npos;
            saw_sent |= l.find("My team preset: sent team_mass for team 1 actions=long_contract,squad_roles,morale,block_offers role_save=false role_pins=" +
                               std::to_string(mine.size())) != std::string::npos;
        }
        for (const std::string& l : app.log_lines) fit_lines += l.find("[players.jerseyfit = 1]") != std::string::npos;  // the in-app edit log
        CHECK(saw_summary, "turbo_gui.log has the summary line");
        CHECK(saw_sent, "turbo_gui.log has the sent line with exactly the four actions");
        CHECK(fit_lines >= not_tight, fmt("%d jersey fit write lines in the edit log for %d changed players", fit_lines, not_tight));

        // the result of the Lua side lands in Mass actions
        app.mailbox->cancel();
        app.lua_queue.clear();
        CHECK(turbo::team_preset_apply(app, club), "a second apply is accepted");
        CHECK(app.mailbox->pending(), "second command queued");
        snap(mid);
        CHECK(mid == after, "the second apply changed no value (already tight)");
        app.mailbox->cancel();
    });
}

}  // namespace team_preset_test
