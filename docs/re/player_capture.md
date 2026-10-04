# FC 27 player capture (miniface from the in-game 3D model): static RE notes

Track C1, 2026-10-03. Game build 1.0.140.64835, memory image `C:\FC 27 Live Editor\turbo_output\fc27_image.bin`
(main module, base `0x140000000`, file offset = VA - base). Static analysis only (capstone), nothing was run in the
game. Helpers: `scripts/re/rx_capture.py` (mmap + capstone), `scripts/re/ripscan.c` (rip-relative / rel32 reference scanner,
0.2 s per pass over `.text1`), `scripts/re/disas.py`, `callers.py`, `qwords.py`, `hash.py`, `sig_capture.py`. Signatures:
`scripts/re/player_capture_signatures.json` (every one checked unique in this image).

Confidence tags: **[H]** read directly from code, **[M]** strong inference, **[L]** guess. Scope: offline career only;
the FE message handler factories live in the game's protected code region (`.data` RVA `0x1139B500`-ish, see 2.1) and
were deliberately not followed.

## 0. Summary

* The game has one **`PlayerCaptureController`** object (0x138 bytes, lazily created, found by name
  `"playerCaptureController"`). It renders up to **6 players per request** from their 3D models into a staging buffer
  and hands the result to the UI image registry under the virtual path
  **`data/ui/imgAssets/playerCapture/p<name>.png`** (string key). The regular minifaces live in the same registry
  under `data/ui/imgAssets/heads/p<playerid>.png` [H].
* Two **plain C++ entry points** exist that do the whole job (no FE message needed):
  `PlayerCapture_RequestStatic_A` @ `0x1470DBB68` and `..._B` @ `0x1470DB908`. Both call the singleton getter
  `0x1470E291C` and then `PlayerCaptureController::Request` -> `Start` (`0x1470E49CC`) [H].
* **Gate**: `Start` returns at once unless a byte at `[settings 0x14C1ED820] + 0x1F6` is non-zero [H]
  (which setting it is: [L], see 4). The second gate is the capture renderer being alive (`[0x14C267B98]`).
* The UI side (FE) talks to it through two Frostbite-style messages, **`FE::FIFA::PlayerCaptureRequest`**
  (payload type `PlayerCaptureRequest`, 0x28 bytes) and **`FE::FIFA::PlayerCaptureInit`** (0x30 bytes). Message ids
  are `djb2-xor(name, 0x1505)`: `0x93BABF28` and `0x97279EC3` [H]. The handlers for them are in the protected region,
  so Turbo should call the C++ entry points instead of posting the messages.
* Career mode drives the same controller from its state machine (`CareerModeStateInitPlayerCaptures` /
  `CareerModeStateUpdatePlayerCaptures`, funcs `0x147BB4730`, `0x147BBB400`) and the youth academy from
  `YouthPlayerManager::PortraitCaptureExecutor` (`0x147DEE14C`, a job named "Special Youth Player Image Capture") [H].
  So the pipeline is initialised and usable in the **career hub** and the **youth academy** screens.
* Where the image ends up: the controller's completion listener publishes the rendered texture into the UI image
  registry (key = png path). It is a **GPU texture / registry entry, not a file on disk** [M]. Turbo therefore
  receives the picture either by (a) hooking the controller's completion delegate (gets the staging buffer) or (b)
  reading the registry entry's texture back (DX12 readback). (a) is the recommended path (see 5).

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
| `PlayerCaptureStream` | `0x14AEDFE60` | `0x1470F2A80` (second request path, see 2.4) |
| `data/ui/imgAssets/playerCapture/p%s.png` | `0x149799650` | `0x1470DBE30` (release), `0x1428DFDD8`, `0x1480024F4`, `0x148002830` |
| `data/ui/imgAssets/heads/p%d.png` | `0x14ABC08A0` | `0x1470DBEE0` (release regular minifaces) |
| `YouthPlayerManager::PortraitCaptureExecutor` | `0x14B0178E8` | `0x147DEE14C` |
| `CareerModeStateInitPlayerCaptures` | `0x14AFFA060` | career state machine `0x147BB4730`, `0x147BB65AC`, `0x147BB84A4`, `0x147BBA6A0` |
| `CareerModeStateUpdatePlayerCaptures` | `0x14AFF9C08` | `0x147BBB400` |
| `captureManagerAvatar` | `0x14B0AFB70` | `0x14847458D` (manager avatar capture, FE data) |
| `avatar.headshot` | `0x14AC3C218` | `0x144F7404C` |
| `EnablePlayerCapture` | `0x14B08D0D0` | `0x14835BDA4`, `0x1485DCBEC`: a **bool field of a UI card data model** (next to `PlayerId`, `IsAIPlayer`, `IsSmallCard`, `ArchetypeId`), not the capture gate |

