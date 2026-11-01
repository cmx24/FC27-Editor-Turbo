// FC 27 LE Turbo GUI - the "Game settings" tab: the game's own settings file (Settings > Game Settings: match, player, the User and CPU
// sliders) and presets of its values (core/game_settings.h).
//
// The overlay runs inside FC 27, and the game keeps its settings in memory and saves them to a NEW file name: so this tab never writes
// the settings file. It reads the one Settings<timestamp> file (first look and Refresh only) and lists every slider of the slider map
// with its value now. 5.0 sliders: a VERIFIED slider with a Custom index is a real slider; releasing it writes that one value into the
// running game's Custom block (gsettings::live, the same path as Load; NOT proven in game). Turbo X sliders (2026-11-01): a FILE-MAPPED
// Custom slider (proven by Settings files, not yet seen on the EA screen of this build) is a slider whose release only queues the value in
// turbo_output\settings_pending.json for turbo_settings_apply --apply (game closed: CRC, backup, --undo). Other rows: "read-only" / "not mapped".
// Presets live in turbo_output\settings_presets (Save current, Load = the live write of the preset, Rename, Delete, Duplicate, Import,
// Export). This tab stages nothing for the apply tool any more; game_settings_stage / _unstage below stay for the tactics pages.
// It never touches CmMgrC* (careers), Squads* or ProfileOptions*.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace turbo {

class App;

struct GameSettingsViewState {
    bool drawn = false;
    std::string file_line;   // "Settings20261014213137933: Personal Settings 1, layout fbchunks-454-bc650, CRC ok, md5 ..." / why not
    bool file_ok = false;
    std::string map_line;    // "slider_map_6AC07E31.json: 85 sliders, 0 verified" / why not
    bool closed_note = false;  // the "close and relaunch, never CmMgrC*" note was drawn
    struct Row {
        std::string id, label, group, value, state;  // value: "50" / "-" (none, or not mapped); state: "verified" / "file-mapped" / "read-only" / "not mapped"
    };
    std::vector<Row> rows;
    int slots = 0;                     // candidate slots listed (read-only)
    std::vector<std::string> presets;  // names
    std::string preset_line;           // the last Save / Load / Rename / Delete / Duplicate / Import / Export outcome
    std::string live_line;             // 5.0 sliders: the last slider write's outcome ("<label> = N written ..." / "Not applied: <why>"; "" = none)
    std::string pending_line;          // always "" since Turbo X (2026-10-31): this page shows no staging; kept for the tests that read it
    std::vector<std::string> preset_errors;
    int file_reads = 0;                // how often the settings file was read (first look, Refresh, after Save)
    bool file_note = false;            // Turbo X sliders: the one-line "file-mapped rows write the Settings file" note was drawn
    int queued = 0;                    // Turbo X sliders: values queued for the Settings file by this page (preset kFileQueueName)
};
const GameSettingsViewState& game_settings_view_state();

void draw_game_settings(App& app);
// Tests: forget the view's state; the settings folder (empty = %LOCALAPPDATA%\EA SPORTS FC 27\settings) and the slider map file
// (empty = <Live Editor>\turbo\settings\slider_map_6AC07E31.json)
void game_settings_reset();
void game_settings_set_paths(const std::filesystem::path& settings_dir, const std::filesystem::path& map_file);

// 2.12 tactic presets (ui_tactic_presets.cpp): the same slider map, settings file read and staging as this tab (nothing else writes)
// Reads the settings file now: every mapped slider's value (0..100), the file's layout. false (with why) when the map or the file is not usable
bool game_settings_current(App& app, std::map<std::string, int>& values, std::string& layout, std::string& map_build, std::string& why);
// What staging `values` would do: how many are verified sliders (written), the others in `skipped`. false (with why) without a slider map
// 2.12.1: include_unverified also counts the sliders the game's table maps but the EA screen has not shown (they are in `unverified_n` of the total)
bool game_settings_plan(App& app, const std::map<std::string, int>& values, int& writable, std::vector<std::string>& skipped, std::string& why,
                        bool include_unverified = false, int* unverified_n = nullptr);
// Stages the values of `name` (the VERIFIED sliders only) like this tab's Load: turbo_output\settings_pending.json, applied with FC 27 closed.
// `staged` gets the count, `skipped` the sliders left out; false (with msg) when nothing could be staged
bool game_settings_stage(App& app, const std::string& name, const std::map<std::string, int>& values, std::string& msg, int& staged,
                         std::vector<std::string>& skipped, bool include_unverified = false);
// Removes turbo_output\settings_pending.json when it is the staged preset `name`'s (before the apply tool ran); msg says what happened.
// 3.0.1: `previous` (the file's text before `name` was staged, "" = none) is written back after the removal when it is a staging file
bool game_settings_unstage(App& app, const std::string& name, std::string& msg, const std::string& previous = std::string());

}  // namespace turbo
