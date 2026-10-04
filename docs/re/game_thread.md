# Game-code hooks and the game-thread dispatcher (Track B foundation, tick hook + prompt Lua commands)

Scope: offline Manager Career on the user's own PC. Only the game's loop / update code and its career-event post entry
were looked at; nothing protection- or online-related. Live Editor's own files are not touched, read or decompiled: the
only thing Turbo relies on is the documented fact that Live Editor runs `post__CareerModeEvent` Lua handlers from a
hook on the game's career-event post function, and that fact is verified at run time (section 4).

## 1. What Turbo.dll does (src/win/game_hooks.cpp, src/core/sigscan.cpp, src/core/gamethread.cpp)

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
   "game_tick":         {"pattern": "48 89 5C 24 08 48 89 6C 24 10 ...", "resolve": "none", "offset": 0, "note": "..."},
   "post_career_event": {"pattern": "4C 8B C0 48 8B CF E8 ?? ?? ?? ?? 48 8D 8C 24 90 00 00 00 E8 ?? ?? ?? ??", "resolve": "rip", "offset": 6}
 }}
```

`pattern`: hex bytes, `??` = any byte (same syntax as the dev service `find`). `offset` (bytes, may be negative) is
added to the match before resolving. `resolve: "none"` = the address is match + offset; `"rip"` = the instruction at
match + offset is decoded and its rip-relative target is the address. An empty pattern is a placeholder (`skipped`).
The shipped entries for build `6AB9813C-211EF000` are also in `docs/re/game_thread-signatures.json`;
`bash scripts/re/py.sh scripts/re/verify_game_thread.py` checks them against the image (uniqueness + resolution).

### Kill switches

| Switch | Effect |
| --- | --- |
| `turbo_output\game_hooks_off.txt` | nothing is installed; existing detours only call the original (re-read every 2 s) |
| `turbo_output\hook_<name>_off.txt` | that hook is not installed / its detour only calls the original (re-read every 2 s); `hook_game_tick_off.txt` also stops the prompt Lua trigger |
| `turbo_output\lua_trigger_off.txt` | the synthetic career event is never sent (Lua commands wait for a real event); re-read every 2 s |
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
void        want_lua_pump(bool wanted);                              // the GUI has a mailbox command waiting (overlay calls it every frame)
turbo::HookReport game_hooks_report();                               // Status tab snapshot
#define HOOK_BODY(name, ...)   /* try { ... } catch (std::exception&) / catch (...) -> game_hook_error */
extern "C" __declspec(dllexport) int turbo_game_pump(void* lua_state);  // Lua-callable: drains the queue on the caller's thread
```

Platform-independent parts, compiled into the native tests: `src/core/sigscan.h` (namespace `turbo`: `build_key`,
`parse_signature_table`, `signature_table_json`, `builtin_signature_table`, `scan_pattern`, `resolve_rip`,
`resolve_signature`) and `src/core/gamethread.h`:

