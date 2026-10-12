// Asset catalog tests (tests/catalog/run.sh): the schema v1 contract (docs/asset-catalog.md section "Schema v1") checked
// on the synthetic fixture, plus core/asset_catalog's names, paths, switches and scope filter.
// Usage: test_asset_catalog <fixture.json> <scratch dir> [catalog.json ...]  (extra files: validated against schema v1 too)
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace turbo::assets;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// ---------------------------------------------------------------- schema v1 validator (the contract, test side)
static bool is_id_array(const json& a, bool sorted_unique = true) {
    if (!a.is_array()) return false;
    int64_t prev = INT64_MIN;
    for (const auto& v : a) {
        if (!v.is_number_integer()) return false;
        const int64_t x = v.get<int64_t>();
        if (sorted_unique && x <= prev) return false;
        prev = x;
    }
    return true;
}
static bool is_id_key(const std::string& k) {
    if (k.empty() || k.size() > 19) return false;
    return std::all_of(k.begin(), k.end(), [](char c) { return c >= '0' && c <= '9'; });
}
static bool is_game_path(const json& v) {
    if (!v.is_string()) return false;
    const std::string s = v.get<std::string>();
    if (s.rfind("data/", 0) != 0 || s.size() > 200 || s.find("..") != std::string::npos) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/' ||
               c == '.' || c == '-';
    });
}
static bool known_source(const std::string& s) {
    static const std::set<std::string> fixed = {"workbook", "extra_names", "slug", "kit_slug", "managers"};
    return fixed.count(s) || (s.rfind("master:", 0) == 0 && s.size() > 7);
}

static std::vector<std::string> validate_v1(const json& j) {
    std::vector<std::string> e;
    auto err = [&](const std::string& s) { e.push_back(s); };
    if (!j.is_object()) return {"top level is not an object"};
    if (j.value("schema", "") != kSchema) err("schema is not turbo-asset-catalog/1");
    for (const char* k : {"build_key", "game_version", "built_at", "builder"})
        if (!j.contains(k) || !j[k].is_string()) err(std::string(k) + " missing or not a string");
    if (j.contains("build_key") && j["build_key"].is_string()) {
        const std::string bk = j["build_key"];
        const size_t dash = bk.find('-');
        if (dash == std::string::npos || dash == 0 || dash + 1 >= bk.size()) err("build_key is not <stamp>-<size>");
    }
    if (!j.contains("sources") || !j["sources"].is_object()) {
        err("sources missing");
    } else {
        for (const auto& [k, v] : j["sources"].items())
            if (!v.is_object() || !v.contains("size") || !v.contains("mtime") || !v["size"].is_number_integer() ||
                !v["mtime"].is_number_integer())
                err("sources." + k + " needs integer size and mtime");
    }
    if (!j.contains("families") || !j["families"].is_object()) return e.push_back("families missing"), e;
    const json& fams = j["families"];
    for (size_t i = 0; i < kFamilyCount; ++i)
        if (!fams.contains(family_key(static_cast<Family>(i)))) err(std::string("family missing: ") + family_key(static_cast<Family>(i)));
    for (const auto& [key, v] : fams.items()) {
        Family f;
        if (!family_from_key(key, f)) {
            err("unknown family: " + key);
            continue;
        }
        if (!v.is_object()) {
            err(key + " is not an object");
            continue;
        }
        if (family_has_models(f)) {
            if (!v.contains("model") || !is_id_array(v["model"])) err(key + ".model: sorted unique integer ids");
            if (!v.contains("preview") || !v["preview"].is_object()) {
                err(key + ".preview missing");
            } else {
                for (const auto& [id, p] : v["preview"].items())
                    if (!is_id_key(id) || !is_game_path(p)) err(key + ".preview." + id + ": id key -> data/... path");
            }
        } else if (f == Family::Kit) {
            if (!v.contains("teams") || !v["teams"].is_object()) {
                err("kit.teams missing");
            } else {
                for (const auto& [id, t] : v["teams"].items())
                    if (!is_id_key(id) || !t.is_object() || !t.contains("slug") || !t["slug"].is_string() || !t.contains("kits") ||
                        !t["kits"].is_array())
                        err("kit.teams." + id + ": {slug, kits}");
            }
        } else if (f == Family::Callnames) {
            for (const auto& [lang, l] : v.items()) {
                if (!l.is_object() || !l.contains("teams") || !l["teams"].is_object()) {
                    err("callnames." + lang + ".teams missing");
                    continue;
                }
                for (const auto& [team, t] : l["teams"].items()) {
                    if (!t.is_object()) {
                        err("callnames." + lang + "." + team + " is not an object");
                        continue;
                    }
                    for (const auto& [kind, ids] : t.items()) {
                        bool known = false;
                        for (int k = 0; k < static_cast<int>(CallKind::Count); ++k)
                            known = known || kind == call_kind_key(static_cast<CallKind>(k));
                        if (!known || !is_id_array(ids)) err("callnames." + lang + "." + team + "." + kind);
                    }
                }
            }
        } else {
            if (!v.contains("ids") || !is_id_array(v["ids"])) err(key + ".ids: sorted unique integer ids");
        }
        if (f == Family::Head3d && v.contains("ids") && v["ids"].is_array()) {
            std::set<int64_t> ids;
            for (const auto& x : v["ids"]) ids.insert(x.get<int64_t>());
            for (const char* sub : {"vars", "toc"}) {
                if (!v.contains(sub) || !v[sub].is_object()) {
                    err(std::string("head3d.") + sub + " missing");
                    continue;
                }
                for (const auto& [id, x] : v[sub].items()) {
                    if (!is_id_key(id) || !ids.count(std::stoll(id))) err(std::string("head3d.") + sub + "." + id + " is not a head id");
                    if (std::string(sub) == "vars" && !is_id_array(x)) err("head3d.vars." + id + ": sorted variations");
                    if (std::string(sub) == "toc" && !x.is_string()) err("head3d.toc." + id + ": string");
                }
            }
            if (!v.contains("generic") || !is_id_array(v["generic"])) {
                err("head3d.generic missing");
            } else {
                for (const auto& x : v["generic"])
                    if (!ids.count(x.get<int64_t>())) err("head3d.generic id is not a head id");
            }
        }
        if (f == Family::Crest && v.contains("paths")) {
            if (!v["paths"].is_object()) err("crest.paths is not an object");
            else
                for (const auto& [id, p] : v["paths"].items())
                    if (!is_id_key(id) || !is_game_path(p)) err("crest.paths." + id);
        }
    }
    if (!j.contains("names") || !j["names"].is_object()) return e.push_back("names missing"), e;
    for (const auto& [table, t] : j["names"].items()) {
        if (table != "player" && table != "team" && table != "commentary") err("unknown names table: " + table);
        if (!t.is_object()) {
            err("names." + table + " is not an object");
            continue;
        }
        for (const auto& [id, n] : t.items())
            if (!is_id_key(id) || !n.is_array() || n.size() != 2 || !n[0].is_string() || !n[1].is_string() ||
                n[0].get<std::string>().empty() || !known_source(n[1].get<std::string>()))
                err("names." + table + "." + id + ": [name, known source]");
    }
    return e;
}

