# Legacy files on the async path: why modded crests hang, and Turbo's crest serve (2.18)

Build 6AC07E31-2145C000 (FC27.exe after the October 2026 title update). Source: the live image
`turbo_dev\evidence\2026-10-18\W1141\fc27_live_6AC07E31.bin` (RVA = offset), the Live Editor log of 2026-10-18 and the Architect's
RE note `turbo_dev\squads_pack\re_compdata\LE_ASYNC_CREST.md` (its FC27-side parts only; nothing in FCLiveEditor.DLL is read,
patched or assumed by Turbo).

## 1. The game side
| FC27 | What |
|---|---|
| +199DB6C | `LegacyFS::TryFindFile(path, Desc* out) -> bool`. Its first 6 bytes in the running game are Live Editor's `FF 25` jump, so Turbo resolves it through two of its calls (section 3). |
| +306C2AC | `BeginLoadAsync(Desc*, <unused>, u64 size, void* buf, const char* path) -> Request*`: the async entry (= BeginLoad +306C18C with file offset 0). |
| +199DCE0 / +199D944 | IsDone(req) / Finish-and-release(req) -> bool: mode B (`[req+0x17] == 8`, resident chunk) memcpy, mode A future. |
| Desc (0x30) | `+0 GUID chunk; +0x10 start; +0x18 end; +0x20 offset; +0x28 size`. |

The three callers of BeginLoadAsync (each: TryFindFile -> allocate buf(desc.size) -> BeginLoadAsync -> poll):
| Caller | Call | Return | NULL result | Size used afterwards |
|---|---|---|---|---|
| ChunkFileLoaderData (+1C3D4B0) | +1C3D587 | +1C3D58C | tested (`test rax,rax; je`): frees buf, " failed to begin load" | its own desc.size (`[rsp+68]`) |
| ChunkFileAsyncLoad (+38886C0; capture, match) | +38887B9 | +38887BE | tested (`test rax,rax; jne`): frees buf through the allocator | its own desc.size (`[rsp+58]` = desc+0x28) |
| script CallPromise (+46AD2F0) | +46AD389 | +46AD38E | NOT tested: passed straight to IsDone, which reads `[req+0x17]` (a NULL would crash) | its own size; Finish's bool ignored |

**Why a modded crest hangs:** for a file in `mods\legacy` Live Editor's TryFindFile answers true and writes only `desc.size` (the
size on disk). GUID / start / end stay stale, so BeginLoad submits a chunk read for a chunk that does not exist and the request never
completes: the career start waits in InitPlayerCaptures (37 %), the match transition waits on its crest. Live Editor serves only the
sync LoadFile path (+199D960), which the async callers never use.

## 2. Crest serve (turbogui/src/core/crest_serve.h, src/win/crest_serve_win.cpp)
Hook `crest_serve` on BeginLoadAsync. Per call:
1. Fast exit to the original: no path, serve off, kill switch of the hook, or the path not under a configured prefix
   (turbo_config.json `crest_serve.paths`, default `data/ui/imgAssets/crest/`; case-insensitive, either slash).
2. The path must be a safe relative path (no drive, no leading slash, no `..`), not killed (`turbo_output\crest_serve_off.txt`),
   present in `<LE folder>\mods\legacy\<path>` with **exactly the requested size** (Live Editor wrote desc.size = the size on disk;
   any other size means Live Editor did not serve this file). Otherwise the original, untouched (native crests).
3. The decoy, once per session on the first serve: the first of a short list of small native legacy files (cmSocialMedia emoji,
   fluxStroke, fifa_ng_db-allow.xml) that is not in `mods\legacy` and that TryFindFile finds with a non-zero GUID and
   0 < size <= 64 KB. Its Desc, path and the 64 KB scratch buffer are static and never written again.
4. The whole file is read; only after a complete read of exactly `size` bytes is it copied into the caller's buf.
5. The original BeginLoadAsync runs for the decoy into the scratch buffer; its Request is returned. IsDone / Finish / release are
   the game's own code on the game's own objects; the caller takes its bytes from buf and its size from its own desc.
6. A failure in 3-4 returns NULL to ChunkFileLoaderData / ChunkFileAsyncLoad (they report a failed load, no hang) and the original
   call to CallPromise and to any unknown caller (today's behaviour: never a crash). The caller is told apart by the return address.

Signatures (exact_build, 6AC07E31 only; install refuses unless all nine are found exactly once, the three callers' calls equal the
BeginLoadAsync pattern, both TryFindFile calls agree and the bytes at BeginLoadAsync are the expected function):
`cs_begin_load_async`, `cs_loader_{tryfind_call,async_call,async_ret}`, `cs_promise_{tryfind_call,async_call,async_ret}`,
`cs_capture_{async_call,async_ret}`.

Log (turbo_output\turbo_gui.log, prefix `crest serve:`): the install line; `decoy <path> (...)` once; `served <path> (<n> bytes ...;
caller <name>, return FC27+...; decoy request <p>) #n` for every serve; `original for <path> (<reason> ...)` for crest calls left to
the game (first 20, then every 200th); `FAILED ...` with what the caller got.

Live Editor's log shows one extra `TryFindFile <decoy>` (native, Result = true) per session, not per serve.

## 3. Not covered
- Other modded legacy files on the async path (kits, heads): the same mechanism would serve them if their prefix is added to
  `crest_serve.paths`; not tested, not shipped by default.
- A match hang with a non-crest cause (SUREFIRE s9: RKC v Vitesse hung with the crest absent).
- (X 2.0.1: covered, section 4.)

## 4. Any build (Turbo X 2.0.1, 2026-11-01)
On build 6AD922D1-221A8000 (title update 10-27) the signature table had no cs_* entries (exact_build), so crest serve logged
`crest serve: off (signature(s) not found exactly once: ...)` every session and modded crests in `mods\legacy` broke the match load
again: the owner's Newcastle career crashed at every kick-off load (FC27.exe+0x1F4D3C4, a NULL resource registered on a job thread
running player_kit.lua; 9 dumps 2026-11-01 19:18-20:03; no match had loaded since the club's custom crest was saved on 10-31).
Moving the 100 crest files out was the stop-gap (turbo_backups\crests_off_20261101_2015, Restore-Crests.cmd).

The fix: when the table lacks the cs_* entries, install_crest_serve looks for the three call sites on the running game in a
background thread (`crest_serve::locate`): each site by its exact 6AC07E31 pattern, else by the relaxed pattern (the same bytes
with the stack / frame displacements open: same length, so every call offset holds). BeginLoadAsync is the target of the three
async calls, accepted only when they agree and its first 24 bytes are kBeginLoadAsyncPattern; both TryFindFile calls must agree;
the three sites must differ (check_resolved). Anything else: off, with the reason. The hook goes on the resolved address
(install_game_hook_at). Log: `crest serve: not in the signature table of build ...: looking for the call sites`, then
`crest serve: on (...); build 6AD922D1-221A8000, call sites found on this build (loader exact|relaxed, ...)` or `crest serve: off (...)`.
Native tests: `crest serve X 2.0.1: any build` (synthetic image: exact, moved locals, mixed, two targets, hooked function,
TryFindFile disagreement, a site twice, a site missing, a broken call). Not yet seen on 6AD922D1 in game.
