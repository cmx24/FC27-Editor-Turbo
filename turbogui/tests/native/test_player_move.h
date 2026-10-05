// Native tests: the "player_move" game call (core/player_move.h, docs/re/realtime_transfers.md): TeamUtil::PlayerMoved,
// PlayerContractManager::AddContractRecord and ContractTerminationManager::ReleasePlayer behind the Caller abstraction.
// Included by test_main.cpp after ListWorld and FakeListGame (the synthetic career: comm -> owner -> manager table with the
// PlayerContractManager, TransferManager, dispatcher, calendar and UserManager). MoveWorld adds the objects this call needs
// (DataController, TeamUtil, ContractTerminationManager, IniSettings, LoansManager, PlayerMoraleManager, SimDayManager, the release
// counter, the user's finance object); FakeMoveGame is a fake game whose functions mutate the fake state the way the game's do
// (teamplayerlinks, EnforceSquadLimits' auto-release and fillers, the morale entry of event 0x5F, the contract records).
#pragma once
#include <algorithm>
#include <set>

struct MoveWorld : ListWorld {
    static constexpr uint64_t kDc = 0x30100000ULL, kProvider = 0x30100200ULL, kProviderVt = 0x14972FB80ULL, kTu = 0x30100800ULL, kCtm = 0x30100900ULL,
                              kCtmVt = 0x14AFF6D58ULL, kIni = 0x30100A00ULL, kLoans = 0x30100B00ULL, kMorale = 0x30100C00ULL, kMoraleVt = 0x14B0156A8ULL,
                              kCounter = 0x30101400ULL, kSimDay = 0x30101500ULL, kFin = 0x30101600ULL, kFinVt = 0x14B0A0000ULL, kLoanBuf = 0x30110000ULL,
                              kPendBuf = 0x30120000ULL;
    static constexpr uint64_t kFnMoved = 0x147DEE368ULL, kFnInTeam = 0x147B90A8CULL, kFnCounts = 0x147B7225CULL, kFnLeague = 0x14154B92CULL,
                              kFnIntl = 0x14479F50CULL, kFnAdd = 0x147E5BF9CULL, kFnRelease = 0x147B94630ULL, kFnMoraleHandle = 0x147D8D354ULL;
    static constexpr int kFa = 111592;
    MoveWorld() {
        using namespace turbo::pm;
        mem.map(kDc, 0x100);
        mem.map(kProvider, 0x100);
        mem.map(kProviderVt, 0x40);
        mem.wr(kDc + kDcProvider, kProvider);
        mem.wr(kDc + kDcHub, kManagers);
        mem.wr(kProvider, kProviderVt);
        mem.wr(kProviderVt + 8, 0x1400B0000ULL);  // slot 1 = Execute
        slot(kTypeDataController, kDc);
        mem.map(kTu, 0x100);
        mem.wr(kTu + kTeamUtilHub, kManagers);
        slot(kTypeTeamUtil, kTu);
        mem.map(kCtm, 0x100);
        mem.map(kCtmVt, 0x40);
        mem.wr(kCtm, kCtmVt);
        mem.wr(kCtm + turbo::tl::kMgrManagers, kManagers);
        slot(kTypeCtm, kCtm);
        mem.map(kIni, 0x100);
        mem.wr(kIni + kIniMax, static_cast<int32_t>(52));
        mem.wr(kIni + kIniMin, static_cast<int32_t>(18));
        slot(kTypeIni, kIni);
        mem.map(kLoans, 0x100);
        mem.map(kLoanBuf, 0x8000);
        mem.map(kPendBuf, 0x4000);
        mem.wr(kLoans, 0x140003000ULL);  // a vtable-shaped first word
        slot(kTypeLoans, kLoans);
        set_loans({});
        set_pending({});
        mem.map(kMorale, 0x700);
        mem.map(kMoraleVt, 0x40);
        mem.wr(kMorale, kMoraleVt);
        mem.wr(kMoraleVt + 8, kFnMoraleHandle);
        mem.wr(kMorale + kMoraleGate, static_cast<uint8_t>(0));
        slot(kTypeMorale, kMorale);
        mem.map(kCounter, 0x100);
        slot(kTypeReleaseCounter, kCounter);
        mem.map(kSimDay, 0x100);
        mem.wr(kSimDay + 0x14, static_cast<int32_t>(0));
        slot(103, kSimDay);
        mem.map(kFin, 0x100);
        mem.map(kFinVt, 0x40);
        mem.wr(kFin, kFinVt);
        for (int i = 0; i < 3; ++i) mem.wr(kFinVt + 8 * static_cast<uint64_t>(i), 0x1400C0000ULL + static_cast<uint64_t>(i) * 0x100);
        mem.wr(kUsers + kUserFinance, kFin);
        const uint64_t cal = calendar();
        mem.wr(cal + turbo::tl::kCalendarDate, static_cast<int32_t>(1));
        mem.wr(cal + turbo::tl::kCalendarDate + 4, static_cast<int32_t>(7));
        mem.wr(cal + turbo::tl::kCalendarDate + 8, static_cast<int32_t>(2026));
    }
    static constexpr uint64_t kUserFinance = turbo::pm::kUserFinance;
    uint64_t calendar() { return mem.chain(kManagers + 0x20 * static_cast<uint64_t>(turbo::tl::kTypeCalendarManager), {turbo::tl::kSlotHolder, 0}); }
    uint64_t dispatcher() { return mem.chain(kManagers + 0x20 * static_cast<uint64_t>(turbo::tl::kTypeDispatcher), {turbo::tl::kSlotHolder, 0}); }
    // the LoansManager's runtime loan list: {+0 pid, +4 returning club}, 0x20 bytes each
    void set_loans(const std::vector<std::pair<int, int>>& loans) {
        for (size_t i = 0; i < loans.size(); ++i) {
            mem.wr(kLoanBuf + i * turbo::pm::kLoanRecord, static_cast<int32_t>(loans[i].first));
            mem.wr(kLoanBuf + i * turbo::pm::kLoanRecord + 4, static_cast<int32_t>(loans[i].second));
        }
        mem.wr(kLoans + turbo::pm::kLoansBegin, kLoanBuf);
        mem.wr(kLoans + turbo::pm::kLoansEnd, kLoanBuf + loans.size() * turbo::pm::kLoanRecord);
    }
    // the TransferManager's pre-signed deals: {+4 buyer team}, 0x88 bytes each
    void set_pending(const std::vector<int>& buyers) {
        for (size_t i = 0; i < buyers.size(); ++i) mem.wr(kPendBuf + i * turbo::pm::kTmPendingRecord + 4, static_cast<int32_t>(buyers[i]));
        mem.wr(kTm + turbo::pm::kTmPendingBegin, kPendBuf);
        mem.wr(kTm + turbo::pm::kTmPendingEnd, kPendBuf + buyers.size() * turbo::pm::kTmPendingRecord);
    }
    int count_of(uint64_t begin_off_obj, uint64_t b_off, uint64_t e_off, uint64_t rec, int team) {
        uint64_t b = 0, e = 0;
        mem.rd(begin_off_obj + b_off, b);
        mem.rd(begin_off_obj + e_off, e);
        int n = 0;
        for (uint64_t p = b; p + rec <= e; p += rec) {
            int32_t t = 0;
            mem.rd(p + 4, t);
            if (t == team) ++n;
        }
        return n;
    }
    int loans_of(int team) { return count_of(kLoans, turbo::pm::kLoansBegin, turbo::pm::kLoansEnd, turbo::pm::kLoanRecord, team); }
    int pending_of(int team) { return count_of(kTm, turbo::pm::kTmPendingBegin, turbo::pm::kTmPendingEnd, turbo::pm::kTmPendingRecord, team); }
    int32_t ini(uint64_t off) {
        int32_t v = 0;
        mem.rd(kIni + off, v);
        return v;
    }
    int max_squad() { return ini(turbo::pm::kIniMax); }
    int min_squad() { return ini(turbo::pm::kIniMin); }
    // the contract record of a player (team, wage), found = false without one
    bool record(int pid, int32_t& team, int32_t& wage) {
        bool found = false;
        int32_t st = 0;
        uint64_t node = 0;
        turbo::tl::contract_status(mem, kPcm, pid, found, st, &node);
        if (!found) return false;
        mem.rd(node + turbo::pm::kPcmNodeTeam, team);
        mem.rd(node + turbo::pm::kPcmNodeWage, wage);
        return true;
    }
};

