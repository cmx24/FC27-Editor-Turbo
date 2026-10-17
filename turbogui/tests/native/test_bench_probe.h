// Native tests: the bench probe (core/bench_probe.h): when it reads your club's sheet around Team Management, what it logs (only a change
// between the polls), the summary 15 s after Team Management closed, squad players missing from the sheet, failed reads. Read-only by design:
// the probe has no write path at all.
#pragma once

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
}
