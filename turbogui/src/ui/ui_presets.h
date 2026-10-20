// FC 27 LE Turbo GUI - player presets: Export / Import / Clone / Create player dialogs of the Players tab (ui_presets.cpp)
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/create_form.h"
#include "core/model.h"
#include "core/player_id.h"
#include "nlohmann/json.hpp"

namespace turbo {

class App;

// The Create player dialog's Player ID box as the last frame drew it (core/player_id.h; tests read it back)
struct CreateIdState {
    std::string text;              // what the box holds
    player_id::Verdict verdict;    // player_id::check of the box
    bool typed_off = false;        // turbo_output\create_player_typed_id_off.txt exists: the box is disabled
    size_t hidden = 0, shown = 0;  // "Use a hidden player's id": ids it lists, and how many its filter and search show
    nlohmann::json sent;           // the overrides of the last "Create player" click (null = none yet)
};
const CreateIdState& create_id_state();

// The Create player dialog's form as the last frame drew it (2.19 create-prefill; tests read it back): the template's row as
// read when the dialog opened, the boxes now, and the "set" a Create click would send (only the changed fields)
struct CreatePrefillState {
    int64_t playerid = 0;
    create_form::Form orig, cur;
    nlohmann::json pending = nlohmann::json::object();
    std::vector<std::string> sections;  // 2.22: the section headings of the last frame, in the order drawn (Player type & tier is first)
};
const CreatePrefillState& create_prefill_state();
// Open the Create player dialog with this database player as the template (the CMTracker and file routes when their player is
// already in the database): he becomes the selected player and the dialog opens at the next frame of the Players editor
void request_create_from(App& app, int64_t playerid);
// The database player an outside player is (create_form::match over this career's players): 0 = none
int64_t database_match(App& app, int64_t playerid, const std::string& name, int64_t birthdate_days);

// What a preset file holds, for the Import dialog's preview (parsed here, not by Lua)
struct PresetPreview {
    bool ok = false;
    std::string error;
    std::string kind;        // "Live Editor preset CSV" | "Turbo player JSON"
    int rows = 0;            // rows in a CSV (Live Editor appends one per save; the last is used)
    int columns = 0;
    std::string name, playerid, overall, potential, position, miniface;
    int64_t birthdate = 0;   // gregorian days, 0 = not in the file
};
PresetPreview preview_preset_file(const std::filesystem::path& file);
// A birth date as a file writes it: gregorian days (Live Editor preset CSV, Turbo player file) or YYYY-MM-DD; 0 = none
int64_t birthdate_days(const std::string& text);

// Buttons "Export...", "Import...", "Clone...", "Create player..." and their dialogs, for the selected player
void player_preset_buttons(App& app, const PlayerRow& p);

// Club picker (one box for the club's name or ID, ui_club_search.h) and the notes under it, shared with the CMTracker
// dialog (ui_cmtracker.cpp)
void club_picker(App& app, int& teamid);
void club_notes(App& app, int teamid);
// "From CMTracker..." dialog (open it with ImGui::OpenPopup("##pcmt")); also finishes a create whose picture was fetched in the background
void cmtracker_dialog(App& app);
// "Season pack..." dialog (the "Season pack..." button of the From CMTracker dialog opens it with ImGui::OpenPopup("##pseasonpack"); it is
// drawn inside that dialog): the season update pack importer (core/season_pack.h)
void season_pack_dialog(App& app);
// Read the pack folder and check its rows against the loaded career (what the picker does); false + err when it is not a pack
bool season_pack_open_folder(App& app, const std::filesystem::path& folder, std::string* err);

// Files an export would replace (the Export dialog asks first). base = the one player's file name; "" = the list
// list_ids, whose file sets Lua names "<name>_<playerid>" (a file ending in "_<playerid>.<ext>" counts)
std::vector<std::filesystem::path> export_clashes(const std::filesystem::path& json_dir, const std::filesystem::path& csv_dir, bool want_json,
                                                  bool want_csv, bool want_mini, const std::string& base, const std::vector<int64_t>& list_ids);

}  // namespace turbo
