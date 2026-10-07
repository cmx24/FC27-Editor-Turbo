// Native tests (Turbo 2.0, track b-core): the slider registry, the tactic profile store, the tactics geometry / preview model and the
// opposition solver with its rules (core/sliders, core/tactic_profiles, core/tactics, core/opposition, core/opp_rules). Included by
// test_main.cpp after its framework (CHECK, run_case, fmt, g_out, g_lua). Pure data: no game memory, no ImGui.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <regex>

#include "core/opp_rules.h"
#include "core/opposition.h"
#include "core/sliders.h"
#include "core/tactic_profiles.h"
#include "core/tactics.h"

namespace tactics_core_test {

using namespace turbo;

static fs::path scratch(const char* name) {
    const fs::path d = g_out / "tactics_core" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

static const PreviewLine* line_of(const PreviewModel& m, const char* id) {
    for (const PreviewLine& l : m.lines)
        if (l.id == id) return &l;
    return nullptr;
}

static double arrow_total(const PreviewModel& m) {
    double s = 0;
    for (const PreviewArrow& a : m.arrows) s += a.y1 - a.y0;
    return s;
}

static PreviewModel preview_with(const char* formation, const std::vector<std::pair<const char*, int>>& vals, Phase ph = Phase::Overall,
                                 bool heat = false) {
    SliderSet t;
    for (const auto& kv : vals) t.set(kv.first, kv.second);
    PreviewOptions o;
    o.phase = ph;
    o.show_heat = heat;
    return build_preview(*find_fallback_formation(formation), t, SliderSet{}, o);
}

// ---------------------------------------------------------------------------------------------------------------- sliders
static void test_sliders() {
    run_case("sliders: registry is consistent, keys unique, defaults in range, the plan's catalogue is there", [] {
        const std::string bad = validate_registry();
        CHECK(bad.empty(), "validate_registry: " + bad);
        std::set<std::string> keys;
        for (const SliderDef& d : slider_registry()) keys.insert(d.key);
        CHECK(keys.size() == slider_registry().size(), "every key is unique");
        CHECK(slider_registry().size() >= 90, fmt("catalogue size %zu", slider_registry().size()));
        for (const char* k : {"match.injury_frequency_user", "match.injury_frequency_cpu", "match.injury_severity_user", "match.injury_severity_cpu",
                              "match.never_injure", "match.weather", "match.time_of_day", "match.difficulty", "match.cpu_subs_off",
                              "match.sprint_speed", "match.pass_error", "match.shot_error", "match.first_touch_error", "match.gk_ability",
                              "match.foul_strictness", "match.card_strictness", "team.formation", "team.mentality", "team.line_depth",
                              "team.line_length", "team.width_def", "team.width_att", "team.marking", "team.press_intensity", "team.press_trigger",
                              "team.engagement_height", "team.forward_runs", "team.tempo", "team.directness", "team.buildup_short",
                              "team.cross_freq", "team.shot_patience", "team.dribble_freq", "team.counter_press", "team.counter_attack",
                              "team.regroup_depth", "team.offside_trap", "team.tackle_aggr", "team.buildup_play", "team.chance_creation",
                              "team.defensive_style", "pos.role1", "pos.role9", "pos.forward_runs", "pos.roam", "pos.hold_pos", "pos.close_down",
                              "pos.shot_freq", "pos.risk_passing", "pos.cross_depth", "opp.injury_frequency_offset", "opp.difficulty_offset",
                              "opp.variety_strength", "opp.quality_scaling", "opp.mirroring", "opp.underdog_bias", "opp.favourite_bias", "opp.jitter",
                              "opp.family_weight_gegenpress", "opp.family_weight_balanced", "opp.formation", "opp.adaptation"})
            CHECK(find_slider(k) != nullptr, std::string("catalogue has ") + k);
        CHECK(find_slider("nope.nothing") == nullptr, "unknown key is null");
        for (const SliderDef& d : slider_registry()) {
            CHECK(d.def >= d.min && d.def <= d.max, d.key + ": default in range");
            CHECK(clamp_slider(d, d.def) == d.def, d.key + ": the default is a legal value");
        }
        CHECK(!sliders_in_scope(SliderScope::Match).empty() && !sliders_in_scope(SliderScope::Team).empty() &&
                  !sliders_in_scope(SliderScope::Position).empty() && !sliders_in_scope(SliderScope::Opposition).empty(),
              "every scope has sliders");
    });

    run_case("sliders: honesty - NEEDS-RE rows never write, only Live and DB reach the game", [] {
        size_t re = 0, live = 0, db = 0;
        for (const SliderDef& d : slider_registry()) {
            if (d.status == SliderStatus::RE) {
                ++re;
                CHECK(!writes_game(d) && preview_only(d) && d.binding.kind == BindingKind::None && d.evidence == SliderEvidence::NeedsRe,
                      d.key + ": RE is preview-only with no binding");
            }
            if (d.status == SliderStatus::Preview) CHECK(!writes_game(d) && preview_only(d), d.key + ": Preview never writes");
            if (d.status == SliderStatus::Local) CHECK(!writes_game(d), d.key + ": Local is not sent to the game");
            if (d.status == SliderStatus::Live) {
                ++live;
                CHECK(d.evidence == SliderEvidence::Real && d.binding.kind == BindingKind::GameVar && !d.binding.targets.empty(), d.key + ": Live is a game variable");
            }
            if (d.status == SliderStatus::DB) {
                ++db;
                CHECK(d.binding.kind == BindingKind::DbField && !d.binding.targets.empty(), d.key + ": DB has a table.field");
            }
        }
        CHECK(re >= 40 && live >= 9 && db >= 20, fmt("re %zu live %zu db %zu", re, live, db));
        // the plan's Live rows are exactly the proven game variables
        auto* d = find_slider("match.injury_frequency_cpu");
        CHECK(d && d->binding.targets.at(0) == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI" && d->status == SliderStatus::Live, "injury frequency cpu binding");
        d = find_slider("match.time_of_day");
        CHECK(d && d->allowed == std::vector<int>({0, 1, 3, 4}), "time of day allows 0 1 3 4");
        // promoted from the schema
        for (const char* k : {"team.line_depth", "team.buildup_play", "team.chance_creation", "team.offensive_style", "team.defensive_style",
                              "team.width_def", "team.width_att", "team.players_in_box_cross", "match.card_strictness", "match.foul_strictness"}) {
            d = find_slider(k);
            CHECK(d && d->status == SliderStatus::DB, std::string(k) + " is DB (schema-proven)");
        }
        CHECK(find_slider("team.mentality")->status == SliderStatus::RE && find_slider("team.tempo")->status == SliderStatus::RE, "unproven rows stay RE");
        CHECK(find_slider("team.formation")->status == SliderStatus::Preview, "formation stays Preview");
        // the preview-only strip
        const auto team = sliders_in_scope(SliderScope::Team);
        const size_t n = count_preview_only(team);
        CHECK(n > 0 && n < team.size(), fmt("team: %zu of %zu preview-only", n, team.size()));
    });

    run_case("sliders: clamp_slider - range, step, enum with gaps, monotone", [] {
        const SliderDef* inj = find_slider("match.injury_frequency_user");
        CHECK(clamp_slider(*inj, -5) == 0 && clamp_slider(*inj, 1000) == 100 && clamp_slider(*inj, 37) == 37, "range clamp");
        const SliderDef* tod = find_slider("match.time_of_day");
        CHECK(clamp_slider(*tod, 2) == 1 && clamp_slider(*tod, 9) == 4 && clamp_slider(*tod, -3) == 0 && clamp_slider(*tod, 3) == 3, "time of day snaps to an allowed value");
        const SliderDef* w = find_slider("match.weather");
        CHECK(clamp_slider(*w, -1) == 0 && clamp_slider(*w, 99) == 8, "weather range 0..8");
        SliderDef st = *inj;
        st.step = 10;
        CHECK(clamp_slider(st, 14) == 10 && clamp_slider(st, 16) == 20 && clamp_slider(st, 100) == 100 && clamp_slider(st, 97) == 100, "step snaps to the nearest step");
        st.max = 95;
        CHECK(clamp_slider(st, 99) <= 95, "step never leaves the range");
        for (const SliderDef& d : slider_registry()) {
            int prev = clamp_slider(d, d.min - 50);
            for (int v = d.min - 50; v <= std::min(d.max + 50, d.min + 300); v += 7) {
                const int c = clamp_slider(d, v);
                CHECK(c >= d.min && c <= d.max, d.key + ": clamp stays in range");
                CHECK(c >= prev, d.key + ": clamp is monotone");
                CHECK(clamp_slider(d, c) == c, d.key + ": clamp is idempotent");
                prev = c;
            }
        }
        const SliderDef* diff = find_slider("opp.difficulty_offset");
        CHECK(clamp_slider(*diff, -9) == -5 && clamp_slider(*diff, 9) == 5, "signed offsets");
    });

    run_case("sliders: SliderSet - set, enable, reset, writes, diff", [] {
        SliderSet s;
        CHECK(!s.set("nope", 5) && s.values.empty(), "an unknown key stores nothing");
        CHECK(s.set("match.injury_frequency_user", 500) && s.values.at("match.injury_frequency_user") == 100 && s.is_enabled("match.injury_frequency_user"), "set clamps and enables");
        CHECK(s.set("match.weather", 3, false) && s.has("match.weather") && !s.is_enabled("match.weather"), "set without enabling");
        CHECK(s.value_or_default("match.difficulty") == 3 && s.value_or_default("zzz") == 0, "defaults for unset keys");
        CHECK(s.set_enabled("match.difficulty", true) && s.value_or_default("match.difficulty") == 3 && s.is_enabled("match.difficulty"), "enabling an unset key gives its default");
        CHECK(!s.set_enabled("zzz", true), "enabling an unknown key fails");
        // game writes: only enabled Live / DB sliders
        s.set("team.tempo", 80);          // RE, enabled
        s.set("team.line_depth", 70);     // DB, enabled
        const auto w = game_writes(s);
        std::map<std::string, int> got;
        for (const SliderWrite& x : w) got[x.target] = x.value;
        CHECK(got.count("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER") && got.at("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER") == 100, "enabled Live is written");
        CHECK(got.count("OVERRIDE_MATCH_DIFFICULTY") && !got.count("OVERRIDE/WEATHER"), "a disabled slider is never written");
        CHECK(got.count("teams.defensivedepth") && got.at("teams.defensivedepth") == 70, "enabled DB is written");
        bool tempo = false;
        for (const SliderWrite& x : w) tempo |= x.key == "team.tempo";
        CHECK(!tempo, "an enabled RE slider is never written");
        CHECK(s.enabled_keys(SliderScope::Team).size() == 2 && s.enabled_keys(SliderScope::Match).size() == 2, "enabled_keys by scope");
        SliderSet b = s;
        CHECK(diff_sliders(s, b).empty(), "equal sets: no diff");
        b.set("team.tempo", 60);
        b.set_enabled("match.difficulty", false);
        b.set("match.cpu_subs_off", 1);
        auto d = diff_sliders(s, b);
        std::map<std::string, SliderChange> dm;
        for (auto& c : d) dm[c.key] = c;
        CHECK(d.size() == 3 && dm["team.tempo"].a == 80 && dm["team.tempo"].b == 60 && dm["match.difficulty"].has_a && !dm["match.difficulty"].has_b &&
                  !dm["match.cpu_subs_off"].has_a && dm["match.cpu_subs_off"].has_b,
              fmt("diff has %zu lines", d.size()));
        s.reset("team.tempo");
        CHECK(!s.has("team.tempo") && !s.is_enabled("team.tempo"), "reset forgets value and flag");
        s.clear();
        CHECK(s.empty() && s.enabled_count() == 0, "clear");
    });

    run_case("sliders: every DB-bound slider's table and field exists in the FC 27 schema (skipped without the file)", [] {
        fs::path schema;
        if (const char* t = std::getenv("TURBO_TESTS")) schema = fs::path(t) / ".." / "le27" / "fc27_db_schema.json";
        if (schema.empty() || !fs::exists(schema)) schema = fs::path(g_lua).parent_path() / ".." / ".." / ".." / "turbo" / "le27" / "fc27_db_schema.json";
        if (!fs::exists(schema)) {
            std::printf("    (no fc27_db_schema.json: schema check skipped)\n");
            CHECK(true, "skipped");
            return;
        }
        const json js = read_json(schema);
        CHECK(js.contains("tables") && js["tables"].is_object(), "schema has tables");
        const json& tables = js["tables"];
        size_t checked = 0;
        for (const SliderDef& d : slider_registry()) {
            if (d.binding.kind != BindingKind::DbField) continue;
            for (const std::string& target : d.binding.targets) {
                const size_t dot = target.find('.');
                const std::string table = target.substr(0, dot);
                CHECK(tables.contains(table), d.key + ": table " + table + " exists");
                if (!tables.contains(table) || dot == std::string::npos) continue;
                std::string field = target.substr(dot + 1);
                std::vector<std::string> fields;
                const size_t ph = field.find("{n}");
                if (ph == std::string::npos) fields.push_back(field);
                else
                    for (int n = 0; n <= 10; ++n) fields.push_back(field.substr(0, ph) + std::to_string(n) + field.substr(ph + 3));
                for (const std::string& f : fields) {
                    const json* hit = nullptr;
                    for (const json& fj : tables[table]["fields"])
                        if (fj.value("name", "") == f) hit = &fj;
                    CHECK(hit != nullptr, d.key + ": " + table + "." + f + " exists");
                    if (!hit) continue;
                    ++checked;
                    const long lo = (*hit)["min"].get<long>(), hi = (*hit)["max"].get<long>();
                    if (hi > lo && !d.range_from_meta) CHECK(d.min >= lo && d.max <= hi, fmt("%s: range %d..%d inside the schema's %ld..%ld", d.key.c_str(), d.min, d.max, lo, hi));
                    if (hi > lo && d.status == SliderStatus::DB && d.key.rfind("pos.role", 0) == 0) CHECK(d.max == hi, d.key + ": role range is the schema's");
                }
            }
        }
        CHECK(checked >= 25, fmt("checked %zu fields", checked));
    });
}

// ---------------------------------------------------------------------------------------------------------------- profiles
static Profile sample_profile(const char* name, ProfileCategory c) {
    Profile p;
    p.category = c;
    p.name = name;
    p.created = "2026-10-06 14:05";
    p.game_build = "27.1.2";
    p.tags = {"a", "b"};
    p.note = "a note with \"quotes\" and \xC3\xA9";
    p.formation = c == ProfileCategory::Team ? "4-3-3" : "";
    p.sliders.set("team.press_intensity", 80);
    p.sliders.set("team.line_depth", 66);
    p.sliders.set("match.weather", 4, false);  // a value that is not enabled
    return p;
}

static void test_profiles() {
    run_case("profiles: JSON round trip keeps every field, flag and unknown key", [] {
        TacticProfileStore s;
        s.build = "2.0.0";
        Profile a = sample_profile("Gegenpress 4-3-3", ProfileCategory::Team);
        a.sliders.unknown_values["team.future_slider"] = "42";
        a.sliders.unknown_values["team.future_obj"] = "{\"x\":[1,2]}";
        a.sliders.unknown_enabled.insert("team.future_slider");
        a.extra["future_field"] = "{\"k\":true}";
        a.rules_json = "{\"turbo_opp_rules\":1,\"rules\":[]}";
        const std::string ida = s.add(a);
        const std::string idb = s.add(sample_profile("Weekend", ProfileCategory::Match));
        CHECK(!ida.empty() && !idb.empty() && ida != idb, "ids assigned");
        s.set_active(ProfileCategory::Team, ida);
        s.active["future_cat"] = "zzz";
        s.extra["top_future"] = "[1,2,3]";
        const std::string j1 = tactic_profiles_json(s);
        TacticProfileStore t;
        std::string err;
        ProfileLoadReport rep;
        CHECK(parse_tactic_profiles_json(j1, t, &err, &rep) && err.empty(), "parses: " + err);
        CHECK(t.profiles.size() == 2 && rep.unknown_keys == 2 && rep.dropped_profiles == 0 && rep.bad_values == 0 && rep.clamped == 0,
              fmt("counts: unknown %zu dropped %zu", rep.unknown_keys, rep.dropped_profiles));
        const Profile* pa = t.find(ida);
        CHECK(pa && pa->name == "Gegenpress 4-3-3" && pa->category == ProfileCategory::Team && pa->formation == "4-3-3" && pa->tags == a.tags &&
                  pa->note == a.note && pa->created == a.created && pa->game_build == "27.1.2" && pa->schema_version == 1,
              "fields");
        CHECK(pa && pa->sliders.values.at("team.press_intensity") == 80 && pa->sliders.is_enabled("team.press_intensity") &&
                  pa->sliders.has("match.weather") && !pa->sliders.is_enabled("match.weather"),
              "values and the enabled flag are separate");
        CHECK(pa && pa->sliders.unknown_values.count("team.future_slider") && pa->sliders.unknown_enabled.count("team.future_slider") && pa->extra.count("future_field"),
              "unknown keys and fields are kept");
        CHECK(t.active.at("team") == ida && t.active.at("future_cat") == "zzz" && t.extra.count("top_future") && t.build == "2.0.0", "active map and top-level extras kept");
        CHECK(tactic_profiles_json(t) == j1, "a second round trip is byte-identical");
        CHECK(pa && pa->rules_json.find("turbo_opp_rules") != std::string::npos, "bundle rules survive");
        // disk round trip
        const fs::path dir = scratch("roundtrip");
        const fs::path p = tactic_profiles_path(dir);
        CHECK(p == dir / "turbo_output" / "tactic_profiles.json", "turbo_output\\tactic_profiles.json");
        TacticProfileStore e;
        CHECK(load_tactic_profiles(p, e, &err) && e.empty() && !e.read_only, "a missing file is an empty store");
        CHECK(save_tactic_profiles(p, s, &err), "save: " + err);
        CHECK(!fs::exists(fs::path(p.string() + ".tmp")), "no .tmp left behind");
        TacticProfileStore back;
        CHECK(load_tactic_profiles(p, back, &err) && tactic_profiles_json(back) == j1, "disk round trip is identical");
    });

    run_case("profiles: malformed entries are dropped or fixed and counted, good ones survive", [] {
        const std::string text = R"({"turbo_tactic_profiles":1,"game":"fc27","profiles":[
          42, "text", {"name":"no id","category":"team"}, {"id":"x1","category":"team"}, {"id":"x2","name":"bad cat","category":"nope"},
          {"id":"ok1","name":"Good","category":"team","values":{"team.tempo":70,"team.press_intensity":"high","team.line_depth":5000,"team.mystery":9,"match.weather":2.5},
           "enabled":["team.tempo","team.mystery","team.line_depth","team.width_att",7]},
          {"id":"ok1","name":"Duplicate id","category":"team"},
          {"id":"builtin.team.gegenpress","name":"fake built-in","category":"team"},
          {"id":"ok2","name":"Second","category":"bundle","tags":[1,"t"],"values":[1,2],"enabled":"no"}]})";
        TacticProfileStore s;
        ProfileLoadReport rep;
        std::string err;
        CHECK(parse_tactic_profiles_json(text, s, &err, &rep), "parses: " + err);
        CHECK(s.profiles.size() == 2, fmt("two profiles survive, got %zu", s.profiles.size()));
        const Profile* g = s.find("ok1");
        CHECK(g && g->sliders.values.at("team.tempo") == 70 && g->sliders.is_enabled("team.tempo"), "valid value kept");
        CHECK(g && !g->sliders.has("team.press_intensity") && !g->sliders.has("match.weather"), "non-integer values dropped");
        CHECK(g && g->sliders.values.at("team.line_depth") == 100, "out-of-range value clamped");
        CHECK(g && g->sliders.unknown_values.count("team.mystery") && g->sliders.unknown_enabled.count("team.mystery"), "unknown key kept with its flag");
        CHECK(g && g->sliders.is_enabled("team.width_att") && g->sliders.values.at("team.width_att") == 50, "enabled with no value gets the default");
        CHECK(rep.bad_values == 2 && rep.unknown_keys == 1 && rep.clamped == 1, fmt("bad %zu unknown %zu clamped %zu", rep.bad_values, rep.unknown_keys, rep.clamped));
        const Profile* o2 = s.find("ok2");
        CHECK(o2 && o2->tags == std::vector<std::string>({"t"}) && o2->sliders.values.empty(), "wrong-shaped tags / values / enabled are ignored");
        // dropped: 42, "text", no id, no name, bad category, duplicate id, builtin id = 7
        CHECK(rep.dropped_profiles == 7, fmt("dropped %zu", rep.dropped_profiles));
        TacticProfileStore x;
        CHECK(!parse_tactic_profiles_json("{not json", x, &err) && err == "not valid JSON", "not JSON");
        CHECK(!parse_tactic_profiles_json("[1]", x, &err), "not an object");
        CHECK(!parse_tactic_profiles_json("{\"turbo_reapply\":1}", x, &err) && err.find("tactic profile") != std::string::npos, "another Turbo file is not a profile file");
        CHECK(!parse_tactic_profiles_json("{\"turbo_tactic_profiles\":0}", x, &err), "version 0 is not valid");
        CHECK(parse_tactic_profiles_json("{\"turbo_tactic_profiles\":1}", x, &err) && x.empty(), "no profiles array is an empty store");
    });

