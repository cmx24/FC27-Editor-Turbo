// Native tests (2.19 career faces, core/career_faces.h): a face picture chosen in a career lives only in that career.
//   core: paths, the overlay store, sync (copy in / put back, another career, no career, a global custom picture kept and put
//         back byte for byte, someone else's file left alone, an unreadable safety record never acted on, the start-up restore)
//   app:  write_face_picture in an identified career writes the career's copy (never the global file directly), another career
//         and no career see the original, Undo, a career not identified is refused, the chooser (apply_real_face) end to end.
// Included by test_main.cpp: career_faces_test::run() from main, career_faces_test::ui_cases() from the UI world.
#pragma once
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/career_faces.h"

namespace career_faces_test {

namespace cf = turbo::career_faces;

inline std::string bytes_of(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string("<absent>");
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
inline void put_file(const fs::path& p, const std::string& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(b.data(), static_cast<std::streamsize>(b.size()));
}
inline std::vector<uint8_t> vec(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

inline void run() {
    const std::string A = "0123456789abcdef", B = "fedcba9876543210";
    const std::string P1 = "data/ui/imgAssets/heads/p211381.dds", P2 = "data/ui/imgAssets/heads/p999001.dds",
                      S1 = "data/ui/imgAssets/heads_staff/heads_staff_501.dds";

    run_case("2.19 career faces: career ids and face paths", [&] {
        CHECK(cf::is_career_id(A) && cf::is_career_id(B), "16 lower-case hex digits");
        CHECK(!cf::is_career_id("") && !cf::is_career_id("career:" + A) && !cf::is_career_id("0123456789ABCDEF") &&
                  !cf::is_career_id("0123456789abcde") && !cf::is_career_id("0123456789abcdeg"),
              "anything else is no career");
        CHECK(cf::is_face_path(P1) && cf::is_face_path(S1) && cf::is_face_path("data/ui/imgAssets/heads/P5.DDS") &&
                  cf::is_face_path("data\\ui\\imgAssets\\heads\\p5.dds"),
              "player and manager minifaces, any letter case or slash");
        CHECK(!cf::is_face_path("data/ui/imgAssets/crest/l1.dds") && !cf::is_face_path("data/ui/imgAssets/heads/../crest/l1.dds") &&
                  !cf::is_face_path("data/ui/imgAssets/heads/sub/p1.dds") && !cf::is_face_path("data/ui/imgAssets/heads/p1.png") &&
                  !cf::is_face_path("data/ui/imgAssets/heads/") && !cf::is_face_path("C:/x/data/ui/imgAssets/heads/p1.dds") &&
                  !cf::is_face_path("data/ui/imgAssets/youthheads/p1.dds"),
              "crests, other folders, subfolders, '..', other types and absolute paths are not career faces");
    });

    run_case("2.19 career faces: copied in for its career only, put back for another career, no career and at Turbo start", [&] {
        const fs::path root = g_out / "career_faces_core";
        fs::remove_all(root);
        const cf::Paths p{root / "turbo_output", root / "mods" / "legacy"};
        const fs::path m1 = cf::mods_file(p, P1), m2 = cf::mods_file(p, P2), ms = cf::mods_file(p, S1);
        CHECK(cf::overlay_file(p, A, P1) == p.turbo_output / "careers" / A / "heads" / "p211381.dds", "overlay: careers\\<id>\\heads\\p<pid>.dds");
        CHECK(cf::overlay_file(p, A, S1) == p.turbo_output / "careers" / A / "heads_staff" / "heads_staff_501.dds", "heads_staff too");

        // nothing anywhere: a sync is a no-op and leaves no record
        cf::SyncResult r = cf::sync(p, "");
        CHECK(r.ok() && r.touched().empty() && !fs::exists(cf::manifest_file(p)), "empty start: nothing to do, no record: " + r.summary());

        // career A's choices: p211381 (a real player WITH a global custom picture before) and p999001 (none before)
        put_file(m1, "GLOBAL-CUSTOM-211381");
        std::string err;
        fs::path undo1;
        CHECK(cf::save_overlay(p, A, P1, vec("A-BOUADDI"), &err, &undo1) && undo1.empty(), "A's copy of p211381: " + err);
        CHECK(cf::save_overlay(p, A, P2, vec("A-NEWFACE"), &err, nullptr), "A's copy of p999001: " + err);
        CHECK(cf::save_overlay(p, B, P1, vec("B-OTHER"), &err, nullptr), "B's own copy of p211381: " + err);
        CHECK(bytes_of(m1) == "GLOBAL-CUSTOM-211381" && !fs::exists(m2), "save_overlay never touches mods\\legacy");
        CHECK(!cf::save_overlay(p, "", P1, vec("x"), &err, nullptr) && !cf::save_overlay(p, A, "data/ui/imgAssets/crest/l1.dds", vec("x"), &err, nullptr) &&
                  !cf::save_overlay(p, A, P1, {}, &err, nullptr),
              "no career, not a face path, no bytes: refused");

        r = cf::sync(p, A);
        CHECK(r.ok() && r.copied.size() == 2 && r.restored.empty() && r.career == A, "A loaded: both copied in: " + r.summary());
        CHECK(bytes_of(m1) == "A-BOUADDI" && bytes_of(m2) == "A-NEWFACE", "mods\\legacy holds A's pictures");
        CHECK(cf::active_career(p) == A && fs::exists(cf::manifest_file(p)), "the safety record names A");
        r = cf::sync(p, A);
        CHECK(r.ok() && r.touched().empty(), "A again (Refresh, a frame later): nothing changes: " + r.summary());

        // another career: A's pictures go, B's come in; the global custom picture is the base B's copy goes over
        r = cf::sync(p, B);
        CHECK(r.ok() && r.restored.size() == 2 && r.copied.size() == 1 && r.career == B, "B loaded: " + r.summary());
        CHECK(bytes_of(m1) == "B-OTHER" && !fs::exists(m2), "B sees its own p211381 and no p999001");

        // no career (main menu, Kick Off, the squad file): the original everywhere, byte for byte
        r = cf::sync(p, "");
        CHECK(r.ok() && r.restored.size() == 1 && r.copied.empty() && r.career.empty(), "no career: " + r.summary());
        CHECK(bytes_of(m1) == "GLOBAL-CUSTOM-211381" && !fs::exists(m2), "the global custom picture is back, p999001 gone");
        CHECK(!fs::exists(cf::manifest_file(p)) && cf::active_career(p).empty(), "no record left");
        CHECK(!fs::exists(p.turbo_output / "careers" / "_base" / "data" / "ui" / "imgAssets" / "heads" / "p211381.dds"),
              "the base copy is cleaned up once put back");

        // the game (or Turbo) ended inside career A: Turbo's start (sync "") puts everything back
        fs::remove(m1);  // this time a real player with NO custom picture (the game's own shows)
        r = cf::sync(p, A);
        CHECK(r.ok() && bytes_of(m1) == "A-BOUADDI", "A loaded again: " + r.summary());
        r = cf::sync(p, "");  // = App construction at the next Turbo start
        CHECK(r.ok() && !fs::exists(m1) && !fs::exists(m2) && r.restored.size() == 2, "Turbo start: nothing of A's left: " + r.summary());

        // manager pictures (heads_staff) work the same way
        CHECK(cf::save_overlay(p, A, S1, vec("A-MANAGER"), &err, nullptr), "A's manager picture: " + err);
        r = cf::sync(p, A);
        CHECK(r.ok() && bytes_of(ms) == "A-MANAGER", "heads_staff copied in: " + r.summary());
        r = cf::sync(p, B);
        CHECK(r.ok() && !fs::exists(ms), "and out for B: " + r.summary());
        cf::sync(p, "");
    });

    run_case("2.19 career faces: Undo, a changed copy, someone else's file, an unreadable record", [&] {
        const fs::path root = g_out / "career_faces_core2";
        fs::remove_all(root);
        const cf::Paths p{root / "turbo_output", root / "mods" / "legacy"};
        const fs::path m1 = cf::mods_file(p, P1);
        std::string err;
        fs::path u1, u2;
        CHECK(cf::save_overlay(p, A, P1, vec("A-ONE"), &err, &u1) && u1.empty(), "first copy: " + err);
        CHECK(cf::sync(p, A).ok() && bytes_of(m1) == "A-ONE", "in");
        // a second choice in the same career: the earlier copy is kept for Undo, mods\legacy follows
        CHECK(cf::save_overlay(p, A, P1, vec("A-TWO"), &err, &u2) && !u2.empty() && bytes_of(u2) == "A-ONE", "second copy, first kept: " + err);
        cf::SyncResult r = cf::sync(p, A);
        CHECK(r.ok() && bytes_of(m1) == "A-TWO", "the changed copy replaces Turbo's earlier one: " + r.summary());
        // Undo of the second choice: the first is back
        CHECK(cf::restore_overlay(p, A, P1, u2, &err) && bytes_of(cf::overlay_file(p, A, P1)) == "A-ONE", "undo 2: " + err);
        CHECK(cf::sync(p, A).ok() && bytes_of(m1) == "A-ONE", "mods\\legacy follows the undo");
        // Undo of the first choice: the career has no copy, the original shows
        CHECK(cf::restore_overlay(p, A, P1, u1, &err) && !cf::has_overlay(p, A, P1), "undo 1: " + err);
        r = cf::sync(p, A);
        CHECK(r.ok() && !fs::exists(m1) && r.restored.size() == 1, "nothing of Turbo's left: " + r.summary());
        // remove_overlay (the miniface editor's Remove in a career)
        CHECK(cf::save_overlay(p, A, P1, vec("A-THREE"), &err, nullptr) && cf::sync(p, A).ok() && bytes_of(m1) == "A-THREE", "in again");
        fs::path u3;
        CHECK(cf::remove_overlay(p, A, P1, &err, &u3) && !u3.empty() && !cf::has_overlay(p, A, P1), "removed: " + err);
        CHECK(!cf::remove_overlay(p, A, P1, &err, &u3), "a second Remove: there is none");
        CHECK(cf::sync(p, A).ok() && !fs::exists(m1), "out of mods\\legacy");

        // someone else (Live Editor, the user) wrote over Turbo's copy: theirs is never removed
        CHECK(cf::save_overlay(p, A, P1, vec("A-FOUR"), &err, nullptr) && cf::sync(p, A).ok(), "in");
        put_file(m1, "USER-WROTE-THIS");
        r = cf::sync(p, "");
        CHECK(r.ok() && r.left_foreign.size() == 1 && bytes_of(m1) == "USER-WROTE-THIS" && !fs::exists(cf::manifest_file(p)),
              "left alone, the record dropped: " + r.summary());
        fs::remove(m1);

        // an unreadable safety record: moved aside, nothing in mods\legacy touched
        put_file(m1, "SOMETHING");
        put_file(cf::manifest_file(p), "{not json");
        r = cf::sync(p, "");
        CHECK(!r.ok() && bytes_of(m1) == "SOMETHING" && !fs::exists(cf::manifest_file(p)), "never acted on: " + r.summary());
        bool bad = false;
        for (auto& e : fs::directory_iterator(p.turbo_output / "careers")) bad = bad || e.path().filename().u8string().find("active.json.bad") == 0;
        CHECK(bad, "kept as active.json.bad for a look");
        r = cf::sync(p, A);
        CHECK(r.ok() && bytes_of(m1) == "A-FOUR", "the next sync works from a clean record (SOMETHING kept as the base): " + r.summary());
        r = cf::sync(p, "");
        CHECK(r.ok() && bytes_of(m1) == "SOMETHING", "and puts SOMETHING back: " + r.summary());

        // the copy-in record is written before mods\legacy changes: a crash between the two leaves the original, never lost
        CHECK(cf::save_overlay(p, B, P2, vec("B-X"), &err, nullptr), "B's copy");
        r = cf::sync(p, B);
        CHECK(r.ok() && bytes_of(cf::mods_file(p, P2)) == "B-X", "B in");
        const std::string rec = bytes_of(cf::manifest_file(p));
        CHECK(rec.find("\"career\": \"" + B + "\"") != std::string::npos && rec.find(P2) != std::string::npos && rec.find("\"fnv\"") != std::string::npos,
              "the record: career, path, hash: " + rec);
        cf::sync(p, "");
    });
}

// A clean start for a UI case: every career's copy put back out of mods\legacy (as at a Turbo start), no career copies kept,
// then the world's career in effect again (nothing to copy in)
inline void reset(App& app, Ui& ui) {
    app.career_faces_sync("", "test reset");
    std::error_code ec;
    fs::remove_all(app.turbo_output() / "careers", ec);
    ui.frames(1);
}

// The App side, in the UI world (in_cm, a career connected): `le` = the world's Live Editor folder
inline void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("UI 2.19 career faces: a face picture written in a career is that career's only; other career / no career see the original; Undo", [&] {
        reset(app, ui);
        const std::string A = "aaaaaaaaaaaaaaa1", B = "bbbbbbbbbbbbbbb2";
        const std::string was = app.reapply.career;
        struct Back {
            App& a;
            std::string was;
            Ui& u;
            ~Back() {
                a.reapply.career = was;
                u.frames(2);
            }
        } back{app, was, ui};
        const std::string rel = legacy_path::player_miniface(3001);
        const fs::path mods = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p3001.dds";
        fs::remove(mods);
        CHECK(app.bridge.state().in_cm, "the UI world is in a career");

        // a career Turbo has not identified: refused, nothing written anywhere
        app.reapply.career.clear();
        ui.frames(2);
        std::string err;
        App::UndoStep::Picture rec;
        CHECK(app.career_faces_wanted().empty() && app.career_faces_refused(&err), "not identified: refused (" + err + ")");
        CHECK(!app.write_face_picture(rel, vec("X"), &err, &rec) && !fs::exists(mods) && err.find("not identified") != std::string::npos,
              "no picture written: " + err);

        // career A identified
        app.reapply.career = A;
        ui.frames(2);
        CHECK(app.career_faces_wanted() == A && app.career_faces_active() == A, "career A in effect: '" + app.career_faces_wanted() + "'");
        CHECK(app.write_face_picture(rel, vec("A-PIC"), &err, &rec), "written: " + err);
        const cf::Paths p = app.career_faces_paths();
        CHECK(rec.career == A && rec.backup.empty() && bytes_of(cf::overlay_file(p, A, rel)) == "A-PIC", "A's own copy, Undo knows it");
        CHECK(bytes_of(mods) == "A-PIC", "copied into mods\\legacy while A is loaded (Live Editor serves it)");
        CHECK(log_has(app, "career faces: a face picture written in this career: career " + A, "; in " + rel), "log line");

        // another career, then none: the original (no file) shows
        app.reapply.career = B;
        ui.frames(2);
        CHECK(!fs::exists(mods), "career B: A's picture is out of mods\\legacy");
        CHECK(log_has(app, "career faces: career identified: career " + B, "; back " + rel), "log line for B");
        app.reapply.career = A;
        ui.frames(2);
        CHECK(bytes_of(mods) == "A-PIC", "A again: in again");
        // leaving the career = in_cm false or a new load; simulated by no identity
        app.reapply.career.clear();
        ui.frames(2);
        CHECK(!fs::exists(mods), "no identified career: out");
        app.reapply.career = A;
        ui.frames(2);

        // Undo: the career's copy goes, the original shows
        CHECK(app.undo_face_picture(rec, &err) && !cf::has_overlay(p, A, rel) && !fs::exists(mods), "undo: " + err);

        // the chooser end to end (apply_real_face: 1004's head and picture to 3001) in career A
        const PlayerRow* pr = app.model.player(3001);
        const Table* pt = app.db.table("players");
        CHECK(pr && pt, "player 3001");
        if (!pr || !pt) return;
        std::string msg;
        bool in_step = false;
        const size_t undo0 = app.undo_count(3001);
        CHECK(apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg, &in_step), "head of 1004 given in career A: " + msg);
        if (in_step) {
            CHECK(cf::has_overlay(p, A, rel), "the chooser wrote A's copy p3001 (not the global file directly)");
            CHECK(fs::exists(mods) && bytes_of(mods) == bytes_of(cf::overlay_file(p, A, rel)), "and it is in mods\\legacy while A is loaded");
            app.reapply.career = B;
            ui.frames(2);
            CHECK(!fs::exists(mods), "career B: Boufal keeps his own face (no p3001 in mods\\legacy)");
            app.reapply.career = A;
            ui.frames(2);
            CHECK(fs::exists(mods), "back in A: the chosen picture");
        } else {
            CHECK(false, "the picture of 1004 was not available to the chooser: " + msg);
        }
        CHECK(app.undo_count(3001) == undo0 + 1 && app.undo(3001), "one Undo");
        CHECK(!cf::has_overlay(p, A, rel) && !fs::exists(mods), "Undo: A's copy and the mods\\legacy file gone");
        ui.frames(2);
    });
}

}  // namespace career_faces_test
