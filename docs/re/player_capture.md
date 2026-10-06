# FC 27 player capture (miniface from the in-game 3D model): RE notes and the Turbo implementation

Track C1 (2026-10-03, static map) and the implementation track (same day: completion flow, descriptor layout, callback
shape, code). Game build 1.0.140.64835, memory image `C:\FC 27 Live Editor\turbo_output\fc27_image.bin` (main module,
base `0x140000000`, file offset = VA - base). Static analysis only (capstone), nothing was run in the game. Helpers:
`scripts/re/rx_capture.py` (mmap + capstone), `scripts/re/ripscan.c` (rip-relative / rel32 reference scanner, build it
with `gcc -O2 -o scripts/re/ripscan.exe scripts/re/ripscan.c` inside MSYS2), `disas.py`, `callers.py`, `qwords.py`,
`hash.py`, `sig_capture.py`. Signatures: `scripts/re/player_capture_signatures.json` (every one checked unique in this
image) and the built-in table in `turbogui/src/core/sigscan.cpp`.

Confidence tags: **[H]** read directly from code, **[M]** strong inference, **[L]** guess. Scope: offline career only;
the FE message handler factories live in the game's protected code region and were not followed.

## 0. Summary

* The game has one **`PlayerCaptureController`** (0x138 bytes, lazily created, registry name
  `"playerCaptureController"`). It renders up to **6 players per batch** from their 3D models and either registers the
  pictures in the UI image registry (`data/ui/imgAssets/playerCapture/p<name>.png`, the career hub path) or hands the
  bytes of each picture to a **callback** (`Request_B`, the path the career avatar / pro card code uses) [H].
* Two plain C++ entry points do the whole job, no FE message needed: `PlayerCapture_RequestStatic_A` @ `0x1470DBB68`
  (registry path: name, players, image names, done callback, mode, extra) and **`PlayerCapture_RequestStatic_B`** @
  `0x1470DB908` (callback path: players, per-picture callback, done callback, mode, extra) [H]. **Turbo uses B.**
* The picture arrives as a byte buffer `(int id, void* data, size_t size)` on the game thread, the same bytes the game
  wraps as an image stream for its UI registry: a picture file in memory [M: DDS expected; decoded at run time by
  `decode_slice`, which also understands PNG / BMP / raw RGBA]. The staging buffer is freed when the batch finishes, so
  the callback copies the bytes at once [H].
* **Gates**: `Start` returns at once unless `[settings 0x14C1ED820]->byte[0x1F6] != 0` [H]; the Rubber message sender
  `[0x14C267B98]` and listener hub `[0x14C267B48]` must exist (they are created with the frontend, i.e. in the menus) [H].
* The 0x6C-byte **PlayerDesc** is built in `.text1` by the career avatar manager-portrait code and by the pro-card code:
  `+0x00` = the id the picture is for (passed back to the callback), `+0x08` = a second id, everything else is a fixed
  default pattern (section 2.3) [H for the layout, M for the meaning of `+0x08`]. Turbo also learns the descriptor the
  game itself uses (detour on `Start`) and can start from it.
* Implemented in Turbo 0.4: `turbogui/src/core/player_capture.{h,cpp}` (platform independent, tested),
  `turbogui/src/win/player_capture_win.cpp` (hooks + request), the "The 3D model" tab of the Miniface editor
  (`ui_images.cpp`), Status tab line, `turbo_output\player_capture.log`. Section 5.

## 1. Strings and where they are used

