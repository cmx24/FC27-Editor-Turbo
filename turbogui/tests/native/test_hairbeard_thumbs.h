// 5.0c E10 lit capture (brief E10-12 D3): the hair / beard picture generator (core/hairbeard_thumbs.h), offline.
// Included by test_main.cpp after test_bodytype_thumbs.h (reuses its Fake capture service, slurp, spit, fresh_dir).
// The in-game part is a HYPOTHESIS: nothing here proves a game capture shows the hair.
#pragma once

#include <map>

#include "core/hairbeard_thumbs.h"

namespace hbt = turbo::hairbeard_thumbs;

namespace hbthumbs_test {
using bodythumbs_test::Fake;
using bodythumbs_test::fresh_dir;
using bodythumbs_test::slurp;
using bodythumbs_test::spit;

struct HbWorld {
    fs::path out;
    Fake svc;
    std::map<std::string, int64_t> codes{{"hairtypecode", 25}, {"facialhairtypecode", 3}};
    std::vector<std::string> writes;  // "hair 7", ...
    double now = 1000.0;
    hbt::GeneratorEnv env() {
        hbt::GeneratorEnv e;
        e.turbo_output = out;
        e.svc = &svc;
        e.read_code = [this](int32_t, hbt::Field f, int64_t* c, std::string*) {
            *c = codes[hbt::db_field(f)];
            return true;
        };
        e.write_code = [this](int32_t, hbt::Field f, int64_t c, std::string*) {
            codes[hbt::db_field(f)] = c;
            writes.push_back(std::string(hbt::file_word(f)) + " " + std::to_string(c));
            return true;
        };
        e.clock = [this]() { return now; };
        e.stamp = []() { return std::string("2026-10-31 07:40:00"); };
        return e;
    }
    void run(hbt::Generator& g, int ticks, double dt = 0.1) {
        for (int i = 0; i < ticks && g.running(); ++i) {
            now += dt;
            g.tick();
        }
    }
};

inline hbt::Reference hb_ref() {
    hbt::Reference r;
    r.player = 2002;
    r.team = 95;
    r.head_class = 1;  // generic head
    r.name = "Test Generic";
    return r;
}

inline void cases() {
    run_case("5.0c E10 lit capture: the step list is exactly the model-only ids, hair first, then beards", [&] {
        const std::vector<int64_t> hair = {420, 7, 0, 7, 3};
        const std::vector<int64_t> beard = {12, 0, 5};
        const auto plan = hbt::plan_steps(hair, beard);
        std::vector<int64_t> got;
        for (const auto& s : plan) got.push_back(s.code);
        const std::vector<int64_t> want = {3, 7, 420, hbt::kBeardBase + 5, hbt::kBeardBase + 12};
        CHECK(got == want, "hair 3 7 420 then beard 5 12 (0 and duplicates left out), " + std::to_string(got.size()) + " steps");
        CHECK(plan.front().label == "hair 3" && plan.back().label == "beard 12", "labels name the field and the id: " + plan.back().label);
        CHECK(hbt::plan_steps(hair, beard, 2).size() == 2, "max 2 = the first 2");
        hbt::Field f;
        int64_t id = 0;
        hbt::split_code(hbt::kBeardBase + 12, f, id);
        CHECK(f == hbt::Field::Beard && id == 12, "a beard step code splits back to beard 12");
        const fs::path d = fresh_dir("hbt_paths");
        CHECK(hbt::picture_file(d, hbt::Field::Hair, 7) == d / "cache" / "hairbeard_thumbs" / "hair_7.png", "hair 7 -> cache\\hairbeard_thumbs\\hair_7.png");
        CHECK(hbt::missing_count(d, hair, beard) == 5, "nothing made yet: 5 missing");
        spit(hbt::picture_file(d, hbt::Field::Beard, 5), "x");
        CHECK(hbt::missing_count(d, hair, beard) == 4, "beard 5 made: 4 missing");
        hbt::configure(d);
        hbt::invalidate();
        CHECK(!hbt::thumb(hbt::Field::Beard, 5).empty() && hbt::thumb(hbt::Field::Hair, 5).empty(), "the index finds beard_5.png and not hair 5");
    });

    run_case("5.0c E10 lit capture: the request file (op hairbeard_thumbs)", [&] {
        hbt::StartRequest rq;
        std::string err;
        CHECK(hbt::parse_request(R"({"op":"hairbeard_thumbs","player":2002,"max":3,"fresh":true})", rq, &err) && rq.player == 2002 && rq.max_steps == 3 && rq.fresh,
              "player 2002, max 3, fresh: " + err);
        CHECK(!hbt::parse_request(R"({"op":"bodytype_thumbs"})", rq, &err), "another op is refused: " + err);
        const fs::path d = fresh_dir("hbt_req");
        spit(hbt::request_file(d), R"({"op":"hairbeard_thumbs","player":0})");
        CHECK(hbt::take_request(d, rq, &err) && rq.player == 0, "take_request reads it: " + err);
        CHECK(!fs::exists(hbt::request_file(d)) && fs::exists(hbt::folder(d) / "request.taken.json"), "and renames it request.taken.json");
    });

    run_case("5.0c E10 lit capture: a full run keeps one PNG per id and gives him his own hair and beard back", [&] {
        HbWorld w;
        w.out = fresh_dir("hbt_run");
        hbt::Generator g(w.env());
        std::string err;
        CHECK(g.start({7, 3}, {5}, hb_ref(), {}, &err), "started: " + err);
        w.run(g, 400);
        CHECK(!g.running(), "the run ended: " + g.status());
        CHECK(fs::exists(hbt::picture_file(w.out, hbt::Field::Hair, 3)) && fs::exists(hbt::picture_file(w.out, hbt::Field::Hair, 7)) &&
                  fs::exists(hbt::picture_file(w.out, hbt::Field::Beard, 5)),
              "hair_3.png, hair_7.png, beard_5.png");
        CHECK(w.codes["hairtypecode"] == 25 && w.codes["facialhairtypecode"] == 3, "his own hair 25 and beard 3 are back");
        CHECK(w.svc.requests.size() == 3 && w.svc.requests[0].camera == turbo::capture::kCameraBody, "3 requests, head and shoulders camera (mode 1)");
        CHECK(fs::exists(hbt::manifest_file(w.out)) && slurp(hbt::manifest_file(w.out)).find("\"done\"") != std::string::npos, "the manifest records done steps");
    });

    run_case("5.0c E10 lit capture: refusals (scanned head, nothing to draw, no service)", [&] {
        HbWorld w;
        w.out = fresh_dir("hbt_refuse");
        hbt::Generator g(w.env());
        std::string err;
        hbt::Reference scanned = hb_ref();
        scanned.head_class = 0;
        CHECK(!g.start({7}, {}, scanned, {}, &err) && err.find("generic head") != std::string::npos, "a scanned head is refused: " + err);
        CHECK(!g.start({0}, {}, hb_ref(), {}, &err), "only 0 = nothing to draw: " + err);
        hbt::GeneratorEnv e = w.env();
        e.svc = nullptr;
        hbt::Generator g2(e);
        CHECK(!g2.start({7}, {}, hb_ref(), {}, &err), "no capture service: refused: " + err);
        CHECK(w.writes.empty(), "a refusal writes nothing");
    });

    run_case("5.0c E10 lit capture: the 45 s deadline per id (the game never answers): failed, the run moves on, his look is back", [&] {
        HbWorld w;
        w.out = fresh_dir("hbt_deadline");
        w.svc.answer = false;
        hbt::Generator g(w.env());
        std::string err;
        CHECK(g.start({7, 3}, {}, hb_ref(), {}, &err), "started: " + err);
        w.run(g, 20, 1.0);  // 20 s: still waiting on the first id
        CHECK(g.running() && g.sequence()->count("failed") == 0, "after 20 s the first id still waits (deadline 45 s)");
        w.run(g, 2000, 1.0);
        CHECK(!g.running(), "the run ended: " + g.status());
        const auto* e3 = g.sequence()->entry(3);
        CHECK(e3 && e3->state == "failed", "hair 3 failed at the deadline: " + (e3 ? e3->state + " " + e3->error : std::string("none")));
        CHECK(!fs::exists(hbt::picture_file(w.out, hbt::Field::Hair, 3)), "no picture for it");
        CHECK(w.codes["hairtypecode"] == 25 && w.codes["facialhairtypecode"] == 3, "his own hair and beard are back");
        CHECK(hbt::kStepDeadlineSec == 45.0, "the deadline is 45 s");
    });

    run_case("5.0c E10 lit capture: resume after a crash continues the manifest and does not redo a done id", [&] {
        HbWorld w;
        w.out = fresh_dir("hbt_resume");
        {
            hbt::Generator g(w.env());
            std::string err;
            CHECK(g.start({3, 7}, {5}, hb_ref(), {}, &err), "first run started: " + err);
            // stop after the first picture (the game "crashes": the generator is dropped, the manifest stays)
            for (int i = 0; i < 400 && g.sequence()->count("done") < 1; ++i) {
                w.now += 0.1;
                g.tick();
            }
            CHECK(g.sequence()->count("done") == 1, "one picture made before the crash");
        }
        const size_t before = w.svc.requests.size();
        hbt::Generator g(w.env());
        std::string err;
        CHECK(g.start({3, 7}, {5}, hb_ref(), {}, &err), "second run started (resume): " + err);
        w.run(g, 400);
        CHECK(!g.running() && g.sequence()->count("done") == 3, "all 3 done after the resume: " + g.status());
        CHECK(w.codes["hairtypecode"] == 25 && w.codes["facialhairtypecode"] == 3,
              "his OWN hair 25 / beard 3 are back after the resume (not the test style he wore at the crash): hair " +
                  std::to_string(w.codes["hairtypecode"]) + " beard " + std::to_string(w.codes["facialhairtypecode"]));
        CHECK(w.svc.requests.size() - before <= 2, "the resumed run asked for at most the 2 ids left (" + std::to_string(w.svc.requests.size() - before) + ")");
    });
}

}  // namespace hbthumbs_test

inline void test_hairbeard_thumbs() { hbthumbs_test::cases(); }
