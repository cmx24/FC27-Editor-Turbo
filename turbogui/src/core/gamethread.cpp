// FC 27 LE Turbo GUI - game-thread dispatcher pieces (see gamethread.h)
#include "gamethread.h"

#include <cstring>
#include <stdexcept>

namespace turbo {

// ---------------------------------------------------------------- JobQueue
bool JobQueue::push(std::function<void()> fn) {
    if (!fn) return false;
    bool dropped = false;
    std::lock_guard<std::mutex> lock(mutex_);
    while (cap_ > 0 && queue_.size() >= cap_) {
        queue_.pop_front();
        dropped = true;
        ++dropped_;
    }
    queue_.push_back(std::move(fn));
    return dropped;
}

size_t JobQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

size_t JobQueue::drain(size_t max_jobs, const std::function<void(const char*)>& on_error) {
    size_t budget;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        budget = queue_.size();  // jobs queued by the jobs themselves wait for the next drain
    }
    if (max_jobs > 0 && budget > max_jobs) budget = max_jobs;
    size_t n = 0;
    while (n < budget) {
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) break;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        ++n;
        try {
            job();
            ++ran_;
        } catch (const std::exception& e) {
            ++failed_;
            if (on_error) on_error(e.what());
        } catch (...) {
            ++failed_;
            if (on_error) on_error("unknown exception");
        }
    }
    return n;
}

// ---------------------------------------------------------------- inline hook detection
const char* inline_hook_name(InlineHook h) {
    switch (h) {
        case InlineHook::None: return "none";
        case InlineHook::JmpRel32: return "jmp rel32";
        case InlineHook::JmpIndirect: return "jmp [rip]";
        case InlineHook::MovabsJmp: return "movabs+jmp";
        case InlineHook::PushRet: return "push+ret";
        case InlineHook::Truncated: return "truncated";
    }
    return "?";
}

static int32_t rd_i32(const uint8_t* p) {
    uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    return static_cast<int32_t>(v);
}

InlineHook detect_inline_hook(const uint8_t* code, size_t len, uint64_t addr, uint64_t* target) {
    if (target) *target = 0;
    if (!code || len == 0) return InlineHook::Truncated;
    if (code[0] == 0xE9) {  // jmp rel32 (MinHook, Detours)
        if (len < 5) return InlineHook::Truncated;
        if (target) *target = addr + 5 + static_cast<int64_t>(rd_i32(code + 1));
        return InlineHook::JmpRel32;
    }
    if (code[0] == 0xFF && len >= 2 && code[1] == 0x25) {  // jmp qword ptr [rip+disp32]
        if (len < 6) return InlineHook::Truncated;
        if (target) *target = addr + 6 + static_cast<int64_t>(rd_i32(code + 2));
        return InlineHook::JmpIndirect;
    }
    if (code[0] == 0x48 && len >= 2 && code[1] == 0xB8) {  // movabs rax, imm64 ; jmp rax
        if (len < 12) return InlineHook::Truncated;
        if (code[10] == 0xFF && code[11] == 0xE0) {
            uint64_t v = 0;
            std::memcpy(&v, code + 2, 8);
            if (target) *target = v;
            return InlineHook::MovabsJmp;
        }
        return InlineHook::None;
    }
    if (code[0] == 0x68) {  // push imm32 ; mov dword [rsp+4], imm32 ; ret
        if (len < 14) return InlineHook::Truncated;
        if (code[5] == 0xC7 && code[6] == 0x44 && code[7] == 0x24 && code[8] == 0x04 && code[13] == 0xC3) {
            uint64_t lo = static_cast<uint32_t>(rd_i32(code + 1));
            uint64_t hi = static_cast<uint32_t>(rd_i32(code + 9));
            if (target) *target = lo | (hi << 32);
            return InlineHook::PushRet;
        }
        return InlineHook::None;
    }
    return InlineHook::None;
}

// ---------------------------------------------------------------- synthetic career event
static void fill_self(uint8_t* block) {
    const uint64_t self = reinterpret_cast<uint64_t>(block);
    for (size_t off = 0; off + 8 <= kSyntheticBlock; off += 8) std::memcpy(block + off, &self, 8);
}

void build_synthetic_event(SyntheticEvent& s, void* noop, int32_t type) {
    for (size_t i = 0; i < kSyntheticVtableSlots; ++i) s.vtable[i] = noop;
    fill_self(s.wrapper);
    fill_self(s.dispatcher);
    fill_self(s.event);
    void* vt = static_cast<void*>(s.vtable);
    void* disp = static_cast<void*>(s.dispatcher);
    std::memcpy(s.wrapper, &disp, 8);     // wrapper->dispatcher
    std::memcpy(s.dispatcher, &vt, 8);    // dispatcher->vtable
    std::memcpy(s.event, &vt, 8);         // event->vtable
    const uint64_t zero = 0;
    std::memcpy(s.event + 8, &zero, 8);   // reference count 0: nothing to release
    const uint64_t t = static_cast<uint64_t>(static_cast<uint32_t>(type));
    std::memcpy(s.event + kSyntheticTypeOffset, &t, 8);
}

bool verify_synthetic_event(const SyntheticEvent& s, void* noop, int32_t type) {
    for (size_t i = 0; i < kSyntheticVtableSlots; ++i)
        if (s.vtable[i] != noop) return false;
    uint64_t v = 0;
    std::memcpy(&v, s.wrapper, 8);
    if (v != reinterpret_cast<uint64_t>(s.dispatcher)) return false;
    std::memcpy(&v, s.dispatcher, 8);
    if (v != reinterpret_cast<uint64_t>(s.vtable)) return false;
    std::memcpy(&v, s.event, 8);
    if (v != reinterpret_cast<uint64_t>(s.vtable)) return false;
    std::memcpy(&v, s.event + 8, 8);
    if (v != 0) return false;
    std::memcpy(&v, s.event + kSyntheticTypeOffset, 8);
    if (v != static_cast<uint64_t>(static_cast<uint32_t>(type))) return false;
    auto self_filled = [](const uint8_t* block, size_t from) {
        const uint64_t self = reinterpret_cast<uint64_t>(block);
        for (size_t off = from; off + 8 <= kSyntheticBlock; off += 8) {
            uint64_t q = 0;
            std::memcpy(&q, block + off, 8);
            if (q != self) return false;
        }
        return true;
    };
    return self_filled(s.wrapper, 8) && self_filled(s.dispatcher, 8) && self_filled(s.event, 0x18);
}

}  // namespace turbo
