// Native tests: the bench probe (core/bench_probe.h): when it reads your club's sheet around Team Management, what it logs (only a change
// between the polls), the summary 15 s after Team Management closed, squad players missing from the sheet, failed reads. Read-only by design:
// the probe has no write path at all.
#pragma once

// 2.15 bench keep: the Keeper's decisions (core/bench_probe.h) on reads of your map sheet and Team Management's working copy (svc+0x550,
// BENCH_FINDINGS), and the probe's lines about the copies. The write itself (tsheet::apply_bench / submit_bench) is tested on the simulated
// sheet in test_team_sheet.h
namespace bench_keep_test {
using namespace turbo::bench;
inline Ids sheet(const std::vector<int32_t>& v) {
    Ids ids;
    ids.fill(-1);
    for (size_t k = 0; k < v.size() && k < ids.size(); ++k) ids[k] = v[k];
    return ids;
}
inline CopyView copy_of(const Ids& ids, uint64_t build, bool live = true) {
    CopyView c;
    c.live = live;
    c.build = build;
    c.ids = ids;
    c.label = "svc+0x550";
    return c;
}
struct Rig {
    Keeper k;
    Namer name = [](int32_t pid) { return "P" + std::to_string(pid); };
    std::vector<std::string> log;
    KeepOut last;
    long long gen = 1;
    double now = 100.0;
    int dirty = 1;
    KeepOut read(const Ids& map, const CopyView& copy, bool on = true, const std::string& why = "", bool may = true, const std::string& wait = "") {
        last = k.on_read(gen, map, dirty, copy, on, why, may, wait, now, name);
        log.insert(log.end(), last.log.begin(), last.log.end());
        return last;
    }
    bool logged(const std::string& part) const {
        for (const auto& l : log)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
    std::string tail() const { return log.empty() ? std::string() : log.back(); }
};
}  // namespace bench_keep_test

static void test_bench_keep() {
    using namespace turbo::bench;
    using bench_keep_test::copy_of;
    using bench_keep_test::Rig;
    using bench_keep_test::sheet;
    std::printf("-- bench keep: your saved bench order into Team Management's working copy (2.15)\n");
    std::vector<int32_t> base;
    for (int i = 1; i <= 25; ++i) base.push_back(i);
    const Ids M = sheet(base);  // your map sheet: the order you saved (XI 1..11, bench 12..18, reserves 19..25)
    std::vector<int32_t> s = base, s2 = base;
    std::swap(s[11], s[14]);  // the game's own order in the copy Team Management builds: 4 places
    std::swap(s[19], s[22]);
    std::swap(s2[15], s2[16]);
    const Ids S = sheet(s), S2 = sheet(s2);
    const CopyView none = copy_of(Ids{}, 0, false);

    // shuffle_of: a permutation of 11..51 only
    CHECK(shuffle_of(M, S).empty() && moved_places(M, S) == 4, "bench keep: the bench / reserves in another order is a shuffle (4 places)");
    CHECK(shuffle_of(M, M) == "same order", "bench keep: the same order is not a shuffle");
    {
        Ids x = S;
        std::swap(x[3], x[15]);
        CHECK(shuffle_of(M, x).find("the starting XI changed (#3 4 -> 16)") == 0, "bench keep: a moved starter is refused: " + shuffle_of(M, x));
        Ids y = S;
        y[24] = 99;
        const std::string w = shuffle_of(M, y);
        CHECK(w.find("other players on the sheet") == 0 && w.find("on now: 99") != std::string::npos && w.find("off now: 25") != std::string::npos,
              "bench keep: another player is refused: " + w);
        Ids e = M;
        e[25] = e[20];
        e[20] = -1;
        CHECK(shuffle_of(M, e) == "the empty places moved (#20)", "bench keep: a moved empty place is refused: " + shuffle_of(M, e));
        Ids d1 = M;
        d1[12] = 12;
        Ids d2 = d1;
        std::swap(d2[11], d2[14]);
        CHECK(shuffle_of(d1, d2) == "player 12 is on the sheet twice", "bench keep: a player twice is refused: " + shuffle_of(d1, d2));
    }

    // A: off by default; the first read learns the copy's state; Team Management opens = the copy is built in the game's order -> the write
    // is due in the same read; one write per build
    {
        Rig r;
        r.read(M, none, false);
        CHECK(r.k.phase() == KeepPhase::Off && r.k.state() == "off (no turbo_output\\bench_keep_on.txt)", "bench keep: off by default: " + r.k.state());
        r.now = 101.0;
        r.read(M, none);
        CHECK(r.k.phase() == KeepPhase::Idle && r.logged("bench keep: watching Team Management's working copy svc+0x550 (not built yet)") &&
                  r.k.state() == "on: waiting for Team Management to build its copy of your sheet",
              "bench keep: on, watching: " + r.k.state());
        r.now = 102.0;
        CHECK(!r.read(M, none).submit, "bench keep: nothing while the copy is not built");
        r.now = 110.0;
        const KeepOut o = r.read(M, copy_of(S, 0xB000));
        CHECK(o.submit && o.saved == M && o.before == S && o.build == 0xB000, "bench keep: a new build in the game's order: the write is due at once");
        CHECK(r.logged("bench keep: Team Management built its working copy svc+0x550 in another order than your saved one (4 places of the bench / reserves; "
                       "the starting XI and the players unchanged)") &&
                  r.logged("P12 #11->#14") && r.logged("Writing your saved order into it"),
              "bench keep: the build is logged with the moves: " + r.tail());
        CHECK(r.k.state() == "on: Team Management's copy has another order (4 places): writing your saved order into it", r.k.state());
        r.k.queued(110.0);
        CHECK(r.k.phase() == KeepPhase::Queued && r.k.state() == "on: writing your saved order into Team Management's copy ...", r.k.state());
        r.now = 110.5;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit, "bench keep: nothing more while the job is queued");
        r.k.on_result(true, "your saved order is in Team Management's working copy, places 11..51 (4 places written, read back)");
        CHECK(r.k.phase() == KeepPhase::Done &&
                  r.k.state() == "on: put back: your saved order is in Team Management's working copy, places 11..51 (4 places written, read back)",
              r.k.state());
        for (double t = 111.0; t < 140.0; t += 1.0) {
            r.now = t;
            CHECK(!r.read(M, copy_of(S2, 0xB000)).submit,
                  "bench keep: ONE write per build (the same build reordered again, not into the game's order: left alone; see I for the game's order)");
        }
        // Team Management closes (the copy is no longer your sheet's), opens again: a new build is written again
        r.now = 150.0;
        r.read(M, none);
        r.now = 160.0;
        const KeepOut o2 = r.read(M, copy_of(S, 0xC000));
        CHECK(o2.submit && o2.build == 0xC000, "bench keep: the next opening (a new build) is written again");
        // a new build without the copy going away first (its records were allocated again)
        r.k.queued(160.0);
        r.k.on_result(true, "ok");
        r.now = 170.0;
        CHECK(r.read(M, copy_of(S, 0xD000)).submit, "bench keep: a new record vector is a new build");
    }
    // B: the copy exists at first sight (P_BENCHWB 16:20: the game builds it DURING the career load and the first Team Management visit
    // shows it without building it again): handled as a new build, written once the map has been quiet for 5 s; then the next build too
    {
        Rig r;
        r.read(M, copy_of(S, 0xB000));
        CHECK(!r.last.submit && r.k.phase() == KeepPhase::Pending &&
                  r.logged("bench keep: watching Team Management's working copy svc+0x550 (built already, e.g. during the career load: handled as a new build)") &&
                  r.logged("bench keep: Team Management built its working copy svc+0x550 in another order than your saved one (4 places") &&
                  r.logged("bench keep: the write waits (your map sheet has not been quiet for 5 s yet (a load, a commit or a save may be running))"),
              "bench keep: a copy built before bench keep looked: armed, waiting for the map's quiet 5 s: " + r.tail());
        r.now = 104.0;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit, "bench keep: 4 s: still waiting");
        r.now = 105.0;
        const KeepOut o = r.read(M, copy_of(S, 0xB000));
        CHECK(o.submit && o.saved == M && o.before == S && o.build == 0xB000, "bench keep: 5 s after the first read: the copy built at the load is written");
        r.k.queued(105.0);
        r.k.on_result(true, "ok");
        r.now = 106.0;
        CHECK(!r.read(M, copy_of(S2, 0xB000)).submit, "bench keep: still one write per build");
        r.now = 107.0;
        r.read(M, none);
        r.now = 108.0;
        CHECK(r.read(M, copy_of(S, 0xB800)).submit, "bench keep: the next build is written");
        // the map changes while the first-sight write waits (a load still settling): judged again, the write uses the new map
        Rig r3;
        r3.read(M, copy_of(S, 0xB000));
        r3.now = 102.0;
        CHECK(!r3.read(S2, copy_of(S, 0xB000)).submit && r3.logged("bench keep: your map sheet changed before the write: the write now uses its order"),
              "bench keep: the map changed while waiting: re-judged");
        r3.now = 107.0;
        const KeepOut o3 = r3.read(S2, copy_of(S, 0xB000));
        CHECK(o3.submit && o3.saved == S2 && o3.before == S, "bench keep: written with the new map's order once it has been quiet 5 s");
        // a copy at first sight that already holds your order: nothing to write
        Rig r2;
        r2.read(M, copy_of(M, 0xB000));
        CHECK(!r2.last.submit && r2.k.phase() == KeepPhase::Done && r2.logged("holds your saved order: nothing to write"), "bench keep: first sight, your order");
    }
    // C: the copy already holds your order; the copy is not your players in another order
    {
        Rig r;
        r.read(M, none);
        r.now = 101.0;
        CHECK(!r.read(M, copy_of(M, 0xB000)).submit && r.k.phase() == KeepPhase::Done &&
                  r.logged("bench keep: Team Management's working copy holds your saved order: nothing to write"),
              "bench keep: the copy in your order: nothing to write");
        Ids x = S;
        std::swap(x[3], x[15]);
        r.now = 102.0;
        r.read(M, none);
        r.now = 103.0;
        CHECK(!r.read(M, copy_of(x, 0xC000)).submit && r.k.phase() == KeepPhase::Done &&
                  r.logged("left alone: Team Management's working copy is not your saved players in another order (the starting XI changed"),
              "bench keep: another starting XI: left alone");
    }
    // D: G2 for bench keep does not hold (a save started 10 s ago): the write waits, logged once, then goes when it holds; or is given up
    {
        Rig r;
        r.read(M, none);
        r.now = 110.0;
        CHECK(!r.read(M, copy_of(S, 0xB000), true, "", false, "a save started 10 s ago").submit, "bench keep: waits while G2 does not hold");
        r.now = 111.0;
        r.read(M, copy_of(S, 0xB000), true, "", false, "a save started 11 s ago");
        r.now = 111.5;
        r.read(M, copy_of(S, 0xB000), true, "", false, "a save started 11 s ago");
        int waits = 0;
        for (const auto& l : r.log) waits += l.find("bench keep: the write waits (") == 0 ? 1 : 0;
        CHECK(waits == 2 && r.logged("bench keep: the write waits (a save started 10 s ago): until 20 s after the build"), "bench keep: each new reason once");
        r.now = 115.0;
        CHECK(r.read(M, copy_of(S, 0xB000)).submit, "bench keep: G2 holds again within 20 s: the write goes");
        Rig r2;
        r2.read(M, none);
        r2.now = 110.0;
        r2.read(M, copy_of(S, 0xB000), true, "", false, "Lua's phase is prematch, not the hub");
        r2.now = 131.0;
        CHECK(!r2.read(M, copy_of(S, 0xB000), true, "", false, "Lua's phase is prematch, not the hub").submit && r2.k.phase() == KeepPhase::Done &&
                  r2.logged("bench keep: not written: 20 s after the build the write was still waiting (Lua's phase is prematch, not the hub): given up "
                            "(nothing written)"),
              "bench keep: given up 20 s after the build: " + r2.tail());
    }
    // E: dropped before the write: the copy changed (a move on the screen), the copy went away, the switch, a reload
    {
        Rig r;
        r.read(M, none);
        r.now = 110.0;
        r.read(M, copy_of(S, 0xB000), true, "", false, "a save started 5 s ago");
        r.now = 111.0;
        CHECK(!r.read(M, copy_of(S2, 0xB000), true, "", true).submit && r.k.phase() == KeepPhase::Done &&
                  r.logged("bench keep: left alone: the working copy changed before the write (a move on the screen?) (nothing written)"),
              "bench keep: the copy moved first: left alone");
        Rig r2;
        r2.read(M, none);
        r2.now = 110.0;
        r2.read(M, copy_of(S, 0xB000), true, "", false, "w");
        r2.now = 111.0;
        r2.read(M, none);
        CHECK(r2.k.phase() == KeepPhase::Done && r2.logged("not written: the working copy went away before the write (Team Management closed?)"),
              "bench keep: the copy gone: dropped");
        Rig r3;
        r3.read(M, none);
        r3.now = 110.0;
        r3.read(M, copy_of(S, 0xB000), true, "", false, "w");
        r3.now = 111.0;
        r3.read(M, copy_of(S, 0xB000), false, "kill switch turbo_output\\bench_keep_off.txt is present");
        CHECK(r3.k.phase() == KeepPhase::Off && !r3.last.submit &&
                  r3.logged("bench keep: switched off (kill switch turbo_output\\bench_keep_off.txt is present): the write is dropped (nothing written)") &&
                  r3.k.state() == "off: kill switch turbo_output\\bench_keep_off.txt is present",
              "bench keep: the kill switch drops it: " + r3.k.state());
        r3.now = 112.0;
        r3.read(M, copy_of(S, 0xB000));
        CHECK(!r3.last.submit && r3.k.phase() == KeepPhase::Pending && r3.logged("(built already, e.g. during the career load: handled as a new build)"),
              "bench keep: switched on again with the copy built: armed, the map's quiet gate starts again");
        r3.now = 117.0;
        CHECK(r3.read(M, copy_of(S, 0xB000)).submit, "bench keep: switched on again: written 5 s later");
        Rig r4;
        r4.read(M, none);
        r4.now = 110.0;
        r4.read(M, copy_of(S, 0xB000), true, "", false, "w");
        r4.gen = 2;
        r4.now = 111.0;
        r4.read(M, copy_of(S, 0xB000));
        CHECK(!r4.last.submit && r4.logged("bench keep: the career was (re)loaded: the write is dropped (nothing written)"), "bench keep: a reload drops it");
    }
    // F: a refused submit is tried again every second and logged once per reason; a queued job that never comes back is given up
    {
        Rig r;
        r.read(M, none);
        r.now = 110.0;
        CHECK(r.read(M, copy_of(S, 0xB000)).submit, "due");
        std::vector<std::string> l;
        r.k.not_queued("a channel E job is still queued or running", l);
        r.k.not_queued("a channel E job is still queued or running", l);
        CHECK(l.size() == 1 && l[0].find("bench keep: the write is not queued yet (a channel E job is still queued or running): tried again until 20 s after") == 0,
              "bench keep: a refusal is logged once");
        r.now = 110.5;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit, "bench keep: not every read");
        r.now = 111.0;
        CHECK(r.read(M, copy_of(S, 0xB000)).submit, "bench keep: tried again a second later");
        r.k.queued(111.0);
        r.now = 127.0;
        r.read(M, copy_of(S, 0xB000));
        CHECK(r.k.phase() == KeepPhase::Done && r.logged("bench keep: not written: the game thread did not run the job (nothing written by it)"),
              "bench keep: a lost job is given up");
    }
    // G: the probe's lines about the copies: built (against your map sheet), changed while built, gone
    {
        Probe p;
        const Namer name = [](int32_t pid) { return "P" + std::to_string(pid); };
        CopyView other = copy_of(S, 0xA000);
        other.label = "svc+0xA60";
        std::vector<std::string> l = p.on_copies(M, 1, {none, copy_of(Ids{}, 0, false)}, name);
        CHECK(l.empty(), "bench probe: nothing while no copy is built");
        l = p.on_copies(M, 1, {copy_of(S, 0xB000), other}, name);
        CHECK(l.size() == 2 && l[0].find("bench probe: Team Management's working copy svc+0x550 built [records 0xB000]: against your map sheet (your saved order): "
                                         "4 of 25 places changed") == 0 &&
                  l[0].find("| 25 players: #0 P1") != std::string::npos && l[1].find("svc+0xA60 built [records 0xA000]") != std::string::npos,
              "bench probe: both copies' builds are logged with their order against the map: " + (l.empty() ? std::string() : l[0]));
        CHECK(p.on_copies(M, 1, {copy_of(S, 0xB000), other}, name).empty(), "bench probe: an unchanged copy logs nothing");
        l = p.on_copies(M, 1, {copy_of(M, 0xB000), other}, name);
        CHECK(l.size() == 1 && l[0].find("bench probe: Team Management's working copy svc+0x550 changed: 4 of 25 places changed") == 0 &&
                  l[0].find("against your map sheet: same order") != std::string::npos,
              "bench probe: a change of the built copy (Turbo's write, a move on the screen) is logged: " + (l.empty() ? std::string() : l[0]));
        l = p.on_copies(M, 1, {none, other}, name);
        CHECK(l.size() == 1 && l[0] == "bench probe: Team Management's working copy svc+0x550 is no longer your sheet's (Team Management closed?)",
              "bench probe: the copy going away is logged");
        // your map sheet: a save clears its dirty flag, a commit changes its ids (Lua's save signal is blind on LE 27.1.5)
        l = p.on_copies(M, 0, {none, other}, name);
        CHECK(l.size() == 1 && l[0].find("bench probe: your map sheet changed [dirty 1 -> 0]: same order | 25 players: #0 P1") == 0,
              "bench probe: the map's dirty flag cleared (a save) is logged: " + (l.empty() ? std::string() : l[0]));
        l = p.on_copies(S, 1, {none, other}, name);
        CHECK(l.size() == 1 && l[0].find("bench probe: your map sheet changed [dirty 0 -> 1]: 4 of 25 places changed") == 0,
              "bench probe: the map's order changed (a commit) is logged: " + (l.empty() ? std::string() : l[0]));
    }
    // H: the write waits until your map sheet has been quiet for 5 s (a commit or a save may be running: Lua's save signal is blind)
    {
        Rig r;
        r.read(M, none);
        r.now = 110.0;
        r.dirty = 0;  // the save after a Yes cleared the map's dirty flag
        r.read(M, none);
        r.now = 112.0;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit &&
                  r.logged("bench keep: the write waits (your map sheet has not been quiet for 5 s yet (a load, a commit or a save may be running)): until 20 s after "
                           "the build"),
              "bench keep: Team Management opened 2 s after the map changed: the write waits: " + r.tail());
        r.now = 114.0;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit, "bench keep: 4 s: still waiting");
        r.now = 115.0;
        CHECK(r.read(M, copy_of(S, 0xB000)).submit, "bench keep: 5 s after the map's last change: the write goes");
    }
    // I: the same build put back into the game's order IN PLACE after the write (P215c 19:00:33 written and read back, 19:01:08 the first
    // Team Management entry: "working copy svc+0x550 changed", same record vector, exactly the order the game built). Written again like a
    // new build; never after a commit or a save; never for your own move on the screen; at most kKeepMaxRewrites per build (no loop)
    auto count = [](const bench_keep_test::Rig& r, const std::string& part) {
        int n = 0;
        for (const auto& l : r.log) n += l.find(part) != std::string::npos ? 1 : 0;
        return n;
    };
    const std::string kPutBack = "bench keep: the game put its own order back into Team Management's working copy svc+0x550 in place";
    // I1: the P215c flow: the copy built during the career load is written, holds; 44 s after the build Team Management is entered and the
    // game puts its own order back: written again at once (the deadline counts from the re-sort, not from the build), once
    {
        Rig r;
        r.read(M, copy_of(S, 0xB000));  // the career load: built already, waits for the map's quiet 5 s
        r.now = 105.0;
        const KeepOut o = r.read(M, copy_of(S, 0xB000));
        CHECK(o.submit && o.before == S, "bench keep in place: the load-built copy is written");
        r.k.queued(105.0);
        r.k.on_result(true, "ok");
        r.now = 105.5;
        CHECK(!r.read(M, copy_of(M, 0xB000)).submit && r.k.phase() == KeepPhase::Done, "bench keep in place: the write held (the next read sees your order)");
        const size_t logged0 = r.log.size();
        for (double t = 106.0; t < 149.0; t += 0.5) {
            r.now = t;
            CHECK(!r.read(M, copy_of(M, 0xB000)).submit, "bench keep in place: nothing while the copy holds your order");
        }
        CHECK(r.log.size() == logged0, "bench keep in place: nothing logged while the copy holds your order");
        r.now = 149.0;  // the first Team Management entry: the same build back in the game's order
        const KeepOut o2 = r.read(M, copy_of(S, 0xB000));
        CHECK(o2.submit && o2.saved == M && o2.before == S && o2.build == 0xB000,
              "bench keep in place: the game's order put back into the same build: written again at once (44 s after the build): " + r.tail());
        CHECK(r.logged(kPutBack + " (the same build; your map sheet unchanged since the write: no commit, no save): 4 of 25 places changed") &&
                  r.logged("P12 #11->#14") && r.logged("Writing your saved order into it again (rewrite 1 of at most 2 for this build)"),
              "bench keep in place: the re-sort is logged with the moves: " + r.tail());
        CHECK(r.k.phase() == KeepPhase::Pending && r.k.state() == "on: Team Management's copy has another order (4 places): writing your saved order into it",
              r.k.state());
        r.k.queued(149.0);
        r.now = 149.2;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit, "bench keep in place: one write per change (nothing more while it is queued)");
        r.k.on_result(true, "your saved order is in Team Management's working copy (again)");
        for (double t = 149.5; t < 210.0; t += 0.5) {
            r.now = t;
            CHECK(!r.read(M, copy_of(M, 0xB000)).submit, "bench keep in place: the rewrite held: nothing more");
        }
        CHECK(count(r, kPutBack) == 1 && r.k.state() == "on: put back: your saved order is in Team Management's working copy (again)",
              "bench keep in place: one rewrite for one re-sort: " + r.k.state());
        // the same, but G2 does not hold at the re-sort: given up 20 s after the RE-SORT (not after the build, 44 s earlier)
        Rig q;
        q.read(M, copy_of(S, 0xB000));
        q.now = 105.0;
        q.read(M, copy_of(S, 0xB000));
        q.k.queued(105.0);
        q.k.on_result(true, "ok");
        q.now = 105.5;
        q.read(M, copy_of(M, 0xB000));
        q.now = 149.0;
        CHECK(!q.read(M, copy_of(S, 0xB000), true, "", false, "Lua's phase is prematch, not the hub").submit && q.k.phase() == KeepPhase::Pending &&
                  q.logged("bench keep: the write waits (Lua's phase is prematch, not the hub): until 20 s after the build"),
              "bench keep in place: the rewrite waits for G2: " + q.tail());
        q.now = 160.0;
        CHECK(!q.read(M, copy_of(S, 0xB000), true, "", false, "Lua's phase is prematch, not the hub").submit && q.k.phase() == KeepPhase::Pending,
              "bench keep in place: 11 s after the re-sort (55 s after the build): still waiting");
        q.now = 165.0;
        CHECK(q.read(M, copy_of(S, 0xB000)).submit, "bench keep in place: G2 holds 16 s after the re-sort: the rewrite goes");
        q.k.queued(165.0);
        q.k.on_result(false, "refused (changed): ...");
        q.now = 165.5;
        q.read(M, copy_of(S, 0xB000));
        q.now = 170.0;
        CHECK(!q.read(M, copy_of(M, 0xB000)).submit && !q.read(M, copy_of(S, 0xB000)).submit && q.k.phase() == KeepPhase::Done,
              "bench keep in place: a refused rewrite is not tried again in this build");
    }
    // I2: no rewrite when your map sheet changed since the write: a commit (the Yes put the game's order into the map), a save (dirty -> 0)
    {
        auto written = [&](Rig& r) {
            r.read(M, none);
            r.now = 110.0;
            r.read(M, copy_of(S, 0xB000));
            r.k.queued(110.0);
            r.k.on_result(true, "ok");
            r.now = 110.5;
            r.read(M, copy_of(M, 0xB000));
        };
        Rig r;
        written(r);
        r.now = 140.0;  // the game put its order back and the Yes committed it, between two reads: map = copy = the game's order
        CHECK(!r.read(S, copy_of(S, 0xB000)).submit &&
                  r.logged(std::string("bench keep: the game put its own order back into Team Management's working copy svc+0x550, but your map sheet ") +
                           "changed since the write (a commit or a save): left alone (nothing written)"),
              "bench keep in place: a commit: no rewrite: " + r.tail());
        for (double t = 140.5; t < 170.0; t += 0.5) {
            r.now = t;
            CHECK(!r.read(S, copy_of(S, 0xB000)).submit, "bench keep in place: after the commit: nothing");
        }
        CHECK(count(r, "your map sheet changed since the write") == 1 && count(r, kPutBack) == 0, "bench keep in place: the commit is logged once");
        Rig r2;  // the map's ids changed (a commit of your own order with a move), then the game puts its order back in place
        written(r2);
        r2.now = 140.0;
        r2.read(S2, copy_of(M, 0xB000));
        r2.now = 150.0;
        CHECK(!r2.read(S2, copy_of(S, 0xB000)).submit && r2.logged("your map sheet changed since the write (a commit or a save): left alone"),
              "bench keep in place: the map's order changed since the write: no rewrite");
        Rig r3;  // a save: the map's ids unchanged, its dirty flag 1 -> 0
        written(r3);
        r3.now = 140.0;
        r3.dirty = 0;
        r3.read(M, copy_of(M, 0xB000));
        r3.now = 150.0;
        CHECK(!r3.read(M, copy_of(S, 0xB000)).submit && r3.logged("your map sheet changed since the write (a commit or a save): left alone"),
              "bench keep in place: a save since the write: no rewrite");
    }
    // I3: no ping-pong: the game puts its order back after every write: 2 rewrites (3 writes in all), then given up for this build; a new
    // build starts again
    {
        Rig r;
        r.read(M, none);
        r.now = 110.0;
        CHECK(r.read(M, copy_of(S, 0xB000)).submit, "bench keep loop: the build is written");
        r.k.queued(r.now);
        r.k.on_result(true, "ok");
        int writes = 1;
        for (int i = 0; i < 60; ++i) {
            r.now += 0.5;
            if (r.read(M, copy_of(M, 0xB000)).submit) ++writes;  // the write landed
            r.now += 0.5;
            if (r.read(M, copy_of(S, 0xB000)).submit) {  // the game puts its order back
                ++writes;
                r.k.queued(r.now);
                r.k.on_result(true, "ok");
            }
        }
        CHECK(writes == 1 + kKeepMaxRewrites && count(r, kPutBack) == kKeepMaxRewrites,
              "bench keep loop: at most " + std::to_string(kKeepMaxRewrites) + " rewrites per build: " + std::to_string(writes) + " writes");
        CHECK(count(r, "bench keep: given up for this build: the game put its own order back into Team Management's working copy again after 2 rewrites "
                       "(no loop with the game) (nothing written)") == 1 &&
                  r.k.state() == "on: given up for this build: the game put its own order back into Team Management's working copy again after 2 rewrites "
                                 "(no loop with the game)",
              "bench keep loop: given up once: " + r.k.state());
        r.now += 1.0;
        r.read(M, none);  // Team Management left: the next visit is a new build, written again, with its own rewrites
        r.now += 1.0;
        CHECK(r.read(M, copy_of(S, 0xC000)).submit, "bench keep loop: a new build is written again");
        r.k.queued(r.now);
        r.k.on_result(true, "ok");
        r.now += 0.5;
        r.read(M, copy_of(M, 0xC000));
        r.now += 0.5;
        CHECK(r.read(M, copy_of(S, 0xC000)).submit && count(r, "(rewrite 1 of at most 2 for this build)") == 2, "bench keep loop: a new build has its own rewrites");
        // the game puts its order back before any read saw the write hold: left alone (no loop), logged once
        Rig r2;
        r2.read(M, none);
        r2.now = 110.0;
        r2.read(M, copy_of(S, 0xB000));
        r2.k.queued(110.0);
        r2.k.on_result(true, "ok");
        for (double t = 110.5; t < 140.0; t += 0.5) {
            r2.now = t;
            CHECK(!r2.read(M, copy_of(S, 0xB000)).submit, "bench keep loop: the write did not hold until the next read: no rewrite");
        }
        CHECK(count(r2, "bench keep: left alone: Team Management's working copy svc+0x550 no longer held your saved order at the next read (changed right "
                        "after the write: no loop with the game): 4 of 25 places changed") == 1 &&
                  count(r2, kPutBack) == 0,
              "bench keep loop: did not hold: logged once: " + r2.tail());
    }
    // I4: your own move on the screen after the write (two substitutes exchanged) is never undone; a later re-sort by the game still is
    {
        Rig r;
        r.read(M, none);
        r.now = 110.0;
        r.read(M, copy_of(S, 0xB000));
        r.k.queued(110.0);
        r.k.on_result(true, "ok");
        r.now = 110.5;
        r.read(M, copy_of(M, 0xB000));
        r.now = 130.0;
        CHECK(!r.read(M, copy_of(S2, 0xB000)).submit &&
                  r.logged("bench keep: Team Management's working copy svc+0x550 changed in place to an order that is not the game's own (a move on the "
                           "screen?): left alone (nothing written): 2 of 25 places changed: P16 #15->#16, P17 #16->#15"),
              "bench keep in place: your swap is left alone: " + r.tail());
        for (double t = 130.5; t < 190.0; t += 0.5) {
            r.now = t;
            CHECK(!r.read(M, copy_of(S2, 0xB000)).submit, "bench keep in place: your swap stays");
        }
        CHECK(count(r, "a move on the screen?): left alone") == 1 && count(r, kPutBack) == 0, "bench keep in place: your swap is logged once, never undone");
        std::vector<int32_t> s3 = s2;  // another move of yours on top
        std::swap(s3[20], s3[21]);
        r.now = 190.0;
        CHECK(!r.read(M, copy_of(sheet(s3), 0xB000)).submit && count(r, "a move on the screen?): left alone") == 2, "bench keep in place: each move once");
        r.now = 200.0;  // the game puts its own order back over your unsaved moves (map unchanged): your saved order is written
        CHECK(r.read(M, copy_of(S, 0xB000)).submit && count(r, kPutBack) == 1, "bench keep in place: the game's order put back later: written again");
    }
}

