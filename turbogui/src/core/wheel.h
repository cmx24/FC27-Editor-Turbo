// FC 27 LE Turbo GUI - where a mouse wheel notch comes from (Windows host: overlay_dx12.cpp, input shield).
// FC 27 reads the mouse through raw input, so no window message carries the wheel while Turbo is shown. A low-level
// mouse hook on its own thread sees every notch, but Windows silently removes such a hook once one call is late (the
// game busy loading, every thread frozen while hooks are installed), and up to 1.1.2 the hook was also skipped for the
// whole session once a single legacy mouse message had been seen: then nothing scrolled or zoomed. The host now takes
// the wheel from exactly one live source (the hook while it is alive, else raw input, else window messages) and puts
// the hook back when it has gone quiet while the mouse moved.
// Header-only, no Win32: the decisions are tested natively (tests/native/test_wheel.h).
// 2.16 gui/perf: Windows routes EVERY mouse event of the desktop through a low-level hook's thread and waits for it, so a
// hook whose thread is late lags the pointer system-wide (Cassio 2026-10-17: "the mouse pointer lags like crazy"; his
// 10-minute logs counted 28 to 82 "installed again" per session: Windows had dropped a late hook that often). The hook is
// now installed only while Turbo is shown (hidden: no hook at all), its thread runs at time-critical priority, and its
// procedure never waits: a notch is one atomic add (WheelSums), the lateness of each event is counted (HookLateness).
#pragma once
#include <atomic>

namespace turbo {

enum class WheelSource { LowLevelHook, RawInput, WindowMessage };

constexpr double kWheelHookQuietSec = 2.0;  // no hook call for this long = the hook is not taken as alive
constexpr double kWheelHookRetrySec = 5.0;  // at most one reinstall this often

// Wheel notches (positive = away from the user) in the high word of MSLLHOOKSTRUCT::mouseData or of WM_MOUSEWHEEL's
// wParam: a signed 16-bit value, 120 per notch
inline float wheel_notches_hiword(unsigned long data) {
    return static_cast<float>(static_cast<short>(static_cast<unsigned short>((data >> 16) & 0xFFFFu))) / 120.0f;
}
// ... in RAWMOUSE::usButtonData (RI_MOUSE_WHEEL): the same signed value in an unsigned word
inline float wheel_notches_word(unsigned short data) { return static_cast<float>(static_cast<short>(data)) / 120.0f; }

// The low-level hook counts as alive while it has been called (any mouse event) within the last kWheelHookQuietSec.
// Its call comes before the same event reaches raw input or the window, so a notch it queued is not queued again.
inline bool wheel_hook_alive(bool installed, double now, double last_hook_call) {
    return installed && last_hook_call >= 0.0 && now - last_hook_call < kWheelHookQuietSec;
}

// Does Turbo take a wheel notch from this source? One source at a time, so nothing scrolls twice.
inline bool wheel_take(WheelSource src, bool hook_alive, bool legacy_mouse_seen) {
    switch (src) {
        case WheelSource::LowLevelHook: return true;  // its own call proves it alive
        case WheelSource::RawInput: return !hook_alive && !legacy_mouse_seen;  // the window gets no WM_MOUSEWHEEL
        case WheelSource::WindowMessage: return !hook_alive;
    }
    return false;
}

// Put the hook back? The cursor moved (a real move calls the hook) after the hook's last call, the hook has been
// quiet for kWheelHookQuietSec, and the last try is kWheelHookRetrySec old. Times < 0 = never.
inline bool wheel_hook_reinstall(bool installed, double now, double last_hook_call, double last_cursor_move,
                                 double last_retry) {
    if (!installed || last_cursor_move < 0.0 || wheel_hook_alive(installed, now, last_hook_call)) return false;
    if (last_hook_call >= 0.0 && last_cursor_move <= last_hook_call) return false;  // the hook saw that move
    return last_retry < 0.0 || now - last_retry >= kWheelHookRetrySec;
}

// Wheel notches the hook saw, summed until the render thread takes them: one atomic add per notch, so the hook procedure
// never waits for a lock (it pushed into the window-message queue under its mutex before 2.16). Ctrl held at the notch
// (it zooms Turbo) is summed apart; the position is the last notch's.
struct WheelSums {
    std::atomic<int> plain{0}, ctrl{0};
    std::atomic<long long> at{0};  // x in the low 32 bits, y in the high 32 bits
    void add(int delta, bool ctrl_down, int x, int y) {
        at.store(static_cast<long long>(static_cast<unsigned int>(x)) | (static_cast<long long>(y) << 32), std::memory_order_relaxed);
        (ctrl_down ? ctrl : plain).fetch_add(delta, std::memory_order_relaxed);
    }
    // Takes both sums (0 = no notch since the last take) and the last position. A sum is clamped to what a WM_MOUSEWHEEL
    // word holds (+-32767, 273 notches).
    void take(int& plain_out, int& ctrl_out, int& x, int& y) {
        auto clamp = [](int v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; };
        plain_out = clamp(plain.exchange(0, std::memory_order_relaxed));
        ctrl_out = clamp(ctrl.exchange(0, std::memory_order_relaxed));
        const long long a = at.load(std::memory_order_relaxed);
        x = static_cast<int>(static_cast<unsigned int>(a & 0xFFFFFFFFll));
        y = static_cast<int>(a >> 32);
    }
};

// How late the hook's thread ran each mouse event: the event's time stamp (MSLLHOOKSTRUCT::time) against the tick count
// when the procedure ran, in ms (GetTickCount resolution, about 16 ms). Windows drops a hook once one call takes longer
// than LowLevelHooksTimeout; every late call before that is pointer lag. Odd stamps (0, ahead of the clock, over a minute)
// are not counted. Lock-free: the procedure records, the render thread reads.
constexpr long long kHookLateMs = 50;       // a pointer lag one can feel
constexpr long long kHookVeryLateMs = 200;  // close to LowLevelHooksTimeout: Windows drops the hook soon
struct HookLateness {
    std::atomic<long long> calls{0}, late{0}, very_late{0}, max_ms{0}, sum_ms{0};
    void record(long long ms) {
        if (ms < 0 || ms > 60000) return;
        calls.fetch_add(1, std::memory_order_relaxed);
        sum_ms.fetch_add(ms, std::memory_order_relaxed);
        if (ms >= kHookLateMs) late.fetch_add(1, std::memory_order_relaxed);
        if (ms >= kHookVeryLateMs) very_late.fetch_add(1, std::memory_order_relaxed);
        long long m = max_ms.load(std::memory_order_relaxed);
        while (ms > m && !max_ms.compare_exchange_weak(m, ms, std::memory_order_relaxed)) {
        }
    }
};
// The tick count when the procedure ran minus the event's stamp, both 32-bit millisecond ticks (wrap-safe)
inline long long hook_late_ms(unsigned long tick_now, unsigned long event_time) {
    return static_cast<long long>(static_cast<unsigned int>(static_cast<unsigned int>(tick_now) - static_cast<unsigned int>(event_time)));
}

}  // namespace turbo
