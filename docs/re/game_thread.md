# Game-code hooks and the game-thread dispatcher (Track B, Turbo 0.4 work)

Scope: offline Manager Career on the user's own PC. Only the game's loop / update code was looked at; nothing
protection- or online-related. Live Editor's own files are not touched.

## 1. What Turbo.dll now does (src/win/game_hooks.cpp, src/core/sigscan.cpp)

1. `install_game_hooks()` (called from `start_overlay` after the input shield) finds FC27.exe (the process's main
   module), reads its PE header and builds the **build key** `build_key(TimeDateStamp, SizeOfImage)`, e.g.
   `6AB9813C-211EF000` for the game as dumped on 2026-10-03. It lists the executable sections (`.text1`, `.xpdata`,
   `.debug` on that build; the protected `.ecode/.xcode/.xtext` sections are not executable in the header and are not
   scanned).
2. It loads the signature table `turbo\signatures_<build>.json` (next to Turbo.dll) if present and for the right
   build, else the built-in table for that build (`kBuiltin` in `sigscan.cpp`). A build in no table switches every
   game hook off (and writes a template `signatures_<build>.json` so a title update can be followed up) instead of
   scanning blind.
3. Every signature is scanned once over the executable sections (copied through `ReadProcessMemory`, 64 KB at a time,
   so an unmapped page never faults). A match must be **unique**; `resolve: "rip"` then decodes the rip-relative
   operand (`E8/E9 rel32`, `0F 8x`, `FF 15/25 [rip]`, `[REX] 8B/8D/89/39/3B/63/85/C7/80/83/81 [rip+disp32]`).
   Each result (`found` / `missing` / `ambiguous` / `skipped` / `bad pattern`) is logged to `turbo_gui.log` and shown
   in the Status tab under "Game hooks".
4. Hooks are installed one at a time with MinHook (`MH_CreateHook` + `MH_EnableHook(target)`, never `MH_ALL_HOOKS`;
   `MH_Initialize` was done by `start_overlay`) under `guard_hold("installing a game-code hook")`.

### Signature table format

```json
{"build": "6AB9813C-211EF000", "game": "FC27.exe",
 "signatures": {
   "game_tick": {"pattern": "48 89 5C 24 ?? 57 48 83 EC 20", "resolve": "none", "offset": 0, "note": "..."},
   "some_call":  {"pattern": "E8 ?? ?? ?? ?? 48 8B D8 EB", "resolve": "rip", "offset": 0}
 }}
```

`pattern`: hex bytes, `??` = any byte (same syntax as the dev service `find`). `offset` (bytes, may be negative) is
added to the match before resolving. `resolve: "none"` = the address is match + offset; `"rip"` = the instruction at
match + offset is decoded and its rip-relative target is the address. An empty pattern is a placeholder (`skipped`).

### Kill switches

| Switch | Effect |
| --- | --- |
| `turbo_output\game_hooks_off.txt` | nothing is installed; existing detours only call the original (re-read every 2 s) |
| `turbo_output\hook_<name>_off.txt` | that hook is not installed / its detour only calls the original (re-read every 2 s) |
| env `TURBO_GUI_NO_GAME_HOOKS=1` | nothing is installed |
| unknown build key | nothing is installed (logged, Status tab) |

### API (src/win/game_hooks.h, namespace `host`)

```cpp
void        install_game_hooks();                                   // start_overlay calls it; never throws
bool        game_hooks_allowed();                                    // build known, global switch off
std::string game_build_key();                                        // "6AB9813C-211EF000", "" before install
uint64_t    game_signature(const char* name);                        // resolved address, 0 unless Found
bool        install_game_hook(const char* name, const char* signature, void* detour, void** original);
bool        install_game_hook_at(const char* name, void* target, void* detour, void** original);  // target must be in an executable section of FC27.exe
bool        game_hook_enabled(const char* name);                     // per-hook + global switch (cached 2 s)
void        game_hook_error(const char* name, const char* what);     // counted; HOOK_BODY calls it
void        game_hook_called(const char* name);
const char* run_on_game_thread(std::function<void()> fn);           // "hook" | "lua" | "" (queued, at most 256)
bool        game_thread_hooked();                                    // game_tick hook active
uint32_t    game_thread_id();                                        // thread the queue last ran on
turbo::HookReport game_hooks_report();                               // Status tab snapshot
#define HOOK_BODY(name, ...)   /* try { ... } catch (std::exception&) / catch (...) -> game_hook_error */
extern "C" __declspec(dllexport) int turbo_game_pump(void* lua_state);  // Lua-callable: drains the queue on the caller's thread
```

