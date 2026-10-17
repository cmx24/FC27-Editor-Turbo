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
    KeepOut read(const Ids& map, const CopyView& copy, bool on = true, const std::string& why = "", bool may = true, const std::string& wait = "") {
        last = k.on_read(gen, map, copy, on, why, may, wait, now, name);
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
        CHECK(r.k.phase() == KeepPhase::Idle && r.logged("bench keep: watching Team Management's working copy svc+0x550 (not built: Team Management closed)") &&
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
            CHECK(!r.read(M, copy_of(S2, 0xB000)).submit, "bench keep: ONE write per build (the same build reordered again: left alone)");
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
    // B: a copy already built at the first read is left alone; the next build is written
    {
        Rig r;
        r.read(M, copy_of(S, 0xB000));
        CHECK(!r.last.submit && r.logged("(built already: left alone, the next build is written)"), "bench keep: a copy built before bench keep looked is left alone");
        r.now = 101.0;
        CHECK(!r.read(M, copy_of(S, 0xB000)).submit, "bench keep: still the same build");
        r.now = 102.0;
        r.read(M, none);
        r.now = 103.0;
        CHECK(r.read(M, copy_of(S, 0xB800)).submit, "bench keep: the next build is written");
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
        CHECK(!r3.last.submit && r3.logged("(built already: left alone"), "bench keep: switched on again: the copy's state is learnt first");
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
        std::vector<std::string> l = p.on_copies(M, {none, copy_of(Ids{}, 0, false)}, name);
        CHECK(l.empty(), "bench probe: nothing while no copy is built");
        l = p.on_copies(M, {copy_of(S, 0xB000), other}, name);
        CHECK(l.size() == 2 && l[0].find("bench probe: Team Management's working copy svc+0x550 built [records 0xB000]: against your map sheet (your saved order): "
                                         "4 of 25 places changed") == 0 &&
                  l[0].find("| 25 players: #0 P1") != std::string::npos && l[1].find("svc+0xA60 built [records 0xA000]") != std::string::npos,
              "bench probe: both copies' builds are logged with their order against the map: " + (l.empty() ? std::string() : l[0]));
        CHECK(p.on_copies(M, {copy_of(S, 0xB000), other}, name).empty(), "bench probe: an unchanged copy logs nothing");
        l = p.on_copies(M, {copy_of(M, 0xB000), other}, name);
        CHECK(l.size() == 1 && l[0].find("bench probe: Team Management's working copy svc+0x550 changed: 4 of 25 places changed") == 0 &&
                  l[0].find("against your map sheet: same order") != std::string::npos,
              "bench probe: a change of the built copy (Turbo's write, a move on the screen) is logged: " + (l.empty() ? std::string() : l[0]));
        l = p.on_copies(M, {none, other}, name);
        CHECK(l.size() == 1 && l[0] == "bench probe: Team Management's working copy svc+0x550 is no longer your sheet's (Team Management closed?)",
              "bench probe: the copy going away is logged");
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
    test_bench_keep();
}
