// Native tests (Turbo 2.1 tactics, channel A): the per-side gameplay slider game variables and the two A/B presets
// (core/gameplay_sliders.h, their rows in core/match_setup.cpp known_vars() and core/sliders.cpp). Included by test_main.cpp after its
// framework (CHECK, run_case, fmt, g_out) and after FakeMatchSetup. Pure data: no game memory, no ImGui.
//
// What is checked, in the order the task asked for: (1) every name exists in docs/re/gamevars_fc27.txt (the test reads the file),
// (2) the preset contents, (3) default off (nothing written, 50 never sent as off), (4) kill-switch gating, (5) clear-all.
#pragma once
#include <cstdlib>
#include <fstream>
#include <set>

#include "core/gameplay_sliders.h"
#include "core/match_setup.h"
#include "core/sliders.h"

namespace gameplay_sliders_test {

using namespace turbo;

// docs/re/gamevars_fc27.txt: next to this header (../../../docs/re), or from the test cwd (the turbogui root), or via TURBO_TESTS
static fs::path find_dump() {
    std::vector<fs::path> c;
    c.push_back(fs::path(__FILE__).parent_path() / ".." / ".." / ".." / "docs" / "re" / "gamevars_fc27.txt");
    c.push_back(fs::path("..") / "docs" / "re" / "gamevars_fc27.txt");
    if (const char* t = std::getenv("TURBO_TESTS")) c.push_back(fs::path(t) / ".." / ".." / "docs" / "re" / "gamevars_fc27.txt");
    for (const fs::path& p : c) {
        std::error_code ec;
        if (fs::exists(p, ec)) return p;
    }
    return fs::path();
}

// Every variable name the dump lists: "NAME<TAB>1<TAB>0x..." rows and the indented "  NAME" rows of the keyword groups
static std::set<std::string> dump_names(const fs::path& p) {
    std::set<std::string> out;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const size_t e = line.find_first_of(" \t", b);
        out.insert(line.substr(b, e == std::string::npos ? std::string::npos : e - b));
    }
    return out;
}

