// 2.20 K1 kitplan: offline timing of the write-once kit plan (core/kit_swap.h) on a SCRATCH LE root (a copy of turbo_output\kits:
// swap_state.json, kit_index.cache and the staged folders the state names), with the game's real Oodle and tocs (read only).
//   step 1: ONE donor plan (build_plan for one staged slot, the kit index from kit_index.cache): the stage-time cost per kit
//   step 2: Runtime::replan_all(): every row of swap_state.json planned and its swap_plan.bin written (the Diagnostics action)
//   step 3: a new Runtime, start(): the boot path, the table from the plans only (no scan, no plan): the boot load time
//   step 4: the same boot with only the rows of the teams given by --boot-teams (the CS218V set: 2056, 112558)
// Nothing outside the scratch root is written.
// Usage: kit_plan_timing <scratch LE root> <FC 27 folder> <team> <slot> [game build] [--boot-teams a,b]
//
// Plan writer (built with -DKITPLAN_RUNTIME; the Tester's pre-boot step, the same code the Kits page's "Rebuild all kit plans" runs in the game):
//   kit_plan_timing --write-plans <LE root> <FC 27 folder> [game build]
//   Every row of <LE root>\turbo_output\kits\swap_state.json is planned (Runtime::replan_all) and its swap_plan.bin written next to the staged
//   kit; then a second Runtime starts the way the game does and must publish the table "from plans" with every row ready. The game build is
//   read from <FC 27 folder>\FC27.exe (TimeDateStamp-SizeOfImage, the key the running game gives) unless given. The game's files are read only.
//   Writes under <LE root>\turbo_output: kits\<team>\<slot>\swap_plan.bin, kits\kit_index.cache (only when missing or stale) and
//   kit_swap_log.txt. swap_state.json is read, never written. Exit 0 = every row planned and the start published from the plans.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
#include "kit_swap.h"

using namespace turbo::kitswap;
namespace fs = std::filesystem;

