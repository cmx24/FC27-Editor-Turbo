// 2.20 K4: a kit import makes the minikit of every slot (core/kit_import.h minikit functions, ui/ui_kits.cpp apply / undo).
// The zip, the game's files and the crest are generated here; the reference header is the first 128 bytes of the MK1 minikits
// installed on the live machine (read once, 2026-10-19, identical in all 66 files of the folder; j0_113974_0.dds, j0_10_0.dds).
// Unit cases run in the main list (minikit_test::unit); the App cases run inside the UI block after the Teams > Kits case
// (minikit_test::ui_cases), on Inter (241), with a scratch Live Editor root.
#pragma once
#include "core/kit_import.h"
#include "ui/ui_kits.h"

namespace minikit_test {

using namespace turbo;
using namespace kit_test;

static const char* kRefHeaderHex = "444453207c0000000710080000010000000100000000040000000000040000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000200000004100000000000000200000000000ff0000ff0000ff000000000000ff0810400000000000000000000000000000000000";

static std::vector<uint8_t> from_hex(const std::string& h) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
    return v;
}

// the BGRA texel (x, y) of mip level `lvl` (0..3) of a minikit file
static const uint8_t* texel(const std::vector<uint8_t>& d, int lvl, int x, int y) {
    size_t off = 128;
    int side = 256;
    for (int i = 0; i < lvl; ++i) {
        off += size_t(side) * size_t(side) * 4;
        side /= 2;
    }
    return &d[off + (size_t(y) * size_t(side) + size_t(x)) * 4];
}

static bool lower_has(const std::string& text, const char* word) {
    std::string t = text;
    for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
    return t.find(word) != std::string::npos;
}

