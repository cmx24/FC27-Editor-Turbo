#include "memmap.h"

#include <algorithm>

namespace turbo {

std::vector<Region> merge_regions(std::vector<Region> regions) {
    std::vector<Region> out;
    for (auto& r : regions) {
        if (r.start < kMinPtr) r.start = kMinPtr;
        if (r.end > kMaxPtr + 1) r.end = kMaxPtr + 1;
    }
    regions.erase(std::remove_if(regions.begin(), regions.end(), [](const Region& r) { return r.end <= r.start; }),
                  regions.end());
    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) { return a.start < b.start; });
    for (const auto& r : regions) {
        if (!out.empty() && r.start <= out.back().end) {
            out.back().end = std::max(out.back().end, r.end);
        } else {
            out.push_back(r);
        }
    }
    return out;
}

bool publish_map(Memory& mem, uint64_t addr, uint32_t capacity, const std::vector<Region>& regions) {
    uint32_t seq = 0;
    mem.rd(addr + 8, seq);
    uint32_t magic = 0;
    mem.rd(addr, magic);
    if (magic != kMapMagic) seq = 0;
    uint32_t writing = (seq | 1u) + ((seq & 1u) ? 2u : 0u);  // next odd value
    if (!mem.wr(addr + 8, writing)) return false;
    uint32_t count = static_cast<uint32_t>(std::min<size_t>(regions.size(), capacity));
    std::vector<uint64_t> buf(size_t(count) * 2);
    for (uint32_t i = 0; i < count; ++i) {
        buf[size_t(i) * 2] = regions[i].start;
        buf[size_t(i) * 2 + 1] = regions[i].end;
    }
    if (count && !mem.write(addr + kMapHeader, buf.data(), buf.size() * 8)) return false;
    uint32_t zero = 0;
    bool ok = mem.wr(addr, kMapMagic) && mem.wr(addr + 4, kMapVersion) && mem.wr(addr + 0x0C, count) &&
              mem.wr(addr + 0x10, capacity) && mem.wr(addr + 0x14, zero);
    uint32_t done = writing + 1;  // even: complete
    return mem.wr(addr + 8, done) && ok;
}

bool map_contains(Memory& mem, uint64_t map, uint64_t addr, uint64_t len) {
    if (len == 0) len = 1;
    if (addr + len < addr) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t magic = 0, s1 = 0, s2 = 0, count = 0, cap = 0;
        if (!mem.rd(map, magic) || magic != kMapMagic) return false;
        if (!mem.rd(map + 8, s1) || (s1 & 1u)) continue;
        if (!mem.rd(map + 0x0C, count) || !mem.rd(map + 0x10, cap) || count > cap) continue;
        bool found = false;
        uint32_t lo = 0, hi = count;  // first entry with end > addr
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            uint64_t end = 0;
            mem.rd(map + kMapHeader + uint64_t(mid) * 16 + 8, end);
            if (end > addr) hi = mid;
            else lo = mid + 1;
        }
        if (lo < count) {
            uint64_t start = 0, end = 0;
            mem.rd(map + kMapHeader + uint64_t(lo) * 16, start);
            mem.rd(map + kMapHeader + uint64_t(lo) * 16 + 8, end);
            found = start <= addr && addr + len <= end;
        }
        if (!mem.rd(map + 8, s2) || s2 != s1) continue;
        return found;
    }
    return false;
}

}  // namespace turbo
