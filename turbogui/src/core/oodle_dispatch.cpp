// FC 27 LE Turbo GUI - the shared Oodle decompress dispatcher (see oodle_dispatch.h)
#include "core/oodle_dispatch.h"

#include "core/attrib_ebx.h"
#include "core/kit_swap.h"

namespace turbo {
namespace oodle {

const char* slot_name(int slot) {
    switch (slot) {
        case kSlotKits: return "3D kit swap";
        case kSlotAttrib: return "attrib EBX patch";
        default: return "?";
    }
}

bool Dispatcher::add(int slot, const Handler& h) {
    if (slot < 0 || slot >= kSlots || !h.on_block) return false;
    Entry& e = entry_[slot];
    if (e.used.exchange(true)) return false;  // once: the storage is read by detours for the life of the process
    e.h = h;
    live_[slot].store(&e, std::memory_order_release);
    return true;
}

void Dispatcher::retire(int slot) {
    if (slot < 0 || slot >= kSlots) return;
    Entry& e = entry_[slot];
    if (!e.used.load()) {
        e.used.store(true);  // a slot that never registered cannot register later either
    }
    e.retired.store(true);
    live_[slot].store(nullptr, std::memory_order_release);
}

bool Dispatcher::has(int slot) const { return slot >= 0 && slot < kSlots && live_[slot].load(std::memory_order_acquire) != nullptr; }

int Dispatcher::registered() const {
    int n = 0;
    for (int i = 0; i < kSlots; ++i) n += has(i) ? 1 : 0;
    return n;
}

SlotStats Dispatcher::stats(int slot) const {
    SlotStats s;
    if (slot < 0 || slot >= kSlots) return s;
    const Entry& e = entry_[slot];
    s.seen = e.seen.load();
    s.overwritten = e.overwritten.load();
    s.skipped = e.skipped.load();
    s.errors = e.errors.load();
    s.registered = has(slot);
    s.retired = e.retired.load();
    return s;
}

int Dispatcher::dispatch(const Call& c) noexcept {
    calls_.fetch_add(1, std::memory_order_relaxed);
    int result = 0;
    bool overwritten = false;
    for (int s = 0; s < kSlots; ++s) {
        Entry* e = live_[s].load(std::memory_order_acquire);
        if (!e) continue;
        const Handler& h = e->h;
        try {
            if (h.on_call) h.on_call(h.user, c);
            if (overwritten) {
                e->skipped.fetch_add(1, std::memory_order_relaxed);  // no double patching
                continue;
            }
            if (h.live && !h.live(h.user)) continue;
            const int r = h.on_block(h.user, c);
            if (r == 1) e->seen.fetch_add(1, std::memory_order_relaxed);
            if (r == 2) {
                e->overwritten.fetch_add(1, std::memory_order_relaxed);
                overwritten = true;
            }
            if (r > result) result = r;
        } catch (const std::exception& ex) {
            e->errors.fetch_add(1, std::memory_order_relaxed);
            if (h.on_error) {
                try {
                    h.on_error(h.user, ex.what());
                } catch (...) {
                }
            }
        } catch (...) {
            e->errors.fetch_add(1, std::memory_order_relaxed);
            if (h.on_error) {
                try {
                    h.on_error(h.user, "unknown exception");
                } catch (...) {
                }
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------- adaptors
namespace {

uint64_t pos(int64_t v) { return v > 0 ? uint64_t(v) : 0; }

void kit_call(void* user, const Call&) { static_cast<KitBinding*>(user)->rt->count_call(); }
bool kit_live(void* user) {
    const KitBinding* b = static_cast<KitBinding*>(user);
    return !b->live || b->live();
}
int kit_block(void* user, const Call& c) {
    return static_cast<KitBinding*>(user)->rt->on_decompressed(c.comp, pos(c.comp_len), c.raw, pos(c.raw_len), c.ret, c.flags, c.phase);
}
void kit_error(void* user, const char* what) {
    const KitBinding* b = static_cast<KitBinding*>(user);
    if (b->error) b->error(what);
}

void attrib_call(void* user, const Call& c) { static_cast<AttribBinding*>(user)->rt->count_call(pos(c.comp_len), pos(c.raw_len)); }
int attrib_block(void* user, const Call& c) {
    return static_cast<AttribBinding*>(user)->rt->on_decompressed(c.comp, pos(c.comp_len), c.raw, pos(c.raw_len), c.ret);
}
void attrib_error(void* user, const char*) {
    const AttribBinding* b = static_cast<AttribBinding*>(user);
    if (b->errors) b->errors->fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

Handler make_kit_handler(KitBinding* b) {
    Handler h;
    h.name = "kit_oodle";
    h.user = b;
    h.on_call = &kit_call;
    h.live = &kit_live;
    h.on_block = &kit_block;
    h.on_error = &kit_error;
    return h;
}

Handler make_attrib_handler(AttribBinding* b) {
    Handler h;
    h.name = "attrib_ebx";
    h.user = b;
    h.on_call = &attrib_call;
    h.on_block = &attrib_block;
    h.on_error = &attrib_error;
    return h;
}

}  // namespace oodle
}  // namespace turbo
