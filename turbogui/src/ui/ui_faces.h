// FC 27 LE Turbo GUI - the real-face chooser for players (Players > Appearance) and managers (Managers > Appearance):
// heads with their minifaces, filters (core/face_filter.h), sort orders, and the writes that give the head.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/asset_catalog.h"
#include "core/face_filter.h"
#include "core/model.h"
#include "ui_face_tile.h"
#include "ui_images.h"

namespace turbo {

class App;

// Popup id of the manager chooser (open it with ImGui::OpenPopup(kManagerFacePopup))
constexpr const char* kManagerFacePopup = "Choose a real face##mgr";

// Heads the chooser lists: every player with a head model (real faces flagged), and managers with a head model
const std::vector<faces::Face>& player_faces(App& app);
const std::vector<faces::Face>& manager_faces(App& app);

// ---- "Game heads" (game asset catalog, docs/asset-catalog.md): 3D heads the game ships that no player or manager of the
// save uses, listed with a "Hidden" badge and the All / In this save / Hidden filter, given through the write gate
// (core/game_heads.h; every write is logged in turbo_output\asset_writes.log).
// The app hands the chooser its catalog after every (re)load or swap (nullptr: none, the chooser behaves as in 2.0.x).
// Call from the UI thread; the chooser copies what it needs, so the catalog may be dropped afterwards.
void set_faces_catalog(std::shared_ptr<const assets::Catalog> catalog);
// Hidden game heads as chooser rows (Face::game_head, id = headassetid), rebuilt when the save or the catalog changes
const std::vector<faces::Face>& game_faces(App& app);
// Give a game head to a player (players table) or a manager (manager table): gate, writes, write log, and for players
// with o.miniface the game's miniface p<head> copied to the player's when the catalog lists it
// The head's picture (its miniface p<head>, else its legend picture) comes with it as his own miniface (players p<id>,
// managers heads_staff_<managerid>): nothing is written while it is loading; *in_step false when it could not come.
bool apply_game_head(App& app, int64_t head_id, int64_t target_id, bool target_is_manager, const RealFaceOptions& o, std::string* msg,
                     bool* in_step = nullptr);
// Name-matched game heads (managers' own heads shipped under another id) as chooser rows: Face::game_head with
// match_manager / match_label set; rebuilt when the save or the catalog changes
const std::vector<faces::Face>& manager_match_faces(App& app);

// 5.0c E7 test hook: what the chooser drew for each tile in its last frame (picture source, chips and their geometry). Off by
// default (nothing recorded); the native test switches it on. UI thread only.
struct FaceTileDebug {
    int64_t id = 0;
    bool game_head = false, manager = false;
    int source = -1;  // face_tile::Source
    std::string why;  // the whole reason of an unavailable tile
    float cell = 0.0f;
    std::vector<face_tile::BadgeKind> chips;
    std::vector<face_tile::BadgeRect> rects;  // cell coordinates
    std::vector<float> drawn_w;               // the drawn label's width per rect (real font)
};
void face_tile_debug_enable(bool on);
const std::vector<FaceTileDebug>& face_tile_debug();

// Managers > Appearance tab: current miniface and head, "Choose a real face...", the appearance fields
void manager_appearance(App& app, const Table& t, const ManagerRow& m);
void manager_real_face_picker(App& app, int64_t managerid);
// Give a manager the head of a player (owner_is_manager false) or of another manager: the head fields (and hair, beard,
// eyes, skin when asked) of the manager table, and the owner's picture as the manager's own heads_staff_<managerid>
// (512 x 512): nothing is written while it is loading; *in_step false when it could not come. Validated writes.
bool apply_real_face_to_manager(App& app, int64_t managerid, int64_t owner_id, bool owner_is_manager, const RealFaceOptions& o,
                                std::string* msg, bool* in_step = nullptr);

// 3D heads of the chooser (the game renders them through app.capture, up to 6 per request, cached in
// turbo_output/cache/faces3d): called every frame from App::draw, so "Render all filtered heads" goes on with the chooser
// closed. Never waits; sends only while the service says available and not busy.
void tick_faces3d(App& app);
bool faces3d_running();      // "Render all filtered heads" is running
std::string faces3d_line();  // "3D heads: 120 / 2779, 2 failed" while it runs, else ""
std::string faces3d_status();  // one line for the Status tab
void faces3d_stop();         // stop sending (the request in flight still finishes)
// Forget the chooser's cached 3D render of a head (turbo_output\cache\faces3d\p_<id>.dds / m_<id>.dds): after his look changed
void faces3d_forget_render(App& app, bool manager, int64_t id);

// 2.20 faces safety (Cassio 2026-10-19): before Turbo's first head change of a player / manager in a scope (the identified
// career, or the squad file outside a career) his own head fields are kept once in turbo_output\careers\<scope>\heads_own.json;
// "His own head" writes them back (and removes the picture Turbo made for him in that career). A head given to a player with a
// real miniface leaves it as it is; one with none gets a miniface rendered from his new 3D head (career only, by tick_faces3d).
bool has_own_head(App& app, bool manager, int64_t id);  // a record to put back (cached; no file work per frame)
bool restore_own_head(App& app, bool manager, int64_t id, std::string* msg);
// The linked head fields (2.20): a head comes with its class, quality, type, variation and skin tone (the chooser copies them
// from the head owner's row, a game head writes its gate set). A head id written alone leaves them as they were: the player
// pages show one line (head_link_banner) with "Apply the linked fields", the same give as the chooser (real miniface kept; his
// own head kept first from the edit history, never the typed id). head_link_offer: that head id, or 0 when nothing to link.
int64_t head_link_offer(App& app, int64_t playerid);
bool apply_linked_head_fields(App& app, int64_t playerid, std::string* msg);
void head_link_banner(App& app, const PlayerRow& p, const char* id_suffix);  // id_suffix "##..." (two different buttons)
void faces_safety_reset();      // drops the queued minifaces (tests)
size_t faces_safety_pending();  // minifaces queued or in flight

}  // namespace turbo
