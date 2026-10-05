// FC 27 LE Turbo GUI - game pictures ("legacy files": minifaces, manager faces, tattoo previews) and custom minifaces.
//
// Game files: only Live Editor's Lua LegacyFileExport can read them. The GUI writes the paths it wants to
//   turbo_output\cache\legacy\want.txt ("#gen <n>", then one path per line, most wanted first); Turbo's Lua side
//   (core/legacy.lua) exports them on career-mode events, or at once with lua\scripts\turbo_images.lua, to
//   turbo_output\cache\legacy\<path>. Paths the game does not have are appended to missing.txt.
// Custom files: Live Editor loads files under <Live Editor>\mods\legacy\<path> instead of the game's. A custom miniface
//   is a DDS written there; anything Turbo replaces or removes is copied to turbo_output\miniface_backups first.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace turbo {

namespace legacy_path {
std::string player_miniface(int64_t playerid);       // data/ui/imgAssets/heads/p<id>.dds
std::string staff_miniface(int64_t headassetid);     // data/ui/imgAssets/heads_staff/heads_staff_<id>.dds
std::string youth_face(int64_t headassetid);         // data/ui/imgAssets/youthheads/p<id>.dds
std::string tattoo_preview(int64_t tattooid);        // data/ui/imgAssets/tattoo/item_<id>_0.dds
// Club crest: data/ui/imgAssets/crest<size>/<folder>/l<teamid>.dds; size 0 = the big "crest" folder (256 x 256),
// otherwise 16, 32, 50, 512 or 1024 ("crest16x16", ...); folder "light", "dark" or "custom"
std::string crest(int64_t teamid, int size, const char* folder);
// Same rule as Lua's legacy.valid_path: data/..., [A-Za-z0-9_/.-] only, no "..", at most 200 bytes
bool valid(const std::string& p);
}  // namespace legacy_path

// Every crest file the game may have for a team (crest + 16/32/50 for light and dark, custom for generated clubs,
// 512 and 1024 where they exist). Which of them exist is only known after the game exported them (missing.txt).
struct CrestVariant {
    int size;            // pixels per side of the game's file (0 = "crest" folder, 256 in FC 27)
    const char* folder;  // light | dark | custom
    std::string path;
};
std::vector<CrestVariant> crest_variants(int64_t teamid);
// The one shown in the Teams list / editor: crest/light (crest/custom for generated clubs)
std::string crest_main_path(int64_t teamid);

constexpr int kPlayerMinifaceSize = 256;
constexpr int kStaffMinifaceSize = 512;
constexpr int kCrestBigSize = 256;

class LegacyImages {
public:
    enum class State { Custom, Game, Waiting, Missing, Invalid };

    explicit LegacyImages(std::filesystem::path le_root);

    std::filesystem::path cache_dir() const;
    std::filesystem::path mods_dir() const;      // <LE>\mods\legacy
    std::filesystem::path backup_dir() const;    // turbo_output\miniface_backups
    std::filesystem::path crest_backup_dir() const;  // turbo_output\crest_backups
    // Backups of crest files (data/ui/imgAssets/crest*/...) go to crest_backup_dir(), game editor configs (data/avatar/...,
    // data/gamesettings/...) to turbo_output\edit_unlock\backups, everything else to backup_dir()
    std::filesystem::path backup_dir_for(const std::string& path) const;

    // Where the picture for `path` is. custom_first: a custom file under mods\legacy wins (what the game shows).
    // Game files not exported yet are asked for (want.txt) and reported as Waiting.
    State locate(const std::string& path, std::filesystem::path* file, bool custom_first = true);
    // Ask for a game file without looking (pre-loading a picker page); `front` puts it first
    void want(const std::string& path, bool front = true);
    // Background loading (ui/preload.h): asked after everything on screen, at most kMaxBackground; false when the path
    // is invalid, missing, already asked or already at hand
    static constexpr size_t kMaxBackground = 20000;
    bool want_background(const std::string& path);
    // Where the game's picture is, without asking for it and without looking at custom files (the background loader)
    State peek(const std::string& path, std::filesystem::path* file = nullptr) const;
    size_t waiting_background() const { return bg_.size(); }

    // Call regularly: writes want.txt when it changed (at most every 0.5 s), re-reads missing.txt / status.txt
    void tick(double now);
    // Force want.txt out now (tests, before running turbo_images.lua)
    bool flush();
    // Re-read missing.txt / status.txt now, without writing want.txt (the game editors' early pass)
    void refresh_missing() { read_missing(); }

    size_t waiting() const { return want_.size() + bg_.size(); }
    size_t missing_count() const { return missing_.size(); }
    const std::string& lua_status() const { return status_; }
    size_t cached_files() const;
    // Empty the cache folder and start a new generation (Lua looks at everything again)
    bool clear_cache(std::string* err = nullptr);

    // Custom file under mods\legacy (any letter case of the name), or empty
    std::filesystem::path custom_file(const std::string& path) const;
    // Write a custom file (previous custom file backed up first). Returns false with a reason.
    bool save_custom(const std::string& path, const std::vector<uint8_t>& bytes, std::string* err = nullptr,
                     std::filesystem::path* backup = nullptr);
    // Back up and delete the custom file(s) for `path`. false when there was none (err says so) or deleting failed.
    bool remove_custom(const std::string& path, std::string* err = nullptr, std::filesystem::path* backup = nullptr);

    // Fix custom DDS files under mods\legacy\data\ui\imgAssets whose mip count claims more levels than the file holds
    // (crests written by Turbo 1.0.0 crash the game while it loads the career). The original goes to the backups.
    // One line per file repaired or failed; empty when every file is fine.
    std::vector<std::string> repair_dds_files();

    uint64_t generation() const { return gen_; }

private:
    bool backup(const std::filesystem::path& f, const std::filesystem::path& dir, std::filesystem::path* out, std::string* err);
    void read_missing();

    std::filesystem::path root_;
    std::vector<std::string> want_;            // most wanted first
    std::unordered_set<std::string> want_set_;
    std::vector<std::string> bg_;              // background loading, after want_ (paths in want_set_ are skipped)
    std::unordered_set<std::string> bg_set_;
    size_t bg_check_ = 0;                      // round-robin position of the arrival check (tick)
    void drop_background(const std::function<bool(const std::string&)>& gone);
    std::unordered_set<std::string> missing_;
    std::string status_;
    uint64_t gen_ = 0;
    bool dirty_ = false;
    double last_write_ = -1.0, next_read_ = 0.0;
    uintmax_t missing_size_ = 0;
};

}  // namespace turbo
