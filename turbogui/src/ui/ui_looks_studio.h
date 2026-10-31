// FC 27 LE Turbo GUI - Faces & looks > Looks studio (brief 2.17-looks-studio, track U; route research_20261017\FULLBODY_FAST_ROUTE.md
// point 6). One page for players AND managers: [person list | large preview | cards]. The cards call what already exists:
//   Face          the real-face chooser (ui_images.h real_face_picker, ui_faces.h manager_real_face_picker)
//   Hair & beard  the faces card of ui_faces3d_gate.h (read-only use of its API)
//   Body type     a grid, one tile per bodytypecode: a figure drawn in code (ui_body_figure.h, 5.0c E8-body) over its plain name
//                 ("Tall, lean", "Like Lionel Messi", "Manager, tall, slim"; "His own body" where nothing names the code). Body-type edits
//                 and the page's Undo are also written to turbo_gui.log.
//   Accessories   accessory slots, boots, goalkeeper gloves   } the fields, titles and value words of track F's core/looks_fields_data.h
//   Kit fit       sleeves, shirt, shorts, socks (players)     }   (managers: their outfit)
// Every change goes through the existing edit path (App::edit via the page's combos / apply_bodytype): a player's change is one step of
// App's per-player Undo; a manager's (the manager table has no App undo) is a step of the page's own Undo, written back through the
// same edit path. The real face of a manager cannot be undone: the page asks first. Hair & beard keeps its own way back ("His own
// hair & beard"). No codes or ids on screen (hover only).
//
// The preview pane draws from a PreviewSource:
//   CapturePreview  the game's picture through the capture service Turbo already ships, with track C's capture::body_request (the
//                   default descriptor, never the learned one), one object per camera: "Full body" (capture::kCameraBodyFull, W2228:
//                   256 x 256, head to boots in kit; the page's default, Cassio 22:41) and "Head and shoulders" (camera 1, W2202:
//                   1024 x 1024). A switch picks the view. Asked by "Draw him", when the view is switched to one with no picture of him
//                   yet, and whenever his look changes while the page is shown; never while another picture is in flight, never by
//                   itself for a player's body made for another head. Managers by their head id: their full body is not tested yet.
//   GameScreenHint  before the first picture: what "Draw him" does.
#pragma once
#include <cstdint>
#include <memory>
#include <set>
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

// 5.0b-A: the Look card of one person (the registry's rows: ui_visual_picker.cpp vp::look_rows; a fold "All fields" with every look column typed).
// The studio draws it in its cards pane; the Manager tab of the Player page calls it for a manager (the integrator wires that call).
// `rec` is the record of the person in his table (players / manager); nothing is drawn for nobody.
void look_card(App& app, const Subject& who, uint64_t rec);

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
std::unique_ptr<PreviewSource> make_capture_preview(int camera);  // capture::kCameraBodyFull or kCameraBody
std::unique_ptr<PreviewSource> make_game_screen_hint();

// The head-and-shoulders camera (capture::cameras() index 1, "Large slice (mode 1, extra 0)" = track C's capture::kCameraBody)
constexpr int kCameraBody = 1;

// ---- the body-type grid (pure: tests call them)
// The tile text: track F's name of a generic body ("Tall, lean") or of a manager body ("Manager body 82"), "Like <the best-rated
// player with that body>" for a player's own body, else "Unique body". A player's tile never shows a number.
std::string body_caption(int64_t code, const bodytype::Catalog* cat, bool manager = false);
// The codes the grid offers. Players: the generic bodies first, then the players' own bodies by name (appearance_labels_data.h, the
// probed codes with example players, track F's census); a body only the other gender has is left out (gender -1: none left out).
// Managers: their own set (track F). `current` is always offered.
std::vector<int64_t> body_grid_codes(const bodytype::Catalog* cat, int64_t current, int gender, bool manager = false);
// 5.0c RV4: off (the default, and at every Body type window open) = manager bodies that draw the same figure show as one tile with a
// plain name; on ("Show all body types", saved in gui_settings.json gui.body_show_all) = every code of the set, with "(n of m)" names (nothing is lost: each code stays reachable).
void body_show_all(bool on);
bool body_show_all();
// 5.0c E8: the old modal's Generic / Specific filter. Generic = a body type something names (the game's Live Editor names, field_labels, the probe);
// Specific = the body model of an individual player (a manager's unnamed own style counts as specific). `generic` is body_generic_codes() of the
// same catalog, computed once per list.
std::set<int64_t> body_generic_codes(const bodytype::Catalog* cat);
bool body_is_specific(int64_t code, const bodytype::Catalog* cat, bool manager, const std::set<int64_t>& generic);

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
    int thumbs = 0;            // generator pictures shown in a tile tooltip (hover; the grid itself is figures)
    int figures = 0;           // of them drawn as a figure (ui_body_figure.h): body_tiles minus fc_pictures
    int fc_pictures = 0;       // Turbo X: of them drawn as FC Editor's body render (ui_fcedit_pictures.h) in place of the figure
    int fields = 0;            // field editors in Accessories and Kit fit
    int look_cards = 0;        // 5.0b-A: Look cards drawn this frame (the registry's card: ONE)
    int look_rows = 0;         // 5.0b-A: picker rows the Look card lists (one per registry column of the person, minus the dedicated cards'); counted when the card is out of view too
    int look_all = 0;          // 5.0b-A: editors of the "All fields" fold drawn this frame (0 while it is closed)
    int cards = 0;             // cards drawn
    bool tattoo_card = false;  // 2.22: the Tattoos card (tattoo_editor, players) was drawn this frame
    bool miniface_card = false;  // 2.22: the Player / Manager miniface card (the whole miniface_editor) was drawn this frame
    std::string body_result;   // what the last body-type click said
    bool asking = false;       // the "can't be undone" question is open
    bool risky_body = false;   // his body was made for another kind of head: no picture is asked by itself
    bool full_view = false;    // the switch is on "Full body"
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
void looks_studio_open_all_fields_for_tests(bool open);   // 5.0b-A: the Look card's "All fields" fold opens on its own (a test cannot scroll to it)

}  // namespace looks
}  // namespace turbo
