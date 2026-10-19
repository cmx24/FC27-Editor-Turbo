// Native tests (2.20 track V, brief 2.20-V-visual-pickers): the visual pickers.
//  core: the name mapping (compiled from data/visual_names.json) loads, every run style has its in-game name (Medalist = 21,
//        Head Down = 8), the GIF reader gives every frame with its delay (synthetic GIFs written here; RDBM26's own
//        runstyle_0.gif too when the folder is on this PC), the animation store (worker, budget, eviction), the folder rules
//        (root or Resources, the setting file, a missing folder).
//  ui:   the picker writes the picked code into the right field and only that field (every other field of the record compared
//        before and after), a missing folder gives the names-only list that still writes, and the perf case: opening the run-style
//        picker on 22 real-size animations (336 x 576, 29 frames, like RDBM26's) never takes more than 16 ms of a frame on this
//        thread while the worker decodes them (measured, printed; RDBM26's own folder measured too when present).
// The RDBM26 pictures are never copied: the synthetic GIFs are made by the test, the real folder is only read.
// Included by test_main.cpp after test_bodytype_thumbs.h; visual_picker_test::core_cases() from main with the core tests,
// visual_picker_test::ui_cases() with the UI cases.
#pragma once
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "core/rdbm_resources.h"
#include "ui/ui_visual_picker.h"

namespace visual_picker_test {

namespace vis = turbo::visual;

// ---- a GIF writer for the tests: 128-colour palette, uncompressed LZW (8-bit codes, a clear code every 100 pixels: the code
// table never grows past 8 bits, so every code is one byte)
inline std::vector<uint8_t> make_gif(int w, int h, int frames, int delay_cs, int seed = 0) {
    std::vector<uint8_t> o;
    auto u16 = [&](int v) { o.push_back(static_cast<uint8_t>(v & 255)); o.push_back(static_cast<uint8_t>((v >> 8) & 255)); };
    const char* hdr = "GIF89a";
    o.insert(o.end(), hdr, hdr + 6);
    u16(w);
    u16(h);
    o.push_back(0xF6);  // global colour table, 128 entries
    o.push_back(0);
    o.push_back(0);
    for (int i = 0; i < 128; ++i) {
        o.push_back(static_cast<uint8_t>(i * 2));
        o.push_back(static_cast<uint8_t>(255 - i * 2));
        o.push_back(static_cast<uint8_t>((i * 37) & 255));
    }
    for (int f = 0; f < frames; ++f) {
        const uint8_t gce[] = {0x21, 0xF9, 0x04, 0x00, static_cast<uint8_t>(delay_cs & 255), static_cast<uint8_t>(delay_cs >> 8), 0x00, 0x00};
        o.insert(o.end(), gce, gce + sizeof(gce));
        o.push_back(0x2C);
        u16(0);
        u16(0);
        u16(w);
        u16(h);
        o.push_back(0);
        o.push_back(7);  // LZW minimum code size: 7 (128 colours); codes start at 8 bits
        std::vector<uint8_t> codes;
        codes.reserve(static_cast<size_t>(w) * h * 101 / 100 + 4);
        int run = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (run == 0) codes.push_back(128);  // clear
                codes.push_back(static_cast<uint8_t>((x / 8 + y / 8 + f * 5 + seed) % 128));
                if (++run == 100) run = 0;
            }
        codes.push_back(129);  // end of information
        for (size_t i = 0; i < codes.size(); i += 255) {
            const size_t n = std::min<size_t>(255, codes.size() - i);
            o.push_back(static_cast<uint8_t>(n));
            o.insert(o.end(), codes.begin() + static_cast<long>(i), codes.begin() + static_cast<long>(i + n));
        }
        o.push_back(0);
    }
    o.push_back(0x3B);
    return o;
}

inline void write_bytes(const fs::path& p, const std::vector<uint8_t>& b) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

inline fs::path real_resources() { return vis::resources_dir(vis::kDefaultRoot); }