```cpp
class JobQueue { bool push(fn); size_t drain(max_jobs, on_error); ran(); failed(); dropped(); };  // bounded, oldest dropped
InlineHook detect_inline_hook(const uint8_t* code, size_t len, uint64_t addr, uint64_t* target); // jmp rel32 / jmp [rip] / movabs+jmp / push+ret
constexpr int32_t kSyntheticCareerEvent = 0x7E7E0001;
struct SyntheticEvent { wrapper[4096]; dispatcher[4096]; event[4096]; void* vtable[64]; };
void build_synthetic_event(SyntheticEvent&, void* noop, int32_t type);  bool verify_synthetic_event(...);
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

`run_on_game_thread(fn)` queues `fn` (`turbo::JobQueue`, at most 256 jobs; the oldest is dropped and counted). It runs:

* **promptly**, inside the `game_tick` hook on the game's per-frame MainLoop frame body (section 3): the generic detour
  `hk_tick` forwards the first four integer arguments and the integer return value, and *after* the original returned
  drains **at most 32 jobs per frame** (`kJobsPerTick`; the rest wait for the next frame, so a burst can never stall a
  frame). Every job runs under try/catch; failures are counted (`failed` in the Status tab) and logged (first 20);
* otherwise, or additionally, **on every career-mode event**: Turbo's Lua bridge (`bridge.on_career_event` ->
  `M.pump_native`) calls the exported `turbo_game_pump()` through `package.loadlib(turbo\Turbo.dll, "turbo_game_pump")`,
  which drains the whole queue on the thread that posts career events (where Live Editor runs its Lua). The function is
  looked up once (only while the GUI mailbox is live) and retried every 60 events if an older Turbo.dll has no such
  export. The pump never nests: a pump that happens inside a queued job (a job that posts a real game event, say)
  returns at once.

`run_on_game_thread` returns which path will run the job (`"hook"`, `"lua"`, or `""` when neither has shown up yet),
so a feature can tell the user "runs on the next career event". The Status tab shows ticks, pumps, jobs run / failed /
dropped, the queue length and three thread ids: the tick's, the Lua pump's and the last drain's. The tick thread and
the pump thread are expected to be the same thread (section 3); a mismatch is logged once as a WARNING and shown in
the Status tab, and in that case the prompt Lua trigger (section 4) stays off while native jobs still run on the tick.

## 3. The per-frame game-thread function (found: the MainLoop frame body)

Material: `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (main module image, base `0x140000000`,
TimeDateStamp `0x6AB9813C`, SizeOfImage `0x211EF000`); python `re_venv` with capstone/pefile/numpy; helpers in
`scripts/re` (`rx.py`, `rx_jobs.py`, `rx_capture.py` + `ripscan.exe`, `disfn.py`, `callers.py`). All addresses are for
this one build; confidence as marked ([H] high: direct disassembly evidence, [M] medium: inferred).

The chain, from the thread to the frame:

| Item | Address | Evidence |
| --- | --- | --- |
| String `"MainLoop"` used as a thread name | `0x14A6BAC78` | ref at `0x1459E1701` [H] |
| `MainLoop` thread creation | `0x1459E16CC` | allocates a 0xA0-byte thread object (`0x140670D24`), calls the thread-create helper `0x140C0750C(obj, "MainLoop", 0, entry=0x141E80D54, ...)`, then starts it (`0x140C07DB0(thread, 3)`). Called from `0x1459E0F30` after `0x1459E0BD8` decided a threaded loop is used [H] |
| `MainLoop` thread entry | `0x141E80D54` | `0x141E80ED8` creates the MainLoop object (0x960 bytes, ctor `0x1459DD130`, stored in the global `0x14D7AE518`) and calls its vtable slot 3 (`0x1459DEC98`, "Initializing MainLoop..."); then the loop at `0x141E80DBA`: `rcx = [0x14D7AE518]; call [vtable+0x20]` (slot 4) and repeat while it returns true [H] |
| `MainLoop` vtable | `0x14AD1D9D0` | first `lea` in the ctor; slot 4 = `0x141DF6360`, slot 49 = `0x1459E09D0` (`xor al,al; ret`), slot 11 = `0x1459E20AC` (quit path) [H] |
| `MainLoop::Update` (slot 4) | `0x141DF6360` | reads the clock (`0x140558C60`), keeps `dt` = now - `[this+0x8C8]`, asks slot 49 whether to run the frame inline: it returns **false** on this build, so the frame body is wrapped into a job named `"mainJob"` (`0x1459DC5D0`, job type hash `0x3693C69D` via `0x140B0BFF0`) and waited for (`0x1414A6838`); then the frame regulator (`[this+0x58]` slots 3/4, or a sleep when `[this+0x38]` is 0), and returns true unless the quit flag `0x14C25BE58` is set [H] |
| **`MainLoop` frame body** (the hook target `game_tick`) | **`0x1459E2E7C`** | `void(MainLoop* this, int64* dt)`; called from the inline path (`0x141DF63CC`) and from the `"mainJob"` callback `0x1459DC740` (`0x1459DC74B`), nowhere else. Body: `[this+0x60]->+0x10` object bracketed by two imported calls (a profiler/frame scope), `0x141C99934`, then `this->vf[42]()` (`[rax+0x150]`), `0x141E300F8`, `this->vf[41]()` (`[rax+0x148]`), `0x142121EC4`, then every registered updatable in the list `[this+0x88..0x90]` gets `vf[36](float dt_seconds)`, then the quit check through `[0x14BDA4260]`, and `[this+0x8D8]++` (the frame counter) [H] |

