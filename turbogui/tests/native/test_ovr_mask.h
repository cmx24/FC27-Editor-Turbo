// Native tests (3.2 calc-ovr, Pit wall addition 2026-10-25): the "Shown OVR (display only)" field of the player header and its store
// core/ovr_mask.h. Included by test_main.cpp after test_calc_ovr.h; ovr_mask_test::ui_cases runs inside the UI block. Cases are named
// "Shown OVR: ..." so TURBO_TEST_ONLY="shown ovr" runs them alone.
//   (a) the LOCKED schema {"version":1,"players":{"<pid>":<int>}} round-trips through set / load / get, written atomically (no .tmp left);
//   (b) Clear removes the pid, and the file when it is the last one;
//   (c) an invalid value (0, 100, -5), a bad career id and a pid of 0 are refused and write nothing; an unreadable file is never rewritten;
//   (e) write_atomic never deletes the real file before its replacement is in place: a failing replacement (test hook, and on Windows a
//       real handle held without FILE_SHARE_DELETE) returns false and leaves the old bytes; one retry is allowed;
//   (d) the header field: inside an identified career Set writes careers\<id>\ovr_mask.json, a bad value is refused with the file
//       unchanged, Clear removes it; an empty or all-spaces box with nothing stored says "Nothing to clear" and writes nothing;
//       outside an identified career nothing is written; the players table is never touched.
#pragma once
#include <string>

#include "core/ovr_mask.h"
#include "ui/ui_player_header.h"
#include "ui/ui_shell.h"

