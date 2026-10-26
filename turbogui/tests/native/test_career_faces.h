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

    // ---- 2.20 faces safety (Cassio 2026-10-19: a head edit never overwrites or deletes the original in-game asset)
    run_case("2.20 faces safety (a): only Turbo's own hash-recorded global picture may be replaced or deleted; any other file is left alone", [&] {
        const fs::path root = g_out / "faces_safety_core";
        fs::remove_all(root);
        const cf::Paths p{root / "turbo_output", root / "mods" / "legacy"};
        const fs::path m1 = cf::mods_file(p, P1), m2 = cf::mods_file(p, P2);
        std::string why, err;
        CHECK(cf::owner_of(p, P1).owner == cf::Owner::None && cf::may_replace_global(p, P1, &why), "absent: Turbo may write it");
        // a file Turbo did not make (the 2,747-file donor set, another tool, the user, Turbo before 2.20)
        put_file(m1, "DONOR-OR-USER-211381");
        CHECK(cf::owner_of(p, P1).owner == cf::Owner::Foreign, "a file with no record of Turbo's: someone else's");
        CHECK(!cf::may_replace_global(p, P1, &why) && why.find("not made by Turbo") != std::string::npos, "never replaced or deleted: " + why);
        // Turbo's own global copy: recorded (hash) before the write
        CHECK(cf::record_global(p, P2, "TURBO-P2", &err), "recorded: " + err);
        put_file(m2, "TURBO-P2");
        const cf::OwnerInfo o2 = cf::owner_of(p, P2);
        CHECK(o2.owner == cf::Owner::TurboGlobal && o2.hash == cf::bytes_hash("TURBO-P2") && cf::may_replace_global(p, P2, &why),
              "Turbo's own copy (same hash): Turbo may replace it");
        CHECK(bytes_of(cf::global_own_file(p)).find("p999001.dds") != std::string::npos, "the record names the path");
        // someone wrote over Turbo's copy: theirs now
        put_file(m2, "USER-OVER-TURBO");
        CHECK(cf::owner_of(p, P2).owner == cf::Owner::Foreign && !cf::may_replace_global(p, P2, &why), "changed by someone else: left alone");
        CHECK(cf::forget_global(p, P2, &err) && bytes_of(cf::global_own_file(p)).find("p999001") == std::string::npos, "forgotten: " + err);
        // an unreadable record: every present file counts as someone else's, and no new record is written over it
        put_file(m2, "TURBO-P2");
        put_file(cf::global_own_file(p), "{not json");
        CHECK(cf::owner_of(p, P2).owner == cf::Owner::Foreign && !cf::may_replace_global(p, P2, &why), "unreadable record: hands off");
        CHECK(!cf::record_global(p, P2, "X", &err) && bytes_of(cf::global_own_file(p)) == "{not json", "never overwritten: " + err);
        fs::remove(cf::global_own_file(p));
        fs::remove(m2);
        // a career copy over a real file: real underneath (kept in careers\_base, back byte for byte on leave)
        fs::path u;
        CHECK(cf::save_overlay(p, A, P1, vec("A-CHOSEN"), &err, &u) && cf::sync(p, A).ok() && bytes_of(m1) == "A-CHOSEN", "A's copy in");
        const cf::OwnerInfo oc = cf::owner_of(p, P1);
        CHECK(oc.owner == cf::Owner::TurboCareer && oc.base_under, "a career copy with a real file under it");
        CHECK(cf::save_overlay(p, A, P2, vec("A-NEW"), &err, nullptr) && cf::sync(p, A).ok(), "A's copy of p999001 in");
        const cf::OwnerInfo on = cf::owner_of(p, P2);
        CHECK(on.owner == cf::Owner::TurboCareer && !on.base_under, "a career copy with nothing under it");
        CHECK(cf::sync(p, "").ok() && bytes_of(m1) == "DONOR-OR-USER-211381" && !fs::exists(m2), "left the career: the real file is back byte for byte");
    });

    run_case("2.20 faces safety (b): which miniface is real (kept) and which may be made", [&] {
        auto k = [](cf::Owner o, bool base, cf::Ships g) {
            cf::MinifaceFacts f;
            f.file.owner = o;
            f.file.base_under = base;
            f.game = g;
            return cf::classify_miniface(f);
        };
        using O = cf::Owner;
        using S = cf::Ships;
        using K = cf::MinifaceKind;
        CHECK(k(O::Foreign, false, S::No) == K::Real && k(O::Foreign, false, S::Unknown) == K::Real, "a file Turbo did not make: real");
        CHECK(k(O::TurboCareer, true, S::No) == K::Real, "a career copy over a real file: real");
        CHECK(k(O::None, false, S::Yes) == K::Real && k(O::TurboGlobal, false, S::Yes) == K::Real && k(O::TurboCareer, false, S::Yes) == K::Real,
              "the game ships one: real (whatever Turbo has in mods\\legacy)");
        CHECK(k(O::None, false, S::Unknown) == K::Unknown && k(O::TurboCareer, false, S::Unknown) == K::Unknown, "the game has not said: unknown");
        CHECK(k(O::None, false, S::No) == K::None, "nothing anywhere: none");
        CHECK(k(O::TurboGlobal, false, S::No) == K::TurboOwn && k(O::TurboCareer, false, S::No) == K::TurboOwn, "only Turbo's own: Turbo's own");
        CHECK(cf::keep_miniface(K::Real) && cf::keep_miniface(K::Unknown) && !cf::keep_miniface(K::None) && !cf::keep_miniface(K::TurboOwn),
              "real and unknown are kept (fail safe); none and Turbo's own may be made");
    });

    run_case("2.20 faces safety: heads_own.json keeps his own head once (the first original wins), per career and outside a career", [&] {
        const fs::path root = g_out / "faces_safety_own";
        fs::remove_all(root);
        const cf::Paths p{root / "turbo_output", root / "mods" / "legacy"};
        std::string err;
        bool created = false;
        CHECK(cf::heads_own_file(p, A) == p.turbo_output / "careers" / A / "heads_own.json", "careers\\<id>\\heads_own.json");
        CHECK(cf::heads_own_file(p, cf::squad_scope()) == p.turbo_output / "careers" / "_squad" / "heads_own.json", "outside a career: _squad");
        CHECK(cf::is_head_scope(A) && cf::is_head_scope(cf::squad_scope()) && !cf::is_head_scope("") && !cf::is_head_scope("../x"), "scopes");
        CHECK(!cf::record_own_head(p, "", false, 211381, "Boufal", {{"headassetid", 211381}}, &err), "no scope: refused");
        CHECK(!cf::record_own_head(p, A, false, 211381, "Boufal", {}, &err), "no fields: refused");
        CHECK(cf::record_own_head(p, A, false, 211381, "Boufal", {{"headassetid", 211381}, {"headclasscode", 0}}, &err, &created) && created,
              "first original stored: " + err);
        CHECK(cf::record_own_head(p, A, false, 211381, "Boufal", {{"headassetid", 1004}, {"headclasscode", 1}}, &err, &created) && !created,
              "a second give: nothing stored over it");
        cf::OwnHead h;
        CHECK(cf::find_own_head(p, A, false, 211381, h) && h.fields["headassetid"] == 211381 && h.fields["headclasscode"] == 0 && h.name == "Boufal" &&
                  !h.at.empty() && !h.picture,
              "the first original wins");
        CHECK(!cf::find_own_head(p, A, true, 211381, h), "managers are kept apart");
        CHECK(!cf::find_own_head(p, B, false, 211381, h) && !cf::find_own_head(p, cf::squad_scope(), false, 211381, h), "per career");
        CHECK(cf::record_own_head(p, cf::squad_scope(), false, 211381, "Boufal", {{"headassetid", 7}}, &err, &created) && created, "squad: " + err);
        CHECK(cf::set_own_picture(p, A, false, 211381, true, &err) && cf::find_own_head(p, A, false, 211381, h) && h.picture, "picture noted");
        const std::string on_disk = bytes_of(cf::heads_own_file(p, A));
        CHECK(on_disk.find("\"211381\"") != std::string::npos && on_disk.find("\"headassetid\": 211381") != std::string::npos, "on disk: " + on_disk);
        CHECK(cf::drop_own_head(p, A, false, 211381, &err) && !cf::find_own_head(p, A, false, 211381, h), "dropped: " + err);
        // an unreadable file: never overwritten, every record refused (the head write is then refused too)
        put_file(cf::heads_own_file(p, B), "{broken");
        CHECK(!cf::record_own_head(p, B, false, 3001, "X", {{"headassetid", 1}}, &err) && bytes_of(cf::heads_own_file(p, B)) == "{broken",
              "unreadable: refused, left as it is: " + err);
    });

    // Pit wall 13:25 / 13:34: a picture that never comes (the Managers page polled heads_staff_503.dds 303 times in 120 frames)
    run_case("2.20 picture lookups: a picture that is not there (waiting for its export, or missing) is looked at once a second over "
             "120 frames, never in every frame; it shows within about a second of arriving",
             [&] {
                 const fs::path root = g_out / "faces_safety_negcache";
                 fs::remove_all(root);
                 turbo::LegacyImages L(root);
                 const std::string w = "data/ui/imgAssets/heads_staff/heads_staff_503.dds", m = "data/ui/imgAssets/heads_staff/heads_staff_504.dds";
                 fs::create_directories(L.cache_dir());
                 { std::ofstream(L.cache_dir() / "missing.txt") << m << "\n"; }
                 double now = 100.0;
                 L.next_frame();
                 L.tick(now);
                 fs::path f;
                 CHECK(L.locate(w, &f) == turbo::LegacyImages::State::Waiting && L.locate(m, &f) == turbo::LegacyImages::State::Missing,
                       "first look: one waits for its export, the game has not the other");
                 // both files appear right after that look; a look at the file (the only way to see them) shows them at once
                 put_file(L.cache_file(w), "dds");
                 put_file(L.cache_file(m), "dds");
                 int seen_w = 0, seen_m = 0;  // the frame each was first seen (0 = never)
                 for (int i = 1; i <= 120; ++i) {
                     now += 1.0 / 60.0;
                     L.next_frame();
                     L.tick(now);
                     if (!seen_w && L.locate(w, &f) == turbo::LegacyImages::State::Game) seen_w = i;
                     if (!seen_m && L.locate(m, &f) == turbo::LegacyImages::State::Game) seen_m = i;
                 }
                 CHECK(seen_w >= 50 && seen_w <= 70, "waiting: not looked at again for about a second, then seen (frame " + std::to_string(seen_w) + " of 120)");
                 CHECK(seen_m >= 50 && seen_m <= 70, "missing: not looked at again for about a second, then seen (frame " + std::to_string(seen_m) + " of 120)");
                 // a new missing.txt, a reload: looked at the next time
                 const std::string w2 = "data/ui/imgAssets/heads_staff/heads_staff_505.dds";
                 CHECK(L.locate(w2, &f) == turbo::LegacyImages::State::Waiting, "another picture waits");
                 put_file(L.cache_file(w2), "dds");
                 L.reload(w2);  // (removes the cached copy and asks again)
                 put_file(L.cache_file(w2), "dds");
                 CHECK(L.locate(w2, &f) == turbo::LegacyImages::State::Game, "after a reload: looked at at once");
                 // calls without frames (unit callers, tools): every call looks, as before
                 turbo::LegacyImages U(root / "u");
                 fs::create_directories(U.cache_dir());
                 const std::string u = "data/ui/imgAssets/heads/p1.dds";
                 CHECK(U.locate(u, &f) == turbo::LegacyImages::State::Waiting, "no frames: waits");
                 put_file(U.cache_file(u), "dds");
                 CHECK(U.locate(u, &f) == turbo::LegacyImages::State::Game, "no frames: seen at the next call");
                 fs::remove_all(root);
             });
}

