// FC 27 LE Turbo GUI - player presets: Export / Import / Clone / Create player dialogs of the Players tab (ui_presets.cpp)
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/model.h"

namespace turbo {

class App;

// What a preset file holds, for the Import dialog's preview (parsed here, not by Lua)
struct PresetPreview {
    bool ok = false;
    std::string error;
    std::string kind;        // "Live Editor preset CSV" | "Turbo player JSON"
    int rows = 0;            // rows in a CSV (Live Editor appends one per save; the last is used)
    int columns = 0;
    std::string name, playerid, overall, potential, position, miniface;
};
PresetPreview preview_preset_file(const std::filesystem::path& file);

// Buttons "Export...", "Import...", "Clone...", "Create player..." and their dialogs, for the selected player
void player_preset_buttons(App& app, const PlayerRow& p);

// Files an export would replace (the Export dialog asks first). base = the one player's file name; "" = the list
// list_ids, whose file sets Lua names "<name>_<playerid>" (a file ending in "_<playerid>.<ext>" counts)
std::vector<std::filesystem::path> export_clashes(const std::filesystem::path& json_dir, const std::filesystem::path& csv_dir, bool want_json,
                                                  bool want_csv, bool want_mini, const std::string& base, const std::vector<int64_t>& list_ids);

}  // namespace turbo