static void unit() {
    run_case("minikits (2.20): the MK1 form, written byte for byte; check; refusals", [&] {
        // the header equals the one the live minikits carry
        const std::vector<uint8_t> ref = from_hex(kRefHeaderHex);
        CHECK(ref.size() == 128 && kits::minikit_header() == ref, "the header is the game's own, as the 66 live files have it");
        std::string err;
        // a 32 x 32 picture is framed to 256; left half green, right half white
        Rgba m = solid(32, 32, 0, 200, 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 16; x < 32; ++x) std::memset(m.at(x, y), 255, 4);
        const std::vector<uint8_t> dds = kits::encode_minikit_dds(m, &err);
        CHECK(dds.size() == 348288 && kits::kMinikitBytes == 348288 && kits::check_minikit_dds(dds, &err), "348,288 bytes and the proven header: " + err);
        CHECK(std::vector<uint8_t>(dds.begin(), dds.begin() + 128) == ref, "the first 128 bytes are the reference header");
        const uint8_t* l = texel(dds, 0, 10, 128);
        const uint8_t* r = texel(dds, 0, 250, 128);
        CHECK(l[0] == 0 && l[1] == 200 && l[2] == 0 && l[3] == 255, "level 0 is BGRA: the green half");
        CHECK(r[0] == 255 && r[1] == 255 && r[2] == 255 && r[3] == 255, "... and the white half");
        const uint8_t* m1 = texel(dds, 1, 5, 64);
        const uint8_t* m3 = texel(dds, 3, 31, 16);
        CHECK(m1[1] == 200 && m1[0] == 0 && m3[0] == 255, "the 128 and 32 pixel levels follow");
        // refusals
        std::vector<uint8_t> bad = dds;
        bad.pop_back();
        CHECK(!kits::check_minikit_dds(bad, &err) && err.find("348288") != std::string::npos, "one byte short refused: " + err);
        bad = dds;
        bad[8] ^= 1;
        CHECK(!kits::check_minikit_dds(bad, &err) && err.find("header") != std::string::npos, "another header refused: " + err);
        CHECK(kits::encode_minikit_dds(solid(32, 16, 1, 2, 3), &err).empty() && err.find("square") != std::string::npos, "a picture that is not square: " + err);
        CHECK(kits::encode_minikit_dds(Rgba(), &err).empty(), "no picture");
        // 256 x 256 BGRA8, 4 mips is the minikit form; other game layouts are not
        DdsFormat f;
        f.pixel = DdsFormat::Pixel::BGRA8;
        f.w = f.h = 256;
        f.mips = 4;
        CHECK(kits::is_minikit_form(f), "the layout of the MK1 files");
        f.mips = 1;
        CHECK(!kits::is_minikit_form(f), "one mip level is another layout");
        f.mips = 4;
        f.pixel = DdsFormat::Pixel::DXT5;
        CHECK(!kits::is_minikit_form(f), "DXT5 is another layout");
        // a game file that is in the form parses as it
        DdsFormat got;
        CHECK(parse_dds_format(dds, got, &err) && kits::is_minikit_form(got), "our own file parses as the minikit form: " + err);
    });

    run_case("minikits (2.20): a minikit drawn from the jersey (no minikit.png in the zip)", [&] {
        const auto& spec = small_spec();
        Rgba jersey = solid(64, 64, 35, 79, 142);  // the club's blue
        kits::MinikitHints h;
        h.have_prim = h.have_sec = true;
        h.prim[0] = 35;
        h.prim[1] = 79;
        h.prim[2] = 142;
        h.sec[0] = 255;
        h.sec[1] = 215;
        h.sec[2] = 0;
        std::string err;
        Rgba plain = kits::render_minikit(jersey, nullptr, h, &err);
        CHECK(plain.w == 256 && plain.h == 256, "256 x 256: " + err);
        CHECK(plain.at(2, 2)[3] == 0 && plain.at(250, 250)[3] == 0, "clear around the shirt");
        const uint8_t* body = plain.at(128, 160);
        CHECK(body[3] == 255 && std::abs(int(body[2]) - 142) < 40 && body[2] > body[0], "the body is the shirt blue: " + std::to_string(int(body[0])) + "," + std::to_string(int(body[1])) + "," + std::to_string(int(body[2])));
        const uint8_t* cuff = plain.at(225, 90);
        CHECK(cuff[3] == 255 && cuff[0] > 180 && cuff[2] < 60, "the cuff is the second colour (gold)");
        const uint8_t* cuff_l = plain.at(256 - 225, 90);
        CHECK(cuff_l[0] > 180 && cuff_l[2] < 60, "... on both sleeves");
        const uint8_t* neck = plain.at(128, 24);
        CHECK(neck[3] == 0, "the neck opening is clear");
        const uint8_t* chest = plain.at(160, 80);
        CHECK(std::abs(int(chest[2]) - 142) < 40, "no crest: the chest is the shirt");
        // the crest on the chest
        Rgba crest = solid(64, 64, 220, 20, 20);
        Rgba with = kits::render_minikit(jersey, &crest, h, &err);
        const uint8_t* c = with.at(162, 80);
        CHECK(c[0] > 190 && c[1] < 60 && c[2] < 60, "the crest sits on the chest (red)");
        CHECK(std::abs(int(with.at(128, 160)[2]) - 142) < 40, "the body is unchanged");
        // a jersey texture with a big black unused area: the hints pick the shirt blue, not the biggest group
        Rgba atlas = solid(64, 64, 0, 0, 0);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 64; ++x) {
                uint8_t* p = atlas.at(x, y);
                p[0] = 40;
                p[1] = 90;
                p[2] = 150;
            }
        Rgba steered = kits::render_minikit(atlas, nullptr, h, &err);
        CHECK(steered.at(128, 160)[2] > 100, "the settings colour steers the pick to the shirt colour, not the black area");
        // no hints: the biggest group wins; a second colour far from it becomes the trim
        kits::MinikitHints none;
        Rgba two = solid(64, 64, 200, 30, 30);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 24; ++x) {
                uint8_t* p = two.at(x, y);
                p[0] = 240;
                p[1] = 240;
                p[2] = 240;
            }
        Rgba tw = kits::render_minikit(two, nullptr, none, &err);
        CHECK(tw.at(128, 160)[0] > 150 && tw.at(128, 160)[1] < 80 && tw.at(225, 90)[1] > 150, "no hints: red shirt, white trim");
        CHECK(kits::render_minikit(Rgba(), nullptr, none, &err).empty() && !err.empty(), "no jersey: refused with a reason");
        // the file made from it is a minikit file
        CHECK(kits::check_minikit_dds(kits::encode_minikit_dds(with, &err), &err), "the drawn minikit encodes in the MK1 form: " + err);
        // a kit zip without minikit.png loads (a note, no problem); the thumbnail is the jersey
        const fs::path dir = g_out / "minikit_unit";
        Opts o;
        o.minikit = false;
        kits::KitPackage pk = kits::load_kit_zip(make_kit_zip(dir / "nomini.zip", spec, o), spec);
        bool noted = false;
        for (const std::string& n : pk.notes) noted = noted || n.find("no minikit.png") == 0;
        CHECK(pk.ok && pk.minikit.empty() && !pk.thumb.empty() && noted, "a zip without minikit.png is ready, Turbo will draw one: " + pk.error);
        kits::MinikitHints sh = kits::minikit_hints(pk.settings);
        CHECK(sh.have_prim && sh.have_sec && sh.prim[0] == 0x23 && sh.prim[1] == 0x4f && sh.prim[2] == 0x8e && sh.sec[0] == 0xff && sh.sec[1] == 0xd7,
              "the settings' teamcolorprim / teamcolorsec become the hints");
    });
}

