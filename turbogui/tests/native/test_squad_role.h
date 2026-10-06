// Native tests: the "squad_role" game call (core/squad_role.h, docs/re/player_status_roles.md sections 3 and 7.2): the PlayerStatusManager's
// fixed table of 52 entries on MoveWorld's synthetic career, the game's Find / AddPlayer / Remove behind the Caller abstraction (a fake that
// behaves like 0x147D8BADC / 0x147BE7BB4 / 0x147D90C80), every refusal before anything is written or called.
#pragma once

struct StatusWorld : MoveWorld {
    static constexpr uint64_t kPsm = 0x30140000ULL, kPsmVt = 0x14AFFB2A0ULL, kTable = 0x30141000ULL;
    static constexpr uint64_t kFnHandle = 0x147BF30C0ULL, kFnFind = 0x147D8BADCULL, kFnAdd = 0x147BE7BB4ULL, kFnRemove = 0x147D90C80ULL;
    StatusWorld() {
        using namespace turbo::roles;
        mem.map(kPsm, 0x100);
        mem.map(kPsmVt, 0x40);
        mem.map(kTable, 0x1000);
        mem.wr(kPsm, kPsmVt);
        mem.wr(kPsmVt + 8, kFnHandle);
        mem.wr(kPsm + kPsmHub, kManagers);
        slot(kTypeStatus, kPsm);
        set_entries({});
    }
    // {pid, role, promised}: the used entries in order, the rest empty {-1, 0xFF, 0, 0}
    void set_entries(const std::vector<std::array<int, 3>>& es, int team = ListWorld::kNapoli) {
        using namespace turbo::roles;
        for (int i = 0; i < kMaxEntries; ++i) {
            const uint64_t e = kTable + static_cast<uint64_t>(i) * kEntrySize;
            const bool used = i < static_cast<int>(es.size());
            mem.wr(e + kEntryPid, static_cast<int32_t>(used ? es[static_cast<size_t>(i)][0] : -1));
            mem.wr(e + kEntryRole, static_cast<uint8_t>(used ? es[static_cast<size_t>(i)][1] : 0xFF));
            mem.wr(e + kEntryPromised, static_cast<uint8_t>(used ? es[static_cast<size_t>(i)][2] : 0));
            mem.wr(e + kEntryDismissed, static_cast<uint8_t>(used && es[static_cast<size_t>(i)][0] == 1003 ? 1 : 0));
        }
        mem.wr(kPsm + kPsmTeam, static_cast<int32_t>(team));
        mem.wr(kPsm + kPsmCount, static_cast<int32_t>(es.size()));
        mem.wr(kPsm + kPsmBegin, kTable);
        mem.wr(kPsm + kPsmEnd, kTable + kTableBytes);
        mem.wr(kPsm + kPsmCap, kTable + kTableBytes);
    }
    int32_t count() {
        int32_t c = -1;
        mem.rd(kPsm + turbo::roles::kPsmCount, c);
        return c;
    }
    turbo::roles::Entry entry(int i) {
        using namespace turbo::roles;
        Entry en;
        const uint64_t e = kTable + static_cast<uint64_t>(i) * kEntrySize;
        mem.rd(e + kEntryPid, en.pid);
        mem.rd(e + kEntryRole, en.role);
        mem.rd(e + kEntryPromised, en.promised);
        mem.rd(e + kEntryDismissed, en.dismissed);
        return en;
    }
    int index_of(int pid) {
        for (int i = 0; i < count(); ++i)
            if (entry(i).pid == pid) return i;
        return -1;
    }
    turbo::roles::Fns fns() const {
        turbo::roles::Fns f;
        f.psm_vtable = kPsmVt;
        f.psm_handle_event = kFnHandle;
        f.find = kFnFind;
        f.is_player_in_team = kFnInTeam;
        f.um_vtable = kUmVt;
        f.add_player = kFnAdd;
        f.remove = kFnRemove;
        return f;
    }
};

