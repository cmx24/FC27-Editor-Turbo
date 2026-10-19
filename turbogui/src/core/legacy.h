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
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace turbo {

namespace legacy_path {
std::string player_miniface(int64_t playerid);       // data/ui/imgAssets/heads/p<id>.dds
std::string staff_miniface(int64_t id);              // data/ui/imgAssets/heads_staff/heads_staff_<id>.dds
// A manager's pictures in the order Turbo shows them (G1 2026-10-17, HEADS3D_CENSUS.md MINIFACES): heads_staff_<managerid>
// (the game ships its staff pictures under the managerid: of the 61 base managers whose head id differs, 11 have one under
// the managerid, none under the head id), heads_staff_<headassetid> (Turbo's copies before 2.13), then the generic
// manager heads' genericManagerHeads/gmh<headassetid>.dds (45 managers). Ids <= 0 and duplicates are left out.
std::vector<std::string> staff_pictures(int64_t managerid, int64_t headassetid);
// The file Turbo writes a manager's miniface to: the first of staff_pictures, heads_staff_<managerid> (the head id
// only when the managerid is unknown, <= 0)
std::string staff_miniface_for(int64_t managerid, int64_t headassetid);
std::string generic_manager_head(int64_t headassetid);  // data/ui/imgAssets/genericManagerHeads/gmh<id>.dds
std::string youth_face(int64_t headassetid);         // data/ui/imgAssets/youthheads/p<id>.dds
std::string legend_picture(int64_t id);             // data/ui/imgAssets/cmLegendaryPlayers/legend_<id>.dds
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
    std::filesystem::path kit_backup_dir() const;    // turbo_output\kit_backups (2.3: minikits written by Teams > Kits)
    // Backups of crest files (data/ui/imgAssets/crest*/...) go to crest_backup_dir(), minikits (data/ui/imgAssets/kits/...)
    // to kit_backup_dir(), game editor configs (data/avatar/..., data/gamesettings/...) to turbo_output\edit_unlock\backups,
    // everything else to backup_dir()
    std::filesystem::path backup_dir_for(const std::string& path) const;

    // Where the picture for `path` is. custom_first: a custom file under mods\legacy wins (what the game shows).
    // Game files not exported yet are asked for (want.txt) and reported as Waiting.
    State locate(const std::string& path, std::filesystem::path* file, bool custom_first = true);
    // The first of `paths` (in order) that is there, custom or game file, or still asked for: a later path is looked at
    // only when the earlier ones are known missing (or invalid). "" when none is there; `file` / `st` as locate's.
    std::string first_present(const std::vector<std::string>& paths, std::filesystem::path* file = nullptr, State* st = nullptr);
    // Ask for a game file without looking (pre-loading a picker page); `front` puts it first
    void want(const std::string& path, bool front = true);
    // Background loading (ui/preload.h): asked after everything on screen, at most background_limit() paths waiting at
    // once. Accepted: listed in want.txt. Known: invalid, missing, already asked (screen or background) or already at
    // hand: nothing to ask. Full: the list is full; the caller keeps the path and asks again later (pictures that arrive
    // leave the list, so it drains).
    enum class Ask { Accepted, Known, Full };
    static constexpr size_t kMaxBackground = 20000;
    Ask ask_background(const std::string& path);
    bool want_background(const std::string& path) { return ask_background(path) == Ask::Accepted; }
    size_t background_limit() const { return bg_limit_; }
    bool background_full() const { return bg_.size() >= bg_limit_; }
    // Lower it to drive refusals (the native tests); kMaxBackground otherwise
    void set_background_limit(size_t n) { bg_limit_ = n; }
    // Where the game's picture is, without asking for it and without looking at custom files (the background loader)
    State peek(const std::string& path, std::filesystem::path* file = nullptr) const;
    size_t waiting_background() const { return bg_.size(); }
    // Forget the background list (the career left, the setting was switched off): want.txt is rewritten without it;
    // what is on screen stays asked
    void clear_background() {
        if (bg_.empty()) return;
        bg_.clear();
        bg_set_.clear();
        bg_check_ = 0;
        dirty_ = true;
    }

    // Call regularly: writes want.txt when it changed (at most every 0.5 s), re-reads missing.txt / status.txt
    void tick(double now);
    // Force want.txt out now (tests, before running turbo_images.lua)
    bool flush();
    // Re-read missing.txt / status.txt now, without writing want.txt (the game editors' early pass)
    void refresh_missing() {
        game_present_.clear();
        waiting_seen_.clear();
        read_missing();
    }
    // 2.20: the next locate() of this path looks at its file even if it was found absent less than a second ago (a pass
    // that reads the file itself, such as the game editors' collect, which runs once a second anyway)
    void look_now(const std::string& path) { waiting_seen_.erase(path); }
    // 2.16 gui/perf-3: the App calls this once per frame; only then are a folder's look and a found game picture kept
    // (within the frame / for a second). Direct callers that never call it (the unit tests, tools) look every time.
    void next_frame() { ++frame_; }

    size_t waiting() const { return want_.size() + bg_.size(); }
    size_t missing_count() const { return missing_.size(); }

    // ---- pictures asked for on screen and Turbo's Lua side answering them (1.2.5)
    // Lua has delivered and refused nothing for this long while pictures asked on screen wait: it is not running (no
    // career loaded, Live Editor busy, the synthetic event off), so the host stops prompting it and the boxes say so.
    // A newly asked picture or reload() starts the wait again.
    static constexpr double kStallSeconds = 20.0;
    // Seconds since `path` was first asked for on screen and not delivered yet; -1 when it is not waiting
    double waiting_for(const std::string& path) const;
    size_t waiting_on_screen() const { return want_.size(); }
    bool stalled() const { return !want_.empty() && now_ - last_progress_ >= kStallSeconds; }
    // Turbo is hidden: Lua is not prompted, so its silence says nothing (the stall clock waits). Call after tick().
    void not_prompting() { last_progress_ = now_; }
    // Pictures asked on screen wait and Lua has not gone quiet: the host prompts Lua (App::lua_images_wanted)
    bool wants_lua() const { return !want_.empty() && !stalled(); }
    // Ask the game for `path` once more, first in the list: the cached copy is dropped (Live Editor exports the file the
    // game loads, so a copy made while a custom file was in place is that custom file), an earlier "not in the game"
    // answer is set aside (Lua lists the path again if the game still has none) and Lua looks at every wanted path
    // again (new generation: it remembers what it handled per generation)
    void reload(const std::string& path);
    // Where the exported copy of `path` is kept (it may not exist); empty for an invalid path
    std::filesystem::path cache_file(const std::string& path) const;
    const std::string& lua_status() const { return status_; }
    size_t cached_files() const;
    // 2.16 gui/perf: the same count for any folder, touching no LegacyImages state (the Settings page runs it on a worker:
    // it walks every file of the cache, 24,675 in a played career, and froze the page when drawn every frame)
    static size_t count_cached_files(const std::filesystem::path& dir);
    // 2.16 gui/perf: the path is asked right now (Lua's background list or on screen): the background loader does not look
    // for it on disk again until it leaves the list (tick notices it arriving, read_missing notices it missing)
    bool asked_now(const std::string& path) const { return bg_set_.count(path) > 0 || want_set_.count(path) > 0; }
    // Empty the cache folder and start a new generation (Lua looks at everything again)
    bool clear_cache(std::string* err = nullptr);

    // 2.19 career faces: files under mods\legacy changed behind LegacyImages (core/career_faces.h): no folder look is kept
    void forget_folder_looks() {
        std::lock_guard<std::mutex> lock(custom_dirs_m_);
        custom_dirs_.clear();
    }
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
    size_t bg_limit_ = kMaxBackground;         // ask_background says Full beyond it
    size_t bg_check_ = 0;                      // round-robin position of the arrival check (tick)
    double next_bg_check_ = 0.0;               // 2.16: the arrival check runs every quarter second (100 paths), not 400 at once
    void drop_background(const std::function<bool(const std::string&)>& gone);
    void drop_wanted(const std::function<bool(const std::string&)>& gone);  // arrived / refused: out of want_ (progress)
    std::unordered_set<std::string> missing_;
    std::unordered_map<std::string, int> missing_lines_;      // times each path is listed in missing.txt
    std::unordered_map<std::string, int> missing_set_aside_;  // reload(): the path's lines in missing.txt then
    std::unordered_map<std::string, double> asked_at_;        // want_ path -> when it was first asked (waiting_for)
    double now_ = 0.0;                                        // last tick
    // 2.16 gui/perf: custom_file's case-insensitive match lists a folder of mods\legacy; the listing is kept per folder and
    // read again only when the folder's last-write time changes (a file added, removed or renamed in it). Before, every
    // picture drawn listed its folder in every frame. Under a mutex: custom_file is const and may be asked from a worker.
    // 2.16 gui/perf-3: the folder's time stamp is read at most once per tick (frame) while the folder is stable (unchanged for
    // 2 s); a folder that just changed is looked at on every call, as before
    struct CustomDir {
        bool listed = false;
        bool recent = true;          // changed less than 2 s before the last look
        uint64_t looked_frame = 0;   // frame_ of the last look (0 = none)
        std::filesystem::file_time_type mtime{};
        std::unordered_map<std::string, std::filesystem::path> by_lower;  // lower-case file name -> the file
    };
    mutable std::mutex custom_dirs_m_;
    mutable std::unordered_map<std::string, CustomDir> custom_dirs_;
    // 2.16 gui/perf-3: game pictures found in the cache (exported once by Lua, never rewritten): locate answers Game without
    // looking at the file for a second after it last saw it (calls without ticks always look). Forgotten by reload() and
    // clear_cache(), and when a look finds the file gone.
    std::unordered_map<std::string, double> game_present_;  // path -> now_ when the file was last seen
    // 2.20: the negative side. A picture found absent (Waiting for its export, or Missing) is looked at again at most once a
    // second (tick() looks at every asked picture once a second and drops what arrived; a new missing.txt, reload() and
    // clear_cache() look at once). Before, every frame that drew it opened its file (gui perf: a manager picture the game
    // never delivers, 2 to 4 file calls per frame for as long as the page was open).
    std::unordered_map<std::string, double> waiting_seen_;  // path -> now_ when its file was last found absent
    uint64_t frame_ = 0;  // next_frame() count: 0 = never called (every call looks at the files)
    double last_progress_ = 0.0;  // a wanted picture arrived or was refused, Lua's status changed, or one was newly asked
    std::string status_;
    uint64_t gen_ = 0;
    bool dirty_ = false;
    double last_write_ = -1.0, next_read_ = 0.0;
    uintmax_t missing_size_ = 0;
};

}  // namespace turbo
