// Native tests: 2.20 K1 "kitplan", the write-once swap plan of a staged kit (core/kit_swap.h: make_swap_plan, save_swap_plan,
// load_swap_plan, replay_plan, assemble_table; Runtime::request_plan / start). Included by test_main.cpp after test_kit_share.h;
// kit_plan_test::run().
//
// The synthetic game (tests/catalog/kit_world.h, stand-in codec: compressed == raw), never an EA file. What is proven, offline:
//   (i)   a plan round-trips: written, read back, the same items, verdicts, census users, blocks and replacement bytes; replayed it gives the
//         table the builder gave;
//   (ii)  a plan whose key no longer matches (another game build, a changed toc, other options, a re-staged or changed picture, a damaged
//         file) is refused with the Kits page's words, the kit is off, and nothing is planned, built or erased at start;
//   (iii) a Runtime started with two plans (and no kit_table.cache, no kit index) publishes the same table, verdicts and status lists as the
//         all-kits builder (build_plan per kit into one builder, then the clash rule between kits) for the same two kits, including a clash;
//   (iv)  a start with zero plans publishes an empty table in under 100 ms and logs no plan, build or scan;
//   (v)   every sharing / census rule case gives the same verdict from a plan read back as from build_plan itself.
// NOT proven here: anything in the game (the Tester's livecheck_kitcache.ps1 -Expect plans).
#pragma once
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../catalog/kit_world.h"
#include "core/kit_swap.h"

