// FC 27 LE Turbo GUI - publishes the game process's readable memory for Turbo's Lua side (see core/memmap.h).
// A background thread walks the address space with VirtualQuery once a second and rewrites the map; it never reads
// game memory itself.
#include <windows.h>

#include <vector>

#include "core/bridge.h"
#include "core/memmap.h"
#include "host.h"

namespace host {

static constexpr uint32_t kCapacity = 65536;

static bool readable(const MEMORY_BASIC_INFORMATION& m) {
    if (m.State != MEM_COMMIT) return false;
    const DWORD p = m.Protect;
    if (p & (PAGE_GUARD | PAGE_NOACCESS | PAGE_WRITECOMBINE)) return false;
    return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                 PAGE_EXECUTE_WRITECOPY)) != 0;
}

static std::vector<turbo::Region> walk() {
    std::vector<turbo::Region> out;
    uint64_t addr = turbo::kMinPtr;
    MEMORY_BASIC_INFORMATION m{};
    while (addr < turbo::kMaxPtr && VirtualQuery(reinterpret_cast<LPCVOID>(addr), &m, sizeof(m)) == sizeof(m)) {
        uint64_t base = reinterpret_cast<uint64_t>(m.BaseAddress), size = m.RegionSize;
        if (size == 0) break;
        if (readable(m)) out.push_back({base, base + size});
        if (base + size <= addr) break;
        addr = base + size;
    }
    return turbo::merge_regions(std::move(out));
}

static DWORD WINAPI map_thread(LPVOID p) {
    const uint64_t map = reinterpret_cast<uint64_t>(p);
    ProcessMemory mem;
    bool logged = false;
    for (;;) {
        std::vector<turbo::Region> regions = walk();
        bool ok = turbo::publish_map(mem, map, kCapacity, regions);
        if (!logged) {
            log("readable-memory map for Turbo's Lua side: %zu regions%s", regions.size(),
                regions.size() > kCapacity ? " (more than the map holds: the highest are left out)" : "");
            if (!ok) log("readable-memory map: writing it failed");
            logged = true;
        }
        Sleep(1000);
    }
    return 0;
}

std::vector<turbo::Region> private_regions(uint64_t min_size) {
    std::vector<turbo::Region> out;
    uint64_t addr = turbo::kMinPtr;
    MEMORY_BASIC_INFORMATION m{};
    while (addr < turbo::kMaxPtr && VirtualQuery(reinterpret_cast<LPCVOID>(addr), &m, sizeof(m)) == sizeof(m)) {
        uint64_t base = reinterpret_cast<uint64_t>(m.BaseAddress), size = m.RegionSize;
        if (size == 0) break;
        if (readable(m) && m.Type == MEM_PRIVATE && size >= min_size) out.push_back({base, base + size});
        if (base + size <= addr) break;
        addr = base + size;
    }
    return turbo::merge_regions(std::move(out));
}

uint64_t start_memmap(uint64_t mailbox) {
    void* map = VirtualAlloc(nullptr, static_cast<SIZE_T>(turbo::map_bytes(kCapacity)), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!map) {
        log("cannot allocate the readable-memory map; Turbo's memory-based tools stay off");
        return 0;
    }
    ProcessMemory mem;
    turbo::publish_map(mem, reinterpret_cast<uint64_t>(map), kCapacity, walk());
    if (mailbox) {
        uint64_t a = reinterpret_cast<uint64_t>(map);
        mem.wr(mailbox + turbo::kMailboxMapPtr, a);
    }
    HANDLE th = CreateThread(nullptr, 0, map_thread, map, 0, nullptr);
    if (th) CloseHandle(th);
    return reinterpret_cast<uint64_t>(map);
}

}  // namespace host
