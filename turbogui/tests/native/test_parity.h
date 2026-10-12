// Native tests (Turbo 2.1 parity): transfer columns (core/transfer_stats.h), your career's scouts (core/scouts.h) and their
// panels (ui/ui_parity.cpp). Included by test_main.cpp after its framework (CHECK, run_case, Ui, g_out). The export CSV here is
// SYNTHETIC (the header of features/export_transfer_history.lua, made-up rows); the world's career_scouts / transfers tables
// are built by gui_world.lua with the real schema's field names.
#pragma once
#include "core/scouts.h"
#include "core/transfer_stats.h"
#include "ui/ui_parity.h"

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
    run_case("parity: export and database moves merge without doubles", [] {
        auto exp = xfer::parse_export_csv(kExportCsv);
        xfer::Move same = exp[0];
        same.source = "database";
        same.date = 20260704;  // two days later: the same move
        xfer::Move other = exp[0];
        other.date = 20270110;  // months later: a second move of the same player
        xfer::Move undated = exp[1];
        undated.date = 0;
        const auto m = xfer::merge(exp, {same, other, undated});
        CHECK(m.size() == exp.size() + 1, fmt("%zu moves", m.size()));
        CHECK(m.back().date == 20270110, "the later move is kept");
    });
    run_case("parity: money and date text", [] {
        CHECK(xfer::money_short(0) == "0" && xfer::money_short(850000) == "850K" && xfer::money_short(4150000) == "4.2M" &&
                  xfer::money_short(-1500000) == "-1.5M" && xfer::money_short(2300000000LL) == "2.3B" && xfer::money_short(999) == "999",
              xfer::money_short(4150000));
        CHECK(xfer::date_text(20260702) == "2026-07-02" && xfer::date_text(0) == "?", "dates");
    });
    run_case("parity: the newest transfer export is found by name", [] {
        const fs::path d = g_out / "parity_exports";
        std::error_code ec;
        fs::remove_all(d, ec);
        fs::create_directories(d);
        CHECK(xfer::newest_export(d).empty(), "none yet");
        CHECK(xfer::newest_export(d / "missing").empty(), "missing folder");
        for (const char* n : {"turbo_transfer_history_2026_08_30.csv", "turbo_transfer_history_2027_01_02.csv", "turbo_fixtures_2028_01_01.csv",
                              "turbo_transfer_history_2029_01_01.txt"})
            std::ofstream((d / n).string()) << "x";
        CHECK(xfer::newest_export(d).filename() == "turbo_transfer_history_2027_01_02.csv", xfer::newest_export(d).string());
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
        // row for 2001 is a different date by four weeks: kept as its own move)
        CHECK(a && a->in == 2 && a->loans_out == 1 && a->out == 2 && a->spent == 25000000 && a->received == 8500000,
              a ? fmt("in %d out %d loans out %d spent %lld received %lld", a->in, a->out, a->loans_out, (long long)a->spent,
                      (long long)a->received)
                : std::string("no stats"));
        CHECK(std::string(transfer_source_line()).find("turbo_transfer_history_2026_08_30.csv") != std::string::npos, transfer_source_line());
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

    run_case("parity: scouts read, Best scouts writes with a backup of the originals, Restore puts them back", [&] {
        auto s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].name() == "Ana Lima" && s[0].experience == 2 && s[0].knowledge == 3 && s[1].experience == 1,
              fmt("%zu scouts", s.size()));
        std::string msg;
        CHECK(scouts::make_best(app.db, 5, out, &msg) == 2, msg);
        CHECK(fs::exists(scouts::backup_path(out)), "backup written");
        s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].experience == 5 && s[0].knowledge == 5 && s[1].experience == 5 && s[1].knowledge == 5, "all at 5");
        CHECK(scouts::make_best(app.db, 3, out, &msg) == 2, msg);  // the backup keeps the originals, not the 5s
        CHECK(scouts::make_best(app.db, 99, out, &msg) == 2 && scouts::read(app.db)[0].experience == 7, "clamped to the field's range");
        { std::ofstream((scouts::kill_switch_path(out)).string()) << ""; }
        CHECK(scouts::make_best(app.db, 5, out, &msg) == 0 && msg.find("scouts_off.txt") != std::string::npos, msg);
        CHECK(scouts::restore(app.db, out, &msg) == 0, "restore refused while turned off");
        fs::remove(scouts::kill_switch_path(out), ec);
        CHECK(scouts::restore(app.db, out, &msg) == 2, msg);
        s = scouts::read(app.db);
        CHECK(s[0].experience == 2 && s[0].knowledge == 3 && s[1].experience == 1 && s[1].knowledge == 1, "originals back");
        CHECK(!fs::exists(scouts::backup_path(out)), "backup removed after a full restore");
        CHECK(scouts::restore(app.db, out, &msg) == 0, "nothing to restore");
        { std::ofstream((scouts::backup_path(out)).string()) << "{\"format\":\"other\"}"; }
        CHECK(scouts::restore(app.db, out, &msg) == 0 && msg.find("not a Turbo scouts backup") != std::string::npos, msg);
        // a backup from another career (names do not match): nothing written, the backup kept
        { std::ofstream((scouts::backup_path(out)).string()) << R"({"format":"turbo-scouts-backup","scouts":[{"scoutid":0,"firstname":"Zed","lastname":"Q","experience":1,"knowledge":1}]})"; }
        CHECK(scouts::restore(app.db, out, &msg) == 0 && fs::exists(scouts::backup_path(out)), msg);
        fs::remove(scouts::backup_path(out), ec);
    });

    run_case("parity UI: Managers > Your scouts: Set every scout and Restore originals", [&] {
        app.request_tab = 2;
        ui.frames(3);
        CHECK(ui.click("Your scouts (career staff)"), "open the scouts section");
        ui.frames(3);
        CHECK(ui.click("Set every scout##scoutbest"), "Set every scout");
        ui.frames(3);
        auto s = scouts::read(app.db);
        CHECK(s.size() == 2 && s[0].experience == 5 && s[1].knowledge == 5, "set to 5 through the panel");
        CHECK(ui.toast_contains("Scouts: 2 of 2 set to level 5"), "toast");
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
        scroll_into_view(ui, "Bulk edit players");
        CHECK(ui.click("Bulk edit players"), "close Bulk edit");
        for (ImGuiWindow* w : GImGui->Windows)
            if (std::string(w->Name).find("##tools") != std::string::npos) ImGui::SetScrollY(w, 0.0f);
        ui.frames(2);
    });
}

}  // namespace parity_test