Platform-independent parts (`src/core/sigscan.h`, namespace `turbo`, compiled into the native tests):

```cpp
std::string build_key(uint32_t timestamp, uint32_t size_of_image);
bool parse_signature_table(const std::string& json, SignatureTable& out, std::string& err);
std::string signature_table_json(const SignatureTable& t);
const SignatureTable* builtin_signature_table(const std::string& build);
std::vector<std::string> builtin_builds();
std::vector<uint64_t> scan_pattern(const uint8_t* buf, size_t len, uint64_t base, const std::vector<uint8_t>& bytes, const std::vector<bool>& mask, size_t max_hits);
bool resolve_rip(const uint8_t* code, size_t len, uint64_t addr, uint64_t& target, std::string& err);
SigResult resolve_signature(const Signature& s, const uint8_t* buf, size_t len, uint64_t base);
```

### Writing a detour

```cpp
using Fn = void* (*)(void*, void*);
static Fn o_thing = nullptr;
static void* hk_thing(void* a, void* b) {
    if (!host::game_hook_enabled("thing")) return o_thing(a, b);   // kill switch: pass through
    host::game_hook_called("thing");
    HOOK_BODY("thing", { /* Turbo's work; may throw, never reaches the game */ });
    return o_thing(a, b);                                           // ALWAYS call the original
}
// at install time (after start_overlay):
host::install_game_hook("thing", "thing_signature", reinterpret_cast<void*>(&hk_thing), reinterpret_cast<void**>(&o_thing));
```

Rules: the detour's prototype must match the target's integer arguments (the first four go in rcx/rdx/r8/r9; a target
with floating-point arguments needs a prototype with the matching `double`/`float` parameters so xmm0-3 are forwarded;
more than four arguments need the exact prototype so the stack arguments line up). Do no work before the call to the
original unless the arguments are fully known. Access violations are not C++ exceptions: `HOOK_BODY` cannot catch
them, so a detour must only touch memory it has validated (use `ProcessMemory::read`, which fails instead of faulting).

## 2. The game-thread dispatcher

`run_on_game_thread(fn)` queues `fn` (at most 256 jobs; the oldest is dropped). It runs:

* **promptly**, inside the `game_tick` hook, when that signature is Found on the running build (see section 3: it is
  not yet, so this path is dormant); the generic detour `hk_tick` forwards the first four integer arguments and the
  integer return value and drains the queue *after* the original returned;
* otherwise **on the next career-mode event**: Turbo's Lua bridge (`bridge.on_career_event` -> `M.pump_native`) calls
  the exported `turbo_game_pump()` through `package.loadlib(turbo\Turbo.dll, "turbo_game_pump")`. Live Editor runs
  Lua inside its hook of the game's career-event post, i.e. on the thread that posts career events, so the queue runs
  on the game's own career thread. The function is looked up once (only while the GUI mailbox is live) and retried
  every 60 events if an older Turbo.dll has no such export. The thread id is recorded (`game_thread_id()`, Status tab)
  for the RE tracks.

`run_on_game_thread` returns which path will run the job (`"hook"`, `"lua"`, or `""` when neither has shown up yet,
e.g. outside a career before any event), so a feature can tell the user "runs on the next career event".

### Overlay Lua mailbox commands: why they are not prompter yet

Mailbox commands are executed by Lua, and Lua lives inside FCLiveEditor.DLL's Lua state. Turbo.dll has no handle to
that state (Live Editor is closed, and calling into another module's `lua_State` from a thread it does not expect
would corrupt it), so the dispatcher cannot *call* Lua. The only entry into Lua is Live Editor's own career-event hook.
What the dispatcher can do, once `game_tick` is hooked, is call the game's career-event post function with a harmless
event from the game thread, which would make Live Editor run Turbo's Lua handler (and thus the mailbox poll) at once.
That needs the event-post function's signature and a known harmless event id: an item for the RE tracks (Live Editor
calls it `post__CareerModeEvent`; the FCE `EventHandlerInterface` strings `HubDino::FCEIIEventHandlerInterface` are a
starting point). Until then `lua\scripts\turbo_exec.lua` stays the manual trigger outside a career.

## 3. Finding the per-frame game-thread function (not finished)

