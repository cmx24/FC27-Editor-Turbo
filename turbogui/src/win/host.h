// FC 27 LE Turbo GUI - Windows host (logging, process memory, overlay entry points)
#pragma once
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/mem.h"
#include "core/memmap.h"

namespace turbo {
class App;
}

namespace host {

// Folder that holds turbo_config.json (the Live Editor folder)
std::filesystem::path le_root();
void log(const char* fmt, ...);

// Memory access to our own process through Read/WriteProcessMemory: a bad address returns false
class ProcessMemory : public turbo::Memory {
public:
    bool read(uint64_t addr, void* out, size_t n) override;
    bool write(uint64_t addr, const void* in, size_t n) override;
};

// Crash guard: turbo_output\turbo_gui_start.flag exists while Turbo does something inside the game that could crash it
// (probing Direct3D 12 in the game, installing hooks, the first 300 frames, the first frames drawn on screen). Waiting for
// Live Editor / the game window and running TurboProbe.exe (a separate process) are not guarded. If the game dies in a
// guarded phase the file stays: the next start tries once more (the flag is rewritten as "RETRY: ..."), and after a second
// failure in a row Turbo stays off until the file is deleted. guard_hold/guard_release are counted; a clean exit removes it.
void guard_hold(const char* why);
void guard_release();

// Real seconds since the first call (real_clock_win.cpp), for the dev service's waits and the frame cost lines (App::now stays
// the game's clock). Live Editor's speedhack scales the game's QueryPerformanceCounter and GetTickCount64 (menu speed 3 by
// default; its "Menu Speed +" hotkey, numpad 9, sets 50): in the driver runs of 2026-10-31 every frame-time line read 50x and
// the dev service's "5 s" open_page wait lasted about 0.1 s. This clock reads the system time (GetSystemTimeAsFileTime,
// what GetLocalTime and the log's stamps read; those stayed real in the same runs). Monotonic: when the system time is set back
// it stands still. Resolution: the system timer tick (1 ms while the game holds a 1 ms timer, 15.6 ms at most). Thread-safe.
double real_seconds();
// QueryPerformanceCounter seconds (scaled by the speedhack; high resolution): for short durations, divided by qpc_speed()
double qpc_seconds();
// How fast QueryPerformanceCounter runs against real_seconds(), measured over windows of at least 1 real second (1 = not
// scaled). qpc_speed_update() once per frame (overlay thread); qpc_speed() is the last measurement (any thread).
void qpc_speed_update();
double qpc_speed();

// Time the DLL was loaded: bridge files older than this minus 2 minutes belong to an earlier game session
std::filesystem::file_time_type load_time();

// Installs the DX12 hooks; returns false (and logs why) if the overlay cannot run
bool start_overlay(HMODULE self);

// Input shield (input_shield.cpp): while Turbo owns the mouse / keyboard the game's DirectInput and raw-input polls
// return no input. Installed after the overlay hooks (MinHook initialised); best effort.
void install_input_shield();
void input_shield_report();
// Turbo's own input reads (render thread, while it builds its frame) bypass the shield: wrap them in this scope.
struct TurboInputScope {
    TurboInputScope();
    ~TurboInputScope();
};
// Readable-memory map for Turbo's Lua side (memmap_win.cpp, core/memmap.h): allocated, its address stored in the
// mailbox, refreshed every second by a background thread. Returns the map address (0 if it could not be allocated).
uint64_t start_memmap(uint64_t mailbox);
// Committed, readable, private (non-image, non-mapped) regions of at least min_size bytes, merged and sorted: what the
// commentary-bank capture scans (core/commentary_bank.h)
std::vector<turbo::Region> private_regions(uint64_t min_size);

// What the overlay wants right now (window shown and the mouse over it / a text field active)
bool input_block_mouse();
bool input_block_keyboard();
// The show/hide key while Turbo is shown and just after a toggle: the game never sees it (input_shield.cpp)
bool input_block_vk(int vk);
int input_hidden_vk();  // that key right now, or 0
// A wheel entry the game read with GetRawInputBuffer (input shield): Turbo takes it while the low-level wheel hook is
// gone (core/wheel.h). `delta` = RAWMOUSE::usButtonData, 120 per notch.
void input_raw_wheel(unsigned short delta);
void stop_overlay();
// The game window the overlay draws on (null before the overlay found it)
HWND game_window();
// Dev service (devtools_win.cpp, core/devops.h): memory tools answering turbo_output\turbo_dev_request.json
void start_devtools();
// Miniface from the game's 3D model (player_capture_win.cpp, core/player_capture.h): resolves the game's capture entry
// points, installs the learning hooks and gives the App its capture service. Call after install_game_hooks().
void install_player_capture(turbo::App& app);
// Turbo X 1.0.3 lane CG (core/player_capture.h SubmitGate): the overlay's ResizeBuffers / ResizeBuffers1 hooks and a replaced swap
// chain call player_capture_display_reinit; every presented frame calls player_capture_frame. While the display settles no new
// capture request is submitted to the game (logged "player capture: paused (display re-init: ...)" / "resumed ...").
void player_capture_display_reinit(const char* what);
void player_capture_frame();

}  // namespace host
