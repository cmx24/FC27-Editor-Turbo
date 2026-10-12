// FC 27 LE Turbo GUI - game asset catalog: every asset the game ships (3D heads, minifaces, gear, crests, kits, balls,
// callname recordings), the proof behind each id, and readable names for ids no database row names.
//
// Contract: docs/asset-catalog.md (schema v1 "turbo-asset-catalog/1"). The catalog is built offline by
// turbo/tools/asset_catalog/build_asset_catalog.py into <Live Editor>\turbo_output\asset_catalog\catalog_<buildkey>.json;
// it holds real names and is never committed or shipped. The GUI loads it on a worker thread and computes "Hidden"
// live against the loaded save (DbUse below): the catalog itself never stores what the save uses.
//
// Platform independent: no ImGui, no Windows calls. A Catalog is filled once by load_*() and then only read, so the GUI
// loads into a fresh object on a worker thread and swaps it in (no locking inside).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace turbo::assets {

constexpr const char* kSchema = "turbo-asset-catalog/1";
constexpr int kLuaGateVersion = 1;

// One entry per key of "families" in the schema (family_key() gives the JSON key)
enum class Family : uint8_t {
    Head3d,       // head3d: 3D heads (EBX), key = headassetid; vars, toc, generic
    Miniface,     // miniface: data/ui/imgAssets/heads/p<id>.dds
    Youthface,    // youthface: data/ui/imgAssets/youthheads/p<id>.dds
    Staffface,    // staffface: data/ui/imgAssets/heads_staff/heads_staff_<id>.dds
    Legendface,   // legendface: legend pictures
    Hair,         // hair: hairtypecode (model + preview)
    Facialhair,   // facialhair: facialhairtypecode
    Boots,        // boots: shoetypecode
    Gkglove,      // gkglove: gkglovetypecode
    Tattoo,       // tattoo: tattoo* fields
    Accessory,    // accessory: accessorycode1..4 (mapping unknown: no gate)
    Eyebrow,      // eyebrow: eyebrowcode (cranium preset)
    Skinsurface,  // skinsurface: skinsurfacepack (cranium preset)
    Crest,        // crest: teamid, exact path per id
    Kit,          // kit: teamid -> folder slug + kit folders
    Ball,         // ball: ball id (model + preview)
    Callnames,    // callnames: per language and commentary team, bank recordings
    Count
};
constexpr size_t kFamilyCount = static_cast<size_t>(Family::Count);

const char* family_key(Family f);                         // "head3d", "miniface", ... ("" for Count)
bool family_from_key(const std::string& key, Family& out);  // inverse of family_key
// Families whose entries are { "model": [...], "preview": {id: path} } (gear-like); the others use "ids"
bool family_has_models(Family f);

// What proves an id (bits; an id can have several)
enum Proof : uint8_t {
    kProofNone = 0,
    kProofModel = 1,      // 3D model / EBX (head3d, gear "model", cranium presets)
    kProofPreview = 2,    // legacy file listed in the game's ChunkFileCollector manifests
    kProofRecording = 4,  // commentary bank recording
    kProofModelTs = 8,    // boots: a 3D model only under content_ts ("model_ts_only"): listed, never writable until checked in game
};

// Callname recording kinds in a bank (schema: callnames.<lang>.teams.<team>.<kind>)
enum class CallKind : uint8_t { Generic, Simple, Link, High, Count };
const char* call_kind_key(CallKind k);  // "generic", "simple", "link", "high"

// The three name tables of the schema ("names.player", "names.team", "names.commentary")
enum class NameKind : uint8_t { Player, Team, Commentary };

struct Name {
    std::string text;    // "" when the catalog has no name (fallback() gives "Head #id" etc.)
    std::string source;  // "master:<lang>", "workbook", "extra_names", "slug", "kit_slug", "managers", "kind",
                         // "loc_strings", "le_team_names", "turbo_team_names"; "" when unnamed
};
// "name from: ita_it master" / "name from: game file name" / ... for tooltips; "" for an empty source
std::string name_source_label(const std::string& source);
// Fallback label for an unnamed id: "Head #41", "Youth face #461618", "Callname #272258", "Team #2", ...
std::string fallback_name(Family f, int64_t id);

// ---- load state, switches, status
enum class State : uint8_t {
    NotLoaded,  // nothing tried yet (or no catalog file at all)
    Off,        // asset_catalog_off.txt: every picker behaves as in 2.0.x
    Current,    // catalog_<running key>.json loaded
    Stale,      // only a catalog for another build: lists work, writes of hidden ids are refused
    Error,      // a file was found but refused (bad JSON, wrong schema); error() says why
};
const char* state_name(State s);  // "not loaded", "off", "current", "stale", "error"

