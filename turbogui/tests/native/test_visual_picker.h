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
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/asset_catalog.h"
#include "core/rdbm_resources.h"
#include "ui/ui_images.h"
#include "ui/ui_setpiece_card.h"
#include "ui/ui_visual_picker.h"

#ifdef _WIN32
#include <x86intrin.h>
// The perf case measures the picker's own work: this thread's CPU time (cycles while it ran), not the wall time a busy PC
// adds by running other programs in between (windows.h is not included by the test program, so the calls are declared here)
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread(void);
extern "C" __declspec(dllimport) int __stdcall QueryThreadCycleTime(void* thread, unsigned long long* cycles);
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* thread, int priority);
extern "C" __declspec(dllimport) int __stdcall GetThreadPriority(void* thread);
#else
#include <ctime>
#endif

namespace visual_picker_test {

// This thread's CPU time in ms. Windows: QueryThreadCycleTime counts time-stamp-counter ticks while the thread runs; the
// counter's ticks per ms are measured once against the steady clock (the counter runs whatever the scheduler does).
inline double thread_cpu_ms() {
#ifdef _WIN32
    static const double ticks_per_ms = [] {
        const auto t0 = std::chrono::steady_clock::now();
        const unsigned long long r0 = __rdtsc();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(50)) {
        }
        const unsigned long long r1 = __rdtsc();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return static_cast<double>(r1 - r0) / ms;
    }();
    unsigned long long c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return static_cast<double>(c) / ticks_per_ms;
#else
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

// The game's window thread runs at normal priority; the build lock runs the tests at BelowNormal next to other builds
struct NormalPriority {
#ifdef _WIN32
    int old = 0;
    NormalPriority() : old(GetThreadPriority(GetCurrentThread())) { SetThreadPriority(GetCurrentThread(), 2); }  // + HIGHEST
    ~NormalPriority() { SetThreadPriority(GetCurrentThread(), old); }
#endif
};

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
    run_case("Visual pickers: the name mapping loads (21 sets, 33 fields, codes ascending, every field's set found)", [] {
        CHECK(vis::set_count() == 21, fmt("21 picture sets (%zu)", vis::set_count()));
        const std::vector<std::string> fields = vis::mapped_fields();
        CHECK(fields.size() == 33, fmt("33 fields (%zu)", fields.size()));
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
        CHECK(codes == 3040 && named == 1396, fmt("3040 codes, 1396 with an in-game name (%zu, %zu)", codes, named));
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

// ---- 2.22 G-pickers: the census (Cassio's players table export) read-only, for the coverage counts
inline std::vector<std::vector<std::string>> read_census(std::vector<std::string>& header) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream f("C:/FC 27 Live Editor/turbo_output/turbo_table_players.csv", std::ios::binary);
    std::string line;
    if (!f || !std::getline(f, line)) return rows;
    auto split = [](const std::string& l) {
        std::vector<std::string> v;
        std::string cur;
        for (char ch : l) {
            if (ch == ',') v.push_back(cur), cur.clear();
            else if (ch != '\r') cur += ch;
        }
        v.push_back(cur);
        return v;
    };
    header = split(line);
    while (std::getline(f, line)) rows.push_back(split(line));
    return rows;
}

inline int census_col(const std::vector<std::string>& header, const char* name) {
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i] == name) return static_cast<int>(i);
    return -1;
}

