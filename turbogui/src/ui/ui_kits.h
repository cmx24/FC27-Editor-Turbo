// FC 27 LE Turbo GUI - Teams > Kits: import a FIFA Kit Creator kit zip (fifakitcreator.com) into a club's kit slot.
//
// Phase 1 (2.3), each part with Undo:
//   1. the zip's settings.json into the club's teamkits row of that slot (kit colours through write_colour, the other
//      settings through App::edit), kept for every career load like Teams > Colours (core/reapply.h; kill switch
//      turbo_output\reapply_off.txt);
//   2. minikit.png as a custom legacy file (mods\legacy\data\ui\imgAssets\kits\j<type>_<teamid>_0.dds) in the game
//      file's own size / format / mips, the crest writer's way (backup in turbo_output\kit_backups);
//   3. the 24 textures converted to FC 27's sizes and formats (core/kit_import.h) and staged with manifest.json in
//      turbo_output\kits\<teamid>\<slot>\ on a worker thread. Turbo's in-match texture swap
//      (core/kit_swap.h, 2.6) serves them to the game; Undo switches it off.
// What was replaced is kept in turbo_output\kits\<teamid>\<slot>\import.json, so Undo works after a restart too.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "core/kit_import.h"
#include "core/legacy.h"

namespace turbo {
namespace kitswap {
struct Status;
}

class App;

// The Kits tab of the Teams editor (ui_teams.cpp); teamid = the club selected on the left (the import's target)
void kits_editor(App& app, int64_t teamid);

// 2.13 (GUI revamp): Club & Kits > Kits page. [slot list | editor] and ONE primary action, "Apply kit" (the ready kits of the
// selected slot, through the same apply_items as the Apply all button). The selected slot's imported record, its
// "Restore EA kit" (= undo_kit_import of that club + slot only: it never touches another club's or slot's kept rows) and the 3D
// controls sit in the editor. Chrome only: the import, the apply and the undo are the 2.3 / 2.6 functions below.
void kits_page(App& app, int64_t teamid);
struct KitSlotRow {
    kits::Slot slot = kits::Slot::Unknown;
    const char* label = "";   // "Home", "Away", "Third", "Goalkeeper", "Fourth"
    bool custom = false;      // imported by Turbo for this club (has_kit_import)
    std::string state;        // "custom *" or "EA"
};
// The slot list of the page, in the order it is drawn (Home, Away, Third, Goalkeeper, Fourth)
std::vector<KitSlotRow> kit_slot_rows(App& app, int64_t teamid);
kits::Slot kit_page_slot();               // the slot selected on the page (Home at start)
void kit_page_select(kits::Slot s);
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

// ---- 2.13: the 3D kit's status in plain words, from the runtime's per-kit-folder verdicts (core/kit_swap.h FolderVerdict).
// name(team id) = a club name (the model's, "Team <id>" as the fallback). Both are "" while the runtime has nothing for the slot.
// One line for a club's slot: "swapped", "not swapped: the shirt is shared with Aston Villa's GK kit; tick the box above to swap both", ...
std::string kit_swap_status_line(const kitswap::Status& st, int64_t teamid, int slot, const std::function<std::string(int64_t)>& name);
// The other clubs' kits that share a texture with the slot ("Aston Villa's GK kit")
std::string kit_swap_shared_clubs(const kitswap::Status& st, int64_t teamid, int slot, const std::function<std::string(int64_t)>& name);

}  // namespace turbo
