// Native tests: the "player_create" game call (core/player_create.h, docs/re/created_players.md): the game's own INSERT through the
// DataController's query layer, event 0x3A, InsertTeamPlayer into Free Agents and op 11's move into a club, behind the Caller abstraction.
// Included by test_main.cpp after test_player_move.h: CreateWorld adds to MoveWorld the event allocator (a global in the "image"), the
// 0x3A / base event vtables and an event buffer; FakeCreateGame is a fake game whose database keeps players / editedplayernames rows and
// link values and whose InsertTeamPlayer / PostEvent record what they were given (op 11's calls go to FakeMoveGame).
#pragma once

struct CreateWorld : MoveWorld {
    static constexpr uint64_t kAllocGlobal = 0x14C269EA8ULL, kAlloc = 0x30102000ULL, kAllocVt = 0x14B0C0000ULL, kInsVt = 0x14AFF67C0ULL,
                              kBaseVt = 0x149803AE8ULL, kEvBuf = 0x30130000ULL, kFnExecute = 0x1400B0000ULL;
    CreateWorld() {
        mem.map(kAllocGlobal, 0x10);
        mem.map(kAlloc, 0x40);
        mem.map(kAllocVt, 0x40);
        mem.wr(kAllocGlobal, kAlloc);
        mem.wr(kAlloc, kAllocVt);
        for (int i = 0; i < 4; ++i) mem.wr(kAllocVt + 8 * static_cast<uint64_t>(i), 0x1400D0000ULL + 0x10 * static_cast<uint64_t>(i));
        mem.map(kInsVt, 0x40);
        for (int i = 0; i < 6; ++i) mem.wr(kInsVt + 8 * static_cast<uint64_t>(i), 0x147B5C490ULL + 0x100 * static_cast<uint64_t>(i));
        mem.map(kBaseVt, 0x10);
        mem.wr(kBaseVt, 0x147B5BCB0ULL);
        mem.map(kEvBuf, 0x1000);
    }
};

struct FakeCreateGame : turbo::pc::Caller {
    CreateWorld& w;
    FakeMoveGame& m;
    std::map<int, std::map<std::string, int64_t>> players;   // pid -> the players row
    std::map<int, std::map<std::string, std::string>> names; // pid -> the editedplayernames row
    std::map<std::pair<int, int>, std::map<std::string, int64_t>> link;  // (pid, team) -> values written by UPDATE teamplayerlinks
    std::set<std::string> known;  // the columns the fake SQL engine accepts (an unknown one fails the statement: count 0)
    size_t max_statement = 0;
    int inserts = 0, updates = 0, selects = 0, allocs = 0, posts = 0, itps = 0;
    struct Posted { uint64_t dispatcher = 0, ev = 0, vt = 0; int id = 0; int32_t ref = -1, evid = 0, pid = 0; } last_post;
    struct Itp { uint64_t dc = 0; int pid = 0, team = 0, jersey = 0, position = 0, suppress = -1; } last_itp;
    bool fail_insert = false, zero_insert = false, fail_update = false, noop_update = false, fail_select = false, wrong_readback = false, alloc_null = false,
         fail_post = false, noop_itp = false, fail_itp = false, wrong_form = false;
    uint64_t next_ev = CreateWorld::kEvBuf;
    FakeCreateGame(CreateWorld& world, FakeMoveGame& move) : w(world), m(move) {}
    std::vector<std::string>& calls() { return m.calls; }
    // ---- op 11's calls: the move game
    bool in_team(uint64_t dc, int pid, int team, bool& in, std::string& err) override { return m.in_team(dc, pid, team, in, err); }
    bool squad_counts(uint64_t dc, int team, int& r, int& l, int& p, std::string& err) override { return m.squad_counts(dc, team, r, l, p, err); }
    bool league_of_team(uint64_t dc, int team, int& lg, std::string& err) override { return m.league_of_team(dc, team, lg, err); }
    bool is_international(int lg, bool& out, std::string& err) override { return m.is_international(lg, out, err); }
    bool player_moved(uint64_t tu, int pid, int from, int to, std::string& err) override { return m.player_moved(tu, pid, from, to, err); }
    bool add_contract(uint64_t pcm, int pid, int team, int months, int wage, int k, uint64_t date, int status, std::string& err) override {
        return m.add_contract(pcm, pid, team, months, wage, k, date, status, err);
    }
    bool release_player(uint64_t ctm, int pid, int& code, std::string& err) override { return m.release_player(ctm, pid, code, err); }
    // ---- the database layer
    static int where_of(const turbo::pc::Where& wh, const char* col, int dflt) {
        for (const auto& c : wh)
            if (c.first == col) return c.second;
        return dflt;
    }
    bool insert_row(uint64_t dc, const std::string& table, const turbo::pc::Row& cols, int& count, std::string& err) override {
        ++inserts;
        calls().push_back("insert " + table);
        max_statement = std::max(max_statement, cols.size());
        if (fail_insert) return (err = "boom", false);
        count = 0;
        if (zero_insert) return true;
        for (const auto& v : cols)
            if (!known.count(table + "." + v.column)) return true;  // the SQL engine fails the statement
        int pid = -1;
        for (const auto& v : cols)
            if (v.column == "playerid") pid = static_cast<int>(v.i);
        if (pid <= 0) return true;
        if (table == "players") {
            if (players.count(pid)) return true;
            for (const auto& v : cols) players[pid][v.column] = v.i;
        } else if (table == "editedplayernames") {
            if (names.count(pid)) return true;
            for (const auto& v : cols)
                if (v.is_string) names[pid][v.column] = v.s;
            names[pid];
        } else {
            return true;
        }
        count = 1;
        return true;
    }
    bool update_row(uint64_t dc, const std::string& table, const turbo::pc::Row& cols, const turbo::pc::Where& wh, std::string& err) override {
        ++updates;
        calls().push_back("update " + table);
        max_statement = std::max(max_statement, cols.size());
        if (fail_update) return (err = "boom", false);
        if (noop_update) return true;
        for (const auto& v : cols)
            if (!known.count(table + "." + v.column)) return true;
        const int pid = where_of(wh, "playerid", -1);
        if (table == "players") {
            if (!players.count(pid)) return true;
            for (const auto& v : cols) players[pid][v.column] = v.i;
        } else if (table == "teamplayerlinks") {
            const int team = where_of(wh, "teamid", -1);
            const auto it = m.teams.find(pid);
            if (it == m.teams.end() || !it->second.count(team)) return true;
            for (const auto& v : cols) link[{pid, team}][v.column] = wrong_form ? v.i + 1 : v.i;
        }
        return true;
    }
    bool select_ints(uint64_t dc, const std::string& table, const std::vector<std::string>& cols, const turbo::pc::Where& wh, int& rows,
                     std::vector<int64_t>& values, std::string& err) override {
        ++selects;
        calls().push_back("select " + table);
        max_statement = std::max(max_statement, cols.size());
        if (fail_select) return (err = "boom", false);
        values.clear();
        const int pid = where_of(wh, "playerid", -1);
        rows = 0;
        if (table == "players") {
            const auto it = players.find(pid);
            if (it == players.end()) return true;
            rows = 1;
            for (const auto& c : cols) {
                const auto f = it->second.find(c);
                int64_t v = f == it->second.end() ? 0 : f->second;
                if (wrong_readback && c == "overallrating") v = 1;
                values.push_back(v);
            }
        } else if (table == "editedplayernames") {
            rows = names.count(pid) ? 1 : 0;
            if (rows)
                for (const auto& c : cols) values.push_back(c == "playerid" ? pid : 0);
        } else if (table == "teamplayerlinks") {
            const int team = where_of(wh, "teamid", -1);
            const auto it = m.teams.find(pid);
            if (it != m.teams.end())
                for (int t : it->second)
                    if (team < 0 || t == team) ++rows;
            if (rows)
                for (const auto& c : cols) {
                    const auto lv = link.find({pid, team});
                    int64_t v = c == "playerid" ? pid : 0;
                    if (lv != link.end() && lv->second.count(c)) v = lv->second.at(c);
                    values.push_back(v);
                }
        }
        return true;
    }
    bool alloc_event(uint64_t size, uint64_t& ev, std::string& err) override {
        ++allocs;
        calls().push_back("alloc event");
        if (size != turbo::pc::kEventSize) return (err = "wrong size", false);
        ev = alloc_null ? 0 : next_ev;
        if (!alloc_null) next_ev += 0x40;
        return true;
    }
    bool post_event(uint64_t dispatcher, int id, uint64_t ev, std::string& err) override {
        ++posts;
        calls().push_back("post event");
        if (fail_post) return (err = "boom", false);
        last_post.dispatcher = dispatcher;
        last_post.id = id;
        last_post.ev = ev;
        w.mem.rd(ev + turbo::pc::kEvVtable, last_post.vt);
        w.mem.rd(ev + turbo::pc::kEvRefcount, last_post.ref);
        w.mem.rd(ev + turbo::pc::kEvId, last_post.evid);
        w.mem.rd(ev + turbo::pc::kEvPid, last_post.pid);
        return true;
    }
    bool insert_team_player(uint64_t dc, int pid, int team, int jersey, int position, int suppress, int& ret, std::string& err) override {
        ++itps;
        calls().push_back("insert team player");
        last_itp = {dc, pid, team, jersey, position, suppress};
        if (fail_itp) return (err = "boom", false);
        ret = 1;
        if (!noop_itp) m.teams[pid].insert(team);
        return true;
    }
};

