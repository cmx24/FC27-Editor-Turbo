// Native tests (X 2.0.2 club identity scope): a club NAME / CREST / KIT edit made with no career loaded is GLOBAL (every save, like the
// squads file); one made in a career lives in that career only (Cassio 2026-11-01: "the Italian team names, crests and kits are global,
// every other combo (name+crest+kit) is save-specific").
//   core: crest / minikit paths on the career route, a career crest copied in only for its career over a global one and the global one
//         put back byte for byte, move_to_career / move_to_global, careers_with / career_files, the 3D kit scope rule
//   app:  identity_scope (global / this career / refused), a crest written in a career is that career's only, with no career it is
//         global, a team name saved with no career is global (not "not assigned"), move_club_identity both ways (name out of
//         custom_team_names.csv for a career), Serie A / B are the Italian leagues
// Included by test_main.cpp: club_scope_test::run() from main, club_scope_test::ui_cases() from the UI world.
#pragma once
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/career_faces.h"
#include "core/kit_swap.h"

namespace club_scope_test {

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
    const std::string C1 = "data/ui/imgAssets/crest/dark/l13.dds", C2 = "data/ui/imgAssets/crest16x16/light/l13.dds",
                      K1 = "data/ui/imgAssets/kits/j0_13_0.dds", CI = "data/ui/imgAssets/crest/light/l110321.dds";

    run_case("X 2.0.2 club scope: crest and minikit paths ride the career route; anything else does not", [&] {
        CHECK(cf::is_crest_path(C1) && cf::is_crest_path(C2) && cf::is_crest_path("data/ui/imgAssets/crest1024x1024/custom/l5.dds") &&
                  cf::is_crest_path("DATA\\UI\\imgAssets\\CREST\\Light\\L13.DDS"),
              "crest, the sized crest folders, light / dark / custom, any case or slash");
        CHECK(!cf::is_crest_path("data/ui/imgAssets/crest/dark/x13.dds") && !cf::is_crest_path("data/ui/imgAssets/crest/other/l13.dds") &&
                  !cf::is_crest_path("data/ui/imgAssets/crest/dark/sub/l13.dds") && !cf::is_crest_path("data/ui/imgAssets/crest/dark/l13.png") &&
                  !cf::is_crest_path("data/ui/imgAssets/crestfoo/dark/l13.dds") && !cf::is_crest_path("data/ui/imgAssets/crest/dark/l.dds") &&
                  !cf::is_crest_path("data/ui/imgAssets/crest/dark/l1a.dds"),
              "another name, folder, depth, type or size folder: not a crest");
        CHECK(cf::is_minikit_path(K1) && !cf::is_minikit_path("data/ui/imgAssets/kits/sub/j0_13_0.dds") &&
                  !cf::is_minikit_path("data/ui/imgAssets/kits/j0_13_0.png"),
              "minikits: one level, .dds");
        CHECK(cf::is_career_file(C1) && cf::is_career_file(K1) && cf::is_career_file("data/ui/imgAssets/heads/p1.dds") &&
                  !cf::is_career_file("data/ui/imgAssets/adboards/a.dds"),
              "the career route: faces, crests, minikits");
    });

    run_case("X 2.0.2 club scope: a career's crest + minikit copied in for its career only, the global crest put back byte for byte", [&] {
        const fs::path root = g_out / "club_scope_core";
        fs::remove_all(root);
        const cf::Paths p{root / "turbo_output", root / "mods" / "legacy"};
        CHECK(cf::overlay_file(p, A, C1) == p.turbo_output / "careers" / A / "crest" / "dark" / "l13.dds", "overlay: careers\\<id>\\crest\\dark\\l13.dds");
        CHECK(cf::overlay_file(p, A, K1) == p.turbo_output / "careers" / A / "kits" / "j0_13_0.dds", "overlay: careers\\<id>\\kits\\...");
        put_file(cf::mods_file(p, C1), "GLOBAL-CREST");  // a global crest of the club (made with no career)
        std::string err;
        std::filesystem::path undo;
        CHECK(cf::save_overlay(p, A, C1, vec("A-CREST"), &err, &undo) && cf::save_overlay(p, A, C2, vec("A-C16"), &err, &undo) &&
                  cf::save_overlay(p, A, K1, vec("A-KIT"), &err, &undo),
              "career A keeps its own crest (two sizes) and minikit: " + err);
        cf::SyncResult r = cf::sync(p, A);
        CHECK(r.ok() && r.copied.size() == 3, "A loaded: 3 copied in (" + r.summary() + ")");
        CHECK(bytes_of(cf::mods_file(p, C1)) == "A-CREST" && bytes_of(cf::mods_file(p, C2)) == "A-C16" && bytes_of(cf::mods_file(p, K1)) == "A-KIT",
              "mods\\legacy holds A's files");
        r = cf::sync(p, B);
        CHECK(r.ok() && bytes_of(cf::mods_file(p, C1)) == "GLOBAL-CREST" && !fs::exists(cf::mods_file(p, C2)) && !fs::exists(cf::mods_file(p, K1)),
              "career B: the global crest back byte for byte, A's other files gone (" + r.summary() + ")");
        r = cf::sync(p, "");
        CHECK(r.ok() && bytes_of(cf::mods_file(p, C1)) == "GLOBAL-CREST", "no career (main menu / squads file): the global crest");
        CHECK(cf::careers_with(p, C1) == std::vector<std::string>{A} && cf::career_files(p, A).size() == 3 && cf::career_files(p, B).empty(),
              "careers_with / career_files");
        // the career's undo folder and heads_own.json never count as its files
        put_file(p.turbo_output / "careers" / A / "_undo" / "l13.dds", "OLD");
        put_file(p.turbo_output / "careers" / A / "heads_own.json", "{}");
        CHECK(cf::career_files(p, A).size() == 3, "_undo and heads_own.json are not career files");
    });

