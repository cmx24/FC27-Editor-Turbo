// FC 27 LE Turbo GUI - unlock the game's own editors (Career > Squad > Edit Player, Edit Manager, Create a Club players,
// main menu Edit Players, optionally the career hub settings). Plan: research/edit_unlock_plan.md.
//
// What greys a field out or hides it is not code: each editor screen reads one small JSON config,
// data/avatar/avatarcustomizationcfg_<screen>.json (career settings: data/gamesettings/gamesettings_context_Career.json),
// through the legacy-file service every time the screen opens. Live Editor's mods\legacy folder replaces game files.
//
//   1. The game's original files are asked for through want.txt (LegacyImages, Lua's LegacyFileExport pump) and copied,
//      with their SHA-256, to turbo_output\edit_unlock\originals\<path> before Turbo writes anything. An export whose hash
//      is one Turbo wrote, or equals the custom file now in mods\legacy, is never taken as an original.
//   2. A name-based recipe (below) is applied to the originals: isEditable / isVisible false -> true except the keep-list
//      (TEAM, PREFERRED_POSITION, BODY_TYPE, player GENDER; manager GENDER unless "Unlock everything"), sections copied
//      only from the game's own sibling files, dependency entries merged, EA's missing brace in
//      managercareer_edit_retiredreal.json repaired. Every other key is left as it is.
//   3. The result is validated (parses back, known names only, numbers in range, keep-list untouched, < 256 KB) and
//      written with LegacyImages::save_custom (tmp + rename; an older custom file is backed up to
//      turbo_output\edit_unlock\backups).
//   4. turbo_output\edit_unlock\manifest.json lists the originals and every file Turbo wrote (with hashes). Restore
//      removes exactly those files, and only while they still hold what Turbo wrote.
// Never written, whatever the options: *_online, managerlive_*, playercareer_*, clubs*, tournament_* or any file that is
// not one of the eight targets.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/legacy.h"
#include "nlohmann/json.hpp"

namespace turbo {
namespace eu {

using ojson = nlohmann::ordered_json;

constexpr int kRecipeVersion = 1;
constexpr size_t kMaxOutputBytes = 256 * 1024;

enum class Group { CareerPlayers, CreatedPlayers, Manager, MainMenu, CareerSettings, Source };

struct FileSpec {
    std::string path;   // legacy path ("data/avatar/...")
    std::string label;  // which screen reads it ("Career > Squad > Edit Player")
    Group group;        // Source = only read (sections and definitions are copied from it), never written
};
// Every file the recipe reads or writes: the 8 targets first, then the 4 read-only sources
const std::vector<FileSpec>& files();
const FileSpec* find_file(const std::string& path);
std::string avatar_path(const std::string& screen);  // data/avatar/avatarcustomizationcfg_<screen>.json
extern const char* const kCareerSettingsPath;        // data/gamesettings/gamesettings_context_Career.json
extern const char* const kCareerSetupPath;           // data/gamesettings/gamesettings_context_Career_Setup.json

// Only the 8 targets, and never an online / Manager Live / Player Career / Clubs / tournament file
bool write_allowed(const std::string& path);

struct Options {
    bool career_players = true;    // Career > Squad > Edit Player
    bool created_players = true;   // Create a Club > squad > edit player
    bool manager = true;           // Edit Manager (created and real managers), career start with a real manager
    bool main_menu = true;         // main menu Customise > Edit Players
    bool career_settings = false;  // advanced: the career hub settings EA locks mid-career
    bool experimental = false;     // "Unlock everything": head editor, Composure / Defensive awareness, manager outfits,
                                   // all celebrations, manager gender, squad settings in the hub
    std::set<std::string> files_off;  // per-file switches (Details): targets not to write
    bool group_on(Group g) const;
    bool file_on(const std::string& path) const;  // a target whose group is on and that is not switched off
    std::string signature() const;                // recorded in the manifest
    nlohmann::json to_json() const;
    static Options from_json(const nlohmann::json& j);  // missing keys keep the defaults above
};

// Read-only files a target needs (stage 1 and, with experimental, the experiments')
std::vector<std::string> sources_for(const std::string& target, const Options& opt);

std::string sha256_hex(const std::string& bytes);

// Parse one of the game's config files (UTF-8 BOM allowed). On failure only the known repair is tried: an entry of a
// dependency tree written without its opening brace ("}," then "parents" -> insert "{"). *repaired says it was needed.
bool parse(const std::string& text, ojson& out, bool* repaired, std::string* err);
// The known repair alone (exposed for the tests); returns the text unchanged when there is nothing to repair
std::string repair_missing_brace(const std::string& text);

// FC 27's avatar-config vocabulary (categories, fields, dependency names: 120 names)
bool known_name(const std::string& name);
// Fields the unlock never touches. Players: TEAM (a move outside the transfer engine), GENDER, PREFERRED_POSITION (set
// by ROLE), BODY_TYPE. Managers: GENDER stays unless manager_gender.
std::set<std::string> keep_list(const std::string& target, const Options& opt);

struct RecipeResult {
    bool ok = false;
    std::string text;                // the file to write
    std::vector<std::string> notes;  // "15 fields unlocked", "Attributes section added", ...
    std::string error;               // why not (ok = false)
    bool repaired = false;           // EA's missing brace was put back
};
// originals: legacy path -> the game's original text (targets and sources at hand)
RecipeResult build(const std::string& target, const std::map<std::string, std::string>& originals, const Options& opt);

// Checks of a recipe output against the target's original (and the names any original at hand uses). "" = fine.
std::string validate(const std::string& target, const std::string& out_text, const ojson& original,
                     const std::set<std::string>& extra_known, const std::set<std::string>& keep);

class EditUnlock {
public:
    enum class State { Off, Waiting, Missing, Original, Unlocked, Restored, Kept, Failed };
    struct FileStatus {
        State state = State::Off;
        std::string note;
    };