static turbo::pc::Columns create_columns() {
    turbo::pc::Columns c;
    auto& p = c["players"];
    p.insert("playerid");
    p.insert("overallrating");
    for (int i = 0; i < 150; ++i) {
        char b[16];
        std::snprintf(b, sizeof(b), "c%03d", i);
        p.insert(b);
    }
    c["editedplayernames"] = {"playerid", "firstname", "surname", "commonname", "playerjerseyname"};
    c["teamplayerlinks"] = {"playerid", "teamid", "jerseynumber", "position", "form", "artificialkey"};
    return c;
}

static void know_columns(FakeCreateGame& g) {
    for (const auto& t : create_columns())
        for (const auto& col : t.second) g.known.insert(t.first + "." + col);
}

// The payload Lua writes: playerid + ncols synthetic columns + overallrating, names (an accented first name), form 3
static nlohmann::json create_payload(int seq, int pid, int team, int ncols = 120, int months = 0, int wage = 0, bool names = true) {
    nlohmann::json j;
    j["seq"] = seq;
    j["playerid"] = pid;
    j["team"] = team;
    j["months"] = months;
    j["wage"] = wage;
    nlohmann::json p = nlohmann::json::object();
    p["playerid"] = pid;
    for (int i = 0; i < ncols; ++i) {
        char b[16];
        std::snprintf(b, sizeof(b), "c%03d", i);
        p[b] = i * 3 + 1;
    }
    p["overallrating"] = 77;
    j["players"] = p;
    if (names) j["names"] = {{"firstname", "Jo\xC3\xA3o"}, {"surname", "Silva"}, {"commonname", ""}, {"playerjerseyname", "Silva"}};
    j["link"] = {{"form", 3}};
    return j;
}

static turbo::pc::Request create_req(int action, int seq, int pid, const nlohmann::json& payload) {
    turbo::pc::Request q;
    q.action = action;
    q.seq = seq;
    q.player = pid;
    q.comm = ListWorld::kComm;
    q.managers = ListWorld::kManagers;
    q.payload_error = turbo::pc::parse_payload(payload.dump(), q.payload);
    q.columns = create_columns();
    return q;
}

