// FC 27 LE Turbo GUI - 4.0 S8: what the two Team pages (ui_team_players.cpp, ui_team_transfers.cpp) share to sit inside the page template
// (ui_page.h): the tail that carries the Team pages' status line into the template's one line and draws the question popup, and the
// declarations of the per-player controls the transfers file exports for the player card (q3, brief 4.0-gui-pages S8).
// Header-only (inline). No logic of its own: carry_status only moves the text g_status already holds into page::done.
#pragma once
#include <string>

#include "ui_page.h"
#include "ui_team_common.h"

namespace turbo {

// q3: ONE player's contract and transfer actions, for that player's card (ui_team_players.cpp). The player is App::sel_player. It draws the
// Contract fields, the contract extension and the move controls by calling the same functions the Team pages call, and the question popup
// those controls open. The page that hosts it carries the status line once per frame: teampage::carry_status("<its page id>") below.
void draw_player_contract_actions(App& app);

// One player's controls, the same functions the cards of Team > Transfers call (ui_team_transfers.cpp). `club` = the club the player is at.
// The player is App::sel_player (the pick Team > Players and Team > Transfers share).
void team_player_move_controls(App& app, int64_t club);    // Move a player: transfer, loan, release, the game's transfer and loan lists
void team_player_extend_controls(App& app, int64_t club);  // Contract extension of that one player

// The texts the My team preset card handed to the draw calls on its last frame (ui_team_players.cpp preset_strip): 0 = the hint line under
// the card title, 1 = the question the button opens. "" until the card has been drawn. The native tests read what was drawn through it.
const char* team_preset_drawn_text(int which);

namespace teampage {

// The Team cards write "Extended X's contract to Y." or "Asked: <what>." into g_status; the template shows ONE line, "Done: ...". The
// text is carried over once and g_status is emptied, so a status never shows twice.
inline void carry_status(const char* page_id) {
    if (g_status.empty()) return;
    const std::string s = g_status;
    g_status.clear();
    static const std::string kAsked = "Asked: ";
    if (s.compare(0, kAsked.size(), kAsked) == 0) page::done(page_id, "sent to the game: " + s.substr(kAsked.size()));
    else page::done(page_id, s);
}

// Declared AFTER the page::Frame: it is destroyed first, so the question popup and the status carry are drawn inside the template's body
// and the status line reads the carried text in the same frame. The old PageFrame's label column (150) is kept for the form rows.
struct PageTail {
    App& app;
    const char* id;
    PageTail(App& a, const char* page_id) : app(a), id(page_id) { form_label_width(S(150.0f)); }
    ~PageTail() {
        confirm_popup(app);
        carry_status(id);
        form_label_width(0.0f);
    }
    PageTail(const PageTail&) = delete;
    PageTail& operator=(const PageTail&) = delete;
};

}  // namespace teampage
}  // namespace turbo