Why this function: it runs **once per frame** (the frame counter at `+0x8D8` increments at its end), it is the top of
the game's own update (the game systems' updates are its callees), it has a plain prologue
(`mov [rsp+8],rbx; mov [rsp+0x10],rbp; mov [rsp+0x18],rsi; push rdi; sub rsp,0x20`, relocatable by MinHook), two integer
arguments and no meaningful return value, and no reentrancy (the loop waits for the job before scheduling the next).
Turbo's work happens *after* it returned, i.e. between two frames, when the game's state is consistent.

Which thread: the frame body runs on the thread that executes the `"mainJob"` job, which is the game's main update
thread for the whole session (the job scheduler has a dedicated `JobScheduler_MainLoop` thread, string `0x14A7EA1B8`,
ref `0x1473661CC` [M]). The career-mode managers (the FCE `HubDino::FCEGameModesFCECareerMode*` objects) post their
events (`PostEvent` below) from inside this update, so this is also the thread Live Editor's `post__CareerModeEvent`
Lua handlers run on: Turbo's Lua pump recorded thread 31512 / 41428 in the two sessions logged on 2026-10-03, and the
tick records its own thread id; the Status tab shows both. The expectation `tick thread == pump thread` is **checked at
run time** and the Lua trigger (section 4) depends on it; native jobs run on the tick regardless, because the frame
body's thread is the game's update thread by construction.

Signature `game_tick` (unique over `.text1`/`.xpdata`/`.debug`, checked by `scripts/re/verify_game_thread.py` and by the
native tests against the dumped bytes):

```
48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 79 60 48 8B F1 48 8B EA 48 8B 5F 10 48 8B CB FF 15 ?? ?? ?? ?? 48 8B 4F 10 E8
```

Not chosen: `MainLoop::Update` (`0x141DF6360`) runs on the "MainLoop" thread, which only schedules the frame job and
waits (its prologue is also shared by four other functions); the `GameLoopImpl` vtable slot 10 (`0x146A26F78`,
"MdfPrep" / "UserManagerUpdate" / "FrameStartJob" / "ScheduleGameplayJobGraph") is the gameplay (match) loop, not the
frontend; the "TickUpdate"/"FrameUpdate" sync events of the earlier notes belong to the time regulator and were not
needed.

## 4. Prompt Lua commands: the synthetic career event

### The problem

The overlay's mailbox commands (export / import / clone / create player / boot / refresh) are executed by Turbo's Lua
side, which lives in Live Editor's Lua state. Turbo.dll never calls into that state: it has no handle to it, the Lua C
API of another module is undocumented, and calling a Lua state from a thread it does not expect corrupts it. Live
Editor's Lua runs only (a) when the launcher runs a script and (b) inside Live Editor's hook of the game's career-event
post function, for `pre__`/`post__CareerModeEvent` handlers. Its Lua API has no timer or per-frame hook (checked in
`lua\DOC.MD` and `lua\libs`). So until now a command waited for the next real career event.

### Options weighed