// A clean start for a UI case: every career's copy put back out of mods\legacy (as at a Turbo start), no career copies kept,
// then the world's career in effect again (nothing to copy in)
inline void reset(App& app, Ui& ui) {
    app.career_faces_sync("", "test reset");
    std::error_code ec;
    fs::remove_all(app.turbo_output() / "careers", ec);
    turbo::faces_safety_reset();  // 2.20: no miniface queued by an earlier case
    ui.frames(1);
}

// ---- 2.20 faces safety, the App side
// A game picture made "the game has none" for a case (its exported copy moved away, listed in missing.txt); put back at the end
struct NoGamePicture {
    App& app;
    fs::path missing_txt;
    std::string keep_missing;
    std::vector<std::pair<fs::path, std::string>> moved;
    NoGamePicture(App& a, const std::vector<std::string>& rels) : app(a) {
        const fs::path cache = app.legacy.cache_dir();
        missing_txt = cache / "missing.txt";
        keep_missing = bytes_of(missing_txt);
        for (const std::string& rel : rels) {
            const fs::path f = cache / fs::u8path(rel);
            const std::string b = bytes_of(f);
            if (b != "<absent>") moved.emplace_back(f, b), fs::remove(f);
        }
        fs::create_directories(cache);
        std::ofstream m(missing_txt, std::ios::binary | std::ios::app);
        for (const std::string& rel : rels) m << "\n" << rel << "\n";
        m.close();
        app.legacy.refresh_missing();
    }
    ~NoGamePicture() {
        for (const auto& [f, b] : moved) put_file(f, b);
        if (keep_missing != "<absent>") put_file(missing_txt, keep_missing);
        else fs::remove(missing_txt);
        app.legacy.refresh_missing();
    }
};