| string | VA | used by |
|---|---|---|
| `FE::FIFA::PlayerCaptureRequest` | `0x1496E5728` | `0x141FFC84A` in message registration `0x141FFC398` |
| `FE::FIFA::PlayerCaptureInit` | `0x1496E53E0` | `0x141FFC8BA` (same function) |
| `FE::FIFA::DynTifoPlayerCaptureRequest/Init` | `0x1496E4D20` / `0x1496E5AB8` | same function (tifo variant, ignore) |
| `PlayerCaptureRequest` / `PlayerCaptureInit` (TypeInfoData names) | `0x14A95A388` / `0x14A95A1A0` | reflection descriptors at `0x14E6D60D0` / `0x14E6D54D0` |
| `PlayerCaptureController` | `0x14AEE00C8` | allocation name in `0x1470E291C` |
| `playerCaptureController` | `0x14961C3A8` | registry name in `0x1470E291C`, lookup `0x140737920` |
| `PlayerCapture` | `0x14AEDFE50` | staging-buffer allocation name in `Start` `0x1470E49CC` |
| `PlayerCaptureStream` | `0x14AEDFE60` | `0x1470F2A80` (per-picture handler, section 2.6) |
| `data/ui/imgAssets/playerCapture/p%s.png` | `0x149799650` | `0x1470DBE30` (release), `0x1428DFDD8` (avatar flow), `0x1480024F4`, `0x148002830` |
| `data/ui/imgAssets/heads/p%d.png` | `0x14ABC08A0` | `0x1470DBEE0` (release regular minifaces) |
| `avatarPlayerCapturesVdd` | | `0x1428DFDD8`: avatar outfit pictures (names `%d_%d`, mode 1, extra 0/3) |
| `cmAvatarPlayerHeadsVdd` | | `0x147D94218`: career avatar manager portrait (section 2.3) |
| `YouthPlayerManager::PortraitCaptureExecutor` | `0x14B0178E8` | `0x147DEE14C` |
| `CareerModeStateInitPlayerCaptures` | `0x14AFFA060` | career state machine `0x147BB4730`, `0x147BB65AC`, `0x147BB84A4`, `0x147BBA6A0` |
| `CareerModeStateUpdatePlayerCaptures` | `0x14AFF9C08` | `0x147BBB400` |
| `captureManagerAvatar` | `0x14B0AFB70` | `0x14847458D` (manager avatar capture, FE data) |
| `EnablePlayerCapture` | `0x14B08D0D0` | `0x14835BDA4`, `0x1485DCBEC`: a **bool field of a UI card data model**, not the capture gate |

## 2. Code map

### 2.1 FE message registration (`0x141FFC398`) [H]

For each `FE::FIFA::*` message the function computes `id = Hash(name, 0x1505)` with `0x140C22FF0`
(`h = (h * 33) ^ c`, djb2-xor) and stores it in a dispatcher object: `PlayerCaptureRequest -> this+0x568`,
`PlayerCaptureInit -> this+0x5E0`. The handler factories are single `jmp`s into the protected region and were not
analysed. Payload sizes from the TypeInfoData: `PlayerCaptureRequest` 0x28, `PlayerCaptureInit` 0x30 [M].

### 2.2 The controller

```
PlayerCaptureController* GetOrCreate()           0x1470E291C   [H]
    arena = FETemp (TLS slot 0x2FB0/0x3050, name "FETemp")
    c = FindExisting()                            0x140737920   (named lookup "playerCaptureController")
    if (!c) { c = arena->vfunc[2](0x138, "PlayerCaptureController"); ctor(c, arena) 0x1470D49E0;
              register under "playerCaptureController" }
    return c
```

Layout [H]:

| off | size | meaning |
|---|---|---|
| 0x00 | i32 | state: 0 idle, 1 submitted (Start), 2 rendered (listener 2), 3 copying (Complete), 4 finishing (OnSlotEvent), back to 0 in Finish |
| 0x08 | ptr | staging buffer (arena alloc in Start, name "PlayerCapture", freed in Finish) |
| 0x10 | i32 | batch number (1 after Start, +1 per further batch of 6) |
| 0x14 | i32 | pictures received in the current batch |
| 0x18 | i32 | request type, -1 in ctor, 0 in Request_A / Request_B (-> CaptureRequest+0) |
| 0x20 | eastl::vector<PlayerDesc 0x6C> | players (copied from the request) |
| 0x40 | i32 | mode: 1 -> staging buffer 0x11E1A30 bytes (6 x 0x2FAF08), else 0x11D300 (6 x 0x2F880) |
| 0x44 | i32 | extra, copied into `CaptureRequest+0x444` |
| 0x48 | eastl::vector<eastl::string 0x18> | **image names** (registry path: `data/ui/imgAssets/playerCapture/p<name>.png`); cleared on the callback path |
| 0x68 | eastl::function 0x20 | **done callback** (invoked by Finish after the buffer is freed) |
| 0x88 | eastl::function 0x20 | **per-picture callback** `(int id, void* data, size_t size)` (Request_B); empty on the registry path |
| 0xA8 | 16-byte SSO string | registry name (`avatarPlayerCapturesVdd`, `cmAvatarPlayerHeadsVdd`, ...); **empty = callback path** |
| 0xC0 | listener 1, vtable `0x14AEE1F30`, GetTypeId `0xB1FEE06D` | "picture copied" event -> `OnSlotEvent` `0x1470F0EB4` |
| 0xF8 | listener 2, vtable `0x14AEE2C70`, GetTypeId `0x694CE8B7` | "rendered" event -> state 2, `Complete` `0x1470F60F0` |
| 0x130 | ptr | arena |

