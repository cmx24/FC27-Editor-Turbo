// FC 27 LE Turbo GUI - where a mouse wheel notch comes from (Windows host: overlay_dx12.cpp, input shield).
// FC 27 reads the mouse through raw input, so no window message carries the wheel while Turbo is shown. A low-level
// mouse hook on its own thread sees every notch, but Windows silently removes such a hook once one call is late (the
// game busy loading, every thread frozen while hooks are installed), and up to 1.1.2 the hook was also skipped for the
// whole session once a single legacy mouse message had been seen: then nothing scrolled or zoomed. The host now takes
// the wheel from exactly one live source (the hook while it is alive, else raw input, else window messages) and puts
// the hook back when it has gone quiet while the mouse moved.
// Header-only, no Win32: the decisions are tested natively (tests/native/test_wheel.h).
#pragma once

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

}  // namespace turbo