    run_case("X 2.0.2 club scope: move_to_career / move_to_global carry a club's files between global and one career", [&] {
        const fs::path root = g_out / "club_scope_move";
        fs::remove_all(root);
        const cf::Paths p{root / "turbo_output", root / "mods" / "legacy"};
        std::string err;
        put_file(cf::mods_file(p, C1), "OLD-UNTRACKED");  // a crest from before 2.0.2 (no record of Turbo's)
        put_file(cf::mods_file(p, CI), "ITALIAN");        // an Italian club's crest: stays global
        CHECK(cf::move_to_career(p, A, C1, &err), "the untracked global crest becomes career A's: " + err);
        CHECK(!fs::exists(cf::mods_file(p, C1)) && bytes_of(cf::overlay_file(p, A, C1)) == "OLD-UNTRACKED", "out of mods\\legacy, into careers\\A");
        bool kept = false;
        for (fs::recursive_directory_iterator it(p.turbo_output / "careers" / "_moved"), end; it != end; ++it)
            kept = kept || (it->is_regular_file() && bytes_of(it->path()) == "OLD-UNTRACKED");
        CHECK(kept, "a copy of the global file kept in careers\\_moved");
        CHECK(bytes_of(cf::mods_file(p, CI)) == "ITALIAN", "another club's global crest untouched");
        CHECK(!cf::move_to_career(p, A, C1, &err) && err.find("no global") != std::string::npos, "nothing global left: refused (" + err + ")");
        // A loaded: its crest shows; main menu: none
        cf::sync(p, A);
        CHECK(bytes_of(cf::mods_file(p, C1)) == "OLD-UNTRACKED", "career A shows it");
        CHECK(!cf::move_to_global(p, A, C1, &err) && err.find("sync first") != std::string::npos, "never while a career's files are in (" + err + ")");
        cf::sync(p, "");
        CHECK(!fs::exists(cf::mods_file(p, C1)), "main menu: no crest (the game's own)");
        // back to global
        CHECK(cf::move_to_global(p, A, C1, &err), "career A's crest becomes global: " + err);
        CHECK(bytes_of(cf::mods_file(p, C1)) == "OLD-UNTRACKED" && !cf::has_overlay(p, A, C1), "in mods\\legacy, A has no own copy");
        CHECK(cf::owner_of(p, C1).owner == cf::Owner::TurboGlobal, "recorded as Turbo's global copy");
        cf::SyncResult r = cf::sync(p, B);
        CHECK(r.ok() && bytes_of(cf::mods_file(p, C1)) == "OLD-UNTRACKED", "every career sees the global crest");
        cf::sync(p, "");
        // a career that already has its own: the global one stays, nothing overwritten
        put_file(cf::overlay_file(p, B, C1), "B-OWN");
        CHECK(!cf::move_to_career(p, B, C1, &err) && bytes_of(cf::overlay_file(p, B, C1)) == "B-OWN" && bytes_of(cf::mods_file(p, C1)) == "OLD-UNTRACKED",
              "B's own wins, the global stays: " + err);
        CHECK(!cf::move_to_global(p, A, C1, &err), "A has no copy: refused");
    });

    run_case("X 2.0.2 club scope: a 3D kit is in the table for its career only (global / an old file: every career)", [&] {
        using turbo::kitswap::slot_in_effect;
        CHECK(slot_in_effect("", "") && slot_in_effect("", A) && slot_in_effect("global", "") && slot_in_effect("global", B),
              "global (and a file written before X 2.0.2): everywhere, the main menu too");
        CHECK(slot_in_effect("career:" + A, A) && !slot_in_effect("career:" + A, B) && !slot_in_effect("career:" + A, ""),
              "a career's kit: only while that career is identified");
        CHECK(!slot_in_effect("weird", A) && !slot_in_effect("career:", ""), "an unknown scope: never");
    });
}