| Option | Verdict |
| --- | --- |
| Call Live Editor's Lua state from the tick (lua_State* is handed to `turbo_game_pump`) | rejected: undocumented LE internals, races with the launcher's script runs |
| Re-implement the commands natively in Turbo.dll (native `InsertDBTableRow`, export, clone...) | robust in the long run but a rewrite of every Lua feature; not done here, noted as follow-up |
| Post a real game event with an unused id through the game's dispatcher | rejected: whatever the dispatcher does with an unknown id (bookkeeping, persistent events) is game state |
| **Hand the game's `PostEvent` entry Turbo's own dispatcher/event objects** | chosen: the game is never reached, only Live Editor's hook runs; harmless with or without that hook |

### How it works

`PostEvent(dispatcher_wrapper, int type, Event* ev)` at `0x14060124C` [H] is a 20-instruction shell (it has 157 direct
callers in the career code, e.g. `DataController::InsertTeamPlayer` `0x147B90074`, `JobMarketManager::MakeOffer`):

```
rax = [ev];              call [rax+8]      ; ev->vf[1]()               (prepare / add-ref)
rax = [ev];              call [rax+0x20]   ; ev->vf[4]()
rcx = [wrapper]; rax = [rcx]; r9 = 0; call [rax+0x30]  ; dispatcher->vf[6](type, ev, 0)   (the dispatch)
rax = [ev];              jmp  [rax+0x10]   ; ev->vf[2]()               (release; its result is returned)
```

Everything it does is a virtual call on the two objects it is given. Turbo builds (`turbo::build_synthetic_event`) three
4 KB objects and a 64-slot vtable whose every slot is `synthetic_noop` (returns its `this`): `wrapper -> dispatcher`,
`dispatcher -> vtable`, `event -> vtable`, `event+8 = 0` (reference count), `event+0x10 = 0x7E7E0001` (the type id, where
the game's career events keep theirs), and every other qword of each object holds the object's own address, so any
code that follows pointer fields of these objects (Live Editor passes `events_manager`, `event_id`, `event` to Lua)
stays inside Turbo-owned, readable memory. Calling `PostEvent(wrapper, 0x7E7E0001, event)` therefore touches nothing of
the game: four no-op calls. What makes it useful is Live Editor's inline hook on `PostEvent`: its detour runs the
`post__CareerModeEvent` Lua handlers, among them Turbo's dispatcher (`core/events.lua`), whose bridge tap sees
`SYNTHETIC_ID` and only polls the GUI mailbox (`bridge.lua on_synthetic_event`: `poll_mailbox(true)`, `pump_native`,
the picture pump; no state files, no reload logic). Id-keyed listeners (every Turbo feature) never match the id;
Live Editor's bundled sample handlers compare `event_id` with enum values and do nothing.

Gates, all checked on every attempt (`maybe_trigger_lua`, run from the tick after the frame body returned):

1. the GUI reported a waiting mailbox command (`want_lua_pump(App::busy())`, every frame from the Present hook);
2. the `game_tick` hook is active and `post_career_event` resolved (otherwise: "Lua commands run on the next event");
3. `turbo_output\lua_trigger_off.txt` is absent (re-read every 2 s);
4. a real career-mode event has already made Turbo's Lua side pump (`g_pump_tid != 0`), **and that thread is the
   tick's thread**: Live Editor's Lua provably runs on this thread, so the synthetic call cannot race the Lua state.
   Before the first real event (a career is being loaded: `POST_LOAD_PREPARE` etc. fire then) commands wait for it;
5. `PostEvent`'s first bytes carry another module's inline hook (`detect_inline_hook`: `jmp rel32`, `jmp [rip]`,
   `movabs+jmp` or `push+ret` whose target is outside `FC27.exe` and `Turbo.dll`; re-read every 2 s while absent). Without
   a hook the call would be a harmless no-op, so it is simply not made;
6. at most one synthetic event per 250 ms (`kTriggerIntervalMs`), and the objects are re-verified
   (`verify_synthetic_event`) right before each one: any modification disables the trigger for the session.

Proof that it worked: `turbo_game_pump` entered while the synthetic call is in progress means Live Editor ran Turbo's
Lua handler ("Lua pumped N" in the Status tab, `lua_trigger_pumps`). The command itself is acknowledged in the mailbox
as usual, so the overlay's "Queued: ..." label clears within one frame plus Lua's work.

Side effects, by design: Live Editor's `pre__CareerModeEvent` handlers also run (its optional sample logger would print
`Career Mode Event 2122186753`); user scripts registered on `post__CareerModeEvent` receive the id `0x7E7E0001` and the
pointers to Turbo's objects. Nothing of the game's event system is involved, so nothing is persisted or dispatched.

