// Native tests (2.17 looks, track T): body-type thumbnails (core/bodytype_thumbs.h). The store the grid reads (paths, the cached
// index, managers never get a player picture), the step list (generic first, the other gender and the manager body set skipped),
// the PNG writer (round trip through decode_image), the Tester's request file, and the generator over track C's capture::Sequence
// with a fake capture service and a fake players table: a full run gives his own code back, a run killed half way (the game
// crashed) continues from the manifest and gives back the manifest's original (not the test code he still wears), another career
// is never written, and a new run never overwrites the record of a player who did not get his code back.
// Included by test_main.cpp after test_capture_sequence.h; test_bodytype_thumbs() is called from main after test_capture_sequence().
#pragma once
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "core/bodytype_thumbs.h"
#include "ui/ui_bodytype_thumbs.h"

namespace bodythumbs_test {

namespace bt = turbo::bodytype_thumbs;
namespace tc = turbo::capture;

inline std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline void spit(const fs::path& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

inline fs::path fresh_dir(const char* name) {
    const fs::path d = test_tmp_dir(name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

// A probe file (bodytypes_fc27.json schema) with a few specific codes; codes 1-9 and 11 are named by field_labels.h
inline void load_catalog(turbo::bodytype::Catalog& cat, const fs::path& dir) {
    const fs::path pf = dir / "probe.json";
    spit(pf, R"({"version": 1, "bodytypes": [
      {"code": 2, "players": 50, "gender": -1},
      {"code": 251, "players": 1, "headclasscode": 0, "gender": 0},
      {"code": 20, "players": 2, "headclasscode": 0, "gender": 0},
      {"code": 82, "players": 2, "headclasscode": 0, "gender": -1},
      {"code": 406, "players": 1, "headclasscode": 0, "gender": -1},
      {"code": 418, "players": 1, "headclasscode": 0, "gender": 1},
      {"code": 500, "players": 1, "headclasscode": 0, "gender": -1}]})");
    std::string err;
    cat.clear();
    CHECK(cat.load_probe(pf, &err), "probe loaded: " + err);
}

// The game's capture service: the picture of a request shows the body type he wears at that moment (a flat colour per code)
struct Fake : tc::CaptureService {
    std::function<int64_t()> wearing;
    std::deque<tc::Result> answers;
    std::vector<tc::Request> requests;
    bool busy = false;
    bool answer = true;
    tc::Status status() override {
        tc::Status s;
        s.installed = true;
        s.busy = busy;
        s.available = !busy;
        return s;
    }
    bool request(const tc::Request& r, std::string*) override {
        requests.push_back(r);
        if (!answer) return true;
        tc::Result a;
        a.ok = true;
        a.id = r.id;
        a.format = "PNG 320x320";
        a.image.w = a.image.h = 320;
        const int64_t c = wearing ? wearing() : 0;
        a.image.px.resize(320 * 320 * 4);
        for (size_t i = 0; i < a.image.px.size(); i += 4) {
            a.image.px[i] = static_cast<uint8_t>(c & 0xFF);
            a.image.px[i + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
            a.image.px[i + 2] = static_cast<uint8_t>((i / 4) % 320 * 4 / 5);  // a gradient: not one flat colour
            a.image.px[i + 3] = 255;
        }
        a.bytes = 4096;
        answers.push_back(a);
        return true;
    }
    bool poll(tc::Result& out) override {
        if (answers.empty()) return false;
        out = answers.front();
        answers.pop_front();
        return true;
    }
    void cancel() override { answers.clear(); }
    const tc::Template* learned() override { return nullptr; }
};

// The players table: career -> player -> bodytypecode (a career reload gives every player his saved code again)
struct World {
    fs::path out;
    Fake svc;
    std::string career = "career:LEICESTER";
    std::map<std::string, std::map<int32_t, int64_t>> db;
    int writes = 0;
    std::vector<std::string> writes_seen;  // "career player code"
    double t = 0.0;
    World(const fs::path& o) : out(o) {
        db["career:LEICESTER"] = {{254470, 5}, {189324, 2}};
        db["career:TORINO"] = {{254470, 5}, {189324, 2}};
        svc.wearing = [this]() { return db[career][254470]; };
    }
    bt::GeneratorEnv env() {
        bt::GeneratorEnv e;
        e.turbo_output = out;
        e.svc = &svc;
        e.read_code = [this](int32_t pid, int64_t* code, std::string* err) {
            auto it = db[career].find(pid);
            if (it == db[career].end()) {
                *err = "no such player";
                return false;
            }
            *code = it->second;
            return true;
        };
        e.write_code = [this](int32_t pid, int64_t code, std::string* err) {
            if (code < 1 || code > 512) {
                *err = "out of range";
                return false;
            }
            ++writes;
            writes_seen.push_back(career + " " + std::to_string(pid) + " " + std::to_string(code));
            db[career][pid] = code;
            return true;
        };
        e.career = [this]() { return career; };
        e.clock = [this]() { return t; };
        e.stamp = []() { return std::string("2026-10-17 22:30:00"); };
        return e;
    }
    void run(bt::Generator& g, int max_ticks = 5000) {
        for (int n = 0; n < max_ticks && g.running(); ++n) {
            g.tick();
            t += 0.1;
        }
    }
};

inline bt::Reference ref_of(int32_t pid, int gender = 0, int head_class = 0) {
    bt::Reference r;
    r.player = pid;
    r.team = 95;
    r.gender = gender;
    r.head_class = head_class;
    r.name = "Reference " + std::to_string(pid);
    return r;
}

// A request with route A's source (a game camera; optionally the probe's mode)
inline bt::StartRequest cam_rq(size_t max_steps, int camera = 1, int mode = -1) {
    bt::StartRequest rq;
    rq.source_given = true;
    rq.source.kind = bt::SourceKind::Camera;
    rq.source.camera = camera;
    rq.source.mode = mode;
    rq.max_steps = max_steps;
    return rq;
}

inline bool same_pixels(const turbo::Rgba& a, const turbo::Rgba& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }

}  // namespace bodythumbs_test

static void test_bodytype_thumbs() {
    using namespace bodythumbs_test;

    run_case("body-type thumbnails: PNG writer round trip (random, flat, gradient, 1x1, 1024 picture)", [&] {
        auto round = [&](const turbo::Rgba& img, const std::string& what) {
            const std::vector<uint8_t> png = bt::encode_png(img);
            turbo::Rgba back;
            std::string err;
            CHECK(!png.empty() && turbo::decode_image(png, back, &err), what + ": decodes: " + err);
            CHECK(same_pixels(img, back), what + ": same pixels");
            return png.size();
        };
        uint32_t seed = 12345;
        auto rnd = [&]() { return seed = seed * 1103515245u + 12345u, static_cast<uint8_t>(seed >> 16); };
        turbo::Rgba r;
        r.w = 37, r.h = 23;
        r.px.resize(size_t(r.w) * r.h * 4);
        for (auto& v : r.px) v = rnd();
        round(r, "random 37x23");
        turbo::Rgba flat;
        flat.w = flat.h = 256;
        flat.px.assign(size_t(256) * 256 * 4, 0);
        for (size_t i = 3; i < flat.px.size(); i += 4) flat.px[i] = 255;
        const size_t flat_size = round(flat, "flat 256x256");
        CHECK(flat_size < 4096, "a flat picture compresses (" + std::to_string(flat_size) + " bytes)");
        turbo::Rgba g;
        g.w = 300, g.h = 200;
        g.px.resize(size_t(g.w) * g.h * 4);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x) {
                uint8_t* p = g.at(x, y);
                p[0] = static_cast<uint8_t>(x), p[1] = static_cast<uint8_t>(y), p[2] = static_cast<uint8_t>(x ^ y), p[3] = static_cast<uint8_t>(x + y);
            }
        round(g, "gradient 300x200");
        turbo::Rgba one;
        one.w = one.h = 1;
        one.px = {1, 2, 3, 4};
        round(one, "1x1");
        // a 1024 picture like the game's: plain background, a figure in the middle with noise (long runs and literals)
        turbo::Rgba big;
        big.w = big.h = 1024;
        big.px.assign(size_t(1024) * 1024 * 4, 0);
        for (int y = 0; y < 1024; ++y)
            for (int x = 0; x < 1024; ++x) {
                uint8_t* p = big.at(x, y);
                const bool body = x > 400 && x < 624 && y > 80 && y < 980;
                p[0] = body ? static_cast<uint8_t>(120 + (rnd() & 15)) : 30;
                p[1] = body ? static_cast<uint8_t>(60 + (rnd() & 7)) : 30;
                p[2] = body ? 40 : 34;
                p[3] = 255;
            }
        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<uint8_t> png = bt::encode_png(big);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        turbo::Rgba back;
        CHECK(turbo::decode_image(png, back) && same_pixels(big, back), "1024 picture round trip");
        std::printf("  note: 1024x1024 picture -> %zu bytes PNG in %.0f ms (raw %zu)\n", png.size(), ms, big.px.size());
        CHECK(png.size() < big.px.size() / 2, "the 1024 picture is compressed");
        CHECK(bt::encode_png(turbo::Rgba{}).empty(), "an empty picture gives nothing");
    });

    run_case("body-type thumbnails: paths, the cached index, managers never get a player picture", [&] {
        const fs::path out = fresh_dir("bodythumbs_index");
        CHECK(bt::folder(out) == out / "looks" / "bodytypes", "folder");
        CHECK(bt::grid_file(out, 6) == out / "looks" / "bodytypes" / "6.png", "grid file");
        CHECK(bt::full_file(out, 6) == out / "looks" / "bodytypes" / "full" / "6.png", "full file (stem not all digits for the grid's scan)");
        CHECK(bt::manifest_file(out).filename() == "manifest.json" && bt::request_file(out).filename() == "make.json", "manifest, request");
        bt::Index ix;
        ix.set_root(out);
        CHECK(ix.refresh(0.0) && ix.thumb(6).empty() && ix.size() == 0, "empty until a picture exists");
        turbo::Rgba img;
        img.w = img.h = 512;
        img.px.assign(size_t(512) * 512 * 4, 200);
        std::string file, err;
        CHECK(bt::save_pictures(out, 6, img, &file, &err) && file == "6.png", "saved: " + err + " " + file);
        CHECK(!ix.refresh(1.0) && ix.thumb(6).empty(), "no new listing before the rescan time (no file check per tile)");
        CHECK(ix.refresh(1.0 + bt::kRescanSec) && ix.thumb(6) == bt::grid_file(out, 6), "listed after the rescan time");
        CHECK(ix.thumb(6, true).empty() && ix.thumb(82, true).empty(), "a manager never gets a player picture");
        turbo::Rgba grid, full;
        CHECK(turbo::load_image_file(bt::grid_file(out, 6), grid) && grid.w == bt::kGridWidth && grid.h == bt::kGridWidth, "grid copy 256 wide");
        CHECK(turbo::load_image_file(bt::full_file(out, 6), full) && full.w == 512, "full copy kept at its size (512, not upscaled)");
        spit(bt::folder(out) / "6_old.png", "x");
        spit(bt::folder(out) / "notes.png.txt", "x");
        CHECK(ix.refresh(0.0, true) && ix.size() == 1, "only <code>.png names count");
        CHECK(ix.missing({1, 6, 7}) == std::vector<int64_t>({1, 7}), "missing()");
        // a manifest that says the code failed in the last run hides an older picture
        spit(bt::manifest_file(out), R"({"version":1,"player":254470,"camera":1,"original":5,"codes":{"6":{"state":"failed","attempts":2}}})");
        CHECK(ix.refresh(0.0, true) && ix.thumb(6).empty(), "failed in the manifest -> no picture");
        // the process-wide lookup
        bt::configure(out);
        bt::invalidate();
        CHECK(bt::thumb(6).empty(), "global: hidden by the manifest");
        fs::remove(bt::manifest_file(out));
        bt::invalidate();
        CHECK(bt::thumb(6) == bt::grid_file(out, 6) && bt::thumb(6, true).empty(), "global: found, managers empty");
        bt::configure(fs::path());
    });

    run_case("body-type thumbnails: step list (generic first, other gender, manager bodies, range, limit)", [&] {
        const fs::path dir = fresh_dir("bodythumbs_plan");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, dir);
        bt::PlanOptions o;
        o.gender = 0;
        o.head_class = 0;
        o.allow_risky = true;
        bt::Plan p = bt::plan_steps(cat, o);
        std::vector<int64_t> got;
        for (const auto& s : p.steps) got.push_back(s.code);
        CHECK(got == std::vector<int64_t>({1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 20, 251, 500}), "men: generic 1-9, 11 first, then specific ascending");
        CHECK(p.generic == 10, "10 generic");
        auto skipped = [&](int64_t c) {
            for (const auto& s : p.skipped)
                if (s.first == c) return s.second;
            return std::string();
        };
        CHECK(skipped(82) == "a manager body", "82 is a manager body: " + skipped(82));
        CHECK(skipped(418) == "a women's body", "418: the probe says women");
        CHECK(skipped(406) == "a women's body", "406: no probe gender, the census says women only");
        CHECK(p.steps[5].label == "Tall, stocky", "label from the census: " + p.steps[5].label);
        CHECK(!p.steps.back().label.empty(), "a specific code has a label: " + p.steps.back().label);
        o.gender = 1;
        p = bt::plan_steps(cat, o);
        got.clear();
        for (const auto& s : p.steps) got.push_back(s.code);
        CHECK(got == std::vector<int64_t>({1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 406, 418, 500}), "women: 20 and 251 skipped");
        o.gender = 0;
        o.field_max = 300;
        o.max_steps = 0;
        p = bt::plan_steps(cat, o);
        CHECK(p.steps.back().code == 251, "out of the database's range: 500 skipped");
        o.field_max = 512;
        o.max_steps = 3;
        p = bt::plan_steps(cat, o);
        CHECK(p.steps.size() == 3 && p.steps[0].code == 1 && p.steps[2].code == 3, "a limit keeps the first N");
        o.max_steps = 0;
        o.allow_risky = false;
        o.head_class = 1;
        p = bt::plan_steps(cat, o);
        CHECK(p.steps.size() == 10, "without allow_risky a generic head gets the generic bodies only");
        o.include_manager_set = true;
        o.allow_risky = true;
        p = bt::plan_steps(cat, o);
        CHECK(p.steps.size() == 14, "with the manager set: 82 too");
    });

