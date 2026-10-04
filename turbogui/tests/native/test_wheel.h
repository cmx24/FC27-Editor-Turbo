// Native tests (1.1.3): where a mouse wheel notch comes from (core/wheel.h). In 1.1.2 nothing scrolled or zoomed in
// game: the low-level wheel hook was skipped once one legacy mouse message had been seen, and a hook Windows removed
// was never noticed. Included by test_main.cpp.
#pragma once
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
}
