// Turbo X: the gear picture generator (core/gear_thumbs.h), offline. Included by test_main.cpp after test_hairbeard_thumbs.h (reuses
// test_bodytype_thumbs.h's fresh_dir, slurp, spit). The in-game part is UNPROVEN: nothing here shows the game draws boots, gloves,
// accessories or tattoos in a capture, nor that it answers a batch of full-body pictures.
#pragma once

#include <deque>
#include <map>

#include "core/gear_thumbs.h"

namespace gtt = turbo::gear_thumbs;

namespace gearthumbs_test {
using bodythumbs_test::fresh_dir;
using bodythumbs_test::slurp;
using bodythumbs_test::spit;
namespace gtc = turbo::capture;

// A capture service that answers batches (one Result per head, by id) and says how many heads it takes
struct GtFake : gtc::CaptureService {
    std::deque<gtc::Result> answers;
    std::vector<gtc::Request> requests;
    int max_batch = 6;
    bool answer = true;
    std::set<int32_t> silent;  // ids that never get a picture
    // Turbo X 1.0.1 stale guard: the picture shows the player's look as the database has it now (vals, set by GtWorld), so two
    // looks give two pictures. frozen: ids whose picture never changes; replay_at: id -> his n-th pictures (1-based) that repeat
    // his previous picture (the game drew the previous look)
    std::map<int32_t, std::map<std::string, int64_t>>* look = nullptr;
    std::set<int32_t> frozen;
    std::map<int32_t, std::set<int>> replay_at;
    std::map<int32_t, int> drawn;
    std::map<int32_t, std::vector<uint8_t>> last_px;
    gtc::Status status() override {
        gtc::Status s;
        s.installed = true;
        s.available = true;
        s.max_batch = max_batch;
        return s;
    }
    bool request(const gtc::Request& r, std::string*) override {
        requests.push_back(r);
        if (!answer) return true;
        for (const gtc::BatchEntry& e : gtc::request_entries(r)) {
            if (silent.count(e.id)) continue;
            gtc::Result a;
            a.ok = true;
            a.id = e.id;
            a.format = "PNG 64x64";
            a.image.w = a.image.h = 64;
            a.image.px.assign(64 * 64 * 4, 200);
            const int n = ++drawn[e.id];
            if (replay_at[e.id].count(n) && last_px.count(e.id)) {
                a.image.px = last_px[e.id];
            } else if (!frozen.count(e.id) && look) {
                uint64_t h = 1469598103934665603ull ^ static_cast<uint32_t>(e.id);
                for (const auto& kv : (*look)[e.id]) {
                    for (char c : kv.first) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
                    h = (h ^ static_cast<uint64_t>(kv.second)) * 1099511628211ull;
                }
                for (int k = 0; k < 8; ++k) a.image.px[static_cast<size_t>(k)] = static_cast<uint8_t>(h >> (8 * k));
            }
            last_px[e.id] = a.image.px;
            a.bytes = 100;
            answers.push_back(a);
        }
        return true;
    }
    bool poll(gtc::Result& out) override {
        if (answers.empty()) return false;
        out = answers.front();
        answers.pop_front();
        return true;
    }
    void cancel() override { answers.clear(); }
    const gtc::Template* learned() override { return nullptr; }
};

struct GtWorld {
    fs::path out;
    GtFake svc;
    std::map<int32_t, std::map<std::string, int64_t>> vals;
    double now = 1000.0;
    GtWorld() {
        for (int32_t p : {2002, 2003, 2004}) vals[p] = {{"hairtypecode", 25}, {"shoetypecode", 40}, {"accessorycode1", 0}, {"tattooleftarm", 0}, {"bodytypecode", 2}};
        svc.look = &vals;
    }
    GtWorld(const GtWorld&) = delete;
    GtWorld& operator=(const GtWorld&) = delete;
    gtt::GeneratorEnv env() {
        gtt::GeneratorEnv e;
        e.turbo_output = out;
        e.svc = &svc;
        e.read_field = [this](int32_t p, const std::string& f, int64_t* v, std::string* err) {
            auto it = vals[p].find(f);
            if (it == vals[p].end()) {
                if (err) *err = "no column " + f;
                return false;
            }
            *v = it->second;
            return true;
        };
        e.write_field = [this](int32_t p, const std::string& f, int64_t v, std::string*) {
            vals[p][f] = v;
            return true;
        };
        e.clock = [this]() { return now; };
        e.stamp = []() { return std::string("2026-10-31 23:00:00"); };
        return e;
    }
    void run(gtt::Generator& g, int ticks, double dt = 0.1) {
        for (int i = 0; i < ticks && g.running(); ++i) {
            now += dt;
            g.tick();
        }
    }
};

inline gtt::Reference gt_ref(int32_t id = 2002, int head_class = 1) {
    gtt::Reference r;
    r.player = id;
    r.team = 95;
    r.head_class = head_class;
    r.name = "Test " + std::to_string(id);
    return r;
}

inline bool own_back(GtWorld& w) {
    for (int32_t p : {2002, 2003, 2004})
        if (w.vals[p]["shoetypecode"] != 40 || w.vals[p]["hairtypecode"] != 25 || w.vals[p]["tattooleftarm"] != 0 || w.vals[p]["bodytypecode"] != 2) return false;
    return true;
}

inline void cases() {
    run_case("Turbo X gear pictures: request, plan, codes, paths", [&] {
        gtt::Request rq;
        std::string err;
        std::vector<std::string> bad;
        CHECK(gtt::parse_request(R"({"op":"gear_thumbs","player":0,"players":[2002,2003,2002],"steps":[{"field":"shoetypecode","id":421,"camera":"body"},{"field":"accessorycode","id":7},{"field":"nosuch","id":1},{"field":"hairtypecode"}]})",
                                 rq, &err, &bad) &&
                  rq.steps.size() == 3 && bad.size() == 1 && rq.players.size() == 2,
              "3 steps kept, the one without an id named, players 2002 2003: " + err);
        CHECK(!gtt::parse_request(R"({"op":"gear_thumbs","players":[1,2,3,4,5,6,7],"steps":[]})", rq, &err), "7 players refused");
        CHECK(!gtt::parse_request(R"({"op":"hairbeard_thumbs","steps":[]})", rq, &err), "another op is refused");
        CHECK(!gtt::parse_request(R"({"op":"gear_thumbs"})", rq, &err), "no steps list is refused");
        const fs::path d = fresh_dir("gtt_plan");
        gtt::Request r2;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":421},{"field":"shoetypecode","id":421},{"field":"accessorycode1","id":7},{"field":"hairtypecode","id":2071},{"field":"nosuch","id":1}]})", r2, &err);
        spit(gtt::picture_file(d, *gtt::find_field("accessorycode1"), 7), "x");
        const gtt::Plan p = gtt::make_plan(d, r2, nullptr);
        CHECK(p.body.size() == 1 && p.head.size() == 1 && p.skipped_existing == 1 && p.rejected.size() == 1,
              "boots 421 once (body), hair 2071 (head), accessory 7 kept, unknown field refused");
        int fi = -1;
        int64_t id = 0;
        CHECK(gtt::split_code(gtt::step_code(4, 7), fi, id) && fi == 4 && id == 7, "a step code splits back");
        CHECK(gtt::picture_file(d, *gtt::find_field("accessorycode"), 7) == d / "pictures" / "assets" / "accessorycode" / "7.png", "accessory -> pictures\\assets\\accessorycode\\7.png");
        spit(gtt::request_file(d), R"({"op":"gear_thumbs","steps":[{"field":"shoetypecode","id":1}]})");
        CHECK(gtt::take_request(d, rq, &err) && !fs::exists(gtt::request_file(d)) && fs::exists(gtt::run_dir(d) / "request.taken.json"), "taken and renamed");
    });

