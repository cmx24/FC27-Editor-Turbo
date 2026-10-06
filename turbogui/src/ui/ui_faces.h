// FC 27 LE Turbo GUI - the real-face chooser for players (Players > Appearance) and managers (Managers > Appearance):
// heads with their minifaces, filters (core/face_filter.h), sort orders, and the writes that give the head.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/face_filter.h"
#include "core/model.h"
#include "ui_images.h"

namespace turbo {

class App;

// Popup id of the manager chooser (open it with ImGui::OpenPopup(kManagerFacePopup))
constexpr const char* kManagerFacePopup = "Choose a real face##mgr";

// Heads the chooser lists: every player with a head model (real faces flagged), and managers with a head model
const std::vector<faces::Face>& player_faces(App& app);
const std::vector<faces::Face>& manager_faces(App& app);

// Managers > Appearance tab: current miniface and head, "Choose a real face...", the appearance fields
void manager_appearance(App& app, const Table& t, const ManagerRow& m);
void manager_real_face_picker(App& app, int64_t managerid);
// Give a manager the head of a player (owner_is_manager false) or of another manager: the head fields (and hair, beard,
// eyes, skin when asked) of the manager table; with o.miniface a player's miniface is copied into the head's
// heads_staff picture (512 x 512), so the manager shows that face. Validated writes.
bool apply_real_face_to_manager(App& app, int64_t managerid, int64_t owner_id, bool owner_is_manager, const RealFaceOptions& o,
                                std::string* msg);

// 3D heads of the chooser (the game renders them through app.capture, up to 6 per request, cached in
// turbo_output/cache/faces3d): called every frame from App::draw, so "Render all filtered heads" goes on with the chooser
// closed. Never waits; sends only while the service says available and not busy.
void tick_faces3d(App& app);
bool faces3d_running();      // "Render all filtered heads" is running
std::string faces3d_line();  // "3D heads: 120 / 2779, 2 failed" while it runs, else ""
std::string faces3d_status();  // one line for the Status tab
void faces3d_stop();         // stop sending (the request in flight still finishes)

}  // namespace turbo
