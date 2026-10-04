# FC 27 commentary speech bank on disk: formats, the extraction tool, what it gives

Track "commentary bank" (2026-10-04). Game build 1.0.140.64835 (`__Installer\installerdata.xml`), Italian pack
`ita_it` (`<game>\commentary\commentaryfull_ita_it.toc` + `commentaryfull_ita_it\cas_01.cas`, `cas_02.cas`, and the
`commentarylaunch_ita_it` pair). Everything below was read from those files with
`turbo/tools/fc27_commentary/fc27_commentary.py`; the game was not touched. Names of hashed fields were resolved by
hashing the strings of the game's memory image (`turbo_output\fc27_image.bin`) with the speech system's hash.

This replaces the "Parsing the bank on disk" item of `docs/callnames.md` §3 (dropped there as out of scope): the whole
chain parses, and the result is the same table FIFA Editor Tool's "Export Data Set" gave for FC 26.

## 0. Result

```
python turbo/tools/fc27_commentary/fc27_commentary.py --lang ita_it --out "C:\FC 27 Live Editor\turbo_dev\masters\raw\ita_it_bank.json" --wav "C:\FC 27 Live Editor\turbo_dev\masters\raw\ita_it_wav"
python turbo/tools/fc27_commentary/fc27_commentary.py --self-test        (synthetic checks + the ita_it facts below)
python turbo/tools/fc27_commentary/fc27_commentary.py --lang ita_it --list   (the 3,329 soundwave families)
```

The data takes 0.3 s; with `--wav` the 9,263 segments are decoded in ~45 s (ffmpeg on PATH or `--ffmpeg`).

| family | Selection rows | unique (segment, variation, id) | ids | segments |
|---|---|---|---|---|
| `pSIMPLE_SURNAME` (generic, key `surname_ID` = commentary id) | 6,886 | 3,443 | 2,533 commentary ids (900002..999952) | 3,443 |
| `pPLAYER_NAMES_SIMPLE` (real, key `player_db_pID` = player id) | 13,756 | 6,878 | 4,039 player ids (27..999999143) | 5,820 |
| `pPLAYER_NAMES_LINK` (real, LINK event) | 3,424 | 1,712 | 985 player ids (978 also in SIMPLE) | 1,599 |

`ita_it_bank.json`: `{"turbo_bank": 1, "lang", "game_build", "source_files", "generic": [{"segment", "variation",
"commentaryid", "intensity", "cm_sim"}…], "real": [{"segment", "variation", "playerid", "intensity", "cm_sim"}…],
"real_link": […same as real…], "wav_dir", "notes"}`. `intensity` / `cm_sim` are the `player_intensity` / `cm_sim`
values the row is selected for (every row of the three families exists for `cm_sim` 0 and 1, hence the halved counts).
Wavs: `ita_it_wav\generic\pSIMPLE_SURNAME_<seg>_<seg>.wav`, `ita_it_wav\real\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav`,
48 kHz 16-bit mono PCM, one per segment, the same naming as the FC 26 exports.

### 0.1 Cross-checks (all pass)

* Generic, against the FC 26 Italian master (`C:\FC_Tools\My Mods\ita\italy_master.xlsm`): Bianchi 900762 = segments
  931 / 1419 / 1420 with variations 1562356 / 1574681 / 1574682 (identical to FC 26); Pirlo 926385 = 1696 / 4876753
  (identical); Del Piero 922149 = segment 2106 (FC 26: 2107), variation 5772903 (identical). Over the whole master:
  2,113 of the 2,114 FC 26 generic rows have their id in FC 27, all 2,113 with the same VariationId, 1,967 also with the
  same SegmentID. FC 27 has 2,533 generic ids against 1,202 in the FC 26 master.
* Real: Lobotka 216435 = segment 271 (FC 26: 1084), variation 1605821 (identical); Rrahmani 244263 = segments
  2426 / 2427 (FC 26: 3637 / 3638), variations 6055688 / 6055689 (identical); **Gutierrez 261865 = segments 5041 / 5042
  in `pPLAYER_NAMES_SIMPLE`** (his own recording, as heard in game). Real segment numbers are renumbered in FC 27; the
  VariationId is the stable key (5,147 of the 6,976 FC 26 real rows keep their VariationId; 3,498 FC 26 real player ids
  are still in FC 27).