// The career in effect for a case (put back at the end)
struct InCareer {
    App& app;
    Ui& ui;
    std::string was;
    InCareer(App& a, Ui& u, const std::string& career) : app(a), ui(u), was(a.reapply.career) {
        app.reapply.career = career;
        ui.frames(2);
    }
    ~InCareer() {
        app.reapply.career = was;
        ui.frames(2);
    }
};

inline std::vector<std::string> head_fields_of(App& app, int64_t pid) {
    std::vector<std::string> v;
    const Table* t = app.db.table("players");
    const PlayerRow* pr = app.model.player(pid);
    if (!t || !pr) return v;
    for (const char* f : {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation", "hairtypecode",
                          "haircolorcode", "facialhairtypecode", "eyecolorcode", "skintonecode"})
        if (t->has(f)) v.push_back(std::string(f) + "=" + std::to_string(app.db.get_int(*t, pr->rec, f, -1)));
    return v;
}

inline void ui_cases_safety(App& app, Ui& ui, const fs::path& le) {
    const std::string A = "aaaaaaaaaaaaaaa1";
    const std::string rel = legacy_path::player_miniface(3001);
    const fs::path mods = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p3001.dds";
    const fs::path writes_log = app.bridge.dir() / "asset_writes.log";

    run_case("UI 2.20 faces safety: a head given to a player whose miniface Turbo did not make leaves it byte for byte; his own head is kept "
             "first (first original wins); every field in asset_writes.log; His own head puts it back",
             [&] {
                 reset(app, ui);
                 InCareer in(app, ui, A);
                 auto keep_capture = app.capture;
                 app.capture.reset();
                 const Table* pt = app.db.table("players");
                 const PlayerRow* pr = app.model.player(3001);
                 const PlayerRow* owner = app.model.player(1004);
                 CHECK(pt && pr && owner, "players 3001 and 1004");
                 if (!pt || !pr || !owner) return;
                 const std::vector<std::string> own = head_fields_of(app, 3001);
                 const int64_t own_head = app.db.get_int(*pt, pr->rec, "headassetid", -1);
                 // his real miniface: a custom file in mods\legacy with no record of Turbo's (as the 2,747-file donor set, or Boufal's)
                 const std::string real = [] {
                     const std::vector<uint8_t> b = encode_dds_dxt5(solid(256, 256, 0x20, 0x40, 0xA0));
                     return std::string(b.begin(), b.end());
                 }();
                 put_file(mods, real);
                 const std::string log_before = bytes_of(writes_log);
                 const size_t undo0 = app.undo_count(3001);
                 std::string msg;
                 bool in_step = false;
                 CHECK(apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg, &in_step), "head of 1004 given: " + msg);
                 CHECK(app.db.get_int(*pt, pr->rec, "headassetid", -1) == app.db.get_int(*pt, owner->rec, "headassetid", -2), "the head is 1004's");
                 CHECK(bytes_of(mods) == real, "his real miniface p3001.dds is byte for byte the same");
                 const cf::Paths p = app.career_faces_paths();
                 CHECK(!cf::has_overlay(p, A, rel) && !fs::exists(cf::careers_dir(p) / A / "heads"), "no career copy made over it");
                 CHECK(in_step && msg.find("real miniface p3001.dds is kept") != std::string::npos, "said so: " + msg);
                 CHECK(log_has(app, "faces safety: player 3001", "real miniface p3001.dds kept"), "logged (the Tester's live check reads this line)");
                 CHECK(app.head_miniface_kept.count(3001) == 1, "no 'use the matching miniface' offer for it");
                 CHECK(faces_safety_pending() == 0, "nothing queued");
                 // his own head, kept before the first write
                 cf::OwnHead oh;
                 CHECK(cf::find_own_head(p, A, false, 3001, oh) && oh.fields["headassetid"] == own_head, "heads_own.json of career A has his own head");
                 CHECK(log_has(app, "heads own: player 3001", "own head kept in " + A), "logged");
                 // every field written: asset_writes.log
                 const std::string log_after = bytes_of(writes_log);
                 const std::string added = log_before == "<absent>" ? log_after : log_after.size() > log_before.size() ? log_after.substr(log_before.size()) : std::string();
                 CHECK(added.find("player 3001") != std::string::npos && added.find("headassetid") != std::string::npos &&
                           added.find("real-face chooser: head of") != std::string::npos,
                       "asset_writes.log: " + added.substr(0, 300));
                 CHECK(app.undo_count(3001) == undo0 + 1, "one undo step");
                 // a second give: the first original still wins
                 CHECK(apply_real_face(app, 3001, 1003, RealFaceOptions{}, &msg, &in_step), "head of 1003 given: " + msg);
                 CHECK(cf::find_own_head(p, A, false, 3001, oh) && oh.fields["headassetid"] == own_head, "the first original still wins");
                 CHECK(bytes_of(mods) == real, "his real miniface still byte for byte the same");
                 // His own head
                 CHECK(has_own_head(app, false, 3001), "His own head is offered");
                 const bool back = restore_own_head(app, false, 3001, &msg);
                 CHECK(back && msg.find("his own head is back") != std::string::npos, "His own head: " + msg);
                 CHECK(head_fields_of(app, 3001) == own, "every head field is his own again");
                 CHECK(bytes_of(mods) == real, "and his real miniface was never touched");
                 CHECK(bytes_of(writes_log).find("His own head") != std::string::npos, "the revert's writes are in asset_writes.log too");
                 CHECK(log_has(app, "heads own: player 3001", "his own head back in " + A), "logged");
                 CHECK(has_own_head(app, false, 3001) && restore_own_head(app, false, 3001, &msg) && msg == "he already has his own head",
                       "the record stays (an Undo of the revert can be put back again): " + msg);
                 // the UI: the chooser shows "His own head"
                 app.request_tab = 0;
                 ui.frames(2);
                 CHECK(ui.click("3001", "##plist") && ui.click("Appearance", "##pedit") && ui.click("Choose a real face..."), "chooser");
                 ui.frames(3);
                 CHECK(ui.find("His own head##faces") != nullptr, "the chooser has the His own head button");
                 // 3.2 f3 (charter 19.17, Amendment B1): hair, beard, eyes and skin are given by their own pickers, not by copy boxes here
                 if (const auto* own_btn = ui.find("His own head##faces")) {
                     CHECK(ui.find("Hair", own_btn->window) == nullptr && ui.find("Beard", own_btn->window) == nullptr && ui.find("Eyes", own_btn->window) == nullptr &&
                               ui.find("Skin", own_btn->window) == nullptr,
                           "ONE PICKER: the player's real-face chooser has no Hair / Beard / Eyes / Skin copy boxes (their pickers are in Appearance)");
                 }
                 ui.click("Close##faces");
                 ui.frames(2);
                 while (app.undo_count(3001) > undo0) app.undo(3001);
                 fs::remove(mods);
                 app.head_miniface_kept.erase(3001);
                 app.capture = keep_capture;
                 faces_safety_reset();
             });

    run_case("UI 2.20 faces safety: a career Turbo has not identified, or an unreadable heads_own.json: the head give is refused, nothing written",
             [&] {
                 reset(app, ui);
                 const std::vector<std::string> own = head_fields_of(app, 3001);
                 std::string msg;
                 {
                     InCareer in(app, ui, "");
                     CHECK(app.bridge.state().in_cm && app.career_faces_wanted().empty(), "in a career Turbo has not identified");
                     CHECK(!apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg) && msg.find("not identified") != std::string::npos,
                           "refused even when no picture would be written: " + msg);
                     CHECK(head_fields_of(app, 3001) == own && !fs::exists(app.turbo_output() / "careers"), "nothing written anywhere");
                 }
                 InCareer in(app, ui, A);
                 const cf::Paths p = app.career_faces_paths();
                 put_file(cf::heads_own_file(p, A), "{broken");
                 CHECK(!apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg) && msg.find("could not be kept") != std::string::npos,
                       "an unreadable heads_own.json: refused: " + msg);
                 CHECK(head_fields_of(app, 3001) == own && bytes_of(cf::heads_own_file(p, A)) == "{broken", "nothing written, the file left as it is");
                 CHECK(!has_own_head(app, false, 3001), "nothing to put back");
                 fs::remove(cf::heads_own_file(p, A));
             });

    run_case("UI 2.20 faces safety: no miniface in a career -> a render of his new 3D head is queued, written as the career's copy only, "
             "and joins the give's ONE undo step; a failed render copies the head owner's picture",
             [&] {
                 reset(app, ui);
                 faces3d_stop();
                 faces_safety_reset();
                 InCareer in(app, ui, A);
                 NoGamePicture none(app, {rel});
                 fs::remove(mods);
                 auto keep_capture = app.capture;
                 auto fake = std::make_shared<FakeCapture>();
                 fake->st.installed = true;
                 fake->st.available = true;
                 fake->st.reason = "ready";
                 fake->st.max_batch = 6;
                 app.capture = fake;
                 const cf::Paths p = app.career_faces_paths();
                 const std::vector<std::string> own = head_fields_of(app, 3001);
                 const size_t undo0 = app.undo_count(3001);
                 std::string msg;
                 bool in_step = false;
                 CHECK(apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg, &in_step), "head of 1004 given: " + msg);
                 CHECK(msg.find("one is being made from his new 3D head") != std::string::npos && faces_safety_pending() == 1, "queued: " + msg);
                 CHECK(log_has(app, "faces safety: player 3001", "a render of his new 3D head is queued for career " + A), "logged");
                 CHECK(!fs::exists(mods) && !cf::has_overlay(p, A, rel), "nothing written before the render");
                 for (int k = 0; k < 150 && fake->requests.empty(); ++k) ui.frames(1);
                 CHECK(fake->requests.size() == 1, fmt("one render asked (%zu)", fake->requests.size()));
                 if (fake->requests.empty()) {
                     app.capture = keep_capture;
                     return;
                 }
                 const capture::Request& r = fake->requests.back();
                 CHECK(r.id == 3001 && !r.manager && r.camera == 0 && !r.use_template, "his own descriptor, portrait camera (the chooser's route)");
                 fake->next = capture::Result();
                 fake->next.ok = true;
                 fake->next.image = solid(300, 300, 0x11, 0x22, 0x33);
                 fake->deliver = true;
                 ui.frames(3);
                 CHECK(faces_safety_pending() == 0, "done");
                 CHECK(cf::has_overlay(p, A, rel) && fs::exists(mods) && bytes_of(mods) == bytes_of(cf::overlay_file(p, A, rel)),
                       "written as career A's own copy (in mods\\legacy only while A is loaded)");
                 CHECK(log_has(app, "faces safety: player 3001", "miniface rendered from his new 3D head for career " + A + " only"), "logged");
                 CHECK(log_has(app, "career faces: data/ui/imgAssets/heads/p3001.dds written for career " + A + " only"), "the career faces line too");
                 cf::OwnHead oh;
                 CHECK(cf::find_own_head(p, A, false, 3001, oh) && oh.picture, "heads_own.json notes the picture made for him");
                 CHECK(app.undo_count(3001) == undo0 + 1, "still ONE undo step (the picture joined the give)");
                 CHECK(app.undo(3001) && head_fields_of(app, 3001) == own, "one Undo: his head back");
                 CHECK(!cf::has_overlay(p, A, rel) && !fs::exists(mods), "and the rendered picture gone");

                 // the game cannot render (two failures): the head owner's picture is copied, for the career only
                 CHECK(apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg, &in_step), "again: " + msg);
                 for (int attempt = 0; attempt < 2; ++attempt) {
                     const size_t asked = fake->requests.size();
                     for (int k = 0; k < 150 && fake->requests.size() == asked; ++k) ui.frames(1);
                     CHECK(fake->requests.size() == asked + 1, fmt("render %d asked", attempt + 1));
                     fake->next = capture::Result();
                     fake->next.ok = false;
                     fake->next.error = "fake: no player in the scene";
                     fake->deliver = true;
                     ui.frames(2);
                 }
                 for (int k = 0; k < 60 && faces_safety_pending() > 0; ++k) ui.frames(1);
                 CHECK(faces_safety_pending() == 0 && cf::has_overlay(p, A, rel), "the owner's picture copied as career A's copy");
                 CHECK(log_has(app, "faces safety: player 3001", "miniface copied from p1004.dds"), "logged");
                 CHECK(app.undo(3001) && !cf::has_overlay(p, A, rel) && head_fields_of(app, 3001) == own, "one Undo puts both back");
                 // a head changed again before the render: the job is dropped (never a picture of the wrong head)
                 CHECK(apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg, &in_step) && faces_safety_pending() == 1, "queued again");
                 CHECK(app.undo(3001), "undone before the render");
                 for (int k = 0; k < 150 && faces_safety_pending() > 0; ++k) ui.frames(1);
                 CHECK(faces_safety_pending() == 0 && !cf::has_overlay(p, A, rel), "dropped, nothing written");
                 CHECK(log_has(app, "faces safety: player 3001", "his head changed since the give"), "said why");
                 app.capture = keep_capture;
                 faces_safety_reset();
                 ui.frames(2);
             });

    run_case("UI 2.20 linked fields: the head of player X given to player Y writes every field that goes with it (class, quality, type, "
             "variation, skin tone); His own head puts every one of them back",
             [&] {
                 reset(app, ui);
                 InCareer in(app, ui, A);
                 auto keep_capture = app.capture;
                 app.capture.reset();
                 const Table* pt = app.db.table("players");
                 const PlayerRow* y = app.model.player(3001);
                 const PlayerRow* x = app.model.player(1004);
                 CHECK(pt && y && x, "players 3001 (Y) and 1004 (X)");
                 if (!pt || !y || !x) return;
                 static const char* kLinked[] = {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation", "skintonecode"};
                 auto get = [&](const PlayerRow* r, const char* f) { return app.db.get_int(*pt, r->rec, f, -1); };
                 auto set = [&](const PlayerRow* r, const char* f, int64_t v) {
                     const Field* fd = pt->field(f);
                     return fd && app.edit_field(*pt, r->rec, *fd, Value::of_int(v));
                 };
                 const size_t undo_y = app.undo_count(3001), undo_x = app.undo_count(1004);
                 // X and Y differ on every linked field (else a read-back proves nothing)
                 CHECK(set(y, "headclasscode", 1), "Y: a generic head (class 1)");
                 if (get(x, "hashighqualityhead") == get(y, "hashighqualityhead")) CHECK(set(x, "hashighqualityhead", 1 - get(y, "hashighqualityhead")), "X quality");
                 if (get(x, "headtypecode") == get(y, "headtypecode")) CHECK(set(x, "headtypecode", get(y, "headtypecode") == 0 ? 1 : 0), "X head type");
                 if (get(x, "headvariation") == get(y, "headvariation")) CHECK(set(x, "headvariation", get(y, "headvariation") == 0 ? 1 : 0), "X variation");
                 if (get(x, "skintonecode") == get(y, "skintonecode"))
                     CHECK(set(x, "skintonecode", get(y, "skintonecode") <= 90 ? get(y, "skintonecode") + 10 : 10), "X skin tone");
                 std::map<std::string, int64_t> xs, ys;
                 for (const char* f : kLinked) {
                     CHECK(pt->has(f), std::string("players has ") + f);
                     xs[f] = get(x, f);
                     ys[f] = get(y, f);
                     CHECK(xs[f] != ys[f], std::string("X and Y differ on ") + f + ": " + std::to_string(xs[f]) + " / " + std::to_string(ys[f]));
                 }
                 std::string msg;
                 CHECK(apply_real_face(app, 3001, 1004, RealFaceOptions{}, &msg, nullptr), "head of X given to Y (no option ticked): " + msg);
                 for (const char* f : kLinked)
                     CHECK(get(y, f) == xs[f], std::string("read back ") + f + " = X's " + std::to_string(xs[f]) + " (is " + std::to_string(get(y, f)) + ")");
                 CHECK(head_link_offer(app, 3001) == 0, "nothing left to link");
                 CHECK(restore_own_head(app, false, 3001, &msg), "His own head: " + msg);
                 for (const char* f : kLinked)
                     CHECK(get(y, f) == ys[f], std::string("His own head: ") + f + " = his own " + std::to_string(ys[f]) + " (is " + std::to_string(get(y, f)) + ")");
                 while (app.undo_count(3001) > undo_y) app.undo(3001);
                 while (app.undo_count(1004) > undo_x) app.undo(1004);
                 app.head_miniface_kept.erase(3001);
                 app.capture = keep_capture;
                 faces_safety_reset();
             });

    run_case("UI 2.20 linked fields: a head id typed alone shows one line and 'Apply the linked fields' (same safe give: his real miniface "
             "kept, his own head from before the typed id); a head given through a game head writes its gate set",
             [&] {
                 reset(app, ui);
                 InCareer in(app, ui, A);
                 auto keep_capture = app.capture;
                 app.capture.reset();
                 const Table* pt = app.db.table("players");
                 const PlayerRow* y = app.model.player(3001);
                 const PlayerRow* x = app.model.player(1004);
                 if (!pt || !y || !x) return;
                 auto get = [&](const PlayerRow* r, const char* f) { return app.db.get_int(*pt, r->rec, f, -1); };
                 auto set = [&](const PlayerRow* r, const char* f, int64_t v) {
                     const Field* fd = pt->field(f);
                     return fd && app.edit_field(*pt, r->rec, *fd, Value::of_int(v));
                 };
                 // his edit history starts here (the cases before left steps of their own; his own head comes from this one's)
                 const auto history_was = app.undo_[3001];
                 app.undo_[3001].clear();
                 const size_t undo_y = app.undo_count(3001), undo_x = app.undo_count(1004);
                 const int64_t h0 = get(y, "headassetid"), c0 = get(y, "headclasscode");
                 if (get(x, "skintonecode") == get(y, "skintonecode"))
                     CHECK(set(x, "skintonecode", get(y, "skintonecode") <= 90 ? get(y, "skintonecode") + 10 : 10), "X skin tone differs");
                 const std::string real = [] {
                     const std::vector<uint8_t> b = encode_dds_dxt5(solid(256, 256, 0x60, 0x20, 0x20));
                     return std::string(b.begin(), b.end());
                 }();
                 put_file(mods, real);  // his real miniface (a file Turbo did not make)
                 CHECK(set(y, "headclasscode", 0) && set(y, "headassetid", 1004), "typed: class 0, head 1004 alone");
                 CHECK(head_link_offer(app, 3001) == 1004, "the linked fields of 1004's head are offered");
                 app.request_tab = 0;
                 ui.frames(2);
                 CHECK(ui.click("3001", "##plist"), "player 3001");
                 ui.frames(2);
                 CHECK(ui.find("Apply the linked fields##hl") != nullptr && ui.find("Keep as it is##hl") != nullptr, "the line with its buttons");
                 CHECK(ui.click("Apply the linked fields##hl"), "apply");
                 ui.frames(2);
                 for (const char* f : {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation", "skintonecode"})
                     CHECK(get(y, f) == get(x, f), std::string("linked: ") + f);
                 CHECK(head_link_offer(app, 3001) == 0 && ui.find("Apply the linked fields##hl") == nullptr, "the line is gone");
                 CHECK(bytes_of(mods) == real, "his real miniface p3001.dds byte for byte the same (never the donor's picture)");
                 CHECK(!cf::has_overlay(app.career_faces_paths(), A, rel), "no career copy over it");
                 CHECK(log_has(app, "linked fields: player 3001", "head 1004"), "logged");
                 cf::OwnHead oh;
                 const bool found = cf::find_own_head(app.career_faces_paths(), A, false, 3001, oh);
                 const bool before_typed = found && oh.fields["headassetid"] == h0 && oh.fields["headclasscode"] == c0;
                 CHECK(before_typed, "his own head kept is the one BEFORE the typed id (found " + std::to_string(found) + ", head " +
                                         std::to_string(oh.fields["headassetid"]) + " class " + std::to_string(oh.fields["headclasscode"]) +
                                         "; before the typing: head " + std::to_string(h0) + " class " + std::to_string(c0) + ")");
                 std::string msg;
                 const bool back = restore_own_head(app, false, 3001, &msg);
                 CHECK(back && get(y, "headassetid") == h0 && get(y, "headclasscode") == c0,
                       "His own head brings back his head, not the typed one: " + msg + " (head " + std::to_string(get(y, "headassetid")) + ")");
                 // "Keep as it is" hides the line for that head
                 CHECK(set(y, "headclasscode", 0) && set(y, "headassetid", 1004) && head_link_offer(app, 3001) == 1004, "typed again");
                 ui.frames(2);
                 CHECK(ui.click("Keep as it is##hl"), "keep");
                 ui.frames(1);
                 CHECK(head_link_offer(app, 3001) == 0, "kept as it is: no line");
                 app.head_miniface_kept.erase(3001);
                 while (app.undo_count(3001) > undo_y) app.undo(3001);
                 while (app.undo_count(1004) > undo_x) app.undo(1004);
                 CHECK(get(y, "headassetid") == h0 && get(y, "headclasscode") == c0, "all undone");
                 app.undo_[3001] = history_was;
                 fs::remove(mods);
                 app.capture = keep_capture;
                 faces_safety_reset();
             });
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
        app.log_lines.clear();  // the lines of the switch only
        app.reapply.career = B;
        ui.frames(2);
        CHECK(!fs::exists(mods), "career B: A's picture is out of mods\\legacy");
        {
            // the switch may pass through a frame with no identified career (the new identity is read first): A's picture is put
            // back in whichever career faces line came first, and career B is identified (paths matched in any letter case)
            bool back = false, b_line = false;
            const std::string want = lower_ascii("; back " + rel);
            for (const auto& l : app.log_lines) {
                back = back || (l.rfind("career faces: ", 0) == 0 && lower_ascii(l).find(want) != std::string::npos);
                b_line = b_line || l.find("career faces: career identified: career " + B) != std::string::npos;
            }
            std::string lines;
            for (const auto& l : app.log_lines) lines += l + " | ";
            CHECK(back && b_line, "log lines for B: " + lines.substr(0, 600));
        }
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
        NoGamePicture none(app, {rel});  // 2.20: he has no miniface (one he had would be kept: ui_cases_safety)
        auto keep_capture = app.capture;
        app.capture.reset();  // no capture service: the owner's picture is copied (the render route: ui_cases_safety)
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
        app.capture = keep_capture;
        ui.frames(2);
    });
    ui_cases_safety(app, ui, le);  // 2.20
}

}  // namespace career_faces_test
