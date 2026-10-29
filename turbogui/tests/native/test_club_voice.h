// FC 27 LE Turbo GUI - native tests: club commentary names (core/club_voice.h, the club pass of voice::process_query).
// Included by test_main.cpp after FakeQuery / djb2_xor (core: voice swaps); test_club_voice() is called from main.
#pragma once

static void test_club_voice() {
    constexpr int32_t kLeicester = 95, kArsenal = 1, kEverton = 7, kSarpsborg = 112199, kGroupId = 0x7777;
    const uint32_t kGoal = djb2_xor("GOAL_SCORED");
    auto table_of = [](std::vector<voice::Table::Team> teams) {
        voice::Table t;
        t.teams = std::move(teams);
        return t;
    };

    run_case("club voice: the exact club-id parameter names", [&] {
        const char* yes[] = {"asset_team_ID", "home_ID", "away_ID", "winning_team_ID", "losing_team_ID", "ta_focus_team1_ID",
                             "ta_focus_team2_ID", "ttp_team_a_ID", "lae_focus_team_1_ID", "lae_focus_team_2_ID", "lae_against_team_1_ID",
                             "lae_against_team_2_ID", "lb_against_team_ID", "news_recap_team_ID", "tr_team_b_ID", "opposite_team_ID"};
        bool all = true;
        for (const char* n : yes) all = all && voice::is_club_param(n);
        CHECK(all, "the 16 names of the brief");
        const char* no[] = {"team_gID", "opposite_team_gID", "team_venue_ID", "home_ID_x", "xhome_ID", "home_id", "Home_ID", "home_",
                            "player_db_pID", "asset_team_I", "", "home_IDD"};
        bool none = true;
        for (const char* n : no) none = none && !voice::is_club_param(n);
        CHECK(none && !voice::is_club_param(nullptr), "never a prefix, suffix or case match; *_gID and team_venue_ID never");
    });

    run_case("club voice: a table with only clubs is not empty, team() finds by id", [&] {
        const voice::Table t = table_of({{kArsenal, kEverton}, {kLeicester, kArsenal}});
        CHECK(!t.empty() && t.voices.empty() && t.kickoffs.empty(), "not empty");
        CHECK(t.team(kLeicester) && t.team(kLeicester)->to == kArsenal && t.team(kArsenal) && !t.team(kSarpsborg), "team()");
    });

    run_case("club voice 1: every club-id param holding B becomes A together; team_gID untouched", [&] {
        const voice::Table t = table_of({{kLeicester, kArsenal}});
        FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
        const size_t asset = fq.add("asset_team_ID", kLeicester), home = fq.add("home_ID", kLeicester), away = fq.add("away_ID", kEverton),
                     gid = fq.add("team_gID", kGroupId), venue = fq.add("team_venue_ID", kLeicester);
        voice::Stats st;
        voice::Guard g;
        const int n = voice::process_query(fq.sync(), t, g, st, false);
        CHECK(n == 2 && fq.value(asset) == kArsenal && fq.value(home) == kArsenal, fmt("both ids rewritten (%d)", n));
        CHECK(fq.set_flag(asset) == 1 && fq.set_flag(home) == 1, "set flag as SetInt writes it");
        CHECK(fq.value(away) == kEverton && fq.value(gid) == kGroupId && fq.value(venue) == kLeicester && fq.set_flag(venue) == 0,
              "the opponent, team_gID and team_venue_ID untouched");
        const voice::StatsSnapshot s = voice::snapshot(st);
        CHECK(s.club_rewrites == 2 && s.rewrites == 0 && s.guard_skips == 0, "counted as club rewrites");
    });

    run_case("club voice 2: a club with no entry is left alone", [&] {
        const voice::Table t = table_of({{kLeicester, kArsenal}});
        FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
        const size_t home = fq.add("home_ID", kSarpsborg), away = fq.add("away_ID", kEverton);
        voice::Stats st;
        voice::Guard g;
        CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0 && fq.value(home) == kSarpsborg && fq.value(away) == kEverton, "unchanged");
    });

    run_case("club voice 3: never two clubs with one voice in a line (collision guard)", [&] {
        const voice::Table t = table_of({{kLeicester, kArsenal}, {kSarpsborg, kArsenal}});
        voice::Stats st;
        voice::Guard g;
        {  // B plays A
            FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
            const size_t home = fq.add("home_ID", kLeicester), away = fq.add("away_ID", kArsenal), asset = fq.add("asset_team_ID", kLeicester);
            CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0 && fq.value(home) == kLeicester && fq.value(away) == kArsenal &&
                      fq.value(asset) == kLeicester,
                  "Leicester v Arsenal: nothing rewritten");
        }
        CHECK(voice::snapshot(st).guard_skips == 1, "guard_skips + 1");
        {  // B1 and B2 both voiced as A
            FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
            const size_t home = fq.add("home_ID", kLeicester), away = fq.add("away_ID", kSarpsborg);
            CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0 && fq.value(home) == kLeicester && fq.value(away) == kSarpsborg,
                  "Leicester v Sarpsborg, both Arsenal: nothing rewritten");
        }
        CHECK(voice::snapshot(st).guard_skips == 2 && voice::snapshot(st).club_rewrites == 0, "guard_skips + 1 again");
        {  // a swap pair is no collision: Leicester -> Arsenal, Arsenal -> Leicester
            const voice::Table sw = table_of({{kArsenal, kLeicester}, {kLeicester, kArsenal}});
            FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
            const size_t home = fq.add("home_ID", kLeicester), away = fq.add("away_ID", kArsenal);
            CHECK(voice::process_query(fq.sync(), sw, g, st, false) == 2 && fq.value(home) == kArsenal && fq.value(away) == kLeicester,
                  "swap pair: both rewritten");
        }
    });

    run_case("club voice 4: no club name (voice_of 0) rewrites the ids to 0", [&] {
        const voice::Table t = table_of({{kLeicester, 0}, {kSarpsborg, 0}});
        FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
        const size_t home = fq.add("home_ID", kLeicester), win = fq.add("winning_team_ID", kLeicester), away = fq.add("away_ID", kSarpsborg);
        voice::Stats st;
        voice::Guard g;
        CHECK(voice::process_query(fq.sync(), t, g, st, false) == 3 && fq.value(home) == 0 && fq.value(win) == 0 && fq.value(away) == 0,
              "0 for both clubs (no recorded club holds id 0; two zeros are no collision)");
        CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0, "a second pass finds nothing to rewrite");
    });

    run_case("club voice: the second pre-handler pass never chains A -> C", [&] {
        const voice::Table t = table_of({{kArsenal, kEverton}, {kLeicester, kArsenal}});  // sorted by id, as build_teams gives it
        FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
        const size_t home = fq.add("home_ID", kLeicester), asset = fq.add("asset_team_ID", kLeicester);
        voice::Stats st;
        voice::Guard g;
        CHECK(voice::process_query(fq.sync(), t, g, st, false) == 2 && fq.value(home) == kArsenal, "first pass: Arsenal");
        CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0 && fq.value(home) == kArsenal && fq.value(asset) == kArsenal,
              "second pass: still Arsenal");
        CHECK(voice::snapshot(st).guard_skips == 1, "counted once");
        CHECK(voice::process_query(fq.sync(), t, g, st, false) == 2 && fq.value(home) == kEverton,
              "a new query at the same address holding Arsenal is rewritten (marks are used once)");
    });

    run_case("club voice: players and clubs in one query; own query, other groups, bad params", [&] {
        voice::Table t = table_of({{kLeicester, kArsenal}});
        t.voices.push_back({278319, 216435, false});
        voice::Stats st;
        voice::Guard g;
        {
            FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
            const size_t p = fq.add("player_db_pID", 278319), home = fq.add("home_ID", kLeicester);
            CHECK(voice::process_query(fq.sync(), t, g, st, false) == 2 && fq.value(p) == 216435 && fq.value(home) == kArsenal, "both passes");
            CHECK(voice::snapshot(st).rewrites == 1 && voice::snapshot(st).club_rewrites == 1, "counted apart");
        }
        {
            FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
            const size_t home = fq.add("home_ID", kLeicester);
            CHECK(voice::process_query(fq.sync(), t, g, st, true) == 0 && fq.value(home) == kLeicester, "Turbo's own query untouched");
        }
        {
            FakeQuery fq(0x1234, kGoal);
            const size_t home = fq.add("home_ID", kLeicester);
            CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0 && fq.value(home) == kLeicester, "another event group untouched");
        }
        {
            FakeQuery fq(voice::kGroupCommentaryDb, kGoal);
            const size_t multi = fq.add("home_ID", kLeicester, 1, 1), str = fq.add("away_ID", kLeicester, 2, 0);
            CHECK(voice::process_query(fq.sync(), t, g, st, false) == 0 && fq.value(multi) == kLeicester && fq.value(str) == kLeicester,
                  "multi-value and non-int params untouched");
        }
    });

    run_case("club voice 5: the store (scope, round trip, bad file, build_teams)", [&] {
        const fs::path dir = test_tmp_dir("club_voice");
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path p = clubvoice::store_path(dir);
        CHECK(p.filename() == "club_voice.json" && p.parent_path().filename() == "callnames", "store path");
        clubvoice::Store s;
        std::string err;
        CHECK(s.load(p, &err) && s.entries.empty() && err.empty(), "a missing file is an empty store");
        auto entry = [](int64_t id, int64_t of, const std::string& scope) {
            clubvoice::Entry e;
            e.teamid = id;
            e.voice_of = of;
            e.scope = scope;
            e.club = "club " + std::to_string(id);
            e.from = "from " + std::to_string(of);
            e.when = "2026-10-28 23:40";
            return e;
        };
        s.upsert(entry(kLeicester, kArsenal, "career:LEICESTER"));
        s.upsert(entry(kLeicester, kEverton, "global"));
        s.upsert(entry(kSarpsborg, 0, "global"));
        s.upsert(entry(kEverton, kEverton, "global"));         // own name: no change
        s.upsert(entry(kArsenal, kEverton, "career:OTHER"));
        s.upsert(entry(kGroupId, kArsenal, "weird"));           // unknown scope: kept, never in effect
        s.upsert(entry(kLeicester, kArsenal, "career:LEICESTER"));  // same club and scope: replaced, not added
        CHECK(s.entries.size() == 6, fmt("one per club and scope (%zu)", s.entries.size()));
        CHECK(s.find_in_effect(kLeicester, "LEICESTER") && s.find_in_effect(kLeicester, "LEICESTER")->voice_of == kArsenal,
              "this career's entry wins over the global one");
        CHECK(s.find_in_effect(kLeicester, "OTHER") && s.find_in_effect(kLeicester, "OTHER")->voice_of == kEverton,
              "another career: the global entry");
        CHECK(!s.find_in_effect(kArsenal, "LEICESTER") && s.find_in_effect(kArsenal, "OTHER"), "a career entry only in its career");
        CHECK(!s.find_in_effect(kGroupId, "LEICESTER") && s.find(kGroupId, "weird"), "unknown scope kept, never in effect");
        const auto teams = clubvoice::build_teams(s, "LEICESTER");  // the career id (App: reapply.career), not its scope
        CHECK(teams.size() == 2 && teams[0].id == kLeicester && teams[0].to == kArsenal && teams[1].id == kSarpsborg && teams[1].to == 0,
              fmt("build_teams: Leicester -> Arsenal, Sarpsborg -> none, own name left out (%zu)", teams.size()));
        CHECK(s.save(p, &err) && fs::exists(p) && !fs::exists(fs::path(p.string() + ".tmp")), "saved: " + err);
        clubvoice::Store r;
        err.clear();
        CHECK(r.load(p, &err) && err.empty() && clubvoice::store_json(r) == clubvoice::store_json(s), "round trip: " + err);
        CHECK(r.forget(kLeicester, "global") && !r.forget(kLeicester, "global") && r.entries.size() == 5, "forget one scope");
        {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f << "{ not json";
        }
        clubvoice::Store b;
        err.clear();
        CHECK(b.load(p, &err) && b.entries.empty() && !fs::exists(p) && err.find("set aside") != std::string::npos, "a bad file is set aside: " + err);
        {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f << R"({"turbo_club_voice": 1, "clubs": [{"teamid": 95, "voice_of": 1, "scope": "global"}, {"teamid": -3, "voice_of": 1},)"
                 R"( {"teamid": 7}, {"teamid": 9, "voice_of": -1}, "x"]})";
        }
        clubvoice::Store d;
        err.clear();
        CHECK(d.load(p, &err) && d.entries.size() == 1 && err.find("4 bad entries") != std::string::npos, "bad entries dropped: " + err);
    });

    run_case("club voice: the recorded-club list of a language", [&] {
        const fs::path dir = test_tmp_dir("club_voice_recorded");
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path p = clubvoice::recorded_path(dir, "fre_fr");
        CHECK(p.filename() == "fre_fr.json" && p.parent_path().filename() == "clubs", "path");
        fs::create_directories(p.parent_path(), ec);
        {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f << R"({"lang": "fre_fr", "ids": [95, 1, 95, -2, 0, 112199, "x"]})";
        }
        std::vector<int32_t> ids;
        std::string err;
        CHECK(clubvoice::load_recorded(p, ids, &err) && ids == std::vector<int32_t>({1, 95, 112199}), "sorted, unique, positive ids");
        CHECK(!clubvoice::load_recorded(dir / "none.json", ids, &err) && ids.empty(), "a missing file is an empty list");
    });
}