inline void core_cases_222() {
    // The colour decode: expected values from the sRGB formula worked out by hand in Python (not from the function under test)
    run_case("2.22 pickers: JSON colour decode (Ginger 0.15/0.06/0.03 -> sRGB 108/70/51 within 1/255), bad files refused", [] {
        const std::string json =
            "{\"$id\":\"1\",\"Color\":{\"$id\":\"2\",\"x\":0.15031396,\"y\":0.061464954,\"z\":0.033104766},"
            "\"LocalizationInfo\":{\"StringId\":\"ID_HEAD_PRESET_HAIRCOLOR_GINGER\"}}";
        vis::Rgb8 c;
        std::string err;
        CHECK(vis::decode_colour_json(std::vector<uint8_t>(json.begin(), json.end()), c, &err), "Ginger decodes: " + err);
        CHECK(std::abs(c.r - 108) <= 1 && std::abs(c.g - 70) <= 1 && std::abs(c.b - 51) <= 1, fmt("Ginger = 108/70/51 (%d/%d/%d)", c.r, c.g, c.b));
        const std::string black = "{\"Color\":{\"x\":0,\"y\":0,\"z\":0}}", white = "{\"Color\":{\"x\":1.0,\"y\":1.0,\"z\":1.0}}";
        CHECK(vis::decode_colour_json(std::vector<uint8_t>(black.begin(), black.end()), c) && c.r == 0 && c.g == 0 && c.b == 0, "black is 0/0/0");
        CHECK(vis::decode_colour_json(std::vector<uint8_t>(white.begin(), white.end()), c) && c.r == 255 && c.g == 255 && c.b == 255, "1.0 is 255");
        const std::string over = "{\"Color\":{\"x\":3.0,\"y\":-1.0,\"z\":0.5}}";
        CHECK(vis::decode_colour_json(std::vector<uint8_t>(over.begin(), over.end()), c) && c.r == 255 && c.g == 0 && std::abs(c.b - 188) <= 1,
              fmt("out of range values are clamped, 0.5 = 188 (%d/%d/%d)", c.r, c.g, c.b));
        const std::string bad = "{\"Colour\":1}", junk = "PNG not json";
        CHECK(!vis::decode_colour_json(std::vector<uint8_t>(bad.begin(), bad.end()), c, &err) && !err.empty(), "no Color object: refused with a reason");
        CHECK(!vis::decode_colour_json(std::vector<uint8_t>(junk.begin(), junk.end()), c), "not JSON: refused");
        // as a picture file the worker decodes: one 1 x 1 pixel
        const fs::path d = test_tmp_dir("visual_colour");
        std::error_code ec;
        fs::remove_all(d, ec);
        write_bytes(d / "haircolor_12.json", std::vector<uint8_t>(json.begin(), json.end()));
        vis::Anim a;
        CHECK(vis::is_colour_file(d / "haircolor_12.json") && !vis::is_colour_file(d / "haircolor_1.png"), "a .json is a colour file");
        CHECK(vis::decode_picture_file(d / "haircolor_12.json", 128, 0, a, &err) && a.frames == 1 && a.atlas.w == 1 && std::abs(a.atlas.px[0] - 108) <= 1 && std::abs(a.atlas.px[2] - 51) <= 1 && a.atlas.px[3] == 255,
              "the worker gives the colour file as one opaque pixel: " + err);
        CHECK(!vis::decode_picture_file(d / "nowhere.json", 128, 0, a, &err), "a missing colour file: no picture, no crash");
    });

    run_case("2.22 pickers: eyebrows - every code maps to its style picture (60101 -> brow_60000, 2150101 -> brow_2150000)", [] {
        const vis::SetDef* eb = vis::find_set("eyebrow");
        CHECK(eb != nullptr, "the eyebrow set exists");
        if (!eb) return;
        CHECK(vis::eyebrow_style(60101) == 60000 && vis::eyebrow_style(2150101) == 2150000 && vis::eyebrow_style(0) == 0 &&
                  vis::eyebrow_style(10000) == 10000,
              "the style of a code: code / 10000 * 10000");
        for (int64_t code : {int64_t(60101), int64_t(2150101), int64_t(60201), int64_t(2190101)}) {
            const vis::CodeDef* c = vis::find_code_or_style(*eb, code);
            CHECK(c && c->file && std::string(c->file) == "brow_" + std::to_string(vis::eyebrow_style(code)) + ".png",
                  fmt("eyebrow %lld has its style's picture (%s)", static_cast<long long>(code), c && c->file ? c->file : "none"));
        }
        // a code nobody listed still maps through its style (the picker never shows a style's variant as 'no picture')
        const vis::CodeDef* unseen = vis::find_code_or_style(*eb, 60199);
        CHECK(unseen && unseen->file && std::string(unseen->file) == "brow_60000.png", "an unlisted variant maps to its style");
        CHECK(vis::find_code_or_style(*vis::find_set("haircolor"), 99) == nullptr, "only eyebrows map through a style");
        // the set lists the variants: grouped by style, ascending
        CHECK(eb->count > 400, fmt("the eyebrow set lists the census and catalog codes (%d)", eb->count));
        for (int i = 1; i < eb->count; ++i) CHECK(vis::codes_begin(*eb)[i - 1].code < vis::codes_begin(*eb)[i].code, "ascending");
        // against Cassio's census when it is on this PC: every used code has a picture file in his RDBM26 folder
        std::vector<std::string> head;
        const auto rows = read_census(head);
        const int col = census_col(head, "eyebrowcode");
        const fs::path real = real_resources();
        if (rows.empty() || col < 0 || real.empty()) {
            std::printf("  (census or RDBM26 folder not on this PC: eyebrow coverage not counted)\n");
            return;
        }
        std::set<int64_t> used;
        size_t before = 0, after = 0;
        for (const auto& r : rows) {
            const int64_t code = std::atoll(r[static_cast<size_t>(col)].c_str());
            used.insert(code);
            std::error_code ec;
            const bool old_rule = fs::is_regular_file(real / "eyebrow" / ("brow_" + std::to_string(code) + ".png"), ec);
            const vis::CodeDef* c = vis::find_code_or_style(*eb, code);
            const bool new_rule = c && !vis::picture_file(real, *eb, *c).empty();
            before += !old_rule;
            after += !new_rule;
        }
        std::printf("  COVERAGE eyebrows: %zu used codes; players without a picture %zu -> %zu of %zu\n", used.size(), before, after, rows.size());
        CHECK(after == 0, fmt("every used eyebrow code has a picture (%zu players without)", after));
    });

    run_case("2.22 pickers: boots and gloves - per-file fallback (boots 165 and 80 only exist in shoes\\)", [] {
        const fs::path d = test_tmp_dir("visual_fallback");
        std::error_code ec;
        fs::remove_all(d, ec);
        const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
        write_bytes(d / "Resources" / "FC26_shoes" / "shoe_100_0.png", png);
        write_bytes(d / "Resources" / "shoes" / "shoe_100_0.png", png);
        write_bytes(d / "Resources" / "shoes" / "shoe_165_0.png", png);  // only the old folder has 165
        write_bytes(d / "Resources" / "FC26_gkgloves" / "gkglove_1.png", png);
        write_bytes(d / "Resources" / "gkgloves" / "gkglove_80.png", png);
        const fs::path res = d / "Resources";
        const vis::SetDef* boots = vis::find_set("boots");
        const vis::SetDef* gloves = vis::find_set("gloves");
        CHECK(boots && gloves, "boot and glove sets");
        if (!boots || !gloves) return;
        const vis::CodeDef* b165 = vis::find_code(*boots, 165);
        const vis::CodeDef* b80 = vis::find_code(*boots, 80);
        const vis::CodeDef* g80 = vis::find_code(*gloves, 80);
        CHECK(b165 && b80 && g80, "ids 165 and 80 are listed (the names data takes the codes of both folders)");
        if (!b165 || !b80 || !g80) return;
        vis::FolderIndex idx;
        for (vis::FolderIndex* ix : {static_cast<vis::FolderIndex*>(nullptr), &idx}) {
            CHECK(vis::picture_file(res, *boots, *vis::find_code(*boots, 100), ix) == res / "FC26_shoes" / "shoe_100_0.png", "boot 100: FC26_ first");
            CHECK(vis::picture_file(res, *boots, *b165, ix) == res / "shoes" / "shoe_165_0.png", "boot 165: the file in shoes\\ although FC26_shoes exists");
            CHECK(vis::picture_file(res, *gloves, *g80, ix) == res / "gkgloves" / "gkglove_80.png", "glove 80: the file in gkgloves\\");
            CHECK(vis::picture_file(res, *boots, *b80, ix).empty(), "boot 80 here has no file anywhere: none");
        }
        // the real folder and the census: players without a boot / glove picture, whole-folder rule before and per-file after
        std::vector<std::string> head;
        const auto rows = read_census(head);
        const fs::path real = real_resources();
        const int cb = census_col(head, "shoetypecode"), cg = census_col(head, "gkglovetypecode");
        if (rows.empty() || cb < 0 || cg < 0 || real.empty()) {
            std::printf("  (census or RDBM26 folder not on this PC: boot / glove coverage not counted)\n");
            return;
        }
        vis::FolderIndex ridx;
        size_t b_before = 0, b_after = 0, g_before = 0, g_after = 0, n165 = 0;
        for (const auto& r : rows) {
            const int64_t sb = std::atoll(r[static_cast<size_t>(cb)].c_str()), sg = std::atoll(r[static_cast<size_t>(cg)].c_str());
            struct Row { const vis::SetDef* s; int64_t code; size_t* before; size_t* after; } rr[2] = {{boots, sb, &b_before, &b_after},
                                                                                                     {gloves, sg, &g_before, &g_after}};
            for (Row& x : rr) {
                const vis::CodeDef* c = vis::find_code(*x.s, x.code);
                // before: the whole folder is FC26_ when it exists, the old one only when FC26_ is missing altogether
                std::error_code e2;
                const bool fc26 = fs::is_directory(real / x.s->dir, e2);
                const fs::path old_dir = real / (fc26 ? x.s->dir : x.s->fallback_dir);
                *x.before += !(c && c->file && fs::is_regular_file(old_dir / c->file, e2));
                *x.after += !(c && !vis::picture_file(real, *x.s, *c, &ridx).empty());
            }
            n165 += sb == 165;
        }
        std::printf("  COVERAGE boots: players without a picture %zu -> %zu; gloves %zu -> %zu (of %zu; %zu players wear boot 165)\n", b_before,
                    b_after, g_before, g_after, rows.size(), n165);
        CHECK(b_after < b_before, "boots: the per-file fallback gives pictures to players that had none");
        CHECK(g_after <= g_before, "gloves: never fewer pictures");
    });

    run_case("2.22 pickers: accessory colours (Accessory_<colour>_<accessory>.png, plain fallback, named swatch)", [] {
        const fs::path d = test_tmp_dir("visual_acc");
        std::error_code ec;
        fs::remove_all(d, ec);
        const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
        write_bytes(d / "Resources" / "Accessory" / "Accessory_6.png", png);
        write_bytes(d / "Resources" / "Accessory" / "Accessory_3_6.png", png);
        write_bytes(d / "Resources" / "Accessory" / "Accessory_0_6.png", png);
        write_bytes(d / "Resources" / "Accessory" / "Accessory_16.png", png);
        const fs::path res = d / "Resources";
        vis::FolderIndex idx;
        CHECK(vis::accessory_picture(res, 3, 6, &idx) == res / "Accessory" / "Accessory_3_6.png", "colour 3, accessory 6: the coloured picture");
        CHECK(vis::accessory_picture(res, 0, 6, &idx) == res / "Accessory" / "Accessory_0_6.png", "colour 0 has its own picture");
        CHECK(vis::accessory_picture(res, 9, 6, &idx) == res / "Accessory" / "Accessory_6.png", "a colour with no file: the plain accessory picture");
        CHECK(vis::accessory_picture(res, 1, 16, &idx) == res / "Accessory" / "Accessory_16.png", "accessory with only the plain picture");
        CHECK(vis::accessory_picture(res, 1, 0, &idx).empty() && vis::accessory_picture(res, 1, 77, &idx).empty(), "no accessory or no file: none");
        CHECK(vis::accessory_picture({}, 1, 6).empty(), "no folder: none");
        vis::Rgb8 c;
        CHECK(vis::accessory_colour_swatch(1, c) && c.r < 40 && c.g < 40 && c.b < 40, "colour 1 (Black) is dark");
        CHECK(vis::accessory_colour_swatch(3, c) && c.r > 180 && c.g < 80, "colour 3 (Red) is red");
        CHECK(!vis::accessory_colour_swatch(99, c) && !vis::accessory_colour_swatch(-1, c), "99 (none) has no swatch");
        const vis::SetDef* ac = vis::find_set("accessorycolour");
        CHECK(ac && vis::set_for_field("accessorycolourcode1") == ac && vis::set_for_field("accessorycolourcode4") == ac, "the four colour fields map to the set");
        CHECK(ac && vis::code_name(*ac, 3) == "Red" && vis::code_name(*ac, 0) == "White" && vis::code_name(*ac, 99) == "None", "LE's colour names");
        // the real folder: the player's current accessory in his colour, counted over the census
        std::vector<std::string> head;
        const auto rows = read_census(head);
        const fs::path real = real_resources();
        if (rows.empty() || real.empty()) {
            std::printf("  (census or RDBM26 folder not on this PC: accessory colour coverage not counted)\n");
            return;
        }
        size_t worn = 0, with_pic = 0, with_exact = 0;
        vis::FolderIndex ridx;
        for (int n = 1; n <= 4; ++n) {
            const int ca = census_col(head, ("accessorycode" + std::to_string(n)).c_str());
            const int cc = census_col(head, ("accessorycolourcode" + std::to_string(n)).c_str());
            if (ca < 0 || cc < 0) continue;
            for (const auto& r : rows) {
                const int64_t acc = std::atoll(r[static_cast<size_t>(ca)].c_str()), col = std::atoll(r[static_cast<size_t>(cc)].c_str());
                if (acc <= 0) continue;
                ++worn;
                const fs::path p = vis::accessory_picture(real, col, acc, &ridx);
                with_pic += !p.empty();
                with_exact += !p.empty() && p.filename().u8string() != "Accessory_" + std::to_string(acc) + ".png";
            }
        }
        std::printf("  COVERAGE accessory colours: %zu worn accessories (4 slots); picture %zu (in his colour %zu), none %zu; before 2.22: 0 pictures (colour not wired)\n",
                    worn, with_pic, with_exact, worn - with_pic);
        CHECK(with_pic > 0, "accessory pictures found");
    });

    run_case("2.22 pickers: generic heads (headtypecode) - the key is verified, gheads\\<n>.png", [] {
        const vis::SetDef* gh = vis::find_set("ghead");
        CHECK(gh && vis::set_for_field("headtypecode") == gh, "headtypecode -> the generic head set");
        if (!gh) return;
        CHECK(gh->count >= 600 && std::string(gh->dir) == "gheads", fmt("the set lists the RDBM26 files and the census codes (%d)", gh->count));
        const vis::CodeDef* c = vis::find_code(*gh, 1024);
        CHECK(c && c->file && std::string(c->file) == "1024.png", "code 1024 -> 1024.png");
        std::vector<std::string> head;
        const auto rows = read_census(head);
        const int col = census_col(head, "headtypecode"), cs = census_col(head, "skintonecode");
        const fs::path real = real_resources();
        if (rows.empty() || col < 0 || real.empty()) {
            std::printf("  (census or RDBM26 folder not on this PC: headtypecode evidence not counted)\n");
            return;
        }
        std::set<int64_t> used, with_file;
        size_t players_no = 0;
        vis::FolderIndex idx;
        for (const auto& r : rows) {
            const int64_t code = std::atoll(r[static_cast<size_t>(col)].c_str());
            used.insert(code);
            const vis::CodeDef* cd = vis::find_code(*gh, code);
            const bool has = cd && !vis::picture_file(real, *gh, *cd, &idx).empty();
            if (has) with_file.insert(code);
            else ++players_no;
        }
        std::printf("  EVIDENCE headtypecode: %zu used codes, %zu with a gheads file (%zu players of %zu without); before 2.22: no picker\n", used.size(),
                    with_file.size(), players_no, rows.size());
        CHECK(used.size() > 500, fmt("the census uses about 529 head type codes (%zu)", used.size()));
        CHECK(with_file.size() >= 520, fmt("at least 520 of the used codes have a gheads picture (%zu)", with_file.size()));
        // the group names come from RDBM26's own *Models.txt lists: African heads belong to dark skin tones, Caucasian to light ones
        if (cs >= 0) {
            double af = 0, afn = 0, ca = 0, can = 0;
            for (const auto& r : rows) {
                const vis::CodeDef* cd = vis::find_code(*gh, std::atoll(r[static_cast<size_t>(col)].c_str()));
                if (!cd || !cd->name) continue;
                const std::string nm = cd->name;
                const double tone = std::atof(r[static_cast<size_t>(cs)].c_str());
                if (nm.rfind("African", 0) == 0) af += tone, ++afn;
                if (nm.rfind("Caucasian", 0) == 0) ca += tone, ++can;
            }
            CHECK(afn > 100 && can > 100 && af / afn > ca / can + 30, fmt("African heads' mean skin tone %.0f vs Caucasian %.0f: the key is the head type", af / std::max(1.0, afn), ca / std::max(1.0, can)));
        }
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
        NormalPriority prio;
        thread_cpu_ms();  // the calibration, before the measured frames
        double worst = 0.0, worst_wall = 0.0, total = 0.0, worst_frame = 0.0;
        int frames = 0, max_uploads = 0, over = 0, over_wall = 0;
        std::string spikes;
        auto timed = [&]() {
            const double c0 = thread_cpu_ms();
            const auto t0 = std::chrono::steady_clock::now();
            draw_section();
            const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const double cpu = thread_cpu_ms() - c0;
            const int up = visual_picker_ui().uploads_last_frame;
            if (cpu > 16.0 || wall > 16.0) {
                if (spikes.size() < 1500)
                    spikes += fmt("    PERF %s frame %d: picker work %.2f ms, wall %.2f ms, %d new animations, %d tiles drawn, %d shown%s\n",
                                  label, frames, cpu, wall, up, visual_picker_ui().tiles_drawn, visual_picker_ui().pictures_shown,
                                  frames == 0 ? " (the opening frame: every row and the picker list built)" : "");
            }
            worst = std::max(worst, cpu);
            worst_wall = std::max(worst_wall, wall);
            over += cpu > 16.0;
            over_wall += wall > 16.0;
            total += cpu;
            ++frames;
            max_uploads = std::max(max_uploads, up);
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
        std::printf("  PERF %s: run-style picker open, %d frames: picker work max %.2f ms, mean %.2f ms, %d frames over 16 ms; "
                    "wall time (information: includes other programs on this PC) max %.2f ms, %d frames over 16 ms; whole test frame max %.1f ms; "
                    "%d of %d tiles animated after %.1f s; at most %d new animations per frame\n",
                    label, frames, worst, frames ? total / frames : 0.0, over, worst_wall, over_wall, worst_frame, shown,
                    visual_picker_ui().tiles_drawn, secs, max_uploads);
        std::printf("%s", spikes.c_str());
        CHECK(visual_picker_ui().open && !visual_picker_ui().names_only, std::string(label) + ": picker open with pictures");
        CHECK(shown > 0, fmt("%s: animations shown (%d)", label, shown));
        CHECK(worst <= 16.0, fmt("%s: no frame of picker work over 16 ms on this thread (worst %.2f ms)", label, worst));
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

    // ---- 2.22 G-pickers: the merged grid, the catalog gate, the set-pieces card, the inline field
    run_case("UI: 2.22 merged hair grid: RDBM picture, the game preview and a grey tile in one field; no RDBM folder = previews and names", [&] {
        if (!p || !rec || !p->has("hairtypecode")) {
            CHECK(false, "the test world has hairtypecode");
            return;
        }
        const vis::SetDef* hs = vis::find_set("hairstyle");
        CHECK(hs != nullptr, "the hair names set");
        if (!hs) return;
        int64_t a = 0, b = 0;  // two named hair codes the game list below does not carry
        const vis::CodeDef* cb = vis::codes_begin(*hs);
        for (int i = 0; i < hs->count && !(a && b); ++i) {
            const int64_t c = cb[i].code;
            if (c <= 0 || c == 3261 || c == 1053) continue;
            (a ? b : a) = c;
        }
        CHECK(a > 0 && b > 0, "two named hair codes");
        const fs::path hash = le / "legacy_filename_hash_list.csv";
        const std::string hash_before = fs::exists(hash) ? read_file(hash) : std::string();
        {
            std::ofstream h(hash);
            h << "hash;file_name\n1;data/ui/imgAssets/hairstyle/item_3261_0.dds\n2;data/ui/imgAssets/hairstyle/item_1053_0.dds\n";
        }
        app.request_tab = 0;
        ui.frames(2);
        const fs::path root = test_tmp_dir("merged_rdbm");
        const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
        write_bytes(root / "Resources" / "hairstyle" / fmt("item_%lld_0.png", static_cast<long long>(a)), png);
        visual_pictures_override((root / "Resources").u8string());
        visual_pictures_refresh(app);
        ui.extra_draw = draw_section;
        ui.frames(2);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode" && !visual_picker_ui().names_only, "the hair grid is open with pictures");
        CHECK(visual_picker_tile_source(a) == TileSource::Rdbm, "a hair code with an RDBM26 file: its RDBM26 picture");
        CHECK(visual_picker_tile_source(3261) == TileSource::Game && visual_picker_tile_source(1053) == TileSource::Game,
              "hair codes the game lists without an RDBM26 file: the game preview");
        CHECK(visual_picker_tile_source(b) == TileSource::Placeholder, "a named hair code with neither: the grey tile");
        const VisualPickerUi& vu = visual_picker_ui();
        CHECK(vu.source_rdbm >= 1 && vu.source_game >= 2 && vu.source_blank >= 1,
              fmt("one grid, three sources (rdbm %d, game %d, grey %d)", vu.source_rdbm, vu.source_game, vu.source_blank));
        CHECK(visual_picker_list_size() > 700, fmt("every named hair code and the game ids are in the one list (%zu)", visual_picker_list_size()));

        // PERF: opening the merged hair grid never takes more than 16 ms of a frame on this thread
        visual_picker_close();
        ui.frames(2);
        {
            NormalPriority prio;
            thread_cpu_ms();
            double worst = 0.0;
            int worst_frame = 0, frame = 0;
            auto timed = [&]() {
                const double c0 = thread_cpu_ms();
                draw_section();
                const double cpu = thread_cpu_ms() - c0;
                if (cpu > worst) worst = cpu, worst_frame = frame;
                ++frame;
            };
            ui.extra_draw = timed;
            visual_picker_request("hairtypecode");
            for (int k = 0; k < 40; ++k) {
                ui.frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            std::printf("  PERF merged hair grid: %zu tiles, picker work max %.2f ms (frame %d of %d)\n", visual_picker_list_size(), worst, worst_frame, frame);
            CHECK(visual_picker_ui().open, "the hair grid stayed open");
            CHECK(worst <= 16.0, fmt("no frame of picker work over 16 ms opening the merged hair grid (worst %.2f ms, frame %d)", worst, worst_frame));
            ui.extra_draw = draw_section;
        }
        visual_picker_close();
        ui.frames(2);

        // no RDBM26 folder: the game previews and the names, no crash, and it still writes
        visual_pictures_override((test_tmp_dir("merged_none") / "nowhere").u8string());
        visual_pictures_refresh(app);
        visual_picker_request("hairtypecode");
        ui.frames(3);
        CHECK(visual_picker_ui().open && !visual_picker_ui().names_only, "hair grid open without the RDBM26 folder (it has the game previews)");
        CHECK(visual_picker_ui().source_rdbm == 0 && visual_picker_tile_source(3261) == TileSource::Game &&
                  visual_picker_tile_source(a) == TileSource::Placeholder,
              "previews where the game has them, grey tiles with the name elsewhere");
        CHECK(visual_picker_ui().tiles_drawn > 0, "tiles drawn");
        const int64_t was = app.db.get_int(*p, rec, "hairtypecode", -1);
        visual_picker_select(1053);
        CHECK(visual_picker_apply(app) && app.db.get_int(*p, rec, "hairtypecode", -1) == 1053, "hair 1053 written from the merged grid");
        visual_apply(app, *p, rec, "hairtypecode", was);
        visual_picker_close();
        ui.frames(2);
        if (hash_before.empty()) {
            std::error_code ec;
            fs::remove(hash, ec);
        } else {
            std::ofstream(hash, std::ios::binary | std::ios::trunc) << hash_before;
        }
        app.request_tab = 0;
        ui.frames(2);
        visual_pictures_override("");
        ui.extra_draw = nullptr;
    });

    run_case("UI: 2.22 hair picker refuses an id FC 27 does not ship (the gallery catalog gate) and greys its tile", [&] {
        if (!p || !rec || !p->has("hairtypecode")) return;
        auto cat = std::make_shared<assets::Catalog>();
        std::string cerr;
        CHECK(cat->load_file("tests/fixtures/asset_catalog_v1_small.json", "0000AAAA-0000BBBB", &cerr) && cat->current(), "catalog fixture loaded: " + cerr);
        set_gear_catalog(cat);
        const int64_t was = app.db.get_int(*p, rec, "hairtypecode", -1);
        std::string msg;
        CHECK(!visual_apply(app, *p, rec, "hairtypecode", 5, &msg) && !msg.empty(), "hair 5 (a preview in the files, no 3D model): refused: " + msg);
        CHECK(app.db.get_int(*p, rec, "hairtypecode", -1) == was, "nothing written by the refusal");
        CHECK(!visual_apply(app, *p, rec, "hairtypecode", 987654, &msg), "an id the game files do not list at all: refused: " + msg);
        CHECK(visual_apply(app, *p, rec, "hairtypecode", 3, &msg) && app.db.get_int(*p, rec, "hairtypecode", -1) == 3, "hair 3 (a model): written: " + msg);
        app.db.set_int(*p, rec, "hairtypecode", was);  // his own value may not be in the fixture catalog: put it back without the gate
        ui.extra_draw = draw_section;
        visual_picker_request("hairtypecode");
        ui.frames(3);
        std::string why;
        CHECK(visual_picker_ui().open && !visual_picker_tile_writable(5, &why) && !why.empty(), "the tile of hair 5 is greyed with a reason: " + why);
        CHECK(visual_picker_tile_writable(3), "the tile of hair 3 can be applied");
        visual_picker_select(5);
        CHECK(!visual_picker_apply(app) && app.db.get_int(*p, rec, "hairtypecode", -1) == was, "Apply on hair 5 refuses and writes nothing");
        visual_picker_close();
        ui.frames(2);
        ui.extra_draw = nullptr;
        set_gear_catalog(nullptr);
    });

    run_case("UI: 2.22 set-pieces card: an inline grid per field, writes only the field of the clicked tile, nothing without a click", [&] {
        if (!p || !rec || !p->has("runstylecode")) {
            CHECK(false, "the test world has runstylecode");
            return;
        }
        visual_pictures_override((test_tmp_dir("visual_none") / "nowhere").u8string());
        visual_pictures_refresh(app);
        auto card = [&]() {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
            if (ImGui::Begin("Set pieces card test")) turbo::setpiece_card::draw(app, 2002);
            ImGui::End();
        };
        ui.extra_draw = card;
        const std::vector<std::string> before = record_values(app, *p, rec);
        ui.frames(4);
        CHECK(record_values(app, *p, rec) == before, "drawing the card writes nothing");
        const vp::GridInfo gi = vp::grid_info("runstylecode");
        CHECK(gi.built && gi.codes >= 20 && gi.tiles > 0, fmt("the run-style grid is inline in the card (%d codes, %d tiles drawn)", gi.codes, gi.tiles));
        CHECK(gi.without_picture == gi.codes && gi.pictures == 0, "no RDBM26 folder: every tile is the grey 'no animation yet' tile, none a raw code");
        CHECK(!vp::grid_info("animpenaltieskickstylecode").built, "a penalty field this database does not have gets no grid (no invented names)");
        const int64_t rs0 = app.db.get_int(*p, rec, "runstylecode", -1);
        vp::grid_select("runstylecode", 21);
        std::string msg;
        CHECK(vp::grid_apply(app, *p, rec, "runstylecode", &msg) && app.db.get_int(*p, rec, "runstylecode", -1) == 21, "Apply writes the chosen run style: " + msg);
        const std::vector<std::string> after = record_values(app, *p, rec);
        int diffs = 0;
        std::string which;
        for (size_t i = 0; i < before.size() && i < after.size(); ++i)
            if (before[i] != after[i]) ++diffs, which += after[i] + " ";
        CHECK(before.size() == after.size() && diffs == 1 && which.rfind("runstylecode=21", 0) == 0, "only runstylecode changed: " + which);
        ui.frames(2);
        visual_apply(app, *p, rec, "runstylecode", rs0);
        ui.frames(2);
        ui.extra_draw = nullptr;
    });

    run_case("UI: 2.22 inline fields (eyebrows, colours, generic head, hair): a picture or swatch under the name, the grid behind Change", [&] {
        if (!p || !rec) return;
        CHECK(vp::has_picture(app, *p, "hairtypecode"), "hair has a picture (the game gallery) to show inline");
        CHECK(!vp::has_picture(app, *p, "overallrating"), "a number field has none");
        {  // a folder with one eyebrow picture: eyebrows and the colour files are worth drawing inline now
            const fs::path root = test_tmp_dir("inline_rdbm");
            const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
            write_bytes(root / "Resources" / "eyebrow" / "brow_60000.png", png);
            visual_pictures_override((root / "Resources").u8string());
            visual_pictures_refresh(app);
        }
        if (p->has("eyebrowcode")) CHECK(vp::has_picture(app, *p, "eyebrowcode"), "eyebrows have a picture to show inline");
        auto page = [&]() {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(900.0f, 700.0f), ImGuiCond_Always);
            if (ImGui::Begin("Inline fields test")) {
                vp::inline_field(app, *p, rec, "hairtypecode", "Hair");
                vp::inline_appearance(app, *p, rec);
            }
            ImGui::End();
        };
        ui.extra_draw = page;
        ui.frames(4);
        const ItemRec* change = ui.find("Change##vpchange", "Inline fields test");
        CHECK(change != nullptr, "a Change button is drawn");
        if (change) {
            CHECK(ui.click(change), "Change opens the grid");
            ui.frames(3);
            CHECK(visual_picker_ui().open && visual_picker_ui().field == "hairtypecode", "the hair grid is open behind Change");
            visual_picker_close();
            ui.frames(2);
        }
        visual_pictures_override("");
        ui.extra_draw = nullptr;
        ui.frames(2);
    });

    ui.extra_draw = nullptr;
    visual_pictures_override("");
    visual_pickers_shutdown();
    (void)le;
}

}  // namespace visual_picker_test