// ---- the App cases (inside the UI block; Inter, 241)
static std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("minikits (2.20): every slot writes the zip's minikit in the MK1 form; drawn fallback; Undo restores; backups", [&] {
        const auto& spec = small_spec();
        kit_panel_spec(&spec);
        const int64_t team = 241;
        const kits::Slot slots[4] = {kits::Slot::Home, kits::Slot::Away, kits::Slot::Goalkeeper, kits::Slot::Third};
        const fs::path cache = le / "turbo_output" / "cache" / "legacy";
        const fs::path missing_txt = cache / "missing.txt";
        const std::string missing_before = read_text(missing_txt);
        // the game has no minikit for the four slots of Inter (the Eerste case: nothing shipped)
        {
            std::ofstream f(missing_txt, std::ios::binary | std::ios::app);
            for (kits::Slot s : slots) f << kits::minikit_path(team, s) << "\n";
        }
        ui.frames(80);  // missing.txt is read once a second
        for (kits::Slot s : slots) CHECK(minikit_state(app, team, s) == LegacyImages::State::Missing, "the game has no minikit for slot " + std::to_string(kits::slot_type(s)));
        const fs::path src = g_out / "minikit_src";
        make_kit_zip(src / "with.zip", spec);
        Opts nm;
        nm.minikit = false;
        make_kit_zip(src / "without.zip", spec, nm);

        // ---- (i) the zip's minikit.png, all four slots
        std::string err;
        for (kits::Slot s : slots) {
            kits::KitPackage pk = kits::load_kit_zip(src / "with.zip", spec);
            KitApply r = apply_kit_import(app, team, s, pk);
            const fs::path f = app.legacy.custom_file(kits::minikit_path(team, s));
            const std::vector<uint8_t> b = f.empty() ? std::vector<uint8_t>() : file_bytes(f);
            CHECK(r.ok && r.minikit && !f.empty() && b.size() == 348288 && kits::check_minikit_dds(b, &err),
                  "slot " + std::to_string(kits::slot_type(s)) + ": 348,288 bytes in the MK1 form: " + r.line + " " + err);
            CHECK(b.size() == 348288 && texel(b, 0, 10, 128)[1] == 200 && texel(b, 0, 250, 128)[0] == 255, "slot " + std::to_string(kits::slot_type(s)) + ": it is the zip's picture");
            CHECK(r.line.find("Minikit updated") != std::string::npos, "the card says it: " + r.line);
            for (const char* w : {"dds", "legacy", "md5", "path"}) CHECK(!lower_has(r.line, w), std::string("the card has no '") + w + "' in it: " + r.line);
        }
        // ---- Undo of each removes it (nothing was there before)
        for (kits::Slot s : slots) {
            std::string msg;
            CHECK(undo_kit_import(app, team, s, &msg) && app.legacy.custom_file(kits::minikit_path(team, s)).empty(), "undo slot " + std::to_string(kits::slot_type(s)) + ": " + msg);
        }

        // ---- (ii) no minikit.png: drawn by Turbo, with the club's crest when the game's crest is there
        {
            DdsFormat cf;
            cf.pixel = DdsFormat::Pixel::BGRA8;
            cf.w = cf.h = 64;
            cf.mips = 1;
            write(cache / "data" / "ui" / "imgAssets" / "crest" / "light" / "l241.dds", encode_dds(solid(64, 64, 220, 20, 20), cf, &err));
            ui.frames(80);
            kits::KitPackage pk = kits::load_kit_zip(src / "without.zip", spec);
            CHECK(pk.ok, "the zip without minikit.png is ready: " + pk.error);
            KitApply r = apply_kit_import(app, team, kits::Slot::Away, pk);
            const fs::path f = app.legacy.custom_file(kits::minikit_path(team, kits::Slot::Away));
            const std::vector<uint8_t> b = f.empty() ? std::vector<uint8_t>() : file_bytes(f);
            CHECK(r.ok && r.minikit && b.size() == 348288 && kits::check_minikit_dds(b, &err), "drawn minikit in the MK1 form: " + r.line + " " + err);
            CHECK(r.line.find("Minikit drawn by Turbo") != std::string::npos && r.line.find("without the crest") == std::string::npos, "the card says Turbo drew it: " + r.line);
            CHECK(b.size() == 348288 && texel(b, 0, 128, 160)[0] > texel(b, 0, 128, 160)[2], "the shirt is the jersey's blue (BGRA: B above R)");
            CHECK(b.size() == 348288 && texel(b, 0, 162, 80)[2] > 190 && texel(b, 0, 162, 80)[0] < 70, "the club's crest (red) is on the chest");
            CHECK(b.size() == 348288 && texel(b, 0, 2, 2)[3] == 0, "clear around the shirt");
            std::string msg;
            CHECK(undo_kit_import(app, team, kits::Slot::Away, &msg) && app.legacy.custom_file(kits::minikit_path(team, kits::Slot::Away)).empty(), "undo: " + msg);
            // the crest not there: the card says so
            fs::remove(cache / "data" / "ui" / "imgAssets" / "crest" / "light" / "l241.dds");
            std::ofstream(missing_txt, std::ios::binary | std::ios::app) << legacy_path::crest(241, 0, "light") << "\n";
            ui.frames(80);
            KitApply r2 = apply_kit_import(app, team, kits::Slot::Away, pk);
            CHECK(r2.minikit && r2.line.find("Minikit drawn by Turbo (without the crest") != std::string::npos, "no crest yet: the card says so: " + r2.line);
            CHECK(undo_kit_import(app, team, kits::Slot::Away, &msg), "undo again: " + msg);
        }

        // ---- (iv) a file that is not Turbo's (the MK1 files) is backed up before the overwrite; (iii) Undo puts it back byte for byte
        {
            const std::string mp = kits::minikit_path(team, kits::Slot::Home);
            Rgba old = solid(256, 256, 11, 22, 33);
            const std::vector<uint8_t> mk1 = kits::encode_minikit_dds(old, &err);
            write(le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "kits" / "j0_241_0.dds", mk1);
            ui.frames(2);
            CHECK(!app.legacy.custom_file(mp).empty(), "an MK1 file is installed for the slot");
            kits::KitPackage pk = kits::load_kit_zip(src / "with.zip", spec);
            const size_t backups_before = fs::exists(app.legacy.kit_backup_dir()) ? std::distance(fs::directory_iterator(app.legacy.kit_backup_dir()), fs::directory_iterator()) : 0;
            KitApply r = apply_kit_import(app, team, kits::Slot::Home, pk);
            const std::vector<uint8_t> now = file_bytes(app.legacy.custom_file(mp));
            const size_t backups_after = std::distance(fs::directory_iterator(app.legacy.kit_backup_dir()), fs::directory_iterator());
            CHECK(r.minikit && now != mk1 && kits::check_minikit_dds(now, &err), "the import replaced it: " + r.line);
            CHECK(backups_after == backups_before + 1, "the old file was backed up first (kit_backups +1)");
            CHECK(r.line.find("the earlier one is kept for Undo") != std::string::npos, "the card says it replaced one: " + r.line);
            // a second import of the same slot keeps the first backup
            KitApply r2 = apply_kit_import(app, team, kits::Slot::Home, pk);
            CHECK(r2.minikit, "imported again: " + r2.line);
            std::string msg;
            CHECK(undo_kit_import(app, team, kits::Slot::Home, &msg), "undo: " + msg);
            CHECK(file_bytes(app.legacy.custom_file(mp)) == mk1, "the MK1 file is back, byte for byte");
        }
        // ---- the sequence the review named: an import that wrote nothing (switched off), then one that does, then Undo
        {
            const std::string mp = kits::minikit_path(team, kits::Slot::Third);
            const std::vector<uint8_t> mk1 = kits::encode_minikit_dds(solid(256, 256, 90, 80, 70), &err);
            write(le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "kits" / "j3_241_0.dds", mk1);
            ui.frames(2);
            kits::KitPackage pk = kits::load_kit_zip(src / "with.zip", spec);
            CHECK(minikit_enabled(app, team) && set_minikit_enabled(app, team, false) && !minikit_enabled(app, team), "the per-club switch goes off");
            KitApply off = apply_kit_import(app, team, kits::Slot::Third, pk);
            CHECK(off.ok && !off.minikit && off.line.find("Minikit not updated: switched off for this club") != std::string::npos, "switched off: no minikit written: " + off.line);
            CHECK(file_bytes(app.legacy.custom_file(mp)) == mk1, "the MK1 file is untouched while switched off");
            CHECK(set_minikit_enabled(app, team, true) && minikit_enabled(app, team), "back on");
            KitApply on = apply_kit_import(app, team, kits::Slot::Third, pk);
            CHECK(on.minikit && file_bytes(app.legacy.custom_file(mp)) != mk1, "now it is written: " + on.line);
            std::string msg;
            CHECK(undo_kit_import(app, team, kits::Slot::Third, &msg), "undo: " + msg);
            CHECK(!app.legacy.custom_file(mp).empty() && file_bytes(app.legacy.custom_file(mp)) == mk1, "Undo brings the MK1 file back, not deletes it: " + msg);
            std::error_code ec;
            fs::remove(le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "kits" / "j3_241_0.dds", ec);
        }
        // ---- the game has its own minikit in the MK1 layout: written with the MK1 writer; in another layout: the game's
        {
            DdsFormat gf;
            gf.pixel = DdsFormat::Pixel::BGRA8;
            gf.w = gf.h = 256;
            gf.mips = 4;
            write(cache / "data" / "ui" / "imgAssets" / "kits" / "j2_241_0.dds", encode_dds(solid(256, 256, 1, 2, 3), gf, &err));
            std::ofstream(missing_txt, std::ios::binary | std::ios::trunc) << missing_before;  // the game has the file now: no missing line
            ui.frames(80);
            CHECK(minikit_state(app, team, kits::Slot::Goalkeeper) == LegacyImages::State::Game, "the game has its own goalkeeper minikit");
            kits::KitPackage pk = kits::load_kit_zip(src / "with.zip", spec);
            KitApply r = apply_kit_import(app, team, kits::Slot::Goalkeeper, pk);
            const std::vector<uint8_t> b = file_bytes(app.legacy.custom_file(kits::minikit_path(team, kits::Slot::Goalkeeper)));
            CHECK(r.minikit && kits::check_minikit_dds(b, &err) && r.line.find("over the game's own") != std::string::npos,
                  "the game's file is in the MK1 layout: written byte for byte as MK1, the card says it is over the game's own: " + r.line + " " + err);
            std::string msg;
            CHECK(undo_kit_import(app, team, kits::Slot::Goalkeeper, &msg) && app.legacy.custom_file(kits::minikit_path(team, kits::Slot::Goalkeeper)).empty(), "undo: " + msg);
        }
        // ---- clean up what this case put in the scratch root
        std::error_code ec;
        fs::remove(cache / "data" / "ui" / "imgAssets" / "kits" / "j2_241_0.dds", ec);
        std::ofstream(missing_txt, std::ios::binary | std::ios::trunc) << missing_before;
        ui.frames(80);
        fs::remove(kits::kits_dir(le) / "241" / "minikit_off.txt", ec);
        kit_panel_spec(nullptr);
        app.toasts.clear();
    });
}

}  // namespace minikit_test