## 2. Code map

### 2.1 FE message registration (`0x141FFC398`) [H]

For each `FE::FIFA::*` message the function computes `id = Hash(name, 0x1505)` with `0x140C22FF0`
(`h = (h * 33) ^ c`, i.e. djb2-xor) and stores it in a big dispatcher object: `PlayerCaptureRequest -> this+0x568`,
`PlayerCaptureInit -> this+0x5E0`, `DynTifoPlayerCaptureRequest -> +0x658`, `DynTifoPlayerCaptureInit -> +0x6D0`.
A per-message handler object is then created by a templated factory (`0x141D5E1EC`, `0x142860818`, ...) and bound
through `[this+0x28]` (the message bus). Every factory is a single `jmp` into the protected region (`0x15139B500`
etc.), so the handler bodies were **not** analysed. Payload layouts from the Frostbite TypeInfoData:
`PlayerCaptureRequest` = 0x28 bytes, `PlayerCaptureInit` = 0x30 bytes (field names not recovered: the field tables
at `0x14BE58DB8` / `0x14BE56E50` are type-node lists, not FieldInfoData) [M].

### 2.2 The controller

```
PlayerCaptureController* GetOrCreate()           0x1470E291C   [H]
    arena = FETemp (TLS slot 0x2FB0/0x3050, name "FETemp")
    c = FindExisting()                            0x140737920   (named lookup "playerCaptureController")
    if (!c) { c = arena->vfunc[2](0x138, "PlayerCaptureController"); ctor(c, arena) 0x1470D49E0;
              register under "playerCaptureController" (0x142AEDFA4 root, 0x1470DE62C insert, slot+0x18 = c) }
    return c
```

Layout (from the ctor `0x1470D49E0` and the users) [H unless marked]:

| off | size | meaning |
|---|---|---|
| 0x00 | i32 | state (1 = capturing, set in Start) |
| 0x08 | ptr | staging buffer (allocated in Start from arena +0x130, name "PlayerCapture") |
| 0x10 | i32 | submitted flag (1 after submit) |
| 0x14 | i32 | 0 at Start |
| 0x18 | i32 | request type, -1 in ctor, 0 in Request; passed to `CaptureRequest_Build` as `type` [M: 0 = portrait] |
| 0x20 | eastl::vector | players, element = `PlayerDesc` **0x6C bytes**, **max 6** used (`min(n, 6)` in Start) |
| 0x40 | i32 | mode: 1 -> staging buffer 0x11E1A30 bytes, else 0x11D300 (= 540 x 540 x 4 + 192) [M: resolution class] |
| 0x44 | i32 | extra int, copied into `CaptureRequest+0x444` [L: camera / output flags] |
| 0x48 | 0x20 | params struct copied from the caller (`0x140CC740C` copy) [M] |
| 0x68 | 0x20 | **completion delegate** `{ctx?, ?, obj +0x10, invoke +0x18}`, invoker default `0x1425FF93C`; copied from caller arg |
| 0x88 | 0x20 | second delegate, invoker default `0x1470DD420` (empty); `Request_B` copies the caller's here |
| 0xA8 | eastl::string (SSO 0x10, cap byte +0xB7 = 0xF) | capture **name**: `%s` in `data/ui/imgAssets/playerCapture/p%s.png` [M] |
| 0xC0 | listener 1, vtable `0x14AEE1F30`, +0x8 `0x149703D68`, +0x30 = controller | registered with `[0x14C267B48]->vfunc(0x38)` at Start; GetTypeId `0x1470EB44C` = `0xB1FEE06D` |
| 0xF8 | listener 2, vtable `0x14AEE2C70`, +0x30 = controller | same; GetTypeId `0x1470EB454` = `0x694CE8B7` |
| 0x130 | ptr | arena |

