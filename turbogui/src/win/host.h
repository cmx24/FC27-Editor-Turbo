// FC 27 LE Turbo GUI - Windows host (logging, process memory, overlay entry points)
#pragma once
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "core/mem.h"

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

// Time the DLL was loaded: bridge files older than this minus 2 minutes belong to an earlier game session
std::filesystem::file_time_type load_time();

// Installs the DX12 hooks; returns false (and logs why) if the overlay cannot run
bool start_overlay(HMODULE self);

// Input shield (input_shield.cpp): while Turbo owns the mouse / keyboard the game's DirectInput and raw-input polls
// return no input. Installed after the overlay hooks (MinHook initialised); best effort.
void install_input_shield();
void input_shield_report();
// What the overlay wants right now (window shown and the mouse over it / a text field active)
bool input_block_mouse();
bool input_block_keyboard();
void stop_overlay();

}  // namespace host