static void test_family() {
    run_case("channel A: the family is consistent (34 sliders, 68 valid distinct names, keys, round trip)", [] {
        const std::string bad = gs::validate();
        CHECK(bad.empty(), "gs::validate: " + bad);
        CHECK(gs::family().size() == 34, fmt("34 sliders (16 + 7 + 11), got %zu", gs::family().size()));
        const std::vector<std::string> names = gs::all_var_names();
        CHECK(names.size() == 68 && std::set<std::string>(names.begin(), names.end()).size() == 68, fmt("68 distinct names, got %zu", names.size()));
        size_t basic = 0, pos = 0, cpu = 0;
        for (const gs::Slider& s : gs::family()) {
            if (s.group == gs::Group::Basic) ++basic;
            if (s.group == gs::Group::Positioning) ++pos;
            if (s.group == gs::Group::CpuBehaviour) ++cpu;
            for (gs::Side side : {gs::Side::User, gs::Side::Cpu}) {
                const std::string n = gs::var_name(s, side);
                CHECK(gv::check_name(n).empty(), n + " is a name Turbo may set");
                CHECK(n.rfind("GAMEPLAY_CUSTOMIZATION/", 0) == 0, n + " has the family prefix");
                const gs::Slider* back = nullptr;
                gs::Side bs = gs::Side::User;
                CHECK(gs::parse_var(n, back, bs) && back == &s && bs == side, n + " parses back to its slider and side");
            }
        }
        CHECK(basic == 16 && pos == 7 && cpu == 11, fmt("groups 16 / 7 / 11, got %zu / %zu / %zu", basic, pos, cpu));
        CHECK(gs::var_name("SPRINT_SPEED", gs::Side::Cpu) == "GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU", "exact CPU name");
        CHECK(gs::var_name("POS_DEF_LINE_HEIGHT", gs::Side::User) == "GAMEPLAY_CUSTOMIZATION/POS_DEF_LINE_HEIGHT_USER", "exact USER name");
        CHECK(gs::registry_key(*gs::slider("SPRINT_SPEED"), gs::Side::User) == "match.sprint_speed_user", "registry key");
        CHECK(gs::registry_key(*gs::slider("INJURY_FREQUENCY"), gs::Side::Cpu) == "match.injury_frequency_cpu_block",
              "the block's injury names do not collide with the proven injury rows");
        const gs::Slider* no = nullptr;
        gs::Side side = gs::Side::User;
        CHECK(!gs::parse_var("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI", no, side), "the proven _CPUAI name is not of this family");
        CHECK(!gs::parse_var("NEVER_INJURE", no, side) && !gs::parse_var("GAMEPLAY_CUSTOMIZATION/NOPE_USER", no, side), "foreign names refused");
    });

    run_case("channel A: every name exists in docs/re/gamevars_fc27.txt (the test reads the file)", [] {
        const fs::path dump = find_dump();
        CHECK(!dump.empty(), "docs/re/gamevars_fc27.txt found next to the tests");
        if (dump.empty()) return;
        const std::set<std::string> names = dump_names(dump);
        CHECK(names.size() > 1000, fmt("the dump lists %zu names", names.size()));
        for (const std::string& n : gs::all_var_names()) CHECK(names.count(n) > 0, "in the dump: " + n);
        for (const char* n : {"GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI",
                              "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER", "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPUAI",
                              "GAMEPLAY_CUSTOMIZATION/POWER_BAR_SPEED_USER", "NEVER_INJURE", "OVERRIDE/WEATHER", "OVERRIDE/TOD",
                              "OVERRIDE_MATCH_DIFFICULTY", "DISABLE_CPU_SUBSTITUTION"})
            CHECK(names.count(n) > 0, std::string("proven name in the dump: ") + n);
        CHECK(names.count("GAMEPLAY_CUSTOMIZATION/NOPE_USER") == 0, "a made-up name is not in the dump (the check has teeth)");
        // every known variable Turbo offers is in the dump
        for (const gv::KnownVar& v : gv::known_vars()) CHECK(names.count(v.name) > 0, std::string("known variable in the dump: ") + v.name);
    });

    run_case("channel A: the known-variable table holds every name, 0..100, off = -1 (never sent), untested; the 9 proven ones unchanged", [] {
        size_t untested = 0;
        for (const std::string& n : gs::all_var_names()) {
            const gv::KnownVar* kv = gv::known(n);
            CHECK(kv != nullptr, "known: " + n);
            if (!kv) continue;
            CHECK(kv->min == 0 && kv->max == 100 && kv->off == -1, n + ": range 0..100, off -1");
            const bool proven_user_injury = n == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER" || n == "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER";
            CHECK(kv->untested == !proven_user_injury, n + (proven_user_injury ? ": the proven USER injury entry stays proven" : ": marked untested"));
            if (kv->untested) ++untested;
            CHECK(std::string(kv->what).find("UNTESTED") != std::string::npos || proven_user_injury, n + ": the text says untested");
        }
        CHECK(untested == 66, fmt("66 untested entries (68 minus the two proven USER injury names), got %zu", untested));
        // one entry per name (the UI and gv::known rely on it), the classic switches are exactly the 9 proven ones
        std::set<std::string> seen;
        for (const gv::KnownVar& v : gv::known_vars()) {
            CHECK(seen.insert(v.name).second, std::string("one entry per name: ") + v.name);
            CHECK(gv::known(v.name) == &v, std::string("known() finds its own entry: ") + v.name);
        }
        CHECK(gv::known_vars().size() == 9 + 66, fmt("9 proven + 66 untested = %zu", gv::known_vars().size()));
        const std::vector<const gv::KnownVar*> proven = gv::known_proven();
        CHECK(proven.size() == 9, fmt("known_proven has the 9 classic switches, got %zu", proven.size()));
        for (const gv::KnownVar* v : proven) CHECK(!v->untested, std::string("proven is not untested: ") + v->name);
        for (const char* n : {"NEVER_INJURE", "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI", "OVERRIDE/WEATHER", "OVERRIDE/TOD", "OVERRIDE_MATCH_DIFFICULTY",
                              "DISABLE_CPU_SUBSTITUTION"}) {
            const gv::KnownVar* kv = gv::known(n);
            CHECK(kv && !kv->untested, std::string("proven switch untouched: ") + n);
        }
        CHECK(gv::known("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPU") && gv::known("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPU")->untested,
              "the block's _CPU injury name is known and untested (the A/B positive control)");
        CHECK(!gv::known("GAMEPLAY_CUSTOMIZATION/POWER_BAR_SPEED_USER"), "power bar speed is not offered (one side, not a per-side slider)");
        CHECK(!gv::known("GAME_SPEED") && !gv::known("ADAPTIVE_DIFFICULTY/ENABLED"), "GAME_SPEED and the adaptive difficulty are never offered");
    });

    run_case("channel A: registry rows are Live, per side, untested, OFF by default: nothing is written until switched on, 50 is never 'off'", [] {
        const std::string bad = validate_registry();
        CHECK(bad.empty(), "validate_registry: " + bad);
        size_t rows = 0;
        SliderSet fresh;
        for (const gs::Slider& s : gs::family()) {
            for (gs::Side side : {gs::Side::User, gs::Side::Cpu}) {
                const std::string var = gs::var_name(s, side);
                const bool proven_user_injury = var == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER" || var == "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER";
                const std::string key = gs::registry_key(s, side);
                const SliderDef* d = find_slider(key);
                if (proven_user_injury) {
                    CHECK(d == nullptr, key + ": no second row for the proven USER injury variable");
                    const SliderDef* p = find_slider(s.block_index == 14 ? "match.injury_frequency_user" : "match.injury_severity_user");
                    CHECK(p && p->status == SliderStatus::Live && !p->untested && p->binding.targets.at(0) == var, var + ": the proven row owns it");
                    continue;
                }
                CHECK(d != nullptr, "registry has " + key);
                if (!d) continue;
                ++rows;
                CHECK(d->status == SliderStatus::Live && d->evidence == SliderEvidence::Real, key + ": Live (the write path is the proven gv::apply)");
                CHECK(d->untested && d->per_side, key + ": untested, per side");
                CHECK(d->scope == SliderScope::Match && d->group.rfind("Per side: ", 0) == 0, key + ": Match scope, per-side group");
                CHECK(d->binding.kind == BindingKind::GameVar && d->binding.targets.size() == 1 && d->binding.targets[0] == var, key + ": binds " + var);
                CHECK(d->min == 0 && d->max == 100 && d->def == 50 && d->kind == SliderKind::Range, key + ": 0..100, neutral default 50");
                CHECK(!fresh.is_enabled(key), key + ": off in a fresh set");
                CHECK(gv::known(var) != nullptr, key + ": its target is a known variable");
            }
        }
        CHECK(rows == 66, fmt("66 per-side rows, got %zu", rows));
        CHECK(game_writes(fresh).empty(), "a fresh set writes nothing: off means cleared, not 50");
        // switching one on sends exactly that one, at its value; switching it off again sends nothing (the panel then clears the variable)
        SliderSet one;
        CHECK(one.set("match.sprint_speed_cpu", 0), "set sprint speed (CPU) to 0");
        std::vector<SliderWrite> w = game_writes(one);
        CHECK(w.size() == 1 && w[0].target == "GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU" && w[0].value == 0 && w[0].status == SliderStatus::Live,
              "one write, the CPU sprint speed, value 0");
        one.set_enabled("match.sprint_speed_cpu", false);
        CHECK(game_writes(one).empty(), "switched off: no write (never 50)");
        // the "both teams" preview rows are untouched (still preview-only until the A/B verdict)
        for (const char* k : {"match.sprint_speed", "match.pass_error", "match.gk_ability"}) {
            const SliderDef* d = find_slider(k);
            CHECK(d && d->status == SliderStatus::RE && !d->untested, std::string(k) + " stays a preview row");
        }
        // the honesty rule: no untested row outside the Live status, and every untested row is a channel A variable
        for (const SliderDef& d : slider_registry())
            if (d.untested) {
                const gs::Slider* s = nullptr;
                gs::Side side = gs::Side::User;
                CHECK(d.status == SliderStatus::Live && gs::parse_var(d.binding.targets.at(0), s, side), d.key + ": untested rows are channel A game variables");
            }
    });
}