Material: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (main module image, base `0x140000000`,
TimeDateStamp `0x6AB9813C`, SizeOfImage `0x211EF000`); python `re_venv` with capstone/pefile/numpy. Helpers used:
`find_str`, `rip_refs` (numpy scan of every rip-relative disp32 in `.text1`), capstone `dis`, `count_pattern`.

Sections (from the header): `.text1` RVA `0x1000`..`0x8B69000` (code), `.xpdata` `0x8B69000`, `.debug` `0xF05D000`
executable; `.reloc`, `.trace`, `.link`, `.ecode`, `.xcode`, `.udata`, `.data1`, `.00cfg`, `.xtext`, `.rdata`, `.rodata`
data. The import directory is not in the dumped headers (the binary is protected), so the Windows message pump could
not be found through the IAT.

What was traced (all addresses are for this one build; confidence as marked):

| Item | Address | Evidence |
| --- | --- | --- |
| Strings `GameLoop`, `GameLoopImpl`, `PauseGameLoopImpl` | `0x14AE7ADC8`, `0x14AE7B050`, `0x14AE7AFF0` | exact NUL-terminated strings |
| Factory that allocates `GameLoopImpl` (0xBA0 bytes) and `PauseGameLoopImpl` (0x248) | `0x146A1E130` (call to the named allocator `0x1420BBFB8` at `0x146A1E446` / `0x146A1E487`) | rip refs to the strings [H] |
| `GameLoopImpl` constructor | `0x146A1E18C` | called right after the allocation with rcx = the object [H] |
| `GameLoopImpl` vtable | `0x14AE7BE98` (first lea in the ctor) | [H]; slots 1-8 are thunks that jump through another vtable at `+0x50..+0x88` of the object's interface, slot 0 is the scalar deleting destructor, slot 9 `0x146A22AA8`; the update slot was not identified |
| `Initializing MainLoop...` logger / init function | `0x1459DEB10` (log call at `0x1459DECCF`, then `call 0x142B09114`, `call 0x142B16348`, a long chain of virtual init calls `[rax+0x128]`, `[rax+0xA0]`, ...) | [H] it is initialisation, not the loop body |
| `Initializing GameLoop...` | `0x1459DFD40` (log at `0x1459DFEA6`), then iterates a list calling slot `+0x98` of each element | [H] init |
| `0x1412DABDC` creates **named sync events**: "TickUpdate", "FrameUpdate", "TimeRegulatorSyncEvent", "TimeRegulatorrenderFinishEvent", "RenderAsyncResourceManager" | call sites `0x145C41AA4`, `0x145C41AB9`, `0x145C471F0`, `0x145C472AA`, `0x14561754x` | [H] the tick is driven by a threaded scheduler ("TickUpdate"/"FrameUpdate" events); the object constructed at `0x145C41A30` (vtable `0x14AD5BD7D`-relative lea, hash `0x7C89E682`) owns both events and a 0x14C8+ byte state: the thread function that waits on "TickUpdate" is the next thing to find |
| Job scheduler thread name `JobScheduler_MainLoop` | string `0x14A7EA1B8`, ref `0x1473661CC` | worker threads, not the game thread |

Conclusion: no per-frame function was pinned down with enough confidence to ship a pattern in the time box, so the
built-in table carries `game_tick` as a placeholder (`skipped`) and the dispatcher uses the Lua pump. A wrong
per-frame hook would run on every frame of the user's game, so the bar is: (a) proven to run on the thread Live
Editor's Lua runs on (compare with `game_thread_id()` from the pump), (b) at most four integer arguments, integer or
void return, (c) a unique pattern checked with `count_pattern` over `.text1`.

Suggested next steps for the RE track:

1. Run the game with Turbo, enter a career, read `game_thread_id` from the Status tab (set by the Lua pump), then use
   the dev service to sample that thread's stack (new op: `SuspendThread` + `GetThreadContext` + `RtlVirtualUnwind`,
   or simply read `rip`/`rsp` a few hundred times) and cluster the return addresses: the outermost `.text1` frames
   that recur every sample are the loop and its per-frame callees.
2. Statically: follow who waits on the "TickUpdate" event created at `0x145C41AB9` (the object's field at
   `+0xA8`); the waiting function is the tick thread's loop, and the function it calls after the wait is the per-frame
   update. Check it is the same thread as (1).
3. Add the pattern as `game_tick` in `turbo\signatures_6AB9813C-211EF000.json` (the file overrides the built-in table
   without a rebuild) and confirm in the Status tab: "Game-thread dispatcher: prompt (game_tick hook)", ticks rising.
