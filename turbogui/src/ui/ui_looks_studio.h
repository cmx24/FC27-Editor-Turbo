// FC 27 LE Turbo GUI - Faces & looks > Looks studio (brief 2.17-looks-studio, track U; route research_20261017\FULLBODY_FAST_ROUTE.md
// point 6). One page for players AND managers: [person list | large preview | cards]. The cards call what already exists:
//   Face          the real-face chooser (ui_images.h real_face_picker, ui_faces.h manager_real_face_picker)
//   Hair & beard  the faces card of ui_faces3d_gate.h (read-only use of its API)
//   Body type     a grid, one tile per bodytypecode, by name ("Tall, lean", "Like Lionel Messi", "Manager body 82"); no pictures in 2.17.0
//   Accessories   accessory slots, boots, goalkeeper gloves   } the fields, titles and value words of track F's core/looks_fields_data.h
//   Kit fit       sleeves, shirt, shorts, socks (players)     }   (managers: their outfit)
// Every change goes through the existing edit path (App::edit via the page's combos / apply_bodytype): a player's change is one step of
// App's per-player Undo; a manager's (the manager table has no App undo) is a step of the page's own Undo, written back through the
// same edit path. The real face of a manager cannot be undone: the page asks first. Hair & beard keeps its own way back ("His own
// hair & beard"). No codes or ids on screen (hover only).
//
// The preview pane draws from a PreviewSource:
//   CapturePreview  the game's picture through the capture service Turbo already ships, camera 1 ("Large slice (mode 1, extra 0)",
//                   the learned descriptor off): in game a 1024 x 1024 HEAD-AND-SHOULDERS picture, players and managers alike (Tester
//                   2026-10-17 22:09), so a line under it says that body, boots and kit fit don't show and where the game shows him
//                   in full. Asked by "Draw him" and again whenever his look changes while the page is shown; never while another
//                   picture is in flight, never by itself for a player's body made for another head.
//   GameScreenHint  before the first picture: his small picture from the game files and what "Draw him" does.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace turbo {

class App;
namespace bodytype {
class Catalog;
}

namespace looks {

// The person the studio shows
struct Subject {
    bool manager = false;
    int64_t id = 0;     // playerid or managerid (0 = nobody picked)
    int64_t club = 0;   // his club (0 = none)
    int64_t head = 0;   // headassetid (managers' pictures)
    int gender = -1;    // players.gender (0 men, 1 women); -1 unknown (managers)
    std::string name;
    bool valid() const { return id > 0; }
};

// Where the large preview comes from. The page asks the sources in order and draws the first one that is ready.
class PreviewSource {
public:
    virtual ~PreviewSource() = default;
    virtual const char* title() const = 0;  // plain words for the line under the preview
    // True when this source can show `s` now (a picture of him, or one being drawn); *why says what is missing, in plain words
    virtual bool ready(App& app, const Subject& s, std::string* why) = 0;
    // Draw into a w x h box at the cursor (the caller reserves the space)
    virtual void draw(App& app, const Subject& s, float w, float h) = 0;
    // His look changed, or "Draw again": ask for a fresh picture. False (and *why) when it cannot now.
    virtual bool refresh(App&, const Subject&, std::string* why) {
        if (why) *why = "This view has no picture to draw.";
        return false;
    }
    // Every frame: collect an answer in flight. Never waits.
    virtual void tick(App&) {}
};
std::unique_ptr<PreviewSource> make_capture_preview();
std::unique_ptr<PreviewSource> make_game_screen_hint();

// The capture camera (index into capture::cameras(): "Large slice (mode 1, extra 0)"); track C's capture::kCameraBody replaces it
constexpr int kCameraBody = 1;

// ---- the body-type grid (pure: tests call them)
// The tile text: track F's name of a generic body ("Tall, lean") or of a manager body ("Manager body 82"), "Like <the best-rated
// player with that body>" for a player's own body, else "Unique body". A player's tile never shows a number.
std::string body_caption(int64_t code, const bodytype::Catalog* cat, bool manager = false);
// The codes the grid offers. Players: the generic bodies first, then the players' own bodies by name (appearance_labels_data.h, the
// probed codes with example players, track F's census); a body only the other gender has is left out (gender -1: none left out).
// Managers: their own set (track F). `current` is always offered.
std::vector<int64_t> body_grid_codes(const bodytype::Catalog* cat, int64_t current, int gender, bool manager = false);

// ---- Accessories and Kit fit: the fields each card edits for a player or a manager (the table's own field names)
std::vector<std::string> card_fields(bool manager, bool kit_fit);

// What the last frame of the page drew (plain text is not an ImGui item, so the tests read it here)
struct StudioState {
    Subject subject;           // the person shown
    std::string source;        // title of the preview source drawn
    std::string preview_line;  // the line under the preview
    bool picture = false;      // a picture of him was drawn
    bool waiting = false;      // a picture is being drawn
    int requests = 0;          // pictures the page asked the game for since the start (or the test reset)
    std::string undo_label;    // "Undo: Body type" ("" = nothing to undo)
    int listed = 0;            // people in the list after the search
    int body_tiles = 0;        // tiles of the body-type grid drawn
    int fields = 0;            // field editors in Accessories and Kit fit
    int cards = 0;             // cards drawn
    std::string body_result;   // what the last body-type click said
    bool asking = false;       // the "can't be undone" question is open
    bool risky_body = false;   // his body was made for another kind of head: no picture is asked by itself
};
const StudioState& studio_state();

// Every frame: collects the preview picture in flight. The page calls it too; REQUIRED wiring for the release: App::draw calls it
// next to tick_faces3d_preview, so that an answer that arrives after the person left the page frees the capture service at once.
void tick_looks_studio(App& app);
// The page (registered as Faces & looks > Looks studio)
void draw_looks_studio(App& app);

// ---- tests
void looks_studio_reset_for_tests();                  // forget the list, the picture, the Undo steps, the question
void looks_studio_set_timeout_for_tests(double seconds);  // the picture timeout (25 s)
void looks_studio_show_managers(bool managers);       // the list's Players / Managers switch

}  // namespace looks
}  // namespace turbo
