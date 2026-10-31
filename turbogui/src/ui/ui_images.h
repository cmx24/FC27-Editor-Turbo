// FC 27 LE Turbo GUI - picture panels (ui_images.cpp)
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/career_faces.h"
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
    // 5.0b-C: the record whose picture this is, so the editor can draw the Body type row beside the picture (players = managers);
    // nullptr = no row (a caller without the record)
    const Table* table = nullptr;
    uint64_t rec = 0;
};

// 5.0b-C (Cassio 10-29: "assigning of minifaces should be a one-click"): a click on a source (catalog, a player's or a
// manager's miniface, head model / youth face, an image file, the 3D result) writes the miniface at once in a career,
// through App::write_face_picture (that career's own copy). Where a miniface goes and what Undo / Restore original use:
struct MinifaceWritePlan {
    std::filesystem::path path;      // the file written: careers\<id>\heads(_staff)\... in a career, else the shared mods\legacy one
    std::string rel;                 // its legacy path (data/ui/imgAssets/heads/p<id>.dds or heads_staff/heads_staff_<managerid>.dds)
    bool career_scoped = false;      // true: the file is that career's own copy (turbo_output\careers\<id>\)
    std::filesystem::path undo_dir;  // where the previous copies are kept for Undo / Restore original (careers\<id>\_undo); empty outside a career
    int size = 0;                    // 256 (players) or 512 (managers)
};
MinifaceWritePlan miniface_write_plan(const career_faces::Paths& paths, bool manager, int64_t id, int64_t headassetid,
                                      const std::string& career);

// What the tests read of a miniface editor (the editors themselves are file-local statics)
#define TURBO_MINIFACE_PROBE 1
struct MinifaceEditorProbe {
    uint64_t version = 0;             // bumps on every change of the editor (source, framing, background, capture fields)
    bool remove_bg = false;           // the "Remove plain background" box
    bool has_source = false;          // a picture is chosen
    int capture_id = 0;               // the "Game id" of the 3D model tab
    int auto_writes = 0;              // files written by a click on a source in this editor session
    int rewrites = 0;                 // re-writes after framing changes (0.6 s after the last one)
    std::string written_path;         // the last file written (full path)
    std::string prev_path;            // the copy Undo brings back (kept in the career's _undo; empty: Undo removes the file)
    size_t undo_steps = 0;            // sources assigned in this session and not undone
    bool original_available = false;  // "Restore original" is offered
    int auto_requests = 0;            // 5.0c E9: renders asked by a look change in this editor session
    bool auto_paused = false;         // a failed / lost auto render paused the auto update (Generate lifts it)
    float fr_zoom = 1.0f;             // the framing's Size
    bool capture_id_typed = false;    // 5.0c E9 R6: the Game id was typed (it no longer follows the person's head id)
};
MinifaceEditorProbe miniface_editor_probe(bool manager);
// The capture fields of the 3D model tab; each real change counts as a change of the editor (the preview and the file follow)
enum class MinifaceCaptureParam { GameId, Camera, UseTemplate, Advanced, Mode, Extra };
void miniface_set_capture_param(bool manager, MinifaceCaptureParam p, int value);
// 5.0c E9: forget which look each miniface was rendered for (a test starts from "nothing rendered"); the deadline (seconds) an auto render may take
// before the editor gives up on it and pauses the auto update (0 = the default)
#define TURBO_MINIFACE_AUTO 1  // the 5.0c E9 test hooks below exist (a tree without them does not define it: the tests then fail at run time)
void miniface_forget_looks();
// the career's saved "rendered look" record (careers\<id>\miniface_looks.txt); tests wipe it between cases
std::filesystem::path miniface_looks_file(const App& app);
void miniface_set_capture_timeout(double seconds);

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
// TURBO-X step 7 (2026-10-31): the two below have NO caller left in the UI (the one picker of ui_visual_picker.h draws their rows);
// they are deleted after the in-game proof. Tattoo per body area with preview and picker
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
// ---- 2.22 merged pickers (ui_visual_picker.cpp): the game gallery of a field behind the one grid. The catalog rules stay here.
// Fields with a game gallery: players' hair, facial hair, boots, small-sided boots, GK gloves, accessories 1-4, the seven tattoo
// fields (step 7, 2026-10-31: the save's tattoo table + the game's tattoo pictures + the catalog's tattoo family); managers' hair,
// facial hair and outfit (step 7: the outfit too).
struct GalleryEntry {
    int64_t id = 0;
    std::string preview;      // the game's picture path ("" = none: 3D only, or not listed)
    std::string name;         // readable name from the game files ("" = none)
    bool model_only = false;  // the game has the 3D model and no preview picture
    bool writable = true;
    std::string why;          // why it cannot be applied ("" = it can)
    bool hidden = false;      // in the game files, no row of the save uses it
    std::string tag;          // "3D only", "Hidden", "Preview only", "Not checked" or "" (data state; the one picker no longer shows it)
    std::string tip;          // the catalog's extra hover lines (data state; the one picker no longer shows them)
    uint32_t areas = 0;       // tattoos: the body areas the save's tattoo table makes it for (bit per tattoo_area_index); 0 = not in the table
    bool in_table = false;    // tattoos: the save's tattoo table has the id
};
// Tattoos: the index (0..6: head, front, back, left arm, right arm, left leg, right leg) of a tattoo field, -1 for any other field
int tattoo_area_index(const std::string& field);
bool gallery_has(const std::string& field, bool manager = false);
const char* gallery_title(const std::string& field, bool manager = false);  // "Hair", "Facial hair", "Boots" ... ("" = none)
// Every id the game lists for the field (ascending, no 0), with the catalog's flags; colour: the player's accessory colour
std::vector<GalleryEntry> gallery_entries(App& app, const std::string& field, bool manager, int64_t colour = 0);
bool gallery_catalog_active(const std::string& field, bool manager);
// May `id` be written (the catalog gate; 0 = none always; no catalog = yes)? Ids the catalog does not list are refused with a reason.
bool gallery_write_ok(App& app, const std::string& field, bool manager, int64_t id, std::string* why);
// Is `id` Hidden now (call before the write; after it the save uses the id)? Then log it (turbo_output\asset_writes.log) after the write.
bool gallery_hidden(App& app, const std::string& field, bool manager, int64_t id);
void gallery_log_hidden(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t old_v, int64_t new_v);
void gallery_stale_line(const std::string& field, bool manager);  // the "catalog built for another game build" lines, when stale
// "All / In this save / Hidden" combo (persisted per field): the scope 0..2, or -1 when the field has no catalog
int gallery_scope_combo(App& app, const std::string& field, bool manager, float width);
bool gallery_scope_shows(int scope, bool hidden);  // scope -1 shows everything
bool gallery_favourite(App& app, const std::string& field, int64_t id);
void gallery_toggle_favourite(App& app, const std::string& field, int64_t id);
// The game asset catalog the gear galleries and tattoo pickers read (docs/asset-catalog.md). The app sets it after every
// catalog load or switch change; nullptr (catalog off, not loaded, refused) = every gallery behaves exactly as in 2.0.
void set_gear_catalog(std::shared_ptr<const assets::Catalog> catalog);
// Gear code combos (widgets.cpp): "Hidden", "preview only", "not in the game files" for a players gear code the catalog
// knows; "" otherwise (no catalog, other tables and fields, ids the save uses)
std::string gear_code_note(App& app, const Table& t, const std::string& field, int64_t id);
// Status tab: picture cache
void images_status(App& app);

}  // namespace turbo