### Known limits

* Needs Live Editor's hook on `PostEvent` specifically. If a Live Editor update hooks a different function, the Status
  tab shows "waiting: no inline hook on post_career_event" and commands run on real events as before.
* Needs one real career-mode event first (to learn the Lua thread); loading a career provides it.
* The mailbox still runs one command per poll; the GUI submits one at a time anyway.

## 5. In-game test plan (integrator)

1. Start the game with Turbo as usual; open the Turbo window, Status tab. Expect under "Game hooks": `Signatures: 2 of 2
   found`, `game_tick: found at 0x1459E2E7C`, `post_career_event: found at 0x14060124C`, `Hooks active: 1`, `game_tick:
   active ... calls N` with N rising every frame (about 60/s in menus), `Game-thread dispatcher: prompt (game_tick hook)`.
   `turbo_gui.log` has `game hook game_tick installed at 0x1459E2E7C` and `game thread: game_tick hook runs on thread T`.
2. Load a Manager Career. Expect `threads: tick T | Lua pump P` with **T == P** and no `(DIFFERENT ...)` marker; the log
   has no `WARNING the game tick runs on thread`. Expect `Prompt Lua commands: ready (jmp rel32 -> FCLiveEditor.DLL)`
   (or `waiting: no inline hook ...` if Live Editor hooks elsewhere: then stop here and report the Status line).
3. In a career menu, **without advancing the calendar**: Turbo Tools -> export a table (or any mailbox command). Expect
   the "Queued: ..." label to clear and the result toast within about a second; `sent` and `Lua pumped` in the Status
   tab both increase by one or two (never more than four per second); the exported file exists.
4. Repeat 3 with an import / clone / create-player command and with the window in a different career screen (squad hub,
   calendar, transfer hub). Expect the same.
5. Create `turbo_output\lua_trigger_off.txt`: within 2 s the Status line reads `off: turbo_output\lua_trigger_off.txt
   present`; a command then waits for the next real event (advance a day: it completes). Delete the file: back to
   `ready`.
6. Create `turbo_output\hook_game_tick_off.txt`: `game_tick: KILLED`, ticks stop rising, commands wait for real events;
   delete it: ticks resume.
7. Stability: 10 minutes of menu use (squad, transfers, calendar, several commands) and one played match (90 minutes
   simulated or played), then advance several days. Expect no crash, `errors 0` on the hook line, `failed 0 | dropped 0`
   on the dispatcher line, frame rate unchanged (the detour does nothing when the queue is empty and no command waits).
8. Live Editor's own features still work (its log shows its career events; no `[LE::...]` errors mentioning event
   2122186753 other than its sample logger line if that script is enabled).

If step 2 shows T != P: report both ids and the `WARNING` log line; the trigger stays off by itself, native jobs still
run on the tick. If step 3 does not complete within 2 s while the Status line says `ready`: report `sent` / `Lua pumped`
(`sent > 0, Lua pumped 0` means Live Editor's detour ran but not Turbo's handler: check `lua\autorun` / the bridge log).

## 6. Follow-ups

* Native execution of the mailbox commands (a native `InsertDBTableRow` equivalent plus the export/clone logic) would
  remove the dependency on Live Editor's hook entirely; the tick hook and dispatcher are ready for it.
* `docs/re/game_thread-signatures.json` + `scripts/re/verify_game_thread.py` are the recipe for a title update: dump
  the new image, run the script, fix the two patterns, drop them into `turbo\signatures_<build>.json` (no rebuild).
