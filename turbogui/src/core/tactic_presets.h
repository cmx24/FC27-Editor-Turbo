// FC 27 LE Turbo GUI - Tactic presets (Turbo 2.12): the EA game's OWN tactic and gameplay settings saved to / loaded from a Turbo JSON
// file, so that the game applies them natively. Pure code: no ImGui, no game memory, no clock (the caller passes timestamps).
//
// What a preset holds (turbo_output\tactic_presets\<name>.json, schema 1):
//   * the tactic block = what Turbo's channel E (core/team_sheet.h) reads and writes in your club's live team sheet: Defensive Approach,
//     Line Height (depth), Build-Up Style, and the role id + focus of each of the 11 slots; the formation (record name, formation id,
//     source formation id and the position of every slot) is RECORDED but never written: no writer for it is proven (team_sheet.h: the
//     preset and the formation stay refused), so a load of another formation leaves the roles alone and says so;
//   * the settings block = the sliders of the Game Settings file as core/game_settings.h reads them (the EA "Custom" gameplay block): saved
//     values of every mapped slider, loaded through the settings staging (turbo_output\settings_pending.json, only VERIFIED sliders, applied
//     with the game closed).
//   Not in a preset: width (the team sheet record and cm_mentalities carry no width the game reads: nothing to save or write), the match
//   setup and Turbo's own tuning sliders.
// Validation REFUSES (never clamps): an unknown or newer schema, another game build, a value outside its range, a slider id that is not a
// plain id. The checks that need the live game (a writable value, band-consistent approach, the role allow-list, the gates) are channel E's
// own (make_plan / the gates) at load time.
// LastLoad (turbo_output\tactic_preset_last_load.json) remembers the values before and after the latest load: Undo puts the "before" back through
// the same guarded job, and after a career load the panel offers a one-click Re-apply of the "after" values (never automatic). It is NOT
// part of reapply_edits.json: that file's rows (kits, callnames, player fields) are never touched by presets.
#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "team_sheet.h"

