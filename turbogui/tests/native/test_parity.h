// Native tests (Turbo 2.1 parity): transfer columns (core/transfer_stats.h), your career's scouts (core/scouts.h) and their
// panels (ui/ui_parity.cpp). Included by test_main.cpp after its framework (CHECK, run_case, Ui, g_out). The export CSV here is
// SYNTHETIC (the header of features/export_transfer_history.lua, made-up rows); the world's career_scouts / transfers tables
// are built by gui_world.lua with the real schema's field names.
#pragma once
#include "core/scouts.h"
#include "core/transfer_stats.h"
#include "ui/ui_parity.h"

#include <chrono>
#include <cstring>

namespace parity_test {

using namespace turbo;

static const char* kExportCsv =
    "type,date,playerid,playername,teamfromid,teamfromname,teamtoid,teamtoname,fee,exchangeplayerid,exchangeplayername,total_deal_value\r\n"
    "transfer,20260702,1003,Ben White,7,Everton,1,Arsenal,25000000,,,25000000\r\n"
    "transfer,20260705,1002,\"Saka, B\",111592,Free Agents,1,Arsenal,0,,,0\r\n"
    "loan,20260710,1001,Odegaard,1,Arsenal,241,\"Inter, Milano\",150000,,,150000\r\n"
    "transfer,20260801,2001,X,1,Arsenal,241,Inter,3000000,77,Y,4500000\r\n"
    "transfer,bad,abc,Nobody,1,Arsenal,7,Everton,1,,,1\r\n"
    "\r\n";

static void run_core() {
    run_case("parity: the transfer history export parses into moves (loans, free agents, exchange value, bad rows)", [] {
        int skipped = 0;
        const auto m = xfer::parse_export_csv(kExportCsv, &skipped);
        CHECK(m.size() == 4 && skipped == 1, fmt("%zu moves, %d skipped", m.size(), skipped));
        if (m.size() < 4) return;
        CHECK(m[0].playerid == 1003 && m[0].from == 7 && m[0].to == 1 && m[0].fee == 25000000 && m[0].date == 20260702, "first row");
        CHECK(m[1].player == "Saka, B" && m[1].from == xfer::kFreeAgents, "quoted name, free agents");
        CHECK(xfer::is_loan(m[2]) && !xfer::is_loan(m[0]), "loan rows");
        CHECK(m[3].fee == 4500000, "total deal value (fee + exchange) wins over the fee");
        int sk2 = 0;
        CHECK(xfer::parse_export_csv("a,b,c\r\n1,2,3\r\n", &sk2).empty() && sk2 == -1, "unknown columns: no rows, flagged");
        CHECK(xfer::parse_export_csv("", &sk2).empty() && sk2 == 0, "empty text");
    });
    run_case("parity: per-club transfer columns", [] {
        const auto st = xfer::per_team(xfer::parse_export_csv(kExportCsv));
        CHECK(st.count(xfer::kFreeAgents) == 0, "free agents are never a row");
        const auto a = st.at(1), e = st.at(7), i = st.at(241);
        CHECK(a.in == 2 && a.out == 1 && a.loans_out == 1 && a.loans_in == 0, fmt("Arsenal in %d out %d loans out %d", a.in, a.out, a.loans_out));
        CHECK(a.spent == 25000000 && a.received == 4500000 && a.net() == -20500000, "Arsenal money (a loan fee is not a sale)");
        CHECK(e.out == 1 && e.received == 25000000 && e.in == 0, "Everton");
        CHECK(i.in == 1 && i.loans_in == 1 && i.spent == 4500000, "Inter");
        const auto rows = xfer::of_team(xfer::parse_export_csv(kExportCsv), 1);
        CHECK(rows.size() == 4 && rows.front().date == 20260801 && rows.back().date == 20260702, "a club's moves, newest first");
    });
    run_case("parity: export and database moves merge without doubles (one move per player, clubs and season)", [] {
        auto exp = xfer::parse_export_csv(kExportCsv);
        xfer::Move same = exp[0];
        same.source = "database";
        same.date = 20260704;  // two days later: the same move
        xfer::Move completion = exp[0];
        completion.source = "database";
        completion.date = 20270110;  // agreement in July, completion in January (a pre-contract): still the same move
        xfer::Move next_season = exp[0];
        next_season.date = 20270801;  // a season later: a second move of the same player
        xfer::Move undated = exp[1];
        undated.date = 0;
        xfer::Move back = exp[0];
        std::swap(back.from, back.to);  // the other way: its own move
        back.date = 20260901;
        const auto m = xfer::merge(exp, {same, completion, next_season, undated, back});
        CHECK(m.size() == exp.size() + 2, fmt("%zu moves", m.size()));
        CHECK(m.size() >= 2 && m[m.size() - 2].date == 20270801 && m.back().date == 20260901, "the next season's move and the move back are kept");
        CHECK(m[0].source == "export", "the export's row stays");
    });
    run_case("parity: this season's moves (from 1 July)", [] {
        CHECK(xfer::season_start(GameDate{2027, 1, 15}) == 20260701 && xfer::season_start(GameDate{2026, 7, 1}) == 20260701 &&
                  xfer::season_start(GameDate{2026, 6, 30}) == 20250701 && xfer::season_start(GameDate{}) == 0,
              "season starts");
        std::vector<xfer::Move> mv(4);
        mv[0].date = 20260630;
        mv[1].date = 20260701;
        mv[2].date = 0;
        mv[3].date = 20270110;
        const auto s = xfer::this_season(mv, GameDate{2027, 1, 15});
        CHECK(s.size() == 3 && s[0].date == 20260701 && s[1].date == 0 && s[2].date == 20270110, fmt("%zu moves", s.size()));
        CHECK(xfer::this_season(mv, GameDate{}).size() == 4, "no date: no filter");
    });
    run_case("parity: money and date text", [] {
        CHECK(xfer::money_short(0) == "0" && xfer::money_short(850000) == "850K" && xfer::money_short(4150000) == "4.2M" &&
                  xfer::money_short(-1500000) == "-1.5M" && xfer::money_short(2300000000LL) == "2.3B" && xfer::money_short(999) == "999",
              xfer::money_short(4150000));
        CHECK(xfer::date_text(20260702) == "2026-07-02" && xfer::date_text(0) == "?", "dates");
    });
    run_case("parity: the transfer export of this save: today's file, never a later-dated one, else the newest written", [] {
        const fs::path d = g_out / "parity_exports";
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d);
        CHECK(xfer::newest_export(d).empty(), "none yet");
        CHECK(xfer::newest_export(d / "missing").empty(), "missing folder");
        const char* torino = "turbo_transfer_history_2027_01_02.csv";     // career A, later in-game date, written first
        const char* leicester = "turbo_transfer_history_2026_08_30.csv";  // career B, earlier in-game date, written last
        for (const char* n : {torino, leicester, "turbo_fixtures_2028_01_01.csv", "turbo_transfer_history_2029_01_01.txt"})
            std::ofstream((d / n).string()) << "x";
        const auto now = fs::file_time_type::clock::now();
        fs::last_write_time(d / torino, now - std::chrono::hours(2), ec);
        fs::last_write_time(d / leicester, now - std::chrono::hours(1), ec);
        auto pick = [&](const GameDate& today) { return xfer::newest_export(d, today).filename().string(); };
        CHECK(pick(GameDate{}) == leicester, "no game date: the most recently written, not the larger name: " + pick(GameDate{}));
        CHECK(pick(GameDate{2026, 8, 30}) == leicester, "B's save on its export day: " + pick(GameDate{2026, 8, 30}));
        CHECK(pick(GameDate{2026, 9, 10}) == leicester, "B later on: A's 2027 file is never picked: " + pick(GameDate{2026, 9, 10}));
        CHECK(pick(GameDate{2027, 1, 2}) == torino, "A's save on its export day (an older write): " + pick(GameDate{2027, 1, 2}));
        CHECK(pick(GameDate{2026, 8, 1}).empty(), "an older save than every export: none");
        CHECK(xfer::export_date(d / torino) == 20270102 && xfer::export_date(d / "turbo_transfer_history_x.csv") == 0, "date in the name");
        fs::remove_all(d, ec);
    });
}

