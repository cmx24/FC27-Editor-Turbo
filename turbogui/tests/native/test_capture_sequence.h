// Native tests (2.17 looks, track C): the full-body picture request and the resumable write -> picture -> save sequence
// (core/player_capture.h: body_request, kCameraBody / kCameraBodyKit, capture::Sequence, the manifest). A fake capture
// service stands in for the game (src/win/player_capture_win.cpp); a fake clock drives the timeouts.
// Included by test_main.cpp (after FakeCapture); test_capture_sequence() is called from main next to test_player_capture().
#pragma once
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace capture_seq_test {

namespace tc = turbo::capture;

// The game's capture service as the sequence sees it: every request is recorded; with auto_ok the picture for the requested
// id is ready at the next poll; answers can also be queued by hand
struct SeqFake : tc::CaptureService {
    tc::Status st;
    std::vector<tc::Request> requests;
    std::deque<tc::Result> answers;
    bool auto_ok = true;
    bool auto_fail = false;  // answer every request with a failed Result
    bool refuse = false;
    int cancels = 0;
    int polls = 0;
    bool can_draw = true;  // status().available (with st.busy false)
    tc::Status status() override {
        tc::Status s = st;
        s.installed = true;
        s.available = can_draw && !s.busy;
        if (!s.available && s.reason.empty()) s.reason = s.busy ? "rendering..." : "the game's frontend renderer is not up";
        return s;
    }
    bool request(const tc::Request& r, std::string* err) override {
        if (refuse) {
            if (err) *err = "the game's frontend renderer is not up";
            return false;
        }
        requests.push_back(r);
        if (auto_ok || auto_fail) {
            tc::Result a;
            a.id = r.id;
            a.label = r.label;
            a.ok = !auto_fail;
            if (a.ok) {
                a.image.w = a.image.h = 2;
                a.image.px.assign(16, 200);
                a.format = "PNG 1024x1024";
                a.bytes = 736000;
            } else {
                a.error = "the game did not start the capture (controller state 0 after the request)";
            }
            answers.push_back(a);
        }
        return true;
    }
    bool poll(tc::Result& out) override {
        ++polls;
        if (answers.empty()) return false;
        out = answers.front();
        answers.pop_front();
        return true;
    }
    void cancel() override { ++cancels; }
    const tc::Template* learned() override { return nullptr; }
};

// Everything a run did, in order: "manifest", "write 6", "save 6", "restore"
struct Rig {
    SeqFake svc;
    double t = 0.0;
    std::vector<std::string> calls;
    std::vector<std::string> manifests;  // every text handed to write_manifest
    std::function<tc::StepResult(int64_t, bool, std::string*)> on_write;
    std::function<tc::StepResult(bool, std::string*)> on_restore;
    bool save_ok = true;
    bool manifest_ok = true;
    int manifest_fail_after = -1;  // >= 0: write_manifest fails from that call on
    std::vector<int64_t> wearing;  // codes put on the reference, in order (restore pushes original)
    int64_t original_seen = -99;

    tc::SequenceHooks hooks(const tc::Sequence** self) {
        tc::SequenceHooks h;
        h.write = [this](int64_t code, bool first, std::string* err) {
            calls.push_back("write " + std::to_string(code) + (first ? "" : " again"));
            if (on_write) return on_write(code, first, err);
            wearing.push_back(code);
            return tc::StepResult::Done;
        };
        h.save = [this](int64_t code, const tc::Result& r, std::string* file, std::string* err) {
            calls.push_back("save " + std::to_string(code) + " " + r.format);
            if (!save_ok) {
                *err = "disk full";
                return false;
            }
            *file = std::to_string(code) + ".png";
            return true;
        };
        h.restore = [this, self](bool first, std::string* err) {
            calls.push_back(std::string("restore") + (first ? "" : " again"));
            if (*self) original_seen = (*self)->original();
            if (on_restore) return on_restore(first, err);
            wearing.push_back(original_seen);
            return tc::StepResult::Done;
        };
        h.write_manifest = [this](const std::string& text, std::string* err) {
            calls.push_back("manifest");
            if (!manifest_ok || (manifest_fail_after >= 0 && int(manifests.size()) >= manifest_fail_after)) {
                *err = "access denied";
                return false;
            }
            manifests.push_back(text);
            return true;
        };
        h.clock = [this]() { return t; };
        h.stamp = []() { return std::string("2026-10-17 22:00:00"); };
        return h;
    }
    // tick until the run is over (or max ticks), 0.1 s per tick
    int run(tc::Sequence& s, int max_ticks = 2000) {
        int n = 0;
        while (s.running() && n < max_ticks) {
            s.tick();
            t += 0.1;
            ++n;
        }
        return n;
    }
    int count_calls(const std::string& prefix) const {
        int n = 0;
        for (const std::string& c : calls)
            if (c.rfind(prefix, 0) == 0) ++n;
        return n;
    }
};

inline std::vector<tc::SequenceStep> seq_steps(std::initializer_list<int64_t> codes) {
    std::vector<tc::SequenceStep> v;
    for (int64_t c : codes) v.push_back({c, "Body " + std::to_string(c)});
    return v;
}

inline tc::SequenceOptions seq_opts(int64_t original = 3) {
    tc::SequenceOptions o;
    o.player = 254470;
    o.team = 95;
    o.camera = tc::kCameraBody;
    o.original = original;
    return o;
}

inline const tc::ManifestEntry* seq_find(const std::vector<tc::ManifestEntry>& v, int64_t code) {
    for (const tc::ManifestEntry& e : v)
        if (e.code == code) return &e;
    return nullptr;
}

}  // namespace capture_seq_test