static void test_bench_probe() {
    using namespace turbo::bench;
    std::printf("-- bench probe: your club's sheet order around Team Management (read-only)\n");
    const Namer name = [](int32_t pid) { return "P" + std::to_string(pid); };
    auto sheet = [](std::vector<int32_t> v) {
        Ids ids;
        ids.fill(-1);
        for (size_t k = 0; k < v.size() && k < ids.size(); ++k) ids[k] = v[k];
        return ids;
    };
    const Ids a = sheet({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
    const Ids b = sheet({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 12, 13, 16, 15});

    // the texts
    CHECK(diff_text(a, a, name) == "same order", "bench probe: an unchanged sheet is 'same order'");
    {
        const std::string d = diff_text(a, b, name);
        CHECK(d.find("5 of 16 places changed") != std::string::npos && d.find("P14 #13->#11") != std::string::npos &&
                  d.find("P12 #11->#12") != std::string::npos && d.find("P1 ") == std::string::npos,
              "bench probe: the diff names each moved player with his old and new index, and only those: " + d);
        const std::string j = diff_text(a, sheet({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17}), name);
        CHECK(j.find("nobody else moved; on the sheet now: P17") == 0 && j.find("off the sheet now: P16") != std::string::npos,
              "bench probe: a player who came on / went off the sheet is named: " + j);
    }
    {
        const std::string o = order_text(sheet({7, -1, 9}), name);
        CHECK(o == "2 players: #0 P7, #1 (empty), #2 P9", "bench probe: the order text keeps the game's index and empty slots: " + o);
    }
    CHECK(missing({1, 2, 99}, a) == std::vector<int32_t>{99} && extra({1, 2, 3}, sheet({1, 2, 3, 50})) == std::vector<int32_t>{50},
          "bench probe: squad players off the sheet and sheet players outside the squad");

    // the schedule
    Probe p;
    Input in;
    in.career = false;
    CHECK(p.step(in).kind == Kind::None, "bench probe: nothing outside a career");
    in.career = true;
    in.load_gen = 4;
    in.phase = "hub";
    in.save_t = 1000;
    in.now = 100.0;
    Due d = p.step(in);
    CHECK(d.kind == Kind::Load && d.always, "bench probe: one read in the hub of each career load");
    std::vector<std::string> lines = p.on_read(d, a, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 99}, name, "team 95, sheet -1, dirty 1");
    CHECK(lines.size() == 2 && lines[0].find("bench probe: career hub") == 0 && lines[0].find("[team 95, sheet -1, dirty 1]") != std::string::npos &&
              lines[1].find("1 squad player(s) not on the sheet: P99") != std::string::npos,
          "bench probe: the load read logs the order and the squad player missing from the sheet");
    in.now = 101.0;
    CHECK(p.step(in).kind == Kind::None, "bench probe: nothing more in the hub while nothing happens");

    in.team_mgmt = true;
    in.now = 110.0;
    d = p.step(in);
    CHECK(d.kind == Kind::Opened && d.label == "Team Management opened", "bench probe: a read when Team Management opens");
    lines = p.on_read(d, a, {}, name);
    CHECK(lines.size() == 1 && lines[0].find("same order as the last read") != std::string::npos, "bench probe: the open read is always logged");
    in.now = 110.5;
    CHECK(p.step(in).kind == Kind::None, "bench probe: at most one read a second while open");
    in.now = 111.0;
    d = p.step(in);
    CHECK(d.kind == Kind::Open && !d.always, "bench probe: a poll a second while Team Management is open");
    CHECK(p.on_read(d, a, {}, name).empty(), "bench probe: an unchanged poll logs nothing");
    in.now = 112.0;
    d = p.step(in);
    lines = p.on_read(d, b, {}, name);  // the user reorders on the screen and the sheet follows
    CHECK(lines.size() == 1 && lines[0].find("2 s after Team Management opened") != std::string::npos && lines[0].find("CHANGED") != std::string::npos,
          "bench probe: a poll that sees another order logs it with the time since Team Management opened");

    in.save_t = 1050;  // the game's Yes autosaves while Team Management is still open
    in.now = 112.2;
    d = p.step(in);
    CHECK(d.kind == Kind::Save && d.label == "save started (Team Management open)", "bench probe: a read when a save starts");
    p.on_read(d, b, {}, name);

    in.team_mgmt = false;
    in.now = 120.0;
    d = p.step(in);
    CHECK(d.kind == Kind::Closed && d.label == "Team Management closed (open 10 s)", "bench probe: a read when Team Management closes");
    p.on_read(d, b, {}, name);
    in.now = 121.0;
    d = p.step(in);
    CHECK(d.kind == Kind::After && d.label == "1 s after Team Management closed", "bench probe: polls after Team Management closed");
    lines = p.on_read(d, a, {}, name);  // the game puts the old order back after the leaving
    CHECK(lines.size() == 1 && lines[0].find("CHANGED") != std::string::npos && lines[0].find("P14 #11->#13") != std::string::npos,
          "bench probe: the shuffle after leaving is logged with the moves");
    for (double t = 122.0; t < 135.0; t += 1.0) {
        in.now = t;
        d = p.step(in);
        CHECK(d.kind == Kind::After, "bench probe: a poll a second for 15 s after closing");
        CHECK(p.on_read(d, a, {}, name).empty(), "bench probe: unchanged polls after closing log nothing");
    }
    in.now = 135.0;
    d = p.step(in);
    CHECK(d.kind == Kind::Final && d.always, "bench probe: the last read 15 s after Team Management closed");
    lines = p.on_read(d, a, {}, name);
    CHECK(lines.size() == 2 && lines[1].find("bench probe: summary: opened -> closed: 5 of 16 places changed") == 0 &&
              lines[1].find("closed -> 15 s after Team Management closed: 5 of 16 places changed") != std::string::npos &&
              lines[1].find("opened -> now: same order") != std::string::npos,
          "bench probe: the summary says what Team Management changed and what came after it: " + (lines.size() > 1 ? lines[1] : std::string()));
    in.now = 140.0;
    CHECK(p.step(in).kind == Kind::None, "bench probe: quiet again after the summary");

    // a new career load starts over; a failed load read is tried again 5 s later; at most 3 failures are logged per load
    in.load_gen = 5;
    in.now = 200.0;
    d = p.step(in);
    CHECK(d.kind == Kind::Load, "bench probe: a new career load reads again");
    CHECK(p.on_fail(d, "locating ...").find("career hub (the sheet as loaded): not read: locating ...") != std::string::npos,
          "bench probe: a failed read says why");
    in.now = 202.0;
    CHECK(p.step(in).kind == Kind::None, "bench probe: no read every frame after a failure");
    in.now = 205.0;
    d = p.step(in);
    CHECK(d.kind == Kind::Load, "bench probe: the load read is tried again 5 s later");
    p.on_fail(d, "locating ...");
    in.now = 210.0;
    d = p.step(in);
    const std::string third = p.on_fail(d, "locating ...");
    in.now = 215.0;
    d = p.step(in);
    CHECK(third.find("further failures in this career load are not logged") != std::string::npos && p.on_fail(d, "locating ...").empty(),
          "bench probe: at most 3 failures logged per career load");
    // leaving the career mid-watch
    in.career = false;
    CHECK(p.step(in).kind == Kind::None, "bench probe: nothing once the career is left");
    // P_BENCHWB 16:20: Team Management's copy is built DURING the load, so Team Management looks "open" from the first read of a load on;
    // the career load's read still comes (it logs the map's order, the live check's C' reference)
    {
        Probe q;
        Input l;
        l.career = true;
        l.load_gen = 7;
        l.phase = "hub";
        l.team_mgmt = true;
        l.now = 300.0;
        Due e = q.step(l);
        CHECK(e.kind == Kind::Opened, "bench probe: the copy built at the load reads as Team Management opened");
        q.on_fail(e, "locating ...");
        l.now = 300.5;
        e = q.step(l);
        CHECK(e.kind == Kind::Load && e.label == "career hub (the sheet as loaded)", "bench probe: the career load's read comes while the copy is built");
        q.on_fail(e, "locating ...");
        l.now = 306.0;
        CHECK(q.step(l).kind == Kind::Load, "bench probe: a failed load read is tried again 5 s later, Team Management open or not");
    }
    test_bench_keep();
}