// Kill switches in turbo_output\ (same convention as reapply_off.txt): the file existing switches the feature off
struct Switches {
    bool catalog_off = false;  // asset_catalog_off.txt: catalog not loaded, lua_gate.txt removed
    bool hidden_off = false;   // asset_hidden_off.txt: catalog loaded (galleries cleaned, gates on), hidden ids not offered
};
Switches read_switches(const std::filesystem::path& turbo_output);

// File locations under <Live Editor>\turbo_output
std::filesystem::path catalog_dir(const std::filesystem::path& turbo_output);  // turbo_output\asset_catalog
std::filesystem::path catalog_file(const std::filesystem::path& turbo_output, const std::string& build_key);
std::filesystem::path catalog_off_path(const std::filesystem::path& turbo_output);  // asset_catalog_off.txt
std::filesystem::path hidden_off_path(const std::filesystem::path& turbo_output);   // asset_hidden_off.txt
std::filesystem::path lua_gate_path(const std::filesystem::path& turbo_output);     // asset_catalog\lua_gate.txt
std::filesystem::path write_log_path(const std::filesystem::path& turbo_output);    // asset_writes.log
// The rebuild command shown in the stale banner, the gates' refusals and Tools > Status. The builder lives in the Turbo
// source repository: run it in a checkout with Python 3 (numpy makes the first run fast); it builds missing or outdated
// commentary banks itself. Release builds ship no builder yet (WP-14), so release users cannot build a catalog.
std::string rebuild_command();  // the bare command line, for the clipboard
std::string rebuild_where();    // one sentence: where to run it, and that release builds cannot
// Build key of an FC27.exe file on disk, read from its PE header ("6AC07E31-2145C000", the format of sigscan's
// build_key); "" when the file cannot be read or is not a PE image. The GUI prefers the running game's key (host).
std::string exe_build_key(const std::filesystem::path& exe);
// Cheap fingerprint of what load_dir() would see (every catalog_*.json name, size and time, and both switch files);
// the GUI polls it and reloads on a change (a rebuilt catalog, a switch file added or removed)
std::string dir_signature(const std::filesystem::path& turbo_output);

// ---- what the loaded save uses (section 3.6), rebuilt by the GUI when model.version() changes
struct DbUse {
    // Per family: every id some current row has (head3d: players.headassetid of any class + manager.headassetid;
    // miniface: players.playerid + players.headassetid; staffface: manager.headassetid; youthface: players.headassetid;
    // gear: the matching players.*code; tattoo: all 7 players.tattoo* fields; crest: teams.teamid; kit:
    // teamkits.teamtechid; callnames: players.playerid)
    std::array<std::unordered_set<int64_t>, kFamilyCount> used;
    std::unordered_set<int64_t> commentary_ids;  // commentarynames.commentaryid + playernamemap (generic callnames)
    std::unordered_set<int64_t> tattoo_table;    // ids in the DB tattoo table ("not in the tattoo table" note)
    bool has_tattoo_table = false;               // false: the table was not read, no note is shown
    uint64_t model_version = 0;                  // model.version() these sets were built from
};

// ---- the asset_writes.log helper (section 4.1): every write of a hidden id, so it can be reverted by hand
struct WriteLogEntry {
    std::string target;  // "player 12345", "manager 7", "team 2"
    std::string field;   // "headassetid"
    std::string old_value, new_value;
    std::string catalog_key;  // build key of the catalog that allowed the write
};
// "<local time ISO>\t<target>\t<field>\t<old>\t<new>\t<catalog key>" appended to write_log_path(); false + err on failure
bool append_write_log(const std::filesystem::path& turbo_output, const WriteLogEntry& e, std::string* err = nullptr);

struct Status {
    State state = State::NotLoaded;
    std::string file;          // the loaded (or refused) catalog file
    std::string build_key;     // the catalog's build key
    std::string running_key;   // the running game's build key ("" when unknown)
    std::string game_version;  // installerdata gameVersion recorded by the builder
    std::string built_at;      // ISO time of the build
    std::string error;         // State::Error / NotLoaded detail
    bool hidden_off = false;   // asset_hidden_off.txt present
    std::array<size_t, kFamilyCount> counts{};  // ids per family (callnames: distinct ids over every bank)
    double load_ms = 0.0;      // read + parse + index time of the last load_file
    std::string note;          // non-fatal remarks ("ignored unknown families: outfit")
};

// ---- the catalog
class Catalog {
public:
    Catalog();
    ~Catalog();
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    // Loads one file. running_key "" = unknown (treated as stale). Refuses bad JSON and other schemas (state Error).
    bool load_file(const std::filesystem::path& file, const std::string& running_key, std::string* err = nullptr);
    // Start-up rule (3.4): switches first; then catalog_<running_key>.json, else the newest catalog_*.json as stale
    bool load_dir(const std::filesystem::path& turbo_output, const std::string& running_key, std::string* err = nullptr);

