// FC 27 LE Turbo GUI - Teams > Kits: import a FIFA Kit Creator kit zip (fifakitcreator.com) into a club's kit slot.
//
// Phase 1 (2.3), each part with Undo:
//   1. the zip's settings.json into the club's teamkits row of that slot (kit colours through write_colour, the other
//      settings through App::edit), kept for every career load like Teams > Colours (core/reapply.h; kill switch
//      turbo_output\reapply_off.txt);
//   2. minikit.png as a custom legacy file (mods\legacy\data\ui\imgAssets\kits\j<type>_<teamid>_0.dds) in the game
//      file's own size / format / mips, the crest writer's way (backup in turbo_output\kit_backups);
//   3. the 24 textures converted to FC 27's sizes and formats (core/kit_import.h) and staged with manifest.json in
//      turbo_output\kits\<teamid>\<slot>\ on a worker thread. The game does not read them yet (phase 2: Turbo's
//      in-match texture swap).
// What was replaced is kept in turbo_output\kits\<teamid>\<slot>\import.json, so Undo works after a restart too.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/kit_import.h"
#include "core/legacy.h"

namespace turbo {

class App;

// The Kits tab of the Teams editor (ui_teams.cpp); teamid = the club selected on the left (the import's target)
void kits_editor(App& app, int64_t teamid);
// Where "Import kit zip..." starts: C:\FC_Tools\My Mods when it exists, else the Live Editor folder (the picker
// remembers the last folder under the key "kit_import")
std::filesystem::path kit_import_start_folder(App& app);

// The teamkits row of a club's slot (0 = none; the lowest teamkitid when there are several) and its teamkitid
uint64_t kit_row(App& app, int64_t teamid, kits::Slot s, int64_t* teamkitid = nullptr);
// The state of the game's own minikit for the slot (Waiting: asked from the game, not exported yet)
LegacyImages::State minikit_state(App& app, int64_t teamid, kits::Slot s);

struct KitApply {
    bool ok = false;          // database part and minikit done (written, or nothing the game has to write)
    std::string line;         // one line for the toast / panel
    int fields = 0;           // teamkits fields written
    std::vector<std::string> skipped;  // settings not written, with the reason
    bool minikit = false;     // minikit written
    std::string minikit_note; // why not, when not
};
// Parts 1 and 2 for one checked package (pk.ok) and the slot it goes to, plus the import record. Refused (ok false)
// while the game's minikit is still loading. A second import of the same slot keeps the first one's "before" values,
// so Undo goes back to the game's kit.
KitApply apply_kit_import(App& app, int64_t teamid, kits::Slot s, const kits::KitPackage& pk);
// Undo of an import: each teamkits field back where it still holds the imported value, the kept edits as they were
// before, the minikit as it was (the earlier custom file from the backup, or the game's own), the staged textures and
// the record deleted. false with the reason (nothing imported, textures still converting, a write refused).
bool undo_kit_import(App& app, int64_t teamid, kits::Slot s, std::string* msg);
bool has_kit_import(App& app, int64_t teamid, kits::Slot s);

// ---- the panel (tests drive it through these; the buttons call the same)
// Read a zip, or every kit zip of a team folder (kits::team_folder_zips), into the panel on a worker thread
bool kit_panel_open(App& app, const std::filesystem::path& zip_or_folder);
bool kit_panel_busy();                 // reading or converting
size_t kit_panel_count();              // kits in the panel
const kits::KitPackage* kit_panel_kit(size_t i);
kits::Slot kit_panel_slot(size_t i);   // the slot the kit goes to (from its file name, or picked)
void kit_panel_set_slot(size_t i, kits::Slot s);
std::string kit_panel_status(size_t i);   // "ready", the refusal, "applied ...", "slot taken by ..."
std::string kit_panel_line();             // the last Apply / Undo / conversion summary
// Textures of FC 27 (nullptr) or a smaller set the tests use; also what the conversion writes
void kit_panel_spec(const std::vector<kits::TexSpec>* spec);

}  // namespace turbo
