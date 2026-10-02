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

// Installs the DX12 hooks; returns false (and logs why) if the overlay cannot run
bool start_overlay(HMODULE self);
void stop_overlay();

}  // namespace host