// Scroll the window that holds `label` (drawn in the last frame, maybe clipped) so the item is at its top
static void scroll_into_view(Ui& ui, const std::string& label) {
    for (const auto& kv : g_items) {
        const ItemRec& r = kv.second;
        if (r.frame != g_frame || (r.label != label && !(r.label.empty() && ImHashStr(label.c_str(), 0, r.seed) == r.id))) continue;
        for (ImGuiWindow* w : GImGui->Windows)
            if (r.window == w->Name) ImGui::SetScrollY(w, w->Scroll.y + (r.rect.Min.y - w->InnerClipRect.Min.y) - 2.0f);
        break;
    }
    ui.frames(2);
}

// Inside test_ui (the world's App): the database transfers table, the Teams list's columns, the scouts editor and its backup.
static void run_ui(App& app, Ui& ui, const fs::path& le) {
    const fs::path out = le / "turbo_output";
    std::error_code ec;
    fs::remove(scouts::backup_path(out), ec);
    fs::remove(scouts::kill_switch_path(out), ec);
    for (const char* n : {"turbo_transfer_history_2026_08_30.csv"}) fs::remove(out / n, ec);

    run_case("parity: the database transfers table reads (Lilian dates, real field names)", [&] {
        const auto m = xfer::read_db_transfers(app.db);
        CHECK(m.size() == 2, fmt("%zu rows", m.size()));
        if (m.size() < 2) return;
        CHECK(m[0].playerid == 1003 && m[0].from == 7 && m[0].to == 1 && m[0].fee == 25000000 && m[0].date == 20260702 &&
                  m[0].source == "database",
              fmt("row 1: %lld %lld->%lld fee %lld date %d", (long long)m[0].playerid, (long long)m[0].from, (long long)m[0].to,
                  (long long)m[0].fee, m[0].date));
        CHECK(m[1].date == 20260830 && m[1].to == 241, "row 2");
    });

    run_case("parity UI: Teams list transfer columns (database first, then the export after Reload)", [&] {
        app.request_tab = 1;
        ui.frames(3);
        CHECK(ui.click("Transfers##xfon"), "Transfers check box");
        ui.frames(2);
        CHECK(transfer_columns_on(), "columns on");
        const xfer::TeamStats* a = transfer_stats_of(app, 1);
        CHECK(a && a->in == 1 && a->out == 1 && a->spent == 25000000 && a->received == 4000000, "Arsenal from the database rows");
        CHECK(std::string(transfer_source_line()).find("no export yet") != std::string::npos, transfer_source_line());
        std::ofstream((out / "turbo_transfer_history_2026_08_30.csv").string(), std::ios::binary) << kExportCsv;
        CHECK(ui.click("Reload##xfre"), "Reload");
        ui.frames(2);
        a = transfer_stats_of(app, 1);
        // export: 1003 in (same as the database row), 1002 in, 1001 loan out, 2001 out to Inter (4.5M; the database's 2026-08-30
        // row for 2001 is the same sale four weeks later, the completion date: counted once, the export's row kept)
        CHECK(a && a->in == 2 && a->loans_out == 1 && a->out == 1 && a->spent == 25000000 && a->received == 4500000,
              a ? fmt("in %d out %d loans out %d spent %lld received %lld", a->in, a->out, a->loans_out, (long long)a->spent,
                      (long long)a->received)
                : std::string("no stats"));
        CHECK(std::string(transfer_source_line()).find("turbo_transfer_history_2026_08_30.csv") != std::string::npos, transfer_source_line());
        CHECK(std::string(transfer_source_line()).find("in 2026/27") != std::string::npos &&
                  std::string(transfer_source_line()).find("not today") != std::string::npos,
              transfer_source_line());
        // an export dated after the save's game date (another career, or a later point of this one) is never shown
        std::ofstream((out / "turbo_transfer_history_2027_06_01.csv").string(), std::ios::binary) << kExportCsv;
        CHECK(ui.click("Reload##xfre"), "Reload again");
        ui.frames(2);
        CHECK(std::string(transfer_source_line()).find("2026_08_30") != std::string::npos, transfer_source_line());
        fs::remove(out / "turbo_transfer_history_2027_06_01.csv", ec);
        app.sel_team = 1;
        ui.frames(2);
        // the test window is narrow: the club's tab bar scrolls; widen the window for the click, then put it back
        ImGuiWindow* win = ImGui::FindWindowByName("FC 27 LE Turbo");
        const ImVec2 size = win ? win->Size : ImVec2(0, 0);
        if (win) ImGui::SetWindowSize("FC 27 LE Turbo", ImVec2(1700.0f, size.y));
        ui.frames(3);
        CHECK(ui.click("Transfers", "##tedit"), "the club's Transfers tab");
        ui.frames(2);
        CHECK(ui.find("Reload##xftre") != nullptr, "Transfers tab drawn");
        CHECK(ui.click("Overview", "##tedit"), "back to Overview");
        if (win) ImGui::SetWindowSize("FC 27 LE Turbo", size);
        ui.frames(2);
        CHECK(ui.click("Transfers##xfon"), "columns off again");
        ui.frames(2);
        CHECK(!transfer_columns_on(), "columns off");
        fs::remove(out / "turbo_transfer_history_2026_08_30.csv", ec);
    });

    const int64_t team = app.bridge.state().user_team;
    auto write_file = [](const fs::path& p, const std::string& text) { std::ofstream(p.string(), std::ios::binary) << text; };
    auto vals = [&]() {
        const auto s = scouts::read(app.db);
        std::string v;
        for (const auto& x : s) v += std::to_string(x.experience) + "/" + std::to_string(x.knowledge) + " ";
        return v;
    };

    run_case("parity: scouts read; Set every scout writes the game's range (0..4, never the 3-bit 5..7) after a backup", [&] {
        auto s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].name() == "Ana Lima" && s[0].experience == 2 && s[0].knowledge == 3 && s[1].experience == 1,
              fmt("%zu scouts", s.size()));
        const Table* t = app.db.table("career_scouts");
        const Field* fe = t ? t->field("experience") : nullptr;
        CHECK(fe && fe->max() == 7 && scouts::level_max(*fe) == 4 && scouts::clamp_level(*fe, 5) == 4 && scouts::clamp_level(*fe, -2) == 0,
              "a 3-bit field holds 7; Turbo stops at 4");
        std::string msg;
        CHECK(scouts::make_best(app.db, 5, out, team, &msg) == 2 && msg.find("level 4 of 4") != std::string::npos, msg);
        CHECK(fs::exists(scouts::backup_path(out)) && scouts::backed_up(out, team) == 2, "backup written for this club");
        CHECK(vals() == "4/4 4/4 ", "a level of 5 writes 4: " + vals());
        CHECK(scouts::make_best(app.db, 3, out, team, &msg) == 2 && vals() == "3/3 3/3 ", msg);  // the backup keeps the originals
        CHECK(scouts::make_best(app.db, 99, out, team, &msg) == 2 && vals() == "4/4 4/4 ", "level 99 gives 4: " + vals());
        CHECK(scouts::make_best(app.db, -5, out, team, &msg) == 2 && vals() == "0/0 0/0 ", "a negative level gives 0: " + vals());
        CHECK(scouts::set_level(app.db, s[1].rec, "knowledge", 7, out, team, &msg) && vals() == "0/0 0/4 ", "one cell, clamped: " + vals());
        CHECK(!scouts::set_level(app.db, s[1].rec, "nationality", 1, out, team, &msg), "only the level fields");
        write_file(scouts::kill_switch_path(out), "");
        CHECK(scouts::make_best(app.db, 4, out, team, &msg) == 0 && msg.find("scouts_off.txt") != std::string::npos, msg);
        CHECK(!scouts::set_level(app.db, s[0].rec, "experience", 4, out, team, &msg), "a cell refused while turned off");
        CHECK(scouts::restore(app.db, out, team, &msg) == 0, "restore refused while turned off");
        fs::remove(scouts::kill_switch_path(out), ec);
        CHECK(scouts::backed_up(out, team) == 2, "still two entries (no duplicates)");
        CHECK(scouts::restore(app.db, out, team, &msg) == 2, msg);
        CHECK(vals() == "2/3 1/1 ", "originals back: " + vals());
        CHECK(!fs::exists(scouts::backup_path(out)), "backup removed after a full restore");
        CHECK(scouts::restore(app.db, out, team, &msg) == 0 && msg.find("no backup") != std::string::npos, msg);
    });

    run_case("parity: scouts backup per career: another career's entries never stand in for this one's originals", [&] {
        std::string msg;
        // career A (another club) left a backup whose scouts have the same ids and names as this career's
        write_file(scouts::backup_path(out),
                   R"({"format":"turbo-scouts-backup","version":2,"scouts":[)"
                   R"({"team":99,"scoutid":0,"firstname":"Ana","lastname":"Lima","experience":0,"knowledge":0},)"
                   R"({"team":99,"scoutid":1,"firstname":"Ben","lastname":"Ray","experience":0,"knowledge":0}]})");
        CHECK(scouts::restore(app.db, out, team, &msg) == 0 && vals() == "2/3 1/1 " && msg.find("2 kept for other careers") != std::string::npos,
              "nothing of A's is written into B: " + msg);
        CHECK(scouts::make_best(app.db, 4, out, team, &msg) == 2 && scouts::backed_up(out, team) == 2 && scouts::backed_up(out, 99) == 2,
              "B's originals added next to A's: " + msg);
        CHECK(scouts::restore(app.db, out, team, &msg) == 2 && vals() == "2/3 1/1 ", "B's own originals back: " + vals() + " " + msg);
        CHECK(fs::exists(scouts::backup_path(out)) && scouts::backed_up(out, team) == 0 && scouts::backed_up(out, 99) == 2,
              "A's entries kept, B's removed");
        // a scout hired after the first backup: his originals are added at the next write
        write_file(scouts::backup_path(out), R"({"format":"turbo-scouts-backup","version":2,"scouts":[)"
                                             R"({"team":)" + std::to_string(team) +
                                                 R"(,"scoutid":0,"firstname":"Ana","lastname":"Lima","experience":2,"knowledge":3}]})");
        CHECK(scouts::make_best(app.db, 4, out, team, &msg) == 2 && scouts::backed_up(out, team) == 2, "the new scout gets an entry: " + msg);
        CHECK(scouts::restore(app.db, out, team, &msg) == 2 && vals() == "2/3 1/1 ", "both restored: " + vals());
        fs::remove(scouts::backup_path(out), ec);
    });

    run_case("parity: a damaged scouts backup: nothing written, no throw, values clamped on restore", [&] {
        std::string msg;
        write_file(scouts::backup_path(out), "{\"format\":\"other\"}");
        CHECK(scouts::restore(app.db, out, team, &msg) == 0 && msg.find("not a Turbo scouts backup") != std::string::npos, msg);
        write_file(scouts::backup_path(out), "{\"format\":5,\"scouts\":[]}");
        CHECK(scouts::restore(app.db, out, team, &msg) == 0 && msg.find("not a Turbo scouts backup") != std::string::npos, msg);
        const std::string t = std::to_string(team);
        for (const std::string& bad : {
                 std::string(R"({"team":)") + t + R"(,"scoutid":"0","firstname":"Ana","lastname":"Lima","experience":1,"knowledge":1})",
                 std::string(R"({"team":)") + t + R"(,"scoutid":0,"firstname":5,"lastname":"Lima","experience":1,"knowledge":1})",
                 std::string(R"({"team":)") + t + R"(,"scoutid":0,"firstname":"Ana","lastname":"Lima","experience":"x","knowledge":1})"}) {
            // a good entry first: a damaged one later must stop the restore before the first write
            write_file(scouts::backup_path(out), std::string(R"({"format":"turbo-scouts-backup","version":2,"scouts":[)") +
                                                     R"({"team":)" + t + R"(,"scoutid":1,"firstname":"Ben","lastname":"Ray","experience":4,"knowledge":4},)" +
                                                     bad + "]}");
            CHECK(scouts::restore(app.db, out, team, &msg) == 0 && msg.find("damaged entry (number 2)") != std::string::npos && vals() == "2/3 1/1 ",
                  "restore: " + msg + " " + vals());
            CHECK(scouts::make_best(app.db, 4, out, team, &msg) == 0 && vals() == "2/3 1/1 ", "make_best refuses: " + msg);
        }
        // out-of-range values (a hand edit, or a 7 an older build wrote) come back clamped to 0..4
        write_file(scouts::backup_path(out), std::string(R"({"format":"turbo-scouts-backup","version":2,"scouts":[)") + R"({"team":)" + t +
                                                 R"(,"scoutid":0,"firstname":"Ana","lastname":"Lima","experience":99,"knowledge":-3}]})");
        CHECK(scouts::restore(app.db, out, team, &msg) == 1 && vals() == "4/0 1/1 ", "clamped: " + vals());
        fs::remove(scouts::backup_path(out), ec);
        const auto s = scouts::read(app.db);
        const Table* tb = app.db.table("career_scouts");
        app.db.set_int(*tb, s[0].rec, "experience", 2);
        app.db.set_int(*tb, s[0].rec, "knowledge", 3);
        app.db.set_int(*tb, s[1].rec, "experience", 1);
        app.db.set_int(*tb, s[1].rec, "knowledge", 1);
        CHECK(vals() == "2/3 1/1 ", "world values again: " + vals());
    });

    run_case("parity UI: Managers > Your scouts: a cell pick (0..4), Set every scout and Restore originals", [&] {
        app.request_tab = 2;
        ui.frames(3);
        CHECK(ui.click("Your scouts (career staff)"), "open the scouts section");
        ui.frames(3);
        CHECK(ui.click("##experience"), "the first scout's experience");
        ui.frames(2);
        CHECK(ui.find("5##experience5") == nullptr, "no 5 in the list (0..4 only)");
        CHECK(ui.click("4##experience4"), "pick 4");
        ui.frames(3);
        auto s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].experience == 4 && s[0].knowledge == 3, "one cell through the panel: " + vals());
        CHECK(scouts::backed_up(out, team) == 2, "the originals were saved before the cell write");
        CHECK(ui.click("Set every scout##scoutbest"), "Set every scout");
        ui.frames(3);
        s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].experience == 4 && s[1].knowledge == 4, "5 stars = level 4 through the panel: " + vals());
        CHECK(ui.toast_contains("Scouts: 2 of 2 set to level 4 of 4"), "toast");
        ui.frames(130);  // the panel looks for the backup every 2 s
        CHECK(ui.click("Restore originals##scoutrestore"), "Restore originals");
        ui.frames(3);
        s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].experience == 2 && s[0].knowledge == 3, "restored through the panel");
        CHECK(ui.click("Your scouts (career staff)"), "close the section");
        ui.frames(2);
    });

    run_case("parity UI: Bulk edit's Pick fills a field name", [&] {
        app.request_tab = 5;
        ui.frames(3);
        scroll_into_view(ui, "Bulk edit players");
        CHECK(ui.click("Bulk edit players"), "open Bulk edit");
        ui.frames(2);
        scroll_into_view(ui, "##fn0");
        CHECK(ui.find("##fp0") != nullptr, "Pick combo next to the first field box");
        char* box = bulk_field_buffer("##fp0");
        CHECK(box != nullptr, "the combo knows its field box");
        const std::string before = box ? box : "";
        CHECK(ui.click("##fp0"), "open Pick");
        ui.frames(2);
        CHECK(ui.click("Release clause  (releaseclause)"), "pick Release clause");
        ui.frames(2);
        CHECK(box && std::string(box) == "releaseclause", std::string("the first field box reads: ") + (box ? box : "(none)"));
        if (box) std::snprintf(box, 40, "%s", before.c_str());  // leave the box as the earlier cases left it
        scroll_into_view(ui, "Bulk edit players");
        CHECK(ui.click("Bulk edit players"), "close Bulk edit");
        for (ImGuiWindow* w : GImGui->Windows)
            if (std::string(w->Name).find("##tools") != std::string::npos) ImGui::SetScrollY(w, 0.0f);
        ui.frames(2);
    });
}

}  // namespace parity_test
