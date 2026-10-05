// FC 27 LE Turbo GUI - the in-memory fallback for the game editors' unlock (research/edit_unlock_plan.md section 5).
//
// A guarded post-hook on the config loader 0x1470F0D50 (void* Load(this, Config* out, const eastl::string* path)): after
// the original returned, when the path is one of the unlocked contexts (core/edit_unlock_rules.h) and the user left
// "Game editors" on, the parsed config is walked and isEditable / isVisible set to 1 with the same keep-list as the data
// override. The detour body is on_config_loaded(): platform-independent so the tests drive it on fake parsed-config
// memory; win/edit_unlock_hook_win.cpp owns the MinHook detour, the cached kill switch
// (turbo_output\edit_unlock_hook_off.txt) and the memory check.
//
// The hook CANNOT add what the file lacks (the Attributes and Brand animations sections of career Edit Player, the
// gear of the experimental switch): that needs the data override.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "edit_unlock_rules.h"

namespace turbo {
namespace edit_unlock {

// Counters the detour updates (atomics only) and the Status tab reads
struct HookCounters {
    std::atomic<uint64_t> loads{0};        // unlocked-context configs the loader returned while the hook was installed
    std::atomic<uint64_t> configs{0};      // of those, patched
    std::atomic<uint64_t> fields{0};       // flag bytes changed 0 -> 1
    std::atomic<uint64_t> kept{0};         // items left alone by the keep-list
    std::atomic<uint64_t> skipped_off{0};  // left alone: switched off (settings, per-file toggle, kill switch)
    std::atomic<uint64_t> refused{0};      // left alone: the walk refused the memory (layout guard)
    std::atomic<int> last_context{-1};
    std::atomic<const char*> last_error{""};  // static text
};

// What the detour knows about the switches (copied from atomics; no allocation)
struct Gate {
    bool live = false;         // installed, enabled in MinHook, the per-hook and global kill switches absent
    bool kill_switch = false;  // turbo_output\edit_unlock_hook_off.txt present (cached)
    Settings settings;
};

// The post-hook body: `out` is the config the original just filled, `path` / `path_len` the copy of the path argument
// taken before the call. Unknown paths are left alone without touching `out`. Returns the context index that was
// patched, -1 when nothing was written.
int on_config_loaded(uint8_t* out, const char* path, size_t path_len, const Gate& gate, const KeepIds& ids,
                     ReadableFn readable, HookCounters& counters);

// The Status tab lines. off_reason empty = installed; the first line is
// "Game editors hook: on | configs patched N | fields unlocked M".
struct StatusInput {
    std::string off_reason;  // "" = installed; else why not ("game build", "hooks off", "kill switch ...")
    bool kill_switch = false;
    Settings settings;
};
std::vector<std::string> status_lines(const StatusInput& in, const HookCounters& c);

// Shown in the GUI next to the switches
constexpr const char* kCannotAddNote =
    "The in-memory fallback only turns on fields the game's file already lists. It cannot add the missing sections "
    "(Attributes and Brand animations in career Edit Player, the experimental gear): those come from the file "
    "override. TEAM and GENDER always stay locked.";

// What the GUI talks to (win/edit_unlock_hook_win.cpp; nullptr in tests and when the host has no game hooks)
class HookService {
public:
    virtual ~HookService() = default;
    virtual void configure(const Settings& s) = 0;               // the GUI's switches (atomics; cheap)
    virtual std::vector<std::string> status() const = 0;         // status_lines()
    virtual bool installed() const = 0;
};

}  // namespace edit_unlock
}  // namespace turbo