    run_case("profiles: a corrupt file is set aside, a newer file is read-only, saves are atomic", [] {
        const fs::path dir = scratch("corrupt");
        const fs::path p = tactic_profiles_path(dir);
        fs::create_directories(p.parent_path());
        {
            std::ofstream f(p, std::ios::binary);
            f << "{\"turbo_tactic_profiles\": 1, \"profiles\": [ broken";
        }
        TacticProfileStore s;
        std::string err;
        CHECK(!load_tactic_profiles(p, s, &err) && err.find("tactic_profiles.json") != std::string::npos, "corrupt: " + err);
        CHECK(s.empty(), "a failed load leaves an empty store");
        s.add(sample_profile("Fresh", ProfileCategory::Team));
        CHECK(save_tactic_profiles(p, s, &err, true), "save with set-aside: " + err);
        const fs::path keep = tactic_profiles_unreadable_path(p);
        CHECK(keep == p.parent_path() / "tactic_profiles.unreadable.json" && fs::exists(keep), "the corrupt file is kept as *.unreadable.json");
        CHECK(read_file(keep).find("broken") != std::string::npos, "its content is untouched");
        TacticProfileStore back;
        CHECK(load_tactic_profiles(p, back, &err) && back.profiles.size() == 1, "the new file loads");
        // a stale .tmp from a crashed save never matters
        {
            std::ofstream f(fs::path(p.string() + ".tmp"), std::ios::binary);
            f << "garbage";
        }
        CHECK(load_tactic_profiles(p, back, &err) && back.profiles.size() == 1, "a stale .tmp is ignored");
        back.add(sample_profile("Second", ProfileCategory::Match));
        CHECK(save_tactic_profiles(p, back, &err) && !fs::exists(fs::path(p.string() + ".tmp")), "the next save replaces the stale .tmp");
        TacticProfileStore again;
        CHECK(load_tactic_profiles(p, again, &err) && again.profiles.size() == 2, "overwrite in place works");
        // a newer file
        const std::string newer = R"({"turbo_tactic_profiles":9,"future":{"a":1},"profiles":[
          {"id":"n1","name":"Newer","category":"team","schema_version":9,"values":{"team.tempo":55,"team.new_thing":3},"enabled":["team.tempo"],"extra_new":1}]})";
        const fs::path pn = dir / "newer.json";
        {
            std::ofstream f(pn, std::ios::binary);
            f << newer;
        }
        TacticProfileStore n;
        CHECK(load_tactic_profiles(pn, n, &err) && n.read_only && n.file_version == 9 && n.profiles.size() == 1, "a newer file loads");
        CHECK(n.profiles[0].locked && n.profiles[0].sliders.values.at("team.tempo") == 55 && n.profiles[0].extra.count("extra_new"), "its profile is locked and complete");
        CHECK(n.add(sample_profile("X", ProfileCategory::Team)).empty() && !n.remove("n1") && !n.rename("n1", "Y") &&
                  !n.overwrite("n1", SliderSet{}, "") && n.duplicate("n1", "C", "").empty() && !n.set_active(ProfileCategory::Team, "n1"),
              "every mutator refuses on a read-only store");
        CHECK(!save_tactic_profiles(pn, n, &err) && err.find("newer") != std::string::npos, "save refuses: " + err);
        CHECK(read_file(pn) == newer, "the newer file is untouched");
        // a locked profile inside a writable store
        TacticProfileStore w;
        w.profiles.push_back(n.profiles[0]);
        CHECK(!w.remove("n1") && !w.rename("n1", "Z") && !w.overwrite("n1", SliderSet{}, ""), "a locked profile is immutable");
    });

    run_case("profiles: built-ins are immutable code, never saved, all valid", [] {
        const auto& b = builtin_profiles();
        CHECK(b.size() >= 15, fmt("%zu built-ins", b.size()));
        std::set<std::string> ids;
        std::set<std::pair<int, std::string>> names;
        for (const Profile& p : b) {
            CHECK(p.builtin && p.id.rfind("builtin.", 0) == 0 && !p.name.empty(), p.id + ": built-in id");
            CHECK(ids.insert(p.id).second && names.insert({static_cast<int>(p.category), p.name}).second, p.id + ": unique id and name");
            for (const auto& kv : p.sliders.values) {
                const SliderDef* d = find_slider(kv.first);
                CHECK(d && slider_value_ok(*d, kv.second), p.id + ": " + kv.first + " is a known key with a legal value");
            }
            CHECK(p.sliders.unknown_values.empty(), p.id + ": no unknown keys");
        }
        TacticProfileStore s;
        const Profile* auth = s.find("builtin.match.authentic");
        CHECK(auth && auth->sliders.enabled_count() == 0 && game_writes(auth->sliders).empty(), "Authentic baseline: everything off, nothing written");
        for (const char* nm : {"Tough referee", "Physical", "Fewer injuries", "High scoring"}) CHECK(s.find_by_name(ProfileCategory::Match, nm) != nullptr, nm);
        const Profile* fi = s.find_by_name(ProfileCategory::Match, "fewer injuries");  // case-insensitive
        CHECK(fi && !game_writes(fi->sliders).empty(), "Fewer injuries writes Live values");
        CHECK(!s.remove("builtin.match.physical") && !s.rename("builtin.match.physical", "Mine") && !s.overwrite("builtin.match.physical", SliderSet{}, ""),
              "built-ins cannot be changed");
        CHECK(s.profiles.empty(), "...and none was added");
        const std::string cid = s.duplicate("builtin.match.physical", "", "2026-10-06 15:00");
        const Profile* c = s.find(cid);
        CHECK(c && !c->builtin && c->name == "Physical copy" && c->sliders.values == s.find("builtin.match.physical")->sliders.values && c->created == "2026-10-06 15:00",
              "a clone of a built-in is an editable user profile");
        CHECK(s.overwrite(cid, SliderSet{}, "") && s.find(cid)->sliders.empty(), "the clone can be overwritten");
        CHECK(s.set_active(ProfileCategory::Match, "builtin.match.authentic") && s.active_profile(ProfileCategory::Match) == auth, "a built-in can be the active profile");
        CHECK(!s.set_active(ProfileCategory::Team, "builtin.match.authentic"), "an id of another category cannot be active");
        const std::string j = tactic_profiles_json(s);
        CHECK(j.find("builtin.match.physical") == std::string::npos && j.find("\"builtin.match.authentic\"") != std::string::npos, "only the user's profiles are saved (the active id is kept)");
        auto list = s.list(ProfileCategory::Match);
        CHECK(list.size() >= 6 && list.front()->builtin && !list.back()->builtin, "list: built-ins first, then the user's");
    });

    run_case("profiles: add / duplicate / rename / remove, unique names and ids, determinism of ids", [] {
        TacticProfileStore s;
        const std::string a = s.add(sample_profile("Mine", ProfileCategory::Team));
        const std::string b = s.add(sample_profile("mine", ProfileCategory::Team));
        const std::string c = s.add(sample_profile("Mine", ProfileCategory::Match));
        CHECK(s.find(a)->name == "Mine" && s.find(b)->name == "mine (2)" && s.find(c)->name == "Mine", "names are unique inside a category");
        Profile clash = sample_profile("Other", ProfileCategory::Team);
        clash.id = a;
        const std::string d = s.add(clash);
        CHECK(d != a && s.profiles.size() == 4, "a clashing id gets a new one");
        Profile fake = sample_profile("Fake", ProfileCategory::Team);
        fake.id = "builtin.team.balanced";
        CHECK(s.add(fake) != "builtin.team.balanced", "a user profile never takes a built-in id");
        CHECK(!s.rename(a, "mine (2)") && s.rename(a, "Mine renamed") && s.find(a)->name == "Mine renamed" && !s.rename(a, ""), "rename: taken or empty names are refused");
        CHECK(s.rename(a, "Mine renamed"), "renaming to its own name is fine");
        const std::string dup = s.duplicate(a, "", "t");
        CHECK(s.find(dup)->name == "Mine renamed copy" && s.find(dup)->sliders.values == s.find(a)->sliders.values, "duplicate");
        s.set_active(ProfileCategory::Team, dup);
        CHECK(s.remove(dup) && s.find(dup) == nullptr && s.active.count("team") == 0, "remove also clears the active entry");
        CHECK(!s.remove("nope") && !s.overwrite("nope", SliderSet{}, ""), "unknown ids");
        SliderSet ns;
        ns.set("team.tempo", 33);
        CHECK(s.overwrite(a, ns, "5-3-2") && s.find(a)->sliders.values.at("team.tempo") == 33 && s.find(a)->formation == "5-3-2", "overwrite");
        TacticProfileStore e1, e2;
        CHECK(e1.make_id("seed") == e2.make_id("seed") && e1.make_id("seed") != e1.make_id("other") && e1.make_id("x").size() == 17 && e1.make_id("x")[0] == 'p',
              "make_id: deterministic, distinct, no clock");
        std::set<std::string> ids;
        for (const Profile& p : s.profiles) ids.insert(p.id);
        CHECK(ids.size() == s.profiles.size(), "ids are unique");
    });

    run_case("profiles: export and import with clash handling at the data level", [] {
        TacticProfileStore src;
        src.add(sample_profile("Alpha", ProfileCategory::Team));
        src.add(sample_profile("Beta", ProfileCategory::Team));
        src.add(sample_profile("Gamma", ProfileCategory::Match));
        std::vector<const Profile*> team = src.list(ProfileCategory::Team), mine;
        for (const Profile* p : team)
            if (!p->builtin) mine.push_back(p);
        const std::string text = export_profiles_json(mine, "2.0.0");
        CHECK(text.find("\"active\"") == std::string::npos && mine.size() == 2, "an export has the chosen profiles and no active map");
        // into an empty store: no clashes
        TacticProfileStore dst;
        ImportPlan plan;
        std::string err;
        CHECK(plan_import(dst, text, plan, &err) && plan.items.size() == 2 && plan.items[0].clash == ClashKind::None, "plan: no clash");
        ImportResult r = apply_import(dst, plan, {});
        CHECK(r.added == 2 && r.skipped == 0 && dst.profiles.size() == 2 && dst.find(mine[0]->id) != nullptr, "imported with their ids");
        // again: same ids clash
        CHECK(plan_import(dst, text, plan, &err) && plan.items[0].clash == ClashKind::SameId && plan.items[0].existing_id == mine[0]->id, "plan: same id clash");
        CHECK(plan.items[0].diff.empty(), "identical content: empty diff");
        // change the existing one so the diff shows
        SliderSet other;
        other.set("team.press_intensity", 20);
        dst.overwrite(mine[0]->id, other, "4-4-2");
        CHECK(plan_import(dst, text, plan, &err), "replan");
        CHECK(!plan.items[0].diff.empty(), "the plan previews the differences");
        r = apply_import(dst, plan, {ClashAction::Skip, ClashAction::Skip});
        CHECK(r.skipped == 2 && dst.profiles.size() == 2 && dst.find(mine[0]->id)->sliders.values.at("team.press_intensity") == 20, "skip changes nothing");
        r = apply_import(dst, plan, {ClashAction::Rename, ClashAction::Rename});
        CHECK(r.renamed == 2 && dst.profiles.size() == 4 && dst.find_by_name(ProfileCategory::Team, "Alpha (2)") != nullptr, "rename adds copies with a new id and name");
        r = apply_import(dst, plan, {ClashAction::Replace});
        CHECK(r.replaced == 1 && r.renamed == 1 && dst.find(mine[0]->id)->sliders.values.at("team.press_intensity") == 80 && dst.find(mine[0]->id)->formation == "4-3-3",
              "replace overwrites the clashing profile (the missing action renames)");
        // same name, different id
        TacticProfileStore d2;
        Profile same = sample_profile("Alpha", ProfileCategory::Team);
        same.created = "another day";  // a different id (ids derive from name, time and count)
        const std::string eid = d2.add(same);
        CHECK(plan_import(d2, text, plan, &err) && plan.items[0].clash == ClashKind::SameName && plan.items[0].existing_id == eid, "plan: same name clash");
        r = apply_import(d2, plan, {ClashAction::Replace, ClashAction::Replace});
        CHECK(d2.find(eid)->sliders.values.at("team.line_depth") == 66 && d2.find_by_name(ProfileCategory::Team, "Alpha")->id == eid && d2.profiles.size() == 2,
              "replace by name keeps the existing id");
        // an export that contains built-ins: they clash by id and can only be renamed
        std::vector<const Profile*> withb = {&builtin_profiles().front()};
        TacticProfileStore d3;
        CHECK(plan_import(d3, export_profiles_json(withb, "2.0.0"), plan, &err), "a built-in export is a valid file");
        CHECK(plan.report.dropped_profiles == 1 && plan.items.empty(), "built-in ids are dropped from an import file");
        // a read-only target imports nothing
        TacticProfileStore ro;
        ro.read_only = true;
        CHECK(plan_import(ro, text, plan, &err), "plan against a read-only store");
        r = apply_import(ro, plan, {});
        CHECK(r.skipped == 2 && r.added == 0 && ro.profiles.empty(), "read-only: nothing imported");
        CHECK(!plan_import(dst, "nonsense", plan, &err) && !err.empty(), "not a profile file");
        // a whole bundle with rules
        Profile bundle = sample_profile("Season", ProfileCategory::Bundle);
        bundle.rules_json = "{\"turbo_opp_rules\":1,\"rules\":[]}";
        TacticProfileStore s3;
        const std::string bid = s3.add(bundle);
        TacticProfileStore d4;
        CHECK(plan_import(d4, export_profiles_json({s3.find(bid)}, "x"), plan, &err) && apply_import(d4, plan, {}).added == 1 &&
                  !d4.find(bid)->rules_json.empty(),
              "a bundle round trips with its rules");
    });

    run_case("profiles: a profile with a value but no enabled flag writes nothing", [] {
        Profile p = sample_profile("Quiet", ProfileCategory::Match);
        p.sliders.clear();
        p.sliders.set("match.injury_frequency_user", 10, false);
        p.sliders.set("match.never_injure", 1, false);
        CHECK(game_writes(p.sliders).empty(), "disabled sliders are never written");
        p.sliders.set_enabled("match.never_injure", true);
        const auto w = game_writes(p.sliders);
        CHECK(w.size() == 1 && w[0].target == "NEVER_INJURE" && w[0].value == 1, "only the enabled one is");
    });
}