    const Status& status() const;
    State state() const;
    bool loaded() const;   // Current or Stale (ids can be listed)
    bool current() const;  // Current: hidden ids may be written
    bool hidden_off() const;
    // One line for Tools > Status: "current - build 6AC07E31-2145C000, built 2026-10-12, 6141 heads, ...",
    // "stale - built for <key>, running <key>: rebuild with <command>", "off (asset_catalog_off.txt)", "not loaded"
    std::string status_line() const;

    // ---- ids and proofs
    uint8_t proof(Family f, int64_t id) const;  // Proof bits, kProofNone when the game does not ship the id
    bool has(Family f, int64_t id) const;       // proof != none
    bool has_model(Family f, int64_t id) const;
    // Boots: a 3D model only under content_ts (no content/ model): listed with its own badge, refused by every gate
    bool model_ts_only(Family f, int64_t id) const;
    // Accessories: the game folder's description of the id ("wrist_athletic_tape"), "" when the catalog has none
    std::string desc(Family f, int64_t id) const;
    std::vector<int64_t> ids(Family f) const;   // sorted; every id with any proof (Kit/Crest: team ids)
    // Exact preview path as listed in the manifests (letter case kept), "" when there is none
    std::string preview_path(Family f, int64_t id) const;

    // ---- head3d extras
    std::vector<int> head_vars(int64_t headassetid) const;  // shipped variations, sorted ({} when none / not a head)
    std::string head_toc(int64_t headassetid) const;        // "L" launch, "S" contentsb, "LS" both ... ("" when unknown)
    bool generic_head(int64_t headassetid) const;

    // ---- kit extras
    std::string kit_slug(int64_t teamid) const;  // "aston_villa" ("" when the team has no kit folder)
    std::vector<std::string> kits(int64_t teamid) const;

    // ---- callnames
    std::vector<std::string> callname_languages() const;                      // "eng_us", "ita_it", ...
    std::vector<std::string> callname_teams(const std::string& lang) const;   // "dr", "gm" ...; "" for single-team packs
    std::vector<int64_t> callname_ids(const std::string& lang, const std::string& team, CallKind k) const;
    // Is there a recording of the id in that bank (any kind)? A voice source / kick-off generic id needs one.
    bool has_recording(const std::string& lang, const std::string& team, int64_t id) const;

    // ---- names (3.5): the builder's first source; the GUI puts the live DB name in front
    Name name(NameKind k, int64_t id) const;

    // ---- write gate (4.1). True when `id` may be written to a field of family `f`:
    //  - the catalog is Current (a stale or missing catalog refuses: `why` says so);
    //  - head3d: the head has a model; gear (hair, facialhair, boots, gkglove, tattoo): a 3D model (preview-only and
    //    content_ts-only boots refused; tattoos the save's table lacks are refused by the galleries, core/gear_ids.h);
    //    eyebrow / skinsurface: listed; 0 = none is always writable for gear codes;
    //  - accessory: no gate yet (always true while loaded).
    bool writable(Family f, int64_t id, std::string* why = nullptr) const;

    // ---- hidden (3.6): the catalog ships the id and no current row uses it. False when hidden_off() or not loaded.
    bool hidden(Family f, int64_t id, const DbUse& use) const;
    // May a picker list the id at all? The save uses it, or the catalog ships it and asset_hidden_off.txt is absent.
    bool offered(Family f, int64_t id, const DbUse& use) const;
    bool hidden_generic_callname(int64_t commentaryid, const DbUse& use) const;
    bool not_in_tattoo_table(int64_t tattooid, const DbUse& use) const;

    // ---- Lua gate file (lua_gate.txt): "#turbo-asset-gate 1", "#build <key>", "#session <id>" (when given: the GUI
    // session written into bridge_dll.json; Lua trusts a gate only with its own session), then one "head3d <id>" line per
    // id. Text is "" unless the catalog is Current and not switched off.
    std::string lua_gate_text(const std::string& session = std::string()) const;
    // Writes lua_gate.txt when lua_gate_text() is not empty, else deletes it. False + err when the file system refuses.
    bool sync_lua_gate(const std::filesystem::path& turbo_output, std::string* err = nullptr,
                       const std::string& session = std::string()) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Scope filter of every asset picker (All / In this save / Hidden), remembered per picker by the GUI
enum class Scope : uint8_t { All, InSave, Hidden };
const char* scope_label(Scope s);              // "All", "In this save", "Hidden"
bool scope_shows(Scope s, bool hidden);        // does a row with this hidden flag pass the filter?

}  // namespace turbo::assets