static turbo::pc::Fns create_fns() {
    turbo::pc::Fns f;
    f.move = move_fns();
    f.query_init = 0x14197D5F4ULL;
    f.query_set_int = 0x140601EA8ULL;
    f.query_set_string = 0x14403F2E8ULL;
    f.query_select = 0x140601F14ULL;
    f.query_where_int = 0x140602D28ULL;
    f.query_destroy = 0x14154EB38ULL;
    f.result_free = 0x1422985A4ULL;
    f.provider_execute = CreateWorld::kFnExecute;
    f.event_allocator = CreateWorld::kAllocGlobal;
    f.event_base_vtable = CreateWorld::kBaseVt;
    f.inserted_vtable = CreateWorld::kInsVt;
    f.insert_team_player = 0x147B90074ULL;
    f.post_event = 0x14060124CULL;
    return f;
}

static void test_player_create() {
    using namespace turbo;
    using namespace turbo::pc;
    const Fns fns = create_fns();
    constexpr int kUser = ListWorld::kNapoli, kFa = MoveWorld::kFa;
    auto has = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
    auto index_of = [](const std::vector<std::string>& v, const char* what) {
        const auto it = std::find(v.begin(), v.end(), std::string(what));
        return it == v.end() ? -1 : static_cast<int>(it - v.begin());
    };
    auto count_of = [](const std::vector<std::string>& v, const char* what) { return static_cast<int>(std::count(v.begin(), v.end(), std::string(what))); };
    // writes into the game: anything but the read-only calls
    auto writes = [&](const FakeCreateGame& g) { return g.inserts + g.updates + g.allocs + g.posts + g.itps + g.m.moves + g.m.adds; };

    run_case("player_create: codes, op number, file names, constants (the Lua-visible contract)", [&] {
        CHECK(kActionCreate == 1 && kActionCheck == 9 && valid_action(1) && valid_action(9) && !valid_action(2) && !valid_action(0), "codes 1 create, 9 check");
        CHECK(kCallOpPlayerCreate == 12 && kCallOpPlayerCreate != kCallOpPlayerMove && kCallOpPlayerCreate != kCallOpTransferList && kCallOpPlayerCreate != kCallOpReveal &&
                  kCallOpPlayerCreate != kCallOpJobOffer && kCallOpPlayerCreate != kCallOpStandingsRefresh && kCallOpPlayerCreate != kCallOpManagerRules,
              "op 12 clashes with no other op");
        CHECK(std::string(payload_name()) == "turbo_player_create.json" && std::string(opt_in_name()) == "call_player_create_on.txt" &&
                  std::string(kill_switch_name()) == "call_player_create_off.txt",
              "file names");
        CHECK(kFaJersey == 99 && kFaPosition == 29 && kEventPlayerInserted == 0x3A && kEventSize == 0x20 && kEvPid == 0x18 && kEvId == 0x10 && kMaxString == 44 &&
                  kMaxPlayerId == 460000 && kOutOff == -1,
              "the game's own values (CreatePlayer / create-into-club)");
        CHECK(kColumnsPerStatement <= 59, "a statement never carries more columns than the game's own INSERT (59 setters)");
        CHECK(std::string(Fns().missing()) == "teamutil_player_moved" && fns.missing() == nullptr, "missing() names the first absent entry");
    });

    run_case("player_create: into Free Agents: INSERT players (+ UPDATE), read-back, names, event 0x3A, InsertTeamPlayer, read-back, form", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        Result r = run(w.mem, g, fns, create_req(kActionCreate, 7, 300001, create_payload(7, 300001, kFa)));
        CHECK(r.ok && r.stage == "done", "ok: " + r.message);
        CHECK(r.written == (kWrotePlayers | kWroteNames | kWroteEvent | kWroteFaLink | kWroteLink) && r.in_team == 1 && r.final_team == kFa, "written mask + read-back");
        CHECK(g.players.count(300001) && g.players[300001].size() == 122 && g.players[300001]["c119"] == 119 * 3 + 1 && g.players[300001]["overallrating"] == 77,
              "every column of the payload is in the game's row (INSERT + UPDATE)");
        CHECK(g.max_statement <= kColumnsPerStatement && g.inserts == 2 && g.updates >= 2, "122 columns in statements of at most 48 (1 INSERT + 2 UPDATE players, 1 INSERT names)");
        CHECK(g.names[300001]["firstname"] == "Jo\xC3\xA3o" && g.names[300001]["surname"] == "Silva" && !g.names[300001].count("commonname"), "names (empty ones are left out)");
        CHECK(g.posts == 1 && g.last_post.id == 0x3A && g.last_post.dispatcher == w.dispatcher() && g.last_post.vt == CreateWorld::kInsVt && g.last_post.ref == 0 &&
                  g.last_post.evid == 0x3A && g.last_post.pid == 300001 && g.allocs == 1,
              "event 0x3A exactly as CreatePlayer builds it, posted to **(hub+0x4F8)");
        CHECK(g.itps == 1 && g.last_itp.dc == MoveWorld::kDc && g.last_itp.pid == 300001 && g.last_itp.team == kFa && g.last_itp.jersey == 99 && g.last_itp.position == 29 &&
                  g.last_itp.suppress == 0,
              "InsertTeamPlayer(dc, pid, 111592, 99, 29, 0)");
        CHECK(mg.moves == 0 && mg.adds == 0, "no move for Free Agents");
        CHECK((g.link[{300001, kFa}]["form"] == 3), "form 3 on the Free Agents link");
        const auto& c = g.calls();
        const int ins = index_of(c, "insert players"), upd = index_of(c, "update players"), nam = index_of(c, "insert editedplayernames"),
                  ev = index_of(c, "post event"), itp = index_of(c, "insert team player"), lk = index_of(c, "update teamplayerlinks");
        CHECK(ins >= 0 && upd > ins && nam > upd && ev > nam && itp > ev && lk > itp, "order: players -> names -> 0x3A -> link -> form");
        CHECK(index_of(c, "in_team") > itp || count_of(c, "in_team") >= 2, "IsPlayerInTeam read back after InsertTeamPlayer");
        CHECK(ins > 0 && index_of(c, "select players") < ins && index_of(c, "select teamplayerlinks") < ins && index_of(c, "select editedplayernames") < ins,
              "the id was checked free in the three tables before the INSERT");
        CHECK(has(r.message, "created through the game's database") && has(r.message, "Free Agents") && r.message.size() < kMbCallTextSize, "message: " + r.message);
    });

    run_case("player_create: into an AI club: op 11's PlayerMoved FA -> club, no contract record; form on the club link", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        Result r = run(w.mem, g, fns, create_req(kActionCreate, 8, 300002, create_payload(8, 300002, 12, 40, 36, 20000)));
        CHECK(r.ok && r.final_team == 12 && r.in_team == 1, "ok: " + r.message);
        CHECK(mg.moves == 1 && mg.last_moved.pid == 300002 && mg.last_moved.from == kFa && mg.last_moved.to == 12 && mg.adds == 0, "PlayerMoved(FA -> 12), no record");
        CHECK(mg.teams[300002] == std::set<int>({12}) && mg.rows(12) == 26, "in the club, not in Free Agents");
        CHECK((r.written & kWroteMoved) && !(r.written & kWroteContract) && (g.link[{300002, 12}]["form"] == 3), "mask + form on the club link");
        CHECK(index_of(g.calls(), "moved") > index_of(g.calls(), "insert team player"), "the move comes after the Free Agents link");
        CHECK(g.inserts == 2 && g.updates == 1, "40 columns: one INSERT players (+ names), only the link UPDATE");
    });

    run_case("player_create: into the user's club: the move adds the contract record (months, wage)", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        Result r = run(w.mem, g, fns, create_req(kActionCreate, 9, 300003, create_payload(9, 300003, kUser, 60, 36, 25000)));
        CHECK(r.ok && r.final_team == kUser, "ok: " + r.message);
        CHECK(mg.adds == 1 && mg.last_add.pid == 300003 && mg.last_add.team == kUser && mg.last_add.months == 36 && mg.last_add.wage == 25000, "AddContractRecord");
        int32_t team = 0, wage = 0;
        CHECK(w.record(300003, team, wage) && team == kUser && wage == 25000, "the record exists");
        CHECK((r.written & kWroteContract) && (r.written & kWroteMoved) && mg.morale.count(300003) == 1, "mask, morale entry by event 0x5F");
        CHECK(has(r.message, "contract record added") && r.message.size() < kMbCallTextSize, "message: " + r.message);
    });

    run_case("player_create: check only (code 9) validates everything and writes nothing", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        Result r = run(w.mem, g, fns, create_req(kActionCheck, 10, 300004, create_payload(10, 300004, kUser, 60, 36, 25000)));
        CHECK(r.ok && r.in_team == 1 && r.written == 0 && writes(g) == 0 && has(r.message, "checked only"), "check: " + r.message);
        CHECK(g.selects == 3 && mg.counts_asked.size() == 1, "three id SELECTs and one SquadCounts");
    });

    run_case("player_create: refusals before anything is written (id in use, link, national / pseudo team, squad full, ranges)", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        g.players[300010]["playerid"] = 300010;
        Result r = run(w.mem, g, fns, create_req(kActionCreate, 1, 300010, create_payload(1, 300010, kFa)));
        CHECK(!r.ok && r.stage == "validate" && has(r.message, "is in use: players") && writes(g) == 0, "players row exists: " + r.message);
        mg.teams[300011].insert(12);
        r = run(w.mem, g, fns, create_req(kActionCreate, 2, 300011, create_payload(2, 300011, kFa)));
        CHECK(!r.ok && has(r.message, "is in use: teamplayerlinks") && writes(g) == 0, "an existing link: " + r.message);
        g.names[300012]["surname"] = "x";
        r = run(w.mem, g, fns, create_req(kActionCreate, 3, 300012, create_payload(3, 300012, kFa)));
        CHECK(!r.ok && has(r.message, "is in use: editedplayernames") && writes(g) == 0, "a names row: " + r.message);
        r = run(w.mem, g, fns, create_req(kActionCreate, 4, 300013, create_payload(4, 300013, 1370)));
        CHECK(!r.ok && has(r.message, "national team") && writes(g) == 0, "national team: " + r.message);
        r = run(w.mem, g, fns, create_req(kActionCreate, 5, 300013, create_payload(5, 300013, 0x1B688)));
        CHECK(!r.ok && has(r.message, "pseudo team") && writes(g) == 0, "youth pool: " + r.message);
        r = run(w.mem, g, fns, create_req(kActionCreate, 6, 300013, create_payload(6, 300013, 1373)));
        CHECK(!r.ok && has(r.message, "knows no league") && writes(g) == 0, "unknown team: " + r.message);
        mg.put_many(4000, 27, 13);
        r = run(w.mem, g, fns, create_req(kActionCreate, 7, 300013, create_payload(7, 300013, 13)));
        CHECK(!r.ok && has(r.message, "squad is full") && writes(g) == 0, "squad full (52): " + r.message);
        r = run(w.mem, g, fns, create_req(kActionCreate, 8, 460000, create_payload(8, 460000, kFa)));
        CHECK(!r.ok && has(r.message, "out of range") && writes(g) == 0, "460000: " + r.message);
        r = run(w.mem, g, fns, create_req(kActionCreate, 9, 31399, create_payload(9, 31399, kFa)));
        CHECK(!r.ok && has(r.message, "player-career") && writes(g) == 0, "the player-career id: " + r.message);
        r = run(w.mem, g, fns, create_req(kActionCreate, 10, 300013, create_payload(10, 300013, kFa, 10, 200)));
        CHECK(!r.ok && has(r.message, "contract length 200") && writes(g) == 0, "months 200: " + r.message);
        r = run(w.mem, g, fns, create_req(3, 11, 300013, create_payload(11, 300013, kFa)));
        CHECK(!r.ok && has(r.message, "unknown player_create code 3") && writes(g) == 0, "code 3: " + r.message);
        Request q = create_req(kActionCreate, 12, 300013, create_payload(12, 300013, kFa));
        q.columns.clear();
        r = run(w.mem, g, fns, q);
        CHECK(!r.ok && has(r.message, "bridge_meta.json") && writes(g) == 0, "no columns known: " + r.message);
        CHECK(g.inserts == 0 && mg.moves == 0, "nothing reached the database");
    });

    run_case("player_create: bad payloads are refused (float, unknown column / table, long string, seq / playerid mismatch, booleans)", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        struct Bad { const char* what; std::function<void(nlohmann::json&)> edit; const char* expect; int seq; int pid; };
        const std::vector<Bad> bads = {
            {"float", [](nlohmann::json& j) { j["players"]["overallrating"] = 77.5; }, "is a float", 20, 300020},
            {"integral float", [](nlohmann::json& j) { j["players"]["c001"] = 4.0; }, "is a float", 20, 300020},
            {"float names? no: a number", [](nlohmann::json& j) { j["names"]["surname"] = 5; }, "is not a string", 20, 300020},
            {"unknown column", [](nlohmann::json& j) { j["players"]["notacolumn"] = 1; }, "unknown column players.notacolumn", 20, 300020},
            {"bad column name", [](nlohmann::json& j) { j["players"]["DROP TABLE"] = 1; }, "is not a column name", 20, 300020},
            {"unknown table", [](nlohmann::json& j) { j["teams"] = nlohmann::json::object(); }, "unknown table or key", 20, 300020},
            {"unknown names column", [](nlohmann::json& j) { j["names"]["nickname"] = "x"; }, "unknown names column", 20, 300020},
            {"unknown link column", [](nlohmann::json& j) { j["link"]["teamid"] = 5; }, "unknown link column", 20, 300020},
            {"45-byte string", [](nlohmann::json& j) { j["names"]["surname"] = std::string(45, 'a'); }, "45 bytes long", 20, 300020},
            {"boolean", [](nlohmann::json& j) { j["players"]["c002"] = true; }, "not an integer", 20, 300020},
            {"string in players", [](nlohmann::json& j) { j["players"]["c003"] = "7"; }, "is a string", 20, 300020},
            {"too big", [](nlohmann::json& j) { j["players"]["c004"] = 4294967296LL; }, "32-bit", 20, 300020},
            {"seq mismatch", [](nlohmann::json& j) { j["seq"] = 19; }, "request #19, not #20", 20, 300020},
            {"playerid mismatch", [](nlohmann::json& j) { j["playerid"] = 300021; j["players"]["playerid"] = 300021; }, "is for player 300021", 20, 300020},
            {"players.playerid differs", [](nlohmann::json& j) { j["players"]["playerid"] = 5; }, "is not the payload's playerid", 20, 300020},
            {"no players", [](nlohmann::json& j) { j.erase("players"); }, "no \"players\"", 20, 300020},
            {"no seq", [](nlohmann::json& j) { j.erase("seq"); }, "no \"seq\"", 20, 300020},
        };
        for (const auto& b : bads) {
            nlohmann::json j = create_payload(20, 300020, kFa);
            b.edit(j);
            Result r = run(w.mem, g, fns, create_req(kActionCreate, b.seq, b.pid, j));
            CHECK(!r.ok && r.stage == "validate" && has(r.message, b.expect) && writes(g) == 0, std::string(b.what) + ": " + r.message);
        }
        Payload p;
        CHECK(!parse_payload("not json", p).empty() && !parse_payload("[1,2]", p).empty() && !parse_payload(std::string(kMaxPayloadBytes + 1, ' '), p).empty(),
              "not an object / too large");
        CHECK(parse_payload(create_payload(1, 5, kFa, 3).dump(), p).empty() && p.players.size() == 5 && p.players[0].column == "playerid" && p.players[0].i == 5 &&
                  p.names.size() == 3 && p.link.size() == 1,
              "a good payload: playerid first, the empty commonname dropped");
        // a 44-byte name is fine (the column holds 44 bytes + NUL)
        nlohmann::json j = create_payload(21, 300022, kFa);
        j["names"]["surname"] = std::string(44, 'b');
        Result r = run(w.mem, g, fns, create_req(kActionCreate, 21, 300022, j));
        CHECK(r.ok && g.names[300022]["surname"].size() == 44, "44 bytes: " + r.message);
    });

    run_case("player_create: a missing signature refuses the call before anything is called", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        const std::vector<std::pair<const char*, std::function<void(Fns&)>>> misses = {
            {"db_query_init", [](Fns& f) { f.query_init = 0; }},          {"db_query_set_int", [](Fns& f) { f.query_set_int = 0; }},
            {"db_query_set_string", [](Fns& f) { f.query_set_string = 0; }}, {"db_query_select_field", [](Fns& f) { f.query_select = 0; }},
            {"db_query_where_int", [](Fns& f) { f.query_where_int = 0; }}, {"db_query_destroy", [](Fns& f) { f.query_destroy = 0; }},
            {"db_result_free", [](Fns& f) { f.result_free = 0; }},        {"db_provider_execute", [](Fns& f) { f.provider_execute = 0; }},
            {"event_allocator_global", [](Fns& f) { f.event_allocator = 0; }}, {"event_base_vtable", [](Fns& f) { f.event_base_vtable = 0; }},
            {"player_inserted_event_vtable", [](Fns& f) { f.inserted_vtable = 0; }}, {"dc_insert_team_player", [](Fns& f) { f.insert_team_player = 0; }},
            {"post_career_event", [](Fns& f) { f.post_event = 0; }},      {"teamutil_player_moved", [](Fns& f) { f.move.player_moved = 0; }},
            {"pcm_add_contract_record", [](Fns& f) { f.move.add_contract = 0; }},
        };
        for (const auto& mm : misses) {
            Fns f = fns;
            mm.second(f);
            const int before = static_cast<int>(g.calls().size());
            Result r = run(w.mem, g, f, create_req(kActionCreate, 30, 300030, create_payload(30, 300030, kFa)));
            CHECK(!r.ok && r.stage == "validate" && has(r.message, (std::string("game function ") + mm.first + " is not resolved").c_str()) &&
                      static_cast<int>(g.calls().size()) == before,
                  std::string(mm.first) + " missing: " + r.message);
        }
    });

    run_case("player_create: a mismatched provider / allocator / event vtable is refused before anything is called", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        Fns f = fns;
        f.provider_execute = 0x1400B0010ULL;  // the provider's slot 1 is another function: not the class the research describes
        Result r = run(w.mem, g, f, create_req(kActionCreate, 31, 300031, create_payload(31, 300031, kFa)));
        CHECK(!r.ok && has(r.message, "db provider vtable slot 1") && g.calls().empty(), "provider: " + r.message);
        f = fns;
        f.inserted_vtable = f.event_base_vtable;
        r = run(w.mem, g, f, create_req(kActionCreate, 31, 300031, create_payload(31, 300031, kFa)));
        CHECK(!r.ok && has(r.message, "is the event base vtable") && g.calls().empty(), "0x3A vtable == base: " + r.message);
        f = fns;
        f.inserted_vtable = 0x14AFF0000ULL;  // not mapped: no slots
        r = run(w.mem, g, f, create_req(kActionCreate, 31, 300031, create_payload(31, 300031, kFa)));
        CHECK(!r.ok && has(r.message, "the 0x3A event vtable") && g.calls().empty(), "unreadable 0x3A vtable: " + r.message);
        w.mem.wr(CreateWorld::kAllocGlobal, static_cast<uint64_t>(0));
        r = run(w.mem, g, fns, create_req(kActionCreate, 31, 300031, create_payload(31, 300031, kFa)));
        CHECK(!r.ok && has(r.message, "event allocator") && g.calls().empty(), "no allocator: " + r.message);
        w.mem.wr(CreateWorld::kAllocGlobal, CreateWorld::kAlloc);
        Request q = create_req(kActionCreate, 31, 300031, create_payload(31, 300031, kFa));
        q.image_base = 0x140000000ULL;
        q.image_size = 0x100000ULL;  // every resolved address is outside this "image"
        r = run(w.mem, g, fns, q);
        CHECK(!r.ok && has(r.message, "outside FC27.exe") && g.calls().empty(), "outside the image: " + r.message);
        w.mem.wr(MoveWorld::kSimDay + 0x14, static_cast<int32_t>(2));
        r = run(w.mem, g, fns, create_req(kActionCreate, 31, 300031, create_payload(31, 300031, kFa)));
        CHECK(!r.ok && g.calls().empty(), "a match day being processed: " + r.message);
    });

    run_case("player_create: a failure after the INSERT reports what exists (and the game doing nothing is never assumed)", [&] {
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            g.zero_insert = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 40, 300040, create_payload(40, 300040, kFa)));
            CHECK(!r.ok && r.stage == "call" && r.written == 0 && has(r.message, "affected 0 rows") && has(r.message, "nothing was written") && g.posts == 0 && g.itps == 0,
                  "INSERT count 0: " + r.message);
        }
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            g.known.erase("players.c100");  // the engine refuses a column the meta has: the UPDATE does nothing, the read-back sees it
            g.noop_update = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 41, 300041, create_payload(41, 300041, kFa)));
            CHECK(r.ok && has(r.message, "WARNING") && has(r.message, "read back differently"), "UPDATE ignored: reported by the read-back: " + r.message);
        }
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            g.fail_update = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 42, 300042, create_payload(42, 300042, kFa)));
            CHECK(!r.ok && r.stage == "call" && r.written == kWrotePlayers && has(r.message, "what exists now: players row") && g.posts == 0, "UPDATE fails: " + r.message);
        }
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            g.noop_itp = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 43, 300043, create_payload(43, 300043, 12)));
            CHECK(!r.ok && r.stage == "check" && r.in_team == 0 && r.written == (kWrotePlayers | kWroteNames | kWroteEvent) &&
                      has(r.message, "does not see player 300043 in Free Agents") && has(r.message, "players row, editedplayernames row, event 0x3A posted") && mg.moves == 0,
                  "InsertTeamPlayer did nothing: " + r.message);
        }
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            mg.fail_moved = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 44, 300044, create_payload(44, 300044, 12)));
            CHECK(!r.ok && (r.written & kWroteFaLink) && !(r.written & kWroteMoved) && has(r.message, "created in Free Agents, but the move to team 12 failed") &&
                      has(r.message, "Free Agents link") && r.message.size() < kMbCallTextSize,
                  "the move fails: " + r.message);
        }
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            g.alloc_null = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 45, 300045, create_payload(45, 300045, kFa)));
            CHECK(!r.ok && g.posts == 0 && g.itps == 0 && r.written == (kWrotePlayers | kWroteNames) && has(r.message, "no memory"), "allocator null: " + r.message);
        }
        {
            CreateWorld w;
            FakeMoveGame mg(w);
            seed_move_world(w, mg);
            FakeCreateGame g(w, mg);
            know_columns(g);
            g.wrong_readback = true;
            g.wrong_form = true;
            Result r = run(w.mem, g, fns, create_req(kActionCreate, 46, 300046, create_payload(46, 300046, kFa)));
            CHECK(r.ok && has(r.message, "overallrating 77 -> 1") && has(r.message, "form 3 -> 4") && !(r.written & kWroteLink), "read-back differences are reported: " + r.message);
        }
    });

    run_case("player_create: the opt-in and kill switch files (core part: names and presence)", [&] {
        const fs::path dir = fs::temp_directory_path() / "turbo_player_create_switch_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        CHECK(!opted_in(dir) && !killed(dir), "no files: not opted in (OFF by default), not killed");
        std::ofstream((dir / "call_player_move_off.txt").string()) << "";
        std::ofstream((dir / "call_player_move_on.txt").string()) << "";
        CHECK(!opted_in(dir) && !killed(dir), "the other calls' files do not count");
        std::ofstream((dir / opt_in_name()).string()) << "";
        CHECK(opted_in(dir), "call_player_create_on.txt opts in");
        std::ofstream((dir / kill_switch_name()).string()) << "";
        CHECK(killed(dir), "call_player_create_off.txt is the kill switch");
        fs::remove_all(dir, ec);
        CHECK(!opted_in(dir / "missing") && !killed(dir / "missing"), "a missing folder is neither");
    });

    run_case("player_create: the payload file and the database columns are read from turbo_output (load_payload)", [&] {
        const fs::path dir = fs::temp_directory_path() / "turbo_player_create_payload_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        Request q;
        load_payload(dir, q);
        CHECK(has(q.payload_error, "missing"), "no file: " + q.payload_error);
        std::ofstream((dir / payload_name()).string()) << create_payload(5, 300050, kFa, 3).dump();
        std::ofstream((dir / "bridge_meta.json").string())
            << R"({"shortname_name_tables_map":{"plyr":"players","edpn":"editedplayernames"},"field_desc_map":{"plyr":{"pid1":{"name":"playerid","depth":20,"min":0},"ovr1":{"name":"overallrating","depth":7,"min":1}},"edpn":{"pid2":{"name":"playerid"}}}})";
        q = Request();
        load_payload(dir, q);
        CHECK(q.payload_error.empty() && q.payload.playerid == 300050 && q.payload.seq == 5, "payload: " + q.payload_error);
        CHECK(q.columns.count("players") && q.columns["players"].count("overallrating") && q.columns["players"].count("playerid") && q.columns.count("editedplayernames"),
              "the long column names from bridge_meta.json");
        fs::remove_all(dir, ec);
    });

    run_case("player_create: the Lua-visible contract through the mailbox call block (op 12)", [&] {
        CreateWorld w;
        FakeMoveGame mg(w);
        seed_move_world(w, mg);
        FakeCreateGame g(w, mg);
        know_columns(g);
        SimMemory mbm;
        const uint64_t mb = 0x31000000ULL;
        mbm.map(mb, kMailboxSize);
        auto op12 = [&](int64_t code, int64_t seq, int64_t pid, const nlohmann::json& payload) {
            const int64_t args[4] = {static_cast<int64_t>(ListWorld::kComm), code, seq, pid};
            CHECK(write_call_request(mbm, mb, static_cast<int32_t>(seq), kCallOpPlayerCreate, args), "request written");
            GameCallBlock b;
            CHECK(read_call_block(mbm, mb, b) && b.op == kCallOpPlayerCreate, "request read");
            Request req = request_from_args(b.args);
            req.managers = ListWorld::kManagers;
            req.payload_error = parse_payload(payload.dump(), req.payload);
            req.columns = create_columns();
            Result res = run(w.mem, g, fns, req);
            CHECK(write_call_result(mbm, mb, b.seq, res.ok ? kCallOk : kCallFailed, res.written, res.in_team, res.message), "result written");
            CHECK(read_call_block(mbm, mb, b), "result read");
            return b;
        };
        GameCallBlock b = op12(9, 60, 300060, create_payload(60, 300060, kUser, 10, 24, 9000));
        CHECK(b.status == kCallOk && b.out[0] == 0 && b.out[1] == 1 && has(b.text, "checked only"), "9: " + b.text);
        b = op12(1, 61, 300060, create_payload(61, 300060, kUser, 10, 24, 9000));
        CHECK(b.status == kCallOk && b.out[0] == (kWrotePlayers | kWroteNames | kWroteEvent | kWroteFaLink | kWroteMoved | kWroteContract | kWroteLink) && b.out[1] == 1,
              "1: everything written, in the club: " + b.text);
        b = op12(1, 62, 300060, create_payload(62, 300060, kUser, 10, 24, 9000));
        CHECK(b.status == kCallFailed && b.out[0] == 0 && has(b.text, "in use"), "1 again: refused: " + b.text);
        // the words
        Request q;
        q.comm = ListWorld::kComm;
        q.action = 1;
        q.seq = 123;
        q.player = 459999;
        int64_t args[4];
        args_from_request(q, args);
        CHECK(args[1] == 1 && args[2] == 123 && args[3] == 459999, "args: comm, code, seq, playerid");
        Request back = request_from_args(args);
        CHECK(back.bad_args.empty() && back.action == 1 && back.seq == 123 && back.player == 459999 && back.comm == ListWorld::kComm, "decoded back");
        args[1] = 1 | (1 << 9);
        CHECK(!request_from_args(args).bad_args.empty(), "unknown bits in the code word");
        args[1] = 1;
        args[2] = 0;
        CHECK(!request_from_args(args).bad_args.empty(), "seq 0");
        args[2] = 5;
        args[3] = -4;
        CHECK(!request_from_args(args).bad_args.empty(), "negative player id");
        args[3] = 0x100000005LL;
        CHECK(!request_from_args(args).bad_args.empty(), "a player id above 32 bits");
    });

    run_case("signatures: the player_create entries resolve on the game's bytes and equal scripts/re/created_players_signatures.json", [&] {
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        CHECK(t != nullptr, "built-in table");
        if (!t) return;
        // bytes read from fc27_image.bin (FC27.exe 1.0.140.64835) at each match address; each pattern unique in the whole image
        struct Blob { const char* name; uint64_t va; uint64_t target; std::vector<uint8_t> bytes; std::vector<uint8_t> at_offset; };
        const std::vector<Blob> blobs = {
        {"db_query_init", 0x14197D5F4ULL, 0x14197D5F4ULL, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0xC0, 0x89, 0x11, 0x48, 0x8B, 0xD9, 0x48, 0xC7, 0x44, 0x24, 0x30, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD0, 0x4C, 0x8D, 0x44, 0x24, 0x30, 0x48, 0x83, 0xC1, 0x08, 0xE8, 0x14, 0xC3, 0xDB, 0xFE, 0x48, 0x8B, 0xC3, 0x48, 0xC7, 0x43, 0x20, 0x00, 0x00, 0x00, 0x00}, {}},
        {"db_query_set_int", 0x140601EA8ULL, 0x140601EA8ULL, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF0, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xE9}, {}},
        {"db_query_set_string", 0x147BA1476ULL, 0x14403F2E8ULL, {0x4C, 0x8D, 0x43, 0x08, 0x48, 0x8D, 0x15, 0x77, 0x3D, 0xA9, 0x01, 0x48, 0x8D, 0x4D, 0xA0, 0xE8, 0x5E, 0xDE, 0x49, 0xFC, 0x4C, 0x8D, 0x43, 0x35, 0x48, 0x8D, 0x15, 0x5B, 0x3D, 0xA9, 0x01}, {}},
        {"db_query_select_field", 0x140601F14ULL, 0x140601F14ULL, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0xE8, 0x26, 0xCB, 0xF4, 0x00, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xC8}, {}},
        {"db_query_where_int", 0x140602D28ULL, 0x140602D28ULL, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xE9, 0x41, 0x8B, 0xF8}, {}},
        {"db_query_destroy", 0x14154EB38ULL, 0x14154EB38ULL, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x51, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x0D, 0x50, 0xB3, 0xD1, 0x0A}, {}},
        {"db_result_free", 0x1422985A4ULL, 0x1422985A4ULL, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x19, 0x48, 0x8B, 0x3D, 0xE8, 0x18, 0xFD, 0x09, 0x48, 0x85, 0xDB, 0x74, 0x19, 0x48, 0x8B, 0x03, 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xFF, 0x10, 0x48, 0x8B, 0x07, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x18, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xCC, 0xCC, 0xCC, 0x48, 0x83, 0xEC, 0x28}, {}},
        {"db_provider_execute", 0x142297D8CULL, 0x142297D8CULL, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x0A}, {}},
        {"event_allocator_global", 0x147B61DB7ULL, 0x14C269EA8ULL, {0x48, 0x8B, 0x0D, 0xEA, 0x80, 0x70, 0x04, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x10, 0x41, 0x8D, 0x54, 0x24, 0x3A, 0x4C, 0x8B, 0xC0, 0x48, 0x85, 0xC0}, {}},
        {"event_base_vtable", 0x147B61DD3ULL, 0x149803AE8ULL, {0x48, 0x8D, 0x0D, 0x0E, 0x1D, 0xCA, 0x01, 0x49, 0x89, 0x08, 0x33, 0xC9, 0x41, 0x89, 0x48, 0x08, 0x48, 0x8D, 0x0D, 0xD6, 0x49, 0x49, 0x03, 0x41, 0x87, 0x58, 0x08, 0x49, 0x89, 0x08, 0x41, 0x89, 0x50, 0x10, 0x41, 0x89, 0x40, 0x18, 0xEB, 0x03}, {}},
        {"player_inserted_event_vtable", 0x147B61750ULL, 0x14AFF67C0ULL, {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD9, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x49, 0x8B, 0xD8}, {0x48, 0x8D, 0x0D, 0xD6, 0x49, 0x49, 0x03}},
        {"dc_insert_team_player", 0x147B90074ULL, 0x147B90074ULL, {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xF0, 0x8B, 0xEA}, {}},
        {"post_career_event", 0x147B9019CULL, 0x14060124CULL, {0x4C, 0x8B, 0xC0, 0x48, 0x8B, 0xCF, 0xE8, 0xA5, 0x10, 0xA7, 0xF8, 0x48, 0x8D, 0x8C, 0x24, 0x90, 0x00, 0x00, 0x00, 0xE8, 0xF0, 0x83, 0x70, 0xFA}, {}},
        };
        for (const auto& b : blobs) {
            const Signature* s = t->find(b.name);
            CHECK(s != nullptr && !s->pattern.empty(), std::string("entry ") + b.name);
            if (!s) continue;
            std::vector<uint8_t> code(0x1000, 0xCC);
            std::memcpy(code.data() + 0x100, b.bytes.data(), b.bytes.size());
            if (!b.at_offset.empty()) std::memcpy(code.data() + 0x100 + s->offset, b.at_offset.data(), b.at_offset.size());
            SigResult r = resolve_signature(*s, code.data(), code.size(), b.va - 0x100);
            CHECK(r.state == SigState::Found && r.match == b.va && r.address == b.target,
                  std::string(b.name) + " resolves: " + r.error + " (match " + hex_addr(r.match) + ", address " + hex_addr(r.address) + ")");
        }
        // InsertTeamPlayer and IsPlayerInTeam share their first 25 bytes: each matches only its own function
        {
            std::vector<uint8_t> code(0x400, 0xCC);
            const auto& itp = blobs[11].bytes;
            const uint8_t in_team[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xD8, 0x8B, 0xEA};
            std::memcpy(code.data() + 0x100, itp.data(), itp.size());
            std::memcpy(code.data() + 0x200, in_team, sizeof(in_team));
            SigResult a = resolve_signature(*t->find("dc_insert_team_player"), code.data(), code.size(), 0x147000000ULL);
            SigResult c = resolve_signature(*t->find("dc_is_player_in_team"), code.data(), code.size(), 0x147000000ULL);
            CHECK(a.hits == 1 && a.match == 0x147000100ULL && c.hits == 1 && c.match == 0x147000200ULL, "the two DataController functions are told apart");
        }
        // the research JSON carries the same patterns, offsets and resolve modes (realtime_signatures.json for the reused entries)
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
        SignatureTable created, realtime;
        const bool have_c = load("created_players_signatures.json", created), have_r = load("realtime_signatures.json", realtime);
        for (const auto& b : blobs) {
            if (std::string(b.name) == "post_career_event") continue;  // game_thread's entry (resolved through InsertTeamPlayer's call)
            const Signature* a = t->find(b.name);
            const Signature* f = have_c ? created.find(b.name) : nullptr;
            if (!f && have_r) f = realtime.find(b.name);
            CHECK(a && f && a->pattern == f->pattern && a->offset == f->offset && a->resolve == f->resolve, std::string(b.name) + ": built-in entry equals the JSON");
        }
        // every name the Windows host resolves exists in the table
        for (const char* n : {"db_query_init", "db_query_set_int", "db_query_set_string", "db_query_select_field", "db_query_where_int", "db_query_destroy",
                              "db_result_free", "db_provider_execute", "event_allocator_global", "event_base_vtable", "player_inserted_event_vtable",
                              "dc_insert_team_player", "post_career_event"})
            CHECK(t->find(n) && !t->find(n)->pattern.empty(), std::string("the host resolves ") + n);
    });
}
