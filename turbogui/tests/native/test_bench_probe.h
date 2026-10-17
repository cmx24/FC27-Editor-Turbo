// Native tests: the bench probe (core/bench_probe.h): when it reads your club's sheet around Team Management, what it logs (only a change
// between the polls), the summary 15 s after Team Management closed, squad players missing from the sheet, failed reads. Read-only by design:
// the probe has no write path at all.
#pragma once

// 2.15 bench keep: the Keeper's decisions (core/bench_probe.h), driven by the probe's own schedule as the card's tick drives them. The write
// itself (tsheet::apply_bench / submit_bench) is tested on the simulated sheet in test_team_sheet.h
namespace bench_keep_test {
using namespace turbo::bench;
struct Rig {
    Probe p;
    Keeper k;
    Input in;
    Namer name = [](int32_t pid) { return "P" + std::to_string(pid); };
    std::vector<std::string> log;
    bool submit = false;
    int reads = 0;
    Rig() {
        in.career = true;
        in.load_gen = 1;
        in.phase = "hub";
        in.save_t = 1000;
        in.now = 100.0;
    }
    // one tick of the card: the keeper's step, then the probe's read (if due) fed to both
    void tick(const Ids& ids, bool on = true, const std::string& why = "", long long save_age = -1) {
        const KeepTick t = k.step(in, on, why, save_age);
        log.insert(log.end(), t.log.begin(), t.log.end());
        submit = t.submit;
        const Due d = p.step(in);
        if (d.kind == Kind::None) return;
        ++reads;
        p.on_read(d, ids, {}, name);
        const std::vector<std::string> l = k.on_read(d, ids, name);
        log.insert(log.end(), l.begin(), l.end());
    }
    bool logged(const std::string& part) const {
        for (const auto& l : log)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
    std::string last() const { return log.empty() ? std::string() : log.back(); }
};
inline Ids sheet(const std::vector<int32_t>& v) {
    Ids ids;
    ids.fill(-1);
    for (size_t k = 0; k < v.size() && k < ids.size(); ++k) ids[k] = v[k];
    return ids;
}
}  // namespace bench_keep_test

static void test_bench_keep() {
    using namespace turbo::bench;
    using bench_keep_test::Rig;
    using bench_keep_test::sheet;
    std::printf("-- bench keep: your saved bench order back after the game reorders it (2.15)\n");
    std::vector<int32_t> base;
    for (int i = 1; i <= 25; ++i) base.push_back(i);
    const Ids U = sheet(base);  // the order the user saved: XI 1..11, bench 12..18, reserves 19..25
    std::vector<int32_t> o = base, s = base, s2 = base;
    std::swap(o[11], o[12]);  // the order Team Management opened with
    std::swap(s[11], s[14]);  // the game's shuffle after the leaving: 4 places
    std::swap(s[19], s[22]);
    std::swap(s2[15], s2[16]);  // another shuffle
    const Ids O = sheet(o), S = sheet(s), S2 = sheet(s2);

    // shuffle_of: a permutation of 11..51 only
    CHECK(shuffle_of(U, S).empty() && moved_places(U, S) == 4, "bench keep: the bench / reserves in another order is a shuffle (4 places)");
    CHECK(shuffle_of(U, U) == "same order", "bench keep: the same order is not a shuffle");
    {
        Ids x = S;
        std::swap(x[3], x[15]);
        CHECK(shuffle_of(U, x).find("the starting XI changed (#3 4 -> 16)") == 0, "bench keep: a moved starter is refused: " + shuffle_of(U, x));
        Ids y = S;
        y[24] = 99;
        const std::string w = shuffle_of(U, y);
        CHECK(w.find("other players on the sheet") == 0 && w.find("on now: 99") != std::string::npos && w.find("off now: 25") != std::string::npos,
              "bench keep: another player on the sheet is refused: " + w);
        Ids e = U;
        e[25] = e[20];
        e[20] = -1;
        CHECK(shuffle_of(U, e) == "the empty places moved (#20)", "bench keep: a moved empty place is refused: " + shuffle_of(U, e));
        Ids d1 = U;
        d1[12] = 12;  // 12 twice (13 gone) in both
        Ids d2 = d1;
        std::swap(d2[11], d2[14]);
        CHECK(shuffle_of(d1, d2) == "player 12 is on the sheet twice", "bench keep: a player twice is refused: " + shuffle_of(d1, d2));
    }

    // A: the Yes saves while Team Management is open; the game reorders 1 s after the close; the write waits for G2's 30 s after the save
    {
        Rig r;
        r.tick(O, false);
        CHECK(r.k.phase() == KeepPhase::Off && r.k.state() == "off (no turbo_output\\bench_keep_on.txt)", "bench keep: off by default: " + r.k.state());
        r.in.now = 101.0;
        r.tick(O);
        CHECK(r.k.phase() == KeepPhase::Idle && r.k.state() == "on: waiting for Team Management", "bench keep: on: " + r.k.state());
        r.in.team_mgmt = true;
        r.in.now = 110.0;
        r.tick(O);
        CHECK(r.k.phase() == KeepPhase::Open, "bench keep: Team Management open");
        r.in.now = 111.0;
        r.tick(U);  // the user reorders
        r.in.save_t = 1050;
        r.in.now = 112.2;
        r.tick(U);
        CHECK(r.logged("bench keep: your order at the save (Team Management open) is the one to keep: 25 players"), "bench keep: the order at the save is kept");
        r.in.team_mgmt = false;
        r.in.now = 113.0;
        r.tick(U);
        CHECK(r.k.phase() == KeepPhase::Watch && !r.logged("reordered"), "bench keep: watching after the close (the close read is the saved order)");
        r.in.now = 114.0;
        r.tick(S, true, "", 4);
        CHECK(r.k.phase() == KeepPhase::Pending &&
                  r.logged("bench keep: the game reordered your bench / reserves 1 s after Team Management closed (4 places; the starting XI and the players unchanged)") &&
                  r.logged("P12 #11->#14"),
              "bench keep: the shuffle 1 s after the close arms the write-back: " + r.last());
        CHECK(r.k.state() == "on: the game reordered your bench (4 places): your saved order goes back once the hub is quiet", r.k.state());
        CHECK(!r.submit, "bench keep: not in the same tick");
        r.in.now = 115.0;
        r.tick(S, true, "", 5);
        CHECK(!r.submit, "bench keep: no write within 30 s of the save (G2)");
        r.in.now = 128.0;
        r.tick(S, true, "", 29);
        CHECK(!r.submit, "bench keep: 29 s after the save: still not");
        r.in.now = 129.0;
        r.tick(S, true, "", 30);
        CHECK(r.submit, "bench keep: 30 s after the save, in the hub, Team Management closed: the write-back is due");
        Ids saved{}, before{};
        std::vector<std::string> l;
        CHECK(r.k.prepare(S, saved, before, l, r.name).empty() && saved == U && before == S && l.empty(), "bench keep: prepare gives the job's ids");
        r.k.queued(129.0);
        CHECK(r.k.phase() == KeepPhase::Queued && r.k.state() == "on: writing your saved order back ...", r.k.state());
        r.in.now = 130.0;
        r.tick(S, true, "", 31);
        CHECK(!r.submit, "bench keep: one job: nothing more while it is queued");
        r.k.on_result(true, "your saved order is back in places 11..51 (4 places written, read back)");
        CHECK(r.k.phase() == KeepPhase::Done && r.k.state() == "on: put back: your saved order is back in places 11..51 (4 places written, read back)",
              r.k.state());
        for (double t = 131.0; t < 150.0; t += 1.0) {
            r.in.now = t;
            r.tick(S2, true, "", 40);
            CHECK(!r.submit, "bench keep: ONE write-back per Team Management visit");
        }
        // the next visit starts over
        r.in.team_mgmt = true;
        r.in.now = 200.0;
        r.tick(U);
        CHECK(r.k.phase() == KeepPhase::Open, "bench keep: a new visit");
        r.in.team_mgmt = false;
        r.in.now = 205.0;
        r.tick(U, true, "", 150);
        CHECK(r.k.phase() == KeepPhase::Watch &&
                  r.logged("bench keep: no save seen while Team Management was open: the order at the close is the one to keep: 25 players"),
              "bench keep: no save in this visit: the order at the close");
        for (double t = 206.0; t <= 220.0; t += 1.0) {
            r.in.now = t;
            r.tick(U, true, "", 160);
        }
        CHECK(r.k.phase() == KeepPhase::Idle && r.logged("bench keep: your order held for 15 s after Team Management closed: nothing to do"),
              "bench keep: no reorder within 15 s: nothing to do");
        CHECK(r.k.state().find("on: waiting for Team Management (last: put back:") == 0, r.k.state());
    }

    // B: the save and the close in one bridge poll: the order at the close is kept, the save still counts for the visit
    {
        Rig r;
        r.tick(O);
        r.in.team_mgmt = true;
        r.in.now = 110.0;
        r.tick(O);
        r.in.now = 111.0;
        r.tick(U);
        r.in.team_mgmt = false;
        r.in.save_t = 1050;
        r.in.now = 113.0;
        r.tick(U, true, "", 0);
        r.in.now = 113.1;
        r.tick(U, true, "", 0);  // the probe's save read (after the close)
        r.in.now = 114.0;
        r.tick(O, true, "", 1);  // the game's reorder is the opening order: with a save seen, it is still put back
        CHECK(r.k.phase() == KeepPhase::Pending, "bench keep: save with the close: the reorder is put back: " + r.last());
    }

    // C: no save (a No) and the order after the close is the one Team Management opened with: the game discarding the edit, left alone
    {
        Rig r;
        r.tick(O);
        r.in.team_mgmt = true;
        r.in.now = 110.0;
        r.tick(O);
        r.in.now = 111.0;
        r.tick(U);
        r.in.team_mgmt = false;
        r.in.now = 113.0;
        r.tick(U);
        r.in.now = 114.0;
        r.tick(O);
        CHECK(r.k.phase() == KeepPhase::Done &&
                  r.logged("bench keep: 1 s after Team Management closed: left alone: the order went back to the one Team Management opened with and no save "
                           "was seen (a No?)"),
              "bench keep: a No's revert is left alone: " + r.last());
        for (double t = 115.0; t < 140.0; t += 1.0) {
            r.in.now = t;
            r.tick(O, true, "", -1);
            CHECK(!r.submit, "bench keep: nothing written after a No");
        }
        // D: no save but another order than the opening one: the order at the close is put back (the brief's fallback)
        Rig r2;
        r2.tick(O);
        r2.in.team_mgmt = true;
        r2.in.now = 110.0;
        r2.tick(O);
        r2.in.team_mgmt = false;
        r2.in.now = 113.0;
        r2.tick(U);
        r2.in.now = 114.0;
        r2.tick(S);
        CHECK(r2.k.phase() == KeepPhase::Pending, "bench keep: no save, another order than the opening one: armed");
        r2.in.now = 115.0;
        r2.tick(S);
        CHECK(r2.submit, "bench keep: no save seen at all: due at once in the hub");
    }

    // E: the sheet changed in more than its order after the close: left alone
    {
        Rig r;
        r.tick(O);
        r.in.team_mgmt = true;
        r.in.now = 110.0;
        r.tick(O);
        r.in.save_t = 1050;
        r.in.now = 111.0;
        r.tick(U);
        r.in.team_mgmt = false;
        r.in.now = 113.0;
        r.tick(U);
        Ids x = S;
        std::swap(x[3], x[15]);
        r.in.now = 114.0;
        r.tick(x);
        CHECK(r.k.phase() == KeepPhase::Done && r.logged("left alone: the sheet changed after Team Management closed, not only its order (the starting XI changed"),
              "bench keep: a moved starter after the close: left alone: " + r.last());
    }

    // F: the gates while the write-back waits: a match, the deadline, Team Management again, a reload, the kill switch, the career left
    auto armed = [&]() {
        Rig* r = new Rig();
        r->tick(O);
        r->in.team_mgmt = true;
        r->in.now = 110.0;
        r->tick(O);
        r->in.save_t = 1050;
        r->in.now = 111.0;
        r->tick(U);
        r->in.team_mgmt = false;
        r->in.now = 113.0;
        r->tick(U);
        r->in.now = 114.0;
        r->tick(S, true, "", 4);
        return std::unique_ptr<Rig>(r);
    };
    {
        auto r = armed();
        CHECK(r->k.phase() == KeepPhase::Pending, "bench keep: armed");
        r->in.phase = "prematch";
        for (double t = 150.0; t < 203.0; t += 1.0) {
            r->in.now = t;
            r->tick(S, true, "", 60);
            CHECK(!r->submit, "bench keep: never outside the hub");
        }
        r->in.now = 204.0;
        r->tick(S, true, "", 60);
        CHECK(r->k.phase() == KeepPhase::Done &&
                  r->logged("bench keep: not written back: 90 s after Team Management closed the write-back was still waiting (Lua's phase is prematch, not the hub): "
                            "given up (nothing written)"),
              "bench keep: given up 90 s after the close: " + r->last());
    }
    {
        auto r = armed();
        r->in.team_mgmt = true;
        r->in.now = 150.0;
        r->tick(S, true, "", 40);
        CHECK(r->k.phase() == KeepPhase::Open && !r->submit &&
                  r->logged("bench keep: Team Management opened again before the write-back: dropped (Turbo never writes while it is open)"),
              "bench keep: Team Management again: dropped");
    }
    {
        auto r = armed();
        r->in.load_gen = 2;
        r->in.now = 150.0;
        r->tick(S, true, "", 40);
        CHECK(r->k.phase() == KeepPhase::Idle && !r->submit && r->logged("bench keep: the career was (re)loaded: the write-back is dropped"),
              "bench keep: a reload drops it");
    }
    {
        auto r = armed();
        r->in.now = 150.0;
        r->tick(S, false, "kill switch turbo_output\\bench_keep_off.txt is present", 40);
        CHECK(r->k.phase() == KeepPhase::Off && !r->submit &&
                  r->logged("bench keep: switched off (kill switch turbo_output\\bench_keep_off.txt is present): the write-back is dropped (nothing written)") &&
                  r->k.state() == "off: kill switch turbo_output\\bench_keep_off.txt is present",
              "bench keep: the kill switch drops it: " + r->k.state());
    }
    {
        auto r = armed();
        r->in.career = false;
        r->in.now = 150.0;
        r->tick(S, true, "", 40);
        CHECK(r->k.phase() == KeepPhase::Idle && r->logged("bench keep: the career was left: the write-back is dropped"), "bench keep: the career left drops it");
    }
    // G: a refused submit is tried again every second and logged once per reason; a queued job that never comes back is given up
    {
        auto r = armed();
        r->in.now = 150.0;
        r->tick(S, true, "", 40);
        CHECK(r->submit, "bench keep: due");
        std::vector<std::string> l;
        r->k.not_queued("a channel E job is still queued or running", l);
        r->k.not_queued("a channel E job is still queued or running", l);
        CHECK(l.size() == 1 && l[0].find("bench keep: the write-back is not queued yet (a channel E job is still queued or running): tried again") == 0,
              "bench keep: a refusal is logged once");
        r->in.now = 150.5;
        r->tick(S, true, "", 40);
        CHECK(!r->submit, "bench keep: not every frame");
        r->in.now = 151.0;
        r->tick(S, true, "", 41);
        CHECK(r->submit, "bench keep: tried again a second later");
        Ids saved{}, before{};
        CHECK(r->k.prepare(S, saved, before, l, r->name).empty(), "prepare");
        r->k.queued(151.0);
        r->in.now = 167.0;
        r->tick(S, true, "", 57);
        CHECK(r->k.phase() == KeepPhase::Done && r->logged("bench keep: not written back: the game thread did not run the job (nothing written by it)"),
              "bench keep: a lost job is given up");
    }
    // H: prepare: the game put the order back itself; a newer reorder; a sheet that changed in its players
    {
        Ids saved{}, before{};
        std::vector<std::string> l;
        auto r = armed();
        CHECK(r->k.prepare(U, saved, before, l, r->name) == "already in place" && r->k.phase() == KeepPhase::Done &&
                  l.back() == "bench keep: nothing written: the sheet holds your saved order again (the game put it back itself)",
              "bench keep: the saved order is back by itself: nothing written");
        auto r2 = armed();
        l.clear();
        CHECK(r2->k.prepare(S2, saved, before, l, r2->name).empty() && before == S2 && saved == U && l.size() == 1 &&
                  l[0].find("the order changed again since the reorder was seen") != std::string::npos,
              "bench keep: a newer reorder: the job starts from the latest");
        auto r3 = armed();
        l.clear();
        Ids y = S;
        y[24] = 99;
        CHECK(r3->k.prepare(y, saved, before, l, r3->name).find("other players") == 0 && r3->k.phase() == KeepPhase::Done,
              "bench keep: other players before the job: nothing written");
    }
    // I: switched on while Team Management is open: no visit until it opens again (the opening order is not known)
    {
        Rig r;
        r.in.team_mgmt = true;
        r.tick(O, false);
        r.in.now = 101.0;
        r.tick(O, true);
        CHECK(r.k.phase() == KeepPhase::Idle, "bench keep: on while Team Management is open: waits for the next visit");
        r.in.team_mgmt = false;
        r.in.now = 102.0;
        r.tick(U);
        r.in.now = 103.0;
        r.tick(S);
        CHECK(r.k.phase() == KeepPhase::Idle, "bench keep: nothing armed without a visit");
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
