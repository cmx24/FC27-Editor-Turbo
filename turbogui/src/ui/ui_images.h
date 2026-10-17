// FC 27 LE Turbo GUI - picture panels (ui_images.cpp)
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/legacy.h"
#include "core/t3db.h"

namespace turbo {

class App;

struct MinifaceTarget {
    bool manager = false;
    std::string path;         // legacy path of the miniface being edited
    int64_t headassetid = 0;  // players: head model (head model / youth face sources); managers: heads_staff id
    int64_t id = 0;           // players: playerid; managers: managerid (the 3D-model capture renders the head id for managers)
    int64_t teamid = 0;       // players: club (0 = none); passed to the 3D-model capture as the second id
    std::string shown;        // managers: the picture shown now (first of legacy_path::staff_pictures; "" = path)
};

// What the real-face chooser copies besides the head; the miniface always comes with the head (never a mixed result)
struct RealFaceOptions {
    bool hair = false, beard = false, eyes = false, skin = false;
};

// One clickable cell of a picture grid (game picture, caption under it); true when clicked
bool picture_cell(App& app, const std::string& path, bool custom_first, float cell, const std::string& caption,
                  const char* id, bool selected, bool none = false);
// Picture of a file fitted into side x side; returns true when it was drawn. A box that cannot show it says why:
// a spinner while it decodes, "unreadable" with the reason in its tooltip (read again in a moment: textures.h).
bool draw_file_picture(App& app, const std::filesystem::path& f, float side);
// What a legacy picture box showed, for the line under it (miniface editors)
struct PictureInfo {
    LegacyImages::State state = LegacyImages::State::Invalid;
    std::filesystem::path file;  // the custom file or the game's exported copy (Custom / Game)
    bool drawn = false;          // the picture is on screen
    bool failed = false;         // the file could not be read: `error` says why (the format refused, cut off, ...)
    std::string error;
    double waiting = -1.0;       // Waiting: seconds since it was asked from the game
    bool stalled = false;        // Waiting: Turbo's Lua side has not answered for LegacyImages::kStallSeconds
};
// Game picture (or custom file); a spinner while it loads, "no answer" / "no picture" with the reason in the tooltip
LegacyImages::State draw_legacy_picture(App& app, const std::string& path, float side, bool custom_first, PictureInfo* info = nullptr);
// The line under a picture box: what it shows ("Custom (mods\legacy)", "The game's own") or why it shows none (still
// loading and for how long, no answer from Turbo's Lua side, not in the game, unreadable and why). `what` names the
// picture ("miniface"); warn: the line is a problem worth a colour.
std::string picture_status_line(App& app, const PictureInfo& info, const char* what, bool* warn = nullptr);
// Read `path`'s picture again (Reload): its files are decoded anew and, unless a custom file is shown, the game is asked
// for its copy again (LegacyImages::reload)
void reload_picture(App& app, const std::string& path, const PictureInfo& info);
// Pictures in a folder (PNG, JPG, BMP, TGA, DDS), sorted by name
std::vector<std::filesystem::path> picture_files(const std::filesystem::path& dir);
// Folder shortcuts of the picture browser (Desktop, Pictures incl. OneDrive, Downloads, turbo_minifaces) that exist
std::vector<std::pair<std::string, std::filesystem::path>> browser_shortcuts();
// Modal picture browser (open it with ImGui::OpenPopup(id)); true when a picture file was chosen (out)
bool file_browser_modal(const char* id, std::filesystem::path& cur_dir, std::filesystem::path& out);

void miniface_editor(App& app, const MinifaceTarget& t);
// "Choose a real face" modal (open it with ImGui::OpenPopup("Choose a real face")); ui_faces.cpp, managers: ui_faces.h
void real_face_picker(App& app, int64_t target_pid);
size_t real_face_count(App& app);
// Give target the head model of owner (validated writes); used by the picker and by tests
bool apply_real_face(App& app, int64_t target_pid, int64_t owner_pid, const RealFaceOptions& o, std::string* msg, bool* in_step = nullptr);
// Tattoo per body area with preview and picker
void tattoo_editor(App& app, const Table& t, uint64_t rec);
// Hair, facial hair, boots, GK gloves and accessories (players) or the outfit (managers: manager = true) with the game's
// preview pictures, picker grids and favourites
void item_galleries(App& app, const Table& t, uint64_t rec, bool manager = false);
// Gear preview pictures the game has (legacy_filename_hash_list.csv paths under data/ui/imgAssets/), by folder and file
// name prefix (any letter case): "shoe/shoe_192_0.dds", "accessories/item_150_3.dds", "boots/item_1_0_0_0.dds", "outfit/item_6067.dds"
struct GearPictureIndex {
    std::map<std::string, std::map<int64_t, std::vector<std::string>>> files;  // lower-case "<folder>/<prefix>" -> id -> "<folder>/<file>" as listed
    void add(const std::string& path);  // one listed path (others are ignored)
    bool empty() const { return files.empty(); }
    std::vector<int64_t> ids(const std::string& folder, const std::string& prefix) const;
    // The listed picture of item id: colour variant _<colour>, else _0, else no suffix, else the first listed one;
    // "" when the game has none (show "no picture in the game", never ask the game for it)
    std::string find(const std::string& folder, const std::string& prefix, int64_t id, int64_t colour = 0) const;
};
// Live Editor's list (<LE> and <LE>\extensions), read once per Live Editor folder
const GearPictureIndex& gear_pictures(App& app);
// Every gallery picture (players' and managers' gear, tattoos) the game has, for the background loading (ui/preload.h)
std::vector<std::string> gallery_preload_paths(App& app);
// The item ids the game has a picture for in a field's gallery (hair, facial hair, boots, gloves, accessories, outfits,
// tattoos), which its code combo offers too; empty for any other field. With a game asset catalog (set_gear_catalog):
// none + the ids that may be written (3D models; hidden ones only while the catalog is current), core/gear_ids.h
std::vector<int64_t> gallery_codes(App& app, const std::string& field);
// The faces3d picker (ui_faces3d_gate.cpp) reuses the Players > Appearance gallery for scanned heads' generic hair
// (hairtypecode: craniumhair/hair_<id>_0) and beard (facialhairtypecode: craniumfacialhair/Facial_hair_<id>_0).
struct GalleryPick {
    int64_t id = 0;
    std::string path;  // the listed picture ("data/ui/imgAssets/..."); "" = none listed (the cell shows the text label)
};
// Picture path of `id` in the hair / facial-hair gallery from the hash list alone ("" = list at hand but no picture; the usual
// file name when nothing is listed; "" for other fields and id <= 0). The catalog-aware twin that the cells draw is gallery_picture_path.
std::string gallery_pick_path(const GearPictureIndex& gx, const std::string& field, int64_t id);
// The picker's cells: `ids` (verified ids, ascending) minus ids <= 0, filtered by hair length (hair::Length, -1 = any; hair only),
// each with its picture path
std::vector<GalleryPick> gallery_pick_list(const GearPictureIndex& gx, const std::string& field, const std::vector<int>& ids,
                                           int hair_length = -1);
std::string gallery_picture_path(App& app, const std::string& field, int64_t id);
// The picture of `id` in side x side; true when a game picture is (being) shown. No preview in the game (none listed, or the
// game has none): a box showing `fallback_text` instead. id <= 0: "none".
bool gallery_picture(App& app, const std::string& field, int64_t id, float side, const std::string& fallback_text);
// Clickable cell: gallery_picture with `caption` under it; true when clicked
bool gallery_picture_cell(App& app, const std::string& field, int64_t id, float side, const std::string& caption,
                          const std::string& fallback_text, const char* bid, bool selected, bool none = false);
// The game asset catalog the gear galleries and tattoo pickers read (docs/asset-catalog.md). The app sets it after every
// catalog load or switch change; nullptr (catalog off, not loaded, refused) = every gallery behaves exactly as in 2.0.
void set_gear_catalog(std::shared_ptr<const assets::Catalog> catalog);
// Gear code combos (widgets.cpp): "Hidden", "preview only", "not in the game files" for a players gear code the catalog
// knows; "" otherwise (no catalog, other tables and fields, ids the save uses)
std::string gear_code_note(App& app, const Table& t, const std::string& field, int64_t id);
// Status tab: picture cache
void images_status(App& app);

}  // namespace turbo
