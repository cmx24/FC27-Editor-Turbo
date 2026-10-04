// FC 27 LE Turbo GUI - career settings unlock (Turbo 1.1.1, advanced, off by default).
//
// In a running Manager Career the hub's Settings screen greys 32 settings. That is data, not code: the game reads
// data/gamesettings/gamesettings_context_Career.json, context FROM_CAREER_MANAGER_HUB, where each of them carries
// "alwaysLocked": true (research/edit_unlock_plan.md sections 2 and 3). Live Editor's mods\legacy folder replaces game
// files, so Turbo writes the game's own file with the flag removed from 27 of them:
//   match setup (quick sim, sim match, tactical view, highlights, restart), training (energy recovery, drills,
//   development rates), transfers and scouting, board expectations, international job offers, manager market,
//   unexpected events, pitch wear, points deduction.
// These stay locked (kKeepLocked): CAREER_COMPETITION, CAREER_CURRENCY, CAREER_DEEPER_SIMULATION,
// CAREER_FINANCIAL_TAKEOVER, CAREER_YOUTH_ACADEMY. The other contexts (player career, pre-game, tournaments) are left
// exactly as they are. Experiment X6 (opt-in): the CAREER_SQUAD category (edit injuries, edit suspensions, release
// players), which exists only in Career_Setup.json, is copied into the hub context.
// EA locks these because they change the simulation mid-career (development, transfers, board): the GUI says so.
//
// Delivery (minimal internal adapter, see Store): the plan's edit_unlock module (export / original + hash / manifest /
// write / restore) is not in this base, so Store does the same steps for this one file and can be swapped for
// edit_unlock's API later: the game's original comes from the existing LegacyFileExport pump (LegacyImages::want ->
// turbo_output\cache\legacy\...), is kept in turbo_output\edit_unlock\original\<path> before any override is written,
// the override is written and removed with LegacyImages::save_custom / remove_custom (tmp + rename, previous file
// backed up), and turbo_output\edit_unlock\career_settings.json records the hashes (FNV-1a 64): an export equal to
// what Turbo wrote is never taken as an original, and Restore leaves a file it did not write alone.
// Nothing of EA's is shipped: the override is made on the user's PC from his own export.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace turbo {

class LegacyImages;

namespace csu {

constexpr const char* kPath = "data/gamesettings/gamesettings_context_Career.json";
constexpr const char* kSetupPath = "data/gamesettings/gamesettings_context_Career_Setup.json";
constexpr const char* kHubContext = "FROM_CAREER_MANAGER_HUB";
constexpr const char* kSquadCategory = "CAREER_SQUAD";
constexpr int kRecipeVersion = 1;
constexpr size_t kMaxOutput = 256 * 1024;

// The settings that stay locked in the hub
const std::vector<std::string>& keep_locked();

struct RecipeOptions {
    bool squad_settings = false;  // X6: copy CAREER_SQUAD from Career_Setup.json into the hub context
};

struct RecipeReport {
    bool ok = false;
    std::string error;
    std::vector<std::string> unlocked;     // settings whose alwaysLocked was removed
    std::vector<std::string> kept_locked;  // keep-list settings still locked
    bool squad_added = false;
    std::string squad_note;  // why the squad category was not added ("" = added or not asked)
};

// The recipe: the game's file text in (a UTF-8 BOM is fine), the override text out. `setup` is Career_Setup.json's
// text (only read for squad_settings; may be empty). False (rep.error) when the file is not what the recipe expects:
// unparsable, no FROM_CAREER_MANAGER_HUB context, no locked setting to unlock, or the result fails validation.
bool apply_recipe(const std::string& original, const std::string& setup, const RecipeOptions& opt, std::string& out, RecipeReport& rep);

// FNV-1a 64 of the bytes as 16 hex digits
std::string content_hash(const std::string& bytes);

// ---------------------------------------------------------------- the file adapter
class Store {
public:
    Store(LegacyImages& legacy, std::filesystem::path le_root);

    std::filesystem::path dir() const;            // turbo_output\edit_unlock
    std::filesystem::path original_file(const std::string& path) const;  // dir\original\<path>
    std::filesystem::path manifest_file() const;  // dir\career_settings.json

    struct Status {
        bool original = false;         // the game's file is kept in original\ (Apply possible)
        bool waiting = false;          // asked the game for it, not exported yet
        bool missing = false;          // the game has no such file
        bool written = false;          // the override in mods\legacy is the one Turbo wrote
        bool foreign = false;          // a file Turbo did not write is in mods\legacy (kept, Apply refused)
        bool squad_settings = false;   // the written override has X6
        bool setup_original = false;   // Career_Setup.json is kept too (needed for X6)
        std::string line;              // one line for the GUI
    };
    // Looks at the files; keeps a freshly exported original (capture) and asks the game for the ones still missing
    Status status();

    // Builds the override from the kept original(s) and writes it. False with the reason in msg.
    bool apply(const RecipeOptions& opt, std::string& msg);
    // Removes Turbo's override (backed up first). A file Turbo did not write is kept and reported. False = nothing done.
    bool restore(std::string& msg);

private:
    bool capture(const std::string& path, std::string* why);  // the game's export -> original\ (hash-guarded)
    bool read_manifest();
    bool write_manifest();
    LegacyImages& legacy_;
    std::filesystem::path root_;
    std::string original_hash_, setup_hash_, written_hash_;
    bool written_squad_ = false;
};

}  // namespace csu
}  // namespace turbo