struct FakeMoveGame : turbo::pm::Caller {
    MoveWorld& w;
    std::map<int, std::set<int>> teams;  // pid -> the teams whose teamplayerlinks hold him
    std::map<int, int> league;           // team -> leagueid (absent = the game knows none: -1)
    std::vector<std::string> calls;      // every call into the game, in order
    std::vector<int> counts_asked;       // the teams SquadCounts was asked about
    int reads = 0, moves = 0, adds = 0, releases = 0;
    int join_writes = 0;                 // PlayerMoved writes the join date and the previous team BEFORE it checks anything
    int auto_released = 0, fillers = 0;  // EnforceSquadLimits' side effects
    std::set<int> morale;                // morale entries: event 0x5F creates one for an arrival at the user's club while the gate is 0
    int release_pool = MoveWorld::kFa;   // the pool ReleasePlayer picks
    bool fail_moved = false, noop_moved = false, half_moved = false, fail_add = false, noop_add = false, wrong_add = false, fail_release = false,
         noop_release = false, budget_ok = true, extra_drop = false, fail_read = false;
    int force_release_code = -1;
    struct Moved { uint64_t tu = 0; int pid = 0, from = 0, to = 0; } last_moved;
    struct Add { uint64_t pcm = 0; int pid = 0, team = 0, months = 0, wage = 0, k = 0; uint64_t date = 0; int status = -1; } last_add;
    uint64_t last_dc = 0, last_ctm = 0;
    explicit FakeMoveGame(MoveWorld& world) : w(world) {}
    // ---- the fake database
    void put(int pid, int team) {
        teams[pid].insert(team);
        if (team == ListWorld::kNapoli && w.status(pid) == -1) w.add_player(pid, 0);
    }
    void put_many(int first, int n, int team) {
        for (int i = 0; i < n; ++i) put(first + i, team);
    }
    int rows(int team) const {
        int n = 0;
        for (const auto& kv : teams)
            if (kv.second.count(team)) ++n;
        return n;
    }
    int calls_total() const { return static_cast<int>(calls.size()); }
    int count_calls(const char* what) const { return static_cast<int>(std::count(calls.begin(), calls.end(), std::string(what))); }
    // ---- the game's functions
    bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) override {
        ++reads;
        calls.push_back("in_team");
        last_dc = dc;
        if (fail_read) {
            err = "boom";
            return false;
        }
        const auto it = teams.find(pid);
        in = it != teams.end() && it->second.count(team) > 0;
        return true;
    }
    bool squad_counts(uint64_t dc, int team, int& r, int& loaned, int& pending, std::string& err) override {
        ++reads;
        calls.push_back("squad_counts");
        counts_asked.push_back(team);
        last_dc = dc;
        if (fail_read) {
            err = "boom";
            return false;
        }
        if (team <= 0) return true;  // the game writes nothing for a team <= 0
        r = rows(team);
        loaned = w.loans_of(team);
        pending = w.pending_of(team);
        return true;
    }
    bool league_of_team(uint64_t dc, int team, int& lg, std::string& err) override {
        ++reads;
        calls.push_back("league");
        last_dc = dc;
        if (fail_read) {
            err = "boom";
            return false;
        }
        const auto it = league.find(team);
        lg = it == league.end() ? -1 : it->second;
        return true;
    }
    bool is_international(int lg, bool& out, std::string& err) override {
        ++reads;
        calls.push_back("intl");
        if (fail_read) {
            err = "boom";
            return false;
        }
        out = lg == 78 || lg == 2136 || lg == 3004;
        return true;
    }
    // EnforceSquadLimits 0x147B99BFC (skips Free Agents, the youth pool and the pseudo teams on both sides)
    void enforce(int pid, int from, int to) {
        const int mx = w.max_squad(), mn = w.min_squad();
        if (to != MoveWorld::kFa) {
            while (rows(to) + w.loans_of(to) + w.pending_of(to) > mx) {
                int victim = -1;  // the "lowest-value reserve": the lowest id other than the arrival
                for (const auto& kv : teams)
                    if (kv.first != pid && kv.second.count(to) && (victim < 0 || kv.first < victim)) victim = kv.first;
                if (victim < 0) break;
                teams[victim].erase(to);
                teams[victim].insert(MoveWorld::kFa);
                ++auto_released;
            }
        }
        if (from != MoveWorld::kFa && rows(from) < mn) fillers += mn - rows(from);
    }
    bool player_moved(uint64_t tu, int pid, int from, int to, std::string& err) override {
        ++moves;
        calls.push_back("moved");
        last_moved = {tu, pid, from, to};
        if (fail_moved) {
            err = "boom";
            return false;
        }
        ++join_writes;  // SetPlayerJoinDate + SetPreviousTeam: before the link check
        if (noop_moved) return true;
        auto& t = teams[pid];
        if (!t.count(from) || t.count(to)) return true;  // MovePlayerLink: a no-op unless he is in `from` and not in `to`
        if (!half_moved) t.erase(from);
        t.insert(to);
        // event 0x60 / 0x5F listeners: the user's club drops his contract record when he leaves; an arrival gets a morale entry (gate 0)
        if (from == ListWorld::kNapoli) {
            bool found = false;
            int32_t st = 0;
            uint64_t node = 0;
            turbo::tl::contract_status(w.mem, ListWorld::kPcm, pid, found, st, &node);
            if (found) w.mem.wr(node + turbo::tl::kPcmNodeKey, static_cast<int32_t>(0));
            morale.erase(pid);
        }
        uint8_t gate = 0;
        w.mem.rd(MoveWorld::kMorale + turbo::pm::kMoraleGate, gate);
        if (to == ListWorld::kNapoli && gate == 0) morale.insert(pid);
        enforce(pid, from, to);
        if (extra_drop) {  // a broken game that also drops one more reserve of `to`
            for (auto& kv : teams)
                if (kv.first != pid && kv.second.count(to)) {
                    kv.second.erase(to);
                    break;
                }
        }
        return true;
    }
    bool add_contract(uint64_t pcm, int pid, int team, int months, int wage, int k, uint64_t date, int status, std::string& err) override {
        ++adds;
        calls.push_back("add");
        last_add = {pcm, pid, team, months, wage, k, date, status};
        if (fail_add) {
            err = "boom";
            return false;
        }
        if (noop_add) return true;
        bool found = false;
        int32_t st = 0;
        uint64_t node = 0;
        turbo::tl::contract_status(w.mem, ListWorld::kPcm, pid, found, st, &node);
        if (!found) node = w.add_player(pid, status);
        w.mem.wr(node + turbo::tl::kPcmNodeStatus, static_cast<int32_t>(status));
        w.mem.wr(node + turbo::pm::kPcmNodeTeam, static_cast<int32_t>(wrong_add ? team + 1 : team));
        w.mem.wr(node + turbo::pm::kPcmNodeWage, static_cast<int32_t>(wage));
        return true;
    }
    bool release_player(uint64_t ctm, int pid, int& code, std::string& err) override {
        ++releases;
        calls.push_back("release");
        last_ctm = ctm;
        if (fail_release) {
            err = "boom";
            return false;
        }
        const int from = ListWorld::kNapoli;  // GetPlayerTeam: his club
        if (force_release_code >= 0) {
            code = force_release_code;
            return true;
        }
        if (!budget_ok) {
            code = 1;
            return true;
        }
        if (rows(from) - 1 < w.min_squad()) {
            code = 2;
            return true;
        }
        code = 0;
        if (noop_release) return true;
        teams[pid].erase(from);
        teams[pid].insert(release_pool);
        bool found = false;
        int32_t st = 0;
        uint64_t node = 0;
        turbo::tl::contract_status(w.mem, ListWorld::kPcm, pid, found, st, &node);
        if (found) w.mem.wr(node + turbo::tl::kPcmNodeKey, static_cast<int32_t>(0));
        morale.erase(pid);
        return true;
    }
};

// The standard career: Napoli (48, the user) / team 12 / team 13 with 25 players each, 5 free agents, national teams 1370 (men) /
// 1371 (women) / 1372 (league 3004), 1373 knows no league
static void seed_move_world(MoveWorld& w, FakeMoveGame& g) {
    g.put_many(1000, 25, ListWorld::kNapoli);
    g.put_many(2000, 25, 12);
    g.put_many(3000, 25, 13);
    g.put_many(9000, 5, MoveWorld::kFa);
    g.league[ListWorld::kNapoli] = 31;
    g.league[12] = 31;
    g.league[13] = 13;
    g.league[1370] = 78;
    g.league[1371] = 2136;
    g.league[1372] = 3004;
    g.league[MoveWorld::kFa] = 76;
    g.teams[1001].insert(1370);  // a called-up player is in his club AND his national team
    g.teams[2001].insert(1370);
    (void)w;
}

static turbo::pm::Fns move_fns() {
    turbo::pm::Fns f;
    f.player_moved = MoveWorld::kFnMoved;
    f.is_player_in_team = MoveWorld::kFnInTeam;
    f.squad_counts = MoveWorld::kFnCounts;
    f.league_of_team = MoveWorld::kFnLeague;
    f.is_international = MoveWorld::kFnIntl;
    f.add_contract = MoveWorld::kFnAdd;
    f.release_player = MoveWorld::kFnRelease;
    f.pcm_vtable = ListWorld::kPcmVt;
    f.tm_vtable = ListWorld::kTmVt;
    f.um_vtable = ListWorld::kUmVt;
    f.ctm_vtable = MoveWorld::kCtmVt;
    f.morale_vtable = MoveWorld::kMoraleVt;
    f.morale_handle_event = MoveWorld::kFnMoraleHandle;
    return f;
}

static turbo::pm::Request move_req(int action, int pid, int from, int to, int months = 0, int wage = 0) {
    turbo::pm::Request q;
    q.action = action;
    q.player = pid;
    q.from = from;
    q.to = to;
    q.months = months;
    q.wage = wage;
    q.comm = ListWorld::kComm;
    q.managers = ListWorld::kManagers;
    return q;
}


