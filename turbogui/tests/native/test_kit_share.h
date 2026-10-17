// Native tests: 3D kits, 2.13 "never half a kit" and the opt-in to change other clubs' kits that share a texture (core/kit_swap.h,
// Options::share_other_clubs, FolderVerdict; ui/ui_kits.h kit_swap_status_line). Included by test_main.cpp after test_kit_fonts.h;
// kit_share_test::run().
//
// The synthetic game (tests/catalog/kit_world.h, stand-in codec: compressed == raw): club 481's goalkeeper folder goalie_home_2_0 has a
// jersey picture whose chunk is a byte-identical copy of club 2's goalkeeper jersey (EA's shared goalkeeper templates: Sevilla and Aston
// Villa), a jersey normal map that IS club 2's chunk, and its own shorts and socks. What is proven: with the option off the whole folder
// stays the game's and the reason names club 2's kit; with it on everything is served and the notes name club 2's kit as changed too (the
// table serves club 2's identical blocks); a folder whose shorts could be served but whose jersey cannot is never partly served; two active
// kits that want the same game data with other pictures both stay whole; the option survives a restart and a file without it loads off.
// NOT proven here: anything in the game (KIT_GK_LIVECHECK.md).
#pragma once
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../catalog/kit_world.h"
#include "core/kit_swap.h"
#include "ui/ui_kits.h"

