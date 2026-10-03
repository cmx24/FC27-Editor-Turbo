// FC 27 LE Turbo GUI - memory access abstraction.
// The Windows build reads/writes its own process through Read/WriteProcessMemory (invalid addresses
// fail instead of crashing); tests use a simulated address space.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace turbo {

constexpr uint64_t kMinPtr = 0x10000ull;
constexpr uint64_t kMaxPtr = 0x7FFFFFFEFFFFull;

inline bool is_ptr(uint64_t p, uint64_t align = 1) {
    return p >= kMinPtr && p <= kMaxPtr && (p % align) == 0;
}

class Memory {
public:
    virtual ~Memory() = default;
    virtual bool read(uint64_t addr, void* out, size_t n) = 0;
    virtual bool write(uint64_t addr, const void* in, size_t n) = 0;

    template <class T> bool rd(uint64_t addr, T& v) {
        if (!is_ptr(addr)) return false;
        return read(addr, &v, sizeof(T));
    }
    template <class T> bool wr(uint64_t addr, const T& v) {
        if (!is_ptr(addr)) return false;
        return write(addr, &v, sizeof(T));
    }
    // Pointer stored at addr, 0 when unreadable or not pointer-shaped.
    uint64_t ptr(uint64_t addr, uint64_t align = 8) {
        uint64_t v = 0;
        if (!rd(addr, v)) return 0;
        return is_ptr(v, align) ? v : 0;
    }
    // Follow base -> [+off0] -> [+off1] ...; 0 if any hop fails
    uint64_t chain(uint64_t base, const std::vector<uint64_t>& offs) {
        uint64_t cur = base;
        for (uint64_t o : offs) {
            if (!is_ptr(cur)) return 0;
            cur = ptr(cur + o);
            if (!cur) return 0;
        }
        return cur;
    }
    bool read_block(uint64_t addr, size_t n, std::vector<uint8_t>& out) {
        out.resize(n);
        if (n == 0) return true;
        if (!is_ptr(addr)) return false;
        return read(addr, out.data(), n);
    }
    std::string read_cstr(uint64_t addr, size_t max_len) {
        std::vector<uint8_t> buf;
        if (!read_block(addr, max_len, buf)) return std::string();
        size_t len = 0;
        while (len < buf.size() && buf[len] != 0) ++len;
        return std::string(reinterpret_cast<const char*>(buf.data()), len);
    }
};

}  // namespace turbo
