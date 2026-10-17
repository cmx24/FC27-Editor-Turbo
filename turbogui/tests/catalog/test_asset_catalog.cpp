// Asset catalog tests (tests/catalog/run.sh): the schema v1 contract (docs/asset-catalog.md section "Schema v1") checked
// on the synthetic fixture, plus core/asset_catalog's names, paths, switches and scope filter.
// Usage: test_asset_catalog <fixture.json> <scratch dir> [catalog.json ...]  (extra files: validated against schema v1 too)
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <iterator>
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
    static const std::set<std::string> fixed = {"workbook", "extra_names", "slug", "kit_slug", "managers", "kind", "loc_strings",
                                                "le_team_names", "turbo_team_names"};
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
            // slug: optional (builder 2), one EBX folder slug per head id
            if (v.contains("slug")) {
                if (!v["slug"].is_object()) err("head3d.slug: not an object");
                else
                    for (const auto& [id, x] : v["slug"].items())
                        if (!is_id_key(id) || !ids.count(std::stoll(id)) || !x.is_string() || x.get<std::string>().empty())
                            err("head3d.slug." + id + ": head id -> folder slug");
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
    for (const auto& [id, sl] : j["families"]["head3d"]["slug"].items())
        synthetic = synthetic && (sl.get<std::string>().rfind("test_", 0) == 0 || sl.get<std::string>() == "generic");
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


// ---------------------------------------------------------------- WP-05: the loader, queries, gates, hidden, files
static void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static const char* kKey = "0000AAAA-0000BBBB";  // the fixture's build key

static void test_loader(const fs::path& fixture, const fs::path& scratch) {
    std::printf("core/asset_catalog loader\n");
    const json fx = read_json(fixture);
    std::string err;

    // ---- current / stale / refused
    Catalog c;
    check(c.load_file(fixture, kKey, &err) && c.state() == State::Current && c.current() && c.loaded(), "fixture loads as current: " + err);
    check(c.status().build_key == kKey && c.status().game_version == "0.0.1.1" && c.status().counts[0] == 5, "status: key, version, head count");
    check(c.status_line().rfind("current - build 0000AAAA-0000BBBB, built 2026-10-11", 0) == 0 && c.status_line().find("5 heads") != std::string::npos,
          "status line (current): " + c.status_line());
    Catalog st;
    check(st.load_file(fixture, "6AC07E31-2145C000") && st.state() == State::Stale && st.loaded() && !st.current(), "other running build: stale");
    check(st.status_line().find("built for 0000AAAA-0000BBBB, running 6AC07E31-2145C000: rebuild with") != std::string::npos,
          "status line (stale): " + st.status_line());
    Catalog unk;
    check(unk.load_file(fixture, "") && unk.state() == State::Stale, "unknown running build: stale");

    const fs::path dir = scratch / "loader";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto refused = [&](const std::string& label, const std::string& text) {
        const fs::path p = dir / "bad.json";
        write_text(p, text);
        Catalog b;
        std::string e;
        const bool ok = b.load_file(p, kKey, &e);
        check(!ok && b.state() == State::Error && !e.empty() && !b.loaded() && b.ids(Family::Head3d).empty() &&
                  b.status_line().rfind("error - ", 0) == 0,
              label + " refused (" + e + ")");
    };
    refused("not JSON", "{ \"schema\": ");
    json bad = fx;
    bad["schema"] = "turbo-asset-catalog/2";
    refused("other schema", bad.dump());
    bad = fx;
    bad.erase("schema");
    refused("no schema", bad.dump());
    bad = fx;
    bad["families"]["hair"]["model"] = json::array({1, "two"});
    refused("a text id", bad.dump());
    bad = fx;
    bad["families"]["boots"]["preview"]["x1"] = "data/ui/x.dds";
    refused("a non-numeric id key", bad.dump());
    bad = fx;
    bad.erase("families");
    refused("no families", bad.dump());
    bad = fx;
    bad.erase("build_key");
    refused("no build key", bad.dump());
    {  // a failed load after a good one leaves nothing behind
        Catalog again;
        again.load_file(fixture, kKey);
        write_text(dir / "bad2.json", "[]");
        check(!again.load_file(dir / "bad2.json", kKey) && !again.has(Family::Head3d, 90001) && !again.loaded(), "refused file clears the old data");
    }
    {  // tolerant: unsorted ids, a newer builder's extra family
        json t = fx;
        t["families"]["hair"]["model"] = json::array({4, 2, 2, 1, 3});
        t["families"]["outfit"] = json{{"ids", json::array({1})}};
        write_text(dir / "tolerant.json", t.dump());
        Catalog tc;
        check(tc.load_file(dir / "tolerant.json", kKey) && tc.ids(Family::Hair) == std::vector<int64_t>({1, 2, 3, 4, 5}) &&
                  tc.status().note.find("outfit") != std::string::npos,
              "unsorted ids sorted, unknown family ignored with a note");
    }

    // ---- ids and proofs
    check(c.ids(Family::Hair) == std::vector<int64_t>({1, 2, 3, 4, 5}), "hair ids = models + previews");
    check(c.proof(Family::Hair, 1) == (kProofModel | kProofPreview) && c.proof(Family::Hair, 3) == kProofModel &&
              c.proof(Family::Hair, 5) == kProofPreview && c.proof(Family::Hair, 6) == kProofNone,
          "hair proofs: both, model only, preview only, none");
    check(c.has(Family::Head3d, 90004) && c.has_model(Family::Head3d, 90004) && !c.has(Family::Head3d, 90005), "head3d proofs");
    check(c.proof(Family::Miniface, 90005) == kProofPreview && c.proof(Family::Callnames, 90003) == kProofRecording &&
              c.proof(Family::Kit, 9003) == kProofModel && c.proof(Family::Crest, 9002) == kProofPreview,
          "picture, recording and kit proofs");
    check(c.ids(Family::Callnames) == std::vector<int64_t>({90001, 90002, 90003, 90004, 990001, 990002}), "callnames: distinct ids over every bank");
    check(c.status().counts[static_cast<size_t>(Family::Callnames)] == 6 && c.status().counts[static_cast<size_t>(Family::Kit)] == 2,
          "counts: callnames, kits");
    check(c.preview_path(Family::Hair, 2) == "data/ui/imgAssets/craniumhair/Hair_2_0.dds", "preview path keeps the listed letter case");
    check(c.preview_path(Family::Hair, 3).empty() && c.preview_path(Family::Hair, 99).empty(), "no preview: empty path");
    check(c.preview_path(Family::Miniface, 90005) == "data/ui/imgAssets/heads/p90005.dds" &&
              c.preview_path(Family::Youthface, 91002) == "data/ui/imgAssets/youthheads/p91002.dds" &&
              c.preview_path(Family::Staffface, 92001) == "data/ui/imgAssets/heads_staff/heads_staff_92001.dds" &&
              c.preview_path(Family::Miniface, 12345).empty(),
          "picture family paths");
    check(c.preview_path(Family::Crest, 9001) == "data/ui/imgAssets/crest/light/l9001.dds", "crest path");

    // ---- extras
    check(c.head_vars(90002) == std::vector<int>({0, 1}) && c.head_vars(90004) == std::vector<int>({2}) && c.head_vars(1).empty(), "head vars");
    check(c.head_toc(90002) == "LS" && c.head_toc(90003) == "S" && c.head_toc(1).empty(), "head toc");
    check(c.generic_head(90100) && !c.generic_head(90001), "generic head");
    check(c.head_slug(90004) == "test_coach_one" && c.head_slug(90100) == "generic" && c.head_slug(1).empty(), "head slug (EBX folder)");
    check(c.kit_slug(9001) == "test_club_one" && c.kits(9001).size() == 2 && c.kit_slug(9002).empty() && c.kits(9002).empty(), "kits");
    check(c.callname_languages() == std::vector<std::string>({"eng_us", "ita_it"}), "callname languages");
    check(c.callname_teams("eng_us") == std::vector<std::string>({"dr", "gm"}) && c.callname_teams("ita_it") == std::vector<std::string>({""}) &&
              c.callname_teams("fra_fr").empty(),
          "callname teams");
    check(c.callname_ids("eng_us", "dr", CallKind::Generic) == std::vector<int64_t>({990001, 990002}) &&
              c.callname_ids("ita_it", "", CallKind::High) == std::vector<int64_t>({90002}) && c.callname_ids("eng_us", "gm", CallKind::Link).empty(),
          "callname ids per kind");
    check(c.has_recording("eng_us", "dr", 90003) && !c.has_recording("eng_us", "gm", 90003) && c.has_recording("ita_it", "", 90004) &&
              !c.has_recording("ita_it", "dr", 90004) && !c.has_recording("fra_fr", "", 90001),
          "recordings per language and team");

    // ---- names
    const Name n1 = c.name(NameKind::Player, 90001);
    check(n1.text == "Test Head 90001" && n1.source == "master:ita_it" && name_source_label(n1.source) == "name from: ita_it master", "player name + source");
    check(c.name(NameKind::Player, 90003).source == "slug" && c.name(NameKind::Team, 9003).text == "Test Club Three" &&
              c.name(NameKind::Commentary, 990002).source == "master:eng_us",
          "slug, team and commentary names");
    check(c.name(NameKind::Player, 90004).text.empty() && c.name(NameKind::Player, 90004).source.empty() && c.name(NameKind::Team, 90001).text.empty(),
          "unnamed id: empty name (fallback_name gives the label)");

    // ---- write gate
    std::string why;
    check(c.writable(Family::Head3d, 90004, &why), "current: a catalog head is writable");
    check(!c.writable(Family::Head3d, 90005, &why) && why.find("not in the game files (catalog 0000AAAA-0000BBBB)") != std::string::npos,
          "current: a head not in the catalog refused: " + why);
    check(c.writable(Family::Hair, 3, &why), "model-only hair writable");
    check(!c.writable(Family::Hair, 5, &why) && why.find("only a preview picture") != std::string::npos, "preview-only hair refused: " + why);
    check(c.writable(Family::Hair, 1) && !c.writable(Family::Hair, 6), "hair with both proofs writable, unknown refused");
    check(c.writable(Family::Boots, 0) && c.writable(Family::Hair, 0) && c.writable(Family::Tattoo, 0) && !c.writable(Family::Head3d, 0),
          "0 = none writable for gear codes only");
    check(!c.writable(Family::Boots, 13) && c.writable(Family::Boots, 12) && c.writable(Family::Tattoo, 31) && !c.writable(Family::Ball, 51),
          "boots / tattoo / ball need a model");
    check(c.writable(Family::Eyebrow, 3) && !c.writable(Family::Eyebrow, 4) && c.writable(Family::Skinsurface, 0), "cranium presets: listed");
    check(c.writable(Family::Accessory, 999) && c.writable(Family::Accessory, 40), "accessories: no gate");
    check(c.writable(Family::Youthface, 91003) && !c.writable(Family::Youthface, 91004), "pictures: listed in the manifests");
    check(!st.writable(Family::Head3d, 90004, &why) && why.find("built for game build 0000AAAA-0000BBBB") != std::string::npos &&
              why.find("6AC07E31-2145C000") != std::string::npos && why.find("build_asset_catalog.py") != std::string::npos,
          "stale: every write refused with the reason and the rebuild command: " + why);
    check(st.writable(Family::Hair, 0) && st.writable(Family::Accessory, 7), "stale: 0 = none and accessories still allowed");
    Catalog none;
    check(!none.writable(Family::Hair, 3, &why) && why.find("not loaded") != std::string::npos, "not loaded: refused");

    // ---- hidden against a synthetic save
    DbUse use;
    use.used[static_cast<size_t>(Family::Head3d)] = {90001, 90002, 12345};
    use.used[static_cast<size_t>(Family::Hair)] = {1, 2};
    use.used[static_cast<size_t>(Family::Callnames)] = {90001};
    use.commentary_ids = {990001};
    use.tattoo_table = {30};
    use.has_tattoo_table = true;
    check(!c.hidden(Family::Head3d, 90001, use) && c.hidden(Family::Head3d, 90003, use) && c.hidden(Family::Head3d, 90100, use), "hidden heads");
    check(!c.hidden(Family::Head3d, 12345, use) && !c.hidden(Family::Head3d, 77777, use), "not hidden: DB-only id, unknown id");
    check(c.hidden(Family::Hair, 5, use) && !c.hidden(Family::Hair, 2, use), "hidden hair (preview-only ids are listed too)");
    check(c.hidden(Family::Callnames, 90003, use) && !c.hidden(Family::Callnames, 90001, use), "hidden own callnames");
    check(c.hidden_generic_callname(990002, use) && !c.hidden_generic_callname(990001, use) && !c.hidden_generic_callname(90003, use),
          "hidden generic callnames (generic recordings only)");
    check(c.not_in_tattoo_table(31, use) && !c.not_in_tattoo_table(30, use) && !c.not_in_tattoo_table(99, use), "not in the tattoo table");
    DbUse no_table;
    check(!c.not_in_tattoo_table(31, no_table), "tattoo table not read: no note");
    check(c.offered(Family::Head3d, 90003, use) && c.offered(Family::Head3d, 12345, use) && !c.offered(Family::Head3d, 77777, use), "offered");
    check(!none.hidden(Family::Head3d, 90003, use) && none.offered(Family::Head3d, 90001, use) && !none.offered(Family::Head3d, 90003, use),
          "not loaded: nothing hidden, DB ids still offered");

    // ---- load_dir: switches, exact build, newest as stale
    const fs::path out = scratch / "dir" / "turbo_output";
    fs::remove_all(out.parent_path(), ec);
    Catalog d;
    check(!d.load_dir(out, kKey, &err) && d.state() == State::NotLoaded && err.find("build_asset_catalog.py") != std::string::npos,
          "no catalog: not loaded, with the rebuild command");
    const std::string sig0 = dir_signature(out);
    fs::create_directories(catalog_dir(out));
    fs::copy_file(fixture, catalog_file(out, "1111AAAA-1111BBBB"), ec);
    fs::last_write_time(catalog_file(out, "1111AAAA-1111BBBB"), fs::file_time_type::clock::now() - std::chrono::hours(2), ec);
    json other = fx;
    other["build_key"] = "2222AAAA-2222BBBB";
    write_text(catalog_file(out, "2222AAAA-2222BBBB"), other.dump());
    write_text(catalog_dir(out) / "catalog_2222AAAA-2222BBBB.summary.txt", "not a catalog");
    const std::string sig1 = dir_signature(out);
    check(sig1 != sig0 && sig1.find("catalog_2222AAAA-2222BBBB.json") != std::string::npos && sig1.find("summary") == std::string::npos,
          "dir signature sees catalog files");
    check(d.load_dir(out, "9999AAAA-9999BBBB") && d.state() == State::Stale && d.status().build_key == "2222AAAA-2222BBBB",
          "no catalog for the running build: newest file, stale");
    write_text(catalog_file(out, kKey), fx.dump());
    check(d.load_dir(out, kKey) && d.state() == State::Current && d.status().build_key == kKey, "the running build's file: current");
    write_text(hidden_off_path(out), "");
    check(dir_signature(out) != sig1, "dir signature sees a switch file");
    check(d.load_dir(out, kKey) && d.current() && d.hidden_off() && !d.hidden(Family::Head3d, 90003, use) &&
              !d.offered(Family::Head3d, 90003, use) && d.offered(Family::Head3d, 90001, use) && d.writable(Family::Head3d, 90004) &&
              d.status_line().find("hidden ids off") != std::string::npos,
          "asset_hidden_off.txt: loaded, gates on, hidden ids not offered");
    fs::remove(hidden_off_path(out), ec);
    write_text(catalog_off_path(out), "");
    check(!d.load_dir(out, kKey) && d.state() == State::Off && !d.loaded() && d.ids(Family::Head3d).empty(), "asset_catalog_off.txt: off");
    fs::remove(catalog_off_path(out), ec);
    write_text(catalog_file(out, kKey), "{ broken");
    check(!d.load_dir(out, kKey, &err) && d.state() == State::Error, "the running build's file broken: error (no silent fallback)");
    write_text(catalog_file(out, kKey), fx.dump());

    // ---- Lua gate
    check(d.load_dir(out, kKey) && d.sync_lua_gate(out, &err), "gate written: " + err);
    const std::string gate = slurp(lua_gate_path(out));
    check(gate == "#turbo-asset-gate 1\n#build 0000AAAA-0000BBBB\nhead3d 90001\nhead3d 90002\nhead3d 90003\nhead3d 90004\nhead3d 90100\n",
          "gate text: header, build, one head3d line per id");
    check(!fs::exists(catalog_dir(out) / "lua_gate.txt.tmp"), "no temporary gate file left");
    check(d.sync_lua_gate(out, &err, "a1b2c3d4") &&
              slurp(lua_gate_path(out)) == "#turbo-asset-gate 1\n#build 0000AAAA-0000BBBB\n#session a1b2c3d4\nhead3d 90001\nhead3d 90002\n"
                                           "head3d 90003\nhead3d 90004\nhead3d 90100\n",
          "gate with the GUI session (asset_gate.lua trusts only its own session)");
    check(d.lua_gate_text("two words").find("#session") == std::string::npos, "a session with a blank is not written");
    Catalog stale;
    stale.load_file(fixture, "9999AAAA-9999BBBB");
    check(stale.lua_gate_text().empty() && stale.sync_lua_gate(out) && !fs::exists(lua_gate_path(out)), "stale: gate removed");
    d.sync_lua_gate(out);
    write_text(catalog_off_path(out), "");
    d.load_dir(out, kKey);
    check(d.sync_lua_gate(out) && !fs::exists(lua_gate_path(out)), "switched off: gate removed");
    fs::remove(catalog_off_path(out), ec);

    // ---- write log
    WriteLogEntry e1{"player 12345", "headassetid", "12345", "90003", kKey};
    WriteLogEntry e2{"player\t7", "hairtypecode", "1", "3\n", kKey};
    check(append_write_log(out, e1, &err) && append_write_log(out, e2, &err), "write log appended: " + err);
    const std::string logtxt = slurp(write_log_path(out));
    size_t lines = 0, tabs = 0;
    for (char ch : logtxt) {
        lines += ch == '\n';
        tabs += ch == '\t';
    }
    check(lines == 2 && tabs == 10, "write log: 2 lines, 6 columns each");
    check(logtxt.find("\tplayer 12345\theadassetid\t12345\t90003\t0000AAAA-0000BBBB\n") != std::string::npos &&
              logtxt.find("\tplayer 7\thairtypecode\t1\t3 \t") != std::string::npos,
          "write log: columns, tabs and line breaks inside values replaced");
    check(logtxt.size() > 25 && logtxt[4] == '-' && logtxt[10] == 'T' && logtxt[22] == ':', "write log: ISO local time first: " + logtxt.substr(0, 25));

    // ---- build key from an FC27.exe header
    {
        std::string pe(1024, '\0');
        pe[0] = 'M';
        pe[1] = 'Z';
        pe[0x3C] = static_cast<char>(0x80);
        std::memcpy(&pe[0x80], "PE\0\0", 4);
        const uint32_t stamp = 0x6AC07E31, size = 0x2145C000;
        std::memcpy(&pe[0x80 + 8], &stamp, 4);
        pe[0x80 + 24] = 0x0B;
        pe[0x80 + 25] = 0x02;  // PE32+
        std::memcpy(&pe[0x80 + 24 + 56], &size, 4);
        write_text(scratch / "fake" / "FC27.exe", pe);
        check(exe_build_key(scratch / "fake" / "FC27.exe") == "6AC07E31-2145C000", "build key from the PE header");
        write_text(scratch / "fake" / "not.exe", "hello");
        check(exe_build_key(scratch / "fake" / "not.exe").empty() && exe_build_key(scratch / "fake" / "missing.exe").empty(), "not a PE: no key");
    }
    fs::remove_all(scratch / "fake", ec);
    fs::remove_all(out.parent_path(), ec);
    fs::remove_all(dir, ec);
}

// A catalog the size of the real one (about 60k ids + 10k names): parsed and indexed in <= 300 ms (optimised builds;
// an -O0 build only reports the time and must stay under 3 s)
static void test_load_time(const fs::path& fixture, const fs::path& scratch) {
    std::printf("core/asset_catalog load time\n");
    json j = read_json(fixture);
    json heads = json::array(), vars = json::object(), toc = json::object(), minis = json::array(), youth = json::array();
    json player_names = json::object();
    for (int i = 0; i < 6200; ++i) {
        heads.push_back(100000 + i);
        vars[std::to_string(100000 + i)] = json::array({0, 1});
        toc[std::to_string(100000 + i)] = "LS";
    }
    for (int i = 0; i < 21000; ++i) minis.push_back(200000 + i);
    for (int i = 0; i < 11500; ++i) youth.push_back(300000 + i);
    for (int i = 0; i < 10000; ++i) player_names[std::to_string(100000 + i)] = json::array({"Test Player " + std::to_string(i), "slug"});
    j["families"]["head3d"] = {{"ids", heads}, {"vars", vars}, {"toc", toc}, {"generic", json::array()}};
    j["families"]["miniface"]["ids"] = minis;
    j["families"]["youthface"]["ids"] = youth;
    json hair = json::array(), prev = json::object();
    for (int i = 1; i <= 1500; ++i) {
        hair.push_back(i);
        prev[std::to_string(i)] = "data/ui/imgAssets/craniumhair/hair_" + std::to_string(i) + "_0.dds";
    }
    j["families"]["hair"] = {{"model", hair}, {"preview", prev}};
    json gen = json::array();
    for (int i = 0; i < 7500; ++i) gen.push_back(400000 + i);
    j["families"]["callnames"]["eng_us"]["teams"]["dr"]["generic"] = gen;
    j["families"]["callnames"]["eng_us"]["teams"]["dr"]["simple"] = minis;
    j["names"]["player"] = player_names;
    const fs::path big = scratch / "big" / "catalog_big.json";
    write_text(big, j.dump());
    const size_t total = 6200 + 21000 + 11500 + 1500 + 7500 + 21000;
    Catalog c;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = c.load_file(big, kKey, &err);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
#ifdef __OPTIMIZE__
    const double limit = 300.0;
#else
    const double limit = 3000.0;
#endif
    std::printf("    %zu ids + 10000 names, %.1f MB: loaded in %.0f ms (limit %.0f ms)\n", total, double(fs::file_size(big)) / 1e6, ms, limit);
    check(ok && c.has(Family::Head3d, 106199) && c.has(Family::Miniface, 220999) && c.name(NameKind::Player, 109999).text == "Test Player 9999",
          "big catalog loads: " + err);
    check(ms <= limit, "big catalog load time within the limit");
    std::error_code ec;
    fs::remove_all(big.parent_path(), ec);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <fixture.json> <scratch dir> [catalog.json ...]\n", argv[0]);
        return 2;
    }
    try {
        test_fixture(argv[1]);
        test_core(argv[2]);
        test_loader(argv[1], argv[2]);
        test_load_time(argv[1], argv[2]);
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
