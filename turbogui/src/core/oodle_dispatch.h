// FC 27 LE Turbo GUI - ONE detour on the game's Oodle decompressor, serving every Turbo feature that rewrites a decompressed block.
//
// Why: OodleLZ_Decompress of oo2core_9_win64.dll can carry only one MinHook detour. The 3D kit swap (core/kit_swap.h, textures and hotspot
// EBX) and the attribulator EBX patch (core/attrib_ebx.h, tactics / settings presets) both recognise a block by its compressed bytes and
// overwrite the decompressed output. Until 2.11 the attrib patch switched itself off when turbo_output\kits3d_on.txt existed. Now the host
// (win/oodle_shared_win.cpp) installs one detour that calls Dispatcher::run, and each feature registers a Handler in its own slot.
//
// Pure (no Windows): native tests in tests/native/test_oodle_dispatch.h.
//
// Contract of run() / dispatch() (the hot path: any game thread, no lock, no allocation, no file access, never throws):
//   1. the ORIGINAL decompress runs first with the game's own arguments and its return value is what the game gets, always;
//   2. then, for the registered slots in slot order (Kits, then Attrib):
//        on_call   every call, even when the handler is not live (counters; the attrib patch's "decompressed before the table" detection);
//        live      optional gate of THIS handler only (a per-feature kill switch); false = on_block is not called, nothing written;
//        on_block  the feature's own recognise-and-overwrite step (kitswap::Runtime::on_decompressed / attrib::Runtime::on_decompressed):
//                  0 = not ours, 1 = recognised (watch only / the original returned another size), 2 = recognised and overwritten;
//   3. no double patching: once a handler answered 2 for a call, the later slots get on_call but never on_block (counted as "skipped");
//      the two features' blocks are disjoint in the game (kit textures and kit hotspot EBX vs attribulator EBX), so this is a guard only;
//   4. an exception inside a slot is swallowed, counted for that slot and passed to its on_error; the other slots still run.
// Slots are fixed at compile time (the order IS the slot number), registered once and retired at most once, never re-used: a handler's
// storage lives as long as the process, so a detour that loaded a slot pointer just before a retire still reads valid memory.
#pragma once
#include <atomic>
#include <cstdint>
#include <exception>

namespace turbo {
namespace kitswap {
class Runtime;
}
namespace attrib {
class Runtime;
}

namespace oodle {

// int64 OodleLZ_Decompress(const void* comp, int64 compLen, void* raw, int64 rawLen, int fuzzSafe, int checkCRC, int verbosity,
//                          void* decBufBase, int64 decBufSize, void* fpCallback, void* callbackUserData, void* decoderMemory,
//                          int64 decoderMemorySize, int threadPhase)
using DecompressFn = int64_t (*)(const void*, int64_t, void*, int64_t, int, int, int, void*, int64_t, void*, void*, void*, int64_t, int);

// What the detour saw, after the original returned `ret`
struct Call {
    const uint8_t* comp = nullptr;
    int64_t comp_len = 0;
    uint8_t* raw = nullptr;
    int64_t raw_len = 0;
    int64_t ret = 0;
    uint32_t flags = 0;  // 1 = fpCallback was set, 2 = decBufBase, 4 = decoderMemory
    int phase = 0;       // threadPhase
};

enum Slot : int { kSlotKits = 0, kSlotAttrib = 1, kSlots = 2 };
const char* slot_name(int slot);

struct Handler {
    const char* name = "";
    void* user = nullptr;
    void (*on_call)(void* user, const Call& c) = nullptr;
    bool (*live)(void* user) = nullptr;
    int (*on_block)(void* user, const Call& c) = nullptr;
    void (*on_error)(void* user, const char* what) = nullptr;
};

struct SlotStats {
    uint64_t seen = 0;         // on_block answered 1
    uint64_t overwritten = 0;  // on_block answered 2
    uint64_t skipped = 0;      // on_block not called: an earlier slot had already overwritten the block
    uint64_t errors = 0;       // exceptions caught
    bool registered = false;
    bool retired = false;
};

class Dispatcher {
public:
    // Once per slot, before or after the hook is enabled. false: bad slot, a handler without on_block, or the slot was used before.
    bool add(int slot, const Handler& h);
    // The slot's handler is no longer called (a failed install). A retired slot is never registered again.
    void retire(int slot);
    bool has(int slot) const;
    int registered() const;  // slots with a live registration (neither empty nor retired)
    SlotStats stats(int slot) const;
    uint64_t calls() const { return calls_.load(std::memory_order_relaxed); }

    // The detour's whole body: the original first, then dispatch(). Returns what the original returned.
    int64_t run(DecompressFn orig, const void* comp, int64_t comp_len, void* raw, int64_t raw_len, int fuzz, int crc, int verb, void* dec_base, int64_t dec_size,
                void* cb, void* cb_user, void* dec_mem, int64_t dec_mem_size, int phase) noexcept {
        const int64_t r = orig(comp, comp_len, raw, raw_len, fuzz, crc, verb, dec_base, dec_size, cb, cb_user, dec_mem, dec_mem_size, phase);
        Call c;
        c.comp = static_cast<const uint8_t*>(comp);
        c.comp_len = comp_len;
        c.raw = static_cast<uint8_t*>(raw);
        c.raw_len = raw_len;
        c.ret = r;
        c.flags = (cb ? 1u : 0u) | (dec_base ? 2u : 0u) | (dec_mem ? 4u : 0u);
        c.phase = phase;
        dispatch(c);
        return r;
    }
    // After the original: 0 = nothing recognised, 1 = recognised, 2 = a block was overwritten
    int dispatch(const Call& c) noexcept;

private:
    struct Entry {
        Handler h;
        std::atomic<uint64_t> seen{0}, overwritten{0}, skipped{0}, errors{0};
        std::atomic<bool> used{false}, retired{false};
    };
    Entry entry_[kSlots];
    std::atomic<Entry*> live_[kSlots] = {};  // published (release) after the entry's handler is written
    std::atomic<uint64_t> calls_{0};
};

// ---------------------------------------------------------------- the two features' adaptors (thin: exactly the calls the two old detours made)
struct KitBinding {
    kitswap::Runtime* rt = nullptr;
    bool (*live)() = nullptr;                // the host's game_hook_live(kit_oodle); null = always
    void (*error)(const char* what) = nullptr;  // the host's game_hook_error
};
// on_call = Runtime::count_call(); on_block = Runtime::on_decompressed(comp, comp_len, raw, raw_len, ret, flags, phase). `b` must outlive the handler.
Handler make_kit_handler(KitBinding* b);

struct AttribBinding {
    attrib::Runtime* rt = nullptr;
    std::atomic<uint64_t>* errors = nullptr;  // the host's "detour errors" counter (attrib_ebx_log.txt); may be null
};
// on_call = Runtime::count_call(comp_len, raw_len); on_block = Runtime::on_decompressed(comp, comp_len, raw, raw_len, ret).
// No `live`: the Runtime's own kill switch (attrib_ebx_off.txt) is read inside on_decompressed.
Handler make_attrib_handler(AttribBinding* b);

}  // namespace oodle
}  // namespace turbo
