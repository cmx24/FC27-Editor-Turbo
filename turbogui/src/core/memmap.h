// FC 27 LE Turbo GUI - readable-memory map for Turbo's Lua side.
//
// Live Editor's Lua memory natives (MEMORY:ReadInt, ReadPointer, ...) crash the game on an address that is not
// readable (seen in FC 27: Turbo's search for the squad-role list read 0x3600000028 and the game died inside
// FCLiveEditor.DLL, 02-10-2026). Lua cannot ask Windows which memory is readable; Turbo.dll can. Every second it
// publishes the process's readable regions here, and Turbo's Lua side reads only addresses inside them
// (lua\libs\v2\imports\turbo\core\mem.lua).
//
// Layout (VirtualAlloc'ed by Turbo.dll; its address is stored at mailbox +0x18):
//   +0x00 u32 magic 'TRMM'   +0x04 u32 version 1   +0x08 u32 seq (odd while being rewritten)
//   +0x0C u32 count          +0x10 u32 capacity     +0x14 u32 reserved
//   +0x20 entries: count x { u64 start, u64 end }  sorted, non-overlapping, end exclusive
// Lua reads seq, looks the address up (binary search), reads seq again: a changed or odd seq means retry.
#pragma once
#include <cstdint>
#include <vector>

#include "mem.h"

namespace turbo {

constexpr uint32_t kMapMagic = 0x4D4D5254;  // "TRMM" little-endian
constexpr uint32_t kMapVersion = 1;
constexpr uint64_t kMapHeader = 0x20;
constexpr uint64_t kMailboxMapPtr = 0x18;  // mailbox field that holds the map's address

struct Region {
    uint64_t start = 0, end = 0;  // [start, end)
};

// Sort, drop empty / non-user-mode parts, merge touching or overlapping regions
std::vector<Region> merge_regions(std::vector<Region> regions);

// Bytes needed for a map holding `capacity` regions
inline uint64_t map_bytes(uint32_t capacity) { return kMapHeader + uint64_t(capacity) * 16; }

// Write regions into the map at addr (seq made odd, entries, count, seq even). More regions than the capacity: the
// highest ones are left out (reading them is refused, never allowed wrongly). Returns false if memory writes fail.
bool publish_map(Memory& mem, uint64_t addr, uint32_t capacity, const std::vector<Region>& regions);

// Reader with the same algorithm as Lua's mem.readable (used by tests to check both sides agree)
bool map_contains(Memory& mem, uint64_t map, uint64_t addr, uint64_t len);

}  // namespace turbo
