// FC 27 LE Turbo GUI - picture panels (ui_images.cpp)
#pragma once
#include <cstdint>
#include <filesystem>
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
};

struct RealFaceOptions {
    bool hair = false, beard = false, eyes = false, skin = false;
    bool miniface = true;
};

// Picture of a file fitted into side x side; returns true when it was drawn
bool draw_file_picture(App& app, const std::filesystem::path& f, float side);
// Game picture (or custom file); placeholder while it loads
LegacyImages::State draw_legacy_picture(App& app, const std::string& path, float side, bool custom_first);
// Pictures in a folder (PNG, JPG, BMP, TGA, DDS), sorted by name
std::vector<std::filesystem::path> picture_files(const std::filesystem::path& dir);
// Folder shortcuts of the picture browser (Desktop, Pictures incl. OneDrive, Downloads, turbo_minifaces) that exist
std::vector<std::pair<std::string, std::filesystem::path>> browser_shortcuts();

void miniface_editor(App& app, const MinifaceTarget& t);
// "Choose a real face" modal (open it with ImGui::OpenPopup("Choose a real face"))
void real_face_picker(App& app, int64_t target_pid);
size_t real_face_count(App& app);
// Give target the head model of owner (validated writes); used by the picker and by tests
bool apply_real_face(App& app, int64_t target_pid, int64_t owner_pid, const RealFaceOptions& o, std::string* msg);
// Tattoo per body area with preview and picker
void tattoo_editor(App& app, const Table& t, uint64_t rec);
// Hair, facial hair, boots, GK gloves and accessories with the game's preview pictures, picker grids and favourites
void item_galleries(App& app, const Table& t, uint64_t rec);
// Item ids the game has previews for (legacy_filename_hash_list.csv), per imgAssets folder (hairstyle, boots, ...)
std::vector<int64_t> gallery_ids(App& app, const std::string& folder);
// Status tab: picture cache
void images_status(App& app);

}  // namespace turbo