```
void Request_A(this, const string* name, const vector<PlayerDesc>* players, const vector<string>* names,
               [rsp+0x28] const Function* onDone, [rsp+0x30] int mode, [rsp+0x38] int extra)       0x1470DB9C4  [H]
    +0x18 = 0; +0x68 = *onDone; +0xA8 = *name; +0x48 = *names; +0x40 = mode; +0x44 = extra; +0x88 = empty; Start(this, players)
void Request_B(this, const vector<PlayerDesc>* players, r8 unused, const Function* onSlot,
               [rsp+0x28] const Function* onDone, [rsp+0x30] int mode, [rsp+0x38] int extra)       0x1470DBA8C  [H]
    +0x18 = 0; +0x68 = *onDone; +0x88 = *onSlot; +0x40 = mode; +0x44 = extra; +0xA8 = ""; +0x48 cleared; Start(this, players)
void Start(this, const vector<PlayerDesc>* players)                                                0x1470E49CC  [H]
    settings = [0x14C1ED820] (lazy via 0x1405A4FCC); if (settings->byte[0x1F6] == 0) return;        <-- GATE
    +0 = 1; +0x20 = copy(*players); +0x14 = 0
    +0x08 = arena->alloc(mode == 1 ? 0x11E1A30 : 0x11D300, "PlayerCapture")
    hub = [0x14C267B48]; hub->vfunc(0x38)(&listener1); hub->vfunc(0x38)(&listener2)
    req = CaptureRequest_Build(&local, +0x18, players[0 .. min(n,6)), +0x40, +0x44)                 0x1470D4AD8
    Submit([0x14C267B98], req)   = sender->vfunc(0x48)(&id 0x2B4062D1, &id, req, 0x448, 0xFF, 0)   0x1470CDA34
    +0x10 = 1
PlayerCapture_RequestStatic_A(rcx unused, name, players, names, [rsp+0x28] onDone, [rsp+0x30] mode, [rsp+0x38] extra)   0x1470DBB68 [H]
PlayerCapture_RequestStatic_B(rcx unused, players, onSlot, onDone, [rsp+0x28] mode, [rsp+0x30] extra)                     0x1470DB908 [H]
```

`CaptureRequest` (0x448): `+0 i32 type`, `+4 PlayerDesc[10]`, `+0x43C count`, `+0x440 mode`, `+0x444 extra` [H]. The
globals `0x14C267B48` / `0x14C267B98` / `0x14C267BA0` / `0x14C267BA8` are the "Rubber" messaging subsystem singletons
created by `0x141446270` (allocation names "Rubber", 0x188 / 0x160 / 0x110 bytes) and torn down by `0x1417BEEB0` [H];
`vfunc 0x48` of `[0x14C267B98]` sends a typed message, `vfunc 0x38` / `0x40` of `[0x14C267B48]` add / remove a listener.

### 2.3 PlayerDesc (0x6C) as the game's own builders write it [H]

Two builders live in `.text1` and write the descriptor dword by dword:

