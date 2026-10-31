// FC 27 LE Turbo GUI - faces3d: per-player generic 3D hair / beard for scanned heads (Players > Appearance, 2.8 picker v1).
//
// What it does: the user enrols scanned players (HeadClass 0) with a generic hair and/or beard model id; Apply writes
// turbo_output\faces3d\players.json, generates turbo_output\lua_override\player_kit.lua and writes faces3d_on.txt. At the next
// game start the Lua loader hook (core/lua_loader.h) serves that override instead of the stock player_kit.lua when its first
// line `-- TURBO build <key> stock <md5> table <hash>` names the running build and the stock it captured.
//
// The override is built like turbo_dev/research/c2_lua_loader/build_gated_v2.py for a list of fixed entries (ported byte-for-byte;
// the F2 launch showed its output in game) or build_gated_v3.py when any entry follows the DB (ported byte-for-byte; the F5 launch
// showed in game that the 3D capture runs the gate and that a DB write of the hair / beard codes shows in the next capture): the stock source is read at build time from the hook's capture cache (turbo_output/faces3d/
// stock_player_kit.cache) and never stored by Turbo; the TEMPLATE (core/faces3d_data.h, generated from
// player_kit_template_v2.lua) holds only Turbo's text plus anchors (stock line number + md5 of that stock line + byte column).
// Every anchor line's md5 and the whole-file md5 are checked before anything is written; any mismatch refuses.
//
// Layers (kept apart so a later live-preview panel can reuse the model and the generator without the files):
//   Table / parse_players_json / players_json   the model and its file format
//   validate                                    product rules (verified ids, HeadClass 0, entry cap) - the GUI path
//   generate_body / generate_override           pure text transform (template + stock + table)
//   apply / remove                              the files in turbo_output
// Platform independent (no ImGui, no Win32).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/lua_loader.h"