namespace ovr_mask_test {

using namespace turbo;

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void ui_cases(App& app, Ui& ui) {
    using shell::Page;
    namespace om = turbo::ovr_mask;
    const std::string kCareer = "0123456789abcdef";

    run_case("Shown OVR: the locked schema round-trips, Clear removes the pid and then the file, invalid values are refused", [&] {
        const fs::path root = fs::temp_directory_path() / "turbo_ovr_mask_test";
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path f = om::file_for(root, kCareer);
        CHECK(f.filename() == "ovr_mask.json" && f.parent_path().filename() == kCareer && f.parent_path().parent_path().filename() == "careers",
              "the file is turbo_output\\careers\\<career id>\\ovr_mask.json: " + f.string());
        std::string err;
        CHECK(om::set(root, kCareer, 1001, 77, &err), "set 1001 -> 77: " + err);
        CHECK(om::set(root, kCareer, 1002, 1, &err) && om::set(root, kCareer, 1003, 99, &err), "the bounds 1 and 99 are accepted: " + err);
        const json j = json::parse(slurp(f));
        CHECK(j.is_object() && j.size() == 2 && j.value("version", 0) == 1 && j["players"].is_object() && j["players"].size() == 3, "schema: version 1 + players object");
        CHECK(j["players"]["1001"] == 77 && j["players"]["1002"] == 1 && j["players"]["1003"] == 99, "pids are decimal-string keys with integer values");
        CHECK(!fs::exists(fs::path(f) += ".tmp"), "atomic write leaves no .tmp");
        CHECK(om::get(root, kCareer, 1001) == 77 && om::get(root, kCareer, 4242) == 0, "get reads it back, 0 = none");
        CHECK(om::set(root, kCareer, 1001, 55, &err) && om::get(root, kCareer, 1001) == 55, "set again replaces the value");
        om::Map m;
        CHECK(om::load(f, m, &err) && m.size() == 3 && m[1001] == 55, "load round-trips: " + err);

        // invalid values, ids: refused, nothing changes
        const std::string before = slurp(f);
        for (int bad : {0, 100, -5, 1000})
            CHECK(!om::set(root, kCareer, 1001, bad, &err) && err.find("1 to 99") != std::string::npos, "refused: " + std::to_string(bad));
        CHECK(!om::set(root, "not-a-career", 1001, 50, &err), "a bad career id is refused");
        CHECK(!om::set(root, kCareer, 0, 50, &err), "pid 0 is refused");
        CHECK(slurp(f) == before, "refused writes left the file as it was");

        // Clear
        CHECK(om::clear(root, kCareer, 1001, &err) && om::get(root, kCareer, 1001) == 0 && fs::exists(f), "Clear removes the pid, the file stays while others remain");
        CHECK(om::clear(root, kCareer, 1001, &err), "Clear of an absent pid is a no-op");
        CHECK(om::clear(root, kCareer, 1002, &err) && om::clear(root, kCareer, 1003, &err), "Clear the last ones");
        CHECK(!fs::exists(f), "the file is removed when it is empty");

        // an unreadable file is never acted on
        fs::create_directories(f.parent_path());
        { std::ofstream o(f, std::ios::binary); o << "{ not json"; }
        CHECK(!om::set(root, kCareer, 1001, 50, &err) && slurp(f) == "{ not json", "unreadable file: refused, left untouched");
        { std::ofstream o(f, std::ios::binary); o << "{\"version\":1,\"players\":{\"7\":150}}"; }
        CHECK(!om::set(root, kCareer, 1001, 50, &err), "a stored value out of 1..99 makes the file unreadable: refused");
        fs::remove_all(root, ec);
    });

    run_case("Shown OVR: a failed replacement never deletes the old file (write_atomic)", [&] {
        const fs::path dir = fs::temp_directory_path() / "turbo_ovr_mask_atomic_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path p = dir / "ovr_mask.json";
        const std::string oldb = "{\"version\":1,\"players\":{\"1001\":77}}\n";
        const std::string newb = "{\"version\":1,\"players\":{\"1001\":55}}\n";
        std::string err;
        CHECK(om::write_atomic(p, oldb, &err) && slurp(p) == oldb, "baseline write: " + err);

        // (a) the replacement fails every time (test hook): the old bytes stay, only the .tmp goes, the result is false with the reason
        om::replace_hook() = [](const fs::path&, const fs::path&, std::error_code& e) {
            e = std::make_error_code(std::errc::permission_denied);
            return false;
        };
        err.clear();
        const bool ok = om::write_atomic(p, newb, &err);
        om::replace_hook() = nullptr;
        CHECK(!ok && !err.empty(), "a failing replacement returns false with the reason: '" + err + "'");
        CHECK(fs::exists(p) && slurp(p) == oldb, "the OLD file is still there with its old content after a failed replacement");
        CHECK(!fs::exists(fs::path(p) += ".tmp"), "the .tmp is removed");

        // (b) the first replacement fails, the retry works: the new bytes are in, no .tmp
        static int calls;
        calls = 0;
        om::replace_hook() = [](const fs::path& from, const fs::path& to, std::error_code& e) {
            if (++calls == 1) {
                e = std::make_error_code(std::errc::device_or_resource_busy);
                return false;
            }
            om::replace_hook() = nullptr;
            return om::replace_file(from, to, e);
        };
        err.clear();
        CHECK(om::write_atomic(p, newb, &err) && calls == 2 && slurp(p) == newb && !fs::exists(fs::path(p) += ".tmp"), "one retry succeeds: " + err);
        om::replace_hook() = nullptr;

#ifdef _WIN32
        // (c) the real thing: another handle holds the file without FILE_SHARE_DELETE, so MoveFileExW cannot replace it
        {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(h != INVALID_HANDLE_VALUE, "fixture handle on the old file");
            err.clear();
            const bool ok2 = om::write_atomic(p, oldb, &err);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            CHECK(!ok2 && slurp(p) == newb && !fs::exists(fs::path(p) += ".tmp"),
                  "a held file: false, its content intact, no .tmp: '" + err + "'");
            CHECK(om::write_atomic(p, oldb, &err) && slurp(p) == oldb, "released: the write goes through: " + err);
        }
#endif
        fs::remove_all(dir, ec);
    });

    run_case("Shown OVR: the player header field writes the career's ovr_mask.json only, refuses bad values, Clear removes it", [&] {
        const bool was_legacy = app.legacy_pages;
        const int64_t was_sel = app.sel_player;
        const std::string was_career = app.reapply.career;
        const fs::path career_dir = app.turbo_output() / "careers" / kCareer;
        const fs::path f = om::file_for(app.turbo_output(), kCareer);
        std::error_code ec;
        // a stale fixture folder (a crashed earlier run) is removed first, but only under the test fixture root (app.bridge.root())
        const std::string root_s = fs::weakly_canonical(app.bridge.root(), ec).generic_string();
        const std::string dir_s = fs::weakly_canonical(career_dir, ec).generic_string();
        CHECK(!root_s.empty() && dir_s.size() > root_s.size() && dir_s.compare(0, root_s.size(), root_s) == 0,
              "the career folder is under the fixture root: " + dir_s);
        if (fs::exists(career_dir) && dir_s.compare(0, root_s.size(), root_s) == 0) fs::remove_all(career_dir, ec);
        CHECK(!fs::exists(career_dir), "no stale career folder left from an earlier run");
        if (app.busy()) ui.click("Cancel");
        app.legacy_pages = false;
        app.sel_player = 1001;
        app.go(Page::PlayerEdit);
        ui.frames(4);
        const PlayerRow* p = app.model.player(1001);
        CHECK(p != nullptr, "fixture player 1001");
        const int stored = p ? p->overall : 0;
        CHECK(ui.find("###hdr_shown_ovr") != nullptr && ui.find("Set###hdr_shown_set") != nullptr && ui.find("Clear###hdr_shown_clear") != nullptr,
              "the header has the Shown OVR box, Set and Clear");

        // outside an identified career: nothing is written
        app.reapply.career.clear();
        ui.frames(2);
        CHECK(ui.type_into(ui.find("###hdr_shown_ovr"), "70"), "type 70 outside a career");
        CHECK(!fs::exists(f) && !player_header_state().shown_note.empty(), "refused: no file, the card says why: '" + player_header_state().shown_note + "'");

        // inside the career
        app.reapply.career = kCareer;
        ui.frames(2);
        CHECK(app.career_faces_wanted() == kCareer, "career in effect");
        CHECK(ui.type_into(ui.find("###hdr_shown_ovr"), "70"), "type 70 + Enter");
        CHECK(fs::exists(f) && om::get(app.turbo_output(), kCareer, 1001) == 70, "ovr_mask.json holds 1001 -> 70");
        CHECK(player_header_state().shown_ovr == 70, "the card reads it back");
        if (p) CHECK(app.model.player(1001)->overall == stored, "the stored overall is untouched");
        const std::string before = slurp(f);
        CHECK(ui.type_into(ui.find("###hdr_shown_ovr"), "150"), "type 150");
        CHECK(slurp(f) == before && om::get(app.turbo_output(), kCareer, 1001) == 70, "150 is refused: the file is unchanged");
        CHECK(ui.type_into(ui.find("###hdr_shown_ovr"), "0"), "type 0");
        CHECK(slurp(f) == before, "0 is refused: the file is unchanged");
        CHECK(ui.click("Clear###hdr_shown_clear"), "click Clear");
        ui.frames(2);
        CHECK(!fs::exists(f) && player_header_state().shown_ovr == 0, "Clear removes the pid and the empty file");
        if (p) CHECK(app.model.player(1001)->overall == stored, "the stored overall is still untouched");

        // an empty box with nothing stored: "nothing to clear", nothing written (3.2 review fix: it said "Cleared")
        CHECK(ui.type_into(ui.find("###hdr_shown_ovr"), ""), "empty box + Enter, nothing stored");
        CHECK(!fs::exists(f), "empty box with nothing stored writes no file");
        CHECK(player_header_state().shown_note.find("Nothing to clear") != std::string::npos && player_header_state().shown_note.find("Cleared") == std::string::npos,
              "the card says nothing to clear, not Cleared: '" + player_header_state().shown_note + "'");
        {
            std::string msg;
            CHECK(!shown_ovr_apply(app, 1001, "   ", &msg) && msg.find("Nothing to clear") != std::string::npos && !fs::exists(f),
                  "an all-spaces box is the same: '" + msg + "'");
            // another player has a value: the empty box for 1001 leaves that file byte for byte as it was
            std::string err;
            CHECK(om::set(app.turbo_output(), kCareer, 2002, 60, &err), "another player holds 60: " + err);
            const std::string other = slurp(f);
            CHECK(!shown_ovr_apply(app, 1001, "", &msg) && msg.find("Nothing to clear") != std::string::npos && slurp(f) == other,
                  "empty box for 1001 does not touch the file that holds 2002: '" + msg + "'");
            CHECK(om::clear(app.turbo_output(), kCareer, 2002, &err) && !fs::exists(f), "fixture value removed");
        }

        fs::remove_all(career_dir, ec);
        app.reapply.career = was_career;
        app.sel_player = was_sel;
        app.legacy_pages = was_legacy;
        ui.frames(2);
    });
}

}  // namespace ovr_mask_test
