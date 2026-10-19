// Native tests (2.16 faces/live-hair, gap 1 + 4): a Turbo 3D hair / beard (band codes, core/faces3d.h) belongs to the head it
// was chosen for, so every way the real-face chooser gives a NEW head leaves the band: another player's head (copy_head through
// apply_real_face), a head given to a manager (apply_real_face_to_manager), a game head (apply_game_head, players and managers).
// A band code left on a new head would keep a Turbo look the user never chose for it, and without Turbo a band code on a head
// whose scan is missing shows no hair or beard at all (FACES_LIVE_HAIR.md, "Inferred, not proven").
// Pictures: the heads' minifaces are made "the game has none" for the duration (cache file moved away + missing.txt), so the
// head fields are written with no picture copied. Everything is put back at the end. Included by test_main.cpp.
#pragma once
#include <fstream>
#include <memory>

#include "core/asset_catalog.h"
#include "core/faces3d.h"
#include "core/legacy.h"
#include "ui/ui_faces.h"
#include "ui/ui_images.h"

static void test_band_new_head_ui(App& app, Ui& ui, const fs::path& le) {
    (void)le;  // the pictures live under app.legacy.cache_dir()
    run_case("2.16 band codes: a new head (another player's, given to a manager, a game head) never keeps a Turbo 3D hair / beard", [&] {
        const Table* t = app.db.table("players");
        const Table* mt = app.db.table("manager");
        CHECK(t && mt, "players and manager tables");
        if (!t || !mt) return;
        const uint64_t rec = app.db.find(*t, "playerid", 3001);
        const uint64_t orec = app.db.find(*t, "playerid", 1003);
        const uint64_t mrec = app.db.find(*mt, "managerid", 502);
        CHECK(rec && orec && mrec, "player 3001, head owner 1003, manager 502");
        CHECK(mt->has("hairtypecode") && mt->has("facialhairtypecode") && mt->has("headassetid"), "manager table has the look fields");
        if (!rec || !orec || !mrec || !mt->has("hairtypecode")) return;

        const int64_t H = faces3d::hair_band_code(155), B = faces3d::beard_band_code(46);
        CHECK(H > 0 && B > 0 && faces3d::hair_from_code(H) == 155 && faces3d::beard_from_code(B) == 46, "band codes of hair 155 / beard 46");

        // what is put back at the end
        static const char* kKeep[] = {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation",
                                      "hairtypecode", "facialhairtypecode", "haircolorcode", "hairstylecode", "facialhaircolorcode"};
        std::vector<std::pair<std::string, int64_t>> keep_p, keep_o, keep_m;
        for (const char* f : kKeep) {
            if (t->has(f)) keep_p.emplace_back(f, app.db.get_int(*t, rec, f, 0)), keep_o.emplace_back(f, app.db.get_int(*t, orec, f, 0));
            if (mt->has(f)) keep_m.emplace_back(f, app.db.get_int(*mt, mrec, f, 0));
        }
        auto read_bytes = [](const fs::path& p, bool& had) {
            std::ifstream f(p, std::ios::binary);
            had = static_cast<bool>(f);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        };
        auto put_bytes = [](const fs::path& p, const std::string& b) {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f.write(b.data(), static_cast<std::streamsize>(b.size()));
        };
        const fs::path backup_file = app.turbo_output() / fs::u8path(faces3d::kPreviewBackupRel);
        bool had_backup = false;
        const std::string keep_backup = read_bytes(backup_file, had_backup);

        // the pictures: "the game has none" (the head is written, no picture copied)
        const fs::path cache = app.legacy.cache_dir();  // turbo_output\cache\legacy
        const fs::path missing_txt = cache / "missing.txt";
        bool had_missing = false;
        const std::string keep_missing = read_bytes(missing_txt, had_missing);
        const std::vector<std::string> pics = {legacy_path::player_miniface(1003), legacy_path::player_miniface(90001),
                                               legacy_path::legend_picture(90001)};
        std::vector<std::pair<fs::path, std::string>> moved;
        for (const std::string& rel : pics) {
            const fs::path p = cache / fs::u8path(rel);
            bool had = false;
            const std::string b = read_bytes(p, had);
            if (had) moved.emplace_back(p, b), fs::remove(p);
        }
        {
            std::ofstream m(missing_txt, std::ios::binary | std::ios::app);
            for (const std::string& rel : pics) m << "\n" << rel << "\n";
        }
        app.legacy.refresh_missing();

        // the game heads of the synthetic catalog (tests/fixtures/asset_catalog_v1_small.json, build 0000AAAA-0000BBBB)
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file("tests/fixtures/asset_catalog_v1_small.json", "0000AAAA-0000BBBB", &cerr) && cat->current(),
              "catalog fixture loaded as current: " + cerr);
        set_faces_catalog(cat);

        auto set_p = [&](uint64_t r, int64_t h, int64_t b) {
            CHECK(app.db.set_int(*t, r, "hairtypecode", h) && app.db.set_int(*t, r, "facialhairtypecode", b), "player codes set");
        };
        auto set_m = [&](int64_t h, int64_t b) {
            CHECK(app.db.set_int(*mt, mrec, "hairtypecode", h) && app.db.set_int(*mt, mrec, "facialhairtypecode", b), "manager codes set");
        };
        auto p_is = [&](uint64_t r, int64_t h, int64_t b) {
            return app.db.get_int(*t, r, "hairtypecode", -1) == h && app.db.get_int(*t, r, "facialhairtypecode", -1) == b;
        };
        auto m_is = [&](int64_t h, int64_t b) {
            return app.db.get_int(*mt, mrec, "hairtypecode", -1) == h && app.db.get_int(*mt, mrec, "facialhairtypecode", -1) == b;
        };
        auto p_codes = [&](uint64_t r) {
            return fmt("%lld / %lld", static_cast<long long>(app.db.get_int(*t, r, "hairtypecode", -1)),
                       static_cast<long long>(app.db.get_int(*t, r, "facialhairtypecode", -1)));
        };
        auto m_codes = [&]() {
            return fmt("%lld / %lld", static_cast<long long>(app.db.get_int(*mt, mrec, "hairtypecode", -1)),
                       static_cast<long long>(app.db.get_int(*mt, mrec, "facialhairtypecode", -1)));
        };
        const RealFaceOptions head_only{};  // hair, beard, eyes, skin not asked
        std::string msg;
        bool in_step = true;

        // 1. another player's head (copy_head): his band codes give way to the owner's own codes
        set_p(orec, 12, 30);
        set_p(rec, H, B);
        CHECK(apply_real_face(app, 3001, 1003, head_only, &msg, &in_step), "head of 1003 given to 3001: " + msg);
        CHECK(app.db.get_int(*t, rec, "headassetid", 0) == app.db.get_int(*t, orec, "headassetid", -1), "the head is his now");
        CHECK(p_is(rec, 12, 30), "band codes replaced by the owner's own hair 12 / beard 30 (got " + p_codes(rec) + ")");
        // 2.20: his picture follows the faces-safety rules (kept when real, made when he has none); only the band codes are this case's
        CHECK(msg.find("given to") != std::string::npos, "given: " + msg);
        CHECK(app.undo(3001) && p_is(rec, H, B), "one Undo puts the head and his band codes back (got " + p_codes(rec) + ")");
        // only the part with a band code is replaced; codes outside the bands stay his own when hair / beard are not asked
        set_p(rec, H, 7);
        CHECK(apply_real_face(app, 3001, 1003, head_only, &msg, &in_step), "again, band hair only: " + msg);
        CHECK(p_is(rec, 12, 7), "band hair replaced, his own beard 7 kept (got " + p_codes(rec) + ")");
        CHECK(app.undo(3001), "undo");
        set_p(rec, 21, 7);
        CHECK(apply_real_face(app, 3001, 1003, head_only, &msg, &in_step), "again, no band code: " + msg);
        CHECK(p_is(rec, 21, 7), "no band code: his own hair 21 / beard 7 untouched (got " + p_codes(rec) + ")");
        CHECK(app.undo(3001), "undo");

        // 2. a player's head given to a manager (copy_head into the manager table)
        set_m(H, B);
        CHECK(apply_real_face_to_manager(app, 502, 1003, false, head_only, &msg, &in_step), "head of 1003 given to manager 502: " + msg);
        CHECK(app.db.get_int(*mt, mrec, "headassetid", 0) == app.db.get_int(*t, orec, "headassetid", -1), "the manager has the head");
        CHECK(m_is(12, 30), "manager: band codes replaced by the owner's own 12 / 30 (got " + m_codes() + ")");

        // 3. a game head to a player: the band gives way to his own codes recorded before Turbo's first look
        //    (turbo_output\faces3d\preview_backup.json), else 0
        faces3d::PreviewBackup bk;
        faces3d::PreviewRecord r3001;
        r3001.name = "player 3001";
        r3001.original = {{"hairtypecode", 44}, {"facialhairtypecode", 2}};
        r3001.written = {{"hairtypecode", H}, {"facialhairtypecode", B}};
        bk[3001] = r3001;
        std::string berr;
        CHECK(faces3d::save_preview_backup(app.turbo_output().u8string(), bk, &berr), "backup written: " + berr);
        set_p(rec, H, B);
        CHECK(apply_game_head(app, 90001, 3001, false, head_only, &msg, &in_step), "game head 90001 given to 3001: " + msg);
        CHECK(app.db.get_int(*t, rec, "headassetid", 0) == 90001 && app.db.get_int(*t, rec, "headclasscode", -1) == 0, "game head 90001 written");
        CHECK(p_is(rec, 44, 2), "band codes replaced by his recorded own hair 44 / beard 2 (got " + p_codes(rec) + ")");
        CHECK(app.undo(3001) && p_is(rec, H, B), "one Undo puts the head and his band codes back (got " + p_codes(rec) + ")");
        fs::remove(backup_file);
        CHECK(apply_game_head(app, 90001, 3001, false, head_only, &msg, &in_step), "again without a recorded original: " + msg);
        CHECK(p_is(rec, 0, 0), "no recorded original: 0 / 0 (a real face ignores these codes) (got " + p_codes(rec) + ")");
        CHECK(app.undo(3001), "undo");
        set_p(rec, 21, B);
        CHECK(apply_game_head(app, 90001, 3001, false, head_only, &msg, &in_step), "again, band beard only: " + msg);
        CHECK(p_is(rec, 21, 0), "band beard left (0), his own hair 21 kept (got " + p_codes(rec) + ")");
        CHECK(app.undo(3001), "undo");
        set_p(rec, 21, 7);
        CHECK(apply_game_head(app, 90001, 3001, false, head_only, &msg, &in_step), "again, no band code: " + msg);
        CHECK(p_is(rec, 21, 7), "no band code: his own hair 21 / beard 7 untouched (got " + p_codes(rec) + ")");
        CHECK(app.undo(3001), "undo");

        // 4. a game head to a manager: the band gives way (no recorded originals for managers: 0)
        set_m(H, B);
        CHECK(apply_game_head(app, 90001, 502, true, head_only, &msg, &in_step), "game head 90001 given to manager 502: " + msg);
        CHECK(app.db.get_int(*mt, mrec, "headassetid", 0) == 90001, "manager: game head 90001 written");
        CHECK(m_is(0, 0), "manager: band codes left, 0 / 0 (got " + m_codes() + ")");
        set_m(33, 4);
        CHECK(apply_game_head(app, 90001, 502, true, head_only, &msg, &in_step), "manager again, no band code: " + msg);
        CHECK(m_is(33, 4), "manager: no band code, his own 33 / 4 untouched (got " + m_codes() + ")");

        // put everything back
        faces_safety_reset();  // 2.20: no miniface left queued for 3001
        set_faces_catalog(app.asset_catalog());
        for (const auto& [f, v] : keep_p) app.db.set_int(*t, rec, f, v);
        for (const auto& [f, v] : keep_o) app.db.set_int(*t, orec, f, v);
        for (const auto& [f, v] : keep_m) app.db.set_int(*mt, mrec, f, v);
        if (had_backup) put_bytes(backup_file, keep_backup);
        else fs::remove(backup_file);
        for (const auto& [p, b] : moved) put_bytes(p, b);
        if (had_missing) put_bytes(missing_txt, keep_missing);
        else fs::remove(missing_txt);
        app.legacy.refresh_missing();
        app.model.refresh_player(3001, app.today());
        app.model.refresh_player(1003, app.today());
        ui.frames(2);
    });
}