    EditUnlock(LegacyImages& legacy, std::filesystem::path le_root);

    std::filesystem::path dir() const;            // turbo_output\edit_unlock
    std::filesystem::path originals_dir() const;  // turbo_output\edit_unlock\originals
    std::filesystem::path manifest_path() const;  // turbo_output\edit_unlock\manifest.json

    // Look for the originals the options need: new exports are copied to originals\ (with their hash) before anything
    // is written; files not exported yet are asked for (want.txt). Returns the lines worth logging (new original,
    // refused export, title update).
    std::vector<std::string> collect(const Options& opt);
    // Every original the enabled targets need is saved (or the game does not have it)
    bool ready(const Options& opt) const;
    bool has_original(const std::string& path) const;
    // Build, validate and write every enabled target that is ready; a target switched off that Turbo wrote is
    // restored. Returns one summary line.
    std::string apply(const Options& opt);
    // Remove exactly the files the manifest lists (only while they still hold what Turbo wrote). Summary line.
    std::string restore();

    FileStatus status(const std::string& path) const;
    static const char* state_name(State s);
    size_t written_count() const;
    bool wrote(const std::string& path) const;
    std::string manifest_error() const { return manifest_error_; }
    // A hash of the originals at hand plus the options: a change means apply() has new work
    std::string input_signature(const Options& opt) const;

private:
    bool load_manifest();
    bool save_manifest();
    bool restore_one(const std::string& path);
    std::string read_original(const std::string& path) const;
    std::string file_sha(const std::filesystem::path& p);  // cached by size and write time

    LegacyImages& legacy_;
    std::filesystem::path root_;
    nlohmann::json manifest_;
    std::string manifest_error_;
    bool manifest_unreadable_ = false;
    std::map<std::string, FileStatus> status_;
    std::map<std::string, std::string> source_state_;  // path -> "waiting" / "missing" while no original is saved
    struct ShaCache {
        uintmax_t size = 0;
        std::filesystem::file_time_type mtime{};
        std::string sha;
    };
    std::map<std::string, ShaCache> sha_cache_;
};

}  // namespace eu
}  // namespace turbo