// The game's functions over the fake table, as FC27.exe does them
struct FakeStatusGame : turbo::roles::Caller {
    StatusWorld& w;
    std::set<int> squad;     // pids IsPlayerInTeam(pid, 48) answers true for
    int find_calls = 0, add_calls = 0, remove_calls = 0, last_add_role = -1;
    int computed_role = 4;   // what ComputeSquadRole gives for AddPlayer(.., 0xFF)
    bool find_blind = false; // Find never finds
    bool add_refuses = false, add_wrong_role = false;
    explicit FakeStatusGame(StatusWorld& world) : w(world) {}
    bool in_team(uint64_t, int pid, int team, bool& in, std::string&) override {
        in = team == ListWorld::kNapoli && squad.count(pid);
        return true;
    }
    // 0x147D8BADC: entries 0..51 of [sub+8], stops at pid -1
    bool find(uint64_t sub, int pid, uint64_t& entry, std::string&) override {
        ++find_calls;
        entry = 0;
        if (find_blind) return true;
        uint64_t b = 0;
        w.mem.rd(sub + 8, b);
        for (int i = 0; i < 52; ++i) {
            int32_t p = 0;
            w.mem.rd(b + static_cast<uint64_t>(i) * 8, p);
            if (p == pid) {
                entry = b + static_cast<uint64_t>(i) * 8;
                break;
            }
            if (p == -1) break;
        }
        return true;
    }
    // 0x147BE7BB4: AddEntry (refuses at 52, no duplicate check), role 1..5 promised 1, 0 / 0xFF computed promised 0, dismissed 0
    bool add_player(uint64_t psm, int pid, int role, bool& added, std::string&) override {
        ++add_calls;
        last_add_role = role;
        added = false;
        const uint64_t sub = psm + 0x10;
        int32_t count = 0;
        uint64_t b = 0;
        w.mem.rd(sub + 4, count);
        w.mem.rd(sub + 8, b);
        if (add_refuses || count >= 52) return true;
        const uint64_t e = b + static_cast<uint64_t>(count) * 8;
        w.mem.wr(e, static_cast<int32_t>(pid));
        w.mem.wr(sub + 4, count + 1);
        const bool automatic = role == 0 || role == 0xFF;
        const int stored = automatic ? computed_role : (add_wrong_role ? 1 : role);
        w.mem.wr(e + 4, static_cast<uint8_t>(stored));
        w.mem.wr(e + 5, static_cast<uint8_t>(automatic ? 0 : 1));
        w.mem.wr(e + 6, static_cast<uint8_t>(0));
        added = true;
        return true;
    }
    // 0x147D90C80: memmove the entries after it down one slot, count--, an empty entry at the end
    bool remove(uint64_t sub, int pid, bool& removed, std::string&) override {
        ++remove_calls;
        removed = false;
        int32_t count = 0;
        uint64_t b = 0;
        w.mem.rd(sub + 4, count);
        w.mem.rd(sub + 8, b);
        for (int i = 0; i < count; ++i) {
            int32_t p = 0;
            w.mem.rd(b + static_cast<uint64_t>(i) * 8, p);
            if (p != pid) continue;
            for (int j = i; j < 51; ++j) {
                uint64_t q = 0;
                w.mem.rd(b + static_cast<uint64_t>(j + 1) * 8, q);
                w.mem.wr(b + static_cast<uint64_t>(j) * 8, q);
            }
            w.mem.wr(b + 51 * 8, static_cast<int32_t>(-1));
            w.mem.wr(b + 51 * 8 + 4, static_cast<uint8_t>(0xFF));
            w.mem.wr(b + 51 * 8 + 5, static_cast<uint8_t>(0));
            w.mem.wr(b + 51 * 8 + 6, static_cast<uint8_t>(0));
            w.mem.wr(sub + 4, count - 1);
            removed = true;
            break;
        }
        return true;
    }
};