namespace kit_share_test {
namespace ks = turbo::kitswap;
namespace kw = kitworld;
namespace sfs = std::filesystem;

constexpr int kSev = 481, kVilla = 2;
struct Spec {
    const char* role;
    uint32_t fmt;
    int w, h, mips;
};
static const Spec kJersey = {"jersey_color", 55, 256, 256, 9}, kJerseyN = {"jersey_normal", 63, 128, 128, 8}, kShorts = {"shorts_color", 55, 128, 128, 8},
                  kSocks = {"socks_color", 55, 64, 64, 7};

struct World {
    sfs::path root, le;
    kw::GameBuilder g;
    std::mt19937 rng{481};
};
// chunks: 0 481 jersey (= bytes of 4), 1 481 jersey normal (= chunk of 5), 2 481 shorts, 3 481 socks, 4 club 2 jersey, 5 club 2 jersey normal,
// 6 club 2 shorts, 7 club 2 socks
static World make_world(const sfs::path& base) {
    World w;
    w.root = base / "game";
    w.le = base / "le";
    w.g.root = w.root;
    using T = kw::GameBuilder::TexSpec;
    uint32_t total = 0;
    for (uint32_t x : ks::bc_mip_sizes(ks::dxgi_of_format(55), 256, 256, 9)) total += x;
    const std::vector<uint8_t> template_jersey = w.g.random_bytes(total);  // EA's goalkeeper jersey template, stored twice
    auto tex = [](int team, const char* part, const char* leaf, const Spec& s, std::vector<uint8_t> raw = {}, int share = -1, size_t block = 8192) {
        return T{kw::P("goalie_home_2_0", part, leaf, team), s.fmt, s.w, s.h, s.mips, std::move(raw), share, block};
    };
    w.g.add_bundle({tex(kSev, "jersey", "jersey_481_2_0_color", kJersey, template_jersey, -1, 16384), tex(kSev, "jersey", "jersey_481_2_0_normal", kJerseyN),
                    tex(kSev, "shorts", "shorts_481_2_0_color", kShorts), tex(kSev, "socks", "socks_481_2_0_color", kSocks)});
    w.g.add_bundle({tex(kVilla, "jersey", "jersey_2_2_0_color", kJersey, template_jersey, -1, 16384), tex(kVilla, "jersey", "jersey_2_2_0_normal", kJerseyN, {}, 1),
                    tex(kVilla, "shorts", "shorts_2_2_0_color", kShorts), tex(kVilla, "socks", "socks_2_2_0_color", kSocks)});
    w.g.write();
    return w;
}
// stages a goalkeeper kit for `team`; the pictures' data per role in `keep`
static sfs::path stage_gk(World& w, int team, const std::vector<Spec>& roles, std::map<std::string, std::vector<uint8_t>>* keep = nullptr) {
    const sfs::path dir = w.le / "turbo_output" / "kits" / std::to_string(team) / "goalkeeper";
    sfs::remove_all(dir);
    std::vector<kw::json> t;
    for (const Spec& s : roles) {
        std::vector<uint8_t> data;
        kw::stage_texture(dir, s.role, s.fmt, s.w, s.h, s.mips, t, w.rng, &data);
        if (keep) (*keep)[s.role] = data;
    }
    kw::write_manifest(dir, team, 2, t);
    return dir;
}
struct Blk {
    std::vector<uint8_t> src;
    uint32_t dsize = 0;
};
static Blk first_block(const World& w, size_t chunk) {
    const auto& c = w.g.chunk_locs[chunk];
    const std::vector<uint8_t> bytes(w.g.cas.begin() + std::ptrdiff_t(c[0]), w.g.cas.begin() + std::ptrdiff_t(c[0] + c[1]));
    std::vector<ks::Block> bl;
    ks::block_headers(bytes, bl);
    Blk b;
    if (bl.empty()) return b;
    b.src.assign(bytes.begin() + std::ptrdiff_t(bl[0].payload), bytes.begin() + std::ptrdiff_t(bl[0].payload + bl[0].csize));
    b.dsize = bl[0].dsize;
    return b;
}
static const ks::PlanItem* item(const ks::Plan& p, const std::string& leaf) {
    for (const auto& i : p.items)
        if (!i.probe && i.res.size() > leaf.size() && i.res.compare(i.res.size() - leaf.size(), leaf.size(), leaf) == 0) return &i;
    return nullptr;
}
static bool has_user(const std::vector<ks::KitUser>& v, int team, const std::string& kind) {
    for (const auto& u : v)
        if (u.team == team && u.kind == kind) return true;
    return false;
}
static std::string read_text(const sfs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static std::string names(int64_t id) { return id == kVilla ? "Aston Villa" : id == kSev ? "Sevilla FC" : "Team " + std::to_string(id); }

static void run() {
    const sfs::path base = test_tmp_dir("kit_share");
    std::error_code ec;
    sfs::remove_all(base, ec);
    World w = make_world(base);
    ks::GameFiles files(w.root);
    std::string err;
    ks::KitIndex idx;
    const bool ready = files.init(&err) && ks::scan_kits(files, kw::stand_in_oodle(), idx, nullptr, &err);
    ks::Options opt;
    opt.watch_club = false;
    std::map<std::string, std::vector<uint8_t>> sev;
    stage_gk(w, kSev, {kJersey, kJerseyN, kShorts, kSocks}, &sev);
    ks::StagedKit gk;
    ks::load_staged(w.le / "turbo_output" / "kits" / "481" / "goalkeeper", gk, &err);

    run_case("kit share: the option off: a goalkeeper kit whose shirt EA shares with another club stays the game's whole, the reason names that club", [&] {
        CHECK(ready && gk.textures.size() == 4, "synthetic game scanned, kit staged: " + err);
        ks::TableBuilder tb;
        const ks::Plan p = ks::build_plan(files, kw::stand_in_oodle(), idx, gk, opt, true, tb, nullptr);
        const ks::PlanItem *jc = item(p, "jersey_481_2_0_color"), *jn = item(p, "jersey_481_2_0_normal"), *sc = item(p, "shorts_481_2_0_color"), *so = item(p, "socks_481_2_0_color");
        CHECK(p.ok && jc && jn && sc && so, "plan with the four textures: " + p.error);
        if (!(jc && jn && sc && so)) return;
        CHECK(!jc->swap && jc->left_by == ks::LeftBy::OtherClubs && jc->reason.find("team 2 goalie_home_2_0") != std::string::npos && jc->reason.find("byte-identical") != std::string::npos,
              "the shirt picture: a byte-identical block in club 2's kit: left, naming it: " + jc->reason);
        CHECK(!jn->swap && jn->left_by == ks::LeftBy::OtherClubs && jn->reason.find("the chunk is also used by team 2 goalie_home_2_0") != std::string::npos,
              "the shirt normal map: club 2's very chunk: left, naming it: " + jn->reason);
        CHECK(!sc->swap && !so->swap && sc->reason.find("left with the rest of this kit (goalie_home_2_0)") != std::string::npos,
              "shorts and socks could be served but are not: never half a kit: " + sc->reason);
        CHECK(p.swapped == 0 && tb.items() == 0, fmt("nothing of the slot goes into the table (%d swapped, %u table items)", p.swapped, tb.items()));
        CHECK(p.folders.size() == 1 && !p.folders[0].swapped && p.folders[0].why == ks::LeftBy::OtherClubs && p.folders[0].role == "jersey_color" &&
                  has_user(p.folders[0].shared, kVilla, "goalie_home_2_0"),
              "one verdict for the folder: left, by the shirt, shared with club 2's GK kit");
        ks::Status st;
        st.folders = p.folders;
        const std::string line = turbo::kit_swap_status_line(st, kSev, 2, names);
        CHECK(line == "not swapped: the shirt is shared with Aston Villa's GK kit; tick the box above to swap both", "the slot's status in plain words: " + line);
        CHECK(turbo::kit_swap_status_line(st, kSev, 2, [](int64_t id) { return "Team " + std::to_string(id); }).find("Team 2's GK kit") != std::string::npos,
              "a club without a name in the model: its id");
        CHECK(turbo::kit_swap_status_line(st, kSev, 0, names).empty(), "another slot: no line");
    });

    run_case("kit share: the option on: every texture served, the notes name club 2's kit that changes too, the table serves club 2's blocks", [&] {
        ks::Options on = opt;
        on.share_other_clubs = true;
        ks::TableBuilder tb;
        const ks::Plan p = ks::build_plan(files, kw::stand_in_oodle(), idx, gk, on, true, tb, nullptr);
        const ks::PlanItem *jc = item(p, "jersey_481_2_0_color"), *jn = item(p, "jersey_481_2_0_normal"), *sc = item(p, "shorts_481_2_0_color"), *so = item(p, "socks_481_2_0_color");
        if (!(jc && jn && sc && so)) {
            CHECK(false, "plan items");
            return;
        }
        CHECK(jc->swap && jn->swap && sc->swap && so->swap && p.swapped == 4, fmt("all four served (%d)", p.swapped));
        CHECK(jc->reason.find("also changes team 2 goalie_home_2_0") != std::string::npos && jc->reason.find("3 of 3") != std::string::npos,
              "the shirt's note: club 2's kit changes too, and how much of it: " + jc->reason);
        CHECK(jn->reason.find("also changes team 2 goalie_home_2_0 (the same chunk)") != std::string::npos, "the normal map's note: " + jn->reason);
        CHECK(p.folders.size() == 1 && p.folders[0].swapped && has_user(p.folders[0].shared, kVilla, "goalie_home_2_0"), "verdict: swapped, also changes club 2's GK kit");
        ks::Status st;
        st.folders = p.folders;
        CHECK(turbo::kit_swap_status_line(st, kSev, 2, names) == "swapped (also changes Aston Villa's GK kit)", "status: " + turbo::kit_swap_status_line(st, kSev, 2, names));
        auto table = tb.finish(nullptr);
        const Blk villa = first_block(w, 4), own = first_block(w, 2);
        const ks::Entry* ve = table->match(villa.src.data(), villa.src.size(), villa.dsize);
        CHECK(ve && ve->data && std::memcmp(ve->data, sev["jersey_color"].data(), villa.dsize) == 0, "club 2's identical jersey block gets the zip's shirt (that kit changes too)");
        CHECK(table->match(own.src.data(), own.src.size(), own.dsize) != nullptr, "the shorts are in the table");
    });

    run_case("kit share: shorts that could be served with a shirt that cannot: the folder is never partly served (option off and on)", [&] {
        World w2 = make_world(base / "partial");
        ks::GameFiles f2(w2.root);
        ks::KitIndex i2;
        f2.init(nullptr);
        ks::scan_kits(f2, kw::stand_in_oodle(), i2, nullptr, nullptr);
        stage_gk(w2, kSev, {{"jersey_color", 55, 128, 128, 8}, kJerseyN, kShorts, kSocks});  // the shirt in another size than the game's
        ks::StagedKit k2;
        ks::load_staged(w2.le / "turbo_output" / "kits" / "481" / "goalkeeper", k2, nullptr);
        for (bool share : {false, true}) {
            ks::Options o = opt;
            o.share_other_clubs = share;
            ks::TableBuilder tb;
            const ks::Plan p = ks::build_plan(f2, kw::stand_in_oodle(), i2, k2, o, true, tb, nullptr);
            const ks::PlanItem *jc = item(p, "jersey_481_2_0_color"), *sc = item(p, "shorts_481_2_0_color");
            const std::string tag = share ? " (option on)" : " (option off)";
            CHECK(jc && !jc->swap && jc->reason.find("is not the game's") != std::string::npos, "the shirt: another size" + tag);
            CHECK(sc && !sc->swap && sc->table_item == 0xFFFFFFFFu && p.swapped == 0 && tb.items() == 0, "the shorts are not served either" + tag);
            CHECK(p.folders.size() == 1 && !p.folders[0].swapped && p.folders[0].why == ks::LeftBy::Other, "verdict: left, by the shirt" + tag);
            ks::Status st;
            st.folders = p.folders;
            const std::string line = turbo::kit_swap_status_line(st, kSev, 2, names);
            CHECK(line.find("not swapped (shirt): the staged texture") == 0, "status keeps the existing reason" + tag + ": " + line);
        }
    });

    run_case("kit share: Runtime: off leaves the whole kit and logs club 2; on serves it, logs 'also changes team 2 goalie_home_2_0'; the option is saved", [&] {
        ks::Runtime rt(w.le, w.root, kw::stand_in_oodle());
        rt.options.watch_club = false;
        rt.set_hook_installed(true, "");
        CHECK(rt.activate(kSev, 2, ks::Mode::Swap, &err), "activate the goalkeeper kit: " + err);
        rt.wait_idle();
        const Blk shorts = first_block(w, 2), villa = first_block(w, 4);
        std::vector<uint8_t> out(shorts.dsize, 0xEE);
        CHECK(rt.status().state == "ready" && rt.on_block(shorts.src.data(), shorts.src.size(), out.data(), shorts.dsize) == 0 && out[0] == 0xEE,
              "option off: the shorts block is not even recognised (the whole kit stays the game's): " + rt.status().message);
        rt.flush_log(true);
        std::string log = read_text(rt.log_path());
        CHECK(log.find("team 481 goalkeeper goalie_home_2_0: LEFT TO THE GAME: jersey_color") != std::string::npos && log.find("team 2 goalie_home_2_0") != std::string::npos &&
                  log.find("share with other clubs off") != std::string::npos,
              "the log says the kit is left and why");
        CHECK(rt.set_slot_options(kSev, 2, true, true, false, true), "tick 'Also change other clubs' kits that share this texture'");
        rt.wait_idle();
        std::vector<uint8_t> o2(shorts.dsize, 0xEE), o3(villa.dsize, 0xEE);
        CHECK(rt.on_block(shorts.src.data(), shorts.src.size(), o2.data(), shorts.dsize) == 2 && std::memcmp(o2.data(), sev["shorts_color"].data(), shorts.dsize) == 0,
              "option on: the zip's shorts");
        CHECK(rt.on_block(villa.src.data(), villa.src.size(), o3.data(), villa.dsize) == 2 && std::memcmp(o3.data(), sev["jersey_color"].data(), villa.dsize) == 0,
              "... and club 2's identical shirt block gets the zip's shirt too");
        rt.flush_log(true);
        log = read_text(rt.log_path());
        CHECK(log.find("team 481 goalkeeper goalie_home_2_0: swapped; also changes team 2 goalie_home_2_0") != std::string::npos &&
                  log.find("(also changes team 2 goalie_home_2_0 (byte-identical blocks") != std::string::npos && log.find("share with other clubs on") != std::string::npos,
              "the log names club 2's kit as changed");
        const ks::Status st = rt.status();
        CHECK(turbo::kit_swap_shared_clubs(st, kSev, 2, names) == "Aston Villa's GK kit", "the clubs named next to the checkbox: " + turbo::kit_swap_shared_clubs(st, kSev, 2, names));
        const std::string state = read_text(rt.state_path());
        CHECK(state.find("\"share_other_clubs\": true") != std::string::npos, "saved in swap_state.json as share_other_clubs");
        {
            ks::Runtime r2(w.le, w.root, kw::stand_in_oodle());
            r2.start();
            r2.wait_idle();
            bool d = false, k = false, sc = true, so = false;
            CHECK(r2.slot_options(kSev, 2, &d, &k, &sc, &so) && d && k && !sc && so, "a restart brings the option back");
            CHECK(r2.set_slot_options(kSev, 2, true, true, false, false) && r2.slot_options(kSev, 2, nullptr, nullptr, nullptr, &so) && !so, "and unticking saves it off");
            r2.wait_idle();
        }
        rt.deactivate(kSev, 2);
        rt.wait_idle();
        {   // a state file written before 2.13 (no share_other_clubs): off
            std::ofstream(w.le / "turbo_output" / "kits" / "swap_state.json")
                << R"({"version":2,"kits":[{"team":481,"slot":"goalkeeper","slot_type":2,"mode":"swap","decals":true,"keep_game_crest":true,"share_within_club":true}]})";
            ks::Runtime r3(w.le, w.root, kw::stand_in_oodle());
            r3.start();
            r3.wait_idle();
            bool sc = false, so = true;
            CHECK(r3.slot_options(kSev, 2, nullptr, nullptr, &sc, &so) && sc && !so, "a file without the key loads with the option off");
            r3.deactivate(kSev, 2);
            r3.wait_idle();
        }
    });

    run_case("kit share: two active kits that want the same game data with other pictures: both stay whole (never half), the line names the other kit", [&] {
        World w3 = make_world(base / "clash");
        std::map<std::string, std::vector<uint8_t>> a, b;
        stage_gk(w3, kSev, {kJersey, kJerseyN, kShorts, kSocks}, &a);
        stage_gk(w3, kVilla, {kJersey, kJerseyN, kShorts, kSocks}, &b);
        ks::Runtime rt(w3.le, w3.root, kw::stand_in_oodle());
        rt.options.watch_club = false;
        rt.set_hook_installed(true, "");
        CHECK(rt.activate(kSev, 2, ks::Mode::Swap, &err) && rt.activate(kVilla, 2, ks::Mode::Swap, &err), "both goalkeeper kits active: " + err);
        rt.wait_idle();
        CHECK(rt.set_slot_options(kSev, 2, true, true, false, true), "Sevilla: share with other clubs");
        rt.wait_idle();
        CHECK(rt.set_slot_options(kVilla, 2, true, true, false, true), "Villa: share with other clubs");
        rt.wait_idle();
        const ks::Status st = rt.status();
        int left = 0;
        for (const auto& f : st.folders) left += !f.swapped && f.why == ks::LeftBy::Clash;
        CHECK(st.state == "ready" && st.folders.size() == 2 && left == 2, fmt("both folders left by the clash (%d of %zu): ", left, st.folders.size()) + st.message);
        const Blk s1 = first_block(w3, 2), s2 = first_block(w3, 6), j = first_block(w3, 0);
        std::vector<uint8_t> o(65536, 0xEE);
        CHECK(rt.on_block(s1.src.data(), s1.src.size(), o.data(), s1.dsize) == 0 && rt.on_block(s2.src.data(), s2.src.size(), o.data(), s2.dsize) == 0 &&
                  rt.on_block(j.src.data(), j.src.size(), o.data(), j.dsize) == 0,
              "neither kit's shorts nor the shared shirt is in the table");
        const std::string line = turbo::kit_swap_status_line(st, kSev, 2, names);
        CHECK(line.find("not swapped: the shirt is made of the same data as Aston Villa's GK kit, which has its own imported kit") == 0, "status: " + line);
        rt.deactivate(kSev, 2);
        rt.deactivate(kVilla, 2);
        rt.wait_idle();
    });

    run_case("kit share: the table: a block three items want, two with one picture and one with another: all three go; exclude_item, then finish again", [&] {
        ks::TableBuilder b;
        std::vector<uint8_t> key(2048, 7);
        auto p1 = std::make_shared<std::vector<uint8_t>>(2048, 1), p2 = std::make_shared<std::vector<uint8_t>>(2048, 2);
        uint64_t head;
        std::memcpy(&head, key.data(), 8);
        const uint64_t f = ks::fnv1a64(key.data(), key.size());
        const uint32_t i1 = b.add_item(p1, true), i2 = b.add_item(p1, true), i3 = b.add_item(p2, true);
        b.add_block(i1, 2048, 2048, head, f, p1->data(), nullptr);
        b.add_block(i2, 2048, 2048, head, f, p1->data(), nullptr);
        b.add_block(i3, 2048, 2048, head, f, p2->data(), nullptr);
        std::set<uint32_t> dropped;
        std::map<uint32_t, std::set<uint32_t>> clash;
        auto t = b.finish(nullptr, &dropped, &clash);
        CHECK(dropped.size() == 3 && t->size() == 0 && clash[i3].count(i1) && clash[i3].count(i2), "all three dropped (none keeps a hole in its texture), each names the others");
        b.exclude_item(i3);
        t = b.finish(nullptr, &dropped, &clash);
        const ks::Entry* e = t->match(key.data(), key.size(), 2048);
        CHECK(dropped.empty() && e && e->data && e->data[0] == 1 && e->alias_n == 1, "without the third: one entry for the two items with the same picture");
    });
    sfs::remove_all(base, ec);
}

}  // namespace kit_share_test