static double since(std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

#ifdef KITPLAN_RUNTIME
#include "nlohmann/json.hpp"
static Status wait_done(Runtime& rt, double limit_s) {
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        Status s = rt.status();
        if (s.state != "building" && s.state != "planning") {
            rt.wait_idle();
            return rt.status();
        }
        if (since(t0) > limit_s * 1000.0) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static void print_status(const char* name, const Status& s, double wall_ms) {
    int swapped = 0, left = 0;
    for (const auto& f : s.folders) (f.swapped ? swapped : left)++;
    std::printf("%s: state %s, origin %s, table %zu blocks, %.1f MB, published %.0f ms after start (table step %.0f ms), wall %.0f ms\n", name, s.state.c_str(),
                s.table_origin.c_str(), s.blocks, double(s.bytes) / 1048576.0, s.ready_after_ms, s.table_ms, wall_ms);
    std::printf("  %s\n  slots %zu, kit folders swapped %d, left %d, items %zu\n  note: %s\n", s.message.c_str(), s.slots.size(), swapped, left, s.items.size(), s.cache_note.c_str());
    int planned = 0;
    for (const auto& p : s.plans) {
        if (p.state == "ready") ++planned;
        else std::printf("  plan team %d slot %d: %s (%s)\n", p.team, p.slot, p.state.c_str(), p.reason.c_str());
    }
    std::printf("  plans ready %d of %zu\n", planned, s.plans.size());
}
#endif

static OodleFn load_oodle(const std::string& game) {
    OodleFn oodle;
#ifdef _WIN32
    HMODULE m = LoadLibraryW((fs::path(game) / "oo2core_9_win64.dll").wstring().c_str());
    using Fn = int64_t (*)(const void*, int64_t, void*, int64_t, int, int, int, void*, int64_t, void*, void*, void*, int64_t, int);
    Fn fn = m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress"))) : nullptr;
    if (fn)
        oodle = [fn](const uint8_t* s, size_t n, uint8_t* d, size_t dn) { return fn(s, int64_t(n), d, int64_t(dn), 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3) == int64_t(dn); };
#endif
    return oodle;
}

#ifdef KITPLAN_RUNTIME
// FC27.exe's TimeDateStamp-SizeOfImage from the file's PE header (the running game's key: src/win/kit_swap_win.cpp, core/asset_catalog exe_build_key)
static std::string exe_build(const fs::path& exe) {
    std::ifstream f(exe, std::ios::binary);
    unsigned char h[4096] = {};
    f.read(reinterpret_cast<char*>(h), sizeof(h));
    const size_t n = size_t(f.gcount());
    auto u32 = [&](size_t o) { return uint32_t(h[o] | (h[o + 1] << 8) | (h[o + 2] << 16) | (uint32_t(h[o + 3]) << 24)); };
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return "";
    const size_t pe = u32(0x3C), opt = pe + 24;
    if (pe < 0x40 || opt + 60 > n || std::memcmp(h + pe, "PE\0\0", 4) != 0) return "";
    char b[32];
    std::snprintf(b, sizeof(b), "%08X-%08X", unsigned(u32(pe + 8)), unsigned(u32(opt + 56)));
    return b;
}

static int write_plans(const std::string& le, const std::string& game, std::string build) {
    if (build.empty()) build = exe_build(fs::path(game) / "FC27.exe");
    if (build.empty()) {
        std::printf("FC27.exe not readable in %s: give the game build (e.g. 6AC07E31-2145C000)\n", game.c_str());
        return 2;
    }
    const OodleFn oodle = load_oodle(game);
    if (!oodle) {
        std::printf("no Oodle (oo2core_9_win64.dll in %s)\n", game.c_str());
        return 2;
    }
    std::printf("plan writer: LE root %s, game %s, build %s\n", le.c_str(), game.c_str(), build.c_str());
    {
        Runtime rt(le, game, oodle);
        rt.require_opt_in = false;
        rt.game_build = build;
        rt.log_line = [](const std::string& l) { std::printf("  log: %s\n", l.c_str()); };
        rt.start();
        print_status("BEFORE (the plans as they are)", wait_done(rt, 60), 0);
        const auto t0 = std::chrono::steady_clock::now();
        rt.replan_all();
        print_status("PLANNED (Rebuild all kit plans)", wait_done(rt, 3600), since(t0));
    }
    Runtime rt(le, game, oodle);
    rt.require_opt_in = false;
    rt.game_build = build;
    const auto t0 = std::chrono::steady_clock::now();
    rt.start();
    const Status s = wait_done(rt, 120);
    print_status("START FROM PLANS (what the next boot does)", s, since(t0));
    int ready = 0;
    for (const auto& p : s.plans) ready += p.state == "ready";
    const bool ok = s.state == "ready" && s.table_origin == "from plans" && !s.plans.empty() && ready == int(s.plans.size());
    // the count the in-game check expects for each club: kit_swap_log.txt "served team T from plans: X of <this> textures ..."
    std::map<int, int> per_team;
    for (const auto& it : s.items)
        if (it.swap && it.team >= 0) ++per_team[it.team];
    for (const auto& t : per_team) std::printf("EXPECT team %d: %d textures in the table (livecheck_kitcache.ps1 -Expect plans -Team %d -ExpectTextures %d)\n", t.first, t.second, t.first, t.second);
    std::printf("PLAN WRITER: %s - %d of %zu kits planned; start from plans: %zu blocks, %.0f ms\n", ok ? "PASS" : "FAIL", ready, s.plans.size(), s.blocks, s.table_ms);
    return ok ? 0 : 1;
}
#endif

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--write-plans") {
#ifdef KITPLAN_RUNTIME
        return write_plans(argv[2], argv[3], argc >= 5 ? argv[4] : "");
#else
        std::fprintf(stderr, "--write-plans needs a build with -DKITPLAN_RUNTIME\n");
        return 2;
#endif
    }
    if (argc < 5) {
        std::fprintf(stderr, "usage: kit_plan_timing <scratch LE root> <FC 27 folder> <team> <slot> [game build] [--boot-teams a,b]\n");
        return 2;
    }
    const std::string le = argv[1], game = argv[2];
    const int team = std::atoi(argv[3]), slot = std::atoi(argv[4]);
    std::string build = "6AC07E31-2145C000";
    std::set<int> boot_teams;
    for (int i = 5; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--boot-teams" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string t;
            while (std::getline(ss, t, ',')) boot_teams.insert(std::atoi(t.c_str()));
        } else {
            build = a;
        }
    }
    const OodleFn oodle = load_oodle(game);
    if (!oodle) {
        std::printf("no Oodle\n");
        return 2;
    }
    int rc = 0;
    // ---- step 1: one donor plan, the way the stage-time planner runs it
    {
        GameFiles files(game);
        std::string err;
        if (!files.init(&err)) {
            std::printf("game files: %s\n", err.c_str());
            return 2;
        }
        const fs::path kits = fs::path(le) / "turbo_output" / "kits";
        KitIndex idx;
        auto t0 = std::chrono::steady_clock::now();
        const bool idx_ok = load_index_cache(files, kits / "kit_index.cache", idx);
        std::printf("kit index from kit_index.cache: %s, %.0f ms (%zu bundles, %zu kit chunks)\n", idx_ok ? "yes" : "NO (signature or file)", since(t0), idx.bundles.size(), idx.users.size());
        if (!idx_ok) return 2;
        static const char* names[] = {"home", "away", "goalkeeper", "third", "fourth"};
        StagedKit sk;
        if (!load_staged(kits / std::to_string(team) / names[slot], sk, &err)) {
            std::printf("staged %d %d: %s\n", team, slot, err.c_str());
            return 2;
        }
        for (int run = 1; run <= 2; ++run) {
            Options o;
            o.share_within_club = true;  // the live rows' choice
            TableBuilder tb;
            t0 = std::chrono::steady_clock::now();
            Plan p = build_plan(files, oodle, idx, sk, o, true, tb, nullptr);
            const double ms = since(t0);
            int sw = 0;
            for (const auto& f : p.folders) sw += f.swapped;
            std::printf("ONE DONOR PLAN run %d (team %d slot %d): %.0f ms, ok %d, items %zu, swapped %d, skipped %d, probes %d, folders swapped %d of %zu%s%s\n", run, team, slot, ms,
                        int(p.ok), p.items.size(), p.swapped, p.skipped, p.probes, sw, p.folders.size(), p.error.empty() ? "" : ", error ", p.error.c_str());
            for (const auto& n : p.notes) std::printf("  note: %s\n", n.c_str());
        }
    }
