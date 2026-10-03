# Unfinished static-RE briefs (portrait capture, job offers)

Session `cse_018KNHBkXKaKC6RHBU17Afhu` started three parallel static-analysis sub-agents at 16:44:59-16:45:22 UTC on
2026-10-03, one each for **portrait capture**, **job offers** and **standings / results**. All three were killed by the
account's 5-hour rate limit (`Agent terminated early ... HTTP 429`) at 16:54:52, 16:54:58 and 16:55:30 UTC.

| Brief | Wanted output file (in the dead container) | Result |
| --- | --- | --- |
| 1. Portrait capture ("miniface from the in-game 3D model") | `/home/claude/re/notes_capture.md` | **No notes were saved.** Nothing recovered. |
| 2. Create a job offer | `/home/claude/re/notes_joboffer.md` | **No notes were saved.** Nothing recovered. |
| 3. Standings and match results | `/home/claude/re/notes_standings.md` | Saved. Preserved in `docs/re/standings-fixtures-notes.md`. |

So briefs 1 and 2 are still to do. They are reproduced verbatim below so they can be re-issued. Both depend on a
memory image of the running game (`fc27.bin`, 555 MB, the game's main module dumped by Turbo's dev service with the
`dump` op, loaded at `0x140000000`). That file and the helper scripts (`rx.py`, `rx2.py`, `fields.py`) lived only in the
dead container, so the image must be dumped again from a running game and every address below re-verified on that
build. Treat the addresses in the briefs as hints for the 2026-10-03 build, not as constants.