static json read_json(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.u8string());
    return json::parse(f);
}

// ---------------------------------------------------------------- cases
static void test_fixture(const fs::path& fixture) {
    std::printf("fixture: %s\n", fixture.u8string().c_str());
    json j;
    try {
        j = read_json(fixture);
    } catch (const std::exception& ex) {
        check(false, std::string("fixture parses: ") + ex.what());
        return;
    }
    check(true, "fixture parses");
    const auto errs = validate_v1(j);
    for (const auto& e : errs) std::printf("    schema: %s\n", e.c_str());
    check(errs.empty(), "fixture validates against schema v1");
    // Synthetic only: every name in the fixture is a "Test ..." name (real names never go in the repository)
    bool synthetic = true;
    for (const auto& [table, t] : j["names"].items())
        for (const auto& [id, n] : t.items()) synthetic = synthetic && n[0].get<std::string>().rfind("Test", 0) == 0;
    for (const auto& [id, t] : j["families"]["kit"]["teams"].items())
        synthetic = synthetic && t["slug"].get<std::string>().rfind("test_", 0) == 0;
    check(synthetic, "fixture names are synthetic (Test ... / test_...)");
    // What later packages rely on: model-only, preview-only, both; a generic head; a multi-team language
    const json& hair = j["families"]["hair"];
    check(hair["preview"].contains("5") && std::find(hair["model"].begin(), hair["model"].end(), 5) == hair["model"].end(),
          "fixture has a preview-only hair id (5)");
    check(!hair["preview"].contains("3") && std::find(hair["model"].begin(), hair["model"].end(), 3) != hair["model"].end(),
          "fixture has a model-only hair id (3)");
    check(j["families"]["head3d"]["generic"].size() == 1, "fixture has one generic head");
    check(j["families"]["callnames"]["eng_us"]["teams"].size() == 2 && j["families"]["callnames"]["ita_it"]["teams"].contains(""),
          "fixture callnames: eng_us dr + gm, ita_it single team");

    // The validator refuses broken files
    json bad = j;
    bad["schema"] = "turbo-asset-catalog/2";
    check(!validate_v1(bad).empty(), "other schema refused");
    bad = j;
    bad["families"]["hair"]["model"] = json::array({2, 1});
    check(!validate_v1(bad).empty(), "unsorted ids refused");
    bad = j;
    bad["families"]["boots"]["preview"]["10"] = "C:/game/shoe_10_0.dds";
    check(!validate_v1(bad).empty(), "preview outside data/ refused");
    bad = j;
    bad["families"].erase("ball");
    check(!validate_v1(bad).empty(), "missing family refused");
    bad = j;
    bad["names"]["player"]["90001"] = json::array({"Test", "somewhere"});
    check(!validate_v1(bad).empty(), "unknown name source refused");
}