    run_case("Turbo X gear pictures: one player = single requests, head then body, own values back", [&] {
        GtWorld w;
        w.out = fresh_dir("gtt_run1");
        gtt::Generator g(w.env());
        gtt::Request rq;
        std::string err;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":421},{"field":"tattooleftarm","id":9},{"field":"hairtypecode","id":2071},{"field":"bodytypecode","id":30}]})", rq, &err);
        const uint64_t gen0 = gtt::generation();
        CHECK(g.start(rq, {gt_ref()}, &err), "started: " + err);
        w.run(g, 2000);
        CHECK(!g.running(), "ended: " + g.status());
        CHECK(g.done() == 4 && gtt::generation() - gen0 == 4, "4 pictures: " + g.status());
        CHECK(fs::exists(gtt::picture_file(w.out, *gtt::find_field("bodytypecode"), 30)) && fs::exists(gtt::picture_file(w.out, *gtt::find_field("hairtypecode"), 2071)), "body 30 and hair 2071 kept");
        CHECK(own_back(w), "his own values are back");
        CHECK(w.svc.requests.size() == 4 && w.svc.requests[0].batch.empty() && w.svc.requests[0].camera == gtc::kCameraBody &&
                  w.svc.requests[1].camera == gtc::kCameraBodyFull && w.svc.requests[1].mode_override == 0,
              "single requests: head camera first, then the full body at mode 0");
        CHECK(!fs::exists(gtt::own_file(w.out)), "own.json removed after a clean restore");
        CHECK(slurp(gtt::log_file(w.out)).find("-> saved") != std::string::npos, "run.log names the saved pictures");
    });

    run_case("Turbo X gear pictures: three players = batches of three, one picture per player per round", [&] {
        GtWorld w;
        w.out = fresh_dir("gtt_batch");
        gtt::Generator g(w.env());
        gtt::Request rq;
        std::string err;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":1},{"field":"shoetypecode","id":2},{"field":"shoetypecode","id":3},{"field":"shoetypecode","id":4},{"field":"tattooleftarm","id":5},{"field":"hairtypecode","id":6},{"field":"hairtypecode","id":7}]})", rq, &err);
        CHECK(g.start(rq, {gt_ref(2002), gt_ref(2003, 0), gt_ref(2004)}, &err), "started: " + err);
        w.run(g, 2000);
        CHECK(!g.running() && g.done() == 7, "7 pictures: " + g.status());
        CHECK(w.svc.requests.size() == 3, "3 requests (hair 6+7 on the two generic heads, then 3 + 2 body items): " + std::to_string(w.svc.requests.size()));
        CHECK(w.svc.requests[0].batch.size() == 2 && w.svc.requests[0].camera == gtc::kCameraBody, "round 1: two hair items, head camera, not on the scanned head");
        bool no_scanned = true;
        for (const auto& e : w.svc.requests[0].batch) no_scanned = no_scanned && e.id != 2003;
        CHECK(no_scanned, "player 2003 (scanned head) gets no hair");
        CHECK(w.svc.requests[1].batch.size() == 3 && w.svc.requests[1].camera == gtc::kCameraBodyFull, "round 2: three body items in one request");
        CHECK(own_back(w), "every player's own values are back");
    });

    run_case("Turbo X gear pictures: the host's max batch caps a round; a silent player's items retry once then fail", [&] {
        GtWorld w;
        w.out = fresh_dir("gtt_cap");
        w.svc.max_batch = 1;
        w.svc.silent = {2003};
        gtt::Generator g(w.env());
        gtt::Request rq;
        std::string err;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":1},{"field":"shoetypecode","id":2}]})", rq, &err);
        CHECK(g.start(rq, {gt_ref(2002), gt_ref(2003)}, &err), "started: " + err);
        w.run(g, 5000, 1.0);
        CHECK(!g.running() && g.done() == 2, "both drawn by 2002 with batches of 1: " + g.status());
        for (const auto& r : w.svc.requests) CHECK(r.batch.empty(), "max batch 1: single requests only");
        GtWorld w2;
        w2.out = fresh_dir("gtt_silent");
        w2.svc.silent = {2003};
        gtt::Generator g2(w2.env());
        CHECK(g2.start(rq, {gt_ref(2002), gt_ref(2003)}, &err), "started: " + err);
        w2.run(g2, 5000, 1.0);
        CHECK(!g2.running() && g2.done() >= 1, "2002's item drawn: " + g2.status());
        CHECK(slurp(gtt::log_file(w2.out)).find("retry") != std::string::npos, "the silent player's item was retried (log)");
        CHECK(own_back(w2), "own values back");
    });

    run_case("Turbo X gear pictures: cancel restores; a scanned-only run refuses hair; a missing column fails the start", [&] {
        GtWorld w;
        w.out = fresh_dir("gtt_cancel");
        w.svc.answer = false;
        gtt::Generator g(w.env());
        gtt::Request rq;
        std::string err;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":1},{"field":"shoetypecode","id":2}]})", rq, &err);
        CHECK(g.start(rq, {gt_ref(2002), gt_ref(2003)}, &err), "started: " + err);
        w.run(g, 5, 1.0);
        CHECK(g.running(), "waiting for the game");
        g.cancel();
        CHECK(!g.running() && own_back(w), "cancelled, own values back");
        gtt::Generator g2(w.env());
        gtt::parse_request(R"({"steps":[{"field":"hairtypecode","id":2071}]})", rq, &err);
        CHECK(!g2.start(rq, {gt_ref(2002, 0)}, &err), "only hair, only a scanned head: nothing to draw: " + err);
        gtt::parse_request(R"({"steps":[{"field":"gkglovetypecode","id":3}]})", rq, &err);
        CHECK(!g2.start(rq, {gt_ref()}, &err) && err.find("could not be read") != std::string::npos, "no such column: refused: " + err);
    });

    run_case("Turbo X gear pictures: a stale capture (the previous look's pixels) is not saved, retried once, kept when the game catches up", [&] {
        GtWorld w;
        w.out = fresh_dir("gtt_stale_once");
        w.svc.replay_at[2002] = {2};  // his 2nd picture repeats his 1st
        gtt::Generator g(w.env());
        gtt::Request rq;
        std::string err;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":1},{"field":"shoetypecode","id":2},{"field":"shoetypecode","id":3}]})", rq, &err);
        CHECK(g.start(rq, {gt_ref()}, &err), "started: " + err);
        w.run(g, 2000);
        const auto& boots = *gtt::find_field("shoetypecode");
        CHECK(!g.running() && g.done() == 3 && g.failed() == 0, "3 pictures, none failed: " + g.status());
        CHECK(w.svc.requests.size() == 4, "4 requests (boots 2 drawn again): " + std::to_string(w.svc.requests.size()));
        CHECK(slurp(gtt::picture_file(w.out, boots, 1)) != slurp(gtt::picture_file(w.out, boots, 2)), "boots 2's picture is not boots 1's");
        const std::string log = slurp(gtt::log_file(w.out));
        CHECK(log.find("retry (stale capture (same as previous look)") != std::string::npos, "run.log names the stale capture and the retry");
        CHECK(own_back(w), "own values back");
    });

    run_case("Turbo X gear pictures: a player whose picture never changes: the repeat fails as stale in the manifest; other players are not compared", [&] {
        GtWorld w;
        w.out = fresh_dir("gtt_stale_frozen");
        w.svc.frozen = {2002};
        gtt::Generator g(w.env());
        gtt::Request rq;
        std::string err;
        gtt::parse_request(R"({"steps":[{"field":"shoetypecode","id":1},{"field":"shoetypecode","id":2}]})", rq, &err);
        CHECK(g.start(rq, {gt_ref()}, &err), "started: " + err);
        w.run(g, 2000);
        const auto& boots = *gtt::find_field("shoetypecode");
        CHECK(!g.running() && g.done() == 1 && g.items().size() == 2, "boots 1 saved, the run ended: " + g.status());
        CHECK(g.items()[1].state == "failed" && g.items()[1].error == "stale capture (same as previous look)", "boots 2 failed as stale: " + g.items()[1].state + " / " + g.items()[1].error);
        CHECK(!fs::exists(gtt::picture_file(w.out, boots, 2)), "boots 2 has no (stale) picture");
        CHECK(w.svc.requests.size() == 3, "boots 2 tried twice: " + std::to_string(w.svc.requests.size()) + " requests");
        const std::string man = slurp(gtt::manifest_file(w.out));
        CHECK(man.find("\"error\": \"stale capture (same as previous look)\"") != std::string::npos && man.find("\"state\": \"failed\"") != std::string::npos,
              "manifest.json: failed, \"stale capture (same as previous look)\"");
        CHECK(slurp(gtt::log_file(w.out)).find("rounds in a row without a picture") == std::string::npos, "stale answers are answers: the run was not stopped as silent");
        CHECK(own_back(w), "own values back");

        GtWorld w2;  // two players whose pictures are the same pixels: one look each in one round, both kept
        w2.out = fresh_dir("gtt_stale_two");
        w2.svc.frozen = {2002, 2003};
        gtt::Generator g2(w2.env());
        CHECK(g2.start(rq, {gt_ref(2002), gt_ref(2003)}, &err), "started: " + err);
        w2.run(g2, 2000);
        CHECK(!g2.running() && g2.done() == 2 && w2.svc.requests.size() == 1, "one round, both kept (only a player's own captures are compared): " + g2.status());
    });
}

}  // namespace gearthumbs_test

inline void test_gear_thumbs() { gearthumbs_test::cases(); }