static void test_squad_role() {
    using namespace turbo::roles;
    std::printf("-- squad_role: the PlayerStatusManager table through the game's Find / AddPlayer / Remove\n");
    auto req_for = [](int action, int pid, int role = 0) {
        Request q;
        q.action = action;
        q.player = pid;
        q.role = role;
        q.comm = ListWorld::kComm;
        return q;
    };
    CHECK(std::string(role_name(3)) == "Rotation" && std::string(role_name(5)) == "Prospect" && std::string(role_name(0xFF)) == "none",
          "squad_role: role names");
    {
        // entry found: ONE byte, the flags after it stay; unchanged when it is the role already; check reads only
        StatusWorld w;
        w.set_entries({{1001, 4, 1}, {1002, 3, 0}, {1003, 2, 1}});
        FakeStatusGame g(w);
        g.squad = {1001, 1002, 1003, 1004};
        Result r = run(w.mem, g, w.fns(), req_for(kActionSet, 1003, 3));
        CHECK(r.ok && r.written && r.how == kHowWritten && r.role == 3 && r.before == 2, "squad_role: existing entry -> role byte written: " + r.message);
        Entry e = w.entry(2);
        CHECK(e.pid == 1003 && e.role == 3 && e.promised == 1 && e.dismissed == 1, "squad_role: wasPromised / renewalDismissed kept");
        CHECK(r.message.find("Important -> Rotation") != std::string::npos, "squad_role: the text names before / after: " + r.message);
        CHECK(g.add_calls == 0 && w.count() == 3, "squad_role: no entry added for a player who has one");
        CHECK(out1(req_for(kActionSet, 1003, 3), r) == kHowWritten, "squad_role: out[1] = written");
        r = run(w.mem, g, w.fns(), req_for(kActionSet, 1002, 3));
        CHECK(r.ok && !r.written && r.how == kHowUnchanged && r.role == 3, "squad_role: same role -> nothing written");
        r = run(w.mem, g, w.fns(), req_for(kActionCheck, 1001));
        CHECK(r.ok && r.role == 4 && r.count == 3 && !r.written && w.entry(0).role == 4, "squad_role: check reads the role only");
        CHECK(out1(req_for(kActionCheck, 1001), r) == 3, "squad_role: check -> out[1] = used entries");
        r = run(w.mem, g, w.fns(), req_for(kActionEnsure, 1001));
        CHECK(r.ok && r.how == kHowUnchanged && g.add_calls == 0 && w.entry(0).role == 4, "squad_role: ensure leaves an existing entry alone");
        r = run(w.mem, g, w.fns(), req_for(kActionCheck, 1004));
        CHECK(r.ok && r.role == kNoEntry && g.add_calls == 0 && w.count() == 3, "squad_role: check of a player without an entry: -2, nothing added");
    }
    {
        // no entry, code 1: the game's AddPlayer with the role, wasPromised back to 0, appended after the used entries; no duplicate later
        StatusWorld w;
        w.set_entries({{1001, 3, 1}, {1002, 3, 0}});
        FakeStatusGame g(w);
        g.squad = {1001, 1002, 1004};
        Result r = run(w.mem, g, w.fns(), req_for(kActionSet, 1004, 5));
        CHECK(r.ok && r.created && r.how == kHowCreated && r.role == 5 && r.count == 3, "squad_role: missing entry created: " + r.message);
        CHECK(g.add_calls == 1 && g.last_add_role == 5, "squad_role: AddPlayer called once with the role");
        Entry e = w.entry(2);
        CHECK(e.pid == 1004 && e.role == 5 && e.promised == 0 && e.dismissed == 0, "squad_role: entry {pid, role, not promised, 0} after the used ones");
        CHECK(w.entry(3).pid == -1, "squad_role: the slot after stays empty");
        CHECK(r.message.find("entry created with Prospect") != std::string::npos, "squad_role: the text says created: " + r.message);
        r = run(w.mem, g, w.fns(), req_for(kActionSet, 1004, 3));
        CHECK(r.ok && r.how == kHowWritten && g.add_calls == 1 && w.count() == 3 && w.entry(2).role == 3, "squad_role: second run finds it, no duplicate");
        // AddPlayer refuses / stores another role: reported
        g.squad.insert(1005);
        g.add_refuses = true;
        r = run(w.mem, g, w.fns(), req_for(kActionSet, 1005, 3));
        CHECK(!r.ok && r.stage == "check" && r.role == kNoEntry && w.count() == 3, "squad_role: AddPlayer refused -> reported: " + r.message);
        g.add_refuses = false;
        g.add_wrong_role = true;
        r = run(w.mem, g, w.fns(), req_for(kActionSet, 1005, 3));
        CHECK(!r.ok && r.stage == "check" && r.message.find("stored squad role Crucial") != std::string::npos, "squad_role: a wrong stored role is reported");
    }
    {
        // code 2: AddPlayer(0xFF): the game computes the role (wasPromised 0 by the game)
        StatusWorld w;
        w.set_entries({{1001, 3, 1}});
        FakeStatusGame g(w);
        g.squad = {1001, 1006};
        Result r = run(w.mem, g, w.fns(), req_for(kActionEnsure, 1006));
        CHECK(r.ok && r.created && r.role == 4 && g.last_add_role == 0xFF, "squad_role: ensure -> AddPlayer(0xFF), the game's role: " + r.message);
        Entry e = w.entry(1);
        CHECK(e.pid == 1006 && e.role == 4 && e.promised == 0, "squad_role: computed entry stored");
        CHECK(r.message.find("the game chose Sporadic") != std::string::npos, "squad_role: the text names the game's role: " + r.message);
    }
    {
        // a full table: codes 1 / 2 refuse; code 4 removes the entry of a player who left through the game's Remove; then room for one
        StatusWorld w;
        std::vector<std::array<int, 3>> full;
        for (int i = 0; i < 52; ++i) full.push_back({2000 + i, 3, i % 2});
        w.set_entries(full);
        FakeStatusGame g(w);
        for (int i = 2; i < 52; ++i) g.squad.insert(2000 + i);  // 2000 and 2001 left the club
        g.squad.insert(1007);
        Result r = run(w.mem, g, w.fns(), req_for(kActionSet, 1007, 3));
        CHECK(!r.ok && r.role == kNoEntry && g.add_calls == 0 && r.message.find("table is full") != std::string::npos, "squad_role: full -> not created: " + r.message);
        r = run(w.mem, g, w.fns(), req_for(kActionEnsure, 1007));
        CHECK(!r.ok && g.add_calls == 0, "squad_role: full -> ensure refused too");
        r = run(w.mem, g, w.fns(), req_for(kActionStale, 0));
        CHECK(r.ok && r.stale == 2 && r.role == 2 && out1(req_for(kActionStale, 0), r) == 52 && g.remove_calls == 0, "squad_role: 2 stale of 52 counted only");
        r = run(w.mem, g, w.fns(), req_for(kActionRemoveStale, 2005));
        CHECK(!r.ok && g.remove_calls == 0 && r.message.find("is at your club") != std::string::npos, "squad_role: a current player's entry is never removed");
        r = run(w.mem, g, w.fns(), req_for(kActionRemoveStale, 2001));
        CHECK(r.ok && r.removed && r.role == 1 && w.count() == 51 && w.index_of(2001) < 0 && g.remove_calls == 1, "squad_role: stale entry removed: " + r.message);
        CHECK(w.entry(1).pid == 2002 && w.entry(1).promised == 0 && w.entry(51).pid == -1, "squad_role: the entries after it moved down, an empty one last");
        r = run(w.mem, g, w.fns(), req_for(kActionRemoveStale, 2001));
        CHECK(r.ok && !r.removed && r.role == 0 && g.remove_calls == 1, "squad_role: no entry -> nothing to remove");
        r = run(w.mem, g, w.fns(), req_for(kActionSet, 1007, 2));
        CHECK(r.ok && r.created && w.count() == 52 && w.entry(51).pid == 1007 && w.entry(51).role == 2, "squad_role: then the entry is created in the freed slot");
    }
    {
        // creation / removal off: kill switch, AddPlayer or Remove not resolved
        StatusWorld w;
        w.set_entries({{1001, 3, 0}, {3001, 3, 0}});
        FakeStatusGame g(w);
        g.squad = {1001, 1008};
        Request q = req_for(kActionSet, 1008, 3);
        q.create_off = true;
        Result r = run(w.mem, g, w.fns(), q);
        CHECK(!r.ok && r.role == kNoEntry && g.add_calls == 0 && r.message.find(create_kill_switch_name()) != std::string::npos,
              "squad_role: create kill switch -> count only: " + r.message);
        q = req_for(kActionRemoveStale, 3001);
        q.create_off = true;
        r = run(w.mem, g, w.fns(), q);
        CHECK(!r.ok && g.remove_calls == 0 && w.count() == 2, "squad_role: create kill switch -> no removal either");
        Fns f = w.fns();
        f.add_player = 0;
        r = run(w.mem, g, f, req_for(kActionSet, 1008, 3));
        CHECK(!r.ok && r.role == kNoEntry && g.add_calls == 0 && r.message.find("psm_add_player") != std::string::npos, "squad_role: no AddPlayer -> count only");
        r = run(w.mem, g, f, req_for(kActionSet, 1001, 5));
        CHECK(r.ok && w.entry(0).role == 5, "squad_role: an existing entry is still written without AddPlayer");
        f = w.fns();
        f.remove = 0;
        r = run(w.mem, g, f, req_for(kActionRemoveStale, 3001));
        CHECK(!r.ok && g.remove_calls == 0 && r.message.find("psm_remove_entry") != std::string::npos, "squad_role: no Remove -> nothing removed");
        const std::filesystem::path dir = g_out / "squad_role_kill";
        std::filesystem::create_directories(dir);
        std::filesystem::remove(dir / kill_switch_name());
        std::filesystem::remove(dir / create_kill_switch_name());
        CHECK(!killed(dir) && !create_killed(dir), "squad_role: on without the kill switches");
        { std::ofstream(dir / kill_switch_name()) << "off"; }
        { std::ofstream(dir / create_kill_switch_name()) << "off"; }
        CHECK(killed(dir) && create_killed(dir), "squad_role: off with turbo_output\\call_squad_role_off.txt / call_squad_role_create_off.txt");
    }
    {
        // every refusal stops before anything is written, added or removed
        StatusWorld w;
        FakeStatusGame g(w);
        g.squad = {1001, 1002, 1009};
        const std::vector<std::array<int, 3>> good = {{1001, 4, 1}, {1002, 3, 0}, {3002, 2, 0}};
        auto untouched = [&]() { return g.add_calls == 0 && g.remove_calls == 0 && w.entry(0).role == 4 && w.entry(1).role == 3; };
        auto refused = [&](const Request& q, const Fns& f, const char* what, const char* text) {
            Result r = run(w.mem, g, f, q);
            CHECK(!r.ok && untouched() && (!text || r.message.find(text) != std::string::npos), std::string("squad_role: ") + what + ": " + r.message);
        };
        w.set_entries(good);
        refused(req_for(kActionSet, 1005, 3), w.fns(), "not in the club", "not in your club");
        refused(req_for(kActionSet, 1001, 6), w.fns(), "role 6", "out of range");
        refused(req_for(kActionSet, 1001, 0), w.fns(), "role 0", "out of range");
        refused(req_for(7, 1001, 3), w.fns(), "unknown code", "unknown squad_role code");
        Fns f = w.fns();
        f.psm_vtable = 0x14AFFB2A8ULL;
        refused(req_for(kActionSet, 1001, 5), f, "vtable mismatch", "not the PlayerStatusManager");
        f = w.fns();
        f.psm_handle_event = 0x147BF30C4ULL;
        refused(req_for(kActionSet, 1001, 5), f, "slot 1 mismatch", "slot 1");
        f = w.fns();
        f.find = 0;
        refused(req_for(kActionSet, 1001, 5), f, "Find not resolved", "psm_get_entry");
        f = w.fns();
        f.add_player = 0x100ULL;
        Request q = req_for(kActionSet, 1009, 3);
        q.image_base = 0x140000000ULL;
        q.image_size = 0x10000000ULL;
        refused(q, f, "AddPlayer outside the image", "outside FC27.exe");
        w.mem.wr(StatusWorld::kPsm + kPsmHub, static_cast<uint64_t>(0x1234));
        refused(req_for(kActionSet, 1001, 5), w.fns(), "back pointer", "manager table");
        w.mem.wr(StatusWorld::kPsm + kPsmHub, MoveWorld::kManagers);
        w.set_entries(good, -1);
        refused(req_for(kActionSet, 1009, 3), w.fns(), "table not set up", "not set up yet");
        w.set_entries(good, 49);
        refused(req_for(kActionSet, 1001, 5), w.fns(), "another team's table", "belongs to team 49");
        w.set_entries(good);
        w.mem.wr(StatusWorld::kPsm + kPsmCount, static_cast<int32_t>(53));
        refused(req_for(kActionSet, 1001, 5), w.fns(), "count 53", "out of range");
        w.set_entries(good);
        w.mem.wr(StatusWorld::kPsm + kPsmEnd, StatusWorld::kTable + kTableBytes - 8);
        refused(req_for(kActionSet, 1001, 5), w.fns(), "not 52 entries", "fixed table");
        w.set_entries(good);
        w.mem.wr(StatusWorld::kPsm + kPsmCap, StatusWorld::kTable + 0x400);
        refused(req_for(kActionSet, 1001, 5), w.fns(), "capacity", "fixed table");
        w.set_entries(good);
        w.mem.wr(StatusWorld::kTable + 2 * kEntrySize, static_cast<int32_t>(-1));
        refused(req_for(kActionSet, 1001, 5), w.fns(), "a used entry without a player", "holds player id -1");
        w.set_entries({{1001, 4, 1}, {1002, 3, 0}, {1001, 2, 0}});
        refused(req_for(kActionSet, 1002, 5), w.fns(), "a pid twice", "two squad role entries");
        w.set_entries(good);
        w.mem.wr(StatusWorld::kTable + 2 * kEntrySize + kEntryRole, static_cast<uint8_t>(9));
        refused(req_for(kActionSet, 1001, 5), w.fns(), "role byte 9", "holds role 9");
        w.set_entries(good);
        w.mem.wr(StatusWorld::kTable + 5 * kEntrySize, static_cast<int32_t>(4444));
        refused(req_for(kActionSet, 1001, 5), w.fns(), "a player after the used entries", "-1 expected");
        w.set_entries(good);
        g.find_blind = true;
        refused(req_for(kActionSet, 1001, 5), w.fns(), "Find disagrees (finds nothing)", "nothing was written");
        g.find_blind = false;
        Result r = run(w.mem, g, w.fns(), req_for(kActionSet, 1009, 3));
        CHECK(r.ok && r.created && g.add_calls == 1 && w.count() == 4, "squad_role: all checks pass -> created");
    }
    {
        // the words
        Request q = req_for(kActionSet, 1001, 4);
        int64_t a[4];
        args_from_request(q, a);
        Request back = request_from_args(a);
        CHECK(back.action == kActionSet && back.player == 1001 && back.role == 4 && back.bad_args.empty(), "squad_role: words round-trip");
        a[1] = 0x101;
        CHECK(!request_from_args(a).bad_args.empty(), "squad_role: unknown code bits refused");
        a[1] = kActionSet;
        a[3] = 6;
        CHECK(!request_from_args(a).bad_args.empty(), "squad_role: role 6 refused in the words");
        a[1] = kActionCheck;
        CHECK(request_from_args(a).bad_args.empty(), "squad_role: the role word is ignored by code 9");
        CHECK(valid_action(kActionEnsure) && valid_action(kActionRemoveStale) && !valid_action(5), "squad_role: codes 1 2 3 4 9");
    }
}