```
void Request_A(this, const eastl::string* name, const vector<PlayerDesc>* players, const void* params,
               Delegate* onDone, int mode, int extra)                    0x1470DB9C4   [H]
    this+0x18 = 0; this+0x68 = *onDone; this+0xA8 = *name; this+0x48 = *params;
    this+0x40 = mode; this+0x44 = extra; reset(this+0x88); Start(this, players)
void Request_B(this, rdx, players, Delegate* d, const eastl::string* s, int mode, int extra)   0x1470DBA8C   [H]
    like Request_A but copies d into +0x88, s into +0x68 and reads the vector from +0x48/+0x50
void Start(this, const vector<PlayerDesc>* players)                      0x1470E49CC   [H]
    settings = [0x14C1ED820] (lazy singleton via 0x1405A4FCC); if (settings->byte[0x1F6] == 0) return;   <-- GATE
    this+0 = 1; this+0x20 = copy(*players); this+0x14 = 0
    this+0x08 = arena->alloc(mode == 1 ? 0x11E1A30 : 0x11D300, "PlayerCapture")
    hub = [0x14C267B48]; hub->vfunc(0x38)(&this->listener1); hub->vfunc(0x38)(&this->listener2)
    list = 0x1470C87C8(players.begin, players.begin + min(n,6)*0x6C)
    req  = CaptureRequest_Build(&local[0x448], this+0x18, &list, this+0x40, this+0x44)     0x1470D4AD8
    CaptureRenderer_Submit([0x14C267B98], req)                                                0x1470CDA34
    this+0x10 = 1
```

`CaptureRequest` (0x448 bytes) [H]: `+0x000 i32 type`, `+0x004 PlayerDesc[10]` (0x6C each), `+0x43C i32 count`,
`+0x440 i32 mode`, `+0x444 i32 extra`. The static wrappers:

```
PlayerCapture_RequestStatic_A(rcx unused, const eastl::string* name, const vector<PlayerDesc>* players,
                              const void* params, [rsp+0x28] Delegate* onDone, [rsp+0x30] int mode,
                              [rsp+0x38] int extra)                                           0x1470DBB68   [H]
PlayerCapture_RequestStatic_B(rcx unused, rdx, players, Delegate* src, [rsp+0x28] int, [rsp+0x30] int)  0x1470DB908 [H]
```
Neither has an `E8` caller in `.text1`; they are reached from the protected region (the FE message handlers) [M].

### 2.3 PlayerDesc (0x6C) and the 0x78 appearance descriptor

The 0x6C element passed to the controller was not fully recovered (the code that fills it is behind the FE message
handlers). What is known [H]: `CaptureSlot_Enqueue` `0x1470E6BF4(owner, PlayerDesc78* d)` copies a **0x78** descriptor
(`+0x74` = used flag) into `owner+0x1188 + slot*0x78` (2 slots) and, when `owner+0x1300 == 1`, calls
`PlayerDesc_FillFromPlayerData` `0x1470DB58C(owner, slot, owner+0x18 playerdata, d)` which fills `d+0x20..d+0x70`
from the loaded player-data object: `+0x20/+0x24` ids (from playerdata `+0xB00/+0xB14`), `+0x28` bool (`+0x73B`),
`+0x2C` (`+0x5C4`), `+0x30` (`+0x48C`), kit/body bytes `+0x38..+0x4B` (from `+0x738..+0x764`), `+0x4C/+0x50`
(`+0xB04/+0xB18`), hair/skin u16s `+0x56..+0x62` (from `+0x72A..+0x736`), `+0x64..+0x70` (`+0xAF4..+0xB0C`). The
first 0x20 bytes (slot index at +0, then ids) come from the FE request. **The practical consequence:** the controller
wants an appearance descriptor, not just a player id; the cheapest way to get a correct one for an arbitrary id is to
let the game build it, i.e. call the static entry with a descriptor copied from a live request (see 5, step 2) or
reuse `0x1470E6BF4` with a loaded player-data object. This is the main open question (6).