static void test_core(const fs::path& scratch) {
    std::printf("core/asset_catalog\n");
    for (size_t i = 0; i < kFamilyCount; ++i) {
        Family f;
        const Family want = static_cast<Family>(i);
        check(family_from_key(family_key(want), f) && f == want, std::string("family key round trip: ") + family_key(want));
    }
    Family f;
    check(!family_from_key("heads", f) && std::string(family_key(Family::Count)).empty(), "unknown family key");
    check(family_has_models(Family::Boots) && !family_has_models(Family::Head3d) && !family_has_models(Family::Crest),
          "model families");
    check(std::string(call_kind_key(CallKind::Link)) == "link", "call kind keys");

    // Scope filter: All shows every row, In this save the DB-used ones, Hidden the hidden ones
    check(scope_shows(Scope::All, true) && scope_shows(Scope::All, false), "scope All");
    check(scope_shows(Scope::InSave, false) && !scope_shows(Scope::InSave, true), "scope In this save");
    check(scope_shows(Scope::Hidden, true) && !scope_shows(Scope::Hidden, false), "scope Hidden");
    check(std::string(scope_label(Scope::InSave)) == "In this save" && std::string(scope_label(Scope::Hidden)) == "Hidden",
          "scope labels");

    check(fallback_name(Family::Head3d, 41) == "Head #41" && fallback_name(Family::Callnames, 272258) == "Callname #272258" &&
              fallback_name(Family::Youthface, 7) == "Youth face #7",
          "fallback names");
    check(name_source_label("master:ita_it") == "name from: ita_it master" && name_source_label("slug") == "name from: game file name" &&
              name_source_label("").empty(),
          "name source labels");

    const fs::path out = scratch / "turbo_output";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out);
    check(catalog_file(out, "6AC07E31-2145C000") == out / "asset_catalog" / "catalog_6AC07E31-2145C000.json", "catalog file name");
    check(lua_gate_path(out) == out / "asset_catalog" / "lua_gate.txt" && write_log_path(out) == out / "asset_writes.log",
          "gate and log paths");
    Switches sw = read_switches(out);
    check(!sw.catalog_off && !sw.hidden_off, "no switch files: both on");
    std::ofstream(hidden_off_path(out)) << "x";
    sw = read_switches(out);
    check(!sw.catalog_off && sw.hidden_off, "asset_hidden_off.txt");
    std::ofstream(catalog_off_path(out)) << "x";
    check(read_switches(out).catalog_off, "asset_catalog_off.txt");

    // Stub catalog: never loaded, refuses every write, never hides anything, no gate file
    Catalog c;
    check(!c.loaded() && !c.current() && c.state() == State::NotLoaded, "new catalog: not loaded");
    std::string why;
    check(!c.writable(Family::Head3d, 90001, &why) && !why.empty(), "not loaded: writes refused with a reason");
    check(c.load_dir(out, "0000AAAA-0000BBBB") == false && c.state() == State::Off && c.status_line().find("off") == 0,
          "asset_catalog_off.txt: state off");
    DbUse use;
    check(!c.hidden(Family::Head3d, 90003, use) && c.ids(Family::Head3d).empty(), "off: nothing listed, nothing hidden");
    fs::create_directories(catalog_dir(out));
    std::ofstream(lua_gate_path(out)) << "#turbo-asset-gate 1\n";
    check(c.lua_gate_text().empty() && c.sync_lua_gate(out) && !fs::exists(lua_gate_path(out)), "no current catalog: gate removed");
    fs::remove_all(out, ec);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <fixture.json> <scratch dir> [catalog.json ...]\n", argv[0]);
        return 2;
    }
    try {
        test_fixture(argv[1]);
        test_core(argv[2]);
        for (int i = 3; i < argc; ++i) {  // optional: validate a real catalog (never committed)
            const auto errs = validate_v1(read_json(argv[i]));
            for (size_t k = 0; k < errs.size() && k < 20; ++k) std::printf("    schema: %s\n", errs[k].c_str());
            check(errs.empty(), std::string("validates: ") + argv[i]);
        }
    } catch (const std::exception& ex) {
        check(false, std::string("exception: ") + ex.what());
    }
    std::printf("RESULT %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
