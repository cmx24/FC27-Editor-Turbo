// Native tests: Tactic presets (2.12; core/tactic_presets.h, ui/ui_tactic_presets.cpp): EA's own tactic (channel E, the simulated team sheet of
// test_team_sheet.h) and the Custom sliders block (the synthetic settings file of test_game_settings.h) saved to / loaded from Turbo JSON.
// Core cases: save -> JSON -> load round trip, validation (every refusal), the request channel E gets (formation refused, unwritable values
// kept), the files, the last-load record (undo, re-apply prompt). UI case: Teams > Tactic presets in the Simple view - Save current, Load asks
// first and writes through the guarded job, the staged sliders, Undo last load, the Re-apply prompt after a career load, Delete, the refusals.
// reapply_edits.json is checked byte for byte at the end. Included by test_main.cpp after test_team_sheet.h and test_game_settings.h.
#pragma once
#include <set>

#include "core/tactic_presets.h"
#include "nlohmann/json.hpp"
#include "test_game_settings.h"
#include "test_team_sheet.h"
#include "ui/ui_tactic_presets.h"

namespace tactic_presets_test {

using namespace turbo;
namespace tp = turbo::tpreset;
using team_sheet_test::SheetWorld;

static tsheet::LiveTactic fake_live() {
    tsheet::LiveTactic lt;
    lt.at.user = 1;
    lt.name = "433";
    lt.formation = 27;
    lt.source_formation = 10;
    lt.line_height = 25;
    lt.build_up = 0;
    lt.approach = 0;
    for (int s = 0; s < 11; ++s) {
        tsheet::SlotView& v = lt.slots[size_t(s)];
        v.pid = 100001 + s;
        v.position = SheetWorld::kPositions[size_t(s)];
        v.role = SheetWorld::kRoles[size_t(s)];
        v.role_id = tsheet::role_id_of(v.role);
        v.focus = tsheet::focus_of(v.role);
    }
    return lt;
}

static tp::Preset sample() {
    const tsheet::LiveTactic live = fake_live();
    const std::map<std::string, int> sl = {{"cpu.sprint_speed", 50}, {"user.sprint_speed", 70}, {"cpu.acceleration", 40}};
    return tp::make_preset("Base 4-3-3", "my note", "2026-10-16T10:00", &live, &sl, gsettings::layout_name(), "6AC07E31");
}

// A different tactic of the same formation: High, depth 90, Counter, slot 9 focus 6
static tp::TacticBlock other_block(const tsheet::LiveTactic& live) {
    tp::TacticBlock b = tp::block_from_live(live);
    b.line_height = 90;
    b.approach = 2;
    b.build_up = 1;
    b.slots[9].focus = 6;
    return b;
}

static std::string parse_err(const std::string& text) {
    tp::Preset p;
    std::string err;
    if (tp::Preset::parse(text, p, &err)) return "";
    return err.empty() ? "refused without a reason" : err;
}

static bool part(const std::string& s, const std::string& x) { return s.find(x) != std::string::npos; }
static bool has_line(const std::vector<std::string>& v, const std::string& x) {
    for (const std::string& s : v)
        if (part(s, x)) return true;
    return false;
}

static void run() {
    std::printf("native tactic presets (2.12)\n");

    run_case("tactic presets: save -> JSON -> load round trip keeps every field", [] {
        const tp::Preset a = sample();
        CHECK(tp::validate(a).empty(), tp::validate(a));
        const std::string text = a.to_json();
        tp::Preset b;
        std::string err;
        CHECK(tp::Preset::parse(text, b, &err), "parse: " + err);
        CHECK(b.name == a.name && b.note == a.note && b.created == a.created && b.game_build == tsheet::kBuild && b.schema == 1, "header");
        CHECK(b.tactic.present && b.tactic.record_name == "433" && b.tactic.formation == 27 && b.tactic.source_formation == 10 && b.tactic.line_height == 25 &&
                  b.tactic.build_up == 0 && b.tactic.approach == 0,
              "team values");
        for (int s = 0; s < 11; ++s) {
            const auto& x = a.tactic.slots[size_t(s)];
            const auto& y = b.tactic.slots[size_t(s)];
            CHECK(x.position == y.position && x.role_group == y.role_group && x.role_id == y.role_id && x.focus == y.focus, "slot " + std::to_string(s));
        }
        CHECK(b.tactic.slots[9].role_group == 9 && b.tactic.slots[9].role_id == 21 && b.tactic.slots[9].focus == 5 && b.tactic.slots[9].position == 25, "slot 9 = the ST of the evidence");
        CHECK(b.settings.present && b.settings.values == a.settings.values && b.settings.layout == gsettings::layout_name() && b.settings.map_build == "6AC07E31", "settings");
        CHECK(b.to_json() == text, "the second serialisation is the first");
        CHECK(tp::summary(b).find("433, Defensive Approach Deep (0), Line Height 25, Build-Up Style Balanced (0); 11 roles; 3 sliders") == 0, tp::summary(b));
        // a settings-only and a tactic-only preset are fine
        tp::Preset t = a;
        t.settings = tp::SettingsBlock();
        CHECK(tp::validate(t).empty() && tp::Preset::parse(t.to_json(), b, &err) && !b.settings.present, "tactic only");
        tp::Preset st = a;
        st.tactic = tp::TacticBlock();
        CHECK(tp::validate(st).empty() && tp::Preset::parse(st.to_json(), b, &err) && !b.tactic.present, "settings only");
        tp::Preset none = a;
        none.tactic = tp::TacticBlock();
        none.settings = tp::SettingsBlock();
        CHECK(part(tp::validate(none), "neither a tactic, settings nor set pieces"), "empty preset refused");
    });

    // ------------------------------------------------------------------------------------------------ 2.12.1: set pieces and the unverified-sliders flag
    run_case("tactic presets 2.12.1: set-piece block - 24 columns, validation, JSON round trip, diff, Lua read-out and command", [] {
        CHECK(tp::setpiece_fields().size() == 24, "24 columns");
        std::set<std::string> names;
        for (const auto& f : tp::setpiece_fields()) names.insert(f.name);
        CHECK(names.size() == 24 && names.count("captainid") && names.count("penaltytakerid") && names.count("cksupport9") && names.count("customsub2out") &&
                  names.count("throwerleft") && names.count("longkicktakerid"),
              "unique names, the ones of cm_teamsheets");
        tp::SetPieceBlock sp;
        sp.present = true;
        sp.team = 1;
        sp.values = {{"captainid", 100001}, {"penaltytakerid", 100009}, {"leftcornerkicktakerid", -1}, {"cksupport3", 100004}, {"customsub0in", 100012}};
        CHECK(tp::setpiece_problem(sp).empty(), tp::setpiece_problem(sp));
        // refusals
        auto refused = [&](tp::SetPieceBlock x, const std::string& want, const char* what) {
            const std::string why = tp::setpiece_problem(x);
            CHECK(part(why, want), std::string(what) + ": " + why);
        };
        { auto x = sp; x.values["notacolumn"] = 5; refused(x, "not one of the 24", "a column that is not one of the 24"); }
        { auto x = sp; x.values["captainid"] = -2; refused(x, "outside -1", "below -1"); }
        { auto x = sp; x.values["captainid"] = 500001; refused(x, "outside -1", "above the id range"); }
        { auto x = sp; x.values.clear(); refused(x, "no values", "no values"); }
        { auto x = sp; x.team = 0; refused(x, "team", "team 0"); }
        // in a preset
        tp::Preset a = sample();
        a.setpieces = sp;
        CHECK(tp::validate(a).empty(), tp::validate(a));
        tp::Preset b;
        std::string err;
        const std::string text = a.to_json();
        CHECK(tp::Preset::parse(text, b, &err), "parse: " + err);
        CHECK(b.setpieces.present && b.setpieces.team == 1 && b.setpieces.values == sp.values && b.to_json() == text, "set pieces round-trip");
        CHECK(part(tp::summary(b), "; 5 set-piece fields"), tp::summary(b));
        tp::Preset only = a;
        only.tactic = tp::TacticBlock();
        only.settings = tp::SettingsBlock();
        CHECK(tp::validate(only).empty() && tp::Preset::parse(only.to_json(), b, &err) && b.setpieces.present && !b.tactic.present && !b.settings.present, "set pieces alone is a preset");
        // a bad file is refused whole
        std::string bad = text;
        const size_t at = bad.find("\"captainid\": 100001");
        CHECK(at != std::string::npos, "the text holds the captain");
        if (at != std::string::npos) bad.replace(at, 19, "\"captainid\": 999999");
        CHECK(part(parse_err(bad), "outside -1"), "a player id above the range: " + parse_err(bad));
        // the diff
        tp::SetPieceBlock now = sp;
        now.values["penaltytakerid"] = 100002;
        now.values.erase("customsub0in");
        const auto d = tp::setpiece_diff(sp, now);
        CHECK(d.size() == 2 && has_line(d, "Penalty taker 100002 -> 100009") && has_line(d, "Pre-set substitution 1: on ? -> 100012"), "diff: " + (d.empty() ? std::string("none") : d[0]));
        CHECK(tp::setpiece_diff(sp, sp).empty(), "no diff for equal values");
        // Lua's read-out
        tp::SetPieceBlock rd;
        const std::string cur = R"({"turbo_setpieces":1,"team":1,"values":{"captainid":100001,"penaltytakerid":100002,"cksupport1":-1},"squad":25,"missing":[]})";
        CHECK(tp::setpieces_parse_current(cur, rd, &err) && rd.present && rd.team == 1 && rd.values.size() == 3 && rd.values["penaltytakerid"] == 100002, "read-out: " + err);
        CHECK(!tp::setpieces_parse_current("{\"turbo_setpieces\":1,\"team\":1}", rd, &err) && !tp::setpieces_parse_current("[1]", rd, &err), "bad read-outs refused");
        CHECK(!tp::setpieces_parse_current(R"({"turbo_setpieces":1,"team":1,"values":{"bogus":4}})", rd, &err) && part(err, "not one of the 24"), "a foreign column refused: " + err);
        // the command Lua gets
        const nlohmann::json c = nlohmann::json::parse(tp::setpieces_command_json("set", sp, "Base"));
        CHECK(c["op"] == "run" && c["module"] == "tactic_setpieces" && c["overrides"]["mode"] == "set" && c["overrides"]["confirm"] == true &&
                  c["overrides"]["team"] == 1 && c["overrides"]["preset"] == "Base" && c["overrides"]["values"].size() == 5 && c["overrides"]["values"]["captainid"] == 100001 &&
                  c["overrides"]["values"]["leftcornerkicktakerid"] == -1,
              c.dump());
        const nlohmann::json u = nlohmann::json::parse(tp::setpieces_command_json("undo", tp::SetPieceBlock(), ""));
        CHECK(u["overrides"]["mode"] == "undo" && !u["overrides"].contains("values"), u.dump());
    });

    run_case("tactic presets 2.12.1: the unverified-sliders flag is saved (default on in this build), old files mean off; the last load remembers the set pieces", [] {
        const tp::Preset a = sample();
        CHECK(a.settings.include_unverified, "make_preset: on by default");
        tp::Preset b;
        std::string err;
        CHECK(tp::Preset::parse(a.to_json(), b, &err) && b.settings.include_unverified, "round trip keeps it: " + err);
        const tsheet::LiveTactic live = fake_live();
        const std::map<std::string, int> sl = {{"cpu.sprint_speed", 50}};
        const tp::Preset off = tp::make_preset("X", "", "2026-10-16T10:00", &live, &sl, gsettings::layout_name(), "6AC07E31", nullptr, false);
        CHECK(!off.settings.include_unverified && off.to_json().find("include_unverified") == std::string::npos, "flag off: no key");
        CHECK(tp::Preset::parse(off.to_json(), b, &err) && !b.settings.include_unverified, "no key = off");
        tp::LastLoad l;
        l.preset = "P";
        l.state = "loaded";
        l.setpieces_sent = true;
        tp::LastLoad l2;
        CHECK(tp::LastLoad::parse(l.to_json(), l2, &err) && l2.setpieces_sent, "last load: set pieces sent: " + err);
        l.setpieces_sent = false;
        CHECK(tp::LastLoad::parse(l.to_json(), l2, &err) && !l2.setpieces_sent, "last load: not sent");
    });

    run_case("tactic presets: validation refuses (never clamps): build, schema, ranges, slider ids, shapes", [] {
        const std::string good = sample().to_json();
        CHECK(parse_err(good).empty(), "the sample parses");
        auto mutate = [&](const std::function<void(json&)>& f) {
            json j = json::parse(good);
            f(j);
            return parse_err(j.dump());
        };
        CHECK(part(mutate([](json& j) { j["game_build"] = "DEADBEEF-00000000"; }), "game build"), "unknown build");
        CHECK(part(mutate([](json& j) { j["turbo_tactic_preset"] = 2; }), "newer Turbo"), "newer schema");
        CHECK(part(mutate([](json& j) { j["turbo_tactic_preset"] = 0; }), "unknown schema"), "schema 0");
        CHECK(part(mutate([](json& j) { j.erase("turbo_tactic_preset"); }), "not a Turbo tactic preset"), "no marker");
        CHECK(part(mutate([](json& j) { j["name"] = ""; }), "name"), "empty name");
        CHECK(part(mutate([](json& j) { j["name"] = std::string(80, 'x'); }), "name"), "long name");
        CHECK(part(mutate([](json& j) { j["name"] = "bad\nname"; }), "name"), "control character in the name");
        CHECK(part(mutate([](json& j) { j.erase("game_build"); }), "game_build"), "no build");
        CHECK(part(mutate([](json& j) { j["tactic"]["line_height"] = 0; }), "line_height 0"), "line height 0");
        CHECK(part(mutate([](json& j) { j["tactic"]["line_height"] = 101; }), "line_height 101"), "line height 101");
        CHECK(part(mutate([](json& j) { j["tactic"]["build_up"] = 4; }), "build_up 4"), "build-up 4");
        CHECK(part(mutate([](json& j) { j["tactic"]["build_up"] = -1; }), "build_up -1"), "build-up -1");
        CHECK(part(mutate([](json& j) { j["tactic"]["approach"] = 4; }), "approach 4"), "approach 4");
        CHECK(part(mutate([](json& j) { j["tactic"]["slots"][3]["role_id"] = 64; }), "slot 3 role_id 64"), "role id 64");
        CHECK(part(mutate([](json& j) { j["tactic"]["slots"][3]["focus"] = 8; }), "slot 3 focus 8"), "focus 8");
        CHECK(part(mutate([](json& j) { j["tactic"]["slots"][3]["position"] = 28; }), "slot 3 position 28"), "position 28");
        CHECK(part(mutate([](json& j) { j["tactic"]["slots"][3]["role_group"] = 15; }), "slot 3 role_group 15"), "role group 15");
        CHECK(part(mutate([](json& j) { j["tactic"]["slots"].erase(10); }), "exactly 11"), "ten slots");
        CHECK(part(mutate([](json& j) { j["tactic"]["line_height"] = 50.5; }), "line_height"), "a fraction is not a whole number");
        CHECK(part(mutate([](json& j) { j["tactic"]["line_height"] = "50"; }), "line_height"), "text where a number belongs");
        CHECK(part(mutate([](json& j) { j["tactic"] = 5; }), "tactic is not an object"), "tactic not an object");
        CHECK(part(mutate([](json& j) { j["settings"]["layout"] = "other"; }), "layout"), "another layout");
        CHECK(part(mutate([](json& j) { j["settings"]["values"]["Bad Id"] = 5; }), "not a slider id"), "bad slider id");
        CHECK(part(mutate([](json& j) { j["settings"]["values"]["cpu.sprint_speed"] = 101; }), "cpu.sprint_speed = 101"), "slider 101");
        CHECK(part(mutate([](json& j) { j["settings"]["values"]["cpu.sprint_speed"] = -1; }), "cpu.sprint_speed = -1"), "slider -1");
        CHECK(part(mutate([](json& j) { j["settings"]["values"]["cpu.sprint_speed"] = 5.5; }), "not a whole number"), "slider fraction");
        CHECK(part(mutate([](json& j) { j["settings"]["values"] = json::object(); }), "no values"), "no values");
        CHECK(!parse_err("not json at all").empty() && part(parse_err("[1,2]"), "not a JSON object") && !parse_err("").empty(), "not JSON / not an object");
        CHECK(part(parse_err(std::string(300 * 1024, ' ')), "too large"), "too large");
        // a number out of the written range of a slot cannot be hidden in the note or created text either
        // 2.20: the limit is 4000 (the community presets carry notes of 833..1208 characters; 300 refused every one of them)
        CHECK(!mutate([](json& j) { j["note"] = std::string(tpreset::kMaxNoteLen + 1, 'n'); }).empty(), "note too long");
        CHECK(mutate([](json& j) { j["note"] = std::string(1208, 'n'); }).empty(), "a community-length note is accepted");
        // unknown extra keys are ignored (a newer writer within the same schema)
        CHECK(mutate([](json& j) { j["extra"] = 1; j["tactic"]["more"] = 2; }).empty(), "unknown keys are ignored");
    });

    run_case("tactic presets: request_for - formation refused whole, unwritable values kept, roles of the same shape only", [] {
        const tsheet::LiveTactic live = fake_live();
        tsheet::Request r;
        std::vector<std::string> notes;
        // the same tactic: a request that changes nothing
        tp::TacticBlock same = tp::block_from_live(live);
        CHECK(tp::request_for(same, live, r, notes).empty() && r.line_height == 25 && r.approach == 0 && r.build_up == 0 && r.slots.size() == 11 && notes.empty(), "same");
        CHECK(tp::diff_lines(same, live).empty(), "no difference lines");
        CHECK(!r.preset && !r.formation, "the request never asks for a formation or preset");
        // a different tactic of the same formation
        tp::TacticBlock b = other_block(live);
        CHECK(tp::request_for(b, live, r, notes).empty() && r.line_height == 90 && r.approach == 2 && r.build_up == 1 && notes.empty(), "values");
        const auto d = tp::diff_lines(b, live);
        CHECK(d.size() == 4 && has_line(d, "Defensive Approach Deep (0) -> High (2)") && has_line(d, "Line Height 25 -> 90") &&
                  has_line(d, "Build-Up Style Balanced (0) -> Counter (1)") && has_line(d, "slot 9 role 21 / focus 5 -> role 21 / focus 6"),
              "diff: " + (d.empty() ? std::string("none") : d[0]));
        bool slot9 = false;
        for (const auto& c : r.slots)
            if (c.slot == 9) slot9 = c.role_id == 21 && c.focus == 6;
        CHECK(slot9, "slot 9 requested");
        // another formation: refused whole, with the way out
        tp::TacticBlock f = b;
        f.source_formation = 11;
        f.record_name = "442";
        const std::string why = tp::request_for(f, live, r, notes);
        CHECK(part(why, "formation: preset 442 (source 11) / live 433 (source 10)") && part(why, "pick the preset's formation in Edit Tactic first"), why);
        CHECK(!tp::formation_note(f, live).empty() && tp::formation_note(b, live).empty(), "formation note");
        CHECK(tp::diff_lines(f, live).empty(), "another formation: nothing is comparable");
        // the same source formation with other positions: refused too
        tp::TacticBlock pos = b;
        pos.slots[4].position = 12;
        CHECK(part(tp::request_for(pos, live, r, notes), "other positions"), "positions");
        // unwritable values stay as they are, the rest loads
        tp::TacticBlock u = b;
        u.build_up = 2;  // Short Passing: stored value 2 is refused until decoded
        CHECK(tp::request_for(u, live, r, notes).empty() && r.build_up == -1 && r.line_height == 90 && r.approach == 2 && has_line(notes, "Build-Up Style"), "build-up kept");
        tp::TacticBlock agg = b;
        agg.approach = 3;
        agg.line_height = 95;  // Aggressive: refused
        CHECK(tp::request_for(agg, live, r, notes).empty() && r.line_height == -1 && r.approach == -1 && r.build_up == 1 && has_line(notes, "Line Height 95"),
              "line height and approach kept together");
        tp::TacticBlock band = b;
        band.approach = 1;  // line 90 is High's band, not Balanced's: 3.0.1 (GUI30H) the Line Height is written and the approach follows its band
        CHECK(tp::request_for(band, live, r, notes).empty() && r.line_height == 90 && r.approach == 2 &&
                  has_line(notes, "Defensive Approach Balanced (1) -> High (2) to match Line Height 90"),
              "inconsistent band: the line wins, the approach follows (" + (notes.empty() ? std::string("no note") : notes[0]) + ")");
        // a role group that differs is left alone, with a note
        tp::TacticBlock g2 = b;
        g2.slots[2].role_group = 5;
        CHECK(tp::request_for(g2, live, r, notes).empty() && r.slots.size() == 10 && has_line(notes, "slot 2: the role group differs"), "group");
        tp::TacticBlock none;
        CHECK(part(tp::request_for(none, live, r, notes), "no tactic part"), "no tactic block");
    });

    run_case("tactic presets 3.0.1 (GUI30H): a preset's Line Height is written, the approach follows its band; values not written are listed, never hidden", [] {
        const tsheet::LiveTactic live = fake_live();  // Line Height 25, Deep (0)
        tsheet::Request r;
        std::vector<std::string> notes;
        // GUI30H_B: GUI30G_A with only line_height 25 -> 60 (approach still Deep): b153e48 dropped both values and diff_lines was empty
        tp::TacticBlock h = tp::block_from_live(live);
        h.line_height = 60;
        CHECK(tp::request_for(h, live, r, notes).empty() && r.line_height == 60 && r.approach == 1, "GUI30H_B: line 60 + Balanced (1) requested, got line " +
                                                                                                        std::to_string(r.line_height) + " approach " + std::to_string(r.approach));
        CHECK(has_line(notes, "Defensive Approach Deep (0) -> Balanced (1) to match Line Height 60"), "GUI30H_B: the note says why the approach changes");
        const auto d = tp::diff_lines(h, live);
        CHECK(has_line(d, "Line Height 25 -> 60") && has_line(d, "Defensive Approach Deep (0) -> Balanced (1)"), "GUI30H_B diff: " + (d.empty() ? std::string("none") : d[0]));
        // each band edge
        for (auto [line, band] : {std::pair<int, int>{1, 0}, {30, 0}, {31, 1}, {61, 2}, {90, 2}}) {
            tp::TacticBlock e = tp::block_from_live(live);
            e.line_height = line;
            CHECK(tp::request_for(e, live, r, notes).empty() && r.line_height == line && r.approach == band, "line " + std::to_string(line));
        }
        // Aggressive's band (91..100): not written by design (team_sheet.cpp: Aggressive writable=false); the difference is listed as not written
        tp::TacticBlock a = tp::block_from_live(live);
        a.line_height = 95;
        a.approach = 3;
        CHECK(tp::request_for(a, live, r, notes).empty() && r.line_height == -1 && r.approach == -1 && has_line(notes, "Line Height 95"), "95 refused");
        const auto da = tp::diff_lines(a, live);
        CHECK(has_line(da, "Line Height 25 -> 95: not written") && has_line(da, "Defensive Approach Deep (0) -> Aggressive (3): not written"),
              "95: the differences are listed as not written: " + (da.empty() ? std::string("none") : da[0]));
        // out of range: never written
        tp::TacticBlock o = tp::block_from_live(live);
        o.line_height = 0;
        CHECK(tp::request_for(o, live, r, notes).empty() && r.line_height == -1 && has_line(notes, "outside 1..100"), "0 refused");
        // the same values: nothing listed (the dialog may say the tactic already holds them)
        CHECK(tp::diff_lines(tp::block_from_live(live), live).empty(), "equal: no lines");
    });

    run_case("tactic presets: the presets folder - write, list, skip bad files, unique names, delete; nothing outside the folder", [] {
        const fs::path root = g_out / "tpresets" / "files";
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path dir = tp::presets_dir(root);
        CHECK(dir.filename() == "tactic_presets", "folder name");
        tp::Preset a = sample();
        std::string err;
        CHECK(tp::write_preset(dir, a, &err) && fs::exists(dir / "Base_4-3-3.json", ec) && a.file == dir / "Base_4-3-3.json", "written: " + err);
        CHECK(!fs::exists(dir / "Base_4-3-3.json.tmp", ec), "no temporary file left");
        tp::Preset b = sample();
        b.name = "Alpha";
        b.tactic.line_height = 80;
        b.tactic.approach = 2;
        CHECK(tp::write_preset(dir, b, &err), err);
        // files that are not acceptable: skipped, named, never listed
        gsettings_test::write_file(dir / "broken.json", std::vector<uint8_t>{'{', '"', 'x'});
        json other = json::parse(a.to_json());
        other["game_build"] = "1234ABCD-00000000";
        other["name"] = "Foreign build";
        std::ofstream(dir / "foreign.json") << other.dump();
        std::ofstream(dir / "notes.txt") << "not a preset";
        std::vector<std::string> errors;
        const auto list = tp::load_presets(dir, &errors);
        CHECK(list.size() == 2 && list[0].name == "Alpha" && list[1].name == "Base 4-3-3", "listed by name");
        CHECK(errors.size() == 2 && (has_line(errors, "broken.json:") && has_line(errors, "foreign.json: made for game build")), "skipped: " + (errors.empty() ? std::string("none") : errors[0]));
        CHECK(tp::load_presets(root / "missing", nullptr).empty(), "a missing folder is an empty list");
        CHECK(tp::unique_name(list, "New") == "New" && tp::unique_name(list, "alpha") == "alpha 2" && tp::unique_name(list, "Base 4-3-3") == "Base 4-3-3 2" &&
                  tp::unique_name(list, "  ") == "Preset",
              "unique names");
        CHECK(tp::write_preset(dir, a, &err) && tp::load_presets(dir, nullptr).size() == 2, "same name = same file");
        tp::Preset bad = sample();
        bad.tactic.line_height = 500;
        CHECK(!tp::write_preset(dir, bad, &err) && part(err, "not valid") && !fs::exists(dir / "Base_4-3-3.json.tmp", ec), "an invalid preset is not written: " + err);
        CHECK(tp::delete_preset(list[0], &err) && !fs::exists(dir / "Alpha.json", ec) && fs::exists(dir / "Base_4-3-3.json", ec), "deleted one");
        CHECK(!tp::delete_preset(tp::Preset(), &err), "no file, no delete");
    });

    run_case("tactic presets: the last-load record - round trip, undo request, re-apply prompt", [] {
        const tsheet::LiveTactic live = fake_live();
        tp::LastLoad l;
        l.preset = "TEST";
        l.when = "2026-10-16 14:00:00";
        l.state = "loaded";
        l.team = 1;
        l.session = "S1";
        l.load_gen = 3;
        l.before = tp::block_from_live(live);
        l.after = other_block(live);
        l.settings_staged = true;
        l.settings_preset = "TEST";
        l.settings_skipped = {"cpu.acceleration"};
        tp::LastLoad m;
        std::string err;
        CHECK(tp::LastLoad::parse(l.to_json(), m, &err), err);
        CHECK(m.preset == "TEST" && m.state == "loaded" && m.team == 1 && m.session == "S1" && m.load_gen == 3 && m.before.same_values(l.before) &&
                  m.after.same_values(l.after) && m.settings_staged && m.settings_preset == "TEST" && m.settings_skipped == l.settings_skipped && m.to_json() == l.to_json(),
              "round trip");
        CHECK(!tp::LastLoad::parse("{}", m, &err) && !tp::LastLoad::parse("nope", m, &err), "not a last-load file");
        json j = json::parse(l.to_json());
        j["after"]["line_height"] = 500;
        CHECK(!tp::LastLoad::parse(j.dump(), m, &err) && part(err, "line_height"), "an out-of-range value in the file is refused: " + err);
        // undo request: the "before" values
        tsheet::LiveTactic now = live;
        now.line_height = 90;
        now.approach = 2;
        now.build_up = 1;
        now.slots[9].focus = 6;
        now.slots[9].role = tsheet::role_word(9, 21, 6);
        tsheet::Request r;
        std::vector<std::string> notes;
        CHECK(tp::undo_request(l, now, r, notes).empty() && r.line_height == 25 && r.approach == 0 && r.build_up == 0, "undo request");
        tp::LastLoad nob = l;
        nob.before = tp::TacticBlock();
        CHECK(!tp::undo_request(nob, now, r, notes).empty(), "no before values: nothing to put back");
        tsheet::LiveTactic moved = now;
        moved.source_formation = 12;
        CHECK(part(tp::undo_request(l, moved, r, notes), "pick the preset's formation"), "the formation was changed since: refused");
        // the prompt
        CHECK(tp::reapply_prompt(l, now, "S1", 3).empty(), "the values are in place: nothing to say");
        CHECK(part(tp::reapply_prompt(l, live, "S1", 3), "in this career load") && part(tp::reapply_prompt(l, live, "S1", 3), "Defensive Approach Deep (0) -> High (2), ..."), "the game replaced it");
        CHECK(part(tp::reapply_prompt(l, live, "S1", 4), "in an earlier career load") && part(tp::reapply_prompt(l, live, "S2", 3), "in an earlier career load"), "after a career load");
        tp::LastLoad un = l;
        un.state = "undone";
        CHECK(tp::reapply_prompt(un, live, "S1", 4).empty(), "an undone load asks nothing");
        un.state = "dismissed";
        CHECK(tp::reapply_prompt(un, live, "S1", 4).empty(), "a dismissed one asks nothing");
        tp::LastLoad other_club = l;
        other_club.team = 2;
        CHECK(tp::reapply_prompt(other_club, live, "S1", 4).empty(), "another club's load: nothing");
        tsheet::LiveTactic shape = live;
        shape.source_formation = 12;
        CHECK(tp::reapply_prompt(l, shape, "S1", 4).empty(), "another formation now: nothing to re-apply");
    });
}

// --------------------------------------------------------------------------------------------------------------------------- UI
static std::string md5_of_file(const fs::path& p) {
    std::vector<uint8_t> b;
    if (!gsettings::read_bytes(p, b)) return "";
    return gsettings::md5_hex(b);
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Teams > Tactic presets (Simple view) - Save current, Load asks first, Undo, Re-apply, Delete, refusals; reapply_edits.json untouched", [&] {
        live_tactic_reset();
        tactic_presets_reset();
        const fs::path out = le / "turbo_output";
        std::error_code ec;
        for (const char* f : {tsheet::opt_in_file(), tsheet::kill_switch_file(), tsheet::master_switch_file(), tsheet::undo_file(), tsheet::db_row_off_file(),
                              tsheet::db_row_off_legacy_file(), tp::last_load_file(), gsettings::pending_file()})
            fs::remove(out / f, ec);
        fs::remove_all(tp::presets_dir(out), ec);
        const fs::path reapply = out / "reapply_edits.json";
        const bool reapply_existed = fs::exists(reapply, ec);
        const std::string reapply_md5 = reapply_existed ? md5_of_file(reapply) : "";
        // the settings file the sliders are read from (synthetic) and its folder's md5s: never written
        const fs::path root = g_out / "tpresets" / "ui";
        fs::remove_all(root, ec);
        const fs::path settings = root / "settings";
        gsettings_test::write_file(settings / gsettings_test::kName, gsettings_test::fixture());
        gsettings_test::write_file(settings / "CmMgrC20261014211520606", std::vector<uint8_t>(4096, 0xC1));
        const auto folder_before = gsettings_test::md5s(settings);
        const fs::path map = root / "slider_map.json";
        std::ofstream(map, std::ios::binary) << gsettings_test::test_map();
        game_settings_set_paths(settings, map);
        game_settings_reset();
        // the simulated game: your club (1) with the tactic of the first UI case of test_team_sheet.h (433, Deep 25, Balanced, slot 9 role 21 focus 5)
        SheetWorld w(1, 10);
        team_sheet_test::ServiceRig rig(w, out);
        rig.find_returns = SheetWorld::kSvc;
        rig.jobs.inline_run = true;
        app.team_sheet = rig.svc;
        const fs::path state_file = out / "bridge_state.json";
        json st = read_json(state_file);
        const json saved = st;
        int bumps = 700;
        auto write_state = [&]() {
            {
                std::ofstream f(state_file.string(), std::ios::binary | std::ios::trunc);
                f << st.dump();
            }
            fs::last_write_time(state_file, fs::file_time_type::clock::now() + std::chrono::seconds(2 * ++bumps));
            app.next_poll = 0.0;
            ui.frames(3);
        };
        st["match_phase"] = "hub";
        st["managers"] = hex_addr(SheetWorld::kManagers);
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        const bool was_advanced = app.prefs.advanced_mode;
        app.prefs.advanced_mode = false;  // the Simple view
        ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1900, 1000));  // 2.13: wide enough for the select panel (it is a rail under S(900))
        ui.frames(2);
        // the tab
        app.request_tab = 1;
        ui.frames(2);
        ui.click("7", "##tlist");
        CHECK(ui.find("Tactic presets", "##tedit") != nullptr, "the tab is there in the Simple view");
        CHECK(ui.find("Tactics", "##tedit") == nullptr, "the Tactics tab is hidden in the Simple view (it stays Advanced)");
        ui.click("Tactic presets", "##tedit");
        ui.frames(3);
        const TacticPresetsState& s = tactic_presets_state();
        CHECK(s.drawn && s.banner.find("EA's own tactic/settings; takes effect at next match / needs Gameplay Type CUSTOM for sliders") == 0 &&
                  s.banner.find("Not proven in a played match yet") != std::string::npos,
              "the honest banner: " + s.banner);
        CHECK(!s.opt_in && !s.live_ok, "channel E is off: nothing read yet");
        // channel E on, from the panel
        CHECK(ui.click("###tpopt", "##tpresets"), "opt-in clicked");
        ui.frames(2);
        CHECK(fs::exists(out / tsheet::opt_in_file(), ec) && s.opt_in, "opt-in file created");
        rig.clock += 6e6;
        ui.frames(130);
        CHECK(s.live_ok && s.live_line.find("\"433\", Defensive Approach Deep (0), Line Height 25, Build-Up Style Balanced (0)") != std::string::npos, "read: " + s.live_line);
        // Save current "BASE": the tactic and the sliders block
        CHECK(ui.type_into(ui.find("##tpname", "##tpresets"), "BASE"), "name typed");
        CHECK(ui.click("Save current##tpsave", "##tpresets"), "Save current");
        ui.frames(2);
        const fs::path base_file = tp::presets_dir(out) / "BASE.json";
        CHECK(fs::exists(base_file, ec) && s.names == std::vector<std::string>({"BASE"}) && s.status.find("Saved \"BASE\"") == 0, "saved: " + s.status);
        tp::Preset base;
        std::string err;
        {
            std::ifstream f(base_file, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            CHECK(tp::Preset::parse(ss.str(), base, &err), "BASE parses: " + err);
        }
        CHECK(base.tactic.line_height == 25 && base.tactic.approach == 0 && base.tactic.build_up == 0 && base.tactic.slots[9].role_id == 21 && base.tactic.slots[9].focus == 5 &&
                  base.tactic.source_formation == 10 && base.settings.present && base.settings.values.size() == 3 && base.settings.values.count("cpu.sprint_speed") == 1 &&
                  base.settings.values.count("user.sprint_speed") == 1,
              "BASE holds the live tactic and the mapped sliders");
        CHECK(gsettings_test::md5s(settings) == folder_before, "saving read the settings file and wrote nothing");
        // TEST: a deliberately different tactic of the same formation and other sliders; OTHER: another formation; ODD: an unwritable build-up
        tp::Preset test = base;
        test.name = "TEST";
        test.tactic.line_height = 90;
        test.tactic.approach = 2;
        test.tactic.build_up = 1;
        test.tactic.slots[9].focus = 6;
        test.settings.values["cpu.sprint_speed"] = 77;
        test.settings.values["user.sprint_speed"] = 33;
        CHECK(tp::write_preset(tp::presets_dir(out), test, &err), err);
        tp::Preset other = test;
        other.name = "OTHER";
        other.tactic.source_formation = 11;
        other.tactic.record_name = "442";
        CHECK(tp::write_preset(tp::presets_dir(out), other, &err), err);
        tp::Preset odd = test;
        odd.name = "ODD";
        odd.tactic.build_up = 2;
        CHECK(tp::write_preset(tp::presets_dir(out), odd, &err), err);
        std::ofstream(tp::presets_dir(out) / "BAD.json") << json{{"turbo_tactic_preset", 9}, {"name", "from the future"}}.dump();
        CHECK(s.names == std::vector<std::string>({"BASE"}), "files made outside the game are not listed before Refresh");
        CHECK(ui.click("Refresh##tprefresh", "##tpresets"), "Refresh");
        ui.frames(3);
        CHECK(s.banner.find("Team Management closed") != std::string::npos && s.banner.find("Edit Tactic screen may still show the old wording") != std::string::npos &&
                  s.banner.find("after each career load, load the tactic again") != std::string::npos && s.banner.find("Build-Up Style 2/3 and Defensive Approach 3") != std::string::npos,
              "the banner states the limits");
        CHECK(s.names == std::vector<std::string>({"BASE", "ODD", "OTHER", "TEST"}), "the list");
        CHECK(!s.errors.empty() && s.errors[0].find("BAD.json: written by a newer Turbo") == 0, "an unusable file is named: " + (s.errors.empty() ? std::string("none") : s.errors[0]));
        auto pick = [&](const std::string& name) {
            for (size_t i = 0; i < s.names.size(); ++i)
                if (s.names[i] == name) {
                    const std::string label = s.names[i] + "  -  " + s.summaries[i] + "##tpsel" + std::to_string(i);
                    CHECK(ui.click(label, "##tpresets"), "picked " + name);
                    ui.frames(3);
                    return;
                }
            CHECK(false, "no preset " + name);
        };
        auto live_is = [&](int lh, int ap, int bu, int role, int focus) {
            return w.i32(SheetWorld::kRecs + tsheet::kRecLineHeight) == lh && w.i32(SheetWorld::kRecs + tsheet::kRecApproach) == ap &&
                   w.i32(SheetWorld::kRecs + tsheet::kRecBuildUp) == bu && w.i32(w.slot(9) + tsheet::kSlotRole) == role && w.i32(w.slot(9) + tsheet::kSlotFocus) == focus;
        };
        CHECK(live_is(25, 0, 0, 38213, 5), "the live tactic before");
        // another formation: refused whole, with the way out; nothing asked, nothing written
        pick("OTHER");
        CHECK(s.selected >= 0 && s.gate.find("pick the preset's formation in Edit Tactic first") != std::string::npos, "gate: " + s.gate);
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load OTHER");
        ui.frames(3);
        CHECK(!s.confirm_open && s.status.find("Not loaded: formation: preset 442 (source 11) / live 433 (source 10)") == 0 && live_is(25, 0, 0, 38213, 5) &&
                  !fs::exists(out / gsettings::pending_file(), ec),
              "refused: " + s.status);
        // an unwritable build-up: the rest loads, the note says so (cancelled here)
        pick("ODD");
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load ODD");
        ui.frames(3);
        CHECK(s.confirm_open && has_line(s.confirm_lines, "note: Build-Up Style"), "ODD asks, with the note");
        CHECK(ui.click("Cancel##tpno", "Load tactic preset"), "cancelled");
        ui.frames(3);
        CHECK(!s.confirm_open && live_is(25, 0, 0, 38213, 5) && !fs::exists(tp::last_load_path(out), ec), "cancel writes nothing");
        // Team Management open: refused with the channel E gate
        st["team_mgmt"] = true;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        pick("TEST");
        CHECK(s.gate.find("Team Management is open") != std::string::npos, "gate: " + s.gate);
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load while Team Management is open");
        ui.frames(3);
        CHECK(!s.confirm_open && s.status.find("Not loaded: G2 career hub: Team Management is open") == 0 && live_is(25, 0, 0, 38213, 5), "refused: " + s.status);
        st["team_mgmt"] = false;
        st["seq"] = st.value("seq", 0LL) + 1;
        write_state();
        ui.frames(70);
        // TEST: Load asks first
        pick("TEST");
        CHECK(s.gate.empty(), "ready: " + s.gate);
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load TEST");
        ui.frames(3);
        CHECK(s.confirm_open && has_line(s.confirm_lines, "Defensive Approach Deep (0) -> High (2)") && has_line(s.confirm_lines, "Line Height 25 -> 90") &&
                  has_line(s.confirm_lines, "Build-Up Style Balanced (0) -> Counter (1)") && has_line(s.confirm_lines, "slot 9 role 21 / focus 5 -> role 21 / focus 6") &&
                  has_line(s.confirm_lines, "sliders: 2 screen-proven + 0 table-mapped") && has_line(s.confirm_lines, "Gameplay Type CUSTOM"),
              "it asks first, with every value");
        {
            bool tab = false;  // 2.12.1: a path in the modal is written with a double backslash (a single one printed "\t" as a tab)
            for (const std::string& l : s.confirm_lines) tab = tab || l.find('\t') != std::string::npos;
            CHECK(!tab && has_line(s.confirm_lines, "run turbo\\turbo_settings_apply.exe --apply"), "no tab character in the modal; the apply tool's path is spelled out");
        }
        CHECK(live_is(25, 0, 0, 38213, 5) && !fs::exists(out / gsettings::pending_file(), ec), "nothing written before the Yes");
        CHECK(ui.click("Load##tpyes", "Load tactic preset"), "confirmed");
        ui.frames(6);
        CHECK(live_is(90, 2, 1, 38214, 6), "the live tactic holds TEST (map record read back)");
        tp::LastLoad ll;
        CHECK(tp::load_last(tp::last_load_path(out), ll, &err) && ll.preset == "TEST" && ll.state == "loaded" && ll.team == 1 && ll.before.line_height == 25 &&
                  ll.after.line_height == 90 && ll.settings_staged && ll.settings_preset == "TEST",
              "last-load file: " + err + " " + ll.state);
        gsettings::Pending pend;
        {
            std::ifstream f(out / gsettings::pending_file(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            CHECK(gsettings::Pending::parse(ss.str(), pend, &err) && pend.preset == "TEST" && pend.values.size() == 2 && pend.values["cpu.sprint_speed"] == 77 &&
                      pend.values["user.sprint_speed"] == 33,
                  "the verified sliders are staged: " + err);
        }
        CHECK(gsettings_test::md5s(settings) == folder_before, "the settings folder was not written (the sliders wait for the apply tool, game closed)");
        CHECK(s.status.find("Loaded \"TEST\": written into your live tactic and read back.") == 0 && s.status.find("Takes effect at the next match") != std::string::npos &&
                  s.status.find("AUTOSAVES") != std::string::npos,
              "status: " + s.status);
        tsheet::UndoRecord cu;
        CHECK(tsheet::load_undo(out / tsheet::undo_file(), cu, &err) && cu.state == "written", "the channel E undo file holds the write too: " + err);
        CHECK(s.can_undo && s.last_line.find("Last load: \"TEST\" (loaded") == 0, "last line: " + s.last_line);
        // Undo last load: asks, puts the values from before back, unstages the sliders
        CHECK(ui.click("Undo last load##tpundo", "##tpresets"), "Undo clicked");
        ui.frames(3);
        CHECK(s.confirm_open && has_line(s.confirm_lines, "Line Height 90 -> 25") && has_line(s.confirm_lines, "slot 9 role 21 / focus 6 -> role 21 / focus 5") &&
                  has_line(s.confirm_lines, "staged sliders") && has_line(s.confirm_lines, "the Tactics card's Undo"),
              "undo asks first");
        CHECK(live_is(90, 2, 1, 38214, 6), "still loaded before the Yes");
        CHECK(ui.click("Undo##tpyes", "Undo the last preset load"), "undo confirmed");
        ui.frames(6);
        CHECK(live_is(25, 0, 0, 38213, 5), "the values from before the load are back");
        CHECK(!fs::exists(out / gsettings::pending_file(), ec), "the staged sliders were discarded");
        CHECK(tp::load_last(tp::last_load_path(out), ll, &err) && ll.state == "undone" && !ll.settings_staged, "last-load file: undone");
        CHECK(!s.can_undo && s.status.find("Put back the values from before the load of \"TEST\"") == 0, "status: " + s.status);
        // load again, then the game "reloads the career": the sheet comes back with the old values and the load belongs to an earlier career load
        pick("TEST");
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load TEST again");
        ui.frames(3);
        CHECK(ui.click("Load##tpyes", "Load tactic preset"), "confirmed again");
        ui.frames(6);
        CHECK(live_is(90, 2, 1, 38214, 6), "loaded again");
        CHECK(s.reapply.empty(), "in place: no prompt");
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecLineHeight, int32_t(25));
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecApproach, int32_t(0));
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecBuildUp, int32_t(0));
        w.mem.wr(w.slot(9) + tsheet::kSlotRole, int32_t(38213));
        w.mem.wr(w.slot(9) + tsheet::kSlotFocus, int32_t(5));
        ll = tp::LastLoad();
        CHECK(tp::load_last(tp::last_load_path(out), ll, &err), err);
        ll.load_gen -= 1;  // made in an earlier career load
        CHECK(tp::save_last(tp::last_load_path(out), ll, &err), err);
        tactic_presets_reset();
        ui.frames(100);
        CHECK(s.reapply.find("Preset \"TEST\" was loaded in an earlier career load and your live tactic does not hold it now") == 0, "the prompt: " + s.reapply);
        CHECK(live_is(25, 0, 0, 38213, 5), "nothing is written by itself");
        CHECK(ui.click("Re-apply##tpreapply", "##tpresets"), "Re-apply clicked");
        ui.frames(3);
        CHECK(s.confirm_open && has_line(s.confirm_lines, "Line Height 25 -> 90"), "re-apply asks first");
        CHECK(ui.click("Re-apply##tpyes", "Re-apply preset"), "re-apply confirmed");
        ui.frames(6);
        CHECK(live_is(90, 2, 1, 38214, 6) && s.status.find("Re-applied \"TEST\"") == 0, "re-applied: " + s.status);
        ui.frames(100);
        CHECK(s.reapply.empty(), "in place again: no prompt");
        // without a channel E undo record (a new career load) the undo goes through the normal gates: a role the game's own sheets never use is refused
        fs::remove(out / tsheet::undo_file(), ec);
        live_tactic_reset();
        ui.frames(3);
        CHECK(ui.click("Undo last load##tpundo", "##tpresets"), "Undo without a channel E record");
        ui.frames(3);
        CHECK(!s.confirm_open && s.status.find("Nothing put back: G6 values: slot 9") == 0 && live_is(90, 2, 1, 38214, 6), "refused, nothing changed: " + s.status);
        // Dismiss: the game replaces it again, the person says no
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecLineHeight, int32_t(25));
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecApproach, int32_t(0));
        w.mem.wr(SheetWorld::kRecs + tsheet::kRecBuildUp, int32_t(0));
        w.mem.wr(w.slot(9) + tsheet::kSlotRole, int32_t(38213));
        w.mem.wr(w.slot(9) + tsheet::kSlotFocus, int32_t(5));
        ui.frames(100);
        CHECK(!s.reapply.empty(), "asks again: " + s.reapply);
        CHECK(ui.click("Dismiss##tpdismiss", "##tpresets"), "dismissed");
        ui.frames(100);
        CHECK(s.reapply.empty() && live_is(25, 0, 0, 38213, 5) && tp::load_last(tp::last_load_path(out), ll, &err) && ll.state == "dismissed", "no more asking");
        // 3.0.1 (GUI30H): the line height wins over an approach of another band; Undo keeps a staging that was there before the load;
        // a load whose tactic part writes nothing never says "already holds" or "Loaded" alone
        CHECK(part(s.sliders_line, "from the Settings file the game saved last, not from the running game"), "the sliders line says where it is read from: " + s.sliders_line);
        auto pending_of = [&](gsettings::Pending& pd) {
            std::ifstream f(out / gsettings::pending_file(), std::ios::binary);
            if (!f) return false;
            std::stringstream ss;
            ss << f.rdbuf();
            std::string e;
            return gsettings::Pending::parse(ss.str(), pd, &e);
        };
        {
            std::string smsg;
            int staged_n = 0;
            std::vector<std::string> skipped_n;
            CHECK(game_settings_stage(app, "KEEP", {{"cpu.sprint_speed", 41}, {"user.sprint_speed", 42}}, smsg, staged_n, skipped_n, false), "an earlier staging: " + smsg);
        }
        tp::Preset h60 = base;
        h60.name = "H60";
        h60.tactic.line_height = 60;  // the approach stays Deep (0): GUI30H_B
        h60.settings.values["cpu.sprint_speed"] = 34;
        h60.tactic.build_up = 2;  // a Build-Up Style Turbo does not write (no decoded label): listed as not written, never -1 in the last-load file
        CHECK(tp::write_preset(tp::presets_dir(out), h60, &err), err);
        tp::Preset a95 = base;
        a95.name = "A95";
        a95.tactic.line_height = 95;  // Aggressive's band: not written by design
        a95.tactic.approach = 3;
        tp::Preset a95t = a95;  // the same without sliders: nothing at all to load
        a95t.name = "A95T";
        a95t.settings = tp::SettingsBlock();
        CHECK(tp::write_preset(tp::presets_dir(out), a95, &err) && tp::write_preset(tp::presets_dir(out), a95t, &err), err);
        CHECK(ui.click("Refresh##tprefresh", "##tpresets"), "Refresh");
        ui.frames(3);
        ui.frames(70);
        pick("H60");
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load H60");
        ui.frames(3);
        CHECK(s.confirm_open && has_line(s.confirm_lines, "Line Height 25 -> 60") && has_line(s.confirm_lines, "Defensive Approach Deep (0) -> Balanced (1)") &&
                  !has_line(s.confirm_lines, "already holds"),
              "H60 asks with the line and the band's approach: " + (s.confirm_lines.empty() ? std::string("no dialog: ") + s.status : s.confirm_lines[0]));
        if (s.confirm_open) {
            CHECK(ui.click("Load##tpyes", "Load tactic preset"), "H60 confirmed");
            ui.frames(6);
        }
        CHECK(live_is(60, 1, 0, 38213, 5), "the live tactic holds Line Height 60 / Balanced (1)");
        CHECK(s.status.find("Loaded \"H60\": written into your live tactic and read back.") == 0, "status: " + s.status);
        CHECK(s.status.find(" Not written: Build-Up Style") != std::string::npos, "the status names what was not written: " + s.status);
        {
            tp::LastLoad hl;
            std::string le;
            CHECK(tp::load_last(tp::last_load_path(out), hl, &le) && hl.after.line_height == 60 && hl.after.approach == 1 && hl.after.build_up == 0,
                  "the last-load file parses and keeps the live Build-Up Style (b153e48 wrote -1 there for an unwritten field): " + le);
        }
        gsettings::Pending pk;
        CHECK(pending_of(pk) && pk.preset == "H60" && pk.values["cpu.sprint_speed"] == 34, "H60's sliders are staged");
        CHECK(ui.click("Undo last load##tpundo", "##tpresets"), "Undo H60");
        ui.frames(3);
        if (s.confirm_open) {
            CHECK(ui.click("Undo##tpyes", "Undo the last preset load"), "undo confirmed");
            ui.frames(6);
        }
        CHECK(live_is(25, 0, 0, 38213, 5), "Undo puts Line Height 25 / Deep back");
        pk = gsettings::Pending();
        CHECK(pending_of(pk) && pk.preset == "KEEP" && pk.values["cpu.sprint_speed"] == 41 && pk.values["user.sprint_speed"] == 42,
              "Undo restored the staging that was there before the load (b153e48 deleted it)");
        // A95 with sliders: the sliders load, the tactic is not changed and the dialog and the status say so
        pick("A95");
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load A95");
        ui.frames(3);
        CHECK(s.confirm_open && !has_line(s.confirm_lines, "already holds") && has_line(s.confirm_lines, "the tactic is NOT changed") &&
                  has_line(s.confirm_lines, "Line Height 25 -> 95: not written"),
              "A95 dialog: " + (s.confirm_lines.empty() ? std::string("none") : s.confirm_lines[0]));
        if (s.confirm_open) {
            CHECK(ui.click("Load##tpyes", "Load tactic preset"), "A95 confirmed");
            ui.frames(6);
        }
        CHECK(live_is(25, 0, 0, 38213, 5) && s.status.find("the tactic is NOT changed") != std::string::npos && s.status.find("Loaded \"A95\".") != 0,
              "A95 status: " + s.status);
        // A95T: nothing to load, and the reason is the unwritten line height, not "already holds"
        pick("A95T");
        CHECK(ui.click("Apply preset##tpload", "##tpresets"), "Load A95T");
        ui.frames(3);
        CHECK(!s.confirm_open && s.status.find("Nothing to load") == 0 && s.status.find("already holds") == std::string::npos &&
                  s.status.find("Line Height 25 -> 95: not written") != std::string::npos,
              "A95T: " + s.status);
        if (s.confirm_open) ui.click("Cancel##tpno", "Load tactic preset");
        fs::remove(out / gsettings::pending_file(), ec);
        for (const char* n : {"H60", "A95", "A95T"}) fs::remove(tp::presets_dir(out) / (std::string(n) + ".json"), ec);
        CHECK(ui.click("Refresh##tprefresh", "##tpresets"), "Refresh");
        ui.frames(3);
        // Delete: two clicks
        pick("OTHER");
        CHECK(ui.click("More##tpmore", "##tpresets"), "More (destructive actions live there)");
        ui.frames(2);
        CHECK(ui.click("Delete##tpdel", "##tpresets"), "Delete");
        ui.frames(2);
        CHECK(fs::exists(tp::presets_dir(out) / "OTHER.json", ec), "one click deletes nothing");
        CHECK(ui.click("Really delete?##tpdel", "##tpresets"), "Really delete?");
        ui.frames(2);
        CHECK(!fs::exists(tp::presets_dir(out) / "OTHER.json", ec) && s.names == std::vector<std::string>({"BASE", "ODD", "TEST"}) && s.status.find("Deleted \"OTHER\"") == 0,
              "deleted: " + s.status);
        // the same name again: the new preset gets a free name and the old file stays
        CHECK(ui.type_into(ui.find("##tpname", "##tpresets"), "BASE"), "name typed");
        CHECK(ui.click("Save current##tpsave", "##tpresets"), "Save current again");
        ui.frames(2);
        CHECK(fs::exists(tp::presets_dir(out) / "BASE_2.json", ec) && s.status.find("saved as \"BASE 2\"") != std::string::npos, "unique name: " + s.status);
        // nothing else was touched
        CHECK(gsettings_test::md5s(settings) == folder_before, "the settings folder is still untouched");
        CHECK(fs::exists(reapply, ec) == reapply_existed && (!reapply_existed || md5_of_file(reapply) == reapply_md5), "reapply_edits.json is byte for byte what it was");
        // clean up
        app.prefs.advanced_mode = was_advanced;
        app.team_sheet = nullptr;
        for (const char* f : {tsheet::opt_in_file(), tsheet::kill_switch_file(), tsheet::undo_file(), tp::last_load_file(), gsettings::pending_file()}) fs::remove(out / f, ec);
        fs::remove_all(tp::presets_dir(out), ec);
        st = saved;
        st["seq"] = saved.value("seq", 0LL) + 50;
        write_state();
        live_tactic_reset();
        tactic_presets_reset();
        game_settings_set_paths(fs::path(), fs::path());
        game_settings_reset();
    });
}

}  // namespace tactic_presets_test