namespace turbo {
namespace tpreset {

constexpr int kSchema = 1;
constexpr int kSlots = tsheet::kSlots;
constexpr size_t kMaxNameLen = 64, kMaxFileBytes = 256 * 1024;
// 2.20: a note may be long (the 10-16 community slider presets carry 833..1208 characters of credits and mapping notes). Until 2.20 the
// limit was 300, so every one of those files was refused and both preset lists were empty (brief 2.20-T-tactics, cause A).
constexpr size_t kMaxNoteLen = 4000;
inline const char* dir_name() { return "tactic_presets"; }                   // <Live Editor>\turbo_output\tactic_presets
inline const char* last_load_file() { return "tactic_preset_last_load.json"; }  // turbo_output (outside the presets folder)
inline const char* build() { return tsheet::kBuild; }

struct SlotRec {
    int position = -1;   // the slot's position code (what the formation puts there): the roles load only into the same positions
    int role_group = 0;  // the role word's group (never written)
    int role_id = 0;     // 0..63
    int focus = 0;       // 0..7
};

struct TacticBlock {
    bool present = false;
    std::string record_name;       // "433"; display only
    int formation = 0, source_formation = 0;  // recorded, never written
    int line_height = 50;          // 1..100 (Line Height = the depth)
    int build_up = 0;              // 0..3 (writable: the known values only)
    int approach = 1;              // 0..3 (writable: the known values only)
    std::array<SlotRec, kSlots> slots{};
    bool same_values(const TacticBlock& o) const;  // line, build-up, approach, roles and focuses (not the formation)
};

struct SettingsBlock {
    bool present = false;
    std::string layout;            // gsettings::layout_name() when saved
    std::string map_build;         // the slider map's build ("6AC07E31")
    std::map<std::string, int> values;  // slider id -> 0..100
    // 2.12.1: Load also stages the sliders the game's own table names but the EA Custom screen has not shown yet (core/game_settings.h
    // Entry::inferred): the one-launch proof build. false = only the 3 screen-proven ones. A preset file without the key means false
    bool include_unverified = false;
};

// 2.12.1: the captain, the set-piece takers, the corner supports and the pre-set substitutions of your club's team sheet (cm_teamsheets, 24
// columns). Written by the Lua module features/tactic_setpieces.lua (guarded, read back, undo); this block is only the saved values.
struct SetPieceField {
    const char* name;   // the database column
    const char* label;  // plain language
};
const std::vector<SetPieceField>& setpiece_fields();  // the 24, in the order Edit Tactic's Set Pieces screen lists them
inline const char* setpieces_current_file() { return "tactic_setpieces_current.json"; }  // turbo_output: Lua's "get"
inline const char* setpieces_result_file() { return "tactic_setpieces_result.json"; }    // turbo_output: Lua's last set / undo / get line
struct SetPieceBlock {
    bool present = false;
    int team = 0;                       // the club the player ids belong to (a preset loads only into the same club)
    std::map<std::string, int> values;  // column -> player id (-1 = nobody), only setpiece_fields()
    bool same_values(const SetPieceBlock& o) const { return values == o.values; }
};
// "" = acceptable; else why not (a column that is not one of the 24, an id outside -1..500000, no values, a team outside 1..200000)
std::string setpiece_problem(const SetPieceBlock& s);
// One line per value that differs (now -> preset): "Penalty taker 12345 -> 23456"
std::vector<std::string> setpiece_diff(const SetPieceBlock& want, const SetPieceBlock& now);
// Reads Lua's tactic_setpieces_current.json ({"turbo_setpieces":1,"team":95,"values":{...}}); false (with err) for anything else
bool setpieces_parse_current(const std::string& text, SetPieceBlock& out, std::string* err);
// The Lua command for a Load: {"op":"run","module":"tactic_setpieces","overrides":{"mode":"set","confirm":true,"team":..,"preset":..,"values":{..}}}
// (mode "undo" without values for Undo). Returned as compact JSON text; App::send parses it.
std::string setpieces_command_json(const std::string& mode, const SetPieceBlock& want, const std::string& preset);

struct Preset {
    int schema = kSchema;
    std::string name, note, created;
    std::string game_build;        // build()
    TacticBlock tactic;
    SettingsBlock settings;
    SetPieceBlock setpieces;
    std::filesystem::path file;    // where it was read from / written to
    std::string to_json() const;
    // false (with err) for anything validate() refuses, and for text that is not a Turbo tactic preset
    static bool parse(const std::string& text, Preset& out, std::string* err);
};

// "" = the preset is acceptable; else the first reason it is refused
std::string validate(const Preset& p);

// ---- reading the game
// The tactic block of the live team sheet (the read-out of channel E)
TacticBlock block_from_live(const tsheet::LiveTactic& lt);
// A new preset of the live tactic (and the settings values, when given; empty map + settings_layout "" = no settings block)
// 2.12.1: `setpieces` (when given and with values) is stored as the set-piece block; a settings block made here has include_unverified true (this
// test build proves the table-mapped sliders on the EA screen in one launch)
Preset make_preset(const std::string& name, const std::string& note, const std::string& created, const tsheet::LiveTactic* live,
                   const std::map<std::string, int>* sliders, const std::string& layout, const std::string& map_build,
                   const SetPieceBlock* setpieces = nullptr, bool include_unverified = true);

// ---- loading: the request channel E understands
// The Request that puts `want` into the live tactic `now`: the formation is never written, so another formation (another source formation or
// other positions in the slots) is REFUSED whole ("pick the preset's formation in Edit Tactic first"). Same formation: Line Height + Defensive
// Approach (together, band-consistent: 3.0.1 the Line Height wins and the approach follows its band, with a note; a line of Aggressive's band
// 91..100 or outside 1..100 writes neither), Build-Up Style when it writes it, the role id and focus
// of every slot whose role group is unchanged. A value Turbo does not write stays as the live tactic has it, and `notes` says so (the rest loads).
// "" and the request, or why nothing can be requested.
std::string request_for(const TacticBlock& want, const tsheet::LiveTactic& now, tsheet::Request& out, std::vector<std::string>& notes);
// One line per value that differs (live -> preset): "Line Height 25 -> 90", "slot 9 role 21 / focus 5 -> role 22 / focus 6". 3.0.1 (GUI30H):
// a difference the request does not write is listed too, ending ": not written" ("Line Height 25 -> 95: not written"), never hidden
std::vector<std::string> diff_lines(const TacticBlock& want, const tsheet::LiveTactic& now);
// The written differences only (what the request changes); Re-apply's "your live tactic does not hold it now" uses these
std::vector<std::string> written_diff_lines(const TacticBlock& want, const tsheet::LiveTactic& now);
// The lines of a diff_lines result that end ": not written"
std::vector<std::string> unwritten_lines(const std::vector<std::string>& diff);
// "formation: preset 433 (source 10) / live 442 (source 12)" when the formations differ, else ""
std::string formation_note(const TacticBlock& want, const tsheet::LiveTactic& now);
// One line for the list: "433, Defensive Approach High (2), Line Height 90, Build-Up Style Counter (1); 11 roles; 3 sliders"
std::string summary(const Preset& p);
// 2.20: the reason the live tactic read failed (live_tactic_read's `why`), in plain words for the Simple view: one line that says what is
// going on and what to do. "" for an empty why. "layout mismatch ..." (e.g. "0 active tactic records") is NOT a hub problem: the line says
// Turbo cannot read this career's tactic instead of sending the player to the hub (brief 2.20-T-tactics, cause B).
std::string live_why_plain(const std::string& why);
// The store scan in one log line: "store scan: 5 listed, 0 skipped in <dir>" + "; skipped: a.json: why | b.json: why" (reasons cut to 160)
std::string scan_line(size_t listed, const std::vector<std::string>& errors, const std::string& dir);

// ---- the files
std::filesystem::path presets_dir(const std::filesystem::path& out_dir);       // <out_dir>\tactic_presets
std::filesystem::path last_load_path(const std::filesystem::path& out_dir);
std::string file_name(const Preset& p);                                         // "<safe name>.json"
// Every readable preset of the folder by name; a file that is not an acceptable preset is skipped and named in `errors` ("x.json: ...")
std::vector<Preset> load_presets(const std::filesystem::path& dir, std::vector<std::string>* errors);
std::string unique_name(const std::vector<Preset>& have, const std::string& wanted);  // "Name", "Name 2" ...
bool write_preset(const std::filesystem::path& dir, Preset& p, std::string* err);   // validates first; sets p.file; .tmp + rename
bool delete_preset(const Preset& p, std::string* err);

// ---- the latest load (Undo and Re-apply)
struct LastLoad {
    int schema = 1;
    std::string preset, when;        // preset name, "2026-10-16 14:05:00"
    std::string state;               // "pending" (submitted) "loaded" "failed" "undone" "dismissed"
    std::string note;
    int team = 0;                    // the user's club
    std::string session;             // the Lua session and career load the load was made in
    long long load_gen = -1;
    TacticBlock before, after;       // the live values before the load / the values the load asked for
    bool settings_staged = false;    // the settings block was staged (turbo_output\settings_pending.json)
    std::string settings_preset;     // the staged preset's name
    std::vector<std::string> settings_skipped;  // sliders left out (not verified)
    std::string settings_previous;   // 3.0.1: turbo_output\settings_pending.json as it was before this load staged its sliders ("" = none);
                                     // Undo puts it back (b153e48 deleted an earlier staging along with the load's own)
    bool setpieces_sent = false;                // the set-piece block was sent to Lua (mode set); Undo sends mode undo
    std::string to_json() const;
    static bool parse(const std::string& text, LastLoad& out, std::string* err);
};
bool load_last(const std::filesystem::path& p, LastLoad& out, std::string* err);  // false when absent or not a last-load file
bool save_last(const std::filesystem::path& p, const LastLoad& l, std::string* err);
// The Request that puts the "before" values back (Undo); same rules as request_for. "" and the request, else why not
std::string undo_request(const LastLoad& l, const tsheet::LiveTactic& now, tsheet::Request& out, std::vector<std::string>& notes);
// After a career load the game may hold other values than the load put there. "" = nothing to say (not loaded, another club, the values are
// in place, an undo or a dismiss happened); else the sentence the panel shows with its Re-apply / Dismiss buttons
std::string reapply_prompt(const LastLoad& l, const tsheet::LiveTactic& now, const std::string& session, long long load_gen);

}  // namespace tpreset
}  // namespace turbo
