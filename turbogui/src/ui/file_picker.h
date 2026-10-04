// FC 27 LE Turbo GUI - file and folder picker drawn inside the overlay (file_picker.cpp).
// Turbo never opens a Windows file dialog, Explorer or a console window by itself: any other window takes the game
// out of full screen (seen in 1.1.0: Export went to the desktop and the screen blinked). Every Browse... of Turbo uses
// this picker; Explorer opens only from its explicit "Open in Explorer" button, which says that it leaves full screen.
#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace turbo {

enum class PickMode { Open, Save, Folder };

struct FilePicker {
    PickMode mode = PickMode::Open;
    std::string title;                      // first line of the dialog
    std::vector<std::string> exts;          // lower case with the dot (".csv"); empty = every file
    std::string key;                        // remembered folder (turbo_output\gui_folders.json); "" = none
    std::filesystem::path start;            // folder used when nothing is remembered
    std::vector<std::pair<std::string, std::filesystem::path>> places;  // shortcut buttons (missing ones are hidden)
    bool ask_overwrite = true;              // Save: confirm before an existing file is chosen

    // state
    std::filesystem::path dir;              // folder shown
    char name[260] = "";                    // Save: file name box
    std::string error;
    bool confirm = false;                   // Save: "replace it?" shown
    bool new_folder_open = false;
    char new_folder[128] = "";
    char path_buf[1024] = "";
    std::filesystem::path shown;            // folder path_buf was filled from
    std::filesystem::path listed;           // folder the cache below lists
    double listed_at = -1.0;
    std::vector<std::filesystem::path> dirs, files;
};

// Draw the picker; open it with ImGui::OpenPopup(popup_id). When it appears it shows the folder remembered for fp.key,
// else fp.dir, else fp.start. Returns true once when the user picked: out = the file (Open / Save) or the folder (Folder).
bool file_picker_modal(const char* popup_id, FilePicker& fp, std::filesystem::path& out);

// ---------------------------------------------------------------- helpers (also used by tests)
// UTF-8 text of a path (ImGui text) and back
std::string path_text(const std::filesystem::path& p);
std::filesystem::path text_path(const std::string& s);
// File name the user typed, made safe for Windows: <>:"/\|?* and control characters -> '_', trailing dots and spaces
// dropped; "" when nothing is left
std::string safe_file_name(const std::string& s);
// The same rule as Turbo's Lua preset.safe_name (core/preset.lua): runs of anything but letters, digits, '-' and '_'
// become '_', no '_' at either end, "player" when empty, at most 60 characters
std::string preset_safe_name(const std::string& s);
bool has_ext(const std::filesystem::path& p, const std::vector<std::string>& exts);
// Folders and matching files of dir, sorted by name (case-insensitive); at most 5000 entries
void list_folder(const std::filesystem::path& dir, const std::vector<std::string>& exts, std::vector<std::filesystem::path>& dirs,
                 std::vector<std::filesystem::path>& files);
// The paths that exist
std::vector<std::filesystem::path> existing_files(const std::vector<std::filesystem::path>& paths);

// Remembered folders: last folder per key, kept in a JSON file (normally <Live Editor>\turbo_output\gui_folders.json)
void folder_store(const std::filesystem::path& file);   // load once per file; "" = memory only
std::filesystem::path remembered_folder(const std::string& key);   // "" when none or gone
void remember_folder(const std::string& key, const std::filesystem::path& dir);

// Creates a folder and its parents in this process (never through cmd.exe: a console window leaves full screen)
bool ensure_folder(const std::filesystem::path& dir, std::string* err = nullptr);
// Explorer on a folder: a separate window, so the game leaves full screen. Windows only (false elsewhere).
bool open_in_explorer(const std::filesystem::path& dir);

}  // namespace turbo