namespace kit_plan_test {
namespace ks = turbo::kitswap;
namespace kw = kitworld;
namespace pfs = std::filesystem;

constexpr int kClub = 95, kOther = 77;
struct Spec {
    const char* role;
    const char* part;
    const char* leaf;  // with %d for the club
    uint32_t fmt;
    int w, h, mips;
};
static const Spec kJc = {"jersey_color", "jersey", "jersey_%d_%d_0_color", 55, 256, 256, 9}, kJn = {"jersey_normal", "jersey", "jersey_%d_%d_0_normal", 63, 128, 128, 8},
                  kSc = {"shorts_color", "shorts", "shorts_%d_%d_0_color", 55, 128, 128, 8}, kSo = {"socks_color", "socks", "socks_%d_%d_0_color", 55, 64, 64, 7};
static const char* kKinds[] = {"home_0_0", "away_1_0", "goalie_home_2_0"};
static const char* kSlotDirs[] = {"home", "away", "goalkeeper"};

// The rule cases (v): the subject is club 95's home jersey normal map (a detail map: its verdict never takes the whole kit with it)
enum class Case { Own, ClubSlotChunk, OtherClubChunk, OtherClubBytes, ClubOwnBytes };
struct World {
    pfs::path root, le;
    kw::GameBuilder g;
    std::mt19937 rng{2020};
};
static uint32_t total_of(const Spec& s) {
    uint32_t t = 0;
    for (uint32_t x : ks::bc_mip_sizes(ks::dxgi_of_format(s.fmt), s.w, s.h, s.mips)) t += x;
    return t;
}
static kw::GameBuilder::TexSpec tex(int team, int slot, const Spec& s, std::vector<uint8_t> raw = {}, int share = -1) {
    char leaf[80];
    std::snprintf(leaf, sizeof(leaf), s.leaf, team, slot);
    return kw::GameBuilder::TexSpec{kw::P(kKinds[slot], s.part, leaf, team), s.fmt, s.w, s.h, s.mips, std::move(raw), share, 8192};
}
// chunks: 0..3 club 95 home (jersey, jersey normal, shorts, socks); then the case's other bundle (4..7)
static World make_world(const pfs::path& base, Case c) {
    World w;
    w.root = base / "game";
    w.le = base / "le";
    w.g.root = w.root;
    const std::vector<uint8_t> jn = w.g.random_bytes(total_of(kJn));
    w.g.add_bundle({tex(kClub, 0, kJc), tex(kClub, 0, kJn, jn), tex(kClub, 0, kSc), tex(kClub, 0, kSo)});
    switch (c) {
        case Case::Own:
        case Case::OtherClubChunk:
            w.g.add_bundle({tex(kOther, 0, kJc), tex(kOther, 0, kJn, {}, c == Case::OtherClubChunk ? 1 : -1), tex(kOther, 0, kSc), tex(kOther, 0, kSo)});
            break;
        case Case::OtherClubBytes:
            w.g.add_bundle({tex(kOther, 0, kJc), tex(kOther, 0, kJn, jn), tex(kOther, 0, kSc), tex(kOther, 0, kSo)});
            break;
        case Case::ClubSlotChunk:
            w.g.add_bundle({tex(kClub, 1, kJc), tex(kClub, 1, kJn, {}, 1), tex(kClub, 1, kSc), tex(kClub, 1, kSo)});
            break;
        case Case::ClubOwnBytes:
            w.g.add_bundle({tex(kClub, 1, kJc), tex(kClub, 1, kJn, jn), tex(kClub, 1, kSc), tex(kClub, 1, kSo)});
            break;
    }
    w.g.write();
    return w;
}
static pfs::path stage(World& w, int team, int slot, std::map<std::string, std::vector<uint8_t>>* keep = nullptr) {
    const pfs::path dir = w.le / "turbo_output" / "kits" / std::to_string(team) / kSlotDirs[slot];
    pfs::remove_all(dir);
    std::vector<kw::json> t;
    for (const Spec* s : {&kJc, &kJn, &kSc, &kSo}) {
        std::vector<uint8_t> data;
        kw::stage_texture(dir, s->role, s->fmt, s->w, s->h, s->mips, t, w.rng, &data);
        if (keep) (*keep)[s->role] = data;
    }
    kw::write_manifest(dir, team, slot, t);
    return dir;
}
static void opt_in(const World& w) {
    pfs::create_directories(w.le / "turbo_output");
    std::ofstream(w.le / "turbo_output" / "kits3d_on.txt") << "\n";
}
static std::string read_text(const pfs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static const ks::PlanItem* subject(const ks::Plan& p) {
    for (const auto& i : p.items)
        if (!i.probe && i.role == "jersey_normal" && i.kind == "home_0_0") return &i;
    return nullptr;
}
static std::string users(const std::vector<ks::KitUser>& v) { return ks::users_text(v); }
static bool same_items(const ks::Plan& a, const ks::Plan& b, std::string* what) {
    if (a.items.size() != b.items.size()) return (*what = fmt("%zu items vs %zu", a.items.size(), b.items.size())), false;
    for (size_t i = 0; i < a.items.size(); ++i) {
        const auto &x = a.items[i], &y = b.items[i];
        if (x.res != y.res || x.kind != y.kind || x.role != y.role || x.swap != y.swap || x.reason != y.reason || x.probe != y.probe || x.blocks != y.blocks ||
            x.bytes != y.bytes || x.table_item != y.table_item || x.left_by != y.left_by || users(x.shared) != users(y.shared))
            return (*what = "item " + std::to_string(i) + " (" + x.res + "): " + x.reason + " | " + y.reason), false;
    }
    if (a.folders.size() != b.folders.size()) return (*what = "folders"), false;
    for (size_t i = 0; i < a.folders.size(); ++i) {
        const auto &x = a.folders[i], &y = b.folders[i];
        if (x.kind != y.kind || x.swapped != y.swapped || x.why != y.why || x.role != y.role || x.reason != y.reason || users(x.shared) != users(y.shared) ||
            x.details_left != y.details_left)
            return (*what = "folder " + x.kind), false;
    }
    if (a.notes != b.notes || a.ok != b.ok || a.error != b.error || a.swapped != b.swapped || a.skipped != b.skipped || a.probes != b.probes) return (*what = "notes / counts"), false;
    return true;
}
// Two tables answer every block of the synthetic game the same way (entry, replacement bytes, item, aliases, swap flag)
static bool same_tables(const World& w, const ks::Table& a, const ks::Table& b, std::string* what) {
    if (a.size() != b.size() || a.items() != b.items() || a.bytes() != b.bytes() || a.min_csize() != b.min_csize() || a.max_csize() != b.max_csize())
        return (*what = fmt("size %zu/%zu items %u/%u bytes %llu/%llu", a.size(), b.size(), a.items(), b.items(), (unsigned long long)a.bytes(), (unsigned long long)b.bytes())), false;
    for (uint32_t i = 0; i < a.items(); ++i)
        if (a.swap_item(i) != b.swap_item(i)) return (*what = fmt("swap flag of item %u", i)), false;
    int seen = 0;
    for (const auto& c : w.g.chunk_locs) {
        const std::vector<uint8_t> bytes(w.g.cas.begin() + std::ptrdiff_t(c[0]), w.g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
        std::vector<ks::Block> bl;
        if (!ks::block_headers(bytes, bl)) continue;
        for (const auto& k : bl) {
            const uint8_t* src = &bytes[size_t(k.payload)];
            const ks::Entry *x = a.match(src, k.csize, k.dsize), *y = b.match(src, k.csize, k.dsize);
            if (!x != !y) return (*what = "one table knows a block the other does not"), false;
            if (!x) continue;
            ++seen;
            if (x->item != y->item || x->alias_n != y->alias_n || !x->data != !y->data || (x->data && std::memcmp(x->data, y->data, x->dsize) != 0))
                return (*what = fmt("block of item %u differs", x->item)), false;
        }
    }
    *what = fmt("%d blocks compared", seen);
    return true;
}
static ks::SlotState slot_state(int team, int slot, ks::Mode m = ks::Mode::Swap, bool share_club = false, bool share_other = false) {
    ks::SlotState s;
    s.team = team;
    s.slot = slot;
    s.mode = m;
    s.share_club = share_club;
    s.share_other = share_other;
    return s;
}

static void run() {
    const pfs::path base = test_tmp_dir("kit_plan");
    std::error_code ec;
    pfs::remove_all(base, ec);
    const ks::OodleFn oodle = kw::stand_in_oodle();

    run_case("kit plan (i): a plan round-trips: written, read back, the same verdicts, census users, blocks and bytes; replayed it is the builder's table", [&] {
        World w = make_world(base / "i", Case::OtherClubBytes);
        stage(w, kClub, 0);
        ks::GameFiles files(w.root);
        std::string err;
        ks::KitIndex idx;
        CHECK(files.init(&err) && ks::scan_kits(files, oodle, idx, nullptr, &err), "synthetic game scanned: " + err);
        const pfs::path dir = w.le / "turbo_output" / "kits" / "95" / "home";
        ks::StagedKit sk;
        CHECK(ks::load_staged(dir, sk, &err), "staged: " + err);
        ks::Options base_opt;
        const ks::SlotState s = slot_state(kClub, 0, ks::Mode::Swap, false, true);
        const ks::PlanKey key = ks::make_plan_key("BUILD-A", ks::index_signature(files), s, dir, base_opt, ks::nonkit_key(idx, base_opt));
        const ks::SwapPlan sp = ks::make_swap_plan(files, oodle, idx, sk, s, base_opt, key);
        CHECK(sp.plan.ok && !sp.records.empty() && !sp.transient && sp.kinds.count("home_0_0"), "planned: " + sp.plan.error);
        const ks::PlanItem* jn = subject(sp.plan);
        CHECK(jn && jn->swap && jn->reason.find("byte-identical blocks") != std::string::npos && !jn->shared.empty() && jn->shared[0].team == kOther,
              "the census found club 77's identical blocks: " + (jn ? jn->reason : std::string("(none)")));
        const pfs::path file = dir / "swap_plan.bin";
        CHECK(ks::save_swap_plan(file, sp) && pfs::exists(file), "written to the kit's staged folder");
        ks::SwapPlan back;
        std::string why, reason;
        CHECK(ks::load_swap_plan(file, key, back, &why, &reason) && why.empty() && reason.empty(), "read back with the same key: " + why);
        std::string what;
        CHECK(same_items(sp.plan, back.plan, &what), "the same items, verdicts and census users: " + what);
        CHECK(back.kinds == sp.kinds && back.donor == sp.donor && back.key.fields == sp.key.fields && back.records.size() == sp.records.size(), "the same kit folders, donor and key");
        bool blocks_same = back.records.size() == sp.records.size();
        for (size_t i = 0; blocks_same && i < sp.records.size(); ++i) {
            const auto &a = sp.records[i], &b = back.records[i];
            blocks_same = a.swap == b.swap && !a.data == !b.data && a.blocks.size() == b.blocks.size() && (!a.data || *a.data == *b.data);
            for (size_t k = 0; blocks_same && k < a.blocks.size(); ++k)
                blocks_same = a.blocks[k].csize == b.blocks[k].csize && a.blocks[k].dsize == b.blocks[k].dsize && a.blocks[k].head == b.blocks[k].head &&
                              a.blocks[k].fnv == b.blocks[k].fnv && a.blocks[k].off == b.blocks[k].off;
        }
        CHECK(blocks_same, "the same blocks and replacement bytes");
        // replayed into a builder: the table build_plan gave
        ks::TableBuilder direct;
        ks::build_plan(files, oodle, idx, sk, ks::plan_options(base_opt, s), true, direct, nullptr);
        ks::TableBuilder replay;
        ks::Plan p;
        ks::replay_plan(back, replay, p);
        const auto ta = direct.finish(nullptr), tb = replay.finish(nullptr);
        CHECK(same_tables(w, *ta, *tb, &what) && ta->size() > 0, "replayed: the same table (" + what + ")");
    });

    run_case("kit plan (ii): a stale key is refused with the Kits page's words; the kit is off at start, nothing planned, built or erased", [&] {
        World w = make_world(base / "ii", Case::Own);
        opt_in(w);
        std::map<std::string, std::vector<uint8_t>> keep;
        const pfs::path dir = stage(w, kClub, 0, &keep);
        std::string err, log;
        {
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-A";
            rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a stale / missing plan in the background (test_auto_plan, test_kit_swap.cpp)
            rt.log_line = [&](const std::string& l) { log += l + "\n"; };
            CHECK(rt.activate(kClub, 0, ks::Mode::Swap, &err), "switched on: " + err);
            rt.wait_idle();
            const ks::Status st = rt.status();
            CHECK(st.state == "ready" && st.table_origin == "from plans" && st.blocks > 0 && pfs::exists(rt.plan_path(kClub, 0)),
                  "switching on planned the kit and published from its plan: " + st.message);
            CHECK(log.find("kit swap: planned team 95 home in") != std::string::npos && log.find("kit swap: table from plans") != std::string::npos, "logged: " + log);
        }
        const pfs::path plan = dir / "swap_plan.bin";
        const std::string state_before = read_text(w.le / "turbo_output" / "kits" / "swap_state.json");
        const std::string plan_before = read_text(plan);
        auto boot = [&](const std::string& build, const std::string& expect_reason, const std::string& expect_why, const std::string& what) {
            std::string blog;
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = build;
            rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a stale / missing plan in the background (test_auto_plan, test_kit_swap.cpp)
            rt.log_line = [&](const std::string& l) { blog += l + "\n"; };
            rt.start();
            rt.wait_idle();
            const ks::Status st = rt.status();
            const ks::Status::PlanRow* row = st.plans.empty() ? nullptr : &st.plans[0];
            CHECK(st.state == "ready" && st.table_origin == "from plans" && st.blocks == 0 && row && row->state != "ready" && row->reason == expect_reason,
                  what + ": the kit is off with \"" + expect_reason + "\": " + (row ? row->state + " / " + row->reason : std::string("no row")));
            CHECK(blog.find("is off: " + expect_reason) != std::string::npos && blog.find("[" + expect_why) != std::string::npos, what + ": a log line says so: " + blog);
            CHECK(blog.find("kit swap: planned") == std::string::npos && blog.find("table built") == std::string::npos && read_text(plan) == plan_before, what + ": nothing planned or rewritten at start");
            CHECK(read_text(w.le / "turbo_output" / "kits" / "swap_state.json") == state_before, what + ": swap_state.json untouched (no row erased)");
            rt.flush_log(true);
            const std::string sl = read_text(rt.log_path());
            CHECK(sl.find("table: from plans, 0 blocks") != std::string::npos && sl.find("plan team 95 home: " + (row ? row->state : std::string("?")) + ": " + expect_reason) != std::string::npos,
                  what + ": kit_swap_log.txt names the reason");
        };
        boot("BUILD-B", ks::kPlanStaleReason, "stale: game build", "another game build");
        {
            ks::SwapPlan sp;
            std::string why, reason;
            ks::GameFiles files(w.root);
            files.init(nullptr);
            const ks::PlanKey k2 = ks::make_plan_key("BUILD-B", ks::index_signature(files), slot_state(kClub, 0), dir, ks::Options(), ks::nonkit_key(ks::KitIndex(), ks::Options()));
            CHECK(!ks::load_swap_plan(plan, k2, sp, &why, &reason) && why == "stale: game build" && reason == "Re-stage this kit (planned for another game version)",
                  "load_swap_plan refuses it: " + why + " / " + reason);
        }
        {   // a game update (layout.toc written again: another write time)
            const pfs::path toc = w.root / "Data" / "layout.toc";
            const auto t0 = pfs::last_write_time(toc);
            pfs::last_write_time(toc, t0 + std::chrono::hours(1));
            boot("BUILD-A", ks::kPlanStaleReason, "stale: kit index", "a changed toc (game update)");
            pfs::last_write_time(toc, t0);
        }
        {   // other kit options (swap_state.json edited outside Turbo)
            std::string s2 = state_before;
            const size_t at = s2.find("\"share_within_club\": false");
            if (at != std::string::npos) s2.replace(at, 26, "\"share_within_club\": true");
            std::ofstream(w.le / "turbo_output" / "kits" / "swap_state.json", std::ios::binary | std::ios::trunc) << s2;
            const std::string edited = s2;
            std::string blog;
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-A";
            rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a stale / missing plan in the background (test_auto_plan, test_kit_swap.cpp)
            rt.start();
            rt.wait_idle();
            const ks::Status st = rt.status();
            CHECK(!st.plans.empty() && st.plans[0].reason == ks::kPlanOptionsReason && st.blocks == 0, "other kit options: " + (st.plans.empty() ? std::string("no row") : st.plans[0].reason));
            CHECK(read_text(w.le / "turbo_output" / "kits" / "swap_state.json") == edited, "the edited row stays as it is");
            std::ofstream(w.le / "turbo_output" / "kits" / "swap_state.json", std::ios::binary | std::ios::trunc) << state_before;
        }
        {   // a picture re-staged with other bytes, same size (the manifest is the same: the bytes check refuses it)
            const pfs::path dds = dir / "shorts_color.dds";
            std::string b = read_text(dds);
            b[b.size() - 1] = char(b[b.size() - 1] ^ 0x5A);
            std::ofstream(dds, std::ios::binary | std::ios::trunc) << b;
            boot("BUILD-A", ks::kPlanStagedReason, "staged data changed", "a staged picture with other bytes");
            b[b.size() - 1] = char(b[b.size() - 1] ^ 0x5A);
            std::ofstream(dds, std::ios::binary | std::ios::trunc) << b;
        }
        {   // a re-stage (the manifest changes)
            const std::string m = read_text(dir / "manifest.json");
            std::ofstream(dir / "manifest.json", std::ios::binary | std::ios::trunc) << m << "\n";
            boot("BUILD-A", ks::kPlanStagedReason, "stale: manifest", "a re-staged kit (other manifest)");
            std::ofstream(dir / "manifest.json", std::ios::binary | std::ios::trunc) << m;
        }
        {   // a damaged plan file
            std::string b = plan_before;
            b[b.size() / 2] = char(b[b.size() / 2] ^ 1);
            std::ofstream(plan, std::ios::binary | std::ios::trunc) << b;
            const std::string damaged = read_text(plan);
            std::string blog;
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-A";
            rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a stale / missing plan in the background (test_auto_plan, test_kit_swap.cpp)
            rt.log_line = [&](const std::string& l) { blog += l + "\n"; };
            rt.start();
            rt.wait_idle();
            const ks::Status st = rt.status();
            CHECK(!st.plans.empty() && st.plans[0].state == "failed" && st.plans[0].reason == ks::kPlanDamagedReason && st.blocks == 0 && read_text(plan) == damaged,
                  "a damaged plan: off, never repaired at start: " + (st.plans.empty() ? std::string("no row") : st.plans[0].reason));
            std::ofstream(plan, std::ios::binary | std::ios::trunc) << plan_before;
        }
        {   // the same files again: from the plan, served
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-A";
            rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a stale / missing plan in the background (test_auto_plan, test_kit_swap.cpp)
            rt.start();
            rt.wait_idle();
            const ks::Status st = rt.status();
            CHECK(!st.plans.empty() && st.plans[0].state == "ready" && st.blocks > 0 && st.table_origin == "from plans", "unchanged files: the kit is served from its plan: " + st.message);
            // Plan it now / Rebuild all kit plans: an explicit action plans it again
            rt.replan_all();
            rt.wait_idle();
            CHECK(!rt.status().plans.empty() && rt.status().plans[0].state == "ready" && rt.status().blocks == st.blocks, "Rebuild all kit plans: planned again, the same table size");
        }
        {   // a plan made for another build, then Plan it now: planned and served
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-C";
            rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a stale / missing plan in the background (test_auto_plan, test_kit_swap.cpp)
            rt.start();
            rt.wait_idle();
            CHECK(rt.status().blocks == 0, "another build: off");
            rt.request_plan(kClub, 0, true);
            rt.wait_idle();
            CHECK(!rt.status().plans.empty() && rt.status().plans[0].state == "ready" && rt.status().blocks > 0, "Plan it now: planned for this build and served");
            rt.deactivate(kClub, 0);
            rt.wait_idle();
        }
    });

    run_case("kit plan (iii): a Runtime started with two plans and no table cache publishes the all-kits builder's table, verdicts and lists (with a clash)", [&] {
        World w = make_world(base / "iii", Case::OtherClubChunk);
        opt_in(w);
        stage(w, kClub, 0);
        stage(w, kOther, 0);
        std::string err;
        {
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-A";
            CHECK(rt.activate(kClub, 0, ks::Mode::Swap, &err) && rt.activate(kOther, 0, ks::Mode::Swap, &err), "two kits switched on: " + err);
            rt.wait_idle();
            CHECK(rt.set_slot_options(kClub, 0, true, true, false, true) && rt.set_slot_options(kOther, 0, true, true, false, true), "both share with other clubs (a clash on the shared chunk)");
            rt.wait_idle();
        }
        pfs::remove(w.le / "turbo_output" / "kits" / "kit_table.cache", ec);
        pfs::remove(w.le / "turbo_output" / "kits" / "kit_index.cache", ec);
        std::string log;
        ks::Runtime rt(w.le, w.root, oodle);
        rt.game_build = "BUILD-A";
        rt.log_line = [&](const std::string& l) { log += l + "\n"; };
        rt.start();
        rt.wait_idle();
        const ks::Status st = rt.status();
        CHECK(st.state == "ready" && st.table_origin == "from plans" && st.plans.size() == 2 && st.plans[0].state == "ready" && st.plans[1].state == "ready",
              "started from the two plans: " + st.message);
        CHECK(log.find("kit swap: planned") == std::string::npos && !pfs::exists(w.le / "turbo_output" / "kits" / "kit_table.cache") &&
                  !pfs::exists(w.le / "turbo_output" / "kits" / "kit_index.cache"),
              "no plan made, no table cache, no kit index scanned at start: " + log);
        // the all-kits builder for the same two kits, in swap_state.json order
        ks::GameFiles files(w.root);
        ks::KitIndex idx;
        CHECK(files.init(&err) && ks::scan_kits(files, oodle, idx, nullptr, &err), "reference: the game scanned");
        ks::TableBuilder tb;
        std::vector<ks::Plan> plans;
        for (const ks::SlotState& s : st.slots) {
            ks::StagedKit sk;
            ks::load_staged(rt.staged_dir(s.team, s.slot), sk, &err);
            plans.push_back(ks::build_plan(files, oodle, idx, sk, ks::plan_options(rt.get_options(), s), s.mode == ks::Mode::Swap, tb, nullptr));
        }
        ks::Assembled ref = ks::assemble_table(plans, tb);
        std::string what;
        CHECK(rt.table() && same_tables(w, *ref.table, *rt.table(), &what) && ref.table->size() > 0, "the same table: " + what);
        bool lists = ref.items.size() == st.items.size() && ref.folders.size() == st.folders.size();
        for (size_t i = 0; lists && i < ref.items.size(); ++i)
            lists = ref.items[i].res == st.items[i].res && ref.items[i].swap == st.items[i].swap && ref.items[i].reason == st.items[i].reason &&
                    ref.items[i].table_item == st.items[i].table_item && ref.items[i].copies == st.items[i].copies && ref.items[i].left_by == st.items[i].left_by;
        for (size_t i = 0; lists && i < ref.folders.size(); ++i)
            lists = ref.folders[i].kind == st.folders[i].kind && ref.folders[i].swapped == st.folders[i].swapped && ref.folders[i].reason == st.folders[i].reason;
        CHECK(lists, fmt("the same item and folder lists (%zu / %zu items)", ref.items.size(), st.items.size()));
        int clashed = 0;
        for (const auto& it : st.items) clashed += it.left_by == ks::LeftBy::Clash;
        CHECK(clashed == 2, fmt("the clash between the two kits is kept (%d items left by it)", clashed));
        CHECK(rt.kinds_of(kClub).count("home_0_0") == 1, "the Kits page's folder list comes from the plans without the index");
        // the in-game check's line: kit_swap_log.txt "served team T from plans: X of N textures in the table loaded by the game, B blocks (first ...)",
        // N = the count the plan writer prints (EXPECT team T: N textures in the table)
        int n_club = 0;
        for (const auto& it : st.items) n_club += it.swap && it.team == kClub;
        rt.flush_log(true);
        const std::string before = read_text(rt.log_path());
        CHECK(n_club > 0 && before.find(fmt("served team %d from plans: 0 of %d textures in the table loaded by the game, 0 blocks\n", kClub, n_club)) != std::string::npos,
              fmt("before the game loads a kit: served team %d from plans: 0 of %d ...", kClub, n_club));
        int served = 0;
        for (const auto& c : w.g.chunk_locs) {  // the game decompresses every kit chunk once (a match day)
            std::vector<uint8_t> chunk(w.g.cas.begin() + std::ptrdiff_t(c[0]), w.g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
            std::vector<ks::Block> bl;
            if (!ks::block_headers(chunk, bl)) continue;
            for (const ks::Block& b : bl) {
                std::vector<uint8_t> out(b.dsize, 0);
                if (rt.on_block(&chunk[size_t(b.payload)], b.csize, out.data(), b.dsize) != 0) ++served;
            }
        }
        rt.flush_log(true);
        const std::string after = read_text(rt.log_path());
        const std::string head = fmt("served team %d from plans: ", kClub);
        const size_t at = after.find(head);
        const std::string line = at == std::string::npos ? std::string() : after.substr(at, after.find('\n', at) - at);
        int x = -1, n = -1;
        unsigned long long blocks = 0;
        const bool parsed = !line.empty() && std::sscanf(line.c_str() + head.size(), "%d of %d textures in the table loaded by the game, %llu blocks", &x, &n, &blocks) == 3;
        CHECK(served > 0 && parsed && x > 0 && x <= n && n == n_club && blocks > 0 && line.find(" (first ") != std::string::npos,
              fmt("after the game loaded the kits (%d blocks served): ", served) + line);
        rt.deactivate(kClub, 0);
        rt.deactivate(kOther, 0);
        rt.wait_idle();
    });

    run_case("kit plan (iv): a start with zero plans publishes an empty table in under 100 ms and logs no plan, build or scan", [&] {
        World w = make_world(base / "iv", Case::Own);
        opt_in(w);
        stage(w, kClub, 0);
        stage(w, kOther, 0);
        std::ofstream(w.le / "turbo_output" / "kits" / "swap_state.json", std::ios::binary)
            << R"({"version":2,"watch_club":true,"kits":[{"team":95,"slot":"home","slot_type":0,"mode":"swap"},{"team":77,"slot":"home","slot_type":0,"mode":"swap"}]})";
        const std::string state_before = read_text(w.le / "turbo_output" / "kits" / "swap_state.json");
        std::string log;
        ks::Runtime rt(w.le, w.root, oodle);
        rt.game_build = "BUILD-A";
        rt.auto_plan = false;  // 2.20 first publish: 4.0 plans a missing plan in the background (test_auto_plan, test_kit_swap.cpp)
        rt.log_line = [&](const std::string& l) { log += l + "\n"; };
        const auto t0 = std::chrono::steady_clock::now();
        rt.start();
        rt.wait_idle();
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const ks::Status st = rt.status();
        CHECK(st.state == "ready" && st.table_origin == "from plans" && rt.table() && rt.table()->size() == 0 && st.ready_after_ms >= 0 && st.ready_after_ms < 100,
              fmt("an empty table, published %.1f ms after start (wall %.1f ms): ", st.ready_after_ms, wall) + st.message);
        CHECK(st.plans.size() == 2 && st.plans[0].state == "none" && st.plans[0].reason == ks::kPlanNoneReason && st.plans[1].reason == ks::kPlanNoneReason,
              "both kits off: " + (st.plans.empty() ? std::string("no rows") : st.plans[0].reason));
        CHECK(log.find("kit swap: planned") == std::string::npos && log.find("table built") == std::string::npos && log.find("scann") == std::string::npos &&
                  log.find("kit swap: team 95 home is off: Re-stage this kit (it has no swap plan yet)") != std::string::npos,
              "no plan, build or scan logged; the reason is: " + log);
        CHECK(!pfs::exists(w.le / "turbo_output" / "kits" / "kit_index.cache") && !pfs::exists(rt.plan_path(kClub, 0)) && !pfs::exists(rt.plan_path(kOther, 0)),
              "no index scanned and saved, no plan written");
        CHECK(read_text(w.le / "turbo_output" / "kits" / "swap_state.json") == state_before, "swap_state.json untouched");
        rt.flush_log(true);
        const std::string sl = read_text(rt.log_path());
        CHECK(sl.find("table: from plans, 0 blocks") != std::string::npos && sl.find("kit index: not read") != std::string::npos && sl.find("scanned") == std::string::npos,
              "kit_swap_log.txt: from plans, the index not read");
        // stage time: an import plans the kit at once (not the next start); switching it on then needs no second plan
        std::string plog;
        ks::Runtime r2(w.le, w.root, oodle);
        r2.game_build = "BUILD-A";
        r2.auto_plan = false;  // 2.20 first publish: 4.0 plans a missing plan in the background (test_auto_plan, test_kit_swap.cpp)
        r2.log_line = [&](const std::string& l) { plog += l + "\n"; };
        r2.start();
        r2.wait_idle();
        r2.request_plan(kClub, 0);
        r2.wait_idle();
        CHECK(plog.find("kit swap: planned team 95 home in") != std::string::npos && pfs::exists(r2.plan_path(kClub, 0)) && r2.status().blocks > 0, "request_plan (the import's exit): planned, written and served");
        std::string err;
        CHECK(r2.activate(kClub, 0, ks::Mode::Swap, &err), "switched on again: " + err);
        r2.wait_idle();
        size_t n = 0;
        for (size_t at = plog.find("kit swap: planned team 95 home"); at != std::string::npos; at = plog.find("kit swap: planned team 95 home", at + 1)) ++n;
        CHECK(n == 1, fmt("one plan for the kit, not one per action (%zu)", n));
        r2.deactivate(kClub, 0);
        r2.deactivate(kOther, 0);
        r2.wait_idle();
    });

    run_case("kit plan (vi): a kit the game has no 3D textures for is planned as 'nothing': not off, not a fault, the club's other kit still served", [&] {
        World w = make_world(base / "vi", Case::Own);
        opt_in(w);
        stage(w, kClub, 0);
        stage(w, kClub, 1);  // the synthetic game has only club 95's home kit
        std::string err;
        {
            ks::Runtime rt(w.le, w.root, oodle);
            rt.game_build = "BUILD-A";
            CHECK(rt.activate(kClub, 0, ks::Mode::Swap, &err) && rt.activate(kClub, 1, ks::Mode::Swap, &err), "home and away switched on: " + err);
            rt.wait_idle();
        }
        ks::Runtime rt(w.le, w.root, oodle);
        rt.game_build = "BUILD-A";
        rt.start();
        rt.wait_idle();
        const ks::Status st = rt.status();
        const ks::Status::PlanRow* home = nullptr;
        const ks::Status::PlanRow* away = nullptr;
        for (const auto& p : st.plans) (p.slot == 0 ? home : away) = &p;
        CHECK(home && home->state == "ready" && st.table_origin == "from plans" && st.blocks > 0, "home: from its plan, served: " + st.message);
        CHECK(away && away->state == "nothing" && away->reason == ks::kNothingToSwap && pfs::exists(rt.plan_path(kClub, 1)),
              "away: planned as nothing (plan kept, so the next start does not plan it again): " + (away ? away->state + " / " + away->reason : std::string("no row")));
        CHECK(st.message.find("off until planned") == std::string::npos, "the start does not call it off: " + st.message);
        rt.flush_log(true);
        const std::string sl = read_text(rt.log_path());
        CHECK(sl.find(std::string("plan team 95 away: nothing: ") + ks::kNothingToSwap) != std::string::npos && sl.find("served team 95 from plans: 0 of ") != std::string::npos,
              "kit_swap_log.txt: the away row says nothing, the club's served line is there");
        rt.deactivate(kClub, 0);
        rt.deactivate(kClub, 1);
        rt.wait_idle();
    });

    run_case("kit plan (v): every sharing and census rule gives the same verdict from a plan read back as from build_plan", [&] {
        struct RuleCase {
            Case c;
            bool share_club, share_other;
            bool served;
            ks::LeftBy by;
            const char* words;
            const char* name;
        };
        const RuleCase cases[] = {
            {Case::Own, false, false, true, ks::LeftBy::None, "", "own chunk: served"},
            {Case::ClubSlotChunk, false, false, false, ks::LeftBy::ThisClub, "the chunk is also used by this club's away_1_0", "this club's other slot, same chunk, option off"},
            {Case::ClubSlotChunk, true, false, true, ks::LeftBy::None, "shared with this club's away_1_0", "this club's other slot, same chunk, option on"},
            {Case::OtherClubChunk, false, false, false, ks::LeftBy::OtherClubs, "the chunk is also used by team 77 home_0_0", "another club, same chunk, option off"},
            {Case::OtherClubChunk, false, true, true, ks::LeftBy::None, "also changes team 77 home_0_0 (the same chunk)", "another club, same chunk, option on"},
            {Case::OtherClubBytes, false, false, false, ks::LeftBy::OtherClubs, "a byte-identical compressed block is also in another club's kit chunk (team 77 home_0_0)",
             "census: another club's identical blocks, option off"},
            {Case::OtherClubBytes, false, true, true, ks::LeftBy::None, "also changes team 77 home_0_0 (byte-identical blocks", "census: another club's identical blocks, option on"},
            {Case::ClubOwnBytes, false, false, false, ks::LeftBy::ThisClubData, "a byte-identical compressed block is also in another kit chunk (team 95 away_1_0)",
             "census: this club's own identical blocks"},
            {Case::ClubOwnBytes, true, true, false, ks::LeftBy::ThisClubData, "a byte-identical compressed block is also in another kit chunk (team 95 away_1_0)",
             "census: this club's own identical blocks, both options on"},
        };
        int n = 0;
        for (const RuleCase& rc : cases) {
            const std::string tag = std::string("rule ") + rc.name;
            World w = make_world(base / ("v" + std::to_string(n++)), rc.c);
            opt_in(w);
            stage(w, kClub, 0);
            ks::GameFiles files(w.root);
            std::string err;
            ks::KitIndex idx;
            if (!(files.init(&err) && ks::scan_kits(files, oodle, idx, nullptr, &err))) {
                CHECK(false, tag + ": game scanned: " + err);
                continue;
            }
            ks::StagedKit sk;
            const pfs::path dir = w.le / "turbo_output" / "kits" / "95" / "home";
            ks::load_staged(dir, sk, &err);
            ks::Options base_opt;
            const ks::SlotState s = slot_state(kClub, 0, ks::Mode::Swap, rc.share_club, rc.share_other);
            ks::TableBuilder direct;
            const ks::Plan ref = ks::build_plan(files, oodle, idx, sk, ks::plan_options(base_opt, s), true, direct, nullptr);
            const ks::PlanItem* it = subject(ref);
            CHECK(it && it->swap == rc.served && it->left_by == rc.by && it->reason.find(rc.words) != std::string::npos,
                  tag + ": build_plan's verdict: " + (it ? it->reason : std::string("(no item)")));
            const ks::PlanKey key = ks::make_plan_key("BUILD-A", ks::index_signature(files), s, dir, base_opt, ks::nonkit_key(idx, base_opt));
            const ks::SwapPlan sp = ks::make_swap_plan(files, oodle, idx, sk, s, base_opt, key);
            ks::SwapPlan back;
            std::string why, what;
            CHECK(ks::save_swap_plan(dir / "swap_plan.bin", sp) && ks::load_swap_plan(dir / "swap_plan.bin", key, back, &why), tag + ": written and read back: " + why);
            CHECK(same_items(ref, back.plan, &what), tag + ": the plan read back has build_plan's verdicts: " + what);
            ks::TableBuilder replay;
            ks::Plan p;
            ks::replay_plan(back, replay, p);
            CHECK(same_tables(w, *direct.finish(nullptr), *replay.finish(nullptr), &what), tag + ": and its table: " + what);
        }
    });
    pfs::remove_all(base, ec);
}

}  // namespace kit_plan_test