// ---------------------------------------------------------------------------------------------------------------- tactics
static void test_tactics() {
    run_case("tactics: the fallback formation table", [] {
        const auto& all = fallback_formations();
        CHECK(all.size() == 12, fmt("%zu formations", all.size()));
        std::set<std::string> ids;
        for (const Formation& f : all) {
            CHECK(ids.insert(f.id).second && f.slots.size() == 11 && !f.from_db, f.id + ": 11 slots, unique");
            CHECK(f.slots[0].label == "GK" && f.slots[0].group == PosGroup::GK, f.id + ": slot 0 is the goalkeeper");
            int def = 0, gk = 0;
            std::set<std::pair<int, int>> pos;
            for (const FormationSlot& s : f.slots) {
                CHECK(s.x >= 0 && s.x <= 1 && s.y >= 0 && s.y <= 1, f.id + ": coordinates inside the pitch");
                def += s.group == PosGroup::Def;
                gk += s.group == PosGroup::GK;
                pos.insert({static_cast<int>(s.x * 1000), static_cast<int>(s.y * 1000)});
            }
            CHECK(gk == 1 && def == f.id[0] - '0', f.id + fmt(": %d defenders, one goalkeeper", def));
            CHECK(pos.size() == 11, f.id + ": no two players on one spot");
            // the formation is left-right symmetric where it has pairs
            double sx = 0;
            for (const FormationSlot& s : f.slots) sx += s.x;
            CHECK(std::fabs(sx / 11.0 - 0.5) < 0.02, f.id + ": centred");
        }
        CHECK(find_fallback_formation("4-2-3-1") != nullptr && find_fallback_formation("9-9-9") == nullptr, "lookup");
        Formation out;
        std::string err;
        std::vector<FormationSlot> s = find_fallback_formation("4-4-2")->slots;
        s[3].x = 7.0f;
        s[4].y = -2.0f;
        CHECK(make_formation("77", "From db", s, out, &err) && out.from_db && out.slots[3].x == 1.0f && out.slots[4].y == 0.0f && out.name == "From db", "make_formation clamps");
        std::vector<FormationSlot> ten = s;
        ten.pop_back();
        CHECK(!make_formation("78", "", ten, out, &err) && !err.empty() && out.id == "77", "10 slots refused, `out` untouched");
        std::vector<FormationSlot> nogk = s;
        nogk[0].label = "CB";
        CHECK(!make_formation("79", "", nogk, out, &err), "slot 0 must be the goalkeeper");
        std::vector<FormationSlot> twogk = s;
        twogk[5].label = "GK";
        CHECK(!make_formation("80", "", twogk, out, &err), "a second goalkeeper is refused");
        CHECK(group_of_label("LWB") == PosGroup::Def && group_of_label("CAM") == PosGroup::Mid && group_of_label("ST") == PosGroup::Att, "group_of_label");
    });

    run_case("tactics: preview geometry is monotone in its sliders and clamped", [] {
        auto def_y = [](int v) { return line_of(preview_with("4-3-3", {{"team.line_depth", v}}), "def_line")->y; };
        auto press_y = [](int v) { return line_of(preview_with("4-3-3", {{"team.engagement_height", v}}), "press_line")->y; };
        auto front_y = [](int v) { return line_of(preview_with("4-3-3", {{"team.line_length", v}}), "front_line")->y; };
        auto band_w = [](const char* key, Phase ph, int v) {
            const PreviewModel m = preview_with("4-3-3", {{key, v}}, ph);
            return m.bands.at(0).x_hi - m.bands.at(0).x_lo;
        };
        auto runs = [](int v) { return arrow_total(preview_with("4-3-3", {{"team.forward_runs", v}}, Phase::WithBall)); };
        auto expo = [](const char* key, int v) {
            SliderSet t;
            t.set(key, v);
            PreviewOptions o;
            o.show_risk = true;
            return build_preview(*find_fallback_formation("4-3-3"), t, SliderSet{}, o).exposure;
        };
        double pd = -1, pp = -1, pw = 9, pa = -1, pb = -1, pr = -1, pe1 = -1, pe2 = -1, pe3 = -1, pf = -1;
        for (int v = 1; v <= 100; v += 3) {
            const double d = def_y(v), p = press_y(v), f = front_y(v), wa = band_w("team.width_att", Phase::WithBall, v),
                         wd = band_w("team.width_def", Phase::WithoutBall, v), r = runs(v), e1 = expo("team.line_depth", v),
                         e2 = expo("team.press_intensity", v), e3 = expo("team.tempo", v);
            CHECK(d >= pd - 1e-9 && p >= pp - 1e-9 && f <= pw + 1e-9 && wa >= pa - 1e-9 && wd >= pb - 1e-9 && r >= pr - 1e-9 && e1 >= pe1 - 1e-9 &&
                      e2 >= pe2 - 1e-9 && e3 >= pe3 - 1e-9,
                  fmt("monotone at %d", v));
            pd = d; pp = p; pw = f; pa = wa; pb = wd; pr = r; pe1 = e1; pe2 = e2; pe3 = e3; pf = f;
        }
        (void)pf;
        CHECK(def_y(100) > def_y(1) + 0.2 && press_y(100) > press_y(1) + 0.2 && band_w("team.width_att", Phase::WithBall, 100) > band_w("team.width_att", Phase::WithBall, 1) + 0.2 &&
                  runs(100) > runs(1),
              "the sliders really move the picture");
        // clamping: every value stays inside the pitch, whatever the sliders hold
        for (const auto& fm : fallback_formations()) {
            for (int v : {1, 50, 100}) {
                SliderSet t;
                for (const SliderDef& d : slider_registry())
                    if (d.scope == SliderScope::Team) t.set(d.key, v * 1000);  // far out of range: set() clamps
                PreviewOptions o;
                o.show_heat = o.show_roam = o.show_risk = true;
                o.selected_slot = 4;
                for (Phase ph : {Phase::WithBall, Phase::WithoutBall, Phase::Overall}) {
                    o.phase = ph;
                    const PreviewModel m = build_preview(fm, t, SliderSet{}, o);
                    bool ok = true;
                    for (const auto& l : m.lines) ok &= l.y >= 0 && l.y <= 1 && l.weight >= 0 && l.weight <= 1;
                    for (const auto& b : m.bands) ok &= b.x_lo >= 0 && b.x_hi <= 1 && b.x_lo < b.x_hi;
                    for (const auto& a : m.arrows) ok &= a.y1 >= a.y0 && a.y1 <= 1 && a.x1 >= 0 && a.x1 <= 1;
                    for (const auto& r : m.rings) ok &= r.radius > 0 && r.radius < 0.5;
                    for (float h : m.heat.cell) ok &= h >= 0 && h <= 1;
                    ok &= m.exposure >= 0 && m.exposure <= 1;
                    CHECK(ok, fm.id + ": everything inside bounds at extreme values");
                    CHECK(m.primitive_count() <= kPrimitiveCap, fm.id + fmt(": %zu primitives", m.primitive_count()));
                }
            }
        }
        // the press line never falls behind the defensive line
        for (int dpt : {1, 50, 100})
            for (int eng : {1, 50, 100}) {
                const PreviewModel m = preview_with("4-4-2", {{"team.line_depth", dpt}, {"team.engagement_height", eng}});
                CHECK(line_of(m, "press_line")->y >= line_of(m, "def_line")->y, "press line is not behind the defensive line");
            }
    });

    run_case("tactics: formation isolation - dots ignore sliders, lines ignore the formation, phases pick the layers", [] {
        const PreviewModel a = preview_with("4-3-3", {{"team.line_depth", 10}, {"team.forward_runs", 5}, {"team.width_att", 90}});
        const PreviewModel b = preview_with("4-3-3", {{"team.line_depth", 95}, {"team.forward_runs", 99}, {"team.width_att", 5}});
        bool same = a.dots.size() == b.dots.size();
        for (size_t i = 0; same && i < a.dots.size(); ++i) same &= a.dots[i].x == b.dots[i].x && a.dots[i].y == b.dots[i].y && a.dots[i].label == b.dots[i].label;
        CHECK(same && a.dots.size() == 11, "formation dots are exact data: no slider moves them");
        const PreviewModel c = preview_with("3-5-2", {{"team.line_depth", 10}, {"team.forward_runs", 5}, {"team.width_att", 90}});
        CHECK(line_of(a, "def_line")->y == line_of(c, "def_line")->y && line_of(a, "press_line")->y == line_of(c, "press_line")->y &&
                  a.bands[0].x_lo == c.bands[0].x_lo && a.bands[0].x_hi == c.bands[0].x_hi,
              "lines and band do not depend on the formation");
        CHECK(a.dots[3].label != c.dots[3].label || a.dots[3].x != c.dots[3].x, "...while the dots do");
        for (const auto& d : a.dots) CHECK(d.slot >= 0 && d.slot < 11, "dot slot index");
        // phases
        const PreviewModel with = preview_with("4-3-3", {}, Phase::WithBall), without = preview_with("4-3-3", {}, Phase::WithoutBall), overall = preview_with("4-3-3", {});
        CHECK(!with.arrows.empty() && with.lines.empty() && without.arrows.empty() && !without.lines.empty() && !overall.arrows.empty() && !overall.lines.empty(),
              "arrows with the ball, lines without it, both overall");
        CHECK(with.bands.size() == 1 && without.bands.size() == 1 && overall.bands.size() == 1, "one width band in every phase");
        // a selected dot's position sliders change only that dot's arrow
        SliderSet team, pos;
        team.set("team.forward_runs", 30);
        pos.set("pos.forward_runs", 95);
        PreviewOptions o;
        o.phase = Phase::WithBall;
        o.selected_slot = 9;
        const PreviewModel sel = build_preview(*find_fallback_formation("4-3-3"), team, pos, o);
        o.selected_slot = -1;
        const PreviewModel none = build_preview(*find_fallback_formation("4-3-3"), team, pos, o);
        double ds = 0, dn = 0;
        for (const auto& ar : sel.arrows) if (ar.slot == 9) ds = ar.y1 - ar.y0;
        for (const auto& ar : none.arrows) if (ar.slot == 9) dn = ar.y1 - ar.y0;
        CHECK(ds > dn + 0.05 && sel.dots[9].selected && !none.dots[9].selected, "the selected player's own slider lengthens his run");
        // defaults: the registry default is used for an unset slider
        const PreviewModel def = preview_with("4-3-3", {});
        const PreviewModel fifty = preview_with("4-3-3", {{"team.line_depth", 50}});
        CHECK(line_of(def, "def_line")->y == line_of(fifty, "def_line")->y, "an unset slider uses its default");
        CHECK(preview_with("4-3-3", {}).greyed == false, "not greyed by default");
        PreviewOptions g;
        g.effects_greyed = true;
        CHECK(build_preview(*find_fallback_formation("4-3-3"), SliderSet{}, SliderSet{}, g).greyed, "a kill switch greys the effect layers");
    });

    run_case("tactics: deterministic heat grid and the preview cache", [] {
        const PreviewModel a = preview_with("4-2-3-1", {{"team.line_depth", 70}, {"team.fluidity", 60}}, Phase::Overall, true);
        const PreviewModel b = preview_with("4-2-3-1", {{"team.line_depth", 70}, {"team.fluidity", 60}}, Phase::Overall, true);
        CHECK(a.heat.cols == kHeatCols && a.heat.rows == kHeatRows && a.heat.cell.size() == static_cast<size_t>(kHeatCols * kHeatRows), "grid size");
        CHECK(a.heat.cell == b.heat.cell, "the same input gives a bit-identical grid");
        float mx = 0;
        for (float v : a.heat.cell) mx = std::max(mx, v);
        CHECK(mx == 1.0f, "normalised: the busiest cell is 1");
        CHECK(preview_with("4-2-3-1", {}, Phase::Overall, false).heat.cell.empty(), "no grid unless asked for (Modelled layers are off by default)");
        const PreviewModel hi = preview_with("4-2-3-1", {{"team.line_depth", 100}, {"team.engagement_height", 100}}, Phase::WithoutBall, true);
        const PreviewModel lo = preview_with("4-2-3-1", {{"team.line_depth", 1}, {"team.engagement_height", 1}}, Phase::WithoutBall, true);
        auto centre_of_mass = [](const HeatGrid& h) {
            double s = 0, w = 0;
            for (int r = 0; r < h.rows; ++r)
                for (int c = 0; c < h.cols; ++c) {
                    s += (r + 0.5) / h.rows * h.at(c, r);
                    w += h.at(c, r);
                }
            return s / w;
        };
        CHECK(centre_of_mass(hi.heat) > centre_of_mass(lo.heat), "a higher block moves the heat up the pitch");
        // cache
        PreviewCache cache;
        SliderSet t, p;
        t.set("team.tempo", 40);
        PreviewOptions o;
        o.show_heat = true;
        const Formation& f = *find_fallback_formation("4-3-3");
        const PreviewModel& m1 = cache.get(f, t, p, o, 7);
        const std::vector<float> first = m1.heat.cell;
        cache.get(f, t, p, o, 7);
        cache.get(f, t, p, o, 7);
        CHECK(cache.builds() == 1, "repeated frames reuse the model");
        t.set("team.tempo", 41);
        cache.get(f, t, p, o, 7);
        CHECK(cache.builds() == 2, "a slider change rebuilds");
        cache.get(f, t, p, o, 8);
        CHECK(cache.builds() == 3, "a new generation rebuilds");
        o.phase = Phase::WithBall;
        cache.get(f, t, p, o, 8);
        CHECK(cache.builds() == 4, "an option change rebuilds");
        cache.get(*find_fallback_formation("5-3-2"), t, p, o, 8);
        CHECK(cache.builds() == 5, "a formation change rebuilds");
        o.phase = Phase::Overall;
        t.set("team.tempo", 40);
        CHECK(cache.get(f, t, p, o, 7).heat.cell == first && cache.builds() == 6, "back to the first input: the same grid");
        CHECK(preview_key(f, t, p, o, 7) == preview_key(f, t, p, o, 7) && preview_key(f, t, p, o, 7) != preview_key(f, t, p, o, 9), "preview_key");
    });

    run_case("tactics: honest labels - no units on D / M items, a model chip, the preview-only strip", [] {
        const std::regex unit("[0-9]\\s*(m|km|yd|yds|ft|cm|mph|kph|%)(\\b|$)|metre|meter|yard|kilomet|km/h|\\bfeet\\b", std::regex::icase);
        const std::regex d_ok("^[A-Za-z ]+ [0-9]+/100 \\(schematic\\)$");
        size_t seen = 0;
        for (Phase ph : {Phase::WithBall, Phase::WithoutBall, Phase::Overall}) {
            SliderSet t, p;
            t.set("team.line_depth", 62);
            PreviewOptions o;
            o.phase = ph;
            o.show_heat = o.show_roam = o.show_risk = true;
            o.selected_slot = 6;
            const PreviewModel m = build_preview(*find_fallback_formation("4-3-3"), t, p, o);
            for (const PreviewLine& l : m.lines) {
                CHECK(l.tier == Tier::Derived && std::regex_match(l.label, d_ok), "Derived line label: " + l.label);
                ++seen;
            }
            for (const PreviewBand& b : m.bands) CHECK(b.tier == Tier::Derived && std::regex_match(b.label, d_ok), "Derived band label: " + b.label);
            for (const PreviewArrow& a : m.arrows) CHECK(a.tier == Tier::Derived, "arrows are Derived");
            if (ph != Phase::WithoutBall) CHECK(std::regex_match(m.arrows_label, d_ok), "arrow label: " + m.arrows_label);
            for (const std::string& l : m.derived_modelled_labels()) {
                CHECK(!std::regex_search(l, unit), "no unit string: " + l);
                ++seen;
            }
            CHECK(m.heat_label == "Relative intensity (arbitrary)" && m.ring_label == "Roaming range (arbitrary)" && m.exposure_label == "Relative exposure (arbitrary)",
                  "Modelled labels are fixed text");
            for (const std::string& l : {m.heat_label, m.ring_label, m.exposure_label})
                CHECK(l.find_first_of("0123456789") == std::string::npos && l.find("(arbitrary)") != std::string::npos, "Modelled label has no number: " + l);
            CHECK(m.model_chip == "Model view: Turbo's estimate, not game output" && m.model_chip == std::string(kModelChip), "the model chip is always set");
        }
        CHECK(seen > 20, "labels were checked");
        CHECK(std::string(tier_letter(Tier::Exact)) == "E" && std::string(tier_letter(Tier::Derived)) == "D" && std::string(tier_letter(Tier::Modelled)) == "M" &&
                  std::string(tier_name(Tier::Modelled)) == "Modelled",
              "tier names");
        const PreviewModel m = preview_with("4-3-3", {});
        CHECK(m.visible_total == sliders_in_scope(SliderScope::Team).size() && m.visible_preview_only == count_preview_only(sliders_in_scope(SliderScope::Team)), "strip counts the team sliders");
        CHECK(m.preview_strip == std::to_string(m.visible_preview_only) + " of " + std::to_string(m.visible_total) + " visible settings are preview-only", "strip text: " + m.preview_strip);
        SliderSet t, p;
        PreviewOptions o;
        o.selected_slot = 2;
        const PreviewModel s = build_preview(*find_fallback_formation("4-3-3"), t, p, o);
        CHECK(s.visible_total > m.visible_total, "a selected dot adds the position sliders to the strip");
        // the registry's tiers: every Preview / RE slider is flagged preview_only; Live and DB never
        for (const SliderDef& d : slider_registry()) CHECK(preview_only(d) == (d.status == SliderStatus::Preview || d.status == SliderStatus::RE), d.key + ": preview_only agrees with the status");
        CHECK(std::string(status_name(SliderStatus::RE)) == "RE" && std::string(status_badge_help(SliderStatus::Live)).find("game") != std::string::npos, "badge text");
    });

    run_case("tactics: duty presets seed the position sliders monotonically", [] {
        const SliderSet d = duty_preset(Duty::Defend), s = duty_preset(Duty::Support), a = duty_preset(Duty::Attack);
        for (const char* k : {"pos.attack_bias", "pos.forward_runs", "pos.depth_bias", "pos.shot_freq"})
            CHECK(d.value_or_default(k) < s.value_or_default(k) && s.value_or_default(k) < a.value_or_default(k), std::string(k) + ": Defend < Support < Attack");
        for (const char* k : {"pos.close_down", "pos.mark_tight"}) CHECK(d.value_or_default(k) > a.value_or_default(k), std::string(k) + ": defenders close down more");
        CHECK(a.enabled_count() == 6 && std::string(duty_name(Duty::Attack)) == "Attack", "all enabled");
    });
}