### 2.4 Second path: PlayerCaptureStream

`0x1470F0EB4` also builds a `CaptureRequest` (`0x1470D4AD8`) and submits it (`0x1470CDA34`); `0x1470F2A80` names
"PlayerCaptureStream". This looks like the streaming/batch variant used by `CareerModeStateUpdatePlayerCaptures`
to pre-render squad portraits [M]. Not needed for a single-player request.

### 2.5 Where the image lands

`UIImages_ReleasePlayerCaptures` `0x1470DBE30(_, name, vector<string>* names)`: `reg = 0x1470E0C38(name)`; for each
`s`: `path = sprintf("data/ui/imgAssets/playerCapture/p%s.png", s)`; `if (0x14838A9CC(reg, path)) 0x14838A6FC(reg,
path)` (has -> release). Its twin `0x1470DBEE0` does the same with `data/ui/imgAssets/heads/p%d.png` and int ids [H].
So captured pictures are registered in the UI image registry keyed by that path, next to the regular minifaces; the
career hub shows them through normal `<img src>` references. The registry entry is a texture (the staging buffer at
controller+0x08 is uploaded by the completion listener) [M]. No file write was found on this path; the `.png` is a
virtual name. Legacy-file override (`mods\legacy\...heads\p<id>.dds`) is a different mechanism (Live Editor's) and
is where Turbo keeps writing the final DDS.

### 2.6 Youth academy and career hub drivers

* `YouthPlayerManager::PortraitCaptureExecutor::Run` `0x147DEE14C(this, Delegate* onDone)`: copies the delegate to
  `this+0x18`, writes trait `0x704DF` to the youth player (`playerdata+0x48`), and schedules a job with
  `0x140C07F3C` (name "Special Youth Player Image Capture", 16 KB stack, affinity 0x4000). `0x147DEE278` is the
  cleanup (clears trait `0x1B688`/`0x1B68A` on every player in the list) [H]. The job body calls the controller
  (not followed; it is reached through the job's delegate).
* Career: `CareerModeStateInitPlayerCaptures` / `ActionInitPlayerCaptures` and `...UpdatePlayerCaptures` in the state
  machine functions `0x147BB4730`, `0x147BB65AC`, `0x147BB84A4`, `0x147BBA6A0`, `0x147BBB400` [H]. The avatar flow
  (`avatarPlayerCapturesVdd`, `0x1428DFDD8`, `0x1479EC6C4`, `0x147A6813C`) uses the same png path for the manager
  avatar.

## 3. Game states in which it works

* The controller is created on first use anywhere (lazy getter), but `Start` needs (a) the settings byte `+0x1F6`
  and (b) the capture renderer `[0x14C267B98]` and listener hub `[0x14C267B48]` to be non-null. Both are set up
  with the frontend render world, i.e. **in the menus (career hub, squad hub, youth academy)**, not during a match [M].
* Career mode explicitly inits captures (`CareerModeStateInitPlayerCaptures`) when entering the hub, which is the
  state FC 26 LE required too ("open the career hub first"). Youth academy renders youth portraits the same way.

## 4. Init requirements and gates

| gate | where | status |
|---|---|---|
| `settings->byte[0x1F6] != 0` | `Start` `0x1470E4A1C` | which setting: **[L]**. Candidates: a "player capture enabled" profile flag. Turbo can read it with `ProcessMemory` (`[0x14C1ED820] + 0x1F6`) and show it in the greyed-out reason. |
| capture renderer `[0x14C267B98]` non-null | `Start`, `0x1470DBC30` | set once the FE render world exists [M] |
| listener hub `[0x14C267B48]` non-null | `Start` | same |
| one capture at a time | controller is a singleton with one state field | wait for `+0x00` back to 0 / completion delegate before the next request [M] |

## 5. Implementation design for Turbo (against `turbogui/src/win/game_hooks.h`)

Goal: "Generate from 3D model" in the Players/Managers Miniface tab -> picture in the editor's `ed.source` ->
existing framing / `encode_dds_dxt5` / `legacy.save_custom`.

1. **Resolve** (once, at DLL load, on any thread): `GetOrCreate = find_sig(PlayerCaptureController_GetOrCreate)`,
   `RequestStatic_A = find_sig(PlayerCapture_RequestStatic_A)`, `Start = find_sig(PlayerCaptureController_Start)`,
   `Submit = find_sig(CaptureRenderer_Submit)`, plus the rip-relative globals `settings` (`0x14C1ED820`, from the
   `mov rax,[rip+..]` at `Start+0x24`), `renderer` (`0x14C267B98`, from `Start+0x141`). All signatures are in
   `scripts/re/player_capture_signatures.json`; `PlayerCaptureHook::available()` = all resolved and
   `ProcessMemory::read<uint8_t>(settings + 0x1F6) != 0` and `read<void*>(renderer) != nullptr`. The reason string
   for the greyed button names the first failing check.
2. **Learn a descriptor** (phase 1, needed because the 0x6C `PlayerDesc` layout is only partly known): install a
   guarded MinHook detour on `PlayerCaptureController_Start` (`0x1470E49CC`). The detour copies `*players` (vector
   of 0x6C) and the controller's `+0x40/+0x44/+0x48..+0x68/+0xA8` into a Turbo-side template the first time the game
   itself captures (opening the career hub triggers `CareerModeStateInitPlayerCaptures`). Log the 0x6C bytes to
   `turbo_output\player_capture.log` so the id field offsets can be confirmed in one session (expected: the player
   id and team id are among the first dwords, the rest is appearance taken from the player-data object).
3. **Request** (game thread, through the dispatcher lambda): build `players` = one 0x6C entry from the template with
   the id fields replaced by the target player id (managers: the manager's head asset via the avatar path, see 6),
   `name` = `"turbo_<id>"`, `params` = template copy, `onDone` = a Turbo delegate `{0, 0, ctx, &on_done}` (same
   0x20 shape as the game's: invoker pointer at +0x18, object at +0x10), `mode` = template mode (0 = 540 px),
   `extra` = template value. Call `RequestStatic_A(nullptr, &name, &players, &params, &onDone, mode, extra)`.
   Reject the request when the controller `+0x00 != 0` (busy).
4. **Receive**: `on_done` runs on the game thread when the render finished. Read the staging buffer at
   `controller+0x08` (`0x11D300` bytes: 540 x 540 RGBA8 [M], verify the first 192 bytes for a header) into a
   Turbo-owned `Rgba`, post it to the UI thread (`App` queue), and release nothing (the game owns the buffer).
   Fallback if the buffer is not plain RGBA: hook `UIImages` registration (the function that stores
   `data/ui/imgAssets/playerCapture/p%s.png`) and read the DX12 texture back through the overlay's device.
5. **UI** (`ui_images.cpp`, miniface editor, tab "The 3D model", feature flag `kPlayerCaptureHook`): button
   "Generate from 3D model" -> `PlayerCaptureHook::request(id)`; while pending show "rendering..." (about 2 s);
   on arrival `ed.source = picture`, `ed.source_label = "3D model capture"`, `remove_bg` on; the user frames and
   presses the existing "Save as miniface". Success is detected by the completion delegate firing; a 10 s timeout
   clears the pending state and reports "the game did not answer (open the career hub first)".
6. **Safety**: every call goes through the dispatcher on the game thread; the detour is guarded (original always
   called); nothing is written to game memory except our own request structs; the feature is off unless
   `available()`.

## 6. Open questions

* Exact layout of the 0x6C `PlayerDesc` (which dword is the player id / team id / is_gk) - to be read from the
  `Start` detour log in one game session (step 2 above).
* The meaning of the settings byte `+0x1F6` and of `mode` / `extra` (camera head vs head+shoulders?). FC 26 LE's
  camera option probably maps to `type` (`controller+0x18`) or `extra`.
* The completion listener bodies (`0x1470E9E84`, `0x1470E3E08`, `0x1470EA454`) were not read for lack of time; the
  staging-buffer pixel format is therefore [M].
* Managers: the avatar path (`captureManagerAvatar`, `0x14847458D`) uses the manager head asset; whether a manager
  can be passed as a `PlayerDesc` (as FC 26 LE did with the manager's player id) is untested.
* `PlayerCaptureInit` (0x30-byte FE message) may carry size / camera; posting it is not required for the C++ path.