// A fake RDBM26 install: Resources\runstyle\runstyle_<n>.gif for the 22 codes RDBM26 has, real size, plus a few still pictures
inline fs::path make_fake_rdbm(const fs::path& root) {
    std::error_code ec;
    fs::remove_all(root, ec);
    const std::vector<uint8_t> gif = make_gif(336, 576, 29, 3);
    const vis::SetDef* rs = vis::find_set("runstyle");
    for (int i = 0; rs && i < rs->count; ++i) {
        const vis::CodeDef& c = vis::codes_begin(*rs)[i];
        if (c.file) write_bytes(root / "Resources" / "runstyle" / c.file, gif);
    }
    write_bytes(root / "Resources" / "fkanim" / "fkanim_0.gif", make_gif(392, 560, 12, 4, 7));
    return root;
}

inline void core_cases() {
    run_case("Visual pickers: the name mapping loads (19 sets, 28 fields, codes ascending, every field's set found)", [] {
        CHECK(vis::set_count() == 19, fmt("19 picture sets (%zu)", vis::set_count()));
        const std::vector<std::string> fields = vis::mapped_fields();
        CHECK(fields.size() == 28, fmt("28 fields (%zu)", fields.size()));
        size_t codes = 0, named = 0;
        for (size_t i = 0; i < vis::set_count(); ++i) {
            const vis::SetDef& s = vis::set_at(i);
            const vis::CodeDef* b = vis::codes_begin(s);
            for (int k = 0; k < s.count; ++k) {
                ++codes;
                named += b[k].name && b[k].name[0];
                if (k > 0) CHECK(b[k - 1].code < b[k].code, fmt("%s: codes ascending at %d", s.id, b[k].code));
            }
        }
        CHECK(codes == 1837 && named == 775, fmt("1837 codes, 775 with an in-game name (%zu, %zu)", codes, named));
        for (const std::string& f : fields) CHECK(vis::set_for_field(f) != nullptr, "set of " + f);
        CHECK(vis::set_for_field("runstylecode") == vis::find_set("runstyle"), "runstylecode -> run styles");
        CHECK(vis::set_for_field("tattooleftleg") == vis::find_set("tattoo"), "every tattoo slot -> tattoos");
        CHECK(vis::set_for_field("overallrating") == nullptr, "a field with no pictures has no set");
        CHECK(vis::code_name(*vis::find_set("haircolor"), 1) == "Blonde", "hair colour 1 = Blonde");
        CHECK(vis::code_name(*vis::find_set("tattoo"), 71, "") == "Tattoo 71", "an unnamed tattoo: title and number, never the number alone");
        CHECK(vis::code_name(*vis::find_set("tattoo"), 71, "Rose sleeve") == "Rose sleeve", "an unnamed tattoo: the editors' name first");
        CHECK(vis::code_name(*vis::find_set("tattoo"), 71, "71") == "Tattoo 71", "a bare-number fallback is not a name");
    });

    run_case("Visual pickers: every run style has its in-game name (22 with an animation; Medalist = 21, Head Down = 8)", [] {
        const vis::SetDef* rs = vis::find_set("runstyle");
        CHECK(rs && rs->kind == vis::data::kGif && rs->count == 24, "run styles: 24 codes, animated");
        if (!rs) return;
        int with_gif = 0;
        for (int i = 0; i < rs->count; ++i) {
            const vis::CodeDef& c = vis::codes_begin(*rs)[i];
            CHECK(c.name && c.name[0], fmt("run style %d has a name", c.code));
            if (c.file) {
                ++with_gif;
                CHECK(std::string(c.file) == "runstyle_" + std::to_string(c.code) + ".gif", fmt("run style %d: its RDBM26 animation", c.code));
            }
        }
        CHECK(with_gif == 22, fmt("22 run styles with an animation (%d)", with_gif));
        CHECK(vis::code_name(*rs, 21) == "Medalist" && vis::code_name(*rs, 8) == "Head Down" && vis::code_name(*rs, 0) == "Default",
              "Medalist 21, Head Down 8, Default 0");
        const std::vector<int> found = vis::filter_codes(*rs, "medal");
        CHECK(found.size() == 1 && found[0] == 21, "search 'medal' finds code 21 only");
        CHECK(vis::filter_codes(*rs, "").size() == 24, "an empty search shows all 24");
        const vis::SetDef* fk = vis::find_set("fkanim");
        CHECK(fk && vis::code_name(*fk, 0) == "Default", "free kick 0 = Default");
    });

    run_case("Visual pickers: the GIF reader gives every frame with its delay (synthetic GIFs; RDBM26's runstyle_0.gif when present)", [] {
        vis::Anim a;
        std::string err;
        CHECK(vis::decode_gif(make_gif(64, 32, 5, 7), 180, 0, a, &err), "a 5-frame GIF decodes: " + err);
        CHECK(a.frames == 5 && a.src_w == 64 && a.src_h == 32 && a.frame_w == 64 && a.frame_h == 32,
              fmt("5 frames of 64 x 32, not scaled up (%d, %dx%d)", a.frames, a.frame_w, a.frame_h));
        CHECK(a.delays_ms.size() == 5 && a.delays_ms[0] == 70 && a.total_ms == 350, "delays from the file: 70 ms each");
        CHECK(a.frame_at(0) == 0 && a.frame_at(69) == 0 && a.frame_at(70) == 1 && a.frame_at(349) == 4 && a.frame_at(350) == 0,
              "frame timing follows the delays and loops");
        CHECK(a.atlas.w == a.cols * a.frame_w && a.atlas.h >= a.frame_h && !a.atlas.empty(), "one atlas for all frames");
        // frames differ (the pattern moves with the frame): frame 0 and frame 1 are not the same pixels
        float u0, v0, u1, v1;
        a.uv(1, u0, v0, u1, v1);
        CHECK(u1 > u0 && v1 > v0 && u1 <= 1.0f && v1 <= 1.0f, "a frame's rectangle inside the atlas");
        const uint8_t* p0 = a.atlas.at(1, 1);
        const uint8_t* p1 = a.atlas.at(static_cast<int>(u0 * a.atlas.w) + 1, static_cast<int>(v0 * a.atlas.h) + 1);
        CHECK(std::memcmp(p0, p1, 3) != 0 && p0[3] == 255, "frame 2 is not frame 1; opaque");

        // tiny delays shown as 100 ms (the browsers' rule); a real-size run style scaled to 180 px
        CHECK(vis::decode_gif(make_gif(16, 16, 3, 0), 180, 0, a) && a.delays_ms[0] == 100, "delay 0 -> 100 ms");
        CHECK(vis::decode_gif(make_gif(336, 576, 29, 3), 180, size_t(16) << 20, a) && a.frames == 29 && a.frame_h == 180 && a.frame_w == 105,
              fmt("336 x 576 x 29 -> 29 frames of 105 x 180 (%d, %dx%d)", a.frames, a.frame_w, a.frame_h));
        CHECK(a.atlas.w <= vis::kMaxAtlasSide && a.atlas.h <= vis::kMaxAtlasSide, "atlas inside the texture limit");
        // a byte limit makes the frames smaller, never drops frames
        CHECK(vis::decode_gif(make_gif(336, 576, 29, 3), 180, size_t(1) << 20, a) && a.frames == 29 && a.bytes() <= (size_t(1) << 20) + 64 * 1024,
              fmt("1 MB limit: 29 frames, %zu bytes", a.bytes()));
        // not a GIF, a cut file
        CHECK(!vis::decode_gif(std::vector<uint8_t>{'P', 'N', 'G', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 180, 0, a, &err) && !err.empty(),
              "not a GIF: refused with a reason");
        std::vector<uint8_t> cut = make_gif(40, 40, 4, 5);
        cut.resize(cut.size() / 2);
        const bool cut_ok = vis::decode_gif(cut, 180, 0, a, &err);
        CHECK(!cut_ok || (a.frames >= 1 && a.frames < 4), "a cut file: the frames before the cut, or refused (never a crash)");

        const fs::path real = real_resources();
        if (real.empty()) {
            std::printf("  (RDBM26 folder not on this PC: runstyle_0.gif not checked)\n");
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(vis::decode_gif_file(real / "runstyle" / "runstyle_0.gif", 180, size_t(16) << 20, a, &err), "RDBM26 runstyle_0.gif decodes: " + err);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(a.frames > 1, fmt("runstyle_0.gif has more than one frame (%d)", a.frames));
        std::printf("  RDBM26 runstyle_0.gif: %d frames, %dx%d -> %dx%d, %d ms loop, decoded in %.0f ms (worker thread in the window)\n", a.frames,
                    a.src_w, a.src_h, a.frame_w, a.frame_h, a.total_ms, ms);
        if (fs::exists(real / "fkanim" / "fkanim_7.gif")) {
            CHECK(vis::decode_gif_file(real / "fkanim" / "fkanim_7.gif", 180, size_t(16) << 20, a, &err) && a.frames > 100 && a.bytes() <= (size_t(16) << 20),
                  fmt("fkanim_7.gif: %d frames in %zu bytes", a.frames, a.bytes()));
        }
    });

    run_case("Visual pickers: the animation store decodes on its thread, keeps to its budget, forgets the oldest", [] {
        const fs::path d = test_tmp_dir("visual_store");
        std::error_code ec;
        fs::remove_all(d, ec);
        for (int i = 0; i < 6; ++i) write_bytes(d / ("a" + std::to_string(i) + ".gif"), make_gif(100, 100, 10, 4, i));
        const size_t one = size_t(100) * 100 * 4 * 10;  // one decoded animation
        vis::AnimStore s(one * 2 * 3 + 100);              // room for three (each counted twice)
        vis::AnimStore::Item it = s.get(d / "a0.gif", 180, 0);
        CHECK(!it.anim && !it.failed, "first ask: queued, nothing yet");
        CHECK(s.wait_idle(10.0), "worker idle");
        it = s.get(d / "a0.gif", 180, 0);
        CHECK(it.anim && it.anim->frames == 10, "then decoded");
        for (int i = 1; i < 6; ++i) s.get(d / ("a" + std::to_string(i) + ".gif"), 180, 0);
        CHECK(s.wait_idle(10.0), "worker idle after five more");
        for (int i = 1; i < 6; ++i) s.get(d / ("a" + std::to_string(i) + ".gif"), 180, 0);  // done: counted, oldest dropped
        CHECK(s.bytes() <= s.budget(), fmt("budget kept (%zu <= %zu)", s.bytes(), s.budget()));
        const std::vector<std::string> ev = s.take_evicted();
        CHECK(ev.size() >= 3, fmt("the oldest were dropped (%zu)", ev.size()));
        CHECK(s.take_evicted().empty(), "evicted keys handed out once");
        it = s.get(d / "missing.gif", 180, 0);
        s.wait_idle(10.0);
        it = s.get(d / "missing.gif", 180, 0);
        CHECK(it.failed && !it.anim, "a missing file fails (no crash, no picture)");
        // many asks then a cancel: nothing left queued, the store still answers
        for (int i = 0; i < 6; ++i) s.get(d / ("b" + std::to_string(i) + ".gif"), 90, 0);
        s.cancel_queued();
        CHECK(s.queued_count() == 0, "cancel empties the queue");
    });

    run_case("Visual pickers: the RDBM26 folder (root or Resources, the setting, missing = names only)", [] {
        const fs::path d = test_tmp_dir("visual_folder");
        std::error_code ec;
        fs::remove_all(d, ec);
        write_bytes(d / "RDBM" / "Resources" / "runstyle" / "runstyle_0.gif", make_gif(8, 8, 2, 5));
        CHECK(vis::resources_dir(d / "RDBM") == d / "RDBM" / "Resources", "the RDBM26 folder: its Resources");
        CHECK(vis::resources_dir(d / "RDBM" / "Resources") == d / "RDBM" / "Resources", "the Resources folder itself");
        CHECK(vis::resources_dir(d / "nowhere").empty() && vis::resources_dir("").empty(), "a missing folder: none");
        const fs::path to = d / "turbo_output";
        CHECK(vis::load_folder_setting(to).empty(), "no setting yet");
        CHECK(vis::save_folder_setting(to, (d / "RDBM").u8string()) && vis::load_folder_setting(to) == (d / "RDBM").u8string(), "setting round trip");
        CHECK(vis::find_resources(to) == d / "RDBM" / "Resources", "the setting is used");
        CHECK(vis::save_folder_setting(to, (d / "gone").u8string()) && vis::find_resources(to).empty(), "a setting to a missing folder: none");
        const vis::SetDef* rs = vis::find_set("runstyle");
        const fs::path res = d / "RDBM" / "Resources";
        CHECK(vis::picture_file(res, *rs, *vis::find_code(*rs, 0)) == res / "runstyle" / "runstyle_0.gif", "the picture of code 0");
        CHECK(vis::picture_file(res, *rs, *vis::find_code(*rs, 21)).empty(), "a picture not in the folder: none");
        CHECK(vis::picture_file(fs::path(), *rs, *vis::find_code(*rs, 0)).empty(), "no folder: no picture");
    });
}

// Every field of the record as text (the "only that field" check)
inline std::vector<std::string> record_values(turbo::App& app, const turbo::Table& t, uint64_t rec) {
    std::vector<std::string> v;
    for (const std::string& n : t.field_names()) {
        turbo::Value x;
        app.db.get(t, rec, *t.field(n), x);
        v.push_back(n + "=" + x.to_string());
    }
    return v;
}

inline void ui_cases(turbo::App& app, Ui& ui, const fs::path& le) {
    using namespace turbo;
    const Table* p = app.db.table("players");
    const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
    auto draw_section = [&app, p, rec]() {
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
        if (ImGui::Begin("Visual pickers test")) visual_pickers_section(app, *p, rec);
        ImGui::End();
    };

    run_case("UI: visual picker writes the picked run style into runstylecode and nothing else (App::edit)", [&] {
        CHECK(p && p->has("runstylecode") && rec != 0, "the test world has runstylecode and player 2002");
        if (!p || !rec) return;
        visual_pictures_override((test_tmp_dir("visual_none") / "nowhere").u8string());
        ui.extra_draw = draw_section;
        ui.frames(2);
        const int64_t before_code = app.db.get_int(*p, rec, "runstylecode", -1);
        const std::vector<std::string> before = record_values(app, *p, rec);
        visual_picker_request("runstylecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "runstylecode", "the run-style picker is open");
        CHECK(visual_picker_ui().current == before_code, "his current value is marked");
        visual_picker_select(21);
        CHECK(visual_picker_apply(app), "Apply writes: " + visual_picker_ui().last_result);
        CHECK(visual_picker_ui().last_result == "Run style set to Medalist", "result in words: " + visual_picker_ui().last_result);
        CHECK(app.db.get_int(*p, rec, "runstylecode", -1) == 21, "runstylecode = 21 (read back)");
        const std::vector<std::string> after = record_values(app, *p, rec);
        int diffs = 0;
        std::string which;
        for (size_t i = 0; i < before.size() && i < after.size(); ++i)
            if (before[i] != after[i]) ++diffs, which += after[i] + " ";
        CHECK(before.size() == after.size() && diffs == 1 && which.rfind("runstylecode=21", 0) == 0, "only runstylecode changed: " + which);
        // a tattoo slot: the same, and only that slot
        const std::vector<std::string> b2 = record_values(app, *p, rec);
        std::string msg;
        CHECK(visual_apply(app, *p, rec, "tattoohead", 3, &msg), "tattoo head written: " + msg);
        const std::vector<std::string> a2 = record_values(app, *p, rec);
        diffs = 0;
        for (size_t i = 0; i < b2.size(); ++i) diffs += b2[i] != a2[i];
        CHECK(diffs == 1 && app.db.get_int(*p, rec, "tattoohead", -1) == 3 && app.db.get_int(*p, rec, "tattooleftarm", -1) == 0,
              "only the head tattoo changed");
        CHECK(!visual_apply(app, *p, rec, "notafield", 1, &msg), "a field the table does not have: refused");
        visual_picker_close();
        ui.frames(2);
        // back as it was (later cases read this player)
        visual_apply(app, *p, rec, "tattoohead", 0);
        visual_apply(app, *p, rec, "runstylecode", before_code);
    });

    run_case("UI: visual picker without the RDBM26 folder is a names-only list that still writes", [&] {
        if (!p || !rec) return;
        visual_pictures_override((test_tmp_dir("visual_none") / "nowhere").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        visual_picker_request("runstylecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().names_only, "names only");
        CHECK(visual_picker_ui().tiles_drawn >= 10, fmt("names listed (%d)", visual_picker_ui().tiles_drawn));
        CHECK(ui.find("Head Down", "###vispicker") != nullptr || ui.find("Medalist", "###vispicker") != nullptr || visual_picker_ui().tiles_drawn > 0,
              "in-game names in the list");
        const int64_t was = app.db.get_int(*p, rec, "runstylecode", -1);
        visual_picker_select(8);
        CHECK(visual_picker_apply(app) && app.db.get_int(*p, rec, "runstylecode", -1) == 8, "Head Down written without pictures");
        visual_apply(app, *p, rec, "runstylecode", was);
        visual_picker_close();
        ui.frames(2);
    });

    // The perf case: the frames of this thread while the run-style picker opens and its animations decode and upload
    auto measure = [&](const std::string& res_dir, const char* label) {
        visual_pickers_shutdown();  // a fresh store: every animation is decoded during the measured frames
        visual_pictures_override(res_dir);
        visual_pictures_refresh(app);
        double worst = 0.0, total = 0.0, worst_frame = 0.0;
        int frames = 0, max_uploads = 0;
        auto timed = [&]() {
            const auto t0 = std::chrono::steady_clock::now();
            draw_section();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            worst = std::max(worst, ms);
            total += ms;
            ++frames;
            max_uploads = std::max(max_uploads, visual_picker_ui().uploads_last_frame);
        };
        ui.extra_draw = timed;
        visual_picker_request("runstylecode");
        int shown = 0;
        const auto start = std::chrono::steady_clock::now();
        // frames paced at about 120 per second (a real window draws at 60), for at most 10 s: the decoding thread gets real time
        for (int k = 0; k < 1200; ++k) {
            const auto f0 = std::chrono::steady_clock::now();
            ui.frame();
            const double fms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count();
            worst_frame = std::max(worst_frame, fms);
            shown = visual_picker_ui().pictures_shown;
            if (k > 5 && shown > 0 && shown == visual_picker_ui().tiles_drawn) break;
            if (fms < 8.0) std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>((8.0 - fms) * 1000.0)));
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("  PERF %s: run-style picker open, %d frames: picker draw max %.2f ms, mean %.2f ms; whole test frame max %.1f ms; "
                    "%d of %d tiles animated after %.1f s; at most %d new animations per frame\n",
                    label, frames, worst, frames ? total / frames : 0.0, worst_frame, shown, visual_picker_ui().tiles_drawn, secs, max_uploads);
        CHECK(visual_picker_ui().open && !visual_picker_ui().names_only, std::string(label) + ": picker open with pictures");
        CHECK(shown > 0, fmt("%s: animations shown (%d)", label, shown));
        CHECK(worst <= 16.0, fmt("%s: no frame of the picker over 16 ms on this thread (worst %.2f ms)", label, worst));
        CHECK(max_uploads <= 2, "at most 2 new animations per frame");
        visual_picker_close();
        ui.frames(2);
    };

    run_case("UI: visual picker PERF: opening the run-style picker never takes more than 16 ms of a frame (22 real-size animations)", [&] {
        if (!p || !rec) return;
        const fs::path fake = make_fake_rdbm(test_tmp_dir("visual_fake_rdbm"));
        measure((fake / "Resources").u8string(), "synthetic 22 x 336x576x29");
        const fs::path real = real_resources();
        if (real.empty()) std::printf("  (RDBM26 folder not on this PC: real-folder perf not measured)\n");
        else measure(real.u8string(), "RDBM26 folder");
    });

    ui.extra_draw = nullptr;
    visual_pictures_override("");
    visual_pickers_shutdown();
    (void)le;
}

}  // namespace visual_picker_test
