// Native tests: the "player_morale" game call (core/player_morale.h, docs/re/player_status_roles.md section 4): the game's level function,
// MoraleStore::Find and SetTotalMorale behind the Caller abstraction on MoveWorld's synthetic career (PlayerMoraleManager at kMorale).
#pragma once

struct MoraleWorld : MoveWorld {
    static constexpr uint64_t kRecs = 0x30130000ULL;
    static constexpr uint64_t kFnFind = 0x147D8B5E8ULL, kFnSet = 0x147D960A8ULL, kFnLevel = 0x147D837D8ULL;
    MoraleWorld() {
        mem.map(kRecs, turbo::morale::kRecSize * 60);
        mem.wr(kMorale + turbo::morale::kPmmHub, kManagers);
        mem.wr(kMorale + turbo::morale::kStore + turbo::morale::kStoreTeam, static_cast<int32_t>(kNapoli));
        set_records({});
    }
    // {pid, emotion-1, total}
    void set_records(const std::vector<std::array<int, 3>>& recs) {
        for (size_t i = 0; i < recs.size(); ++i) {
            const uint64_t r = kRecs + i * turbo::morale::kRecSize;
            mem.wr(r + turbo::morale::kRecPid, static_cast<int32_t>(recs[i][0]));
            mem.wr(r + turbo::morale::kRecEmotion, static_cast<int32_t>(recs[i][1]));
            mem.wr(r + turbo::morale::kRecTotal, static_cast<int32_t>(recs[i][2]));
        }
        mem.wr(kMorale + turbo::morale::kStore + turbo::morale::kStoreBegin, kRecs);
        mem.wr(kMorale + turbo::morale::kStore + turbo::morale::kStoreEnd, kRecs + recs.size() * turbo::morale::kRecSize);
    }
    turbo::morale::Fns fns() const {
        turbo::morale::Fns f;
        f.pmm_vtable = kMoraleVt;
        f.pmm_handle_event = kFnMoraleHandle;
        f.find_record = kFnFind;
        f.set_total = kFnSet;
        f.level = kFnLevel;
        f.is_player_in_team = kFnInTeam;
        f.um_vtable = kUmVt;
        return f;
    }
};

// The game's functions over the fake store: GetMoraleLevel with six thresholds per emotion type (clamped to 0..4, as 0x147D837D8)
struct FakeMoraleGame : turbo::morale::Caller {
    MoraleWorld& w;
    std::set<int> squad;  // pids IsPlayerInTeam(pid, 48) answers true for
    int thresholds[5][6] = {{0, 15, 40, 65, 75, 95}, {0, 10, 30, 50, 70, 90}, {0, 20, 45, 60, 80, 100}, {0, 15, 40, 65, 75, 95}, {0, 5, 25, 55, 60, 110}};
    int set_calls = 0, level_calls = 0;
    bool keep_total = false;  // SetTotalMorale "refuses" (writes nothing)
    explicit FakeMoraleGame(MoraleWorld& world) : w(world) {}
    bool in_team(uint64_t, int pid, int team, bool& in, std::string&) override {
        in = team == ListWorld::kNapoli && squad.count(pid);
        return true;
    }
    bool find_record(uint64_t store, int pid, uint64_t& rec, std::string&) override {
        uint64_t b = 0, e = 0;
        w.mem.rd(store + turbo::morale::kStoreBegin, b);
        w.mem.rd(store + turbo::morale::kStoreEnd, e);
        rec = 0;
        for (uint64_t r = b; r < e; r += turbo::morale::kRecSize) {
            int32_t p = 0;
            w.mem.rd(r, p);
            if (p == pid) {
                rec = r;
                break;
            }
        }
        return true;
    }
    bool level(uint64_t, int total, int emotion, int& out, std::string&) override {
        ++level_calls;
        const int e = emotion < 0 ? 0 : (emotion > 4 ? 4 : emotion);
        const int* t = thresholds[e];
        out = 5;
        for (int i = 1; i < 6; ++i)
            if (total < t[i]) {
                out = i - 1;
                break;
            }
        return true;
    }
    bool set_total(uint64_t, uint64_t rec, int total, std::string&) override {
        ++set_calls;
        if (!keep_total) w.mem.wr(rec + turbo::morale::kRecTotal, static_cast<int32_t>(total));
        return true;
    }
};