Constraints that apply to both briefs (from the project's ground rules): Offline Career Mode only; no anti-cheat or
online work; do not touch `FCLiveEditor.DLL`. See `docs/HANDOVER-0.3.0.md` section 1 for the open question about
analysing the game binary at all.

The user's priority order at the time (16:44 UTC): miniface generation from an in-game snapshot "exactly like FC 26 Live
Editor does", editing league results and tables, and creating job offers.

---

## Brief 1: portrait capture

```text
You are doing static reverse engineering of EA SPORTS FC 27 (PC) to support a user-owned modding tool (offline career mode only, no anti-cheat or online work). Goal: find how the game renders a PLAYER PORTRAIT ("miniface") from the in-game 3D player model, so a DLL injected into the game can request a portrait for any player id and get the image back (FC 26 Live Editor did exactly this via the game's PlayerCapture system).

Material (all local, cloud container):
- /home/claude/re/fc27.bin : the game's main module image dumped from memory (loaded at 0x140000000; file offset = VA - 0x140000000; PE headers intact; code mostly in section .text1 RVA 0x1000..0x8B69000; strings live in other sections too).
- /home/claude/re/rx.py : helpers (import with sys.path.insert(0,'/home/claude/re'); from rx import *): find_str(s) -> VAs of NUL-terminated string; rip_refs([targets]) -> {target: [(instr_addr, tail)]} code refs via rip-relative disp32 (fast, numpy); dis(addr, n) capstone disassembly; func_start(addr) heuristic; cstr(addr). capstone and pefile are installed. Python 3.
- Known strings (VA): 'FE::FIFA::PlayerCaptureRequest' @0x1496e5728 (ref at 0x141ffc84d), 'YouthPlayerManager::PortraitCaptureExecutor' @0x14b0178e8 (ref in func 0x147dee14c), 'data/ui/imgAssets/playerCapture/p%s.png' @0x149799650 (refs at 0x1428dff03, 0x1470dbe7a, 0x14800261c, 0x14800270d, 0x14800297e, 0x148002a1e, 0x148002ad9, 0x148002b79). Other related strings exist: PlayerCaptureInit, PlayerCaptureController, PlayerCaptureStream, CareerModeStateInitPlayerCaptures, CareerModeStateUpdatePlayerCaptures, DynamicPortrait, HasDynamicPortrait, IsDynamicPortraitOnDisk, DYNAMIC_PORTRAIT_DOWNLOAD_COMPLETE, playerCaptureController, captureManagerAvatar, avatar.headshot, EnablePlayerCapture, DynTifoPlayerCaptureRequest. Use find_str to locate them.
- The binary is protected (EA anti-tamper) but most functions are plain x64 MSVC code; no RTTI.

Tasks:
1. Map the portrait/player-capture pipeline: who builds a capture request (which struct, fields such as player id, size, camera, output name), how it is sent (message/event system, queue, vtable call), what renders it, and where the result ends up (texture object, UI image registry keyed by 'data/ui/imgAssets/playerCapture/p<id>.png', file on disk?).
2. Find the most practical entry point an injected DLL could call (function address + calling convention + argument layout, or a message object to construct and post) to request a capture for an arbitrary player id, and how to read the result (e.g. a registry lookup by that path, a texture handle, a callback).
3. Note anything that suggests the capture only works in certain game states (career mode, youth academy) or needs initialisation (PlayerCaptureInit).
Be concrete: list addresses, offsets, struct layouts you infer, with confidence levels and the evidence (disassembly snippets). Do not guess without saying so. Spend at most ~45 minutes. Write your full findings to /home/claude/re/notes_capture.md and return a concise summary (key addresses, the recommended call path, open questions).
```

## Brief 2: create a job offer

```text
You are doing static reverse engineering of EA SPORTS FC 27 (PC) for a user-owned modding tool (offline career mode only, no anti-cheat or online work). Goal: make it possible for an injected DLL / Lua script to CREATE A JOB OFFER for the user's manager from a chosen club in Manager Career (FC 26 Live Editor had a "Create job offer" feature).

Material (cloud container):
- /home/claude/re/fc27.bin : main module image dumped from memory (base 0x140000000; file offset = VA - base; PE headers intact; code mostly in .text1 RVA 0x1000..0x8B69000).
- /home/claude/re/rx.py helpers (sys.path.insert(0,'/home/claude/re'); from rx import *): find_str(s), rip_refs([targets]) -> {target: [(instr_addr, tail)]}, dis(addr, n), func_start(addr), cstr(addr). capstone/pefile installed.
- Relevant strings exist, e.g. 'JobMarketManager' @0x14b0201f0 (ref at 0x147f1830a), 'JobMarketManager::ProcessAITeamsMessages::AIHireManagerReply', 'HubDino::FCEGameModesFCECareerModeJobMarketManager', 'HubDino::FCEGameModesFCECareerModeJobSwitchManager', 'JobOfferAccept', 'JobOfferApplicationApply', 'JobOfferApplicationDeclined', 'JobOfferExpired', 'JobOfferSign', 'JOB_OFFER', 'JOB_OFFER_CHANCE', 'JOB_OFFER_SCHEDULE', 'JOBOFFER/JOB_OFFER_EXPIRE', 'JOBOFFER/CLUB_OFFER_RESPONSE_DAYS_MIN/MAX', 'JOBOFFER/MAX_SIMULTANEOUS_APPLICATIONS', career events ENUM ..._JOB_OFFERS_GENERATED / _JOB_OFFER_RECEIVED / _JOB_OFFER_ACCEPTED, 'CM_JobOffer_ExpiresOn'. Use find_str to locate others.
- Runtime facts already known: career managers are reached through a manager table (FCE GM comm service); the JobMarketManager and JobSwitchManager are career-mode managers (Live Editor Lua has ENUM_FCEGameModesFCECareerModeJobMarketManager etc.). Career events are posted through an events manager (Live Editor hooks "post__CareerModeEvent").

Tasks:
1. Find the JobMarketManager's code: how job offers / applications are stored (container type eastl vector/list, element struct layout: team id, wage, expiry date, objectives, state), and where the user's offers list lives relative to the manager object.
2. Find the function(s) that create/generate a job offer (e.g. on application accepted, on season end, AI hire reply) and the event posted when an offer is received (JOB_OFFER_RECEIVED) — the minimal sequence that makes the game show a new offer (email/notification + Job Offers screen).
3. Recommend the most practical way for an injected DLL to create an offer for a given team id: call a game function (address, calling convention, args) or insert a struct into the list + post the event. State risks.
Be concrete: addresses, offsets, inferred struct layouts with confidence levels and disassembly evidence. Do not present guesses as facts. Spend at most ~45 minutes. Write full findings to /home/claude/re/notes_joboffer.md and return a concise summary.
```

## Brief 3 (done, kept for reference): standings and results

The output is `docs/re/standings-fixtures-notes.md`. The original brief is not repeated here.