* Audio: the decoded FC 27 segments have exactly the frame count of the FC 26 wavs of the same variation (generic 931:
  30,703; 1419: 39,368; 1696: 23,925; 2106 vs FC 26 2107: 29,852; real 271 vs FC 26 1084: 39,306; 2426 vs 3637:
  35,490), RMS within 0.3 %, cross-correlation 0.993 / 0.9997 at a 2-sample lag (decoder difference, inaudible).
  Every one of the 9,263 decoded segments has the sample count its stream header gives.
* The game's own answers (Turbo's audio-service build, `masters\raw\spoken_ita_it_inmatch.json`, 2,462 surnames, 751
  players): every one of the 2,462 surnames is a bank generic id with `player_intensity` 2, and every generic id with
  intensity 2 that the build asked about was answered "yes" (2,462 = 2,462). The 50 ids the bank has only at intensity 1
  (900863, 900918, 901515, …) were asked and answered "no" (Turbo asks with `player_intensity = 2`, like the game's
  Create Player list). 21 intensity-2 ids (999931..999952) are not in the database's `commentarynames`, so they were not
  asked. Players: all 751 answered players are in `pPLAYER_NAMES_LINK` (747 also in SIMPLE); see §6 for what this says
  about Turbo's SIMPLE query.

## 1. toc (`commentaryfull_<lang>.toc`, `commentarylaunch_<lang>.toc`)

