// FC 27 LE Turbo GUI - visual pickers (2.20 track V): run style and free-kick run-up as animated tiles, hair, beard, eyebrows,
// colours, kit, boots, gloves, accessories and tattoos as pictures, body types from the body-type pictures - each tile under its
// in-game name (the code small and grey), a large animated preview of the tile under the mouse (or the one picked), a search
// box, the current value marked. One click picks, Apply writes the field through the App's validated edit (App::edit: undo
// step, kept edits, scoped-edit rules unchanged); a body type through apply_bodytype (its risky-pairing refusal included).
//
// The pictures come from the user's own RDBM26 folder (core/rdbm_resources.h; Settings > Preferences > "Pictures for the
// pickers"). Without it every picker is a names-only list with one line saying where the pictures come from.
#pragma once
#include <cstdint>
#include <string>

#include "core/t3db.h"

namespace turbo {

class App;

// The pickers of every mapped field the table has, grouped (Movement, Hair and face, Kit and boots, Tattoos), plus the body
// type grid; one row per field: its picture, its in-game name, "Choose...". Draws the picker window when one is open.
void visual_pickers_section(App& app, const Table& t, uint64_t rec, bool manager = false);
// One row only (a page that shows a single field), and the picker window when its field is the open one
void visual_field_row(App& app, const Table& t, uint64_t rec, const char* field, bool manager = false);
// Settings > Preferences: the RDBM26 folder line (set, default, what was found)
void visual_pictures_setting(App& app);

// Write `code` into `field` of the record through App::edit (bodytypecode: apply_bodytype). false with *msg when refused.
bool visual_apply(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t code, std::string* msg = nullptr);

// ---- tests
struct VisualPickerUi {
    bool open = false;
    std::string field;
    int64_t selected = -1, current = -1, previewed = -1;
    int tiles_drawn = 0;        // tiles drawn in the last frame of the open picker
    int pictures_shown = 0;     // of those, tiles with their picture (animated or still) on screen
    int uploads_last_frame = 0; // animations given to the window's textures in the last frame
    bool names_only = false;    // no pictures folder: the list of names
    std::string last_result;    // "Run style set to Medalist", or the refusal
};
const VisualPickerUi& visual_picker_ui();
// Open the picker of `field` for the record on the next frame (what "Choose..." does)
void visual_picker_request(const std::string& field);
void visual_picker_select(int64_t code);   // what a click on a tile does
bool visual_picker_apply(App& app);        // what Apply does (on the record the picker was opened for)
void visual_picker_close();
// Look for the pictures folder again (after the setting changed); tests point it at a folder of their own
void visual_pictures_refresh(App& app);
void visual_pictures_override(const std::string& resources_dir);  // "" = back to the setting / default
// Stop the decoding thread and forget every animation (tests; the game never unloads Turbo, so the window never calls it)
void visual_pickers_shutdown();

}  // namespace turbo
