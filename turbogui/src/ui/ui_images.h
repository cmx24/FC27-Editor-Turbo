// FC 27 LE Turbo GUI - picture panels (ui_images.cpp)
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

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
};

struct RealFaceOptions {
    bool hair = false, beard = false, eyes = false, skin = false;
    bool miniface = true;
};

// One clickable cell of a picture grid (game picture, caption under it); true when clicked
bool picture_cell(App& app, const std::string& path, bool custom_first, float cell, const std::string& caption,
                  const char* id, bool selected, bool none = false);
// Picture of a file fitted into side x side; returns true when it was drawn
bool draw_file_picture(App& app, const std::filesystem::path& f, float side);
// Game picture (or custom file); placeholder while it loads
LegacyImages::State draw_legacy_picture(App& app, const std::string& path, float side, bool custom_first);
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
bool apply_real_face(App& app, int64_t target_pid, int64_t owner_pid, const RealFaceOptions& o, std::string* msg);
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
// Status tab: picture cache
void images_status(App& app);

}  // namespace turbo