* `00 D1 CE 01`, a signature block, data from **0x22C**. The `Data\Win32` copies of the two ita_it tocs are identical
  install stubs; the real ones are under `commentary\`.
* Header at 0x22C: 15 big-endian u32, offsets relative to 0x22C:

| # | ita_it full | meaning |
|---|---|---|
| 0 | 0x3C | header size (= bundle hash map offset) |
| 1 | 0x50 | bundle table |
| 2 | 4 | bundle count |
| 3 | 0x90 | chunk hash map (perfect hash, not needed) |
| 4 | 0x40CC | chunk GUID table: 20-byte entries, GUID **byte-reversed** + u32 (0x80000000 flag, low 24 bits = entry index × 4) |
| 5 | 4,111 | chunk count |
| 6 | 0x181F8 | chunk entries, 16 bytes: cas id (8 bytes), u32 offset, u32 size (also #7, #9) |
| 8 | 0x282E8 | 0xAEC bytes that look obfuscated, then the bundle data: bundle names were not decoded (not needed) |
| 10 | 0x403C | chunk hash map size (4 × count) |

* Bundle table entry (16 bytes): u32 name offset (unused), u32 size (0x40000000 = inline), u64 offset. Inline bundle:
  9 big-endian u32 (`0, 0, flag table offset, count, 0x24, 0x24, 0x24, 0, count`), then the location table from
  0x24: per entry, if its flag byte is 0x84 an 8-byte cas id first, then u32 offset + u32 size; the flag table (one byte
  per entry, only 0x84 and 0x00 seen) follows the locations exactly.
* **cas id** (8 bytes): bytes 2..5 = the install package, the last 2 = the cas file number. ita_it full = `906EA4AE`
  (`commentaryfull_ita_it\cas_NN.cas`), launch = `D8633AE0`. 5 chunks of the full toc (2 of the launch toc) point into a
  base-game package `A3A00DE1` (cas 3, 4, 8: another folder); none of the commentary families uses them.
* ita_it full: 4 bundles - `ita_it_demo` (681 soundwaves), `ita_it_full` (3,329 soundwaves: the match bank),
  `ita_it_commentary_brt` (1 EBX + 1 res, type 0x428EC9D4), `ita_it_critical` (101); chunk entries: 3,027 in cas_01,
  1,079 in cas_02. Launch: `ita_it_launch_commentary_brt`, `ita_it_full_penta` (294 soundwaves), `ita_it_penta_commentary_brt`.

## 2. Bundle manifest and blocks

* Location 0 of a bundle is its manifest, **stored as is** (not block-compressed): u32 BE size, u32 LE magic
  **0x9D798ED6**, 7 × u32 LE (total, ebx, res, chunk counts, strings offset from +4, meta offset, meta size), sha1 × total
  (20 bytes), ebx (name offset, size), res (name offset, size), res types (u32), res meta (16 bytes), res ids (u64),
  chunks (GUID, offset, size). Locations 1.. follow the order ebx, res, chunks. Names are plain
  (`sound/speech/loccommentary/ita_it/soundwaves/ita_it_full/psimple_surname`).
* Every other location is a Frostbite block stream: u32 BE decompressed size, u16 LE code (low 7 bits = type, bits
  8..11 = compressed size bits 16..19, the rest 0x70), u16 BE compressed size. Type **0x19 = Oodle** (Leviathan, header
  `8C 0C`; `OodleLZ_Decompress` from the game's `oo2core_9_win64.dll`, loaded with ctypes), **0x00 = stored** (the audio:
  256 KB blocks `00 04 00 00 00 74 00 00` - the "0x0070..0x0074 blocks" of `docs/callnames.md` §3).
* A soundwave family = one EBX (`RIFF … EBX\0 EBXD … EFIX … EBXX`, ~550 bytes, holds the chunk GUID and size) + one res
  of type **0xB2C465F6** (`SBle`, the data below) + one audio chunk. The EBX type info was not needed: the res is
  self-describing, and the chunk GUID is found by matching every 16 bytes of the EBX against the toc's chunk table.

## 3. `SBle` (the family's data sets)

Little-endian. `SBle`, u32 size, u16 data set count (4), …, +0x18 pointer to the data set pointer array. **Pointers**
are 8 bytes: u32 offset from the start of the resource + u32 offset of the next pointer to fix up (a relocation chain).
Names are hashed with **djb2-xor** (`h = 5381; h = h * 33 ^ c`, the same hash `docs/callnames.md` §5.1 found for the
speech query parameters).

DSET: `TESD`, +0x04 header size, +0x08 name hash, +0x0C type hash, +0x38 rows, +0x3C u16 fields, +0x3E u16 indexes,
+0x40 u16 field table, +0x42 u16 index table, +0x44 u32 parameter table (offsets from the DSET).

* field (0x18 bytes): hash, flags (bits 24..31 = value width in bytes, low byte 2 = int, 5 = float; no data pointer =
  constant), base, 0, data pointer. Value = (base + stored) mod 2^32; a float field is that taken as float bits.
* index (0x20 bytes): pointer to a **prefix-sum array** (u16 while rows < 65,536), 16 zero bytes, u32, u32 (bits 24..31 =
  parameter count, low byte = first parameter). Keys run over the product of the parameters' value sets in parameter
  order, the first parameter outermost; the rows of key k are `[prefix[k], prefix[k+1])` - the rows are sorted by key.
* parameter (0x18 bytes): hash, flags (low 24 bits = value count, bits 24..31 = width of the sorted value list), min,
  max, value list pointer (values = min + stored; no list = every value min..max).

The four data sets of a family (names resolved from the image strings unless marked):

| DSET | rows (surname / player names) | fields | meaning |
|---|---|---|---|
| `Selection` 0x11D5B185 | 6,886 / 13,756 | `0x6AC4E4EA` (u16, FET's **SegmentID**; name not found in the image), `VariationId` 0xF5F914D9 (u32 + base 1,560,343 / 1,560,724) | indexes over (`cm_sim`, `surname_ID` 0xDE127DE4 / `player_db_pID` 0x73D7AD2D, `player_intensity` 0xA896C286), (`cm_sim`, id), (`cm_sim`) |
| `Variations` 0xA29AF127 | 3,443 / 5,820 | `VariationId`, `SegmentCount` (constant 1), `StreamChunkIndex`, 6 more constants | index over `0x6AC4E4EA` 0..rows-1: the Selection's SegmentID is the row of this table |
| `Segments` 0x3FE2AFD5 | 3,443 / 5,820 | `Duration` 0x6CFCCE5B (float, s), `0xE8E591DD` = byte offset of the stream in the chunk + 3 (low 2 bits set), `0xD506D74E` constant 0 (chunk index) | index over `0xD06D5A58` 0..rows-1 |
| `Chunks` 0xA28D4A0D | 1 | `ChunkSize` 0xDC19107B (10,376,916 for psimple_surname = the chunk's logical size), `ChunkId` | index over `ChunkIndex` |

With `SegmentCount` = 1 everywhere and as many Segments as Variations, SegmentID = variation row = segment row; the
tool checks that each Selection row's VariationId equals `Variations[SegmentID].VariationId` ("Variations check: ok").
The resource ends with a small table: the chunk GUID, the three selector hashes, the EBX partition GUID, two type hashes.

## 4. Audio

* One chunk per family (psimple_surname: GUID `6f1fc52a-…-8102b130`, cas_01 at 214,548,227, 40 stored 256 KB blocks).
* Each segment is an **EA SNS stream** at its offset (4-byte aligned): `48 00 00 0C` + SNR header (u32 BE: version 1,
  codec 14 = EA Opus, channel config 0 = mono, 48,000 Hz; u32 BE: type 1 = streamed, sample count), then `44` blocks
  (u24 BE size, u32 BE samples, **one Opus packet**: TOC 0x78 = hybrid full-band 20 ms, 960 samples), then a `45`
  block of size 4 or 5. The first block's sample count is 960 − 312: the 312-sample encoder delay = the Ogg pre-skip;
  the last block's count trims the end.
* `--wav`: the packets are wrapped in an Ogg Opus stream (RFC 7845, pure Python, Ogg CRC) and decoded by ffmpeg to
  s16le, written with `wave`. Without ffmpeg the tool writes the `.opus` files instead (any player opens them).

## 5. Tool layout and rules

`turbo/tools/fc27_commentary/fc27_commentary.py`: Python 3.11 standard library + ctypes, read-only on the game folder,
Oodle loaded from the game folder at run time (never copied). Every layer checks what it reads (magics, counts,
location table length, prefix sums cover every row exactly once, value lists sorted, chunk size = `ChunkSize`, sample
counts = header) and stops with `error: …` instead of guessing. Only downloaded packs (`<game>\commentary\…`) are
handled: the base game's `eng_us` toc lives in `Data\Win32` with its cas data in the install packages (not mapped).

## 6. Findings for Turbo, next steps

* **The spoken set can come from the bank.** `generic` ids with intensity 2 = exactly what the game's audio service
  answers (§0.1). A list made from `ita_it_bank.json` (`scripts/callnames_spoken_list.py`-style, or read directly) works
  without waiting for a screen that binds the bank, and covers the 21 ids the database does not list.
* **Turbo's `PLAYER_LOW_SIMPLE` query answers nothing.** The build asked 21,340 players; 3,213 database players have
  rows in `pPLAYER_NAMES_SIMPLE` (Gutierrez among them) but no player came back with bit 1. The LINK query works (751
  answered, all in `pPLAYER_NAMES_LINK`; 68 more LINK ids in `ids.json` - low ids such as 27, 41, 250, 330 - were not
  answered, possibly not among the 21,340 checked). Both families are keyed the same way (`cm_sim`, `player_db_pID`,
  `player_intensity`), so the difference is in how the event `PLAYER_LOW_SIMPLE` reaches its family, not in the bank.
  Next step: follow that event's candidates in the language db during a match (`docs/callnames.md` §5.1) and, until
  then, take the real players from the bank (`real` ∪ `real_link`).
* 362 real segments are shared by several player ids (e.g. segment 1459: 51, 239598, 239599, 246487 - the same person
  under several ids).
* Other languages: run the tool with `--lang` once the pack is downloaded; nothing in the code is Italian-specific
  except the self-test's facts.