* `0x147D94218` (career avatar, registry `cmAvatarPlayerHeadsVdd` / image `data/ui/imgAssets/%s/%s_%d.png` with
  `heads_staff`): a **manager head** capture. id = `[this+0x48]` (compared with 9999 = the user's created avatar),
  second id = `[this+0x4C]`. Uses `RequestStatic_B` through the FE image service's vtable slot `+0x48` with mode 0,
  extra 0 and a per-picture lambda that captures the id (`0x147D8E738`).
* `0x1485DF1AC` (pro / clubs card, "initializeSessionCall"): id = 0x7AA7 (31399, the pro's player id), second id =
  a service value (`[service vtable+8]()`), mode 0, extra 0, `RequestStatic_B` through the same vtable slot.

| off | value | note |
|---|---|---|
| 0x00 | **id** | player id (pro card), manager head id (avatar); `OnSlot` passes it to the per-picture callback |
| 0x04 | -1 | |
| 0x08 | second id | `[obj+0x4C]` / a service value [M: team id] |
| 0x0C, 0x10 | -1, -1 | |
| 0x14 .. 0x1F | 0 | |
| 0x20 .. 0x2F | -1, 1, 0, 0 | 16-byte constant at `0x14B191980` (a second constant `0, 2, 0, 0` at `0x14B191990` is used by `0x1446183F8`) |
| 0x30 .. 0x4B | 0 | |
| 0x4C | -1 | |
| 0x50 | 1 | |
| 0x54, 0x58 | -1, -1 | |
| 0x5C .. 0x63 | 0 | |
| 0x64 | byte | 1 in the pro-card builder, 0 in the manager builder [L: player vs staff head] |
| 0x65 .. 0x67 | 0 | |
| 0x68 | byte | manager builder: `id == 9999` |
| 0x69 .. 0x6B | 0 | |

The avatar outfit flow (`0x1428DFDD8`) keeps an array of `{PlayerDesc; int outfit_id (+0x6C); int flag (+0x70)}`
(stride 0x74) and names its pictures `%d_%d` (id, outfit id) with mode 1 and extra 3 (or `%d` with extra 0) through
vtable slot `+0x28` (= `RequestStatic_A`) [H]. The 0x78 descriptor of `CaptureSlot_Enqueue` `0x1470E6BF4` /
`PlayerDesc_FillFromPlayerData` `0x1470DB58C` (four `optional<int>` then appearance fields from a loaded player-data
object) is a different struct used by the 3D viewer slots, not the capture request [M].

`core/player_capture.cpp::default_desc()` reproduces this table; `turbo_output\player_capture.log` records the
descriptors of the game's own requests (hex + non-zero dwords) so the meaning of `+0x08` and `+0x64` can be confirmed
from a squad-hub capture.

### 2.4 The callback object: eastl::function [H]

`Function` = `{ uint8_t storage[16]; void* (*manager)(void* to, void* from, int op); R (*invoker)(Args..., const void* storage) }`
(0x20 bytes). Ops: 0 destruct, 1 copy, 2 move (the game's lambda manager `0x147D8F794` copies its captured int for
ops 1 and 2 and ignores 0). Copy helpers `0x1470DCD08` / `0x14056CB50` call `manager(&dst.storage, &src.storage, 1)` then
copy the two pointers; destroy helpers `0x1470DD9E4` / `0x14056F208` call `manager(&storage, nullptr, 0)` when the
manager pointer is non-null. The empty invoker `0x1470DD420` writes `0xDEADC0DE` to address 0 (a deliberate crash), so
a delegate must always carry a real invoker. The per-picture invoker is called as
`invoker(int id, void* data, size_t size, const void* storage)` from `OnSlot+0x1AB`; the done invoker as
`invoker(const void* storage)` from `Finish+0x6C`, only when `storage` has a non-null manager (`+0x10`).
`core/player_capture.h::Delegate` / `delegate_manager` implement this shape.

### 2.5 Events and the renderer side

* `Submit` sends Rubber message `0x2B4062D1` (type id function `0x14388BC80`) with the 0x448 request; message
  `0xC0DB457A` with `{0, 3}` is sent by `OnSlotEvent` (next batch) and `Finish` (reset) [H].
* `Complete` `0x1470F60F0` (listener 2, state 3) sends, for slots 0..5, a message of type `0xBAD03A44` (type id
  function `0x14388B150`) with payload `{buffer + slot * sliceSize, sliceSize, 0}` (0x10 bytes, flags 0xFF / 0):
  "copy picture `slot` into this memory" [H].
* `0x142109598` (a message handler in `.text1` for ids `0xC33E10AC`, `0xBDD945F4`, `0x393610B6`) answers the copy with
  event `0xB1FEE06D` and payload `{0, size, slot}` (0xC bytes) [H]; `0x143B43D84` maps event ids to names (`0x694CE8B7`
  = the "rendered" event).
* Slice sizes: `0x2F880` (mode 0) and `0x2FAF08` (mode 1); the payload's `size` is the real picture size, which the
  game passes to the stream constructor `0x14838A390` (protected) and to the callback. The pixel format is not visible
  statically (the receiving services are opaque virtual calls); Turbo sniffs it (DDS / PNG / BMP / raw RGBA square).

### 2.6 Completion flow [H]

```
Start ──Submit──▶ renderer ──event 0x694CE8B7──▶ listener2: state 2, Complete(): state 3, 6 x copy messages
       ◀──event 0xB1FEE06D {0,size,slot} per copy── listener1: OnSlotEvent(ctl, payload) 0x1470F0EB4:
            OnSlot(ctl, slot, size) 0x1470F2A80:
                data = buffer + slot * sliceSize
                if (name != "")  registry[name].add(stream(data, size)) under names[(batch-1)*6 + slot]
                else if (onSlot) onSlot.invoker(players[(batch-1)*6+slot].id, data, size, &onSlot)   <-- Turbo's picture
            if (++received == players - (batch-1)*6)  state 4, Finish(ctl) 0x1470DD438:
                state 0; send 0xC0DB457A {0,3}; unregister listeners; arena->free(buffer); if (onDone) onDone.invoker(&onDone)
            else if (received == 6)  next batch: state 1, send {0,3}, Build + Submit the next 6 players, ++batch
```

### 2.7 Other drivers

* Youth academy: `YouthPlayerManager::PortraitCaptureExecutor::Run` `0x147DEE14C` schedules the job "Special Youth
  Player Image Capture" (16 KB stack, affinity 0x4000) whose body is the generic job runner `[0x14C264488]`; the vector
  copy helpers `0x147D7D460` / `0x147D76358` show it builds `vector<PlayerDesc>` too [H].
* Career hub: `CareerModeStateInitPlayerCaptures` / `UpdatePlayerCaptures` state machine `0x147BB4730` .. `0x147BBB400`
  (through the FE message path) [H]; the user's manager portrait: `0x147D94218` (2.3).
* The game's own per-picture lambdas (`0x147D7A7C8` for the manager head, `0x1485D2194` for the pro card) hand the bytes to
  an image service (`service 0xEB37E7B -> vfunc 0x28 -> +0x1C0 -> vfunc 0x20(data, size), vfunc 0x28(id)`), i.e. they
  never write a file [H].

## 3. Game states in which it works

* `Start` needs the settings byte, the Rubber sender and hub (created with the frontend) and the capture renderer behind
  the messages: **menus** (career hub, squad screens, youth academy, create player), not during a match [M].
* One capture at a time: the controller is a singleton with one state field; Turbo refuses a request while `+0 != 0`.

## 4. Gates (what Turbo checks before asking)

| gate | where | Turbo |
|---|---|---|
| `settings->byte[0x1F6] != 0` | `Start+0x50` | read through `PlayerCapture_Settings` (rip at `Start+0x24`); when the settings object does not exist yet the request still goes (Start creates it) |
| `[0x14C267B98]` non-null | `Start+0x141` | `PlayerCapture_Renderer` |
| `[0x14C267B48]` non-null | `Start+0xB4` | `PlayerCapture_ListenerHub` |
| controller state 0 | `+0x00` | checked on the game thread right before the call, and after it (1..4 = started) |

## 5. Implementation (Turbo 0.4)

* **Signatures** (built-in table `sigscan.cpp`, build `6AB9813C-211EF000`; the file `turbo\signatures_<build>.json`
  overrides it): `PlayerCaptureController_GetOrCreate`, `PlayerCapture_RequestStatic_B`, `PlayerCaptureController_Start`,
  `PlayerCapture_Settings` / `_ListenerHub` / `_Renderer` (Start's pattern with `rip` at +0x24 / +0xB4 / +0x141),
  `PlayerCaptureStream_OnSlot`. All unique in the image (`sig_capture.py`); the native tests resolve them on a synthetic
  buffer.
* **Hooks** (pass-through, learn only): `pc_start` on `Start` logs every request (type, mode, extra, registry name, first
  image name, up to 6 descriptors in hex) and keeps the first non-Turbo one as the **template**; `pc_slot` on `OnSlot`
  logs slot, size and the first 16 bytes of every picture (the format sniff). Kill switches: `turbo_output\hook_pc_start_off.txt`,
  `hook_pc_slot_off.txt`, `game_hooks_off.txt`, and `turbo_output\player_capture_off.txt` (whole feature).
* **Request** (`host::GameCapture::request`): queued through `run_on_game_thread`; on the game thread: read the
  globals, `GetOrCreate()`, check state 0, build `eastl::vector<PlayerDesc>{&desc, &desc+1, ...}` and the two
  delegates (storage = the job pointer, manager = `delegate_manager`), call
  `RequestStatic_B(nullptr, &vec, &onSlot, &onDone, mode, extra)` with `g_own_request` set so `pc_start` tags it, then
  check the state moved to 1..4. The descriptor: the learned template with `+0x00` (and `+0x08` when a team id is known)
  replaced, else `default_desc(id, second_id, !manager, false)`. Mode / extra: camera presets `(0,0)`, `(1,0)`,
  `(1,3)`, `(0,3)` or the template's pair; advanced overrides in the tab.
* **Batches** (real-face chooser, after 1.2.3): `Request::batch` carries up to 6 heads (distinct ids); the job owns a
  `PlayerDesc[6]` and the vector spans the used ones, so the controller renders them as one batch (2.6: one `OnSlot`
  picture per slot with its descriptor's id, then `Finish` and the done callback). A per-job `BatchBook` matches pictures
  to heads by id; `poll()` hands out one `Result` per picture, then a failed one for each head without a picture once the
  done callback fired (or the 20 s timeout). `turbo_output\player_capture_batch1.txt` = one head per request.
* **Receive**: `on_slot_invoker` copies the bytes (≤ 32 MB) into the job under a mutex; `on_done_invoker` marks it
  finished. `poll()` (Turbo's render thread) decodes with `decode_slice` and returns `Result{image, format}`; a failure
  saves the bytes to `turbo_output\player_capture_<id>.bin`. Timeout 20 s after the job ran; while the Lua pump is the
  dispatcher the job waits for the next career-mode event (the tab says so).
* **UI**: Miniface editor tab "The 3D model" (players and managers): game id (editable), camera combo, "start from the
  learned descriptor", advanced mode / extra, Generate, Cancel, status line, counters; the picture becomes the New
  source (background removal on) and "Save as miniface" writes the DDS as before. Status tab: one line with the service
  state. Jobs are never freed (the game may hold our delegate storage); only their bytes are released.

## 6. Open questions (for the first in-game session)

* Which (mode, extra) gives the head / head-and-shoulders / body framings FC 26 LE offered; whether `type` (+0x18,
  always 0 on these paths) matters. The camera combo and the advanced fields exist to find out.
* Meaning of `+0x08` (team id?) and `+0x64` for a *player* request: compare the log of a squad-hub capture (the FE path
  builds the descriptor from `PlayerCaptureRequest`) with Turbo's. If the FE descriptor differs, "start from the learned
  descriptor" uses it with the id swapped.
* Picture format and size of the callback bytes (DDS expected: `OnSlot` wraps them as an image stream). The log prints
  the first 16 bytes of every picture.
* Managers: whether the head id or the managerid is the right `+0x00` for a staff head outside the career avatar flow.
* Whether the capture renderer answers from the Players tab of the career hub (where Turbo is normally used); if not,
  the squad hub / player bio screen has to be open.
