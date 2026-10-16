// Native tests: the season update pack importer (core/season_pack.h, plan B3). Synthetic data only: a fixture pack of 5 transfers, 1 manager
// move and 1 bad row is written under the run's temp folder (TMP), read, previewed, turned into pages, "executed" by a fake that does what
// features/player_moves.lua and features/manager_move.lua do to a snapshot, undone from the record and compared with the original.
// Included by test_main.cpp; season_pack_test::run(). Nothing here touches the game, a save or the installed game folder.
#pragma once
#include <filesystem>
#include <fstream>

#include "core/season_pack.h"

namespace season_pack_test {

using namespace turbo;
using namespace turbo::season_pack;
// declarations (not just the directive) so the test files' own Snapshot / Page / Preview / ... never make a name ambiguous
using turbo::season_pack::Snapshot;
using turbo::season_pack::Pack;
using turbo::season_pack::Preview;
using turbo::season_pack::PreviewRow;
using turbo::season_pack::Page;
using turbo::season_pack::Kind;
using turbo::season_pack::UndoStore;
using turbo::season_pack::UndoSet;
using turbo::season_pack::UndoPlan;
using turbo::season_pack::preview;
using turbo::season_pack::make_pages;
using turbo::season_pack::make_undo;
using turbo::season_pack::read_pack;
using nlohmann::json;
namespace fs = std::filesystem;

static fs::path tmp_dir(const char* name) {
    std::error_code ec;
    fs::path p = fs::temp_directory_path(ec) / "season_pack_test" / name;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}
static void put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

// Five clubs of 20 players (ids 1..5; club 5 is the user's), a national team (90), 5 managers (101..105 at clubs 1..5), a free manager
// (199), one free-agent player.
static Snapshot world() {
    Snapshot s;
    s.user_team = 5;
    for (int t = 1; t <= 5; ++t) {
        s.teams[t] = {"Club" + std::to_string(t), false};
        for (int i = 1; i <= 20; ++i) {
            s.player_club[t * 1000 + i] = t;
            s.player_name[t * 1000 + i] = "P" + std::to_string(t * 1000 + i);
        }
        s.manager_team[100 + t] = t;
        s.manager_name[100 + t] = "Mgr" + std::to_string(100 + t);
    }
    s.teams[90] = {"Nationland", true};
    s.manager_team[199] = 0;
    s.manager_name[199] = "Mgr199";
    s.player_club[9001] = kFreeAgents;
    s.player_name[9001] = "Freebie";
    return s;
}

// What the Lua modules do to the live database (an independent re-statement, not the code under test)
static void execute(Snapshot& s, const std::vector<json>& sends) {
    for (const auto& snd : sends) {
        const json& o = snd["overrides"];
        if (snd["module"] == "player_moves") {
            for (const auto& a : o["actions"]) {
                const int64_t pid = a["playerid"];
                if (a["action"] == "transfer") s.player_club[pid] = a["to_teamid"];
                else if (a["action"] == "release") s.player_club[pid] = kFreeAgents;
            }
        } else if (snd["module"] == "manager_move") {
            const int64_t mid = o["managerid"], to = o["teamid"];
            const int64_t old = s.manager_team[mid];
            if (to > 0) {
                for (auto& kv : s.manager_team)
                    if (kv.second == to && kv.first != mid) kv.second = old;
                s.manager_team[mid] = to;
            } else {
                int64_t repl = o.contains("replacement") ? o["replacement"].get<int64_t>() : 0;
                s.manager_team[mid] = 0;
                if (repl > 0) s.manager_team[repl] = old;
            }
        }
    }
}
static std::vector<json> all_sends(const std::vector<Page>& pages) {
    std::vector<json> v;
    for (const auto& p : pages) v.insert(v.end(), p.sends.begin(), p.sends.end());
    return v;
}

static const char* kTransfers =
    "\xEF\xBB\xBFplayerid,player,from,to,fee\r\n"
    "1001,Pone,1,2,\"1,000,000\"\r\n"       // club id -> club id, fee with a comma inside quotes
    "1002,Ptwo,Club1,Club3,5000000\r\n"      // names
    "2001,Pthree,2,3,0\r\n"
    "3001,Pfour,3,4,12\r\n"
    "4001,Pfive,,1,0\r\n"                    // from left empty: his live club
    "999999,Nobody,1,2,0\r\n";               // the bad row: not in the career
static const char* kManagers = "managerid,manager,to\n103,Mgr103,1\n";

static void write_fixture(const fs::path& dir) {
    put(dir / "pack.json", "{\"name\":\"Fixture Window\",\"season\":\"2026/27 winter\",\"source\":\"our own fixture\"}");
    put(dir / "transfers.csv", kTransfers);
    put(dir / "managers.csv", kManagers);
    put(dir / "kits" / "alpha.zip", "x");
    put(dir / "kits" / "notes.txt", "x");
}

static const PreviewRow* row_for(const Preview& pv, Kind k, int64_t id) {
    for (const auto& r : pv.rows)
        if (r.kind == k && r.id == id) return &r;
    return nullptr;
}

static void run() {
    std::printf("native season pack importer (B3)\n");

    run_case("season pack: the fixture pack previews 6 rows and rejects 1 with its reason", [] {
        const fs::path dir = tmp_dir("fixture");
        write_fixture(dir);
        Pack pack;
        std::string err;
        CHECK(read_pack(dir, pack, &err), err);
        CHECK(pack.info.name == "Fixture Window" && pack.info.season == "2026/27 winter" && pack.info.source == "our own fixture", "pack.json read");
        CHECK(pack.transfers.size() == 6 && pack.managers.size() == 1 && pack.unreadable.empty(), "6 transfer rows + 1 manager row read");
        CHECK(pack.kits.size() == 1 && pack.kits[0] == "alpha.zip", "kits listed (zip only)");
        CHECK(pack.transfers[0].playerid == 1001 && pack.transfers[0].to_text == "2", "BOM and CRLF handled");
        const Preview pv = preview(pack, world());
        CHECK(pv.accepted == 6 && pv.rejected == 1 && pv.rows.size() == 7, "6 accepted, 1 rejected");
        const PreviewRow* bad = row_for(pv, Kind::Transfer, 999999);
        CHECK(bad && !bad->ok && bad->reason == "player 999999 is not in the career" && bad->line == 7, bad ? bad->reason : "bad row missing");
        const PreviewRow* t1 = row_for(pv, Kind::Transfer, 1001);
        CHECK(t1 && t1->ok && t1->from == 1 && t1->to == 2 && t1->text == "P1001: Club1 -> Club2", t1 ? t1->text : "missing");
        const PreviewRow* t2 = row_for(pv, Kind::Transfer, 1002);
        CHECK(t2 && t2->ok && t2->to == 3, "club names resolve");
        const PreviewRow* t5 = row_for(pv, Kind::Transfer, 4001);
        CHECK(t5 && t5->ok && t5->from == 4 && t5->to == 1, "empty from = his live club");
        const PreviewRow* m = row_for(pv, Kind::Manager, 103);
        CHECK(m && m->ok && m->displaced == 101 && m->text == "Mgr103 -> Club1; swaps with Mgr101 -> Club3", m ? m->text : "manager row missing");
        bool fee_note = false, undo_note = false, kit_note = false;
        for (const auto& n : pv.notes) {
            fee_note |= n.find("Fees") != std::string::npos;
            undo_note |= n.find("Undo restores club links") != std::string::npos;
            kit_note |= n.find("kit zip") != std::string::npos;
        }
        CHECK(fee_note && undo_note && kit_note, "honesty notes present");
        CHECK(pv.pages() == 1, "one page");
    });

    run_case("season pack: apply then undo round-trips the snapshot (record written to a file first, fee never sent)", [] {
        const fs::path dir = tmp_dir("roundtrip");
        write_fixture(dir);
        Pack pack;
        CHECK(read_pack(dir, pack, nullptr), "read");
        const Snapshot orig = world();
        Snapshot live = orig;
        const Preview pv = preview(pack, live);
        const auto pages = make_pages(pv);
        CHECK(pages.size() == 1 && pages[0].rows.size() == 6, "one page of 6 rows");
        CHECK(pages[0].sends.size() == 2 && pages[0].sends[0]["module"] == "player_moves" && pages[0].sends[1]["module"] == "manager_move", "player send, then manager send");
        const json& act = pages[0].sends[0]["overrides"]["actions"];
        CHECK(act.size() == 5, "5 transfers in the player send");
        for (const auto& a : act) CHECK(a["fee"] == 0 && a["wage"] == 0 && a["action"] == "transfer", "fee ignored: 0, wage 0");
        const json& mo = pages[0].sends[1]["overrides"];
        CHECK(mo["enabled"] == true && mo["confirm"] == true && mo["managerid"] == 103 && mo["teamid"] == 1, "manager_move overrides");

        // the record is written before the page is sent
        UndoStore store;
        store.add(undo_rows_of(pv, pages[0]));
        const fs::path rec = undo_store_path(tmp_dir("le_root"));
        std::string err;
        CHECK(save_undo_store(rec, store, &err), err);
        CHECK(rec.parent_path().filename() == "turbo_output" && rec.filename() == "season_pack_undo.json", "own file in turbo_output");
        UndoStore back;
        CHECK(load_undo_store(rec, back, &err), err);
        CHECK(back.sets.size() == 1 && back.sets[0].pack == "Fixture Window" && back.sets[0].rows.size() == 6, "record round-trips through the file");

        execute(live, all_sends(pages));
        CHECK(live.player_club[1001] == 2 && live.player_club[1002] == 3 && live.player_club[3001] == 4 && live.player_club[4001] == 1, "players moved");
        CHECK(live.manager_team[103] == 1 && live.manager_team[101] == 3, "manager swap");
        CHECK(live.player_club != orig.player_club && live.manager_team != orig.manager_team, "the model changed");

        const UndoSet* set = back.find("Fixture Window");
        CHECK(set != nullptr, "set found");
        const UndoPlan up = make_undo(*set, live);
        CHECK(up.skipped.empty() && up.done.size() == 6, "all 6 rows undone");
        execute(live, all_sends(up.pages));
        CHECK(live.player_club == orig.player_club, "player clubs restored");
        CHECK(live.manager_team == orig.manager_team, "manager teams restored");
        // a second undo finds nothing to do
        const UndoPlan again = make_undo(*set, live);
        CHECK(again.done.empty() && again.skipped.size() == 6 && again.pages.empty(), "undo twice does nothing");
        CHECK(back.remove("Fixture Window") && back.sets.empty() && !back.remove("Fixture Window"), "set removed");
    });

    run_case("season pack: 120 rows page as 50 / 50 / 20, undo pages the same way", [] {
        Snapshot s;
        s.user_team = 99;
        s.teams[99] = {"Mine", false};
        for (int t = 20; t <= 23; ++t) s.teams[t] = {"Source" + std::to_string(t), false};
        for (int t = 11; t <= 16; ++t) {
            s.teams[t] = {"T" + std::to_string(t), false};
            for (int i = 0; i < 20; ++i) s.player_club[5000 + t * 100 + i] = t;   // 20 players each: 20 more arrive, 52 is the cap
        }
        std::string csv = "playerid,to\n";
        int moved = 0;
        for (int i = 1; i <= 208; ++i) {
            s.player_club[i] = 20 + (i - 1) / 52;   // four full squads of 52, 30 leave each (120 rows)
            if ((i - 1) % 52 < 30) csv += std::to_string(i) + "," + std::to_string(11 + (++moved % 6)) + "\n";
        }
        const fs::path dir = tmp_dir("paging");
        put(dir / "transfers.csv", csv);
        Pack pack;
        CHECK(read_pack(dir, pack, nullptr) && pack.transfers.size() == 120, "120 rows read");
        CHECK(pack.info.name == "paging", "name defaults to the folder");
        const Preview pv = preview(pack, s);
        CHECK(pv.accepted == 120 && pv.rejected == 0 && pv.pages() == 3, "120 accepted, 3 pages");
        const auto pages = make_pages(pv);
        CHECK(pages.size() == 3 && pages[0].rows.size() == 50 && pages[1].rows.size() == 50 && pages[2].rows.size() == 20, "50 / 50 / 20");
        for (const auto& p : pages) CHECK(p.sends.size() == 1 && p.sends[0]["overrides"]["actions"].size() == p.rows.size(), "one player send per page");
        Snapshot live = s;
        UndoStore store;
        for (const auto& p : pages) {
            store.add(undo_rows_of(pv, p));
            execute(live, p.sends);
        }
        CHECK(store.sets.size() == 1 && store.sets[0].rows.size() == 120, "pages add to one set");
        const UndoPlan up = make_undo(store.sets[0], live);
        CHECK(up.pages.size() == 3 && up.pages[0].rows.size() == 50 && up.pages[2].rows.size() == 20, "undo pages 50 / 50 / 20");
        execute(live, all_sends(up.pages));
        CHECK(live.player_club == s.player_club, "restored");
        // a mixed page still counts player rows and manager rows against the same 50
        CHECK(kPageRows == 50, "the CMTracker page cap is kept");
    });

    run_case("season pack: rejections (user club, wrong from, duplicate, national, free agents, squad limits, bad rows, manager rules)", [] {
        Snapshot w = world();
        w.manager_team[106] = 4;   // two more managers at Club4 (no row below swaps with Club4)
        w.manager_team[107] = 4;
        const fs::path dir = tmp_dir("rejects");
        put(dir / "transfers.csv",
            "playerid,from,to\n"
            "5001,5,1\n"        // user's club leaves
            "1003,1,5\n"        // to the user's club
            "1004,2,3\n"        // wrong from
            "1005,1,2\n"        // fine
            "1005,1,3\n"        // duplicate
            "1006,1,90\n"       // national team
            "1007,1,777\n"      // unknown team
            "1008,1,Club1\n"    // already there
            "1009,1,0\n"        // team 0
            "abc,1,2\n"         // bad id
            "1010,1,111592\n"   // release is not a pack row
            "9001,,2\n"         // free agent signs: fine
            "1011,1,2\n");
        put(dir / "managers.csv",
            "managerid,to\n"
            "105,1\n"           // manages the user's club
            "101,5\n"           // to the user's club
            "106,777\n"         // unknown team
            "104,0\n"           // free agent not in a pack
            "103,90\n"          // national
            "107,4\n"           // already there
            "555,2\n"           // unknown manager
            "199,2\n"           // free manager joins club 2 (Mgr102 -> free agent)
            "102,3\n");         // Mgr102 is now at team 0 (after the swap above): a later row sees the earlier ones
        Pack pack;
        CHECK(read_pack(dir, pack, nullptr), "read");
        CHECK(pack.unreadable.size() == 1 && pack.unreadable[0].line == 11, "the bad id is unreadable at its line");
        const Preview pv = preview(pack, w);
        auto reason_of = [&](Kind k, int64_t id, size_t line) -> std::string {
            for (const auto& r : pv.rows)
                if (r.kind == k && r.id == id && r.line == line) return r.ok ? "OK" : r.reason;
            return "(missing)";
        };
        auto has = [](const std::string& s, const char* part) { return s.find(part) != std::string::npos; };
        CHECK(has(reason_of(Kind::Transfer, 5001, 2), "touches your club"), reason_of(Kind::Transfer, 5001, 2));
        CHECK(has(reason_of(Kind::Transfer, 1003, 3), "touches your club"), reason_of(Kind::Transfer, 1003, 3));
        CHECK(reason_of(Kind::Transfer, 1004, 4) == "he is at Club1 now, not at Club2", reason_of(Kind::Transfer, 1004, 4));
        CHECK(reason_of(Kind::Transfer, 1005, 5) == "OK", "first of the duplicates is fine");
        CHECK(reason_of(Kind::Transfer, 1005, 6) == "duplicate: this player already has a row at line 5", reason_of(Kind::Transfer, 1005, 6));
        CHECK(has(reason_of(Kind::Transfer, 1006, 7), "national team"), reason_of(Kind::Transfer, 1006, 7));
        CHECK(has(reason_of(Kind::Transfer, 1007, 8), "'777' is not in the career"), reason_of(Kind::Transfer, 1007, 8));
        CHECK(reason_of(Kind::Transfer, 1008, 9) == "he already plays for Club1", reason_of(Kind::Transfer, 1008, 9));
        CHECK(has(reason_of(Kind::Transfer, 1009, 10), "not in the career"), reason_of(Kind::Transfer, 1009, 10));
        CHECK(has(reason_of(Kind::Transfer, 1010, 12), "releasing him"), reason_of(Kind::Transfer, 1010, 12));
        CHECK(reason_of(Kind::Transfer, 9001, 13) == "OK", "a free agent can sign");
        // Club1 had 20: 1005 left (19), 1011 would leave 18 (still >= 18: fine)
        CHECK(reason_of(Kind::Transfer, 1011, 14) == "OK", reason_of(Kind::Transfer, 1011, 14));
        CHECK(has(reason_of(Kind::Manager, 105, 2), "touches your club"), reason_of(Kind::Manager, 105, 2));
        CHECK(has(reason_of(Kind::Manager, 101, 3), "touches your club"), reason_of(Kind::Manager, 101, 3));
        CHECK(has(reason_of(Kind::Manager, 106, 4), "'777' is not in the career"), reason_of(Kind::Manager, 106, 4));
        CHECK(has(reason_of(Kind::Manager, 104, 5), "free agent"), reason_of(Kind::Manager, 104, 5));
        CHECK(has(reason_of(Kind::Manager, 103, 6), "national team"), reason_of(Kind::Manager, 103, 6));
        CHECK(reason_of(Kind::Manager, 107, 7) == "Mgr107 already manages Club4"|| reason_of(Kind::Manager, 107, 7) == "manager 107 already manages Club4", reason_of(Kind::Manager, 107, 7));
        CHECK(reason_of(Kind::Manager, 555, 8) == "manager 555 is not in the career", reason_of(Kind::Manager, 555, 8));
        CHECK(reason_of(Kind::Manager, 199, 9) == "OK", reason_of(Kind::Manager, 199, 9));
        CHECK(reason_of(Kind::Manager, 102, 10) == "Mgr102 -> Club3; swaps with Mgr103 -> free agent" || reason_of(Kind::Manager, 102, 10) == "OK",
              reason_of(Kind::Manager, 102, 10));
        // the manager swap on the free manager's row names the displaced manager
        const PreviewRow* fm = row_for(pv, Kind::Manager, 199);
        CHECK(fm && fm->displaced == 102 && fm->from == 0 && fm->to == 2, "free manager displaces Mgr102");
        // undo of the free-manager join: reverse with a replacement
        Snapshot live = w;
        const auto pages = make_pages(pv);
        UndoStore store;
        for (const auto& p : pages) store.add(undo_rows_of(pv, p));
        for (const auto& p : pages) execute(live, p.sends);
        const UndoPlan up = make_undo(store.sets[0], live);
        execute(live, all_sends(up.pages));
        CHECK(live.player_club == w.player_club && live.manager_team == w.manager_team, "mixed page undoes cleanly (incl. free agent signing via release, free manager)");
    });

    run_case("season pack: squad limits, unknown user club, undo skips what moved since", [] {
        Snapshot w = world();
        // Club2 drops from 20 to 18 with two leavers (fine), the third would leave 17 (rejected)
        const fs::path dir = tmp_dir("limits");
        put(dir / "transfers.csv", "playerid,to\n2001,1\n2002,1\n2003,1\n");
        Pack pack;
        CHECK(read_pack(dir, pack, nullptr), "read");
        Preview pv = preview(pack, w);
        CHECK(pv.accepted == 2 && pv.rejected == 1, "third leaver rejected");
        CHECK(pv.rows[2].reason == "Club2 would have 17 players, fewer than the 18 a match squad needs" ||
                  pv.rows[2].reason.find("fewer than the 18") != std::string::npos, pv.rows[2].reason);
        // Club1 full at 52: arrival rejected
        for (int i = 21; i <= 52; ++i) w.player_club[1000 + i] = 1;
        put(dir / "transfers.csv", "playerid,to\n2001,1\n");
        CHECK(read_pack(dir, pack, nullptr), "read 2");
        pv = preview(pack, w);
        CHECK(pv.accepted == 0 && pv.rows[0].reason == "Club1 has 52 players, the most a squad holds", pv.rows[0].reason);
        // user club unknown: everything is rejected
        Snapshot nouser = world();
        nouser.user_team = 0;
        put(dir / "transfers.csv", "playerid,to\n1001,2\n");
        CHECK(read_pack(dir, pack, nullptr), "read 3");
        pv = preview(pack, nouser);
        CHECK(pv.accepted == 0 && pv.rows[0].reason.find("your club is not known") != std::string::npos, "no user club: refused");

        // undo skips a player who moved since, a manager who moved since, and a swap partner who moved
        Snapshot base = world();
        put(dir / "transfers.csv", "playerid,to\n1001,2\n1002,3\n");
        put(dir / "managers.csv", "managerid,to\n103,1\n");
        CHECK(read_pack(dir, pack, nullptr), "read 4");
        pv = preview(pack, base);
        CHECK(pv.accepted == 3, "3 accepted");
        const auto pages = make_pages(pv);
        UndoStore store;
        store.add(undo_rows_of(pv, pages[0]));
        Snapshot live = base;
        execute(live, all_sends(pages));
        live.player_club[1001] = 4;      // moved on since
        live.manager_team[101] = 2;      // the swap partner moved
        UndoPlan up = make_undo(store.sets[0], live);
        CHECK(up.done.size() == 1 && up.done[0] == "P1002: Club3 -> Club1", up.done.empty() ? "nothing done" : up.done[0]);
        CHECK(up.skipped.size() == 2, "two skipped");
        bool moved = false, partner = false;
        for (const auto& s : up.skipped) {
            moved |= s == "P1001: moved since (now at Club4)";
            partner |= s == "Mgr103: his swap partner moved since";
        }
        CHECK(moved && partner, "skip reasons name what moved");
        execute(live, all_sends(up.pages));
        CHECK(live.player_club[1002] == 1 && live.player_club[1001] == 4, "only the unmoved row came back");
    });

    run_case("season pack: reading problems (not a pack, missing columns, empty files, row bound), undo file robustness", [] {
        Pack pack;
        std::string err;
        const fs::path empty = tmp_dir("emptydir");
        CHECK(!read_pack(empty, pack, &err) && err.find("no transfers.csv or managers.csv") != std::string::npos, err);
        CHECK(!read_pack(empty / "nope", pack, &err) && err.find("not a folder") != std::string::npos, err);
        const fs::path d = tmp_dir("badcols");
        put(d / "transfers.csv", "who,where\n1,2\n");
        put(d / "managers.csv", "");
        put(d / "pack.json", "[1,2]");
        CHECK(read_pack(d, pack, &err), err);
        CHECK(pack.transfers.empty() && pack.problems.size() == 3, "three problems: columns, empty manager file, pack.json");
        Preview pv = preview(pack, world());
        CHECK(pv.accepted == 0 && pv.pages() == 0 && make_pages(pv).empty(), "nothing to apply");
        // header aliases, a missing "to" value and one oversized pack
        const fs::path a = tmp_dir("alias");
        put(a / "transfers.csv", "PLAYER_ID,From_Team,To_Team\n1001,1,2\n1002,1,\n");
        CHECK(read_pack(a, pack, &err) && pack.transfers.size() == 1 && pack.unreadable.size() == 1 && pack.unreadable[0].reason == "no target team", "aliases and missing target");
        const fs::path big = tmp_dir("big");
        std::string csv = "playerid,to\n";
        for (size_t i = 1; i <= kMaxPackRows + 5; ++i) csv += std::to_string(i) + ",2\n";
        put(big / "transfers.csv", csv);
        CHECK(read_pack(big, pack, &err) && pack.transfers.size() == kMaxPackRows && !pack.problems.empty(), "row bound");
        // undo file: missing = empty, garbage = error and an empty store, unknown shapes dropped
        UndoStore st;
        const fs::path rec = tmp_dir("undofile") / "season_pack_undo.json";
        CHECK(load_undo_store(rec, st, &err) && st.sets.empty(), "missing file is an empty store");
        put(rec, "not json");
        CHECK(!load_undo_store(rec, st, &err) && st.sets.empty(), "garbage refused");
        put(rec, "{\"turbo_season_pack_undo\":1,\"sets\":[{\"pack\":\"p\",\"rows\":[{\"id\":1,\"from\":2,\"to\":3},{\"id\":\"x\"}]},{\"nopack\":1}]}");
        CHECK(load_undo_store(rec, st, &err) && st.sets.size() == 1 && st.sets[0].rows.size() == 1, "bad entries dropped");
        // the reapply store is never touched by this module
        CHECK(undo_store_path("x").filename() != "reapply_edits.json", "separate file");
    });
}

}  // namespace season_pack_test
