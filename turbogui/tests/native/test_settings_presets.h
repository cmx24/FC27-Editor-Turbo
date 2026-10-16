// Native tests (T4 settings-ui, 2.10): the apply tool's evidence guard (turbo_settings_apply.exe --undo / --undo --force REFUSE while a
// game-written Settings* file is not copied into the evidence folder) and the verified-only preset flow. Included by test_main.cpp after
// test_game_settings.h (uses its Rig, fixture and helpers); every folder is under the run's output folder, the real settings folder is
// never opened. The guard is the pure part of src/settings_apply/main.cpp (TURBO_SETTINGS_APPLY_GUARD_ONLY skips the Windows main).
#pragma once
#define TURBO_SETTINGS_APPLY_GUARD_ONLY
#include "settings_apply/main.cpp"
#undef TURBO_SETTINGS_APPLY_GUARD_ONLY
#include "test_game_settings.h"

namespace settings_presets_test {

using namespace turbo;
using namespace turbo::gsettings;
using namespace gsettings_test;
using turbo::settings_tool::undo_evidence_guard;

// Turbo writes a change (backup + md5s), then the game "saves": its own newer file replaces Turbo's (the incident)
static std::string game_saves(Rig& r, const std::string& stamp) {
    r.env.stamp = stamp;
    Outcome o = patch(r.env, r.map, {{0xEA, 4, "cpu.sprint_speed"}}, "t");
    CHECK(o.ok, o.message);
    std::error_code ec;
    fs::remove(r.settings / kName, ec);
    write_file(r.settings / "Settings20261015101516976", fixture(11));
    return md5_of(r.settings / "Settings20261015101516976");
}

static std::vector<uint8_t> bytes_of(const fs::path& p) {
    std::vector<uint8_t> b;
    read_bytes(p, b);
    return b;
}

static void run() {
    std::printf("native settings presets: evidence guard and verified-only flow (2.10)\n");

    run_case("settings tool: --undo (and --undo --force) refuse while a game-written Settings file is not in the evidence folder; nothing changes", [] {
        Rig r("sp_refuse");
        const std::string game = game_saves(r, "20261015-100000-000");
        const fs::path ev = r.root / "evidence";
        const auto g = undo_evidence_guard(r.settings, r.out, fs::path(), false);
        CHECK(!g.ok && g.message.find("--evidence") != std::string::npos && g.message.find("Settings20261015101516976") != std::string::npos, g.message);
        CHECK(md5_of(r.settings / "Settings20261015101516976") == game && !fs::exists(r.settings / kName) && !fs::exists(ev), "game file untouched, nothing created");
        // the tool runs the guard before undo_last for --force too (the guard has no force input), so --force is refused the same way
        const auto g2 = undo_evidence_guard(r.settings, r.out, fs::path(), false);
        CHECK(!g2.ok, "same refusal for --force");
    });

    run_case("settings tool: --evidence copies the game-written file first (md5 verified), then --undo --force replaces it", [] {
        Rig r("sp_copy");
        const std::string orig = md5_of(r.settings / kName);
        const std::string others = r.others();
        const std::string game = game_saves(r, "20261015-100100-000");
        const fs::path ev = r.root / "evidence" / "g_s1";
        auto g = undo_evidence_guard(r.settings, r.out, ev, true);  // dry run: reports, copies nothing
        CHECK(g.ok && g.lines.size() == 1 && g.lines[0].find("would be copied") != std::string::npos && !fs::exists(ev), "dry run");
        g = undo_evidence_guard(r.settings, r.out, ev, false);
        CHECK(g.ok && g.lines.size() == 1 && g.lines[0].find("copied to") != std::string::npos, g.message);
        CHECK(md5_of(ev / "Settings20261015101516976") == game, "evidence copy has the game file's md5");
        g = undo_evidence_guard(r.settings, r.out, ev, false);
        CHECK(g.ok && g.lines.size() == 1 && g.lines[0].find("already in the evidence folder") != std::string::npos, "second call");
        const Outcome o = undo_last(r.env, true);
        CHECK(o.ok && md5_of(r.settings / kName) == orig && r.others() == others, o.message);
        CHECK(!fs::exists(r.settings / "Settings20261015101516976") && md5_of(ev / "Settings20261015101516976") == game, "game file replaced, its copy kept");
    });

    run_case("settings tool: a copy under another name counts; an unrelated file of the same name is not overwritten", [] {
        Rig r("sp_alias");
        game_saves(r, "20261015-100200-000");
        const fs::path ev = r.root / "ev";
        write_file(ev / "my_copy.bin", bytes_of(r.settings / "Settings20261015101516976"));
        auto g = undo_evidence_guard(r.settings, r.out, ev, false);
        CHECK(g.ok && g.lines.size() == 1 && g.lines[0].find("already in the evidence folder") != std::string::npos, "alias copy accepted");
        Rig r2("sp_alias2");
        const std::string game2 = game_saves(r2, "20261015-100300-000");
        const fs::path ev2 = r2.root / "ev";
        write_file(ev2 / "Settings20261015101516976", std::vector<uint8_t>(10, 1));
        g = undo_evidence_guard(r2.settings, r2.out, ev2, false);
        CHECK(g.ok && md5_of(ev2 / ("Settings20261015101516976." + game2.substr(0, 8))) == game2, "second copy made: " + g.message);
        CHECK(bytes_of(ev2 / "Settings20261015101516976").size() == 10, "the unrelated file is kept");
    });

    run_case("settings tool: nothing to protect -> the guard lets undo through (no backup, Turbo's own file, the original)", [] {
        Rig r("sp_ok");
        auto g = undo_evidence_guard(r.settings, r.out, fs::path(), false);
        CHECK(g.ok && g.lines.empty(), "no backup yet");
        r.env.stamp = "20261015-100400-000";
        Outcome o = patch(r.env, r.map, {{0xEA, 5, "cpu.sprint_speed"}}, "t");
        CHECK(o.ok, o.message);
        g = undo_evidence_guard(r.settings, r.out, fs::path(), false);
        CHECK(g.ok && g.lines.empty(), "the file is still the one Turbo wrote");
        o = undo_last(r.env, false);
        CHECK(o.ok, o.message);
        g = undo_evidence_guard(r.settings, r.out, fs::path(), false);
        CHECK(g.ok && g.lines.empty(), "after the undo the original is back");
    });

    run_case("settings presets: Apply (stage + apply) writes verified entries only; unverified stay read-only; nothing in the shipped map is verified", [] {
        Rig r("sp_verified");
        SliderMap shipped;
        std::string err;
        CHECK(load_map(fs::path("..") / "turbo" / "package" / "turbo" / "settings" / map_file(), shipped, &err), err);
        int verified = 0, with_proof = 0;
        for (const Entry& e : shipped.entries) {
            verified += e.verified ? 1 : 0;
            with_proof += e.proof.empty() ? 0 : 1;
        }
        CHECK(!shipped.entries.empty() && verified == 3 && with_proof == 3, "exactly the three G-S1 Custom bytes are verified, each with a proof");
        Preset p;
        p.name = "Mixed";
        p.values = {{"cpu.sprint_speed", 41}, {"user.sprint_speed", 59}, {"cpu.acceleration", 12}, {"match.half_length", 6}};
        const Plan pl = plan_preset(p, r.map);
        CHECK(pl.values.size() == 2 && pl.skipped.size() == 2 && pl.values.count("cpu.acceleration") == 0, "plan: 2 verified, 2 read-only skipped");
        const uint32_t acc = r.at(0xEB);
        std::string msg;
        CHECK(stage_pending(r.out, p, r.map, "2026-10-15T10:00:00", &msg), msg);
        const Outcome o = apply_pending(r.env, r.map);
        CHECK(o.ok && r.at(0xEA) == 41 && r.at(0x116) == 59 && r.at(0xEB) == acc, "applied the verified two: " + o.message);
        // verified without a proof is refused at load: an unproven row cannot become writable
        SliderMap bad;
        std::string t = test_map();
        const std::string cut = ", \"proof\": \"EV/test#cpu\"";
        const size_t k = t.find(cut);
        CHECK(k != std::string::npos, "fixture text");
        t.erase(k, cut.size());
        CHECK(!SliderMap::parse(t, bad, &err) && err.find("proof") != std::string::npos, "verified without proof refused: " + err);
        // only unverified values: nothing staged
        Preset q;
        q.name = "Unproven";
        q.values = {{"cpu.acceleration", 3}};
        std::error_code ec;
        fs::remove(r.out / pending_file(), ec);
        CHECK(!stage_pending(r.out, q, r.map, "t", &msg) && !fs::exists(r.out / pending_file(), ec), msg);
    });
}

}  // namespace settings_presets_test
