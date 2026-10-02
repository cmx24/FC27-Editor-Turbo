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

// Crash guard: turbo_output\turbo_gui_start.flag exists while a risky phase (hooks installed but not yet proven by a
// number of good frames; first frames drawn on screen) is running. If the game dies in that phase the file stays, and the
// next start refuses to hook (see init_thread). guard_hold/guard_release are counted; a clean process exit removes it.
void guard_hold(const char* why);
void guard_release();

// Installs the DX12 hooks; returns false (and logs why) if the overlay cannot run
bool start_overlay(HMODULE self);
void stop_overlay();

}  // namespace host
