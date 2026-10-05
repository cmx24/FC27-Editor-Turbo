// Native tests: the "player_morale" game call (core/player_morale.h, docs/re/player_status_roles.md section 4): the game's level function,
// MoraleStore::Find and SetTotalMorale behind the Caller abstraction on MoveWorld's synthetic career (PlayerMoraleManager at kMorale);
// creating a missing record (GetPlayerEmotionType, MoraleStore::Create, InitMorale: section 4.2) only when every check passes.
#pragma once

struct MoraleWorld : MoveWorld {
    static constexpr uint64_t kRecs = 0x30130000ULL;
    static constexpr uint64_t kFnFind = 0x147D8B5E8ULL, kFnSet = 0x147D960A8ULL, kFnLevel = 0x147D837D8ULL;
    static constexpr uint64_t kFnEmotion = 0x147B866BCULL, kFnCreate = 0x147D81108ULL, kFnInit = 0x147D93B08ULL;
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
        f.emotion = kFnEmotion;
        f.create = kFnCreate;
        f.init_morale = kFnInit;
        return f;
    }
    turbo::morale::Fns fns_no_create() const {
        turbo::morale::Fns f = fns();
        f.emotion = f.create = f.init_morale = 0;
        return f;
    }
};

// The game's functions over the fake store: GetMoraleLevel with six thresholds per emotion type (clamped to 0..4, as 0x147D837D8)
struct FakeMoraleGame : turbo::morale::Caller {
    MoraleWorld& w;
    std::set<int> squad;  // pids IsPlayerInTeam(pid, 48) answers true for
    int thresholds[5][6] = {{0, 15, 40, 65, 75, 95}, {0, 10, 30, 50, 70, 90}, {0, 20, 45, 60, 80, 100}, {0, 15, 40, 65, 75, 95}, {0, 5, 25, 55, 60, 110}};
    int set_calls = 0, level_calls = 0, emotion_calls = 0, create_calls = 0, init_calls = 0;
    bool keep_total = false;  // SetTotalMorale "refuses" (writes nothing)
    int emotion_value = 1;    // players.emotion (1..8)
    bool create_null = false; // Create returns null
    bool find_blind = false;  // Find never finds (the duplicate guard must still see the pid in the vector)
    int init_total = 55;      // what InitMorale computes
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
        if (find_blind) return true;
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
    bool emotion(uint64_t, int, int& out, std::string&) override {
        ++emotion_calls;
        out = emotion_value;
        return true;
    }
    // as 0x147D81108: append a 0x60-byte record {pid, emotion - 1, 0}, null at 52 (no duplicate check)
    bool create(uint64_t store, int pid, int emotion0, uint64_t& rec, std::string&) override {
        ++create_calls;
        rec = 0;
        uint64_t b = 0, e = 0;
        w.mem.rd(store + turbo::morale::kStoreBegin, b);
        w.mem.rd(store + turbo::morale::kStoreEnd, e);
        if (create_null || (e - b) / turbo::morale::kRecSize >= 52) return true;
        rec = e;
        w.mem.wr(rec + turbo::morale::kRecPid, static_cast<int32_t>(pid));
        w.mem.wr(rec + turbo::morale::kRecEmotion, static_cast<int32_t>(emotion0));
        w.mem.wr(rec + turbo::morale::kRecTotal, static_cast<int32_t>(0));
        w.mem.wr(store + turbo::morale::kStoreEnd, e + turbo::morale::kRecSize);
        return true;
    }
    bool init_morale(uint64_t, uint64_t rec, std::string&) override {
        ++init_calls;
        w.mem.wr(rec + turbo::morale::kRecTotal, static_cast<int32_t>(init_total));
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
        // record missing, creation signatures missing: count only, store full the same
        MoraleWorld w;
        w.set_records({{1001, 0, 50}});
        FakeMoraleGame g(w);
        g.squad = {1001, 1004};
        Result r = run(w.mem, g, w.fns_no_create(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.total == kNoRecord && g.set_calls == 0, "morale: no record -> -2, nothing written: " + r.message);
        CHECK(r.message.find("no morale record") != std::string::npos && r.message.find("dc_player_emotion") != std::string::npos,
              "morale: the text says why (signature missing): " + r.message);
        CHECK(g.emotion_calls == 0 && g.create_calls == 0 && g.init_calls == 0, "morale: no creation without the signatures");
        Fns f = w.fns();
        f.init_morale = 0;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.total == kNoRecord && r.message.find("pmm_init_morale") != std::string::npos && g.create_calls == 0,
              "morale: one creation signature missing -> count only");
        std::vector<std::array<int, 3>> full;
        for (int i = 0; i < 52; ++i) full.push_back({2000 + i, 0, 50});
        w.set_records(full);
        r = run(w.mem, g, w.fns_no_create(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.total == kNoRecord && g.set_calls == 0, "morale: store full, no record -> count only");
        // store full with creation on: refused before Create (the game would return null)
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.total == kNoRecord && r.message.find("store is full") != std::string::npos && g.create_calls == 0 && g.emotion_calls == 0,
              "morale: 52 records -> Create not called: " + r.message);
        full.push_back({3000, 0, 50});
        w.set_records(full);
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1001));
        CHECK(!r.ok && r.message.find("corrupt") != std::string::npos, "morale: 53 records refused as corrupt");
    }
    {
        // record missing, creation resolved: created the game's way, then very happy ("record created")
        MoraleWorld w;
        w.set_records({{1001, 0, 50}});
        FakeMoraleGame g(w);
        g.squad = {1001, 1004};
        g.emotion_value = 2;  // players.emotion 2 -> record type 1: very happy 70..89
        Result r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(r.ok && r.created, "morale: missing record created -> ok: " + r.message);
        CHECK(g.emotion_calls == 1 && g.create_calls == 1 && g.init_calls == 1 && g.set_calls == 1, "morale: emotion, Create, InitMorale, SetTotalMorale once each");
        CHECK(r.total == 89 && r.level == kLevelVeryHappy && r.records == 2, "morale: the new record is very happy (" + std::to_string(r.total) + ")");
        CHECK(r.rec == MoraleWorld::kRecs + kRecSize, "morale: the record the game's Find returns is the created one");
        CHECK(r.message.find("record created") != std::string::npos && r.message.find("55 -> 89 (very happy)") != std::string::npos,
              "morale: the text says record created, InitMorale's total and the new one: " + r.message);
        int32_t pid = 0, emo = -1;
        w.mem.rd(MoraleWorld::kRecs + kRecSize + kRecPid, pid);
        w.mem.rd(MoraleWorld::kRecs + kRecSize + kRecEmotion, emo);
        CHECK(pid == 1004 && emo == 1, "morale: Create got the pid and emotion - 1");
        // again: Find finds it now, no second record
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(r.ok && !r.created && g.create_calls == 1 && r.records == 2, "morale: second run finds the record, nothing created");
        // codes 2 and 9 never create
        r = run(w.mem, g, w.fns(), req_for(kActionValue, 1001 + 0, 60));
        CHECK(r.ok && !r.created, "morale: value on an existing record");
        g.squad.insert(1010);
        r = run(w.mem, g, w.fns(), req_for(kActionValue, 1010, 60));
        CHECK(!r.ok && r.total == kNoRecord && g.create_calls == 1 && r.message.find("only the very happy") != std::string::npos, "morale: code 2 does not create");
        r = run(w.mem, g, w.fns(), req_for(kActionCheck, 1010));
        CHECK(!r.ok && r.total == kNoRecord && g.create_calls == 1, "morale: code 9 does not create");
        // the create kill switch: count only
        Request q = req_for(kActionVeryHappy, 1010);
        q.create_off = true;
        r = run(w.mem, g, w.fns(), q);
        CHECK(!r.ok && r.total == kNoRecord && g.create_calls == 1 && g.emotion_calls == 1 && r.message.find("call_player_morale_create_off.txt") != std::string::npos,
              "morale: create kill switch -> count only: " + r.message);
        // Create returns null: reported, nothing set
        g.create_null = true;
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1010));
        CHECK(!r.ok && !r.created && g.create_calls == 2 && g.init_calls == 1 && g.set_calls == 3 && r.message.find("returned no record") != std::string::npos,
              "morale: Create null -> InitMorale / SetTotalMorale not called: " + r.message);
        g.create_null = false;
        // an emotion type outside 1..8: nothing created
        g.emotion_value = 0;
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1010));
        CHECK(!r.ok && g.create_calls == 2 && r.message.find("emotion type is 0") != std::string::npos, "morale: emotion 0 -> nothing created");
        g.emotion_value = 1;
        // duplicate guard: Find says null but the vector holds the pid -> nothing created
        g.find_blind = true;
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && g.create_calls == 2 && r.message.find("duplicate guard") != std::string::npos, "morale: duplicate guard: " + r.message);
        g.find_blind = false;
        // create kill switch file
        const std::filesystem::path dir = g_out / "morale_create_kill";
        std::filesystem::create_directories(dir);
        std::filesystem::remove(dir / create_kill_switch_name());
        CHECK(!create_killed(dir), "morale: creation on without the create kill switch");
        { std::ofstream(dir / create_kill_switch_name()) << "off"; }
        CHECK(create_killed(dir), "morale: creation off with turbo_output\\call_player_morale_create_off.txt");
    }
    {
        // no record: every validation failure stops before GetPlayerEmotionType / Create / InitMorale
        MoraleWorld w;
        w.set_records({{1001, 0, 50}});
        FakeMoraleGame g(w);
        g.squad = {1001, 1004};
        auto none_called = [&g]() { return g.emotion_calls == 0 && g.create_calls == 0 && g.init_calls == 0 && g.set_calls == 0; };
        Result r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1005));
        CHECK(!r.ok && none_called(), "morale: not in the club -> nothing created");
        w.mem.wr(MoveWorld::kMorale + kGate, static_cast<uint8_t>(1));
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && none_called(), "morale: gate set -> nothing created");
        w.mem.wr(MoveWorld::kMorale + kGate, static_cast<uint8_t>(0));
        w.mem.wr(MoveWorld::kMorale + kStore + kStoreTeam, static_cast<int32_t>(49));
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && none_called(), "morale: store of another team -> nothing created");
        w.mem.wr(MoveWorld::kMorale + kStore + kStoreTeam, static_cast<int32_t>(ListWorld::kNapoli));
        Fns f = w.fns();
        f.pmm_vtable = 0x14B0156B0ULL;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && none_called(), "morale: vtable mismatch -> nothing created");
        f = w.fns();
        f.pmm_handle_event = 0x147D8D358ULL;
        r = run(w.mem, g, f, req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && none_called(), "morale: slot 1 mismatch -> nothing created");
        w.mem.wr(MoveWorld::kMorale + kPmmHub, static_cast<uint64_t>(0x1234));
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && none_called(), "morale: back pointer mismatch -> nothing created");
        w.mem.wr(MoveWorld::kMorale + kPmmHub, MoveWorld::kManagers);
        w.mem.wr(MoveWorld::kMorale + kStore + kStoreEnd, MoraleWorld::kRecs + kRecSize + 8);
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(!r.ok && r.message.find("corrupt") != std::string::npos && none_called(), "morale: vector not whole records -> nothing created");
        w.set_records({{1001, 0, 50}});
        f = w.fns();
        f.create = 0x100ULL;
        Request q = req_for(kActionVeryHappy, 1004);
        q.image_base = 0x140000000ULL;
        q.image_size = 0x10000000ULL;
        r = run(w.mem, g, f, q);
        CHECK(!r.ok && r.message.find("outside FC27.exe") != std::string::npos && none_called(), "morale: Create outside the image -> nothing created");
        r = run(w.mem, g, w.fns(), req_for(kActionVeryHappy, 1004));
        CHECK(r.ok && r.created && g.create_calls == 1, "morale: all checks pass -> created");
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
