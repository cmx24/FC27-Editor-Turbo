// FC 27 LE Turbo GUI - the one page frame of the 4.0 GUI overhaul (S1; plan 0a/0b, skill TURBO OVERRIDE 2 and 3).
//
// A page that uses the template draws, in this order:
//     title                       (one line, title size)
//     help                        (optional, one grey line of at most 100 characters, rule R5)
//     body                        (the page's own widgets; a child window that scrolls, so the bar never leaves the screen)
//     status line                 (ONE line: "Done: <what>" or "Couldn't <what>: <why>", rule R6)
//     action bar                  [Apply]  [Undo: <what>]  [Back]
// Nothing here is new logic:
//   - Undo is the Changes list's global Undo (ui_changes.h: last_undoable / undo_label / undo_last). The frame keeps no undo store.
//   - Back is App::back() (the place the last navigation left, ui_shell.h back_place()). It is always drawn and is disabled, with a
//     tooltip, when the page was opened directly; it is never an Escape-only exit.
//   - Apply is optional (a page that stages nothing, like Data export, has none) and is returned to the page by end(); the page
//     runs its own existing function for it.
// Keyboard / gamepad order (explicit): the body's widgets in drawing order, then Apply, Undo, Back (Tab and the pad's next-item go
// the same way; the bar is the last stop). The focused bar button gets a 2 px outline, so focus is not shown by colour alone.
//
// Use:
//     void draw_x_page(turbo::App& app) {
//         turbo::page::Frame f(app, {"x_page", "Page title", "One grey help line.", nullptr});
//         ...widgets...
//         if (clicked) turbo::page::done("x_page", "did the thing");
//     }                                  // ~Frame draws the status line and the bar
// tests/lint_one_component.py and test_gui_labels.h treat every function that constructs a page::Frame as a TEMPLATE page and hold
// it to the stricter checks (no raw BeginTable, no own undo store, no "classic"/"old"/"new" label).
#pragma once
#include <string>
#include <vector>

namespace turbo {

class App;

namespace page {

struct Spec {
    const char* id;           // stable, unique per page: "data_export" (child window and status line key)
    const char* title;        // "Data export"
    const char* help;         // nullptr or one grey line, <= 100 characters
    const char* apply_label;  // nullptr = the page has no Apply; else <= 28 characters, e.g. "Apply changes"
    bool apply_enabled = true;
    // false = the bar has no Undo (4.0 S7b): for a page where the bar Undo would mislead. Bulk edit has its own Undo that restores
    // the whole run (the bar one takes back only the newest row of the Changes list: one player's edits); Player creation makes players, which no Undo step takes back.
    // Every other page keeps the bar Undo (the default).
    bool show_undo = true;
};

class Frame {
public:
    Frame(App& app, const Spec& spec);
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    // Draw the status line and the action bar and close the frame. Returns true on the frame Apply was clicked. Called by the
    // destructor when the page does not call it (then the result is dropped).
    bool end();

private:
    App& app_;
    Spec spec_;
    bool ended_ = false;
};

// The page's one status line. Replaces the previous text of that page. "Done: " / "Couldn't " are added here, so every page
// says it the same way (rule R6 and the copy deck).
void done(const char* page_id, const std::string& what);                              // "Done: <what>"
void couldnt(const char* page_id, const std::string& what, const std::string& why);   // "Couldn't <what>: <why>"
const std::string& status_text(const char* page_id);                                   // empty = nothing yet

// What the frame drew last (tests): the bar buttons in drawing order and whether each was enabled.
struct Record {
    std::string id, title;
    bool has_apply = false;
    std::vector<std::string> bar;  // stable ids in drawing order: "###pg_apply", "###pg_undo" (not when Spec::show_undo is false), "###pg_back"
    bool undo_enabled = false, back_enabled = false;
    int frame = -1;  // ImGui frame count it was drawn in
};
const Record* last_record(const char* page_id);
void reset_for_tests();

}  // namespace page
}  // namespace turbo
