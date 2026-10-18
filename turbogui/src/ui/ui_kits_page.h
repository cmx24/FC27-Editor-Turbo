// FC 27 LE Turbo GUI - Kits & club (GUI revamp plan step 7, brief 2.16-gui-pages track "kits"; TURBO_GUI_REVAMP_PLAN.md 5.3 and the mockup).
//
// The tidy pages of the Kits & club section, written as plain functions the navigation re-cut (gui/revamp-2) wires in. They are NOT
// registered with the shell here: Page::Kits and Page::Identity are registered by ui_club_page.cpp until the wiring builder swaps them
// for these functions (a second registration of a page is refused, and which one would win depends on static initialisation order).
//
//   draw_kits_page             Kits: which kit does my club wear? [slot list | editor], ONE main button "Apply kit". Preview, "In matches"
//                              (one 3D tick, the honest line, the share choices, the per-match option shown as "planned"), the kit maker's
//                              colours and numbers folded away, the team-folder import under More. No developer controls, no ids.
//   draw_club_identity_page    Club name, colours & crest: three cards (the Identity merge), each with its own Save, built from the
//                              public club_*_section functions of ui_club_page.h.
//   draw_kit_diagnostics       The old Teams > Kits body with every developer control (texture trees, watch only, hook line ...) for the
//                              wiring builder to put under Advanced > Kit diagnostics. Nothing is lost.
//
// Behaviour comes only from the existing functions of ui_kits.h (import, apply, restore, status words) and the public kitswap::Runtime.
#pragma once
#include <cstdint>
#include <string>

#include "core/kit_import.h"

namespace turbo {

class App;

void draw_kits_page(App& app);
void draw_club_identity_page(App& app);
void draw_kit_diagnostics(App& app);

// What the last Kits page frame drew (plain text is not an ImGui item, so the tests read it here)
struct KitsPageState {
    int64_t teamid = 0;        // the club the page showed
    kits::Slot slot = kits::Slot::Home;
    bool custom = false;       // the slot holds a kit Turbo imported
    std::string status;        // the Preview card's one status line
    bool pitch_tick = false;   // the 3D tick as drawn
    bool pitch_enabled = false;
    std::string pitch_line;    // the line under the 3D tick (the honest line, or why the tick is off)
    bool planned_shown = false;  // the per-match option, shown as "planned" and not selectable
    bool apply_enabled = false;  // the page's one main button
};
const KitsPageState& kits_page_state();

// The plain words of a slot's state in the slot list ("Custom", "Game's own")
const char* kit_slot_state_words(bool custom);

// The 3D tick's one explaining line, from the state it is in (pure: the tests and the page share it).
//   runtime  false = no game hooks this session; opted false = the 3D option is not switched on yet; pending = the switch is made
//   but the game has not been restarted; staged = the kit's pictures are ready; active_swap = the slot is served on the pitch
//   watch_only = the slot is set to look only (nothing is replaced)
std::string kit_pitch_line(bool runtime, bool opted, bool pending, bool staged, bool active_swap, bool watch_only);

}  // namespace turbo
