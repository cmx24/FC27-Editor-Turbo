// Native tests (3.2 calc-ovr, Pit wall addition 2026-10-25): the "Shown OVR (display only)" field of the player header and its store
// core/ovr_mask.h. Included by test_main.cpp after test_calc_ovr.h; ovr_mask_test::ui_cases runs inside the UI block. Cases are named
// "Shown OVR: ..." so TURBO_TEST_ONLY="shown ovr" runs them alone.
//   (a) the LOCKED schema {"version":1,"players":{"<pid>":<int>}} round-trips through set / load / get, written atomically (no .tmp left);
//   (b) Clear removes the pid, and the file when it is the last one;
//   (c) an invalid value (0, 100, -5), a bad career id and a pid of 0 are refused and write nothing; an unreadable file is never rewritten;
//   (d) the header field: inside an identified career Set writes careers\<id>\ovr_mask.json, a bad value is refused with the file
//       unchanged, Clear removes it; outside an identified career nothing is written; the players table is never touched.
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

    run_case("Shown OVR: the player header field writes the career's ovr_mask.json only, refuses bad values, Clear removes it", [&] {
        const bool was_legacy = app.legacy_pages;
        const int64_t was_sel = app.sel_player;
        const std::string was_career = app.reapply.career;
        const fs::path career_dir = app.turbo_output() / "careers" / kCareer;
        const fs::path f = om::file_for(app.turbo_output(), kCareer);
        std::error_code ec;
        const bool had_dir = fs::exists(career_dir);
        if (!had_dir) fs::remove_all(career_dir, ec);
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

        fs::remove_all(career_dir, ec);
        app.reapply.career = was_career;
        app.sel_player = was_sel;
        app.legacy_pages = was_legacy;
        ui.frames(2);
    });
}

}  // namespace ovr_mask_test
