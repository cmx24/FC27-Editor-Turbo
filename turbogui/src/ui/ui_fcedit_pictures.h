// FC 27 LE Turbo GUI - FC Editor's asset pictures as a picture source of the ONE visual picker (ui_visual_picker.cpp).
//
// FC Editor (Cassio's install, default C:\FC_Tools\FC Editor) keeps a picture per code under assets\, in sub-folders named exactly
// like the database field, either under a game-version folder or plain:
//   assets\27\hairtypecode\<id>.png   assets\26\hairtypecode\...   assets\hairtypecode\...   (png or jpg)
//   assets\bodytypecode\<id> <name>.png   ("1 Average and Lean.png": matched by the leading integer)
// Resolution order of (field, id): assets\27\<field> -> 26 -> 25 -> 24 -> 17 -> assets\<field>; .png before .jpg in one folder.
// The folders are listed ONCE into an in-memory index (field -> id -> best path) when first asked, and again only after the folder
// setting changed (refresh()). Nothing is copied: the picker decodes the file where it is (the same decoder as the RDBM26 pictures;
// stb_image reads png and jpg). A missing folder = an empty index: every picker is as it was.
// Setting: Settings > Preferences, next to the RDBM26 folder (gui_settings.json gui.fc_editor_dir; "" = the default folder).
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>

namespace turbo {

class App;

namespace fcedit {

constexpr const char* kDefaultDir = "C:\\FC_Tools\\FC Editor";
constexpr const char* kPrefKey = "fc_editor_dir";  // GuiPrefs::fc_editor_dir (gui_settings.json gui.fc_editor_dir)
// The game-version folders looked at, newest first, then the plain field folder
constexpr const char* kVersions[] = {"27", "26", "25", "24", "17"};

// The index of one root folder (pure: tests build one from a folder of their own)
struct Index {
    std::map<std::string, std::unordered_map<int64_t, std::string>> fields;  // field -> id -> the picture's path (u8)
    size_t pictures = 0;
    double ms = 0.0;           // time the listing took
    bool found = false;        // root\assets exists
    std::string root;          // the folder it was built from (u8)
    bool has(const std::string& field) const;
    std::string picture(const std::string& field, int64_t id) const;
};
// pack (Turbo X): <turbo_output>\pictures (same layout: assets\<field>\<id>.png), listed BEFORE every FC Editor folder; its pictures
// are Turbo's own (FC Editor's FC 27 webp renders converted, the game's outfit / accessory previews, the in-game captures of the
// gear generator). Empty = none.
Index build_index(const std::filesystem::path& root, const std::filesystem::path& pack = std::filesystem::path());
// The folder a field's pictures are kept under: accessorycode1..4 share "accessorycode"; every other field is its own name
std::string folder_field(const std::string& field);
// Turbo's picture pack folder: <turbo_output>\pictures
std::filesystem::path pack_folder(App& app);

// The folder in use: the override (tests), else the setting (app.prefs.fc_editor_dir), else kDefaultDir
std::filesystem::path folder(App& app);
// The index of that folder (built once; again after refresh())
const Index& index(App& app);
// The picture of `id` of `field` (the database column name), "" when there is none
std::string picture(App& app, const std::string& field, int64_t id);
// Has the index any picture of this field? (vp::has_picture: the row shows a picture instead of a combo)
bool has_field(App& app, const std::string& field);
// Forget the index: the folder setting changed (built again on the next ask)
void refresh();
// Tests: point the resolver at a folder of their own ("" = back to the setting)
void override_dir(const std::string& dir);
// The id a picture file name stands for: "123.png" -> 123, "10 Lionel Messi.png" -> 10, "notes.png" -> -1
int64_t id_of_name(const std::string& stem);

}  // namespace fcedit
}  // namespace turbo