namespace turbo::faces3d {

// One enrolled player. hair / beard = generic model id (0 = none: the scanned one stays). hair_colour: hair, eyebrow and
// facial-hair colours from the DB for a hair entry (hc); beard_colour: the generic beard takes the DB facial-hair colours (bc).
// follow (gate v3 "follow"): the ids come per call from the player's DB codes (initData.Hair / FacialHairType) when the code
// is >= 1, HairStyle == 0 and the id is verified; else the part's fallback (0 = none: that part stays stock). hair / beard are
// then NOT written to players.json nor into the gate: in memory they are the picker's choice for a live preview / Keep.
struct Entry {
    int64_t hair = 0, beard = 0;
    bool hair_colour = true, beard_colour = true;
    std::string name;  // comment in the generated Lua only (sanitised there)
    bool follow = false;
    bool follow_hair = true, follow_beard = true;   // "parts" (follow only; at least one)
    int64_t hair_fallback = 0, beard_fallback = 0;  // follow only; verified ids of a followed part
};
using Table = std::map<int64_t, Entry>;  // playerid -> entry, sorted

constexpr size_t kMaxEntries = 100;

// players.json: {"<playerid>": {"mode": "fixed" | "follow" (default fixed), "name": "<text>", "colours": <bool>,
//   "hair_colour": <bool>, "beard_colour": <bool>,
//   fixed : "hair": <int>, "beard": <int>
//   follow: "parts": ["hair", "beard"] (default both), "hair_fallback": <int>, "beard_fallback": <int>}}
// (research\c2_lua_loader\build_gated_v3.py). "colours" (default true) is the default of both colour flags. Strict: unknown fields
// for the mode, non-integer / < 1 model ids, non-boolean flags, keys that are not a positive player id (no leading zero), bad
// parts, a fallback for a part not followed or not verified are refused.
bool parse_players_json(const std::string& bytes, Table& out, std::string* err);
// The file Turbo writes (sorted, one entry per line, LF, explicit flags; parse_players_json reads it back to the same Table, except
// a follow entry's in-memory hair / beard, which are not written). A table of fixed entries is written exactly as 2.8 did.
std::string players_json(const Table& t);

// ---- verified ids (faces3d_data.h: mesh + colour + coeff exist for <id>_1_0)
const std::vector<int>& ok_hair_ids();
const std::vector<int>& ok_beard_ids();
bool hair_id_ok(int64_t id);
bool beard_id_ok(int64_t id);

// Product rules for Apply: at most kMaxEntries entries, each fixed one with a hair or a beard, a follow one with at least one part
// and verified fallbacks of followed parts only, every id verified, every player scanned
// (headclass(playerid) == 0; -1 = not in the DB). Returns one message per problem (empty = ok).
std::vector<std::string> validate(const Table& t, const std::function<int64_t(int64_t playerid)>& headclass);

// ---- the generator
const char* template_text();  // the embedded TEMPLATE v2 (LF)
const char* template_md5();
const char* template_v3_text();  // the embedded TEMPLATE v3 (LF): gate block with follow entries + the verified id ranges
const char* template_v3_md5();
const char* template_v4_text();  // the embedded TEMPLATE v4 (LF): v3 + the live band rule (2.16), splices identical to v3
const char* template_v4_md5();
// 5.0c E13: TEMPLATE v5 (LF) = v4 + the clean-scan splices (a band / entry hair or beard on a real face, HeadClass 0, replaces
// the old hair / beard painted into the scan: _loadStarHead composites the scan colour / normal / specmask with generic skin under
// the stock region masks). Served live unless turbo_output/faces3d/hair_replace_off.txt exists (kHairReplaceOffRel).
const char* template_v5_text();
const char* template_v5_md5();
bool has_follow(const Table& t);
// What apply builds from: always v4 since 2.16 (v3 + the band rule, so looks change without a restart). v2 / v3 stay for tests
const char* template_for(const Table& t);

// ---- live looks (2.16, template v4): a reserved band in the two DB codes the game's 3D model build reads per call
// A real face (HeadClass 0, HairStyle 0) whose hairtypecode is kHairBandBase + i takes generic hair band_hair_ids()[i - 1], whose
// facialhairtypecode is kBeardBandBase + i takes generic beard band_beard_ids()[i - 1]; for players and managers alike, with no
// list and no restart. EA's own data never uses the bands (base tables: hairtypecode max 3022, facialhairtypecode max 304; both
// fields are range-checked to 10000 / 512). The lists are FROZEN and append-only (the index is what the save holds).
constexpr int64_t kHairBandBase = 8039;   // hairtypecode 8040 .. 8039 + 1371
constexpr int64_t kBeardBandBase = 335;   // facialhairtypecode 336 .. 335 + 105
// "His own, live" (9999 / 511): no generic part and the saved list (players.json) is skipped for him, so a look of the saved list is
// dropped without a restart. No model id (hair ids end at 8039, beard ids at 335), on no allow-list, in no EA row; inside 10000 / 512.
// Like a band code it is ignored by the game's own file on a real face.
constexpr int64_t kHairOwnCode = 9999, kBeardOwnCode = 511;
// preview_backup.json key of a manager's own codes: kManagerBackupKey + managerid (player ids stay far below)
constexpr int64_t kManagerBackupKey = 1000000000000LL;
// A Turbo code of that field (a band code or the "his own, live" code): never recorded as his own, replaced when he gets a new head
bool is_turbo_hair_code(int64_t hairtypecode);
bool is_turbo_beard_code(int64_t facialhairtypecode);
// The in-memory player_kit.lua of 2.16 (the hook calls exactly this at the boot capture): template v4 over the captured stock with the
// saved list. nullptr + why when refused (stock md5, an anchor); a saved list the generator refuses falls back to the bands alone
// (note "saved list refused (...): bands only"), else note = "<N> players on the saved list".
// 2.20 K2 (crest2d): with a non-empty shirt-crest list the build also carries the crest splices (see CrestList); a list the build
// refuses never costs the faces: the build is redone without it (note "... shirt-crest list refused (...)"). A non-empty list adds
// "; <N> clubs on the shirt-crest list" to note.
// 5.0c E13: hair_replace = true builds template v5 (the hook passes it unless kHairReplaceOffRel exists); false (the default) = v4,
// byte for byte as before. A v5 build the generator refuses falls back to v4 (note "...; hair replace refused (...): v4").
using CrestList = std::set<int64_t>;
std::shared_ptr<const std::string> build_live_override(const std::string& stock, const Table& saved, std::string& note, std::string& why,
                                                       const CrestList& crest = CrestList{}, bool hair_replace = false);
extern const char* const kHairReplaceOffRel;  // "faces3d\\hair_replace_off.txt" under turbo_output: present = serve v4 (no hair replace)
// false when kHairReplaceOffRel exists under turbo_output (read once at the hook install, with players.json, never on a game thread)
bool hair_replace_on(const std::string& turbo_output_utf8);
const std::vector<int>& band_hair_ids();
const std::vector<int>& band_beard_ids();
int64_t hair_band_code(int64_t hair_id);    // 0 when the id is not on the list
int64_t beard_band_code(int64_t beard_id);  // 0 when the id is not on the list
int64_t hair_from_code(int64_t hairtypecode);          // the generic hair a band code selects, 0 = not a band code
int64_t beard_from_code(int64_t facialhairtypecode);   // the generic beard a band code selects, 0 = not a band code
// The two fields a look writes (never hairstylecode: stock uses it in a real face's own asset names)
extern const char* const kLookFields[2];  // hairtypecode, facialhairtypecode
// What applying `hair` / `beard` (generic ids, 0 = his own) writes given his current codes and his recorded originals (field ->
// value; only fields that change). A part set to his own leaves the band: his recorded original when it is not a band code, else 0
// (a real face ignores that code; it only matters if the head ever becomes generic).
std::map<std::string, int64_t> look_writes(int64_t hair, int64_t beard, const std::map<std::string, int64_t>& current,
                                           const std::map<std::string, int64_t>& original);
// Can a look change show now (this game start)? st = the hook state (nullptr: no game hooks)
struct LiveLooks {
    bool active = false;
    bool restart_needed = false;  // the hook or the right file is not running yet: one game restart turns it on
    std::string why;              // plain words for the panel
};
LiveLooks live_looks(const lualoader::Faces3dStatus* st);
std::string template_stock_md5(const std::string& tmpl);  // the template's @stock_md5 ("" when unreadable)

struct Built {
    bool ok = false;
    std::string bytes;  // generate_body: the body; generate_override: header line + body
    std::string md5;    // of bytes
    std::string error;  // why !ok (nothing must be written then)
};
// The gated player_kit.lua body from stock + table (build_gated_v2.py / build_gated_v3.py build(), chosen by the template's
// markers: v3 has --@@OKHAIR@@ and --@@OKBEARD@@): refuses on a template error, the stock md5, any anchor line md5, a column out
// of range, or a bad entry (a follow entry with a v2 template too). Fixed entries with neither hair nor beard are left out.
// crest (2.20 K2): the clubs whose jersey and shorts crest comes from their 2D crest file (crest/<dark|light>/l<teamid>.dds,
// the stock generic-kit branch of _loadTeamCrest). Empty = not one byte changes. Non-empty = the Turbo-owned crest splices, anchored
// on stock 9399e39a line md5s like the template's: a global TURBO_CREST2D table before _loadTeamCrest (no new main-chunk local) and
// the listed test on its is_generic_kit line (player path: rawget TeamId; crowd path: kitData has no TeamId, its Kit when a number).
Built generate_body(const std::string& tmpl, const std::string& stock, const Table& t, const CrestList& crest = CrestList{});
// table field of the header: first 12 hex digits of md5(players.json bytes as written)
std::string table_hash(const std::string& players_json_bytes);
// `-- TURBO build <build> stock <md5(stock)> table <table_hash>\r\n` + body (CRLF header line, as the F1b/F2 overrides)
Built generate_override(const std::string& tmpl, const std::string& stock, const Table& t, const std::string& build_key,
                        const std::string& players_json_bytes, const CrestList& crest = CrestList{});

// ---- files (relative to turbo_output)
extern const char* const kPlayersRel;   // "faces3d\\players.json"
extern const char* const kOverrideRel;  // "lua_override\\player_kit.lua" (the lua_loader row's override)
extern const char* const kOptInRel;     // "faces3d_on.txt"

extern const char* const kAppliedDirRel;     // "faces3d\applied": players.json as applied, as <table>.json (what a game start runs)
extern const char* const kPreviewBackupRel;  // "faces3d\preview_backup.json"

bool load_players(const std::string& turbo_output_utf8, Table& out, std::string* err);  // missing file = empty table, true

// ---- 2.20 K2: the shirt-crest list, turbo_output\kits\crest2d.json = {"teams": [<teamid>, ...]} (global: every career and Kick Off;
// the game reads it at its next start, when Turbo builds player_kit.lua)
extern const char* const kCrest2DRel;  // "kits\\crest2d.json"
constexpr size_t kMaxCrestTeams = 4000;
bool parse_crest2d_json(const std::string& bytes, CrestList& out, std::string* err);
std::string crest2d_json(const CrestList& c);  // {"teams": [..]} sorted, one line, "\n"
bool load_crest2d(const std::string& turbo_output_utf8, CrestList& out, std::string* err);  // missing file = empty list, true
// atomic; an empty list deletes the file (no file = no crest change, as before 2.20)
bool save_crest2d(const std::string& turbo_output_utf8, const CrestList& c, std::string* err);
// The boot read (win/lua_loader_win.cpp, right after players.json, on the loader's own thread, never a game thread): a missing
// file = an empty list and no log line; an unreadable one = an empty list and ONE log line in log_line. Never touches the saved
// faces list (players.json is read on its own, before this).
CrestList load_crest2d_for_boot(const std::string& turbo_output_utf8, std::string& log_line);

// ---- the list the running game compiled (the gate is compiled once per game start; later Applies do not reach it)
// The entries of a generated override's gate block (fixed or follow, colour flags, fallbacks); false when there is no gate block
bool parse_gate_entries(const std::string& override_bytes, Table& out);
// The list of the game start whose override header carried `header_table`: faces3d\applied\<table>.json (its md5 must match),
// else the override on disk when its header still names that table. false (and why) when neither tells.
bool running_table(const std::string& turbo_output_utf8, const std::string& header_table, Table& out, std::string* why);

// Can the live preview work for `playerid` this session? st = the hook state (nullptr: no game hooks), running = the list of this
// game start (nullptr: unknown). active only when the override is served (substituted, right build and stock, no kill switch, no
// mods\legacy file) and he follows the DB in that list; `why` says what is missing, in words for the panel.
struct LiveGate {
    bool active = false;
    bool member = false;  // in the list of this game start
    bool follow_hair = false, follow_beard = false;
    std::string why;
};
LiveGate live_gate(const lualoader::Faces3dStatus* st, const Table* running, int64_t playerid);

// ---- preview backup (crash safety): the DB codes a preview replaced, written before the first DB write
struct PreviewRecord {
    std::string name;
    std::map<std::string, int64_t> original;  // field -> value before the first preview
    std::map<std::string, int64_t> written;   // field -> value the last preview wrote
};
using PreviewBackup = std::map<int64_t, PreviewRecord>;
bool load_preview_backup(const std::string& turbo_output_utf8, PreviewBackup& out, std::string* err);  // missing = empty, true
bool save_preview_backup(const std::string& turbo_output_utf8, const PreviewBackup& b, std::string* err);  // atomic; empty = delete
// The fields a preview may write, in order: hairtypecode, facialhairtypecode, hairstylecode
extern const char* const kPreviewFields[3];
// What a preview of hair / beard writes given his current codes (field -> value): hairtypecode = hair (> 0), facialhairtypecode =
// beard (> 0), hairstylecode = 0 when a part is chosen and it is not 0 (the follow gate takes the DB codes only with HairStyle == 0;
// that hairstylecode is initData.HairStyle is an inference). Only fields that change.
std::map<std::string, int64_t> preview_writes(int64_t hair, int64_t beard, const std::map<std::string, int64_t>& current);

struct ApplyResult {
    bool ok = false;
    std::string message;  // what was written, or why nothing was
    std::string override_md5, table;
};
// validate (when headclass is set) -> players.json -> override (template v4) from the stock cache -> faces3d_on.txt, each atomic;
// any failure before the opt-in leaves faces3d_on.txt untouched. An empty table still writes the override (since 2.16 the live
// band rule needs it; remove() turns the feature off). build_key "" refuses (game build unknown).
ApplyResult apply(const std::string& turbo_output_utf8, const Table& t, const std::string& build_key,
                  const std::function<int64_t(int64_t)>& headclass);
// Deletes the override and faces3d_on.txt (players.json is kept, so the choices come back)
bool remove(const std::string& turbo_output_utf8, std::string* err);
// The override on disk holds the live band rule (template v4)
bool override_has_live_looks(const std::string& turbo_output_utf8);

}  // namespace turbo::faces3d
