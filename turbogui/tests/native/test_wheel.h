// Native tests (1.1.3): where a mouse wheel notch comes from (core/wheel.h). In 1.1.2 nothing scrolled or zoomed in
// game: the low-level wheel hook was skipped once one legacy mouse message had been seen, and a hook Windows removed
// was never noticed. Included by test_main.cpp.
#pragma once
#include <atomic>
#include <chrono>
#include <thread>

#include "core/wheel.h"

static void test_wheel_sources() {
    run_case("wheel 1.1.3: notches from the hook's mouseData and raw input's usButtonData (signed, 120 per notch)", [&] {
        CHECK(wheel_notches_hiword(0x00780000ul) == 1.0f && wheel_notches_hiword(0xFF880000ul) == -1.0f,
              "one notch up / down in the high word");
        CHECK(wheel_notches_hiword(0x00F0FFFFul) == 2.0f, "the low word is ignored");
        CHECK(wheel_notches_word(120) == 1.0f && wheel_notches_word(static_cast<unsigned short>(-240)) == -2.0f,
              "raw input's word read as signed (down is not +545 notches)");
    });

    run_case("wheel 1.1.3: one live source at a time (hook, else raw input, else window messages)", [&] {
        CHECK(!wheel_hook_alive(false, 10.0, 9.9) && !wheel_hook_alive(true, 10.0, -1.0), "not installed / never called");
        CHECK(wheel_hook_alive(true, 10.0, 9.0) && !wheel_hook_alive(true, 10.0, 7.5), "alive while called lately");
        // hook alive: only the hook, whatever legacy messages were seen (1.1.2 dropped the hook after one legacy click)
        CHECK(wheel_take(WheelSource::LowLevelHook, true, true) && wheel_take(WheelSource::LowLevelHook, true, false),
              "the hook's notch is taken even after a legacy mouse message");
        CHECK(!wheel_take(WheelSource::RawInput, true, false) && !wheel_take(WheelSource::WindowMessage, true, true),
              "nothing taken twice while the hook is alive");
        // hook gone: raw input when the game sends no legacy messages, else the window message
        CHECK(wheel_take(WheelSource::RawInput, false, false), "hook gone: raw input carries the wheel");
        CHECK(!wheel_take(WheelSource::RawInput, false, true) && wheel_take(WheelSource::WindowMessage, false, true),
              "hook gone and legacy messages arrive: the window message, raw input not counted again");
    });

    run_case("wheel 1.1.3: a hook that went quiet while the mouse moved is installed again (rate limited)", [&] {
        CHECK(!wheel_hook_reinstall(true, 10.0, 9.5, 9.6, -1.0), "alive: no reinstall");
        CHECK(!wheel_hook_reinstall(true, 10.0, 5.0, 4.0, -1.0), "quiet but the mouse did not move since: no reinstall");
        CHECK(wheel_hook_reinstall(true, 10.0, 5.0, 9.9, -1.0), "quiet while the mouse moved: reinstall");
        CHECK(wheel_hook_reinstall(true, 10.0, -1.0, 9.9, -1.0), "never called though the mouse moved: reinstall");
        CHECK(!wheel_hook_reinstall(true, 10.0, 5.0, 9.9, 7.0) && wheel_hook_reinstall(true, 12.5, 5.0, 12.4, 7.0),
              "not more often than every 5 seconds");
        CHECK(!wheel_hook_reinstall(false, 10.0, 5.0, 9.9, -1.0) && !wheel_hook_reinstall(true, 10.0, 5.0, -1.0, -1.0),
              "not installed / no move seen yet");
    });

    // 2.16 gui/perf: the hook procedure never waits for a lock: notches are summed with atomic adds and taken per frame
    run_case("wheel 2.16: notches summed lock-free (Ctrl apart, last position, take resets, clamped to a WM_MOUSEWHEEL word)", [&] {
        WheelSums w;
        int plain = 1, ctrl = 1, x = 0, y = 0;
        w.take(plain, ctrl, x, y);
        CHECK(plain == 0 && ctrl == 0, "nothing summed: 0 and 0");
        w.add(120, false, 10, 20);
        w.add(120, false, 11, 21);
        w.add(-120, true, -5, 1600);
        w.take(plain, ctrl, x, y);
        CHECK(plain == 240 && ctrl == -120, fmt("two notches up, one Ctrl notch down (%d, %d)", plain, ctrl));
        CHECK(x == -5 && y == 1600, fmt("the last notch's position, negative x kept (%d, %d)", x, y));
        w.take(plain, ctrl, x, y);
        CHECK(plain == 0 && ctrl == 0, "taken: the sums start again from 0");
        for (int i = 0; i < 400; ++i) w.add(120, false, 0, 0);
        for (int i = 0; i < 400; ++i) w.add(-120, true, 0, 0);
        w.take(plain, ctrl, x, y);
        CHECK(plain == 32767 && ctrl == -32767, fmt("clamped to a signed word (%d, %d)", plain, ctrl));
        // threads adding while the render thread takes: nothing lost, nothing counted twice
        WheelSums m;
        std::atomic<bool> go{false};
        std::vector<std::thread> adders;
        for (int t = 0; t < 4; ++t)
            adders.emplace_back([&] {
                while (!go.load()) {
                }
                for (int i = 0; i < 5000; ++i) m.add(1, (i & 1) != 0, i, i);  // 10,000 per sum: under the word clamp
            });
        long long got_plain = 0, got_ctrl = 0;
        go = true;
        for (int i = 0; i < 2000; ++i) {
            int a = 0, b = 0;
            m.take(a, b, x, y);
            got_plain += a;
            got_ctrl += b;
        }
        for (auto& t : adders) t.join();
        int a = 0, b = 0;
        m.take(a, b, x, y);
        got_plain += a;
        got_ctrl += b;
        CHECK(got_plain == 10000 && got_ctrl == 10000, fmt("4 threads x 5000 adds: %lld + %lld taken", got_plain, got_ctrl));
    });

    run_case("wheel 2.16: hook lateness from the event stamp (wrap-safe, odd stamps dropped, thresholds, max)", [&] {
        CHECK(hook_late_ms(1000, 990) == 10, "10 ms late");
        CHECK(hook_late_ms(5, 0xFFFFFFF6ul) == 15, "across the 32-bit tick wrap: 15 ms");
        CHECK(hook_late_ms(1000, 1010) > 60000, "a stamp ahead of the clock reads as huge (then dropped)");
        HookLateness l;
        l.record(0);
        l.record(16);
        l.record(kHookLateMs);
        l.record(kHookVeryLateMs + 50);
        l.record(-1);
        l.record(hook_late_ms(1000, 1010));
        CHECK(l.calls.load() == 4, fmt("four counted, odd stamps dropped (%lld)", l.calls.load()));
        CHECK(l.late.load() == 2 && l.very_late.load() == 1, fmt("late %lld, very late %lld", l.late.load(), l.very_late.load()));
        CHECK(l.max_ms.load() == kHookVeryLateMs + 50 && l.sum_ms.load() == 16 + kHookLateMs + kHookVeryLateMs + 50, "max and sum");
    });

    run_case("wheel 2.16: the hook procedure's own work is O(1) and cheap (timed: a stamp, a lateness record, a notch)", [&] {
        WheelSums w;
        HookLateness l;
        const int n = 2000000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) {
            l.record(hook_late_ms(static_cast<unsigned long>(i + 3), static_cast<unsigned long>(i)));
            if ((i & 7) == 0) w.add(120, (i & 16) != 0, i, -i);
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / n;
        std::printf("    PERF hook procedure core (lateness record + 1 notch in 8): %.1f ns per mouse event\n", ns);
        CHECK(l.calls.load() == n, "every event recorded");
        CHECK(ns < 2000.0, fmt("well under a microsecond per event on any machine (%.1f ns; Windows waits up to LowLevelHooksTimeout)", ns));
    });
}