static void test_player_move() {
    using namespace turbo;
    using namespace turbo::pm;
    const Fns fns = move_fns();
    constexpr int kUser = ListWorld::kNapoli, kFa = MoveWorld::kFa;
    auto has = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
    auto index_of = [](const std::vector<std::string>& v, const char* what) {
        const auto it = std::find(v.begin(), v.end(), std::string(what));
        return it == v.end() ? -1 : static_cast<int>(it - v.begin());
    };

    run_case("player_move: codes, names, constants and the op number (the Lua-visible contract)", [&] {
        CHECK(kActionMove == 1 && kActionRelease == 2 && kActionCheck == 9, "codes 1 move, 2 release, 9 check only");
        CHECK(valid_action(1) && valid_action(2) && valid_action(9) && !valid_action(0) && !valid_action(3) && !valid_action(8) && !valid_action(10) &&
                  !valid_action(-1),
              "only 1 / 2 / 9 are actions");
        CHECK(std::string(action_name(1)) == "move" && std::string(action_name(2)) == "release" && std::string(action_name(9)) == "check" &&
                  std::string(action_name(3)) == "unknown action",
              "names");
        CHECK(kCallOpPlayerMove == 11 && kCallOpPlayerMove != kCallOpTransferList && kCallOpPlayerMove != kCallOpJobOffer && kCallOpPlayerMove != kCallOpReveal,
              "op 11 is the one after transfer_list's 10 and clashes with no other op");
        CHECK(kTeamFreeAgents == 111592 && kTeamFreeAgents == 0x1B3E8 && kPlayerCareer == 31399, "special ids");
        CHECK(!is_special_team(kTeamFreeAgents) && is_special_team(0x1B688) && is_special_team(0x1B29D) && is_special_team(0x1B72C) && is_special_team(0x20128) &&
                  is_special_team(0x20260) && !is_special_team(48),
              "Free Agents is allowed, the pseudo teams and the other pools are not");
        CHECK(std::string(kill_switch_name()) == "call_player_move_off.txt", "kill switch file name");
        CHECK(std::string(Fns().missing(kActionMove)) == "teamutil_player_moved" && fns.missing(kActionMove) == nullptr && fns.missing(kActionRelease) == nullptr &&
                  fns.missing(kActionCheck) == nullptr,
              "missing() names the first absent entry, a complete table has none");
    });

    run_case("player_move: user <- AI club: PlayerMoved, then the contract record, read-backs (the whole sequence)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2001, 12, kUser, 36, 25000));
        CHECK(r.ok && r.stage == "done" && r.called && r.contract_called && r.release_code == -1, "ok: " + r.message);
        CHECK(r.from_ok == 1 && r.to_ok == 1, "the two read-backs: left 12, in 48");
        CHECK(g.moves == 1 && g.adds == 1 && g.releases == 0, "PlayerMoved once, AddContractRecord once, no release");
        CHECK(g.last_moved.tu == MoveWorld::kTu && g.last_moved.pid == 2001 && g.last_moved.from == 12 && g.last_moved.to == kUser,
              "PlayerMoved(teamUtil, pid, from, to) on the hub's TeamUtil");
        CHECK(g.last_add.pcm == ListWorld::kPcm && g.last_add.pid == 2001 && g.last_add.team == kUser && g.last_add.months == 36 && g.last_add.wage == 25000 &&
                  g.last_add.k == 100 && g.last_add.date == w.calendar() + turbo::tl::kCalendarDate && g.last_add.status == 0,
              "AddContractRecord(pcm, pid, team, months, wage, 100, &calendar+0x34, 0)");
        CHECK(index_of(g.calls, "moved") >= 0 && index_of(g.calls, "add") > index_of(g.calls, "moved"), "the record comes after the move");
        CHECK(g.last_dc == MoveWorld::kDc, "the reads ran on the hub's DataController");
        CHECK(g.teams[2001] == std::set<int>({kUser, 1370}), "his club link moved (the national-team link is not touched)");
        CHECK(g.rows(kUser) == 26 && g.rows(12) == 24, "squads 26 / 24");
        int32_t team = 0, wage = 0;
        CHECK(w.record(2001, team, wage) && team == kUser && wage == 25000, "the PCM record holds the wage and the club");
        CHECK(g.morale.count(2001) == 1 && g.auto_released == 0 && g.fillers == 0, "morale entry created, the game had nothing to clean up");
        CHECK(has(r.message, "moved from team 12 to team 48") && has(r.message, "contract record added: 36 months, wage 25000") && !has(r.message, "WARNING"),
              "message: " + r.message);
        CHECK(r.at.dc == MoveWorld::kDc && r.at.team_util == MoveWorld::kTu && r.at.pcm == ListWorld::kPcm && r.at.managers == ListWorld::kManagers &&
                  r.at.user_team == kUser && r.at.max_squad == 52 && r.at.min_squad == 18 && r.at.morale_gate == 0 &&
                  r.at.date == w.calendar() + turbo::tl::kCalendarDate && r.at.ctm == 0,
              "the located objects (no CTM for a move)");
        CHECK(r.rows_to == 25 && r.loaned_to == 0 && r.pending_to == 0 && r.rows_from == 25, "squad counts read before the call");
        CHECK(w.mem.failed_reads == 0, "no read outside the synthetic memory");
    });

    run_case("player_move: AI <- AI, user -> AI and Free Agents on either side (no contract record unless the user signs)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        // AI <- AI: months / wage are ignored (the record exists for the user's club only)
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13, 24, 3000));
        CHECK(r.ok && r.called && !r.contract_called && r.from_ok == 1 && r.to_ok == 1 && g.adds == 0, "AI <- AI: " + r.message);
        CHECK(has(r.message, "moved from team 12 to team 13") && has(r.message, "months / wage ignored") && g.teams[2003] == std::set<int>({13}), "text and link");
        CHECK(g.morale.count(2003) == 0 && w.status(2003) == -1, "no morale entry, no record for an AI club's player");
        // user -> AI: his record is dropped by the game's own 0x60 listeners (the fake does it), Turbo adds nothing
        CHECK(w.status(1003) == 0, "he has a record at the user's club");
        r = run(w.mem, g, fns, move_req(kActionMove, 1003, kUser, 12));
        CHECK(r.ok && r.called && !r.contract_called && g.teams[1003] == std::set<int>({12}) && w.status(1003) == -1 && g.rows(kUser) == 24, "user -> AI: " + r.message);
        // free-agent signing by the user
        g.counts_asked.clear();
        r = run(w.mem, g, fns, move_req(kActionMove, 9001, kFa, kUser, 24, 5000));
        CHECK(r.ok && r.contract_called && g.teams[9001] == std::set<int>({kUser}) && g.rows(kFa) == 4 && has(r.message, "moved from Free Agents to team 48"),
              "free agent -> user: " + r.message);
        CHECK(g.counts_asked == std::vector<int>({kUser, kUser}),
              "SquadCounts asked about the club only (before and after), never about Free Agents (EnforceSquadLimits skips it)");
        int32_t team = 0, wage = 0;
        CHECK(w.record(9001, team, wage) && team == kUser && wage == 5000, "his record");
        // free agent -> AI, AI -> free agent (no limit on the Free Agents side), user -> free agent
        r = run(w.mem, g, fns, move_req(kActionMove, 9002, kFa, 13));
        CHECK(r.ok && g.teams[9002] == std::set<int>({13}) && g.rows(13) == 27, "free agent -> AI: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionMove, 3001, 13, kFa));
        CHECK(r.ok && g.teams[3001] == std::set<int>({kFa}) && has(r.message, "to Free Agents"), "AI -> free agent: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionMove, 1004, kUser, kFa));
        CHECK(r.ok && g.teams[1004] == std::set<int>({kFa}), "user -> free agent (a plain move: no compensation): " + r.message);
        CHECK(g.auto_released == 0 && g.fillers == 0, "the game never had to release or sign anybody");
        // many free agents: the Free Agents side has no squad limit
        for (int i = 0; i < 100; ++i) g.put(8000 + i, kFa);
        r = run(w.mem, g, fns, move_req(kActionMove, 3002, 13, kFa));
        CHECK(r.ok, "Free Agents with 100+ players takes one more: " + r.message);
    });

    run_case("player_move: already in the club, wrong `from`, same team, bad arguments (nothing is called)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        auto refused = [&](const Request& q, const char* text) {
            const int before = g.moves + g.adds + g.releases;
            Result r = run(w.mem, g, fns, q);
            CHECK(!r.ok && r.stage == "validate" && has(r.message, text) && !r.called && g.moves + g.adds + g.releases == before && g.join_writes == 0,
                  std::string("refused (") + text + "): " + r.message);
        };
        refused(move_req(kActionMove, 1005, 12, kUser), "already in team 48");  // he IS in the club (and not in `from`)
        refused(move_req(kActionMove, 2003, 13, kUser), "is not in team 13");   // wrong `from`: he plays for 12
        refused(move_req(kActionMove, 7777, 12, kUser), "is not in team 12");   // a player in no team at all
        refused(move_req(kActionMove, 2003, 12, 12), "same team");
        refused(move_req(kActionMove, 0, 12, kUser), "positive");
        refused(move_req(kActionMove, -4, 12, kUser), "positive");
        refused(move_req(kActionMove, 2003, 0, kUser), "`from`");
        refused(move_req(kActionMove, 2003, 12, 0), "`to`");
        refused(move_req(kActionMove, 2003, -12, kUser), "`from`");
        refused(move_req(kActionMove, kPlayerCareer, 12, kUser), "player-career");
        refused(move_req(kActionMove, 2003, 12, 13, -1, 0), "contract length");
        refused(move_req(kActionMove, 2003, 12, 13, kMaxMonths + 1, 0), "contract length");
        refused(move_req(kActionMove, 2003, 12, 13, 12, -1), "wage");
        refused(move_req(kActionMove, 2003, 12, 13, 12, kMaxWage + 1), "wage");
        refused(move_req(0, 2003, 12, 13), "unknown player_move code 0");
        refused(move_req(3, 2003, 12, 13), "unknown player_move code 3");
        // a malformed mailbox word
        Request q = move_req(kActionMove, 2003, 12, 13);
        q.bad_args = "player_move: the mode word has unknown bits set";
        refused(q, "unknown bits");
        // the boundary values are accepted
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, kUser, kMaxMonths, kMaxWage));
        CHECK(r.ok && g.last_add.months == kMaxMonths && g.last_add.wage == kMaxWage, "120 months / the wage bound are accepted: " + r.message);
        CHECK(w.mem.failed_reads == 0, "no read outside the synthetic memory");
    });

    run_case("player_move: a national team (the game's own predicate) or a pseudo team on either side is refused; Free Agents is not", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        auto refused = [&](const Request& q, const char* text) {
            Result r = run(w.mem, g, fns, q);
            CHECK(!r.ok && r.stage == "validate" && has(r.message, text) && !r.called && g.moves == 0 && g.adds == 0 && g.join_writes == 0,
                  std::string("refused (") + text + "): " + r.message);
        };
        refused(move_req(kActionMove, 2002, 12, 1370), "national team (league 78)");
        refused(move_req(kActionMove, 2002, 12, 1371), "national team (league 2136)");
        refused(move_req(kActionMove, 2002, 12, 1372), "national team (league 3004)");
        refused(move_req(kActionMove, 2001, 1370, 13), "national team (league 78)");  // he IS in 1370 (called up): still not a club
        refused(move_req(kActionMove, 2002, 12, 1373), "knows no league");
        refused(move_req(kActionMove, 2002, 12, 99999), "knows no league");
        for (int t : {0x1B688, 0x1B29D, 0x1B72C, 0x20128, 0x20260}) {
            refused(move_req(kActionMove, 2002, 12, t), "pseudo team or another free-agent pool");
            refused(move_req(kActionMove, 2002, t, 13), "pseudo team or another free-agent pool");
        }
        // the national-team rule is checked with the game's two functions, in that order
        g.calls.clear();
        run(w.mem, g, fns, move_req(kActionMove, 2002, 12, 1370));
        CHECK(index_of(g.calls, "league") >= 0 && index_of(g.calls, "intl") == index_of(g.calls, "league") + 1, "GetLeagueOfTeam, then IsInternationalLeague");
        // a club in a normal league and Free Agents (never asked) pass
        g.calls.clear();
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, kFa));
        CHECK(r.ok && g.count_calls("league") == 1, "Free Agents is not looked up (one league lookup, for the club): " + r.message);
        // the game's functions failing stop the call before anything is moved
        g.fail_read = true;
        const int moves_before = g.moves;
        r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "GetLeagueOfTeam: boom") && g.moves == moves_before && !r.called, "a failing read refuses: " + r.message);
    });

    run_case("player_move: the squad limits come from the ini and SquadCounts; the call is refused when the game would auto-release or sign fillers", [&] {
        // the fake models EnforceSquadLimits: prove the side effects exist, so the refusals below are what saves the reserve
        {
            MoveWorld w2;
            FakeMoveGame g2(w2);
            seed_move_world(w2, g2);
            g2.put_many(4000, 27, 13);  // team 13: 52 players
            std::string err;
            g2.player_moved(MoveWorld::kTu, 2002, 12, 13, err);
            CHECK(g2.auto_released == 1 && g2.rows(13) == 52, "the game itself would release a reserve of a full club");
            for (int i = 0; i < 6; ++i) g2.teams.erase(2010 + i);  // team 12: 25 - 1 (moved above) - 6 = 18
            g2.player_moved(MoveWorld::kTu, 2006, 12, 13, err);
            CHECK(g2.fillers >= 1, "and sign fillers for a club left under the minimum");
        }
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        // to full: 52 rows
        g.put_many(4000, 27, 13);
        CHECK(g.rows(13) == 52, "team 13 has 52 players");
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "squad is full") && has(r.message, "rows 52 + loaned out 0 + pre-signed 0 = 52") &&
                  has(r.message, "MAX_SQUAD_SIZE 52") && has(r.message, "release his lowest-value reserve") && !r.called && g.moves == 0 && g.auto_released == 0,
              "to full: " + r.message);
        CHECK(r.rows_to == 52 && r.loaned_to == 0 && r.pending_to == 0, "the counts that decided it");
        // 51 rows: exactly one place left
        g.teams[4000].erase(13);
        r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(r.ok && g.rows(13) == 52 && g.auto_released == 0 && g.fillers == 0, "51 rows + the arrival = 52 = MAX: allowed, nothing auto-released: " + r.message);
        // loaned-out players and pre-signed deals count towards the limit (only those naming this team)
        w.set_loans({{7001, 13}, {7002, 13}, {7003, 99}});
        w.set_pending({13, 77});
        r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(!r.ok && has(r.message, "rows 52 + loaned out 2 + pre-signed 1 = 55"), "loans and pre-signed deals counted: " + r.message);
        for (int pid : {4001, 4002, 4003, 4004}) g.teams[pid].erase(13);  // rows 48 + 2 + 1 = 51
        r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(r.ok && r.rows_to == 48 && r.loaned_to == 2 && r.pending_to == 1 && g.auto_released == 0, "48 + 2 + 1 + the arrival = 52: allowed: " + r.message);
        w.set_loans({});
        w.set_pending({});
        // the limits are read from the ini, not hard-coded
        w.mem.wr(MoveWorld::kIni + kIniMax, static_cast<int32_t>(30));
        r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, 13));
        CHECK(!r.ok && has(r.message, "MAX_SQUAD_SIZE 30"), "MAX_SQUAD_SIZE 30 from the ini: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, kUser));
        CHECK(r.ok && r.at.max_squad == 30, "the user's club (25 + 1 = 26 <= 30) is allowed: " + r.message);
        // the user's own club obeys the same limit
        MoveWorld w4;
        FakeMoveGame g4(w4);
        seed_move_world(w4, g4);
        g4.put_many(6000, 27, kUser);
        CHECK(g4.rows(kUser) == 52, "the user's club has 52 players");
        r = run(w4.mem, g4, fns, move_req(kActionMove, 2005, 12, kUser, 12, 100));
        CHECK(!r.ok && has(r.message, "team 48's squad is full") && g4.adds == 0 && g4.moves == 0 && g4.auto_released == 0, "the user's full club refuses an arrival: " + r.message);
        r = run(w4.mem, g4, fns, move_req(kActionMove, 1003, kUser, 12));
        CHECK(r.ok && g4.rows(kUser) == 51, "but he can still lose a player: " + r.message);
        // from at the minimum: the game would sign fillers
        MoveWorld w3;
        FakeMoveGame g3(w3);
        seed_move_world(w3, g3);
        for (int i = 0; i < 7; ++i) g3.teams.erase(2010 + i);  // team 12: 18 players
        CHECK(g3.rows(12) == 18, "team 12 has the minimum");
        r = run(w3.mem, g3, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "team 12 has only 18 players") && has(r.message, "MIN_SQUAD_SIZE 18") && has(r.message, "filler") && !r.called &&
                  g3.moves == 0 && g3.fillers == 0,
              "from at minimum: " + r.message);
        g3.put(2999, 12);  // 19
        r = run(w3.mem, g3, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(r.ok && g3.rows(12) == 18 && g3.fillers == 0, "19 rows - 1 = 18 = MIN: allowed, no filler signed: " + r.message);
        // MIN_SQUAD_SIZE from the ini too
        w3.mem.wr(MoveWorld::kIni + kIniMin, static_cast<int32_t>(25));
        r = run(w3.mem, g3, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(!r.ok && has(r.message, "MIN_SQUAD_SIZE 25"), "MIN_SQUAD_SIZE 25 from the ini: " + r.message);
        // Free Agents as `from` has no minimum
        w3.mem.wr(MoveWorld::kIni + kIniMin, static_cast<int32_t>(18));
        r = run(w3.mem, g3, fns, move_req(kActionMove, 9001, kFa, 13));
        CHECK(r.ok, "free agent out: no minimum: " + r.message);
    });

    run_case("player_move: a player on loan is not moved (the LoansManager's list)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        w.set_loans({{2002, 12}, {2500, 13}});
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "is on loan") && has(r.message, "returning club 12") && g.moves == 0, "on loan: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(r.ok, "another player moves: " + r.message);
        bool loaned = false;
        int club = 0;
        CHECK(loan_state(w.mem, MoveWorld::kLoans, 2500, loaned, club).empty() && loaned && club == 13, "loan_state finds his record");
        CHECK(loan_state(w.mem, MoveWorld::kLoans, 4, loaned, club).empty() && !loaned, "and none for a player who is not on loan");
    });

    run_case("player_move: check only (code 9) runs the whole chain and the read-backs and calls nothing", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        Result r = run(w.mem, g, fns, move_req(kActionCheck, 2001, 12, kUser, 36, 25000));
        CHECK(r.ok && r.stage == "done" && !r.called && !r.contract_called && g.moves == 0 && g.adds == 0 && g.releases == 0 && g.join_writes == 0,
              "nothing was called: " + r.message);
        CHECK(r.from_ok == 1 && r.to_ok == 1, "check outputs: he IS in from, he is NOT in to");
        CHECK(has(r.message, "is allowed (checked only, nothing was called)") && has(r.message, "team 48 rows 25 + loaned out 0 + pre-signed 0 (limit 52)") &&
                  has(r.message, "team 12 rows 25 (minimum 18)") && has(r.message, "arrival at your club") && has(r.message, "a contract record would be added"),
              "message with the counts and the limits: " + r.message);
        CHECK(g.teams[2001] == std::set<int>({12, 1370}) && g.rows(12) == 25, "the database is untouched");
        CHECK(g.count_calls("moved") == 0 && g.count_calls("add") == 0 && g.count_calls("release") == 0, "no game function that changes anything");
        // an AI -> AI check, no record
        r = run(w.mem, g, fns, move_req(kActionCheck, 2003, 12, 13));
        CHECK(r.ok && !has(r.message, "arrival at your club") && g.moves == 0, "AI <- AI check: " + r.message);
        // refusals are the same as for the move, and the outputs say what was read
        r = run(w.mem, g, fns, move_req(kActionCheck, 2003, 13, kUser));  // wrong from
        CHECK(!r.ok && has(r.message, "is not in team 13") && r.from_ok == 0 && r.to_ok == 1, "wrong from: from_ok 0, to_ok 1: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionCheck, 1005, 12, kUser));  // already in `to`
        CHECK(!r.ok && has(r.message, "already in team 48") && r.from_ok == 0 && r.to_ok == 0, "already in `to`: to_ok 0: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionCheck, 2003, 12, 1370));  // national: refused before the read
        CHECK(!r.ok && has(r.message, "national team") && r.from_ok == -1 && r.to_ok == -1, "national: not read: " + r.message);
        g.put_many(4000, 27, 13);
        r = run(w.mem, g, fns, move_req(kActionCheck, 2003, 12, 13));
        CHECK(!r.ok && has(r.message, "squad is full") && r.from_ok == 1 && r.to_ok == 1, "full: the preconditions held, the limit did not: " + r.message);
        // an existing record is reported
        w.add_player(2004, 0);
        r = run(w.mem, g, fns, move_req(kActionCheck, 2004, 12, kUser, 12, 10));
        CHECK(r.ok && has(r.message, "a contract record exists already (none would be added)"), "existing record: " + r.message);
        // no months asked
        r = run(w.mem, g, fns, move_req(kActionCheck, 2005, 12, kUser));
        CHECK(r.ok && has(r.message, "no contract record asked"), "months 0: " + r.message);
        CHECK(g.moves == 0 && g.adds == 0 && g.releases == 0, "still nothing called");
        // a check also stops at the same walls (a closed sim state)
        w.mem.wr(MoveWorld::kSimDay + 0x14, static_cast<int32_t>(2));
        r = run(w.mem, g, fns, move_req(kActionCheck, 2005, 12, kUser));
        CHECK(!r.ok && has(r.message, "processing a match day"), "check while the game processes a day: " + r.message);
    });

    run_case("player_move: the contract record is added once (an existing record is left alone, months 0 adds none, a failing add is reported)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        // a record the game already holds for him (stale: another team, another wage)
        uint64_t node = w.add_player(2002, 0);
        w.mem.wr(node + kPcmNodeTeam, static_cast<int32_t>(12));
        w.mem.wr(node + kPcmNodeWage, static_cast<int32_t>(9));
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, kUser, 36, 25000));
        CHECK(r.ok && r.called && !r.contract_called && g.adds == 0, "no second record: " + r.message);
        int32_t team = 0, wage = 0;
        CHECK(w.record(2002, team, wage) && team == 12 && wage == 9, "the existing record is untouched");
        CHECK(has(r.message, "a contract record exists already (team 12, wage 9): none added") && has(r.message, "WARNING: it names another team"), "message: " + r.message);
        // the same request twice: the second one is refused (he is in the club already), so AddContractRecord ran at most once overall
        Result r2 = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, kUser, 12, 777));
        CHECK(r2.ok && g.adds == 1, "a player without a record gets exactly one");
        r2 = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, kUser, 12, 777));
        CHECK(!r2.ok && has(r2.message, "already in team 48") && g.adds == 1 && g.moves == 2, "repeating the request changes nothing and adds nothing: " + r2.message);
        // months 0: PlayerMoved only
        r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, kUser));
        CHECK(r.ok && r.called && !r.contract_called && g.adds == 1 && w.status(2004) == -1 && !has(r.message, "contract record"), "months 0: no record: " + r.message);
        // the game failing to add the record
        g.fail_add = true;
        r = run(w.mem, g, fns, move_req(kActionMove, 2005, 12, kUser, 12, 100));
        CHECK(!r.ok && r.stage == "call" && has(r.message, "AddContractRecord: boom") && has(r.message, "moved from team 12 to team 48") && r.called && r.from_ok == 1 && r.to_ok == 1,
              "failing AddContractRecord (the move happened, and says so): " + r.message);
        g.fail_add = false;
        // the call ran but made no record
        g.noop_add = true;
        r = run(w.mem, g, fns, move_req(kActionMove, 2006, 12, kUser, 12, 100));
        CHECK(!r.ok && r.stage == "check" && has(r.message, "has no record for him") && r.contract_called, "no record afterwards: " + r.message);
        g.noop_add = false;
        // a record with the wrong content is flagged
        g.wrong_add = true;
        r = run(w.mem, g, fns, move_req(kActionMove, 2007, 12, kUser, 12, 100));
        CHECK(r.ok && has(r.message, "WARNING: the record reads team 49, wage 100"), "wrong record flagged: " + r.message);
    });

    run_case("player_move: release: the user's player goes through ContractTerminationManager::ReleasePlayer (code 0 / 1 / 2 and the game's reason)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        Result r = run(w.mem, g, fns, move_req(kActionRelease, 1003, kUser, 0));
        CHECK(r.ok && r.stage == "done" && r.called && r.release_code == 0 && g.releases == 1 && g.moves == 0 && g.adds == 0, "released: " + r.message);
        CHECK(g.last_ctm == MoveWorld::kCtm && r.at.ctm == MoveWorld::kCtm, "on the hub's ContractTerminationManager");
        CHECK(g.teams[1003] == std::set<int>({kFa}) && g.rows(kUser) == 24 && w.status(1003) == -1, "he is a free agent, his record is gone");
        CHECK(r.from_ok == 1 && r.to_ok == 1 && has(r.message, "released by the game") && has(r.message, "his contract record is gone"), "read-backs and message: " + r.message);
        // the budget cannot pay: the game's code 1 with its reason
        g.budget_ok = false;
        r = run(w.mem, g, fns, move_req(kActionRelease, 1004, kUser, 0));
        CHECK(!r.ok && r.stage == "call" && r.called && r.release_code == 1 && has(r.message, "budget cannot pay the compensation") && g.teams[1004] == std::set<int>({kUser}) &&
                  r.from_ok == -1,
              "code 1: " + r.message);
        g.budget_ok = true;
        // the game's own code 2 (squad at the minimum) and a code Turbo does not know
        g.force_release_code = 2;
        r = run(w.mem, g, fns, move_req(kActionRelease, 1004, kUser, 0));
        CHECK(!r.ok && r.release_code == 2 && has(r.message, "squad would drop below the minimum"), "code 2: " + r.message);
        g.force_release_code = 9;
        r = run(w.mem, g, fns, move_req(kActionRelease, 1004, kUser, 0));
        CHECK(!r.ok && r.release_code == 9 && has(r.message, "not a code Turbo knows"), "an unknown code: " + r.message);
        g.force_release_code = -1;
        // the game's own release pool
        g.release_pool = 0x20128;
        r = run(w.mem, g, fns, move_req(kActionRelease, 1005, kUser, 0));
        CHECK(r.ok && r.to_ok == 1 && has(r.message, "free-agent pool 0x20128"), "another pool is found and named: " + r.message);
        g.release_pool = kFa;
        // the call ran and did nothing
        g.noop_release = true;
        r = run(w.mem, g, fns, move_req(kActionRelease, 1006, kUser, 0));
        CHECK(!r.ok && r.stage == "check" && r.from_ok == 0 && r.to_ok == 0 && has(r.message, "did not do what was asked"), "no-op release: " + r.message);
        g.noop_release = false;
        // the call failing
        g.fail_release = true;
        r = run(w.mem, g, fns, move_req(kActionRelease, 1007, kUser, 0));
        CHECK(!r.ok && r.stage == "call" && has(r.message, "ReleasePlayer: boom") && !r.called, "failing call: " + r.message);
        g.fail_release = false;
        // `to` is ignored, the player must be in `from` and in no free-agent pool
        r = run(w.mem, g, fns, move_req(kActionRelease, 1008, kUser, 424242));
        CHECK(r.ok, "`to` is ignored by a release: " + r.message);
        // the squad minimum, Turbo's own check first
        MoveWorld w2;
        FakeMoveGame g2(w2);
        seed_move_world(w2, g2);
        for (int i = 0; i < 7; ++i) g2.teams.erase(1010 + i);  // the user's club: 18 players
        r = run(w2.mem, g2, fns, move_req(kActionRelease, 1003, kUser, 0));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "has only 18 players") && g2.releases == 0, "squad at the minimum: nothing called: " + r.message);
        CHECK(w.mem.failed_reads == 0 && w2.mem.failed_reads == 0, "no read outside the synthetic memory");
    });

    run_case("player_move: release of another club's player is PlayerMoved(pid, club, Free Agents); the other refusals", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        Result r = run(w.mem, g, fns, move_req(kActionRelease, 2005, 12, 424242));  // `to` is ignored
        CHECK(r.ok && r.called && g.moves == 1 && g.releases == 0 && g.last_moved.from == 12 && g.last_moved.to == kFa && g.last_moved.pid == 2005, "AI release: " + r.message);
        CHECK(g.teams[2005] == std::set<int>({kFa}) && r.from_ok == 1 && r.to_ok == 1 && has(r.message, "to Free Agents (no compensation: not your club)"), "state and message");
        // refusals: a free agent, a national team, the wrong club, a pseudo team, on loan, a club at the minimum
        auto refused = [&](const Request& q, const char* text) {
            const int before = g.moves + g.releases;
            Result x = run(w.mem, g, fns, q);
            CHECK(!x.ok && x.stage == "validate" && has(x.message, text) && g.moves + g.releases == before, std::string("refused (") + text + "): " + x.message);
        };
        refused(move_req(kActionRelease, 9001, kFa, 0), "free agent already");
        refused(move_req(kActionRelease, 2006, 1370, 0), "national team");
        refused(move_req(kActionRelease, 2006, 13, 0), "is not in team 13");
        refused(move_req(kActionRelease, 2006, 0x1B688, 0), "pseudo team");
        refused(move_req(kActionRelease, 0, 12, 0), "positive");
        refused(move_req(kActionRelease, 2006, 0, 0), "`from`");
        w.set_loans({{2007, 12}});
        refused(move_req(kActionRelease, 2007, 12, 0), "is on loan");
        w.set_loans({});
        MoveWorld w2;
        FakeMoveGame g2(w2);
        seed_move_world(w2, g2);
        for (int i = 0; i < 7; ++i) g2.teams.erase(2010 + i);  // team 12: 18
        r = run(w2.mem, g2, fns, move_req(kActionRelease, 2006, 12, 0));
        CHECK(!r.ok && has(r.message, "has only 18 players") && g2.moves == 0 && g2.fillers == 0, "club at the minimum: " + r.message);
    });

    run_case("player_move: the game doing less (or more) than asked is reported with the read-backs, never assumed", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        g.fail_moved = true;
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(!r.ok && r.stage == "call" && has(r.message, "TeamUtil::PlayerMoved: boom") && !r.called && g.teams[2002].count(12) == 1, "failing call: " + r.message);
        g.fail_moved = false;
        g.noop_moved = true;
        r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(!r.ok && r.stage == "check" && r.called && r.from_ok == 0 && r.to_ok == 0 && has(r.message, "did not move him as asked"), "the game moved nobody: " + r.message);
        g.noop_moved = false;
        g.half_moved = true;  // linked into `to`, still in `from`
        r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(!r.ok && r.stage == "check" && r.from_ok == 0 && r.to_ok == 1 && has(r.message, "still in team 12 yes"), "half moved: " + r.message);
        g.half_moved = false;
        g.teams[2003].erase(12);
        // a squad changed beyond the move: the move is reported ok with a warning naming the squad
        g.extra_drop = true;
        r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, 13));
        CHECK(r.ok && has(r.message, "WARNING: team 13 now has") && has(r.message, "the game changed that squad too"), "squad warning: " + r.message);
        g.extra_drop = false;
        // a clean move has no warning
        MoveWorld w2;
        FakeMoveGame g2(w2);
        seed_move_world(w2, g2);
        Result ok = run(w2.mem, g2, fns, move_req(kActionMove, 2002, 12, 13));
        CHECK(ok.ok && !has(ok.message, "WARNING"), "a clean move has no warning: " + ok.message);
    });

    run_case("player_move: the morale gate byte is reported (closed gate, unreadable manager) and never refuses a move", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        // closed gate (set by the job-switch event): the game creates no morale entry for the arrival
        w.mem.wr(MoveWorld::kMorale + kMoraleGate, static_cast<uint8_t>(1));
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, kUser, 12, 100));
        CHECK(r.ok && r.at.morale_gate == 1 && has(r.message, "WARNING: the morale gate is closed") && g.morale.count(2002) == 0, "closed gate: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionMove, 2003, 12, 13));
        CHECK(r.ok && !has(r.message, "morale gate"), "an AI club's move says nothing about morale: " + r.message);
        // an odd byte: not read
        w.mem.wr(MoveWorld::kMorale + kMoraleGate, static_cast<uint8_t>(7));
        r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, kUser));
        CHECK(r.ok && r.at.morale_gate == -1 && has(r.message, "morale gate not read: the byte holds 7"), "odd byte: " + r.message);
        w.mem.wr(MoveWorld::kMorale + kMoraleGate, static_cast<uint8_t>(0));
        // the signature missing: a note, the move still runs
        Fns f = fns;
        f.morale_vtable = 0;
        r = run(w.mem, g, f, move_req(kActionMove, 2005, 12, kUser));
        CHECK(r.ok && has(r.message, "morale gate not read: signature morale_vtable was not found"), "no morale_vtable: " + r.message);
        // the manager missing from the table: a note
        w.mem.wr(MoveWorld::kManagers + 0x20 * 83 + turbo::tl::kSlotCount, static_cast<int32_t>(0));
        r = run(w.mem, g, fns, move_req(kActionMove, 2006, 12, kUser));
        CHECK(r.ok && has(r.message, "morale gate not read: manager slot 83"), "no morale manager: " + r.message);
        w.mem.wr(MoveWorld::kManagers + 0x20 * 83 + turbo::tl::kSlotCount, static_cast<int32_t>(1));
        // a manager that is not the PlayerMoraleManager (the slot holds something else): refused, nothing called
        const int before = g.calls_total();
        w.mem.wr(MoveWorld::kMorale, 0x14B000000ULL);
        r = run(w.mem, g, fns, move_req(kActionMove, 2007, 12, kUser));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "is not the PlayerMoraleManager") && g.calls_total() == before, "foreign morale vtable refused: " + r.message);
        w.mem.wr(MoveWorld::kMorale, MoveWorld::kMoraleVt);
        w.mem.wr(MoveWorld::kMoraleVt + 8, 0x147D00000ULL);
        r = run(w.mem, g, fns, move_req(kActionMove, 2007, 12, kUser));
        CHECK(!r.ok && has(r.message, "slot 1") && has(r.message, "not the HandleEvent") && g.calls_total() == before, "slot 1 of the morale vtable must be the resolved HandleEvent: " + r.message);
        w.mem.wr(MoveWorld::kMoraleVt + 8, MoveWorld::kFnMoraleHandle);
        r = run(w.mem, g, fns, move_req(kActionMove, 2007, 12, kUser));
        CHECK(r.ok, "restored: " + r.message);
    });

    run_case("player_move: corrupted or foreign objects are refused before anything is called (not even a read of the game)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        const uint64_t disp = w.dispatcher(), cal = w.calendar();
        auto save64 = [&](uint64_t a) {
            uint64_t v = 0;
            w.mem.rd(a, v);
            return v;
        };
        auto save32 = [&](uint64_t a) {
            int32_t v = 0;
            w.mem.rd(a, v);
            return v;
        };
        // each case: corrupt, run, expect the text and not a single call into the game, restore, prove the world works again
        auto refused = [&](const char* what, const char* text, const std::function<void()>& corrupt, const std::function<void()>& restore, int action = kActionMove,
                           const std::function<void(Request&)>& tweak = nullptr, Fns f = move_fns()) {
            corrupt();
            Request q = action == kActionRelease ? move_req(kActionRelease, 1003, kUser, 0) : move_req(action, 2002, 12, 13);
            if (tweak) tweak(q);
            const int before = g.calls_total() + g.moves + g.adds + g.releases;
            Result r = run(w.mem, g, f, q);
            CHECK(!r.ok && r.stage == "validate" && has(r.message, text) && !r.called && g.calls_total() + g.moves + g.adds + g.releases == before,
                  std::string(what) + ": " + r.message);
            restore();
            Result ok = run(w.mem, g, move_fns(), move_req(kActionCheck, 2003, 12, 13));
            CHECK(ok.ok, std::string(what) + ": restored: " + ok.message);
        };
        auto set64 = [&](uint64_t a, uint64_t v) { return std::function<void()>([&w, a, v] { w.mem.wr(a, v); }); };
        auto set32 = [&](uint64_t a, int32_t v) { return std::function<void()>([&w, a, v] { w.mem.wr(a, v); }); };
        auto restore64 = [&](uint64_t a) {
            const uint64_t v = save64(a);
            return std::function<void()>([&w, a, v] { w.mem.wr(a, v); });
        };
        auto restore32 = [&](uint64_t a) {
            const int32_t v = save32(a);
            return std::function<void()>([&w, a, v] { w.mem.wr(a, v); });
        };
        auto slot_of = [](int type) { return ListWorld::kManagers + 0x20 * static_cast<uint64_t>(type); };
        auto holder_of = [](int type) { return ListWorld::kHolders + static_cast<uint64_t>(type) * 0x10; };
        auto image = [](Request& q) {
            q.image_base = 0x140000000ULL;
            q.image_size = 0x10000000ULL;
        };
        // the comm service / owner / hub chain
        refused("comm service unknown", "comm service is not known", [] {}, [] {}, kActionMove, [](Request& q) { q.comm = 0; });
        refused("owner empty", "no career-mode owner", set64(ListWorld::kComm + turbo::tl::kCommOwner, 0), restore64(ListWorld::kComm + turbo::tl::kCommOwner));
        refused("hub empty", "no manager table", set64(ListWorld::kOwner + turbo::tl::kOwnerManagers, 0), restore64(ListWorld::kOwner + turbo::tl::kOwnerManagers));
        refused("hub differs from the one Lua published", "manager table mismatch", [] {}, [] {}, kActionMove, [](Request& q) { q.managers = 0x1234000ULL; });
        // DataController: count, hub back pointer, db provider, its vtable and slot 1
        refused("dc slot count", "holds 0 objects", set32(slot_of(kTypeDataController) + turbo::tl::kSlotCount, 0), restore32(slot_of(kTypeDataController) + turbo::tl::kSlotCount));
        refused("dc holder empty", "holds no object", set64(holder_of(kTypeDataController), 0), restore64(holder_of(kTypeDataController)));
        refused("dc unmapped", "DataController at", set64(holder_of(kTypeDataController), 0x31000000ULL), restore64(holder_of(kTypeDataController)));
        refused("dc hub back pointer", "DataController's manager table", set64(MoveWorld::kDc + kDcHub, ListWorld::kOwner), restore64(MoveWorld::kDc + kDcHub));
        refused("dc without a db provider", "no db provider", set64(MoveWorld::kDc + kDcProvider, 0), restore64(MoveWorld::kDc + kDcProvider));
        refused("db provider vtable slot 1", "db provider vtable", set64(MoveWorld::kProviderVt + 8, 0), restore64(MoveWorld::kProviderVt + 8));
        refused("db provider vtable outside FC27.exe", "no db provider with a vtable in FC27.exe", set64(MoveWorld::kProvider, 0x7FF600001000ULL), restore64(MoveWorld::kProvider),
                kActionMove, image);
        // TeamUtil
        refused("teamUtil hub back pointer", "TeamUtil's manager table", set64(MoveWorld::kTu + kTeamUtilHub, ListWorld::kOwner), restore64(MoveWorld::kTu + kTeamUtilHub));
        refused("teamUtil holder empty", "holds no object", set64(holder_of(kTypeTeamUtil), 0), restore64(holder_of(kTypeTeamUtil)));
        refused("teamUtil unmapped", "TeamUtil at", set64(holder_of(kTypeTeamUtil), 0x31000000ULL), restore64(holder_of(kTypeTeamUtil)));
        // PlayerContractManager / TransferManager / UserManager: vtable + back pointer
        refused("pcm vtable", "not the PlayerContractManager", set64(ListWorld::kPcm, 0x14B000000ULL), restore64(ListWorld::kPcm));
        refused("pcm manager table", "PlayerContractManager's manager table", set64(ListWorld::kPcm + turbo::tl::kMgrManagers, ListWorld::kOwner),
                restore64(ListWorld::kPcm + turbo::tl::kMgrManagers));
        refused("tm vtable", "not the TransferManager", set64(ListWorld::kTm, 0x14B000000ULL), restore64(ListWorld::kTm));
        refused("tm manager table", "TransferManager's manager table", set64(ListWorld::kTm + turbo::tl::kMgrManagers, ListWorld::kOwner),
                restore64(ListWorld::kTm + turbo::tl::kMgrManagers));
        refused("um vtable", "not the UserManager", set64(ListWorld::kUm, 0x14B000000ULL), restore64(ListWorld::kUm));
        refused("um manager table", "UserManager's manager table", set64(ListWorld::kUm + turbo::tl::kMgrManagers, ListWorld::kOwner),
                restore64(ListWorld::kUm + turbo::tl::kMgrManagers));
        refused("no users", "no users", set32(ListWorld::kUm + turbo::tl::kUmCount, 0), restore32(ListWorld::kUm + turbo::tl::kUmCount));
        // the TransferManager's pre-signed deals SquadCounts walks
        const uint64_t pb = MoveWorld::kTm + kTmPendingBegin, pe = MoveWorld::kTm + kTmPendingEnd;
        auto restore_pair = [&](uint64_t a, uint64_t b) {
            const uint64_t va = save64(a), vb = save64(b);
            return std::function<void()>([&w, a, b, va, vb] {
                w.mem.wr(a, va);
                w.mem.wr(b, vb);
            });
        };
        refused("pending begin after end", "pre-signed deals", [&] { w.mem.wr(pb, MoveWorld::kPendBuf + 0x100); w.mem.wr(pe, MoveWorld::kPendBuf); }, restore_pair(pb, pe));
        refused("pending not whole records", "not a whole number of 136-byte", [&] { w.mem.wr(pe, MoveWorld::kPendBuf + 0x90); }, restore_pair(pb, pe));
        refused("pending more than 5000", "5001 records", [&] { w.mem.wr(pe, MoveWorld::kPendBuf + 5001 * kTmPendingRecord); }, restore_pair(pb, pe));
        refused("pending unmapped", "is not readable", [&] { w.mem.wr(pb, 0x31000000ULL); w.mem.wr(pe, 0x31000000ULL + kTmPendingRecord); }, restore_pair(pb, pe));
        refused("pending begin not a pointer", "not a pointer", [&] { w.mem.wr(pb, 0x10ULL); w.mem.wr(pe, 0x10ULL + kTmPendingRecord); }, restore_pair(pb, pe));
        // the dispatcher, the calendar
        refused("dispatcher without an event sink", "no event sink", set64(disp, 0), restore64(disp));
        refused("calendar date zero", "is not a date", set32(cal + turbo::tl::kCalendarDate, 0), restore32(cal + turbo::tl::kCalendarDate));
        refused("calendar month 13", "is not a date", set32(cal + turbo::tl::kCalendarDate + 4, 13), restore32(cal + turbo::tl::kCalendarDate + 4));
        refused("calendar year 1800", "is not a date", set32(cal + turbo::tl::kCalendarDate + 8, 1800), restore32(cal + turbo::tl::kCalendarDate + 8));
        refused("calendar without a vtable", "CalendarManager", set64(cal, 0), restore64(cal));
        // the ini limits
        refused("ini MIN 0", "not plausible", set32(MoveWorld::kIni + kIniMin, 0), restore32(MoveWorld::kIni + kIniMin));
        refused("ini MAX below MIN", "not plausible", set32(MoveWorld::kIni + kIniMax, 10), restore32(MoveWorld::kIni + kIniMax));
        refused("ini MAX 5000", "not plausible", set32(MoveWorld::kIni + kIniMax, 5000), restore32(MoveWorld::kIni + kIniMax));
        refused("ini holder empty", "holds no object", set64(holder_of(kTypeIni), 0), restore64(holder_of(kTypeIni)));
        // the LoansManager
        refused("loans without a vtable", "no vtable in FC27.exe", set64(MoveWorld::kLoans, 0), restore64(MoveWorld::kLoans));
        refused("loans holder empty", "holds no object", set64(holder_of(kTypeLoans), 0), restore64(holder_of(kTypeLoans)));
        const uint64_t lb = MoveWorld::kLoans + kLoansBegin, le = MoveWorld::kLoans + kLoansEnd;
        refused("loans begin after end", "is after end", [&] { w.mem.wr(lb, MoveWorld::kLoanBuf + 0x40); w.mem.wr(le, MoveWorld::kLoanBuf); }, restore_pair(lb, le));
        refused("loans not whole records", "not a whole number of 32-byte", [&] { w.mem.wr(le, MoveWorld::kLoanBuf + 0x30); }, restore_pair(lb, le));
        refused("loans more than 20000", "20001 records", [&] { w.mem.wr(le, MoveWorld::kLoanBuf + 20001 * kLoanRecord); }, restore_pair(lb, le));
        refused("loans unmapped", "is not readable", [&] { w.mem.wr(lb, 0x31000000ULL); w.mem.wr(le, 0x31000000ULL + kLoanRecord); }, restore_pair(lb, le));
        // the game is busy
        refused("SimDayManager processing a day", "processing a match day", set32(MoveWorld::kSimDay + 0x14, 3), restore32(MoveWorld::kSimDay + 0x14));
        refused("SimDayManager missing", "SimDayManager is not available", set32(slot_of(103) + turbo::tl::kSlotCount, 0), restore32(slot_of(103) + turbo::tl::kSlotCount));
        // resolved addresses outside FC27.exe
        {
            auto outside = [&](const char* what, const std::function<void(Fns&)>& tweak) {
                Fns f = move_fns();
                tweak(f);
                refused(what, "outside FC27.exe", [] {}, [] {}, kActionMove, image, f);
            };
            outside("PlayerMoved outside", [](Fns& f) { f.player_moved = 0x7FF600001000ULL; });
            outside("IsPlayerInTeam outside", [](Fns& f) { f.is_player_in_team = 0x7FF600001000ULL; });
            outside("SquadCounts outside", [](Fns& f) { f.squad_counts = 0x7FF600001000ULL; });
            outside("GetLeagueOfTeam outside", [](Fns& f) { f.league_of_team = 0x7FF600001000ULL; });
            outside("IsInternationalLeague outside", [](Fns& f) { f.is_international = 0x7FF600001000ULL; });
            outside("AddContractRecord outside", [](Fns& f) { f.add_contract = 0x7FF600001000ULL; });
            outside("pcm vtable outside", [](Fns& f) { f.pcm_vtable = 0x7FF600001000ULL; });
            outside("morale vtable outside", [](Fns& f) { f.morale_vtable = 0x7FF600001000ULL; });
            Request q = move_req(kActionMove, 2002, 12, 13);
            image(q);
            Result r = run(w.mem, g, move_fns(), q);
            CHECK(r.ok, "inside the image: " + r.message);
        }
        // release-only objects: a broken ContractTerminationManager stops a release, never a move
        refused("ctm vtable", "not the ContractTerminationManager", set64(MoveWorld::kCtm, 0x14B000000ULL), restore64(MoveWorld::kCtm), kActionRelease);
        refused("ctm manager table", "ContractTerminationManager's manager table", set64(MoveWorld::kCtm + turbo::tl::kMgrManagers, ListWorld::kOwner),
                restore64(MoveWorld::kCtm + turbo::tl::kMgrManagers), kActionRelease);
        refused("ctm slot count", "holds 0 objects", set32(slot_of(kTypeCtm) + turbo::tl::kSlotCount, 0), restore32(slot_of(kTypeCtm) + turbo::tl::kSlotCount), kActionRelease);
        refused("release counter slot", "release counter manager", set32(slot_of(kTypeReleaseCounter) + turbo::tl::kSlotCount, 0),
                restore32(slot_of(kTypeReleaseCounter) + turbo::tl::kSlotCount), kActionRelease);
        refused("release counter cut off", "slot 60", set64(holder_of(kTypeReleaseCounter), 0x31000000ULL), restore64(holder_of(kTypeReleaseCounter)), kActionRelease);
        refused("user finance missing", "finance object", set64(ListWorld::kUsers + kUserFinance, 0), restore64(ListWorld::kUsers + kUserFinance), kActionRelease);
        refused("user finance vtable slot 2", "finance object vtable", set64(MoveWorld::kFinVt + 16, 0), restore64(MoveWorld::kFinVt + 16), kActionRelease);
        refused("release function outside", "outside FC27.exe", [] {}, [] {}, kActionRelease, image, [] {
            Fns f = move_fns();
            f.release_player = 0x7FF600001000ULL;
            return f;
        }());
        w.mem.wr(MoveWorld::kCtm, 0x14B000000ULL);
        w.mem.wr(MoveWorld::kFinVt + 16, 0ULL);
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, 13));
        CHECK(r.ok, "a broken CTM / finance object does not stop a move: " + r.message);
    });

    run_case("player_move: a missing signature refuses what needs it, before anything is called", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        struct Miss {
            const char* name;
            std::function<void(Fns&)> clear;
            bool move;     // a move / check is refused
            bool release;  // a release is refused
        };
        const std::vector<Miss> misses = {
            {"teamutil_player_moved", [](Fns& f) { f.player_moved = 0; }, true, true},
            {"dc_is_player_in_team", [](Fns& f) { f.is_player_in_team = 0; }, true, true},
            {"dc_squad_counts", [](Fns& f) { f.squad_counts = 0; }, true, true},
            {"dc_get_league_of_team", [](Fns& f) { f.league_of_team = 0; }, true, true},
            {"is_international_league", [](Fns& f) { f.is_international = 0; }, true, true},
            {"pcm_vtable", [](Fns& f) { f.pcm_vtable = 0; }, true, true},
            {"tm_vtable", [](Fns& f) { f.tm_vtable = 0; }, true, true},
            {"um_vtable", [](Fns& f) { f.um_vtable = 0; }, true, true},
            {"pcm_add_contract_record", [](Fns& f) { f.add_contract = 0; }, true, false},
            {"ctm_release_player", [](Fns& f) { f.release_player = 0; }, false, true},
            {"ctm_vtable", [](Fns& f) { f.ctm_vtable = 0; }, false, true},
        };
        for (const auto& m : misses) {
            Fns f = fns;
            m.clear(f);
            for (int action : {kActionMove, kActionCheck}) {
                if (!m.move && action == kActionMove) continue;  // would really move him: the check below proves the entry is not needed
                const int before = g.calls_total();
                Result r = run(w.mem, g, f, move_req(action, 2002, 12, 13));
                if (m.move)
                    CHECK(!r.ok && r.stage == "validate" && has(r.message, (std::string("game function ") + m.name + " is not resolved").c_str()) && g.calls_total() == before,
                          std::string(m.name) + " missing (" + action_name(action) + "): " + r.message);
                else
                    CHECK(r.ok, std::string(m.name) + " is not needed by " + action_name(action) + ": " + r.message);
            }
            const int before_release = g.calls_total();
            Result r = run(w.mem, g, f, move_req(kActionRelease, 2005, 12, 0));
            if (m.release)
                CHECK(!r.ok && r.stage == "validate" && has(r.message, (std::string("game function ") + m.name + " is not resolved").c_str()) && g.calls_total() == before_release,
                      std::string(m.name) + " missing (release): " + r.message);
            else
                CHECK(r.ok, std::string(m.name) + " is not needed by a release: " + r.message);
        }
        CHECK(g.moves == 1 && g.releases == 0, "only the release that needed nothing missing ran (one PlayerMoved)");
        // the missing-signature check is not fooled by the Lua side: an empty table names the first entry
        Result r = run(w.mem, g, Fns(), move_req(kActionMove, 2006, 12, 13));
        CHECK(!r.ok && has(r.message, "teamutil_player_moved is not resolved"), "empty table: " + r.message);
    });

    run_case("player_move: the kill switch file turns the call off (core part: file name and presence)", [&] {
        const fs::path dir = fs::temp_directory_path() / "turbo_player_move_kill_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        CHECK(!killed(dir), "no file: not killed");
        std::ofstream((dir / "call_transfer_list_off.txt").string()) << "";
        std::ofstream((dir / "call_job_offer_off.txt").string()) << "";
        CHECK(!killed(dir), "the other calls' switches do not turn this one off");
        std::ofstream((dir / kill_switch_name()).string()) << "";
        CHECK(killed(dir) && fs::exists(dir / "call_player_move_off.txt", ec), "call_player_move_off.txt turns it off");
        fs::remove(dir / kill_switch_name(), ec);
        CHECK(!killed(dir), "removing the file turns it on again");
        CHECK(!killed(dir / "does_not_exist"), "a missing folder is not a kill switch");
        fs::remove_all(dir, ec);
    });

    run_case("player_move: the Lua-visible contract through the mailbox call block (op 11: ok, text, status, from_ok, to_ok)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        SimMemory mbm;
        const uint64_t mb = 0x31000000ULL;
        mbm.map(mb, kMailboxSize);
        int seq = 200;
        // what Lua writes (TurboPlayerMove(code, pid, from, to, months, wage)) and what turbo_game_call does with it
        auto op11 = [&](int64_t code, int64_t pid, int64_t from, int64_t to, int64_t months, int64_t wage, int64_t mode_extra = 0) {
            const int64_t args[4] = {static_cast<int64_t>(ListWorld::kComm), code | (months << 8) | mode_extra, pid | (wage << 32), from | (to << 32)};
            CHECK(write_call_request(mbm, mb, ++seq, kCallOpPlayerMove, args), "request written");
            GameCallBlock b;
            CHECK(read_call_block(mbm, mb, b) && b.op == kCallOpPlayerMove && b.seq == seq && b.status == kCallIdle, "request read");
            Request req = request_from_args(b.args);
            req.managers = ListWorld::kManagers;
            Result res = run(w.mem, g, fns, req);
            CHECK(write_call_result(mbm, mb, b.seq, res.ok ? kCallOk : kCallFailed, res.from_ok, res.to_ok, res.message), "result written");
            CHECK(read_call_block(mbm, mb, b) && b.result_seq == seq, "result read");
            CHECK(b.text.size() < kMbCallTextSize, "the text fits the call block");
            return b;
        };
        GameCallBlock b = op11(9, 2002, 12, kUser, 36, 25000);
        CHECK(b.status == kCallOk && b.out[0] == 1 && b.out[1] == 1 && has(b.text, "checked only") && g.moves == 0, "9 check: ok, preconditions 1 / 1: " + b.text);
        b = op11(1, 2002, 12, kUser, 36, 25000);
        CHECK(b.status == kCallOk && b.out[0] == 1 && b.out[1] == 1 && has(b.text, "moved from team 12 to team 48") && has(b.text, "contract record added: 36 months, wage 25000") &&
                  g.moves == 1 && g.adds == 1 && g.last_add.months == 36 && g.last_add.wage == 25000 && g.last_moved.pid == 2002 && g.last_moved.from == 12 && g.last_moved.to == kUser,
              "1 move: ok, left 12 / in 48, the words arrived intact: " + b.text);
        b = op11(1, 2002, 12, kUser, 36, 25000);
        CHECK(b.status == kCallFailed && b.out[0] == -1 && b.out[1] == -1 && has(b.text, "already in team 48") && g.moves == 1, "1 again: failed, nothing read back: " + b.text);
        b = op11(2, 1003, kUser, 0, 0, 0);
        CHECK(b.status == kCallOk && b.out[0] == 1 && b.out[1] == 1 && has(b.text, "released by the game") && g.releases == 1, "2 release: ok, left the club / in a free-agent pool: " + b.text);
        b = op11(2, 2003, 12, 0, 0, 0);
        CHECK(b.status == kCallOk && has(b.text, "to Free Agents") && g.moves == 2, "2 release of an AI club's player: " + b.text);
        b = op11(3, 2004, 12, 13, 0, 0);
        CHECK(b.status == kCallFailed && has(b.text, "unknown player_move code 3"), "code 3: failed: " + b.text);
        b = op11(1, 0, 12, 13, 0, 0);
        CHECK(b.status == kCallFailed && has(b.text, "positive"), "pid 0: failed: " + b.text);
        b = op11(1, 2004, 12, 13, 0, 0, 1 << 20);
        CHECK(b.status == kCallFailed && has(b.text, "unknown bits"), "garbage in the mode word: failed: " + b.text);
        b = op11(1, 2004, 12, 13, 200, 0);
        CHECK(b.status == kCallFailed && has(b.text, "contract length 200"), "months 200: failed: " + b.text);
        // the words: every field survives the packing, the documented formula is the one the DLL reads
        Request q = move_req(kActionMove, 460000, 131368, 1073741823, kMaxMonths, kMaxWage);
        int64_t args[4];
        args_from_request(q, args);
        CHECK(args[0] == static_cast<int64_t>(ListWorld::kComm) && args[1] == (1 | (kMaxMonths << 8)) && args[2] == (460000LL | (static_cast<int64_t>(kMaxWage) << 32)) &&
                  args[3] == (131368LL | (1073741823LL << 32)),
              "args_from_request is the Lua formula: comm, code | months << 8, pid | wage << 32, from | to << 32");
        Request back = request_from_args(args);
        CHECK(back.action == 1 && back.months == kMaxMonths && back.player == 460000 && back.wage == kMaxWage && back.from == 131368 && back.to == 1073741823 &&
                  back.comm == ListWorld::kComm && back.bad_args.empty(),
              "request_from_args decodes it back");
        // negative values never decode to a valid request
        q = move_req(kActionMove, -1, -1, -1, 0, 0);
        args_from_request(q, args);
        back = request_from_args(args);
        Result r = run(w.mem, g, fns, back);
        CHECK(!r.ok && r.stage == "validate", "negative ids are refused: " + r.message);
        CHECK(kMbCallEnd <= kMailboxSize, "the call block holds op 11's words");
    });

    run_case("player_move: every message fits the 512-byte text of the call block (the longest ones)", [&] {
        MoveWorld w;
        FakeMoveGame g(w);
        seed_move_world(w, g);
        g.extra_drop = true;
        w.mem.wr(MoveWorld::kMorale + kMoraleGate, static_cast<uint8_t>(1));
        w.add_player(2002, 0);
        Result r = run(w.mem, g, fns, move_req(kActionMove, 2002, 12, kUser, 120, kMaxWage));
        CHECK(r.ok && r.message.size() < kMbCallTextSize && has(r.message, "WARNING: the morale gate is closed"), "longest success text: " + std::to_string(r.message.size()) + " bytes: " + r.message);
        r = run(w.mem, g, fns, move_req(kActionCheck, 2003, 12, kUser, 120, kMaxWage));
        CHECK(r.ok && r.message.size() < kMbCallTextSize, "longest check text: " + std::to_string(r.message.size()) + " bytes");
        g.put_many(4000, 27, 13);
        r = run(w.mem, g, fns, move_req(kActionMove, 2004, 12, 13));
        CHECK(!r.ok && r.message.size() < kMbCallTextSize, "longest refusal: " + std::to_string(r.message.size()) + " bytes");
    });

    run_case("signatures: the player_move entries resolve on the game's bytes (a look-alike overload next to PlayerMoved does not match) and equal the research JSON files", [&] {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        CHECK(t != nullptr, "built-in table");
        if (!t) return;
        // bytes read from fc27_image.bin (FC27.exe 1.0.140.64835) at each match address (scripts/re/realtime_signatures.json and
        // scripts/re/player_move_signatures.json; every pattern was proven unique in the whole image with sig_jobs.find)
        struct Blob { const char* name; uint64_t va; uint64_t target; std::vector<uint8_t> bytes; };
        const std::vector<Blob> blobs = {
        {"teamutil_player_moved", 0x147DEE368ULL, 0x147DEE368ULL,
         {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x8B, 0x11, 0x45, 0x8B, 0xF0}},
        {"dc_is_player_in_team", 0x147B90A8CULL, 0x147B90A8CULL,
         {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xD8, 0x8B, 0xEA}},
        {"dc_squad_counts", 0x147B7225CULL, 0x147B7225CULL,
         {0x85, 0xD2, 0x7E, 0x77, 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x4C, 0x89, 0x70, 0x20, 0x41, 0x57}},
        {"dc_get_league_of_team", 0x14154B92CULL, 0x14154B92CULL,
         {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x63, 0xF2, 0x48, 0x8D, 0x1D, 0xCF, 0x5B, 0xD7, 0x0B}},
        {"is_international_league", 0x14479F50CULL, 0x14479F50CULL,
         {0x83, 0xF9, 0x4E, 0x74, 0x22, 0x81, 0xF9, 0x58, 0x08, 0x00, 0x00, 0x74, 0x1A, 0x81, 0xF9, 0xBC, 0x0B, 0x00, 0x00, 0x74, 0x12, 0x8B, 0x05, 0x99, 0xD4, 0x6F, 0x07}},
        {"pcm_add_contract_record", 0x147E5BF9CULL, 0x147E5BF9CULL,
         {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55, 0x48, 0x8D, 0x68, 0xC1, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00}},
        {"ctm_release_player", 0x147B94630ULL, 0x147B94630ULL,
         {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0x41, 0x08}},
        {"ctm_vtable", 0x147F180C3ULL, 0x14AFF6D58ULL,
         {0x49, 0x8B, 0x4E, 0x10, 0x48, 0x89, 0x48, 0x08, 0x48, 0x8D, 0x05, 0x86, 0xEC, 0x0D, 0x03, 0x48, 0x89, 0x02, 0xEB, 0x03, 0x48, 0x8B, 0xD6, 0x48, 0x63, 0x8B, 0xD0, 0x03, 0x00, 0x00}},
        {"morale_vtable", 0x147D7DE08ULL, 0x14B0156A8ULL,
         {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x05, 0x8A, 0x78, 0x29, 0x03, 0x48, 0x89, 0x51, 0x08, 0x48, 0x89, 0x01, 0x48, 0x8B, 0xD9, 0x48, 0x83, 0xC1, 0x10, 0xE8, 0x23, 0x0F, 0x00, 0x00, 0x48, 0x8D, 0x8B, 0x18, 0x05, 0x00, 0x00}},
        {"morale_handle_event", 0x147D8D354ULL, 0x147D8D354ULL,
         {0x83, 0xFA, 0x1E, 0x0F, 0x84, 0xF6, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18}},
        };
        CHECK(blobs.size() == 10, "ten entries");
        for (const auto& b : blobs) {
            const Signature* s = t->find(b.name);
            CHECK(s != nullptr && !s->pattern.empty(), std::string("entry ") + b.name);
            if (!s) continue;
            std::vector<uint8_t> code(0x200, 0xCC);
            std::memcpy(code.data() + 0x100, b.bytes.data(), b.bytes.size());
            SigResult r = resolve_signature(*s, code.data(), code.size(), b.va - 0x100);
            CHECK(r.state == SigState::Found && r.match == b.va && r.address == b.target,
                  std::string(b.name) + " resolves: " + r.error + " (match " + hex_addr(r.match) + ", address " + hex_addr(r.address) + ")");
        }
        // all together in one buffer, next to the 5-argument PlayerMoved overload 0x147DEE410 (same prologue up to mov r10,[rcx]): each matches once
        const uint8_t overload[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41,
                                    0x57, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x8B, 0x11, 0x41, 0x8B, 0xF8, 0x4C, 0x8B, 0xF9, 0x45, 0x8B, 0xE1, 0x44, 0x8B, 0xF2, 0x49, 0x8B, 0x82, 0x18};
        std::vector<uint8_t> all(0x2000, 0xCC);
        std::memcpy(all.data() + 0x1800, overload, sizeof(overload));
        for (size_t i = 0; i < blobs.size(); ++i) std::memcpy(all.data() + 0x100 + i * 0x100, blobs[i].bytes.data(), blobs[i].bytes.size());
        for (size_t i = 0; i < blobs.size(); ++i) {
            SigResult r = resolve_signature(*t->find(blobs[i].name), all.data(), all.size(), 0x147000000ULL);
            CHECK(r.hits == 1 && r.match == 0x147000000ULL + 0x100 + i * 0x100, std::string(blobs[i].name) + " unique next to its look-alikes");
        }
        // the research JSON files carry the same patterns, offsets and resolve modes
        auto load = [&](const char* file, SignatureTable& out) {
            fs::path json_path = fs::path("..") / "scripts" / "re" / file;
            std::error_code ec;
            if (!fs::exists(json_path, ec)) json_path = fs::path("scripts") / "re" / file;
            std::ifstream jf(json_path);
            CHECK(static_cast<bool>(jf), std::string(file) + " found at " + json_path.string());
            if (!jf) return false;
            std::string text((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
            std::string err;
            CHECK(parse_signature_table(text, out, err), std::string(file) + " parses: " + err);
            return true;
        };
        SignatureTable realtime, extra;
        const bool have_rt = load("realtime_signatures.json", realtime), have_pm = load("player_move_signatures.json", extra);
        for (const auto& b : blobs) {
            const Signature* a = t->find(b.name);
            const Signature* f = have_rt ? realtime.find(b.name) : nullptr;
            if (!f && have_pm) f = extra.find(b.name);
            CHECK(a && f && a->pattern == f->pattern && a->offset == f->offset && a->resolve == f->resolve, std::string(b.name) + ": built-in entry equals the JSON");
        }
        // every function the Windows host asks the table for exists under exactly that name
        for (const char* n : {"teamutil_player_moved", "dc_is_player_in_team", "dc_squad_counts", "dc_get_league_of_team", "is_international_league", "pcm_add_contract_record",
                              "ctm_release_player", "pcm_vtable", "tm_vtable", "um_vtable", "ctm_vtable", "morale_vtable", "morale_handle_event"})
            CHECK(t->find(n) && !t->find(n)->pattern.empty(), std::string("the host resolves ") + n);
    });
}
