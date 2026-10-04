# Live team names: where FC 27 gets a club's shown name (build 6AB9813C-211EF000)

Static analysis of `turbo_output\fc27_image.bin` (FC27.exe 1.0.140.64835, base `0x140000000`, dumped with Live Editor
27.1.2 loaded). Nothing was run against the game. Signatures: `docs/re/team_names-signatures.json` (all unique,
`scripts/re/check_signatures.py`). Code: `core/teamname_override.*`, `win/teamname_override_win.cpp`, `ui/ui_identity.cpp`.
Disassembled again for this note: steps 3 to 6 below, the vtable slots `0x250` / `0x290` / `0x218` of both vtables,
the 11 direct callers of Lookup, the 12 Live Editor patch sites and every signature. The view-model addresses under
"Caching" come from the first research pass and were not re-checked.

## The chain

Every screen gets a club's name from the localization service, never from `teams.teamname`:

1. Keys: `"TeamName_%d"` (`0x14A948668`), `"TeamName_Abbr3_%d"`, `"TeamName_Abbr10_%d"`, `"TeamName_Abbr15_%d"`,
   `"TeamName%s_%d"` with `_Abbr3` / `_Abbr10` / `_Abbr15` (GetTeamName), `"TeamName_Abbr%d_%d"` (the match overlay).
   The users `sprintf` the key and call the wrapper below. These are the keys of Live Editor's `custom_team_names.csv`.
2. The service: global `0x14C283340` (written once at `0x14255B390`), a `LocImpl` (vtables `0x14AB8AE38` and
   `0x149740820`, identical slots).
3. `0x14073E9D4` `LocalizeString(loc, eastl::string* out, const char* key, int mode)`: `call [vtbl+0x250]` with
   variant -1 (4,357 call sites).
4. Slot `0x250` = `0x1421E201C`: when `this+0x3B` (IWL) is set it tries `"IWL_<key>"` first, then the key; calls Lookup
   into a temporary and copies it to `out`.
5. **`0x1421E2120` `int LocImpl::Lookup(this, eastl::string* out, const char* key, int mode)`: the hook point.**
   Prologue `mov [rsp+8],rbx; mov [rsp+10h],rsi; ... mov r12d,r9d; mov r15,r8; mov rbx,rdx; mov rdi,rcx` (the
   signature covers the argument registers). Mode 0 appends `"_upper"` to the key (and, when that is missing and the
   game variable `LOCALIZE_UPPER_STRINGS` is 1, retries the plain key). It enters the critical section `0x14D249CB8`,
   asks the update table (`this+0x30`, when `this+0x39`) and the main table (`this+0x28`) through StrTab::GetString,
   copies the hit into `out` (`0x1406C20BC`), and on a miss sets `out = "*"` (`0x1406C04D4` at `0x1421E223A`) + key.
   Returns 1 when found, else the table's status (0 / 2 / 3); `"NOLOCB:" + key` when no table is loaded. 11 direct
   callers (slot `0x250` twice, the rest other slots of the same object).
6. Slot `0x290` = `0x142394D44` `GetTeamName(this, out, int teamId, int abbr, int keepCase)`: abbr 0 = `_Abbr3`,
   1 = `_Abbr10`, 2 = `_Abbr15`, other = full; first an ini redirect map at `this+0x88` (`TEAM_IDS` & co.,
   `0x142394E78`), else the wrapper with mode 1 (so Lookup), then `ToUpper` (slot `0x218`) unless keepCase.
7. `0x140B1C034` `StrTab::GetString(table, out, key)`: hash (`0x140B1C5EC`: CRC-32 of each byte `& 0xDF`, so
   case-insensitive) then `GetStringByHash` `0x140B1C6B8` (the T3DB dynamic strings, then the static chunks).

`eastl::string` here: 0x18 bytes, 15-byte inline buffer, byte `+0xF` = inline length or `0x80` for heap
(heap pointer `+0`, size `+8`), `+0x10` the allocator name. Turbo writes `out` only through the game's own
`assign(const char*)` `0x1406C04D4`, which Lookup itself uses on that object.

## Live Editor