// ---------------------------------------------------------------------------------------------------------------- opposition
static Fixture sample_fixture() {
    Fixture f;
    f.user.ovr = 78;
    f.opp.ovr = 74;
    f.opp.pace = 70;
    f.opp.passing = 66;
    f.opp.age = 25;
    f.opp.n_wide = 2;
    f.opp.mentality = 4;
    f.opp.reputation = 65;
    return f;
}

static RuleSet rules_of(const std::string& json) {
    RuleSet r;
    std::string err;
    size_t dropped = 0;
    if (!parse_opp_rules_json(json, r, &err, &dropped)) throw std::runtime_error("rules: " + err);
    return r;
}

static int sum_abs(const OppProfile& p) {
    int s = 0;
    for (const auto& kv : p.team_deltas) s += std::abs(kv.second);
    return s;
}

static void test_opposition() {
    run_case("opposition: seeded generator and fixture seeds - deterministic, no global state", [] {
        SeededRng a(12345), b(12345), c(12346), z(0);
        std::vector<uint64_t> va, vb;
        for (int i = 0; i < 50; ++i) {
            va.push_back(a.next());
            vb.push_back(b.next());
        }
        CHECK(va == vb, "same seed, same stream");
        CHECK(c.next() != SeededRng(12345).next(), "another seed, another stream");
        bool in = true, nz = true;
        for (int i = 0; i < 2000; ++i) {
            const double u = z.unit(), s = z.signed_unit();
            in &= u >= 0.0 && u < 1.0 && s >= -1.0 && s < 1.0;
            nz &= z.next() != 0;
        }
        CHECK(in && nz, "seed 0 still runs; values stay in range");
        double mean = 0;
        SeededRng m(99);
        for (int i = 0; i < 20000; ++i) mean += m.signed_unit();
        CHECK(std::fabs(mean / 20000.0) < 0.05, "signed_unit is centred");
        FixtureId id{7, 2027, 12, 1001, 0};
        const uint64_t s0 = fixture_seed(id);
        CHECK(s0 != 0 && s0 == fixture_seed(id), "fixture seed is stable and non-zero");
        FixtureId v = id;
        v.career = 8;  CHECK(fixture_seed(v) != s0, "career");
        v = id; v.season = 2028; CHECK(fixture_seed(v) != s0, "season");
        v = id; v.matchday = 13; CHECK(fixture_seed(v) != s0, "matchday");
        v = id; v.opp_teamid = 1002; CHECK(fixture_seed(v) != s0, "opponent");
        v = id; v.salt = 1; CHECK(fixture_seed(v) != s0, "salt (re-roll)");
    });

    run_case("opposition: determinism by seed, re-roll changes the wobble, no hidden state", [] {
        const Fixture f = sample_fixture();
        OppParams p;
        p.jitter = 100;
        const RuleSet rs = builtin_rules();
        const OppProfile a = solve_opposition(f, FixtureId{1, 2027, 5, 1001, 0}, p, rs);
        const OppProfile b = solve_opposition(f, FixtureId{1, 2027, 5, 1001, 0}, p, rs);
        CHECK(a.active && a.team_deltas == b.team_deltas && a.live_offsets == b.live_offsets && explain_text(a) == explain_text(b) && a.seed == b.seed,
              "the same fixture gives the same profile");
        // an unrelated solve in between changes nothing (no global generator)
        solve_opposition(sample_fixture(), FixtureId{9, 9, 9, 9, 9}, p, rs);
        CHECK(solve_opposition(f, FixtureId{1, 2027, 5, 1001, 0}, p, rs).team_deltas == a.team_deltas, "no hidden state");
        const OppProfile c = solve_opposition(f, FixtureId{1, 2027, 5, 1001, 1}, p, rs);
        CHECK(c.seed != a.seed && c.team_deltas != a.team_deltas, "a re-roll (new salt) gives a different wobble");
        const OppProfile d = solve_opposition(f, FixtureId{1, 2027, 6, 1001, 0}, p, rs);
        CHECK(d.team_deltas != a.team_deltas, "another matchday gives another wobble");
        OppParams q = p;
        q.jitter = 0;
        CHECK(solve_opposition(f, FixtureId{1, 2027, 5, 1001, 0}, q, rs).team_deltas == solve_opposition(f, FixtureId{1, 2027, 6, 1001, 7}, q, rs).team_deltas,
              "without jitter the seed does not matter");
    });

    run_case("opposition: styles are blended, quality sets amplitude, never style", [] {
        // an aggressive young fast side that is as good as you: Gegenpress leads, but it is a blend
        Fixture f = sample_fixture();
        f.opp.ovr = 79;
        f.opp.pace = 82;
        f.opp.age = 23;
        f.opp.mentality = 5;
        f.opp.passing = 72;
        OppParams p;
        p.jitter = 0;
        RuleSet none;
        OppProfile g = solve_opposition(f, FixtureId{}, p, none);
        double sum = 0;
        int nonzero = 0;
        for (double w : g.blend) {
            sum += w;
            nonzero += w > 0.01;
        }
        CHECK(std::fabs(sum - 1.0) < 1e-9 && nonzero >= 3, fmt("a blend of %d families, sums to 1", nonzero));
        CHECK(g.dominant == 0 && g.team_deltas.at("team.press_intensity") > 0 && g.team_deltas.at("team.engagement_height") > 0, "Gegenpress presses higher");
        // a weak slow defensive side: Low block
        Fixture w = sample_fixture();
        w.opp.ovr = 62;
        w.opp.pace = 58;
        w.opp.age = 31;
        w.opp.mentality = 1;
        w.opp.passing = 52;
        OppProfile lb = solve_opposition(w, FixtureId{}, p, none);
        CHECK(lb.team_deltas.count("team.line_depth") && lb.team_deltas.at("team.line_depth") < 0 && lb.team_deltas.at("team.counter_attack") > 0, "Low block drops deep and counters");
        // family weights and a forced family
        OppParams z = p;
        z.family_weight[0] = 0;
        CHECK(solve_opposition(f, FixtureId{}, z, none).blend[0] == 0.0, "weight 0 removes a family");
        z = p;
        z.force_family = 3;
        OppProfile fw = solve_opposition(f, FixtureId{}, z, none);
        CHECK(fw.blend[3] == 1.0 && fw.dominant == 3 && fw.team_deltas.at("team.width_att") > 0 && fw.team_deltas.at("team.cross_freq") > 0, "a pinned family takes the whole blend");
        // mirroring pulls toward the user's own style
        Fixture m = f;
        m.user.pace = 85; m.user.age = 22; m.user.ovr = 79; m.user.mentality = 5;
        OppParams mp = p;
        const double base_g = solve_opposition(w, FixtureId{}, mp, none).blend[0];
        Fixture wm = w;
        wm.user = m.user;
        mp.mirroring = 100;
        CHECK(solve_opposition(wm, FixtureId{}, mp, none).blend[0] > base_g, "mirroring moves the blend toward the user's style");
        // underdog and favourite bias
        Fixture u = sample_fixture();
        u.opp.ovr = 60;
        OppParams b0 = p, b1 = p;
        b0.underdog_bias = 0;
        b1.underdog_bias = 100;
        CHECK(solve_opposition(u, FixtureId{}, b1, none).blend[1] > solve_opposition(u, FixtureId{}, b0, none).blend[1], "underdog bias favours the low block");
        // quality sets the amplitude: monotone in the squad band, flat when quality scaling is off
        double prev = -1;
        for (double ovr = 50; ovr <= 90; ovr += 2) {
            Fixture q = sample_fixture();
            q.opp.ovr = ovr;
            OppParams qp = p;
            qp.quality_scaling = 100;
            const OppProfile r = solve_opposition(q, FixtureId{}, qp, none);
            CHECK(r.amplitude >= prev - 1e-9, fmt("amplitude is non-decreasing in quality at %.0f", ovr));
            prev = r.amplitude;
            qp.quality_scaling = 0;
            CHECK(std::fabs(solve_opposition(q, FixtureId{}, qp, none).amplitude - 15.0) < 1e-9, "quality scaling 0: every squad the same amplitude");
        }
        Fixture lo = sample_fixture(), hi = sample_fixture();
        lo.opp.ovr = 55;
        hi.opp.ovr = 88;
        OppParams qp = p;
        qp.quality_scaling = 100;
        CHECK(solve_opposition(hi, FixtureId{}, qp, none).amplitude > solve_opposition(lo, FixtureId{}, qp, none).amplitude * 1.5, "an elite squad changes more than a weak one");
        // the variety strength scales the amplitude; 0 and a zero global scale switch it off
        OppParams vs = p;
        vs.variety_strength = 100;
        const OppProfile big = solve_opposition(f, FixtureId{}, vs, none);
        vs.variety_strength = 25;
        CHECK(sum_abs(big) > sum_abs(solve_opposition(f, FixtureId{}, vs, none)), "variety strength scales the changes");
        vs.variety_strength = 0;
        OppProfile off = solve_opposition(f, FixtureId{}, vs, none);
        CHECK(!off.active && off.team_deltas.empty() && off.live_offsets.empty() && explain_text(off).find("off") != std::string::npos, "variety 0 = off");
        OppParams gs = p;
        gs.global_scale = 0;
        CHECK(!solve_opposition(f, FixtureId{}, gs, builtin_rules()).active, "global scale 0 = off, even with rules");
        gs = p;
        gs.enabled = false;
        CHECK(!solve_opposition(f, FixtureId{}, gs, builtin_rules()).active, "the none mode");
    });

    run_case("opposition: max_delta clamps every change, rules cannot break the bound", [] {
        Fixture f = sample_fixture();
        f.opp.ovr = 95;
        f.opp.pace = 90;
        f.opp.age = 21;
        f.opp.mentality = 6;
        OppParams p;
        p.variety_strength = 100;
        p.jitter = 100;
        p.quality_scaling = 100;
        const RuleSet big = rules_of(R"({"turbo_opp_rules":1,"rules":[
          {"id":"add_huge","priority":5,"when":[],"then":[{"op":"add","target":"team.tempo","value":90},{"op":"add","target":"team.line_depth","value":-90}]},
          {"id":"set_huge","priority":9,"when":[],"then":[{"op":"set","target":"team.press_intensity","value":99}]},
          {"id":"amp","priority":1,"when":[],"then":[{"op":"scale_amplitude","value":5}]}]})");
        for (int md : {15, 5, 1, 0}) {
            p.max_delta = md;
            for (int salt = 0; salt < 20; ++salt) {
                const OppProfile r = solve_opposition(f, FixtureId{1, 1, 1, 1, salt}, p, big);
                for (const auto& kv : r.team_deltas) CHECK(std::abs(kv.second) <= md && kv.second != 0, fmt("%s = %d within +/-%d", kv.first.c_str(), kv.second, md));
                if (md > 0) CHECK(r.team_deltas.at("team.tempo") == md && r.team_deltas.at("team.line_depth") == -md && r.team_deltas.at("team.press_intensity") == md,
                                  "huge rule changes are clamped to the bound");
            }
        }
        p.max_delta = 15;
        // Live offsets are clamped to their slider range
        const RuleSet live = rules_of(R"({"turbo_opp_rules":1,"rules":[
          {"id":"l","priority":1,"when":[],"then":[{"op":"add","target":"opp.injury_frequency_offset","value":80},{"op":"add","target":"opp.difficulty_offset","value":-9},{"op":"add","target":"opp.injury_severity_offset","value":0}]}]})");
        const OppProfile r = solve_opposition(f, FixtureId{}, p, live);
        CHECK(r.live_offsets.at("opp.injury_frequency_offset") == 50 && r.live_offsets.at("opp.difficulty_offset") == -5 && !r.live_offsets.count("opp.injury_severity_offset"),
              "Live offsets clamp to their ranges, zero offsets are left out");
    });

    run_case("opposition: rule priority, pinning, stop, disabled, tie order", [] {
        const Fixture f = sample_fixture();
        OppParams p;
        p.jitter = 0;
        p.variety_strength = 100;
        p.force_family = 5;  // balanced: the style contributes nothing, so the rules' own numbers are what is left
        auto run = [&](const char* rules) { return solve_opposition(f, FixtureId{}, p, rules_of(rules)); };
        // set: the higher priority wins
        OppProfile r = run(R"({"turbo_opp_rules":1,"rules":[
          {"id":"low","priority":10,"when":[],"then":[{"op":"set","target":"team.tempo","value":9}]},
          {"id":"high","priority":50,"when":[],"then":[{"op":"set","target":"team.tempo","value":5}]}]})");
        CHECK(r.team_deltas.at("team.tempo") == 5, "set: the higher priority wins");
        CHECK(r.hits.size() == 2 && r.hits[0].id == "high" && r.hits[1].id == "low", "hits are in priority order");
        CHECK(r.hits[1].effects.at(0).find("ignored") != std::string::npos, "the overridden effect says it was ignored");
        // add after a pin is ignored; set after an add is ignored; adds accumulate
        r = run(R"({"turbo_opp_rules":1,"rules":[
          {"id":"pin","priority":30,"when":[],"then":[{"op":"set","target":"team.cross_freq","value":4}]},
          {"id":"late_add","priority":20,"when":[],"then":[{"op":"add","target":"team.cross_freq","value":6},{"op":"add","target":"team.dribble_freq","value":3}]},
          {"id":"also_add","priority":10,"when":[],"then":[{"op":"add","target":"team.dribble_freq","value":4}]},
          {"id":"first_add","priority":40,"when":[],"then":[{"op":"add","target":"team.shot_patience","value":2}]},
          {"id":"late_set","priority":5,"when":[],"then":[{"op":"set","target":"team.shot_patience","value":-9}]}]})");
        CHECK(r.team_deltas.at("team.cross_freq") == 4 && r.team_deltas.at("team.dribble_freq") == 7 && r.team_deltas.at("team.shot_patience") == 2,
              "a pin blocks lower adds; a lower set cannot undo a higher add; adds accumulate");
        // ties: by id; stop: ends the evaluation; disabled: skipped
        r = run(R"({"turbo_opp_rules":1,"rules":[
          {"id":"b","priority":10,"when":[],"then":[{"op":"set","target":"team.tempo","value":3}]},
          {"id":"a","priority":10,"when":[],"then":[{"op":"set","target":"team.tempo","value":8}]}]})");
        CHECK(r.team_deltas.at("team.tempo") == 8 && r.hits[0].id == "a", "equal priority: the smaller id goes first");
        r = run(R"({"turbo_opp_rules":1,"rules":[
          {"id":"s","priority":90,"stop":true,"when":[],"then":[{"op":"add","target":"team.tempo","value":2}]},
          {"id":"never","priority":10,"when":[],"then":[{"op":"add","target":"team.tempo","value":5}]}]})");
        CHECK(r.team_deltas.at("team.tempo") == 2 && r.hits.size() == 1, "stop ends the evaluation");
        r = run(R"({"turbo_opp_rules":1,"rules":[{"id":"off","priority":90,"enabled":false,"when":[],"then":[{"op":"add","target":"team.tempo","value":5}]}]})");
        CHECK(r.hits.empty() && !r.team_deltas.count("team.tempo"), "a disabled rule never fires");
        // conditions: all, any, every operator, family facts
        FactSet facts;
        facts["a"] = 1;
        const OppRule rule = rules_of(R"({"turbo_opp_rules":1,"rules":[{"id":"c","when":{"all":[{"fact":"strength_ratio","op":">=","value":1}],"any":[{"fact":"aerial","op":"==","value":1},{"fact":"home","op":"==","value":1}]},
          "then":[{"op":"add","target":"team.tempo","value":1}]}]})").rules.at(0);
        facts = {{"strength_ratio", 1.0}, {"aerial", 0}, {"home", 1}};
        CHECK(rule_matches(rule, facts), "all and any hold");
        facts["home"] = 0;
        CHECK(!rule_matches(rule, facts), "any fails");
        facts = {{"strength_ratio", 0.9}, {"aerial", 1}, {"home", 1}};
        CHECK(!rule_matches(rule, facts), "all fails");
        CHECK(!rule_matches(rule, {}), "a missing fact is false");
        CHECK(eval_condition({"x", "<", 2}, {{"x", 1}}) && !eval_condition({"x", "<", 1}, {{"x", 1}}) && eval_condition({"x", "<=", 1}, {{"x", 1}}) &&
                  eval_condition({"x", ">", 0}, {{"x", 1}}) && eval_condition({"x", "==", 1}, {{"x", 1}}) && eval_condition({"x", "!=", 2}, {{"x", 1}}) &&
                  !eval_condition({"x", "!=", 1}, {{"x", 1}}) && !eval_condition({"x", "??", 1}, {{"x", 1}}),
              "operators");
        // a built-in rule fires on its fixture and shows in the explanation
        Fixture u = sample_fixture();
        u.opp.ovr = 60;  // ratio 0.77: underdog
        const OppProfile e = solve_opposition(u, FixtureId{}, OppParams{}, builtin_rules());
        bool underdog = false;
        for (const RuleHit& h : e.hits) underdog |= h.id == "underdog_low_block";
        const std::string text = explain_text(e);
        CHECK(underdog && text.find("underdog_low_block") != std::string::npos && text.find("priority 60") != std::string::npos, "explain lists the rules that fired");
        CHECK(text.find("Seed") != std::string::npos && text.find("Style blend:") != std::string::npos && text.find("Strength ratio 0.77") != std::string::npos &&
                  text.find("Defensive line depth") != std::string::npos,
              "explain shows the seed, ratio, blend and the changes");
        const OppProfile ne = solve_opposition(sample_fixture(), FixtureId{}, OppParams{}, RuleSet{});
        CHECK(explain_text(ne).find("No rule fired.") != std::string::npos, "explain: no rule");
    });

    run_case("opposition: rules JSON - built-ins as data, override by id, malformed rules, files", [] {
        const RuleSet& b = builtin_rules();
        CHECK(b.rules.size() >= 5, fmt("%zu built-in rules", b.rules.size()));
        std::set<std::string> ids;
        for (const OppRule& r : b.rules) {
            CHECK(r.builtin && !r.then.empty() && ids.insert(r.id).second, r.id + ": built-in, has actions, unique");
            for (const RuleCond& c : r.all) CHECK(std::find(known_facts().begin(), known_facts().end(), c.fact) != known_facts().end(), r.id + ": known fact " + c.fact);
        }
        // override by id: a user copy with enabled=false switches the built-in off
        RuleSet user = rules_of(R"({"turbo_opp_rules":1,"rules":[
          {"id":"underdog_low_block","priority":60,"enabled":false,"when":[{"fact":"strength_ratio","op":"<","value":0.85}],"then":[{"op":"add","target":"team.tempo","value":1}]},
          {"id":"mine","priority":1,"when":[],"then":[{"op":"add","target":"team.tempo","value":1}]}]})");
        const RuleSet merged = merge_rules(b, user);
        CHECK(merged.rules.size() == b.rules.size() + 1, "an overriding id replaces, a new id is added");
        size_t n_under = 0;
        for (const OppRule& r : merged.rules)
            if (r.id == "underdog_low_block") {
                ++n_under;
                CHECK(!r.enabled && !r.builtin, "the user's copy wins");
            }
        CHECK(n_under == 1, "no duplicate id");
        Fixture u = sample_fixture();
        u.opp.ovr = 60;
        bool fired = false;
        for (const RuleHit& h : solve_opposition(u, FixtureId{}, OppParams{}, merged).hits) fired |= h.id == "underdog_low_block";
        CHECK(!fired, "the disabled override does not fire");
        // round trip
        const std::string j = opp_rules_json(user);
        RuleSet back;
        std::string err;
        size_t dropped = 9;
        CHECK(parse_opp_rules_json(j, back, &err, &dropped) && dropped == 0 && back.rules.size() == 2 && opp_rules_json(back) == j, "user rules round trip");
        CHECK(back.rules[0].all.size() == 1 && back.rules[0].all[0].op == "<" && back.rules[0].all[0].value == 0.85 && !back.rules[0].enabled && back.rules[0].priority == 60,
              "fields kept");
        // malformed rules
        const std::string bad = R"({"turbo_opp_rules":1,"rules":[
          7, {"priority":1,"then":[]}, {"id":"","then":[]},
          {"id":"no_then","when":[]},
          {"id":"bad_op","when":[{"fact":"home","op":"~","value":1}],"then":[{"op":"add","target":"team.tempo","value":1}]},
          {"id":"bad_fact","when":[{"fact":"mood","op":"<","value":1}],"then":[{"op":"add","target":"team.tempo","value":1}]},
          {"id":"bad_target","when":[],"then":[{"op":"add","target":"match.weather","value":1}]},
          {"id":"re_target","when":[],"then":[{"op":"add","target":"opp.cpu_error_offset","value":1}]},
          {"id":"local_target","when":[],"then":[{"op":"add","target":"opp.jitter","value":1}]},
          {"id":"bad_family","when":[],"then":[{"op":"boost_family","target":"tiki","value":1}]},
          {"id":"bad_scale","when":[],"then":[{"op":"scale_amplitude","value":-2}]},
          {"id":"bad_priority","priority":"high","when":[],"then":[{"op":"add","target":"team.tempo","value":1}]},
          {"id":"half","when":[],"then":[{"op":"add","target":"team.nope","value":1},{"op":"add","target":"team.tempo","value":1},{"op":"jump","value":1}]},
          {"id":"dup","when":[],"then":[{"op":"add","target":"team.tempo","value":1}]},
          {"id":"dup","when":[],"then":[{"op":"add","target":"team.tempo","value":2}]},
          {"id":"ok_live","when":[],"then":[{"op":"add","target":"opp.injury_frequency_offset","value":5}]}]})";
        CHECK(parse_opp_rules_json(bad, back, &err, &dropped), "parses: " + err);
        std::set<std::string> kept;
        for (const OppRule& r : back.rules) kept.insert(r.id);
        CHECK(kept == std::set<std::string>({"half", "dup", "ok_live"}), "only the valid rules survive");
        CHECK(dropped == 13, fmt("dropped %zu rules", dropped));
        for (const OppRule& r : back.rules)
            if (r.id == "half") CHECK(r.then.size() == 1 && r.then[0].target == "team.tempo", "a rule keeps its valid actions");
        CHECK(!parse_opp_rules_json("nope", back, &err) && !parse_opp_rules_json("{\"turbo_tactic_profiles\":1}", back, &err), "not a rules file");
        // files: set-aside, newer version, atomic
        const fs::path dir = scratch("rules");
        const fs::path p = opp_rules_path(dir);
        CHECK(p == dir / "turbo_output" / "opp_rules.json", "turbo_output\\opp_rules.json");
        RuleSet e;
        CHECK(load_opp_rules(p, e, &err) && e.rules.empty(), "a missing file is an empty set");
        CHECK(save_opp_rules(p, user, &err) && !fs::exists(fs::path(p.string() + ".tmp")), "save: " + err);
        CHECK(load_opp_rules(p, e, &err) && opp_rules_json(e) == j, "disk round trip");
        {
            std::ofstream f(p, std::ios::binary);
            f << "{ corrupt";
        }
        CHECK(!load_opp_rules(p, e, &err), "corrupt file");
        CHECK(save_opp_rules(p, user, &err, true) && fs::exists(opp_rules_unreadable_path(p)) && read_file(opp_rules_unreadable_path(p)) == "{ corrupt", "set aside as *.unreadable.json");
        const std::string newer = R"({"turbo_opp_rules":4,"rules":[{"id":"n","when":[],"then":[{"op":"add","target":"team.tempo","value":1}]}]})";
        {
            std::ofstream f(p, std::ios::binary);
            f << newer;
        }
        CHECK(load_opp_rules(p, e, &err) && e.read_only && e.rules.size() == 1, "a newer file loads read-only");
        CHECK(!save_opp_rules(p, e, &err) && read_file(p) == newer, "and is never overwritten");
    });

    run_case("opposition: parameters come from the sliders; the engine source uses no rand() or clock", [] {
        const OppParams d;
        const OppParams s = params_from_sliders(SliderSet{});
        CHECK(s.variety_strength == d.variety_strength && s.quality_scaling == d.quality_scaling && s.mirroring == d.mirroring && s.underdog_bias == d.underdog_bias &&
                  s.favourite_bias == d.favourite_bias && s.jitter == d.jitter,
              "unset sliders give the OppParams defaults");
        for (size_t i = 0; i < kFamilyCount; ++i) CHECK(s.family_weight[i] == d.family_weight[i], "family weight default");
        SliderSet t;
        t.set("opp.variety_strength", 77);
        t.set("opp.jitter", 5);
        t.set("opp.family_weight_possession", 90);
        const OppParams q = params_from_sliders(t);
        CHECK(q.variety_strength == 77 && q.jitter == 5 && q.family_weight[2] == 90 && q.family_weight[0] == 50, "slider values are read");
        CHECK(family_index("wing_play") == 3 && family_index("nope") == -1 && std::string(family_label(1)) == "Low block counter", "family helpers");
        for (size_t i = 0; i < kFamilyCount; ++i) CHECK(find_slider(std::string("opp.family_weight_") + kFamilyKeys[i]) != nullptr, "every family has a weight slider");
        // static check of the sources: a determinism guard
        const fs::path src = fs::path(g_lua).parent_path() / ".." / ".." / "src" / "core";
        const std::regex bad("\\b(rand|srand|time|clock|random_device)\\s*\\(|std::chrono|os\\.time");
        size_t scanned = 0;
        for (const char* f : {"opposition.cpp", "opp_rules.cpp", "tactics.cpp", "sliders.cpp", "tactic_profiles.cpp"}) {
            if (!fs::exists(src / f)) continue;
            ++scanned;
            CHECK(!std::regex_search(read_file(src / f), bad), std::string(f) + ": no rand(), time() or clock");
        }
        if (scanned == 0) std::printf("    (sources not found: determinism scan skipped)\n");
    });
}

static void run() {
    std::printf("native tactics core (sliders, profiles, preview, opposition)\n");
    test_sliders();
    test_profiles();
    test_tactics();
    test_opposition();
}

}  // namespace tactics_core_test
