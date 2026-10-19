// Native tests (1.2.5): minifaces that did not show up when a player was opened for editing or a face was assigned
// (core/legacy.h: on-screen pictures prompt Lua, Reload; ui/textures.h: failed reads retried, editor previews, the
// descriptor limit; core/image.h: errors that name the format; ui_images.cpp: the boxes say why).
// Included by test_main.cpp after the Ui driver.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

static void mf_put32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[o + size_t(i)] = uint8_t(v >> (8 * i));
}
static void mf_write(const fs::path& p, const std::vector<uint8_t>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
// A DX10 DDS of FC 27's miniface size in BC7 (a format Turbo does not decode)
static std::vector<uint8_t> mf_bc7_dds() {
    std::vector<uint8_t> d(148 + 45 * 45 * 16, 0);
    std::memcpy(d.data(), "DDS ", 4);
    mf_put32(d, 4, 124);
    mf_put32(d, 8, 0x1007);
    mf_put32(d, 12, 180);
    mf_put32(d, 16, 180);
    mf_put32(d, 28, 1);
    mf_put32(d, 76, 32);
    mf_put32(d, 80, 0x4);
    std::memcpy(d.data() + 84, "DX10", 4);
    mf_put32(d, 128, 98);  // DXGI_FORMAT_BC7_UNORM
    mf_put32(d, 132, 3);
    mf_put32(d, 140, 1);
    return d;
}
static bool log_has(const App& app, const std::string& a, const std::string& b = "") {
    for (const auto& l : app.log_lines)
        if (l.find(a) != std::string::npos && (b.empty() || l.find(b) != std::string::npos)) return true;
    return false;
}

// ---- pure parts (no ImGui)
static void test_miniface_show() {
    run_case("pictures (1.2.5): a DDS Turbo cannot read names its format, a cut-off one the bytes missing", [&] {
        const std::vector<uint8_t> dds = encode_dds_dxt5(solid(180, 180, 1, 2, 3));  // like FC 27's own minifaces
        Rgba img;
        std::string err;
        CHECK(decode_image(dds, img, &err) && img.w == 180 && img.h == 180, "the game's own format reads: " + err);
        // a copy Live Editor's export is still writing
        std::vector<uint8_t> cut(dds.begin(), dds.begin() + 128 + 1000);
        CHECK(!decode_image(cut, img, &err) && err.find("180 x 180 DXT5 needs 32400 bytes") != std::string::npos &&
                  err.find("the file has 1000") != std::string::npos,
              "cut off: " + err);
        CHECK(!decode_image(mf_bc7_dds(), img, &err) && err.find("BC7 (DXGI 98)") != std::string::npos, "BC7: " + err);
        std::vector<uint8_t> ati = dds;
        std::memcpy(ati.data() + 84, "ATI2", 4);
        CHECK(!decode_image(ati, img, &err) && err.find("compression ATI2 is not supported") != std::string::npos, "ATI2: " + err);
        std::vector<uint8_t> nopf = dds;
        mf_put32(nopf, 80, 0);
        CHECK(!decode_image(nopf, img, &err) && err.find("flags 0x0") != std::string::npos, "no pixel format: " + err);
        std::vector<uint8_t> huge = dds;
        mf_put32(huge, 12, 99999);
        CHECK(!decode_image(huge, img, &err) && err.find("99999") != std::string::npos, "size out of range: " + err);
    });

    run_case("legacy files (1.2.5): a picture asked on screen prompts Lua until it goes quiet; Reload asks the game again", [&] {
        fs::path le = g_out / "legacy_wait";
        fs::remove_all(le);
        LegacyImages L(le);
        const std::string a = legacy_path::player_miniface(71191), b = legacy_path::player_miniface(2002);
        fs::path f;
        L.tick(100.0);
        CHECK(!L.wants_lua() && !L.stalled() && L.waiting_for(a) < 0.0, "nothing asked: Lua is left alone");
        CHECK(L.locate(a, &f) == LegacyImages::State::Waiting, "a player's miniface the cache does not have is asked");
        CHECK(L.wants_lua() && L.waiting_for(a) == 0.0 && L.waiting_on_screen() == 1, "Lua prompted at once");
        L.tick(110.0);
        CHECK(L.wants_lua() && std::abs(L.waiting_for(a) - 10.0) < 1e-9, fmt("still prompted, asked %.1f s ago", L.waiting_for(a)));
        L.tick(100.0 + LegacyImages::kStallSeconds + 1.0);
        CHECK(L.stalled() && !L.wants_lua(), "no answer for 20 s: Lua is not prompted for ever");
        L.locate(a, &f);
        CHECK(L.stalled(), "the same picture drawn again does not start over");
        L.locate(b, &f);
        CHECK(!L.stalled() && L.wants_lua(), "a newly asked picture prompts Lua again");
        L.tick(150.0);
        CHECK(L.stalled(), "quiet again");
        fs::create_directories(L.cache_dir());
        std::ofstream(L.cache_dir() / "status.txt") << "exported 3 missing 0 waiting 2";
        L.tick(152.0);
        CHECK(!L.stalled() && L.lua_status() == "exported 3 missing 0 waiting 2", "Lua wrote its counts: it runs");
        L.tick(180.0);
        CHECK(L.stalled(), "quiet");
        L.not_prompting();
        CHECK(!L.stalled(), "Turbo hidden (Lua not prompted): its silence does not count");
        // Lua delivers a, the game has no b
        mf_write(L.cache_file(a), encode_dds_dxt5(solid(8, 8, 9, 9, 9)));
        L.tick(181.0);
        CHECK(L.locate(a, &f) == LegacyImages::State::Game && L.waiting_for(a) < 0.0, "delivered: no longer waiting");
        std::ofstream(L.cache_dir() / "missing.txt") << b << "\n";
        L.tick(183.0);
        CHECK(L.locate(b, &f) == LegacyImages::State::Missing && !L.wants_lua(), "refused: nothing left to prompt Lua for");
        // Reload: the refusal is set aside, Lua looks again (new generation), b first in the list
        const uint64_t g0 = L.generation();
        L.reload(b);
        CHECK(L.locate(b, &f) == LegacyImages::State::Waiting && L.generation() > g0 && L.wants_lua(), "asked again");
        L.flush();
        std::string want = read_file(L.cache_dir() / "want.txt");
        CHECK(want.rfind("#gen " + std::to_string(L.generation()) + "\n" + b + "\n", 0) == 0, "new generation, first in the list: " + want);
        { std::ofstream(L.cache_dir() / "missing.txt", std::ios::app) << "data/ui/imgAssets/heads/p1.dds\n"; }
        L.tick(185.0);
        CHECK(L.locate(b, &f) == LegacyImages::State::Waiting, "missing.txt read again (another path): b's old line stays set aside");
        { std::ofstream(L.cache_dir() / "missing.txt", std::ios::app) << b << "\n"; }
        L.tick(187.0);
        CHECK(L.locate(b, &f) == LegacyImages::State::Missing, "Lua refused b again: not in the game");
        // Reload a delivered picture: its cached copy goes (Live Editor exports the custom file while one is in place)
        CHECK(fs::exists(L.cache_file(a)), "a cached");
        L.reload(a);
        CHECK(!fs::exists(L.cache_file(a)) && L.locate(a, &f) == LegacyImages::State::Waiting, "a's copy dropped, asked again");
        CHECK(L.cache_file("data/../x.dds").empty(), "no cache place for an invalid path");
        std::string err;
        CHECK(L.clear_cache(&err) && L.locate(b, &f) == LegacyImages::State::Waiting, "emptied cache: everything asked again");
    });
}

// ---- with the ImGui context (UI part)
static void ui_cases_miniface_show(App& app, Ui& ui, const fs::path& le) {
    run_case("textures (1.2.5): a failed read is retried, an editor's preview can be forgotten, never more textures than descriptors", [&] {
        fs::path dir = g_out / "tex_retry";
        fs::remove_all(dir);
        fs::path f = dir / "late.dds";
        const std::vector<uint8_t> good = encode_dds_dxt5(solid(8, 8, 10, 200, 10));
        mf_write(f, std::vector<uint8_t>(good.size(), 0));  // the copy is still being written: zeros of the final size
        const auto t0 = fs::last_write_time(f);
        {
            TextureCache c;
            c.new_frame(10.0);
            TextureCache::Pic p = c.file(f, 64);
            CHECK(p.failed && !p.tex && !p.error.empty(), "unreadable at first: " + p.error);
            mf_write(f, good);
            fs::last_write_time(f, t0);  // same size and time stamp: only a new read can tell
            c.new_frame(10.5);
            p = c.file(f, 64);
            CHECK(p.failed && !p.tex, "not read again before kRetryFirst");
            c.new_frame(10.0 + TextureCache::kRetryFirst + 0.1);
            p = c.file(f, 64);
            CHECK(p.tex && !p.failed && p.w == 8, "read again a second later: shown (1.2.4 kept it unreadable)");

            // editor previews: the same version is the same picture, until the editor forgets it (another player)
            Rgba red = solid(4, 4, 255, 0, 0), blue = solid(4, 4, 0, 0, 255);
            TextureCache::Pic a = c.pixels("ed", 2, red);
            TextureCache::Pic b = c.pixels("ed", 2, blue);
            CHECK(a.tex && b.tex == a.tex, "same version: the texture made before");
            const size_t made = c.created();
            c.forget_pixels("ed");
            TextureCache::Pic d = c.pixels("ed", 2, blue);
            CHECK(d.tex && d.tex != a.tex && c.created() == made + 1, "forgotten: a new texture");
            const unsigned char* px = d.tex ? static_cast<const unsigned char*>(d.tex->GetPixels()) : nullptr;
            CHECK(px && px[0] == 0 && px[2] == 255, "with the new pixels (blue)");
        }
        {
            // descriptor limit: a burst of new pictures in one frame stops at kHardMaxTextures and waits (no error)
            TextureCache c;
            c.new_frame(1.0);
            Rgba tiny = solid(4, 4, 1, 2, 3);
            size_t made = 0;
            for (size_t i = 0; i < TextureCache::kHardMaxTextures + 12; ++i)
                if (c.pixels("k" + std::to_string(i), 1, tiny).tex) ++made;
            CHECK(made == TextureCache::kHardMaxTextures && c.size() == TextureCache::kHardMaxTextures, fmt("%zu made", made));
            TextureCache::Pic w = c.pixels("one more", 1, tiny);
            CHECK(!w.tex && !w.failed, "the next one waits (spinner), it is not an error");
            c.new_frame(1.02);
            CHECK(c.size() == TextureCache::kHardMaxTextures, "drawn in the last frame: kept");
            c.new_frame(1.04);
            CHECK(c.size() == TextureCache::kMaxTextures, fmt("not drawn in the last frame, near the limit: back to %zu (%zu)",
                                                              TextureCache::kMaxTextures, c.size()));
        }
        fs::remove_all(dir);
    });

    run_case("UI (1.2.5): Players > Miniface of a player whose miniface is not cached: Lua prompted, how long, Reload, delivered, "
             "unreadable says why, Remove asks the game again",
             [&] {
                 const std::string path = legacy_path::player_miniface(2002);
                 const fs::path cached = app.legacy.cache_file(path);
                 const fs::path custom = le / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads" / "p2002.dds";
                 fs::remove(cached);
                 fs::remove(custom);
                 app.request_tab = 0;
                 ui.frames(2);
                 CHECK(ui.click("2002", "##plist"), "player 2002 (a generic head: never loaded in the background)");
                 CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
                 ui.frames(2);
                 CHECK(app.legacy.waiting_for(path) >= 0.0, "his miniface is asked from the game");
                 // (the test world's background loading never ends: Lua is also prompted for it, as in 1.2.4)
                 const auto background = [&] { return app.preloader && app.preloader->loading() && app.legacy.waiting_background() > 0; };
                 CHECK(app.legacy.wants_lua() && app.lua_images_wanted(), "Lua is prompted for it");
                 {
                     std::unique_ptr<Preloader> pre = std::move(app.preloader);  // as if the background loading were done
                     CHECK(app.lua_images_wanted(), "by this picture alone (1.2.4: only while the background loading ran)");
                     app.preloader = std::move(pre);
                 }
                 CHECK(ui.find("Reload##mfnow") != nullptr, "Reload under the picture");
                 // nothing comes back
                 ui.t += LegacyImages::kStallSeconds + 2.0;
                 ui.frames(2);
                 CHECK(app.legacy.stalled(), fmt("no answer for 20 s: stalled (%zu waiting on screen)", app.legacy.waiting_on_screen()));
                 CHECK(!app.legacy.wants_lua() && app.lua_images_wanted() == background(), "Lua is not prompted for ever for it");
                 {
                     std::unique_ptr<Preloader> pre = std::move(app.preloader);
                     CHECK(!app.lua_images_wanted(), "no prompting at all once the background loading is done");
                     app.preloader = std::move(pre);
                 }
                 CHECK(log_has(app, "no answer from Turbo's Lua side"), "said in the log");
                 PictureInfo pi;
                 pi.state = LegacyImages::State::Waiting;
                 pi.waiting = app.legacy.waiting_for(path);
                 pi.stalled = true;
                 bool warn = false;
                 std::string line = picture_status_line(app, pi, "miniface", &warn);
                 CHECK(warn && line.find("No answer from the game for 2") != std::string::npos && line.find("Reload") != std::string::npos,
                       "the line under the box says so: " + line);
                 pi.stalled = false;
                 pi.waiting = 3.0;
                 line = picture_status_line(app, pi, "miniface", &warn);
                 CHECK(!warn && line == "Loading from the game... 3 s", "while waiting: " + line);
                 const uint64_t g0 = app.legacy.generation();
                 CHECK(ui.click("Reload##mfnow"), "Reload");
                 CHECK(!app.legacy.stalled() && app.legacy.wants_lua() && app.lua_images_wanted() && app.legacy.generation() > g0,
                       "asked again: new generation, Lua prompted");
                 app.legacy.flush();
                 const std::string want = read_file(le / "turbo_output" / "cache" / "legacy" / "want.txt");
                 CHECK(want.find("\n" + path + "\n") == want.find('\n'), "first in want.txt: " + want);
                 // Lua delivers it: shown
                 const size_t made = app.textures.created();
                 mf_write(cached, encode_dds_dxt5(solid(180, 180, 30, 160, 30)));
                 // 2.20: a picture not there is looked at once a second (never in every frame): shown within about a second
                 int frames = 0;
                 while (frames < 90 && (app.legacy.waiting_for(path) >= 0.0 || app.textures.created() <= made)) {
                     ui.frame();
                     ++frames;
                 }
                 CHECK(app.legacy.waiting_for(path) < 0.0 && app.textures.created() > made,
                       "delivered and shown (after " + std::to_string(frames) + " frames of 1/60 s)");
                 PictureInfo shown;
                 shown.state = LegacyImages::State::Game;
                 shown.drawn = true;
                 CHECK(picture_status_line(app, shown, "miniface") == "The game's own", "the game's own");
                 // a custom file Turbo cannot read: the box says why (tooltip, the line under it, the log)
                 // 2.19: in a career Turbo writes the career's own copy (core/career_faces.h); Remove takes that copy away
                 {
                     std::string werr;
                     CHECK(app.write_face_picture(path, mf_bc7_dds(), &werr), "unreadable picture written in this career: " + werr);
                 }
                 ui.frames(3);
                 CHECK(log_has(app, "picture unreadable: ", "p2002.dds: DDS DX10 format BC7 (DXGI 98) is not supported"), "logged with its format");
                 PictureInfo bad;
                 bad.state = LegacyImages::State::Custom;
                 bad.file = custom;
                 bad.failed = true;
                 bad.error = "DDS DX10 format BC7 (DXGI 98) is not supported";
                 line = picture_status_line(app, bad, "miniface", &warn);
                 CHECK(warn && line.find("Unreadable custom file p2002.dds: DDS DX10 format BC7") != std::string::npos, "line: " + line);
                 // Remove: the cached copy goes too (Live Editor exports the custom file while it is in place) and the
                 // game's own is asked again
                 CHECK(ui.click("Remove custom miniface"), "Remove");
                 ui.frames(2);
                 CHECK(ui.click("Remove", "##rmminiface"), "confirm");
                 ui.frames(1);
                 CHECK(!fs::exists(custom) && !fs::exists(cached), "custom file and cached copy gone");
                 CHECK(app.legacy.waiting_for(path) >= 0.0 && app.legacy.wants_lua(), "the game's own asked again");
                 fs::remove(cached);
             });

    run_case("UI (1.2.5): Miniface > The 3D model: a render still asked for the previous player is cancelled when another is opened", [&] {
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = true;
        fake->st.available = true;
        fake->st.reason = "ready";
        app.capture = fake;
        app.request_tab = 0;
        ui.frames(2);
        CHECK(ui.click("1002", "##plist"), "player 1002");
        CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
        CHECK(ui.click("The 3D model", "##mface"), "3D model tab");
        ui.frames(2);
        CHECK(ui.click("Generate from 3D model"), "Generate");
        ui.frames(2);
        CHECK(fake->pending && fake->requests.size() == 1, "his render asked");
        const int cancels = fake->cancels;
        CHECK(ui.click("1003", "##plist"), "another player before the picture came");
        CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
        CHECK(ui.click("The 3D model", "##mface"), "3D model tab");
        ui.frames(2);
        CHECK(fake->cancels == cancels + 1 && !fake->pending, "the old request is dropped: the service is free (1.2.4: busy until Cancel)");
        CHECK(ui.click("Generate from 3D model"), "Generate for him");
        ui.frames(2);
        CHECK(fake->requests.size() == 2 && fake->requests.back().id == 1003, "his own render asked");
        fake->next = capture::Result();
        fake->next.ok = true;
        fake->next.format = "PNG 256x256";
        fake->next.image = solid(256, 256, 200, 40, 40);
        fake->deliver = true;
        ui.frames(3);
        CHECK(!fake->pending && ui.toast_contains("3D model rendered for player 1003"), "his picture arrives in his editor");
        app.capture.reset();
    });

    run_case("UI (1.2.5): Miniface: the next player's New preview is his own picture (the editor's versions start again)", [&] {
        fs::path pics = le / "turbo_minifaces";
        fs::create_directories(pics);
        { std::ofstream o(pics / "face.png", std::ios::binary); o.write(reinterpret_cast<const char*>(kTestPng), sizeof(kTestPng)); }
        app.request_tab = 0;
        ui.frames(2);
        // each player's editor starts from version 1: picking the same file for two players in a row gives both the
        // same version number
        CHECK(ui.click("2002", "##plist"), "player 2002 first (the editor starts again for 1002)");
        CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
        ui.frames(2);
        CHECK(ui.click("1002", "##plist"), "player 1002");
        CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
        CHECK(ui.click("An image file", "##mface"), "source: an image file");
        ui.frames(2);
        CHECK(ui.click("face.png", "##mffiles"), "a picture for him");
        ui.frames(2);
        CHECK(ui.click("1003", "##plist"), "player 1003");
        CHECK(ui.click("Miniface", "##pedit"), "Miniface tab");
        CHECK(ui.click("An image file", "##mface"), "source: an image file");
        ui.frames(4);
        const size_t before = app.textures.created();
        CHECK(ui.click("face.png", "##mffiles"), "a picture for him too");
        ui.frames(2);
        CHECK(app.textures.created() > before, fmt("a preview texture of his own (%zu -> %zu; 1.2.4 drew the previous player's)", before,
                                                   app.textures.created()));
    });
}