// ---- the App side (the UI world is an identified career)
inline bool log_has_any(App& app, const std::string& a) {
    for (const auto& l : app.log_lines)
        if (l.find(a) != std::string::npos) return true;
    return false;
}

inline void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("UI X 2.0.2 club scope: identity_scope, a crest written in a career is that career's only, with no career it is global; move both ways",
             [&] {
        const std::string A = "aaaaaaaaaaaaaaa1", B = "bbbbbbbbbbbbbbb2";
        app.career_faces_sync("", "test reset");
        std::error_code ec;
        fs::remove_all(app.turbo_output() / "careers", ec);
        const std::string was = app.reapply.career;
        struct Back {
            App& a;
            std::string was;
            Ui& u;
            ~Back() {
                a.reapply.career = was;
                a.bridge.test_set_in_cm(true);
                u.frames(2);
            }
        } back{app, was, ui};
        const int64_t team = 1;  // a club of the UI world
        const std::string rel = "data/ui/imgAssets/crest/dark/l" + std::to_string(team) + ".dds";
        const fs::path mods = le / "mods" / "legacy" / fs::u8path(rel);
        fs::remove(mods, ec);
        std::string err, where, refuse;

        // a career Turbo has not identified: refused
        app.reapply.career.clear();
        ui.frames(2);
        CHECK(app.identity_scope(&refuse).empty() && refuse.find("not identified") != std::string::npos, "unidentified career: refused (" + refuse + ")");
        CHECK(!app.write_identity_file(rel, vec("X"), &err) && !fs::exists(mods), "nothing written: " + err);

        // career A
        app.reapply.career = A;
        ui.frames(2);
        CHECK(app.identity_scope() == "career:" + A, "career A: " + app.identity_scope());
        CHECK(app.write_identity_file(rel, vec("A-CREST"), &err, &where) && where == "this career", "written for A: " + err);
        const cf::Paths p = app.career_faces_paths();
        CHECK(bytes_of(cf::overlay_file(p, A, rel)) == "A-CREST" && bytes_of(mods) == "A-CREST", "A's copy, in mods\\legacy while A is loaded");
        // a global crest is never removed from inside a career; A's own is
        bool removed = false;
        CHECK(app.remove_identity_file(rel, &err, &removed) && removed && !fs::exists(mods) && !cf::has_overlay(p, A, rel), "A's copy removed: " + err);
        CHECK(app.write_identity_file(rel, vec("A-CREST"), &err), "written again: " + err);

        // career B: not there
        app.reapply.career = B;
        ui.frames(2);
        CHECK(!fs::exists(mods), "career B does not see A's crest");

        // no career (main menu): global
        app.bridge.test_set_in_cm(false);
        ui.frames(2);
        CHECK(app.identity_scope() == "global", "main menu: global");
        CHECK(!fs::exists(mods), "main menu does not see A's crest");
        CHECK(app.write_identity_file(rel, vec("GLOBAL"), &err, &where) && where == "global" && bytes_of(mods) == "GLOBAL", "a global crest: " + err);
        CHECK(cf::owner_of(p, rel).owner == cf::Owner::TurboGlobal, "recorded as Turbo's global copy");
        app.bridge.test_set_in_cm(true);
        app.reapply.career = B;
        ui.frames(2);
        CHECK(bytes_of(mods) == "GLOBAL", "career B sees the global crest");
        CHECK(!app.remove_identity_file(rel, &err) && err.find("GLOBAL") != std::string::npos && bytes_of(mods) == "GLOBAL",
              "a global crest is never removed from a career: " + err);
        app.reapply.career = A;
        ui.frames(2);
        CHECK(bytes_of(mods) == "A-CREST", "career A: its own over the global one");
        app.reapply.career = B;
        ui.frames(2);
        CHECK(bytes_of(mods) == "GLOBAL", "B again: the global one, byte for byte");

        // move: the club to career B (the global crest becomes B's), then global again
        std::string msg;
        CHECK(app.move_club_identity(team, "career:" + B, &msg), "moved to B: " + msg);
        CHECK(cf::has_overlay(p, B, rel) && bytes_of(mods) == "GLOBAL", "B's copy now, shown in B: " + msg);
        app.bridge.test_set_in_cm(false);
        ui.frames(2);
        CHECK(!fs::exists(mods), "main menu: no global crest any more");
        app.bridge.test_set_in_cm(true);
        app.reapply.career = B;
        ui.frames(2);
        CHECK(app.move_club_identity(team, "global", &msg), "back to global: " + msg);
        app.bridge.test_set_in_cm(false);
        ui.frames(2);
        CHECK(bytes_of(mods) == "GLOBAL" && !cf::has_overlay(p, B, rel), "global again: " + msg);
        CHECK(log_has_any(app, "club scope: "), "logged");
        app.bridge.test_set_in_cm(true);
        fs::remove(mods, ec);
        app.career_faces_sync("", "test end");
        fs::remove_all(app.turbo_output() / "careers", ec);
    });

    run_case("UI X 2.0.2 club scope: the career a club's edits belong to (the one holding them, else the one whose club it is)", [&] {
        const ReapplyStore saved = app.reapply;
        const std::string A = "aaaaaaaaaaaaaaa1", B = "bbbbbbbbbbbbbbb2";
        struct Back {
            App& a;
            ReapplyStore r;
            ~Back() {
                a.reapply = r;
                a.team_names.forget(1, "career:aaaaaaaaaaaaaaa1");
            }
        } back{app, saved};
        app.reapply.careers[A].label = "Coach A, " + app.model.team_name(1);
        app.reapply.careers[B].label = "Coach B, " + app.model.team_name(7);
        tnames::Entry e;
        e.teamid = 1;
        e.text[tnames::Full] = "Owner Test";
        e.scope = "career:" + A;
        app.team_names.upsert(e);
        CHECK(app.owner_career_of(1) == A, "one career holds the club's name: " + app.owner_career_of(1));
        app.reapply.set_kit_field_in("career:" + A, 7, 0, -1, "x", "now", "teamcolorprimr", 1);
        app.reapply.set_kit_field_in("career:" + B, 7, 0, -1, "x", "now", "teamcolorprimr", 2);
        CHECK(app.owner_career_of(7) == B, "two careers hold kit rows: the one whose own club it is (" + app.owner_career_of(7) + ")");
        std::string why;
        CHECK(app.owner_career_of(424242, &why).empty() && !why.empty(), "no career holds anything: none (" + why + ")");
    });

    run_case("UI X 2.0.2 club scope: a team name saved with no career is GLOBAL; in a career it is that career's; Serie A / B are Italian", [&] {
        CHECK(is_italian_club_league(31) && is_italian_club_league(32) && !is_italian_club_league(13) && !is_italian_club_league(-1), "Serie A 31, Serie B 32");
        const std::string was = app.reapply.career;
        struct Back {
            App& a;
            std::string was;
            Ui& u;
            ~Back() {
                a.reapply.career = was;
                a.bridge.test_set_in_cm(true);
                u.frames(2);
            }
        } back{app, was, ui};
        const int64_t team = 1;
        // the database name and Live Editor's file go back at the end (other cases read Arsenal)
        const Table* tt = app.db.table("teams");
        const uint64_t trec = tt ? app.db.find(*tt, "teamid", team) : 0;
        Value orig;
        CHECK(tt && tt->field("teamname") && app.db.get(*tt, trec, *tt->field("teamname"), orig), "Arsenal's stored name read");
        const fs::path csvf = team_names_file(le);
        const std::string csv_before = bytes_of(csvf);
        struct Restore {
            App& a;
            const Table* t;
            uint64_t rec;
            Value v;
            fs::path csv;
            std::string before;
            ~Restore() {
                if (t && t->field("teamname")) a.db.set(*t, rec, *t->field("teamname"), v, nullptr);
                a.model.refresh_team(1);
                if (before == "<absent>") {
                    std::error_code ec;
                    fs::remove(csv, ec);
                } else {
                    put_file(csv, before);
                }
            }
        } restore{app, tt, trec, orig, csvf, csv_before};
        app.bridge.test_set_in_cm(false);
        ui.frames(2);
        TeamNameSave r = save_team_name(app, team, "Scope Test FC", "", "", "");
        const tnames::Entry* g = app.team_names.find(team, "global");
        CHECK(r.ok && g && g->text[tnames::Full] == "Scope Test FC", "main menu: saved as global (" + r.line + ")");
        CHECK(app.team_names.find(team, "") == nullptr, "never 'not assigned' any more");
        app.bridge.test_set_in_cm(true);
        app.reapply.career = "ccccccccccccccc3";
        ui.frames(2);
        r = save_team_name(app, team, "Career Only FC", "", "", "");
        const tnames::Entry* c = app.team_names.find(team, "career:ccccccccccccccc3");
        CHECK(r.ok && c && c->text[tnames::Full] == "Career Only FC" && app.team_names.find(team, "global"), "in a career: that career's; global kept");
        app.team_names.forget(team, "global");
        app.team_names.forget(team, "career:ccccccccccccccc3");
        app.save_team_names(nullptr);
        app.team_names_publish();
    });
}

}  // namespace club_scope_test