Live Editor hooks exactly one function on this path: **StrTab::GetString `0x140B1C034`** (`FF 25 ...` `jmp [rip]`
through a slot page near the exe, residue `.. .. 66 90`). It is the only Live Editor hook on the localization path
and it reads the same keys as `custom_team_names.csv`, which it loads once at start: that is why a rename needed a
restart. (Inferred from the image and Live Editor's own Lua test `lua\libs\v2\tests\game_loc_test.lua`; Live Editor's
DLL was not read.) Lookup, LocalizeString, the wrapper, GetTeamName, GetStringByHash and the hash function carry their
original prologues.

All Live Editor patch sites in the image: **12**, not the 4 `game_thread.md` found with its pointer scan (which cannot
see `jmp [rip]` slots outside the exe): 10 `jmp [rip]` (`0x140B1C034`, `0x1417BC630`, `0x14199EE80`, `0x14199F08C`,
`0x1419B8800`, `0x141CCDF74` ViewManager::ProcessAction, `0x142B24E08`, `0x147B70518`, `0x147B8C0D8`, `0x147F2E998`)
and 2 23-byte stubs (`0x146A1D1A0`, `0x146A1FFC8`). Checked byte by byte on 04-10-2026.

## The hook (Turbo 1.1.1)

`team_names` on `loc_lookup`, ABOVE Live Editor: the original runs first (the game, and Live Editor's detour inside
GetString, answer as before), then for a `[IWL_]TeamName[_Abbr15|_Abbr10|_Abbr3]_<id>` key of a club in Turbo's table
`out` = Turbo's text (upper case for mode 0) through `0x1406C04D4`, return 1. The key is parsed before the call (it is
the caller's string and could live in `out`). Lock-free: an atomic pointer to an immutable table, old tables kept.
Never hooked: `0x140B1C034` (Live Editor's jump would be relocated; it switches between the jump and the stub from
session to session) and anything below it (Live Editor may answer its CSV keys without calling down).

Install guards: the three signatures (`loc_lookup`, `eastl_string_assign_cstr`, `loc_lookup_out_assign`), the guard's
call target equal to the assign and its site inside Lookup (`+0x110`), and no foreign inline hook on Lookup's entry.

## Caching (what a rename reaches at once)

Lookup caches nothing (the tables are searched on every call, the dynamic strings re-queried), so the next lookup on
any thread gets the new name, the main menu included. View models localize once when they are built and keep the text
(`0x1484CE748` stores `"TeamName_%d"` in its view at `+0xD8`; the hub header `0x1484B8914`; fixture, news and table
lists when the list is built; the scoreboard Abbr3 at match load through
`OverlayFifaTranslatorFunctions::GetTeamNameFromDBID` `0x143EBD5F0`). So a screen opened after Save shows the new name;
the screen already open behind Turbo's window shows it when it is built again (leave it and come back). Nothing
re-localizes every frame.

Not used: the message types `Localization.DataUpdated` (name getter `0x149358A48`) and `Localization.LanguageChanged`
exist, but no listener was found, so posting one is not known to be safe.

## Gaps

- Clubs listed in the `TEAM_IDS` ini redirect of GetTeamName are localized under another key; a second hook on
  `loc_get_team_name` (by team id and abbreviation, after the call) would cover them. Not needed for any club seen so far.
- Strings the game formats with arguments (`0x142561A60` & co.) call GetString directly, but team names reach them
  already localized.

## Long name (checked 04-10-2026 for 1.1.4)

FC 27 has no localization key for a club's long or official name. Every `TeamName` string format in the image is one of
the four above (`TeamName_%d`, `TeamName_Abbr3_%d`, `TeamName_Abbr10_%d`, `TeamName_Abbr15_%d`, plus `TeamName%s_%d` and
`TeamName_Abbr%d_%d` building the same keys); no `TeamName_Long`, `TeamNameLong`, `TeamFullName`, `OfficialName` or
`_Full_` key exists (`scripts/re/strings_grep.py "TeamName|FullName|OfficialName|ClubName|LongName"`). "Full" in the
UI names (`TeamNameFull`, `mTeamNameFull`, `teamNameFull`, the debug line `TeamName-LOCFull`) is `TeamName_%d` itself,
next to `TeamName15` / `TeamName10` / `TeamName3` (Abbr15 / Abbr10 / Abbr3). The `teams` table has only `teamname`.
So the Name form's long name is kept in `turbo_output\team_names.json` (`"long"`) only; the game never shows it.
