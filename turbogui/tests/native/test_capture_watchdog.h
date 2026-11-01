// Turbo X 1.0.1 lane A: the player-capture request watchdog (core/player_capture.h mark_ran / seconds_since_ran / answer_overdue,
// used by src/win/player_capture_win.cpp run_request and poll). At e797310 run_request published `ran` BEFORE it recorded the start
// time, so a poll on the GUI thread between the two writes read the clock's epoch and reported "did not finish the capture in 75 s"
// 1.4 s after the request (turbo_gui.log 2026-10-31 21:08:25). Included by test_main.cpp after test_gear_thumbs.h.
#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <thread>

#include "core/player_capture.h"

namespace capture_watchdog_test {
namespace pc = turbo::capture;
using clk = std::chrono::steady_clock;

inline std::string win_source() {
    const fs::path p = fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "win" / "player_capture_win.cpp";
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The body of `name(` up to the matching closing brace ("" when not found)
inline std::string function_body(const std::string& src, const std::string& head) {
    const size_t at = src.find(head);
    if (at == std::string::npos) return std::string();
    const size_t open = src.find('{', at);
    if (open == std::string::npos) return std::string();
    int depth = 0;
    for (size_t i = open; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}' && --depth == 0) return src.substr(open, i - open + 1);
    }
    return std::string();
}

inline void cases() {
    run_case("Turbo X capture watchdog: right after ran is published the check never reports a timeout", [&] {
        std::atomic<bool> ran{false};
        clk::time_point t_ran{};
        const clk::time_point t0 = clk::now();
        CHECK(pc::seconds_since_ran(ran, t_ran, t0) < 0.0 && !pc::answer_overdue(pc::seconds_since_ran(ran, t_ran, t0), false, 75.0),
              "not run yet: no elapsed time, no timeout");
        pc::mark_ran(ran, t_ran, t0);
        const double s0 = pc::seconds_since_ran(ran, t_ran, t0);
        CHECK(ran.load() && s0 == 0.0 && !pc::answer_overdue(s0, false, 75.0), "published: 0 s since it ran, no timeout");
        const double s76 = pc::seconds_since_ran(ran, t_ran, t0 + std::chrono::seconds(76));
        CHECK(pc::answer_overdue(s76, false, 75.0) && s76 > 75.9 && s76 < 76.1, "76 s later and not finished: timeout, 76 s elapsed");
        CHECK(!pc::answer_overdue(s76, true, 75.0), "finished: never a timeout");
        CHECK(!pc::answer_overdue(pc::seconds_since_ran(ran, t_ran, t0 + std::chrono::seconds(74)), false, 75.0), "74 s: no timeout yet");

        // two threads: the game thread publishes request after request, the watchdog checks each one the moment it sees `ran`
        constexpr int kJobs = 4000;
        struct Slot {
            std::atomic<bool> ran{false};
            clk::time_point t_ran{};
        };
        std::unique_ptr<Slot[]> slots(new Slot[kJobs]);
        std::thread game([&] {
            for (int i = 0; i < kJobs; ++i) pc::mark_ran(slots[i].ran, slots[i].t_ran, clk::now());
        });
        int false_timeouts = 0;
        double worst = 0.0;
        for (int i = 0; i < kJobs; ++i) {
            double s = -1.0;
            while ((s = pc::seconds_since_ran(slots[i].ran, slots[i].t_ran, clk::now())) < 0.0) std::this_thread::yield();
            if (pc::answer_overdue(s, false, 75.0)) ++false_timeouts;
            worst = std::max(worst, s);
        }
        game.join();
        CHECK(false_timeouts == 0 && worst < 5.0, "4000 requests checked as soon as ran was seen: no timeout (" + std::to_string(false_timeouts) +
                                                      "), worst elapsed " + std::to_string(worst) + " s");
    });

    run_case("Turbo X capture watchdog: the Windows host records the start time before ran and reports the real elapsed time", [&] {
        const std::string src = win_source();
        CHECK(!src.empty(), "src/win/player_capture_win.cpp read");
        const std::string run = function_body(src, "void run_request(Job* job)");
        CHECK(!run.empty(), "run_request found");
        CHECK(run.find("job->ran = true") == std::string::npos && run.find("job->ran.store(") == std::string::npos,
              "run_request does not publish ran by hand (before its start time)");
        const size_t mark = run.find("mark_ran(job->ran, job->t_ran,");
        const size_t cancel = run.find("if (job->cancelled) return;");
        CHECK(mark != std::string::npos && cancel != std::string::npos && mark < cancel, "run_request: mark_ran (time, then ran) is its first step");
        const std::string poll = function_body(src, "bool poll(Result& out) override");
        CHECK(poll.find("seconds_since_ran(j->ran, j->t_ran, now)") != std::string::npos && poll.find("answer_overdue(since, j->finished, kAnswerTimeout)") != std::string::npos,
              "poll: the watchdog reads ran (acquire) before t_ran and decides with answer_overdue");
        CHECK(poll.find("did not finish the capture in 75 s") == std::string::npos && poll.find("std::string(t) + \" s after the request ran") != std::string::npos,
              "poll: the timeout message carries the real elapsed seconds, not a fixed 75 s");
    });
}

}  // namespace capture_watchdog_test

inline void test_capture_watchdog() { capture_watchdog_test::cases(); }
