// FC 27 LE Turbo GUI - Club & Kits area (2.13 GUI revamp, track T3; GUI_REVAMP_SPEC.md 3.3): the pages Kits, Identity and
// Stadium & extras, registered with the shell from ui_club_page.cpp. The sections below are what the pages and the legacy
// Teams tabs (until the legacy tab bar goes) draw: each wraps one proven editor (ui_identity.h / ui_kits.h), so every
// editor keeps exactly one call site. Chrome only: no data logic here.
#pragma once
#include <cstdint>

#include "ui_shell.h"

namespace turbo {

class App;

// The club the Club & Kits pages work on: App::sel_team (shared with the Teams tab), the user's club when none is chosen
// yet; 0 when there is no club to show
int64_t club_page_team(App& app);

// Sections (they read the club from App::sel_team; "club_page_team" has filled it before). Nothing is drawn but a hint when
// the club or its table is not there.
void club_name_section(App& app);
void club_crest_section(App& app);
// The club's spoken name in matches: another club's recorded name or none (ui_club_voice.cpp, core/club_voice.h)
void club_voice_section(App& app);
// The club's colours. `legacy` true (the Teams > Colours tab) also draws the stadium colours and the kit colours below
// them, as that tab always did; the Identity page draws the club colours only (stadium: Stadium & extras, kits: Kits).
void club_colours_section(App& app, bool legacy);
void club_stadium_section(App& app);                    // stadium colours and goal net
void club_kit_colours_section(App& app, int64_t teamid);  // kit colours, fonts, name layout of every kit of the club

// A request to open a page of this area from a link (the club-tools "Kits" link): the shell takes it and navigates
// (App::go once the shell has it). Page::None = nothing asked.
void club_request_page(shell::Page p);
shell::Page club_take_page_request();

// What the last Identity page frame drew (plain text is not an ImGui item, so the tests read it here)
struct ClubPageState {
    int64_t teamid = 0;      // the club the page showed
    bool wide = false;       // Identity: name + colours | crest side by side
};
const ClubPageState& club_page_state();

}  // namespace turbo