static void test_capture_sequence() {
    using namespace capture_seq_test;

    run_case("capture sequence: body request (mode 1 presets, default descriptor, his team)", [&] {
        CHECK(tc::cameras().size() > size_t(tc::kCameraBodyKit), "body tc::cameras exist");
        CHECK(tc::cameras()[tc::kCameraBody].mode == 1 && tc::cameras()[tc::kCameraBody].extra == 0, "kCameraBody = mode 1, extra 0");
        CHECK(tc::cameras()[tc::kCameraBodyKit].mode == 1 && tc::cameras()[tc::kCameraBodyKit].extra == 3, "kCameraBodyKit = mode 1, extra 3");
        tc::Request r = tc::body_request(208596, 95);
        CHECK(r.id == 208596 && r.second_id == 95 && !r.manager && !r.use_template && r.camera == tc::kCameraBodyFull, "fields (full body by default)");
        CHECK(r.batch.empty() && r.mode_override == -1 && r.extra_override == -1, "single, no overrides");
        CHECK(r.label == "player 208596", "label: " + r.label);
        // a learned template (the hub's avatar portrait: +0x68 set, mode 0) must not leak into a body picture
        tc::Template t;
        t.learned = true;
        t.desc = tc::default_desc(9999, 1, false, true);
        t.desc.set_i32(0x30, 4242);
        t.mode = 0;
        t.extra = 0;
        tc::Plan p = tc::plan_request(r, &t);
        const tc::PlayerDesc want = tc::default_desc(208596, 95, true, false);
        CHECK(std::memcmp(p.desc.b, want.b, tc::kPlayerDescSize) == 0, "default descriptor: " + tc::describe_desc(p.desc));
        CHECK(p.mode == 1 && p.extra == 0 && p.type == -1 && p.note == "default descriptor, full-body camera not configured: head and shoulders",
              "no table given: head and shoulders, says so: " + p.note);
        tc::Request rf = tc::body_request(208596, 95, tc::kCameraBodyFull);
        p = tc::plan_request(rf, &t);
        CHECK(p.mode == 1 && p.extra == 0 && p.type == -1 && p.note == "default descriptor, full-body camera not configured: head and shoulders",
              "full body without a table: head and shoulders, says so: " + p.note);
        r = tc::body_request(208596, 95, tc::kCameraBodyKit, "Saka");
        p = tc::plan_request(r, &t);
        CHECK(p.mode == 1 && p.extra == 3 && r.label == "Saka", "kit camera + label");
        r = tc::body_request(208596, -7, 0);
        CHECK(r.camera == tc::kCameraBodyFull && r.second_id == -1, "other camera -> full body; no team -> -1");
        r = tc::body_request(208596, 95, tc::kCameraBody);
        p = tc::plan_request(r, &t);
        CHECK(r.camera == tc::kCameraBody && p.mode == 1 && p.extra == 0 && p.type == -1 && p.note == "default descriptor", "head-and-shoulders camera kept");
        std::string err;
        CHECK(tc::check_request(tc::body_request(208596, 95), 6, &err), "passes the service's request check: " + err);
        CHECK(!tc::check_request(tc::body_request(0, 95), 6, &err), "id 0 refused");
    });

    run_case("capture sequence: the full-body camera table (body_camera.txt) and its plan", [&] {
        tc::BodyCamera b;
        std::string err;
        CHECK(tc::parse_body_camera("# camera-2 probe, W22xx\ntype = 2\nmode=1\nextra = 0x0\ndesc 0x24 = 2   # LE's camera\ndesc 76 = -1\n", b, &err) && b.ok,
              "parsed: " + err);
        CHECK(b.type == 2 && b.mode == 1 && b.extra == 0 && b.desc.size() == 2 && b.desc[0].off == 0x24 && b.desc[0].value == 2 &&
                  b.desc[1].off == 76 && b.desc[1].value == -1 && !b.desc[0].team,
              "values: " + tc::describe_body_camera(b));
        CHECK(tc::describe_body_camera(b) == "type 2, mode 1, extra 0, +24=2, +4C=-1", "describe: " + tc::describe_body_camera(b));
        CHECK(tc::parse_body_camera("desc 0x30 = 0xFFFFFFFF", b, &err) && b.ok && b.type == -1 && b.desc[0].value == -1, "a dword alone, hex");
        CHECK(tc::parse_body_camera("desc 0x0C = team\nmode = 2", b, &err) && b.ok && b.desc[0].team && b.mode == 2, "team value: " + err);
        CHECK(tc::describe_body_camera(b) == "type -1, mode 2, extra 0, +0C=team", "describe team: " + tc::describe_body_camera(b));
        CHECK(!tc::parse_body_camera("type = team", b, &err) && err.find("desc line only") != std::string::npos, "team only for desc: " + err);
        CHECK(tc::parse_body_camera("# nothing yet\n\n", b, &err) && !b.ok && err.empty(), "comments only: not configured, no error");
        CHECK(tc::parse_body_camera("", b, &err) && !b.ok, "empty");
        CHECK(tc::parse_body_camera("mode = 1\n", b, &err) && !b.ok, "mode alone says nothing about the camera");
        struct Bad {
            const char* text;
            const char* why;
        };
        const Bad bad[] = {{"type 2", "no '='"}, {"type = two", "not a number"}, {"type = 256", "type must be"}, {"mode = 16", "mode must be"},
                           {"extra = -1", "extra must be"}, {"desc 0x00 = 5", "0x04..0x68"}, {"desc 0x6C = 5", "0x04..0x68"},
                           {"desc 0x26 = 5", "multiple of 4"}, {"desc 0x08 = 95", "second id"}, {"desc 0x24 = 1\ndesc 0x24 = 2", "twice"},
                           {"camera = 2", "unknown key"}, {"desc = 2", "unknown key"}, {"type = 0x1FFFFFFFF", "not a number"}};
        for (const Bad& x : bad) {
            b.ok = true;
            CHECK(!tc::parse_body_camera(x.text, b, &err) && !b.ok && err.find(x.why) != std::string::npos, std::string(x.text) + " -> " + err);
        }
        CHECK(!tc::parse_body_camera("type = 1\n\ntype = 300", b, &err) && err.rfind("line 3:", 0) == 0, "names the line: " + err);
        std::string nine;
        for (int i = 0; i < 9; ++i) nine += "desc " + std::to_string(0x0C + 4 * i) + " = 1\n";
        CHECK(!tc::parse_body_camera(nine, b, &err) && err.find("at most 8") != std::string::npos, "9 dwords: " + err);
        // the plan: table values on top of the default descriptor; ids and flags stay the request's
        CHECK(tc::parse_body_camera("type = 2\nmode = 1\nextra = 5\ndesc 0x24 = 2\n", b, &err) && b.ok, "table");
        tc::Request r = tc::body_request(254470, 95, tc::kCameraBodyFull);
        tc::Plan p = tc::plan_request(r, nullptr, &b);
        tc::PlayerDesc want = tc::default_desc(254470, 95, true, false);
        want.set_i32(0x24, 2);
        CHECK(std::memcmp(p.desc.b, want.b, tc::kPlayerDescSize) == 0 && p.type == 2 && p.mode == 1 && p.extra == 5, "plan: " + tc::describe_desc(p.desc));
        CHECK(p.note.find("full-body camera (type 2, mode 1, extra 5, +24=2)") != std::string::npos, "note: " + p.note);
        // the built-in camera is the one W2228 proved: the default player descriptor + 0C = team, +50 = 2, mode 0, extra 0
        const tc::BodyCamera full = tc::body_camera_builtin();
        CHECK(full.ok && full.mode == 0 && full.extra == 0 && full.type == -1 && full.source.find("W2228") != std::string::npos, "built in: mode 0, extra 0");
        CHECK(tc::describe_body_camera(full) == "type -1, mode 0, extra 0, +0C=team, +50=2, +58=0, +5C=-1", "describe: " + tc::describe_body_camera(full));
        p = tc::plan_request(tc::body_request(254470, 95), nullptr, &full);
        // the W2228 log line of the request that drew the full body (22:31:35)
        CHECK(tc::describe_desc(p.desc) == "+00=254470 +04=-1 +08=95 +0C=95 +10=-1 +20=-1 +24=1 +4C=-1 +50=2 +54=-1 +5C=-1 +64=1",
              "W2228 descriptor: " + tc::describe_desc(p.desc));
        CHECK(p.mode == 0 && p.extra == 0 && p.type == -1 && p.desc.flag64() && !p.desc.flag68(), "mode 0 / extra 0, player flags");
        CHECK(tc::hex_bytes(p.desc.b, 0x10) == "06 E2 03 00 FF FF FF FF 5F 00 00 00 5F 00 00 00", "first 16 bytes as LE's (W2220 hex): " + tc::hex_bytes(p.desc.b, 0x10));
        p = tc::plan_request(tc::body_request(254470, -1), nullptr, &full);
        CHECK(p.desc.i32(0x0C) == -1 && p.desc.i32(0x50) == 2, "no team: +0C stays -1");
        // the table is used only by the full-body camera; a manager head keeps his flags under it
        r.camera = tc::kCameraBody;
        p = tc::plan_request(r, nullptr, &b);
        CHECK(p.type == -1 && p.desc.i32(0x24) == 1 && p.extra == 0, "other cameras ignore the table");
        tc::Request m = tc::body_request(235732, 1, tc::kCameraBodyFull);
        m.manager = true;
        p = tc::plan_request(m, nullptr, &b);
        CHECK(!p.desc.flag64() && p.desc.id() == 235732 && p.type == 2, "manager flags kept");
        // a batch: every head gets the table
        tc::Request batch = tc::body_request(1, 2, tc::kCameraBodyFull);
        batch.batch = {{11, 2, false, "a"}, {12, 2, false, "b"}};
        const std::vector<tc::Plan> plans = tc::plan_batch(batch, nullptr, &b);
        CHECK(plans.size() == 2 && plans[1].type == 2 && plans[1].desc.i32(0x24) == 2 && plans[1].desc.id() == 12, "batch");
        // a sequence on the full-body camera: its manifest names the camera, not a preset's mode / extra
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::SequenceOptions o = seq_opts();
        o.camera = tc::kCameraBodyFull;
        tc::Sequence s(rig.svc, o, seq_steps({6}), rig.hooks(&self));
        self = &s;
        CHECK(s.options().camera == tc::kCameraBodyFull && tc::SequenceOptions().camera == tc::kCameraBodyFull, "full body is the default");
        CHECK(s.start(&err), "start");
        rig.run(s);
        CHECK(s.count("done") == 1 && rig.svc.requests.size() == 1 && rig.svc.requests[0].camera == tc::kCameraBodyFull, "drawn on the full-body camera");
        CHECK(rig.manifests.back().find("\"camera\": -2") != std::string::npos && rig.manifests.back().find("\"mode\"") == std::string::npos,
              "manifest: camera -2, no preset mode");
    });

    run_case("capture sequence: the host's request ends once every head is handed out and the game is done (W2202)", [&] {
        using S = tc::Settle;
        CHECK(tc::settle_request(false, true, false, true, 99.0, 20.0) == S::Running, "a head still waiting: poll() ends it");
        CHECK(tc::settle_request(false, false, true, true, 99.0, 20.0) == S::Running, "failed, heads left to report: poll() ends it");
        CHECK(tc::settle_request(true, true, false, true, 0.5, 20.0) == S::Over, "picture handed, done fired: over");
        CHECK(tc::settle_request(true, false, true, true, 0.5, 20.0) == S::Over, "every head reported, request failed: over");
        CHECK(tc::settle_request(true, false, false, true, 5.0, 20.0) == S::Running, "picture handed before the done callback: wait for it");
        CHECK(tc::settle_request(true, false, false, true, 20.5, 20.0) == S::TimedOut, "no done callback 20 s after it ran: timed out");
        CHECK(tc::settle_request(true, false, false, false, 99.0, 20.0) == S::Running, "never ran: not timed out here");
        // the book side: one head, its picture taken before the done callback -> every head handed out
        tc::BatchEntry e;
        e.id = 254470;
        tc::BatchBook book({e});
        CHECK(book.add_picture(254470, std::vector<uint8_t>(16, 1)) == 0 && !book.all_handed(), "picture in, not handed");
        tc::BatchBook::Picture pic;
        CHECK(book.take_picture(pic) && book.all_handed(), "handed: the editor stops polling here");
        CHECK(tc::settle_request(book.all_handed(), true, false, true, 1.0, 20.0) == S::Over, "done fires later: status() ends it");
    });

    run_case("capture sequence: three codes, in order, manifest after every step, his code back at the end", [&] {
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::Sequence s(rig.svc, seq_opts(3), seq_steps({6, 11, 6, 2}), rig.hooks(&self));  // 6 twice: once
        self = &s;
        CHECK(s.total() == 3 && !s.running(), "deduplicated, not started");
        std::string err;
        // the manifest says "pending" for a code BEFORE its write reaches the game
        rig.on_write = [&](int64_t code, bool, std::string*) {
            std::vector<tc::ManifestEntry> m;
            CHECK(tc::Sequence::parse_manifest(rig.manifests.back(), m, &err), "manifest readable at write: " + err);
            const tc::ManifestEntry* e = seq_find(m, code);
            CHECK(e && e->state == "pending" && e->attempts == 1, "pending before the write of " + std::to_string(code));
            rig.wearing.push_back(code);
            return tc::StepResult::Done;
        };
        CHECK(s.start(&err), "start: " + err);
        CHECK(!s.start(&err), "no second start");
        rig.run(s);
        CHECK(s.phase() == tc::Sequence::Phase::Finished && s.restored() && !s.running(), "finished and restored: " + s.message());
        CHECK(s.count("done") == 3 && s.count("failed") == 0, "3 done");
        CHECK(rig.svc.requests.size() == 3, "3 requests");
        for (const tc::Request& r : rig.svc.requests) {
            CHECK(r.id == 254470 && r.second_id == 95 && r.camera == tc::kCameraBody && !r.use_template && !r.manager, "body request for the reference");
        }
        CHECK(rig.svc.requests[1].label == "player 254470 as Body 11", "label names the code: " + rig.svc.requests[1].label);
        const std::vector<int64_t> wore = {6, 11, 2, 3};
        CHECK(rig.wearing == wore, "wrote 6, 11, 2, then his own 3");
        // order of the hooks for one code
        std::string seq;
        for (const std::string& c : rig.calls) seq += c + ";";
        CHECK(seq.find("manifest;write 6;save 6 PNG 1024x1024;manifest;manifest;write 11;") != std::string::npos, "order: " + seq);
        CHECK(rig.count_calls("restore") == 1, "one restore");
        CHECK(seq.size() > 17 && seq.substr(seq.size() - 17) == "restore;manifest;", "restore, then the last manifest: " + seq);
        std::vector<tc::ManifestEntry> m;
        int32_t player = 0;
        int camera = -1;
        int64_t original = -1;
        CHECK(tc::Sequence::parse_manifest(rig.manifests.back(), m, &err, &player, &camera, &original), "final manifest: " + err);
        CHECK(player == 254470 && camera == tc::kCameraBody && original == 3 && m.size() == 3, "header fields");
        const tc::ManifestEntry* e = seq_find(m, 11);
        CHECK(e && e->state == "done" && e->file == "11.png" && e->w == 2 && e->h == 2 && e->bytes == 736000 && e->format == "PNG 1024x1024" &&
                  e->attempts == 1 && e->label == "Body 11" && e->time == "2026-10-17 22:00:00",
              "entry 11");
        CHECK(rig.manifests.back().find("\"finished\": true") != std::string::npos && rig.manifests.back().find("\"restored\": true") != std::string::npos,
              "finished + restored in the file");
        CHECK(rig.manifests.back().find("\"mode\": 1") != std::string::npos && rig.manifests.back().find("\"extra\": 0") != std::string::npos,
              "mode / extra in the file");
    });

    run_case("capture sequence: a crash mid-picture resumes at that code, his own code comes from the manifest", [&] {
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::Sequence s(rig.svc, seq_opts(3), seq_steps({1, 2, 3}), rig.hooks(&self));
        self = &s;
        std::string err;
        CHECK(s.start(&err), "start");
        // code 1 done; code 2 written and requested, then the game dies before the picture
        rig.svc.auto_ok = true;
        int guard = 0;
        while (!(s.current_code() == 2 && s.phase() == tc::Sequence::Phase::Waiting) && guard++ < 200) {
            if (s.current_code() == 2) rig.svc.auto_ok = false;
            s.tick();
            rig.t += 0.1;
        }
        CHECK(s.phase() == tc::Sequence::Phase::Waiting && s.current_code() == 2, "waiting on code 2");
        const std::string crashed = rig.manifests.back();
        // relaunch: he still wears test code 2, the caller only knows that
        Rig rig2;
        const tc::Sequence* self2 = nullptr;
        tc::Sequence s2(rig2.svc, seq_opts(2), seq_steps({1, 2, 3}), rig2.hooks(&self2));
        self2 = &s2;
        CHECK(s2.resume(crashed, &err), "resume: " + err);
        CHECK(s2.original() == 3, "original from the manifest, not the leftover test code");
        CHECK(s2.entry(1) && s2.entry(1)->state == "done", "1 stays done");
        CHECK(s2.entry(2) && s2.entry(2)->state == "failed" && s2.entry(2)->attempts == 1 && s2.entry(2)->error.find("crashed") != std::string::npos,
              "2 counted as one attempt");
        // second crash on code 2
        CHECK(s2.start(&err), "start 2");
        guard = 0;
        rig2.svc.auto_ok = false;
        while (s2.phase() != tc::Sequence::Phase::Waiting && guard++ < 200) {
            s2.tick();
            rig2.t += 0.1;
        }
        CHECK(s2.current_code() == 2 && rig2.count_calls("write 1") == 0, "resumed at 2, 1 not redrawn");
        const std::string crashed2 = rig2.manifests.back();
        Rig rig3;
        const tc::Sequence* self3 = nullptr;
        tc::Sequence s3(rig3.svc, seq_opts(2), seq_steps({1, 2, 3}), rig3.hooks(&self3));
        self3 = &s3;
        CHECK(s3.resume(crashed2, &err), "resume 3: " + err);
        CHECK(s3.entry(2) && s3.entry(2)->state == "crashed" && s3.entry(2)->attempts == 2, "2 crashed twice: never again");
        CHECK(s3.start(&err), "start 3");
        rig3.run(s3);
        CHECK(rig3.count_calls("write 2") == 0 && rig3.count_calls("write 3") == 1 && rig3.count_calls("write 1") == 0, "only 3 drawn");
        CHECK(s3.phase() == tc::Sequence::Phase::Finished && s3.restored() && rig3.original_seen == 3, "restored to his own 3");
        CHECK(s3.count("done") == 2 && s3.count("crashed") == 1, "2 done, 1 crashed: " + s3.message());
    });

    run_case("capture sequence: resume refuses another player, another camera, bad text; keeps codes of other lists", [&] {
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::Sequence s(rig.svc, seq_opts(), seq_steps({5}), rig.hooks(&self));
        self = &s;
        std::string err;
        CHECK(s.start(&err), "start");
        rig.run(s);
        const std::string done = rig.manifests.back();
        tc::SequenceOptions other = seq_opts();
        other.player = 1;
        Rig r2;
        const tc::Sequence* x = nullptr;
        tc::Sequence s2(r2.svc, other, seq_steps({5}), r2.hooks(&x));
        CHECK(!s2.resume(done, &err) && err.find("player 254470") != std::string::npos, "other player: " + err);
        tc::SequenceOptions kit = seq_opts();
        kit.camera = tc::kCameraBodyKit;
        tc::Sequence s3(r2.svc, kit, seq_steps({5}), r2.hooks(&x));
        CHECK(!s3.resume(done, &err) && err.find("camera") != std::string::npos, "other camera: " + err);
        tc::Sequence s4(r2.svc, seq_opts(), seq_steps({5}), r2.hooks(&x));
        CHECK(!s4.resume("{nope", &err), "bad JSON");
        CHECK(!s4.resume("{\"version\":1}", &err), "no codes");
        CHECK(!s4.resume("{\"version\":2,\"codes\":{}}", &err), "other version");
        CHECK(!s4.resume("{\"version\":1,\"player\":254470,\"camera\":1,\"codes\":{\"x7\":{}}}", &err), "bad code key: " + err);
        CHECK(!s4.resume("{\"version\":1,\"player\":254470,\"camera\":1,\"codes\":{\"7\":{\"attempts\":\"two\"}}}", &err), "wrong type: " + err);
        // another list (codes 7, 8) resumes on top of the first: 5 is kept in the file, untouched
        Rig r5;
        const tc::Sequence* x5 = nullptr;
        tc::Sequence s5(r5.svc, seq_opts(), seq_steps({7, 8}), r5.hooks(&x5));
        x5 = &s5;
        CHECK(s5.resume(done, &err), "resume other list: " + err);
        CHECK(s5.total() == 2 && s5.entries().size() == 3, "2 to run, 3 in the file");
        CHECK(s5.start(&err), "start other list");
        r5.run(s5);
        std::vector<tc::ManifestEntry> m;
        CHECK(tc::Sequence::parse_manifest(r5.manifests.back(), m, &err) && m.size() == 3, "3 codes in the file");
        CHECK(seq_find(m, 5) && seq_find(m, 5)->state == "done" && seq_find(m, 7) && seq_find(m, 7)->state == "done", "5 kept, 7 done");
        CHECK(r5.count_calls("write 5") == 0, "5 not redrawn");
    });

    run_case("capture sequence: failures, three in a row stop the run and put his code back", [&] {
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::Sequence s(rig.svc, seq_opts(3), seq_steps({1, 2, 3, 4, 5}), rig.hooks(&self));
        self = &s;
        rig.svc.auto_ok = false;
        rig.svc.auto_fail = true;
        std::string err;
        CHECK(s.start(&err), "start");
        rig.run(s);
        CHECK(s.phase() == tc::Sequence::Phase::Aborted && s.restored(), "aborted + restored: " + s.message());
        CHECK(s.message().find("3 codes in a row") != std::string::npos, "says why: " + s.message());
        CHECK(s.count("failed") == 3 && s.count("todo") == 2 && rig.svc.requests.size() == 3, "3 failed, 2 never tried");
        CHECK(s.entry(1)->error.find("controller state") != std::string::npos, "the game's reason kept: " + s.entry(1)->error);
        CHECK(rig.wearing.back() == 3, "his own code last");
        // a success in between resets the count
        Rig r2;
        const tc::Sequence* x = nullptr;
        tc::Sequence s2(r2.svc, seq_opts(), seq_steps({1, 2, 3, 4, 5}), r2.hooks(&x));
        x = &s2;
        r2.svc.auto_ok = false;
        int n = 0;
        r2.on_write = [&](int64_t code, bool, std::string*) {
            r2.svc.auto_fail = (++n % 3) != 0;  // fail, fail, ok, fail, fail
            r2.svc.auto_ok = !r2.svc.auto_fail;
            return tc::StepResult::Done;
        };
        CHECK(s2.start(&err), "start 2");
        r2.run(s2);
        CHECK(s2.phase() == tc::Sequence::Phase::Finished && s2.count("done") == 1 && s2.count("failed") == 4, "ran to the end: " + s2.message());
        // a refused request and a failed save are step failures too
        Rig r3;
        const tc::Sequence* x3 = nullptr;
        tc::Sequence s3(r3.svc, seq_opts(), seq_steps({1}), r3.hooks(&x3));
        x3 = &s3;
        r3.svc.refuse = true;
        CHECK(s3.start(&err), "start 3");
        r3.run(s3);
        CHECK(s3.entry(1)->state == "failed" && s3.entry(1)->error.find("refused") != std::string::npos && s3.restored(), "refused: " + s3.entry(1)->error);
        Rig r4;
        const tc::Sequence* x4 = nullptr;
        tc::Sequence s4(r4.svc, seq_opts(), seq_steps({1}), r4.hooks(&x4));
        x4 = &s4;
        r4.save_ok = false;
        CHECK(s4.start(&err), "start 4");
        r4.run(s4);
        CHECK(s4.entry(1)->state == "failed" && s4.entry(1)->error.find("disk full") != std::string::npos, "save failed: " + s4.entry(1)->error);
    });

    run_case("capture sequence: timeouts (no picture, busy service, slow write) and pictures for other ids", [&] {
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::Sequence s(rig.svc, seq_opts(), seq_steps({1}), rig.hooks(&self));
        self = &s;
        rig.svc.auto_ok = false;
        std::string err;
        CHECK(s.start(&err), "start");
        // a picture for someone else while waiting: dropped, still waiting
        int guard = 0;
        while (s.phase() != tc::Sequence::Phase::Waiting && guard++ < 50) s.tick();
        tc::Result other;
        other.id = 99;
        other.ok = true;
        rig.svc.answers.push_back(other);
        s.tick();
        CHECK(s.phase() == tc::Sequence::Phase::Waiting && s.message().find("id 99") != std::string::npos, "other id dropped: " + s.message());
        rig.t += 24.0;
        s.tick();
        CHECK(s.phase() == tc::Sequence::Phase::Waiting && rig.svc.cancels == 0, "still waiting at 24 s");
        rig.t += 1.5;
        s.tick();
        CHECK(rig.svc.cancels == 1 && s.entry(1)->state == "failed" && s.entry(1)->error.find("no picture after 25 s") != std::string::npos,
              "timeout cancels: " + s.entry(1)->error);
        rig.run(s);
        CHECK(s.phase() == tc::Sequence::Phase::Finished && s.restored(), "finished");
        // busy: no request while another picture is in flight; then it goes
        Rig r2;
        const tc::Sequence* x2 = nullptr;
        tc::Sequence s2(r2.svc, seq_opts(), seq_steps({1}), r2.hooks(&x2));
        x2 = &s2;
        r2.svc.st.busy = true;
        CHECK(s2.start(&err), "start 2");
        for (int i = 0; i < 30; ++i) {
            s2.tick();
            r2.t += 0.1;
        }
        CHECK(r2.svc.requests.empty() && s2.phase() == tc::Sequence::Phase::Requesting, "waits while busy");
        r2.svc.st.busy = false;
        r2.run(s2);
        CHECK(r2.svc.requests.size() == 1 && s2.count("done") == 1, "went once free");
        Rig r3;
        const tc::Sequence* x3 = nullptr;
        tc::Sequence s3(r3.svc, seq_opts(), seq_steps({1}), r3.hooks(&x3));
        x3 = &s3;
        r3.svc.st.busy = true;
        CHECK(s3.start(&err), "start 3");
        r3.run(s3, 400);
        CHECK(r3.svc.requests.empty() && s3.entry(1)->state == "failed" && s3.entry(1)->error.find("busy") != std::string::npos, "busy too long");
        // the game cannot draw (a match, no menu renderer): wait, then say why; once it can, the request goes
        Rig r6;
        const tc::Sequence* x6 = nullptr;
        tc::Sequence s6(r6.svc, seq_opts(), seq_steps({1, 2}), r6.hooks(&x6));
        x6 = &s6;
        r6.svc.can_draw = false;
        CHECK(s6.start(&err), "start 6");
        for (int i = 0; i < 260 && s6.count("failed") == 0; ++i) {
            s6.tick();
            r6.t += 0.1;
        }
        CHECK(r6.svc.requests.empty() && s6.entry(1)->state == "failed" && s6.entry(1)->error.find("could not draw for 25 s: the game's frontend renderer is not up") != std::string::npos,
              "cannot draw: " + s6.entry(1)->error);
        r6.svc.can_draw = true;
        r6.run(s6);
        CHECK(r6.svc.requests.size() == 1 && s6.entry(2)->state == "done", "drawn once the game can");
        // a write that answers Wait a few times, then Done; one that never finishes; one that fails
        Rig r4;
        const tc::Sequence* x4 = nullptr;
        tc::Sequence s4(r4.svc, seq_opts(), seq_steps({1, 2, 3}), r4.hooks(&x4));
        x4 = &s4;
        r4.on_write = [&](int64_t code, bool first, std::string* e) {
            if (code == 1) return first ? tc::StepResult::Wait : tc::StepResult::Done;
            if (code == 2) return tc::StepResult::Wait;
            *e = "value out of range";
            return tc::StepResult::Failed;
        };
        CHECK(s4.start(&err), "start 4");
        r4.run(s4);
        CHECK(s4.entry(1)->state == "done" && r4.count_calls("write 1 again") == 1, "wait then done");
        CHECK(s4.entry(2)->state == "failed" && s4.entry(2)->error.find("did not finish") != std::string::npos, "slow write: " + s4.entry(2)->error);
        CHECK(s4.entry(3)->state == "failed" && s4.entry(3)->error.find("value out of range") != std::string::npos && r4.svc.requests.size() == 1,
              "failed write: no request");
    });

    run_case("capture sequence: cancel, restore failures, manifest failures, refused starts", [&] {
        Rig rig;
        const tc::Sequence* self = nullptr;
        tc::Sequence s(rig.svc, seq_opts(3), seq_steps({1, 2}), rig.hooks(&self));
        self = &s;
        rig.svc.auto_ok = false;
        std::string err;
        CHECK(s.start(&err), "start");
        int guard = 0;
        while (s.phase() != tc::Sequence::Phase::Waiting && guard++ < 50) s.tick();
        s.cancel();
        CHECK(rig.svc.cancels == 1 && s.entry(1)->state == "cancelled" && s.entry(1)->attempts == 0, "cancel drops the request, attempt not counted");
        rig.run(s);
        CHECK(s.phase() == tc::Sequence::Phase::Cancelled && s.restored() && rig.wearing.back() == 3, "cancelled + restored");
        CHECK(rig.count_calls("write 2") == 0, "nothing after the cancel");
        // a cancelled code is drawn on resume
        Rig r1;
        const tc::Sequence* x1 = nullptr;
        tc::Sequence s1(r1.svc, seq_opts(3), seq_steps({1, 2}), r1.hooks(&x1));
        x1 = &s1;
        CHECK(s1.resume(rig.manifests.back(), &err) && s1.start(&err), "resume after cancel: " + err);
        r1.run(s1);
        CHECK(s1.count("done") == 2, "both drawn after the cancel");
        // restore fails / never finishes: said loudly
        Rig r2;
        const tc::Sequence* x2 = nullptr;
        tc::Sequence s2(r2.svc, seq_opts(), seq_steps({1}), r2.hooks(&x2));
        x2 = &s2;
        r2.on_restore = [](bool, std::string* e) {
            *e = "player row locked";
            return tc::StepResult::Failed;
        };
        CHECK(s2.start(&err), "start 2");
        r2.run(s2);
        CHECK(!s2.restored() && !s2.running() && s2.message().find("COULD NOT BE PUT BACK: player row locked") != std::string::npos, s2.message());
        CHECK(r2.manifests.back().find("\"restored\": false") != std::string::npos, "file says not restored");
        Rig r3;
        const tc::Sequence* x3 = nullptr;
        tc::Sequence s3(r3.svc, seq_opts(), seq_steps({1}), r3.hooks(&x3));
        x3 = &s3;
        r3.on_restore = [](bool, std::string*) { return tc::StepResult::Wait; };
        CHECK(s3.start(&err), "start 3");
        r3.run(s3);
        CHECK(!s3.restored() && !s3.running() && s3.message().find("NOT PUT BACK IN TIME") != std::string::npos, s3.message());
        // the manifest cannot be written: stop before writing anything more, restore
        Rig r4;
        const tc::Sequence* x4 = nullptr;
        tc::Sequence s4(r4.svc, seq_opts(), seq_steps({1, 2}), r4.hooks(&x4));
        x4 = &s4;
        r4.manifest_fail_after = 2;  // start + "pending 1" go through, "done 1" does not
        CHECK(s4.start(&err), "start 4");
        r4.run(s4);
        CHECK(s4.phase() == tc::Sequence::Phase::Aborted && s4.restored() && r4.count_calls("write 2") == 0, "aborted: " + s4.message());
        Rig r5;
        const tc::Sequence* x5 = nullptr;
        tc::Sequence s5(r5.svc, seq_opts(), seq_steps({1}), r5.hooks(&x5));
        r5.manifest_ok = false;
        CHECK(s5.start(&err), "start 5");
        r5.run(s5);
        CHECK(s5.phase() == tc::Sequence::Phase::Aborted && r5.count_calls("write") == 0 && r5.count_calls("restore") == 1, "no manifest: no write");
        // refused starts
        tc::SequenceOptions none = seq_opts();
        none.player = 0;
        tc::Sequence s6(r5.svc, none, seq_steps({1}), r5.hooks(&x5));
        CHECK(!s6.start(&err) && err.find("reference") != std::string::npos, "no player: " + err);
        tc::SequenceHooks h = r5.hooks(&x5);
        h.restore = nullptr;
        tc::Sequence s7(r5.svc, seq_opts(), seq_steps({1}), h);
        CHECK(!s7.start(&err) && err.find("missing") != std::string::npos, "no restore hook: " + err);
        // his own code must be known (else the end of the run cannot put it back); a resumed manifest can supply it
        Rig r9;
        const tc::Sequence* x9 = nullptr;
        tc::Sequence s9(r9.svc, seq_opts(-1), seq_steps({1}), r9.hooks(&x9));
        CHECK(!s9.start(&err) && err.find("his own code is not known") != std::string::npos && r9.calls.empty(), "unknown original: " + err);
        Rig r10;
        const tc::Sequence* x10 = nullptr;
        tc::Sequence s10(r10.svc, seq_opts(-1), seq_steps({1}), r10.hooks(&x10));
        x10 = &s10;
        CHECK(s10.resume("{\"version\":1,\"player\":254470,\"camera\":1,\"original\":4,\"codes\":{}}", &err) && s10.original() == 4, "original from the manifest");
        CHECK(s10.start(&err), "start with the manifest's original: " + err);
        r10.run(s10);
        CHECK(s10.restored() && r10.original_seen == 4, "restored to 4");
        // an empty list: straight to restore, finished
        Rig r8;
        const tc::Sequence* x8 = nullptr;
        tc::Sequence s8(r8.svc, seq_opts(), {}, r8.hooks(&x8));
        CHECK(s8.start(&err), "start empty");
        r8.run(s8);
        CHECK(s8.phase() == tc::Sequence::Phase::Finished && s8.restored() && r8.svc.requests.empty(), "empty list");
    });
}
