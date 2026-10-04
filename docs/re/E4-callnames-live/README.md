# Live reads of the commentary bank structures (track E4, 2026-10-03/04)

Checkpoint of the dev-service reads behind docs/callnames.md sections 5 and 6, taken in the career hub (Manager Career
turbo04, Italian pack ita_it, FC27.exe build 6AB9813C-211EF000). Heap addresses are only valid for that game session;
the static ones (image base 0x140000000) stay valid for the build.

* `row_runs_2026-10-04.json` - every run of 64-byte selection rows with a surname-range value found by
  `scripts/re/callnames_bank.py`-style finds (0x0D/0x0E id bytes) in 0x3D4000000..0x3DC000000: 138,802 rows, 159 runs.
* `tables_live_2026-10-04.json` - per-run statistics (true start through the value -1 sentinel row, rows, distinct
  values, min/max, array count before the sentinel), when the analysis finished before the game restart.
* `table_3d74d4190.json` - one whole table read back: 1,762 rows, 888 distinct values 855181..856234 (two rows per
  value; not surname ids: a club/other name family), preceded by a sentinel row with value 0xFFFFFFFF and index 0.
* `registry_events_2026-10-04.json` - the 10,201 named commentary events of the registry with their ids.
* `lang_events.json`, `handlers.json`, `selector.json`, `db.json` - the pointer chain SpeechSystem -> registry ->
  PLAYER_NAME_FE / PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK node -> handler -> owner -> language db (empty in the hub),
  the selector object ([SpeechSystem+0x58], vtable 0x14AB5CB20, slot 0xd8 = 0x141a8a2fc) and the base CommentaryDb.

Follow-up (same date, after the 00:47 restart): the audio-service chain the build in `core/commentary_audio.h` walks was
read back in the hub (registry 0x35CD11B0 -> service 0x683819A0 -> names 0x683F0920 -> inner 0x67388FC0 -> audio
0x82D5A4E0 -> CommentaryBridge 0x82D5A630), docs/callnames.md section 5.4; the 18 signatures are in
docs/re/E4-callnames-signatures.json (scripts/re/verify_callnames.py).
