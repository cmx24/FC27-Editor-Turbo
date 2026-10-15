// FC 27 LE Turbo GUI - faces3d: per-player generic 3D hair / beard for scanned heads (Players > Appearance, 2.8 picker v1).
//
// What it does: the user enrols scanned players (HeadClass 0) with a generic hair and/or beard model id; Apply writes
// turbo_output\faces3d\players.json, generates turbo_output\lua_override\player_kit.lua and writes faces3d_on.txt. At the next
// game start the Lua loader hook (core/lua_loader.h) serves that override instead of the stock player_kit.lua when its first
// line `-- TURBO build <key> stock <md5> table <hash>` names the running build and the stock it captured.
//
// The override is built like turbo_dev/research/c2_lua_loader/build_gated_v2.py (ported byte-for-byte; the F2 launch showed its
// output in game): the stock source is read at build time from the hook's capture cache (turbo_output/faces3d/
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
#include <string>
#include <vector>

namespace turbo::faces3d {

// One enrolled player. hair / beard = generic model id (0 = none: the scanned one stays). hair_colour: hair, eyebrow and
// facial-hair colours from the DB for a hair entry (hc); beard_colour: the generic beard takes the DB facial-hair colours (bc).
struct Entry {
    int64_t hair = 0, beard = 0;
    bool hair_colour = true, beard_colour = true;
    std::string name;  // comment in the generated Lua only (sanitised there)
};
using Table = std::map<int64_t, Entry>;  // playerid -> entry, sorted

constexpr size_t kMaxEntries = 100;

// players.json: {"<playerid>": {"hair": <int>, "beard": <int>, "hair_colour": <bool>, "beard_colour": <bool>, "colours": <bool>,
// "name": "<text>"}}. "colours" (default true) is the default of both colour flags (research format). Strict: unknown fields,
// non-integer / < 1 model ids, non-boolean flags, keys that are not a positive player id (no leading zero) are refused.
bool parse_players_json(const std::string& bytes, Table& out, std::string* err);
// The file Turbo writes (sorted, one entry per line, LF, explicit flags; parse_players_json reads it back to the same Table)
std::string players_json(const Table& t);

// ---- verified ids (faces3d_data.h: mesh + colour + coeff exist for <id>_1_0)
const std::vector<int>& ok_hair_ids();
const std::vector<int>& ok_beard_ids();
bool hair_id_ok(int64_t id);
bool beard_id_ok(int64_t id);

// Product rules for Apply: at most kMaxEntries entries, each with a hair or a beard, every id verified, every player scanned
// (headclass(playerid) == 0; -1 = not in the DB). Returns one message per problem (empty = ok).
std::vector<std::string> validate(const Table& t, const std::function<int64_t(int64_t playerid)>& headclass);

// ---- the generator
const char* template_text();  // the embedded TEMPLATE v2 (LF)
const char* template_md5();
std::string template_stock_md5(const std::string& tmpl);  // the template's @stock_md5 ("" when unreadable)

struct Built {
    bool ok = false;
    std::string bytes;  // generate_body: the body; generate_override: header line + body
    std::string md5;    // of bytes
    std::string error;  // why !ok (nothing must be written then)
};
// The gated player_kit.lua body from stock + table (build_gated_v2.py build()): refuses on a template error, the stock md5, any
// anchor line md5, a column out of range, or a bad entry. Entries with neither hair nor beard are left out.
Built generate_body(const std::string& tmpl, const std::string& stock, const Table& t);
// table field of the header: first 12 hex digits of md5(players.json bytes as written)
std::string table_hash(const std::string& players_json_bytes);
// `-- TURBO build <build> stock <md5(stock)> table <table_hash>\r\n` + body (CRLF header line, as the F1b/F2 overrides)
Built generate_override(const std::string& tmpl, const std::string& stock, const Table& t, const std::string& build_key,
                        const std::string& players_json_bytes);

// ---- files (relative to turbo_output)
extern const char* const kPlayersRel;   // "faces3d\\players.json"
extern const char* const kOverrideRel;  // "lua_override\\player_kit.lua" (the lua_loader row's override)
extern const char* const kOptInRel;     // "faces3d_on.txt"

bool load_players(const std::string& turbo_output_utf8, Table& out, std::string* err);  // missing file = empty table, true

struct ApplyResult {
    bool ok = false;
    std::string message;  // what was written, or why nothing was
    std::string override_md5, table;
};
// validate (when headclass is set) -> players.json -> override from the stock cache -> faces3d_on.txt, each atomic; any failure
// before the opt-in leaves faces3d_on.txt untouched. An empty table = remove(). build_key "" refuses (game build unknown).
ApplyResult apply(const std::string& turbo_output_utf8, const Table& t, const std::string& build_key,
                  const std::function<int64_t(int64_t)>& headclass);
// Deletes the override and faces3d_on.txt (players.json is kept, so the choices come back)
bool remove(const std::string& turbo_output_utf8, std::string* err);

}  // namespace turbo::faces3d