#ifdef KITPLAN_RUNTIME
    // ---- step 2: every row planned (the explicit Diagnostics action), plans written
    {
        Runtime rt(le, game, oodle);
        rt.require_opt_in = false;
        rt.game_build = build;
        rt.log_line = [](const std::string& l) { std::printf("  log: %s\n", l.c_str()); };
        rt.start();
        Status s0 = wait_done(rt, 60);
        print_status("BOOT BEFORE ANY PLAN (expect every row off, no build)", s0, 0);
        const auto t0 = std::chrono::steady_clock::now();
        rt.replan_all();
        const Status s = wait_done(rt, 1800);
        print_status("REPLAN ALL (the Diagnostics action)", s, since(t0));
        rc |= s.state == "ready" ? 0 : 1;
    }
    // ---- step 3: the boot, from plans only
    for (int run = 1; run <= 2; ++run) {
        Runtime rt(le, game, oodle);
        rt.require_opt_in = false;
        rt.game_build = build;
        rt.log_line = [](const std::string& l) { std::printf("  log: %s\n", l.c_str()); };
        const auto t0 = std::chrono::steady_clock::now();
        rt.start();
        const Status s = wait_done(rt, 120);
        char name[64];
        std::snprintf(name, sizeof(name), "BOOT FROM PLANS, all rows, run %d", run);
        print_status(name, s, since(t0));
        rc |= s.table_origin == "from plans" ? 0 : 1;
    }
    // ---- step 4: the boot with only the given teams' rows (a copy of swap_state.json with those rows, the plans as they are)
    if (!boot_teams.empty()) {
        const fs::path state = fs::path(le) / "turbo_output" / "kits" / "swap_state.json";
        const fs::path keep = state.string() + ".all";
        std::error_code ec;
        fs::copy_file(state, keep, fs::copy_options::overwrite_existing, ec);
        {
            std::ifstream f(state);
            std::stringstream b;
            b << f.rdbuf();
            nlohmann::json j = nlohmann::json::parse(b.str(), nullptr, false);
            nlohmann::json out = j;
            out["kits"] = nlohmann::json::array();
            for (const auto& k : j["kits"])
                if (boot_teams.count(k.value("team", -1))) out["kits"].push_back(k);
            std::ofstream(state, std::ios::trunc) << out.dump(2);
        }
        for (int run = 1; run <= 2; ++run) {
            Runtime rt(le, game, oodle);
            rt.require_opt_in = false;
            rt.game_build = build;
            const auto t0 = std::chrono::steady_clock::now();
            rt.start();
            const Status s = wait_done(rt, 120);
            char name[80];
            std::snprintf(name, sizeof(name), "BOOT FROM PLANS, CS218V rows only, run %d", run);
            print_status(name, s, since(t0));
            rc |= s.table_origin == "from plans" ? 0 : 1;
        }
        fs::copy_file(keep, state, fs::copy_options::overwrite_existing, ec);
        fs::remove(keep, ec);
    }
#endif
    return rc;
}
