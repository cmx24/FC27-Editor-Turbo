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
namespace visual {
enum class FolderState : uint8_t;  // core/rdbm_resources.h: NotFound, FoundNoSets, Found
}

// The pickers of every mapped field the table has, grouped (Movement, Hair and face, Kit and boots, Tattoos), plus the body
// type grid; one row per field: its picture, its in-game name, "Choose...". Draws the picker window when one is open.
void visual_pickers_section(App& app, const Table& t, uint64_t rec, bool manager = false);
// One row only (a page that shows a single field), and the picker window when its field is the open one
void visual_field_row(App& app, const Table& t, uint64_t rec, const char* field, bool manager = false);
// Settings > Preferences: the RDBM26 folder line (set, default, what was found)
void visual_pictures_setting(App& app);
// 3.1 G2b: what the pictures folder is (NotFound / FoundNoSets = the folder exists but has no runstyle, hairstyle or fkanim folder /
// Found), and the one line that says why there are no pictures ("" when the sets are found; names the path and the three folders)
visual::FolderState visual_pictures_state(App& app);
std::string visual_pictures_note(App& app);

// Write `code` into `field` of the record through App::edit (bodytypecode: apply_bodytype). false with *msg when refused.
bool visual_apply(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t code, std::string* msg = nullptr);

// ---- 2.22: the one grid per field, and the inline field
// The grid of a field is ONE window whatever opens it: every tile is the RDBM26 picture (a colour as a swatch), else the game's own
// preview (the gallery of ui_images.cpp, for hair, beard, boots, gloves, accessories), else a grey "no picture yet" tile; every tile
// carries the in-game name. Fields with a catalog gate (hair and beard of players) refuse an id the game does not ship (visual_apply).
namespace vp {
// Has `field` of this table a picture, a swatch or a game preview to show (so an inline field is worth drawing)?
bool has_picture(App& app, const Table& t, const char* field, bool manager = false);
// The inline field: the current value's picture or swatch (56 px), its name, and a "Change" button that opens the grid. `label`
// (may be null) is drawn first. Draws the grid window too (same id scope as the button). true on the frame the grid wrote.
bool inline_field(App& app, const Table& t, uint64_t rec, const char* field, const char* label = nullptr, bool manager = false);
// The eyebrows, hair / beard / eye colours, accessory colours and generic head as inline fields (the Player page)
void inline_appearance(App& app, const Table& t, uint64_t rec, bool manager = false);
// Open the grid of `field` for the record (what "Change" does); the owner then calls window() every frame
void open(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager = false);
void window(App& app, const Table& t, uint64_t rec);
// The picture or swatch of the field's current value in side x side; false when RDBM26 has none for it (the caller shows its own)
bool draw_field_picture(App& app, const Table& t, uint64_t rec, const char* field, bool manager, float side);
// An INLINE grid of the field (not a window): tiles in the page, animated where RDBM26 has a GIF, a grey "no animation yet" /
// "no picture yet" tile where it has none, the current value marked, a click picks, "Apply" writes through visual_apply. `height`: the
// grid's height in pixels (it scrolls). true on the frame it wrote. The Set pieces card (ui_setpiece_card.cpp) is made of these.
bool inline_grid(App& app, const Table& t, uint64_t rec, const char* field, const char* title, float height, bool manager = false);
struct GridInfo {
    bool built = false;
    int codes = 0, tiles = 0, pictures = 0, without_picture = 0;  // list size; tiles / pictures drawn in the last frame; tiles with no picture
    int64_t selected = -1, current = -1;
};
GridInfo grid_info(const char* field);                  // tests
void grid_select(const char* field, int64_t code);      // what a click on a tile does
bool grid_apply(App& app, const Table& t, uint64_t rec, const char* field, std::string* msg = nullptr);  // what Apply does
}  // namespace vp

// ---- tests
// Where a tile's picture comes from (the merged grid)
enum class TileSource { Rdbm, Swatch, Game, Placeholder, NamesOnly };
struct VisualPickerUi {
    bool open = false;
    std::string field;
    int64_t selected = -1, current = -1, previewed = -1;
    int tiles_drawn = 0;        // tiles drawn in the last frame of the open picker
    int pictures_shown = 0;     // of those, tiles with their picture (animated or still) on screen
    int uploads_last_frame = 0; // animations given to the window's textures in the last frame
    bool names_only = false;    // no pictures folder: the list of names
    int source_rdbm = 0, source_swatch = 0, source_game = 0, source_blank = 0;  // the whole list, by where each tile's picture comes from
    std::string last_result;    // "Run style set to Medalist", or the refusal
    std::string pictures_line;  // 3.1 G2b: the "where are the pictures / why none" line the open grid shows in its note ("" when it shows none)
};
const VisualPickerUi& visual_picker_ui();
// Open the picker of `field` for the record on the next frame (what "Choose..." does)
void visual_picker_request(const std::string& field);
void visual_picker_select(int64_t code);   // what a click on a tile does
TileSource visual_picker_tile_source(int64_t code);  // of the open picker (NamesOnly: not in the list)
bool visual_picker_tile_writable(int64_t code, std::string* why = nullptr);  // the catalog gate, greyed tiles
size_t visual_picker_list_size();          // codes in the open picker, before search and filters
bool visual_picker_apply(App& app);        // what Apply does (on the record the picker was opened for)
void visual_picker_close();
// Look for the pictures folder again (after the setting changed); tests point it at a folder of their own
void visual_pictures_refresh(App& app);
void visual_pictures_override(const std::string& resources_dir);  // "" = back to the setting / default
// Stop the decoding thread and forget every animation (tests; the game never unloads Turbo, so the window never calls it)
void visual_pickers_shutdown();

}  // namespace turbo
