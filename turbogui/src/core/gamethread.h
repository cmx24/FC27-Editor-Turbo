// FC 27 LE Turbo GUI - game-thread dispatcher pieces that need no Windows (compiled into the native tests).
//
// src/win/game_hooks.cpp builds the real dispatcher on top of these:
//   * JobQueue: the bounded queue of work for the game thread, drained in bounded batches from the game_tick hook (the
//     game's per-frame MainLoop frame body, docs/re/game_thread.md) and whole from Turbo's Lua pump;
//   * detect_inline_hook: tells whether a function's first bytes were replaced by another module's detour (Live
//     Editor hooks the career-event post function; Turbo only uses that entry while that hook is in place);
//   * the synthetic career event: Turbo-owned dispatcher/event objects handed to the game's PostEvent entry so that
//     Live Editor's hook runs its Lua handlers at once. PostEvent only makes virtual calls on the two objects it is
//     given, every slot of which is Turbo's no-op, so nothing of the game is touched (section 4 of the notes).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace turbo {

// ---------------------------------------------------------------- job queue
class JobQueue {
public:
    explicit JobQueue(size_t capacity = 256) : cap_(capacity) {}

    // Appends a job; when the queue is full the oldest job is dropped (returns true when one was dropped)
    bool push(std::function<void()> fn);
    size_t size() const;
    size_t capacity() const { return cap_; }

    // Runs jobs in order: at most max_jobs of them (0 = all that are queued when the call starts). A job that throws
    // is counted as failed and reported through on_error (may be null); the rest still run. Returns jobs run.
    size_t drain(size_t max_jobs, const std::function<void(const char* what)>& on_error = nullptr);

    long long ran() const { return ran_.load(); }
    long long failed() const { return failed_.load(); }
    long long dropped() const { return dropped_.load(); }

private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
    size_t cap_;
    std::atomic<long long> ran_{0}, failed_{0}, dropped_{0};
};

// ---------------------------------------------------------------- inline hook detection
enum class InlineHook { None, JmpRel32, JmpIndirect, MovabsJmp, PushRet, Truncated };
const char* inline_hook_name(InlineHook h);

// Looks at the first bytes of a function (code[0..len) living at `addr`). JmpRel32 / MovabsJmp / PushRet set `target`
// to the detour; JmpIndirect sets it to the pointer slot the jmp reads (the caller reads the real target from it).
InlineHook detect_inline_hook(const uint8_t* code, size_t len, uint64_t addr, uint64_t* target);

// ---------------------------------------------------------------- synthetic career event
// Event id of Turbo's synthetic career-mode event. Outside every id the game or Live Editor's enum uses; Turbo's Lua
// side (core/events.lua SYNTHETIC_ID) treats it as "poll the GUI mailbox, nothing else".
constexpr int32_t kSyntheticCareerEvent = 0x7E7E0001;
constexpr size_t kSyntheticBlock = 4096;    // bytes per object: far more than any event or dispatcher the game has
constexpr size_t kSyntheticVtableSlots = 64;
constexpr size_t kSyntheticTypeOffset = 0x10;  // where the game's career events keep their type id

// The game's PostEvent(dispatcher_wrapper, type, event) does: event->vf[1](), event->vf[4](),
// (*dispatcher_wrapper)->vf[6](type, event, 0), event->vf[2](). With these objects every one of those lands in
// `noop`. Every other qword of an object holds the object's own address, so anything that walks pointer fields (Live
// Editor's hook passes the pointers to Lua) stays inside readable, Turbo-owned memory.
struct SyntheticEvent {
    alignas(16) uint8_t wrapper[kSyntheticBlock];     // +0 -> dispatcher
    alignas(16) uint8_t dispatcher[kSyntheticBlock];  // +0 -> vtable
    alignas(16) uint8_t event[kSyntheticBlock];       // +0 -> vtable, +8 = 0 (ref count), +0x10 = type
    void* vtable[kSyntheticVtableSlots];
};
void build_synthetic_event(SyntheticEvent& s, void* noop, int32_t type);
// True when `s` is laid out as build_synthetic_event leaves it (checked again right before every use)
bool verify_synthetic_event(const SyntheticEvent& s, void* noop, int32_t type);

}  // namespace turbo