    run_case("body-type thumbnails: the Tester's request file", [&] {
        const fs::path out = fresh_dir("bodythumbs_request");
        bt::StartRequest rq;
        std::string err;
        CHECK(!bt::take_request(out, rq, &err) && err.empty(), "no request");
        spit(bt::request_file(out), R"({"player": 254470, "source": "camera", "camera": 2, "mode": 2, "max": 5})");
        CHECK(bt::take_request(out, rq, &err) && rq.player == 254470 && rq.source_given && rq.source.kind == bt::SourceKind::Camera &&
                  rq.source.camera == 2 && rq.source.mode == 2 && rq.max_steps == 5 && !rq.fresh,
              "read: " + err);
        CHECK(rq.source.text() == "camera 2 mode 2", "source in words: " + rq.source.text());
        CHECK(!fs::exists(bt::request_file(out)) && fs::exists(bt::folder(out) / "make.taken.json"), "taken once (renamed)");
        CHECK(!bt::take_request(out, rq, &err), "not run twice");
        spit(bt::request_file(out), "{not json");
        err.clear();
        CHECK(!bt::take_request(out, rq, &err) && !err.empty() && !fs::exists(bt::request_file(out)), "unreadable: refused and removed");
        CHECK(!bt::parse_request(R"({"source": "camera", "camera": -1})", rq, &err), "camera -1 without a mode refused");
        CHECK(!bt::parse_request(R"({"source": "mirror"})", rq, &err), "an unknown source refused");
        CHECK(!bt::parse_request(R"({"source": "camera", "camera": 99})", rq, &err), "a camera this build lacks refused");
        // 2.17.0 (G4 blocker C2): the window never starts a run from a file; only the button, after a confirmation
        const std::string ui = slurp(fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui" / "ui_bodytype_thumbs.cpp");
        CHECK(!ui.empty() && ui.find("take_request") == std::string::npos && ui.find("BeginPopupModal") != std::string::npos,
              "no request file starts a run in the window; the button asks for a confirmation first");
        CHECK(!bt::parse_request(R"({"source": "camera", "camera": -3})", rq, &err), "camera -3 refused");
        CHECK(bt::parse_request(R"({"source": "camera", "camera": -2})", rq, &err) && rq.source.camera == -2 && rq.source.mode == -1,
              "camera -2 (track C's kCameraBodyFull) accepted: " + err);
        {
            Fake game;
            bt::SourceAdapter full(rq.source, &game);
            CHECK(full.request(tc::body_request(254470, 95), &err) && game.requests.size() == 1 && game.requests[0].camera == -2 &&
                      game.requests[0].mode_override == -1 && game.requests[0].extra_override == -1 && !game.requests[0].use_template,
                  "camera -2 is sent as Request::camera, with no mode / extra overrides");
        }
        CHECK(!bt::parse_request(R"({"source": "lua"})", rq, &err), "lua is no source (route B dropped)");
        CHECK(bt::parse_request(R"({"source": "camera"})", rq, &err) && rq.source.camera == bt::kCameraBodyFull, "camera alone = full body");
        CHECK(bt::parse_request(R"({"source": "none"})", rq, &err) && rq.source.kind == bt::SourceKind::None && rq.source.text() == "none", "none");
        CHECK(bt::parse_request("{}", rq, &err) && rq.player == 0 && !rq.source_given && rq.max_steps == 0, "defaults: selected player, the proven source");
        bt::SourceChoice c;
        CHECK(!bt::read_source_file(out, c), "no source file");
        spit(bt::folder(out) / "source.json", R"({"source": "camera", "camera": 3})");
        CHECK(bt::read_source_file(out, c, &err) && c.kind == bt::SourceKind::Camera && c.camera == 3, "source file: " + err);
    });

    run_case("body-type thumbnails: a full run (pictures, manifest, run log before each request, his own code back)", [&] {
        const fs::path out = fresh_dir("bodythumbs_run");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        bt::Generator g(w.env());
        bt::StartRequest rq = cam_rq(4, 2, 2);
        std::string err;
        CHECK(!bt::generator_running(), "nothing running before");
        CHECK(g.start(cat, ref_of(254470), rq, &err), "started: " + err);
        CHECK(g.running() && bt::generator_running(), "running (other capture requests wait)");
        w.run(g);
        CHECK(!g.running() && !bt::generator_running(), "ended");
        CHECK(w.db["career:LEICESTER"][254470] == 5, "his own code back: " + std::to_string(w.db["career:LEICESTER"][254470]));
        CHECK(w.writes == 5, "4 codes + 1 restore written: " + std::to_string(w.writes));
        bt::Manifest m;
        CHECK(bt::load_manifest(bt::manifest_file(out), m, &err) && m.finished && m.restored && m.original == 5 && m.player == 254470, "manifest: " + err);
        CHECK(m.count("done") == 4, "4 done");
        std::vector<std::string> hashes;
        for (int64_t c : {1, 2, 3, 4}) {
            turbo::Rgba img;
            CHECK(turbo::load_image_file(bt::grid_file(out, c), img) && img.w == bt::kGridWidth, "grid picture of " + std::to_string(c));
            CHECK(fs::exists(bt::full_file(out, c)), "full picture of " + std::to_string(c));
            hashes.push_back(std::string(img.px.begin(), img.px.begin() + 4));
        }
        CHECK(hashes[0] != hashes[1] && hashes[1] != hashes[2], "each picture shows the body type he wore for it");
        for (const tc::Request& r : w.svc.requests)
            CHECK(r.id == 254470 && r.second_id == 95 && !r.use_template && !r.manager && r.camera == 2 && r.mode_override == 2 && r.extra_override == -1,
                  "body request through the chosen camera and mode");
        const std::string log = slurp(bt::log_file(out));
        const size_t wr = log.find("code 3 (Average height, stocky), attempt 1: writing it on player 254470");
        const size_t kept = log.find("code 3: picture kept");
        CHECK(wr != std::string::npos && kept != std::string::npos && wr < kept, "run log: the code is written down before its picture");
        CHECK(log.find("his own body type is back") != std::string::npos, "run log: the end");
        bt::RunInfo ri;
        CHECK(bt::read_run_info(out, ri, &err) && ri.career == "career:LEICESTER" && ri.player == 254470 && ri.source == "camera 2 mode 2",
              "run record: career, player, source");
        bt::configure(out);
        bt::invalidate();
        CHECK(bt::thumb(3) == bt::grid_file(out, 3), "the grid finds the pictures");
        bt::configure(fs::path());
    });

    run_case("body-type thumbnails: refusals (no real face, unknown career, busy, nothing to draw)", [&] {
        const fs::path out = fresh_dir("bodythumbs_refuse");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        bt::Generator g(w.env());
        std::string err;
        CHECK(!g.start(cat, ref_of(254470, 0, 1), bt::StartRequest{}, &err) && err.find("real face") != std::string::npos, "generic head: " + err);
        CHECK(!g.start(cat, ref_of(0), bt::StartRequest{}, &err), "no player");
        w.career.clear();
        CHECK(!g.start(cat, ref_of(254470), bt::StartRequest{}, &err) && err.find("career") != std::string::npos, "no career: " + err);
        w.career = "career:LEICESTER";
        w.svc.busy = true;
        CHECK(!g.start(cat, ref_of(254470), cam_rq(0), &err) && err.find("another picture") != std::string::npos, "busy: " + err);
        w.svc.busy = false;
        turbo::bodytype::Catalog empty;
        bt::Plan p = bt::plan_steps(empty, bt::PlanOptions{});
        CHECK(p.steps.size() == 10, "without a probe file: the 10 named bodies");
        CHECK(w.writes == 0 && !fs::exists(bt::manifest_file(out)), "nothing written by a refusal");
    });

    run_case("body-type thumbnails: the game crashes half way, the next run continues and gives back HIS code", [&] {
        const fs::path out = fresh_dir("bodythumbs_resume");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        bt::StartRequest rq = cam_rq(5);
        std::string err;
        {
            bt::Generator g(w.env());
            CHECK(g.start(cat, ref_of(254470), rq, &err), "started: " + err);
            // two pictures, then the third code is written and the game goes down while drawing it
            w.svc.answer = true;
            for (int n = 0; n < 5000 && g.sequence()->count("done") < 2; ++n) g.tick(), w.t += 0.1;
            w.svc.answer = false;
            for (int n = 0; n < 50; ++n) g.tick(), w.t += 0.1;
            CHECK(g.sequence()->current_code() == 3, "drawing code 3 when it dies: " + std::to_string(g.sequence()->current_code()));
        }  // Turbo goes with the game: no restore
        CHECK(!bt::generator_running(), "the dead run no longer counts as running");
        bt::Manifest m;
        CHECK(bt::load_manifest(bt::manifest_file(out), m, &err) && !m.finished && m.entry(3) && m.entry(3)->state == "pending", "manifest: 3 pending");
        CHECK(w.db["career:LEICESTER"][254470] == 3, "he still wears the test code in this world (worst case: no reload)");
        w.svc.answer = true;
        bt::Generator g2(w.env());
        CHECK(g2.start(cat, ref_of(254470), rq, &err), "continued: " + err);
        CHECK(g2.sequence()->original() == 5, "his original from the manifest (5), not the code he wears (3)");
        w.run(g2);
        CHECK(w.db["career:LEICESTER"][254470] == 5, "his own code back: " + std::to_string(w.db["career:LEICESTER"][254470]));
        CHECK(bt::load_manifest(bt::manifest_file(out), m, &err) && m.finished && m.restored && m.count("done") == 5, "5 done after the resume");
        CHECK(m.entry(3) && m.entry(3)->state == "done" && m.entry(3)->attempts == 2, "code 3: the crash counted as one attempt, drawn on the second (" +
              (m.entry(3) ? std::to_string(m.entry(3)->attempts) : std::string("?")) + ")");
        int writes_of_1 = 0;
        for (const std::string& s : w.writes_seen)
            if (s == "career:LEICESTER 254470 1") ++writes_of_1;
        CHECK(writes_of_1 == 1, "code 1 (done before the crash) was not drawn again");
    });

    run_case("body-type thumbnails: another career is loaded mid-run: nothing is written there", [&] {
        const fs::path out = fresh_dir("bodythumbs_career");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        bt::Generator g(w.env());
        bt::StartRequest rq = cam_rq(6);
        std::string err;
        CHECK(g.start(cat, ref_of(254470), rq, &err), "started: " + err);
        for (int n = 0; n < 5000 && g.sequence()->count("done") < 1; ++n) g.tick(), w.t += 0.1;
        w.career = "career:TORINO";
        w.run(g);
        CHECK(!g.running(), "the run stopped");
        bool torino_written = false;
        for (const std::string& s : w.writes_seen)
            if (s.rfind("career:TORINO", 0) == 0) torino_written = true;
        CHECK(!torino_written, "never a write in TORINO");
        CHECK(w.db["career:TORINO"][254470] == 5, "TORINO untouched");
        bt::Manifest m;
        CHECK(bt::load_manifest(bt::manifest_file(out), m, &err) && !m.restored, "the manifest says his code was not put back");
        // back in LEICESTER the leftover is put back by the button
        w.career = "career:LEICESTER";
        CHECK(g.put_back_leftover(&err), "put back: " + err);
        CHECK(w.db["career:LEICESTER"][254470] == 5, "LEICESTER: his own code");
        CHECK(bt::load_manifest(bt::manifest_file(out), m, &err) && m.restored, "manifest now restored");
    });

    run_case("body-type thumbnails: a new run never overwrites the record of a player who did not get his code back", [&] {
        const fs::path out = fresh_dir("bodythumbs_startover");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        bt::StartRequest rq = cam_rq(3);
        std::string err;
        {
            bt::Generator g(w.env());
            CHECK(g.start(cat, ref_of(254470), rq, &err), "first run: " + err);
            for (int n = 0; n < 5000 && g.sequence()->count("done") < 1; ++n) g.tick(), w.t += 0.1;
        }  // dies with player 254470 wearing a test code
        const int64_t wearing = w.db["career:LEICESTER"][254470];
        CHECK(wearing != 5, "he wears a test code: " + std::to_string(wearing));
        bt::Generator g2(w.env());
        CHECK(!g2.start(cat, ref_of(189324), rq, &err) && err.find("did not give him his own body type back") != std::string::npos, "other player refused: " + err);
        rq.fresh = true;
        CHECK(!g2.start(cat, ref_of(254470), rq, &err), "a fresh run of the same player refused too: " + err);
        CHECK(w.db["career:LEICESTER"][189324] == 2, "189324 untouched");
        CHECK(g2.put_back_leftover(&err) && w.db["career:LEICESTER"][254470] == 5, "put back: " + err);
        rq.fresh = false;
        CHECK(g2.start(cat, ref_of(189324), rq, &err), "now another player may start: " + err);
        w.run(g2);
        CHECK(w.db["career:LEICESTER"][189324] == 2 && w.db["career:LEICESTER"][254470] == 5, "both have their own codes");
        bool kept_aside = false;
        for (fs::directory_iterator it(bt::folder(out)), end; it != end; ++it)
            if (it->path().filename().string().rfind("manifest.2026", 0) == 0) kept_aside = true;
        CHECK(kept_aside, "the old manifest is kept next to the new one");
        CHECK(std::string(turbo::bodytype_thumbs_busy_text()).empty(), "no busy text after the run");
    });

    run_case("body-type thumbnails: source none refuses; the default is the full-body camera; source.json overrides it", [&] {
        const fs::path out = fresh_dir("bodythumbs_none");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        bt::Generator g(w.env());
        std::string err;
        bt::StartRequest rq;
        rq.source_given = true;
        rq.source.kind = bt::SourceKind::None;  // none named explicitly
        CHECK(!g.start(cat, ref_of(254470), rq, &err) && err.find("nothing to make") != std::string::npos, "refused: " + err);
        CHECK(w.writes == 0 && w.svc.requests.empty() && !fs::exists(bt::manifest_file(out)), "no write, no request, no manifest");
        bt::SourceChoice nc;
        nc.kind = bt::SourceKind::None;
        bt::SourceAdapter none(nc, &w.svc);
        tc::Result r;
        CHECK(!none.status().available && !none.request(tc::body_request(254470, 95), &err) && !none.poll(r), "the none source never draws");
        // nothing named, no source.json: the full-body camera (track C's kCameraBodyFull)
        {
            bt::Generator gd(w.env());
            rq = bt::StartRequest{};
            rq.max_steps = 1;
            CHECK(gd.start(cat, ref_of(254470), rq, &err), "default start: " + err);
            w.run(gd);
            CHECK(!w.svc.requests.empty() && w.svc.requests.back().camera == bt::kCameraBodyFull && w.svc.requests.back().mode_override == -1,
                  "default = camera -2, no overrides");
            bt::RunInfo ri;
            CHECK(bt::read_run_info(out, ri, &err) && ri.source == "camera -2", "run record: camera -2");
        }
        // source.json overrides the default when the request names none
        rq.fresh = true;
        spit(bt::folder(out) / "source.json", R"({"source": "camera", "camera": 3})");
        rq = bt::StartRequest{};
        rq.max_steps = 2;
        rq.fresh = true;
        CHECK(g.start(cat, ref_of(254470), rq, &err), "started from source.json: " + err);
        w.run(g);
        CHECK(!w.svc.requests.empty() && w.svc.requests.back().camera == 3, "camera 3 from source.json");
        CHECK(w.db["career:LEICESTER"][254470] == 5, "his own code back");
    });

    run_case("body-type thumbnails: a crashed run is continued only with the same source", [&] {
        const fs::path out = fresh_dir("bodythumbs_source_resume");
        turbo::bodytype::Catalog cat;
        load_catalog(cat, out);
        World w(out);
        std::string err;
        {
            bt::Generator g(w.env());
            CHECK(g.start(cat, ref_of(254470), cam_rq(4, 1), &err), "started: " + err);
            for (int n = 0; n < 5000 && g.sequence()->count("done") < 1; ++n) g.tick(), w.t += 0.1;
        }  // the game goes down; he wears a test code
        bt::Generator g2(w.env());
        CHECK(!g2.start(cat, ref_of(254470), cam_rq(4, 3), &err) && err.find("did not give him his own body type back") != std::string::npos,
              "another camera is a new run, refused while his code is not back: " + err);
        CHECK(g2.start(cat, ref_of(254470), cam_rq(4, 1), &err), "the same source continues: " + err);
        w.run(g2);
        CHECK(w.db["career:LEICESTER"][254470] == 5, "his own code back");
    });
}