static void test_player_morale() {
    using namespace turbo::morale;
    std::printf("-- player_morale: very happy through the game's level function, SetTotalMorale, count-only paths\n");
    auto req_for = [](int action, int pid, int value = 0) {
        Request q;
        q.action = action;
        q.player = pid;
        q.value = value;
        q.comm = ListWorld::kComm;
        return q;
    };
    {
        // record exists: the top very-happy total of HIS emotion type (emotion 0: 75..94 -> 94; emotion 1: 70..89 -> 89; 5+ clamps to 4: 60..109)
        MoraleWorld w;
        w.set_records({{1001, 0, 100}, {1002, 1, 110}, {1003, 7, 20}});
        FakeMoraleGame g(w);
        g.squad = {1001, 1002, 1003};
        Result r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1001));
        CHECK(r.ok, "morale: record exists -> ok: " + r.message);
        CHECK(r.total == 94 && r.level == kLevelVeryHappy, "morale: emotion 0 -> 94, level 4 (" + std::to_string(r.total) + ")");
        CHECK(r.message.find("100 -> 94 (very happy)") != std::string::npos, "morale: text names before / after / level: " + r.message);
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1002));
        CHECK(r.ok && r.total == 89 && r.level == 4, "morale: emotion 1 -> 89");
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1003));
        CHECK(r.ok && r.total == 109 && r.level == 4, "morale: emotion 7 clamps to type 4 -> 109");
        CHECK(g.set_calls == 3, "morale: one SetTotalMorale per call");
        int32_t t = 0;
        w.mem.rd(MoraleWorld::kRecs + kRecTotal, t);
        CHECK(t == 94, "morale: record +0x2C holds 94");
        // explicit value
        r = run(w.mem, g, w.fns(), req_for(kActionValue, 1001, 60));
        CHECK(r.ok && r.total == 60 && r.level == 2, "morale: explicit value 60 -> content");
        r = run(w.mem, g, w.fns(), req_for(kActionValue, 1001, 121));
        CHECK(!r.ok && g.set_calls == 4, "morale: value 121 refused");
        // check only: nothing written
        r = run(w.mem, g, w.fns(), req_for(kActionCheck, 1002));
        CHECK(r.ok && g.set_calls == 4 && r.target == 89, "morale: check writes nothing, reports the target");
        // the game keeps its value: reported
        g.keep_total = true;
        r = run(w.mem, g, w.fns(), req_for(kActionValue, 1001, 30));
        CHECK(!r.ok && r.stage == "check" && r.total == 60, "morale: a SetTotalMorale that changed nothing is reported");
    }
    {
        // record missing: count only (no Create in this version), store full the same
        MoraleWorld w;
        w.set_records({{1001, 0, 50}});
        FakeMoraleGame g(w);
        g.squad = {1001, 1004};
        Result r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.total == kNoRecord && g.set_calls == 0, "morale: no record -> -2, nothing written: " + r.message);
        CHECK(r.message.find("no morale record") != std::string::npos, "morale: the text says why");
        std::vector<std::array<int, 3>> full;
        for (int i = 0; i < 52; ++i) full.push_back({2000 + i, 0, 50});
        w.set_records(full);
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.total == kNoRecord && g.set_calls == 0, "morale: store full, no record -> count only");
        full.push_back({3000, 0, 50});
        w.set_records(full);
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("corrupt") != std::string::npos, "morale: 53 records refused as corrupt");
    }
    {
        // refusals: not the user's club, gate set, store of another team, vtable mismatch, missing signature
        MoraleWorld w;
        w.set_records({{1001, 0, 50}, {1005, 0, 50}});
        FakeMoraleGame g(w);
        g.squad = {1001};
        Result r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1005));
        CHECK(!r.ok && r.message.find("not in your club") != std::string::npos && g.set_calls == 0, "morale: player not in the user's club refused");
        w.mem.wr(MoveWorld::kMorale + kGate, static_cast<uint8_t>(1));
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("gate byte") != std::string::npos && g.set_calls == 0, "morale: gate set refused");
        w.mem.wr(MoveWorld::kMorale + kGate, static_cast<uint8_t>(0));
        w.mem.wr(MoveWorld::kMorale + kStore + kStoreTeam, static_cast<int32_t>(49));
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("belongs to team 49") != std::string::npos && g.set_calls == 0, "morale: store of another team refused");
        w.mem.wr(MoveWorld::kMorale + kStore + kStoreTeam, static_cast<int32_t>(ListWorld::kNapoli));
        Fns f = w.fns();
        f.pmm_vtable = 0x14B0156B0ULL;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("not the PlayerMoraleManager") != std::string::npos, "morale: vtable mismatch refused");
        f = w.fns();
        f.pmm_handle_event = 0x147D8D358ULL;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("slot 1") != std::string::npos, "morale: slot 1 mismatch refused");
        f = w.fns();
        f.set_total = 0;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("pmm_set_total") != std::string::npos && g.set_calls == 0, "morale: missing signature refused");
        // level function not resolved: 85, said so
        f = w.fns();
        f.level = 0;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1001));
        CHECK(r.ok && r.total == 85 && r.message.find("not resolved") != std::string::npos, "morale: no level function -> 85 with a note: " + r.message);
        CHECK(r.level == -1, "morale: level not read without the function");
        // a level function that never answers 4 -> 85 with a note
        for (auto& row : g.thresholds) row[4] = row[5];
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1001));
        CHECK(r.ok && r.total == 85 && r.message.find("no total") != std::string::npos, "morale: no very-happy total -> 85");
    }
    {
        // stale records: counted, nothing written
        MoraleWorld w;
        w.set_records({{1001, 0, 50}, {1006, 0, 50}, {1007, 0, 50}});
        FakeMoraleGame g(w);
        g.squad = {1001};
        Result r = run(w.mem, g, w.fns(), req_for(kActionStale, 0));
        CHECK(r.ok && r.stale == 2 && r.total == 2 && r.level == 3 && g.set_calls == 0, "morale: 2 stale of 3 counted, nothing written: " + r.message);
        uint64_t e = 0;
        w.mem.rd(MoveWorld::kMorale + kStore + kStoreEnd, e);
        CHECK(e == MoraleWorld::kRecs + 3 * kRecSize, "morale: the store is unchanged");
    }
    {
        // the words and the kill switch
        Request q = req_for(kActionValue, 1001, 77);
        int64_t a[4];
        args_from_request(q, a);
        Request back = request_from_args(a);
        CHECK(back.action == kActionValue && back.player == 1001 && back.value == 77 && back.bad_args.empty(), "morale: words round-trip");
        a[1] = 0x101;
        CHECK(!request_from_args(a).bad_args.empty(), "morale: unknown code bits refused");
        const std::filesystem::path dir = g_out / "morale_kill";
        std::filesystem::create_directories(dir);
        std::filesystem::remove(dir / kill_switch_name());
        CHECK(!killed(dir), "morale: on without the kill switch");
        { std::ofstream(dir / kill_switch_name()) << "off"; }
        CHECK(killed(dir), "morale: off with turbo_output\\call_player_morale_off.txt");
    }
}
