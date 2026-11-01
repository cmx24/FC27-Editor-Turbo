// FC 27 LE Turbo GUI - Named transactional multi-save edit profiles (<turbo_output>/profiles/<slot>.turbox.json).
// Stores a SHA-256-verified snapshot of kept player edits, kit colours/fields, player callnames, club commentary swaps,
// and live team name overrides, with pre-apply diff preview and 1-click session revert. Never touches EA save files.
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace turbo {
class App;

namespace profiles {

struct ProfileSlotInfo {
    std::string slug;        // filename stem without ".turbox" (e.g. "serie_a_realism")
    std::string name;        // display name
    std::string career;      // optional career label recorded when saved
    std::string updated_at;  // ISO-like timestamp
    std::string sha256;      // payload SHA-256 hex digest
    size_t players = 0;
    size_t kits = 0;
    size_t callnames = 0;
    size_t club_voices = 0;
    size_t team_names = 0;
    bool valid_checksum = true;

    size_t total_items() const { return players + kits + callnames + club_voices + team_names; }
};

struct ProfileDiff {
    std::string slug;
    std::string name;
    size_t players_changed = 0;
    size_t kits_changed = 0;
    size_t callnames_changed = 0;
    size_t club_voices_changed = 0;
    size_t team_names_changed = 0;
    bool valid_checksum = true;
    std::vector<std::string> lines;

    size_t total_changes() const {
        return players_changed + kits_changed + callnames_changed + club_voices_changed + team_names_changed;
    }
};

std::filesystem::path profiles_dir(const std::filesystem::path& le_root);
std::string sanitize_slug(const std::string& name);
std::string sha256_hex(const std::string& data);

std::vector<ProfileSlotInfo> list_profiles(const std::filesystem::path& le_root);
bool save_profile(App& app, const std::string& name, std::string* out_slug = nullptr, std::string* err = nullptr);
bool diff_profile(App& app, const std::string& slug, ProfileDiff& out, std::string* err = nullptr);
bool apply_profile(App& app, const std::string& slug, std::string* summary = nullptr, std::string* err = nullptr);
bool delete_profile(const std::filesystem::path& le_root, const std::string& slug, std::string* err = nullptr);

bool can_revert_last_apply();
std::string last_applied_profile_name();
bool revert_last_apply(App& app, std::string* summary = nullptr, std::string* err = nullptr);
void reset_session_for_tests();

}  // namespace profiles
}  // namespace turbo