static void test_presets() {
    run_case("channel A: presets - the baseline clears everything and sends nothing; the extremes are exactly the CPU list", [] {
        CHECK(gs::presets().size() == 2, "two presets");
        const gs::Preset* base = gs::preset("baseline");
        const gs::Preset* ext = gs::preset("cpu_extremes");
        CHECK(base && ext && !gs::preset("nope"), "baseline and cpu_extremes exist");
        if (!base || !ext) return;
        CHECK(base->clear_all_first && base->writes.empty(), "baseline: clear all, no writes");
        CHECK(gs::describe(*base) == std::vector<std::string>{"clear every override, send nothing"}, "baseline description");
        CHECK(std::string(base->title).find("baseline") != std::string::npos && std::string(ext->title).find("CPU handicap extremes") != std::string::npos,
              "button titles");
        const std::vector<std::pair<std::string, int32_t>> want = {
            {"GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU", 0},        {"GAMEPLAY_CUSTOMIZATION/ACCELERATION_CPU", 0},
            {"GAMEPLAY_CUSTOMIZATION/PASS_ERROR_CPU", 100},        {"GAMEPLAY_CUSTOMIZATION/SHOT_ERROR_CPU", 100},
            {"GAMEPLAY_CUSTOMIZATION/TRAP_ERROR_CPU", 100},        {"GAMEPLAY_CUSTOMIZATION/GK_ABILITY_CPU", 0},
            {"GAMEPLAY_CUSTOMIZATION/POS_DEF_LINE_HEIGHT_CPU", 100}, {"GAMEPLAY_CUSTOMIZATION/POS_RUN_FREQUENCY_CPU", 0},
            {"GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPU", 100},  {"GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPU", 100},
        };
        CHECK(ext->clear_all_first, "extremes: clear all first");
        CHECK(ext->writes.size() == want.size(), fmt("extremes: %zu writes", ext->writes.size()));
        for (size_t i = 0; i < want.size() && i < ext->writes.size(); ++i)
            CHECK(ext->writes[i].name == want[i].first && ext->writes[i].value == want[i].second,
                  fmt("extremes write %zu: %s = %d", i, ext->writes[i].name.c_str(), ext->writes[i].value));
        for (const gs::Write& w : ext->writes) {
            CHECK(w.name.size() > 4 && w.name.compare(w.name.size() - 4, 4, "_CPU") == 0, w.name + ": CPU side only (your team is untouched)");
            CHECK(w.name.find("_CPUAI") == std::string::npos, w.name + ": the _CPU block name, not the proven _CPUAI one");
            CHECK(gv::known(w.name) != nullptr && w.value >= 0 && w.value <= 100 && w.value != gs::kOff, w.name + ": known, 0..100, not 'off'");
        }
        const std::vector<std::string> lines = gs::describe(*ext);
        CHECK(lines.size() == want.size() + 1 && lines[0] == "clear every override first" && lines[1] == "GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU = 0" &&
                  lines.back() == "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_CPU = 100",
              "the preview lists the clear, then every write with its value");
    });

    run_case("channel A: kill switches gate the plan: tactics_off.txt or call_gamevar_off.txt means nothing is sent", [] {
        const gs::Preset* ext = gs::preset("cpu_extremes");
        std::vector<gs::Write> out;
        std::string why = gs::plan(*ext, true, false, "", out);
        CHECK(!why.empty() && why.find("tactics_off.txt") != std::string::npos && out.empty(), "tactics_off.txt blocks: " + why);
        why = gs::plan(*ext, false, true, "kill switch turbo_output\\call_gamevar_off.txt is present", out);
        CHECK(!why.empty() && why.find("call_gamevar_off.txt") != std::string::npos && out.empty(), "call_gamevar_off.txt blocks: " + why);
        why = gs::plan(*ext, true, true, "x", out);
        CHECK(!why.empty() && out.empty(), "both switches block");
        why = gs::plan(*ext, false, false, "", out);
        CHECK(why.empty() && out.size() == ext->writes.size(), "no switch: the plan holds every write");
        for (size_t i = 0; i < out.size(); ++i) CHECK(out[i].name == ext->writes[i].name && out[i].value == ext->writes[i].value, "planned write unchanged");
        // the host refusing (vars not ready) means apply sends nothing either
        FakeMatchSetup fake;
        fake.vars_off = true;
        const gs::ApplyReport r = gs::apply(fake, *ext, out);
        CHECK(r.sent == 0 && fake.sets.empty(), "host off: nothing sent");
        CHECK(r.refused == int(out.size()) && r.first_error.find("call_gamevar_off.txt") != std::string::npos, "every write refused with the kill switch text: " + r.first_error);
        // a preset naming an unknown variable is refused as a whole
        gs::Preset bogus{"bogus", "bogus", "", true, {{"GAMEPLAY_CUSTOMIZATION/NOPE_CPU", 1}}};
        why = gs::plan(bogus, false, false, "", out);
        CHECK(!why.empty() && out.empty(), "unknown variable refuses the preset: " + why);
    });

    run_case("channel A: apply clears every active override first, then sends the writes; the baseline only clears; 'off' is never sent", [] {
        FakeMatchSetup fake;
        for (const char* n : {"GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_USER", "OVERRIDE/WEATHER"}) {
            msetup::VarLine l;
            l.name = n;
            l.value = 3;
            l.active = true;
            fake.lines[n] = l;
        }
        const gs::Preset* base = gs::preset("baseline");
        std::vector<gs::Write> out;
        CHECK(gs::plan(*base, false, false, "", out).empty() && out.empty(), "baseline plan: nothing to send");
        gs::ApplyReport r = gs::apply(fake, *base, out);
        CHECK(r.cleared_all == 1 && r.sent == 0 && r.refused == 0, "baseline: cleared, nothing sent");
        CHECK(fake.clears.size() == 2 && fake.sets.empty(), fmt("baseline cleared the 2 active overrides (%zu) and set none (%zu)", fake.clears.size(), fake.sets.size()));
        // the extremes: clear first, then the 10 writes in order
        fake.clears.clear();
        const gs::Preset* ext = gs::preset("cpu_extremes");
        CHECK(gs::plan(*ext, false, false, "", out).empty(), "extremes plan");
        r = gs::apply(fake, *ext, out);
        CHECK(r.cleared_all == 1 && r.sent == 10 && r.refused == 0 && r.first_error.empty(), fmt("extremes: cleared, 10 sent (%d), 0 refused (%d)", r.sent, r.refused));
        CHECK(fake.clears.size() == 2, "the extremes cleared the active overrides first");
        CHECK(fake.sets.size() == 10, fmt("10 set calls, got %zu", fake.sets.size()));
        for (size_t i = 0; i < fake.sets.size() && i < ext->writes.size(); ++i)
            CHECK(fake.sets[i].first == ext->writes[i].name && fake.sets[i].second == ext->writes[i].value, "set " + fake.sets[i].first);
        for (const auto& s : fake.sets) CHECK(s.first.find("_USER") == std::string::npos, s.first + ": your team is never touched by the extremes");
        // a planned 'off' (-1) or unknown name is refused, not sent
        fake.sets.clear();
        std::vector<gs::Write> planned = {{"GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU", gs::kOff}, {"GAMEPLAY_CUSTOMIZATION/NOPE_CPU", 5}, {"GAMEPLAY_CUSTOMIZATION/PASS_ERROR_CPU", 250}};
        r = gs::apply(fake, *ext, planned);
        CHECK(r.sent == 1 && r.refused == 2 && fake.sets.size() == 1 && fake.sets[0].first == "GAMEPLAY_CUSTOMIZATION/PASS_ERROR_CPU" && fake.sets[0].second == 100,
              "off and unknown refused, an out-of-range value clamped to 100");
        CHECK(r.first_error.find("'off' is never sent") != std::string::npos, "the refusal names the rule: " + r.first_error);
    });

    run_case("channel A: clear-all after the extremes leaves no active override (the host ledger drives clear_all)", [] {
        FakeMatchSetup fake;
        const gs::Preset* ext = gs::preset("cpu_extremes");
        std::vector<gs::Write> out;
        CHECK(gs::plan(*ext, false, false, "", out).empty(), "plan");
        gs::apply(fake, *ext, out);
        // the game thread reports every set as ok: the ledger marks them active
        for (const auto& s : fake.sets) {
            msetup::VarResult r;
            r.ok = true;
            r.name = s.first;
            r.value = s.second;
            fake.results.push_back(r);
        }
        msetup::VarResult r;
        size_t polled = 0;
        while (fake.poll(r)) ++polled;
        size_t active = 0;
        for (const msetup::VarLine& l : fake.vars()) active += l.active ? 1 : 0;
        CHECK(polled == 10 && active == 10, fmt("10 outcomes polled (%zu), 10 active overrides (%zu)", polled, active));
        fake.clears.clear();
        std::string why;
        CHECK(fake.clear_all(why), "clear all accepted");
        CHECK(fake.clears.size() == 10, fmt("clear all clears exactly the 10 active names, got %zu", fake.clears.size()));
        std::set<std::string> cleared(fake.clears.begin(), fake.clears.end());
        for (const gs::Write& w : ext->writes) CHECK(cleared.count(w.name) > 0, "cleared: " + w.name);
    });
}

static void run() {
    std::printf("native channel A: per-side gameplay sliders and A/B presets\n");
    test_family();
    test_presets();
}

}  // namespace gameplay_sliders_test
