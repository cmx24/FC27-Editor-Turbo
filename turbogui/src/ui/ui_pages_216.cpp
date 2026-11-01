// FC 27 LE Turbo GUI - the 2.16 pages wired into the navigation (TURBO_GUI_REVAMP_PLAN.md section 6, PITWALL\wiring_216.md): one entry per
// page, calling the page builders' draw function. They go into the registry of the NEW pages (ui_shell.h), which App::draw_pages asks first;
// the pages registered before 2.16 stay where they were.
#include "app.h"
#include "imgui.h"
#include "ui_career_pages.h"
#include "ui_changes.h"
#include "ui_kits_page.h"
#include "ui_player_creation.h"
#include "ui_bulk_edit.h"
#include "ui_player_header.h"
#include "ui_player_profile.h"
#include "ui_team_pages.h"
#include "ui_shell.h"
#include "ui_tactics_page.h"
#include "ui_looks_studio.h"
#include "ui_split.h"
#include "ui_theme.h"

namespace turbo {

// (2.22: the Populate panel moved into the "Player type & tier" card, the first card of the Positions tab: ui_player_profile.cpp)

// Players > Player. 2.20: one page with tabs, as the Pit wall decided (13:02). 3.0 (brief 3.0-shell, track B): a read-only header card for the
// selected player above the tabs (ui_player_header.cpp) and four tabs, ids kept: Profile | Attributes & roles | Looks | Callname. Turbo X
// (2026-10-31): the header is the hero header and the tab bar a segmented control (theme::segmented); the bodies are drawn as before.
// The first two are the new pages (ui_player_profile.cpp), Looks is the Appearance Editor itself for the selected player, and
// Callname is the callname editor (callname_editor: list, Preview, Apply, Undo) for the selected player. shell::player_edit_tab(): 0..3.
static void draw_player_callname_tab(App& app) {
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!t || !p) {
        theme::muted_text("Pick a player (Profile tab) to choose what the commentator says for him.");
        return;
    }
    if (!app.db.table_alive(*t, p->rec)) {
        theme::err_text("The game data changed (another career was loaded?). Use Reload from game in Settings.");
        return;
    }
    callname_editor(app, *t, *p);
}

static void draw_player_edit(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    // the Profile's "Open Team > Players" button goes to the new page. Set where the page is drawn (not at start-up) so that the Profile's own test, which draws
    // it without the shell, still sees its default (the squad page)
    profile_team_players_page() = shell::Page::TeamPlayers;

    draw_player_header(app);  // nothing when no player is picked

    // Turbo X (DESIGN_SPEC_X.md): a segmented control (pill buttons, the selected one accent-tinted) instead of the window-wide tab bar;
    // the four ids are unchanged, shell::player_edit_tab() (0..3) is the selection, set by code or by a click
    static const char* const kSegments[] = {"Profile###pe_profile", "Attributes & roles###pe_ability", "Looks###pe_appearance", "Callname###pe_callname"};
    static int s_shown = -1;  // the segment drawn last frame; one line in turbo_gui.log per change (the live check of the wiring reads which were really shown)
    int& want = shell::player_edit_tab();
    if (want < 0 || want > 3) want = 0;
    ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
    theme::segmented(kSegments, 4, want);
    ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

    // Persistent Left Pane: 320px finder with quick actions across ALL 4 tabs
    if (split::pane_x("##select", 320.0f)) {  // X 2.0 layout: false = the person folded the list away (a ">" strip shows it again)
        if (ImGui::Button("+ Create###pf_create")) app.go(shell::Page::CreatePlayer);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create or clone a player");
        ImGui::SameLine();
        if (ImGui::Button("Bulk edit###pf_bulk")) app.go(shell::Page::BulkEdit);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Edit multiple players at once");
        ImGui::Spacing();
        draw_classic_player_list(app);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Editor for the selected tab
    ImGui::BeginChild("##editor", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!p) {
        draw_player_empty_editor(app);
    } else {
        const int now = want;
        switch (now) {
            case 0: draw_player_profile_editor(app); break;
            case 1: draw_player_ability_editor(app); break;
            case 2: looks::draw_looks_studio_embedded(app); break;
            default: draw_player_callname_tab(app); break;
        }
        if (now != s_shown) {
            static const char* const names[] = {"Profile", "Attributes & roles", "Looks", "Callname"};
            app.log(std::string("Edit a player tab: ") + names[now]);
        }
        s_shown = now;
    }
    ImGui::EndChild();
}
TURBO_REGISTER_NEW_PAGE(shell::Page::PlayerEdit, draw_player_edit);

// Players > Player creation (2.20 ia-creation): create / clone / import / export / delete in one page.
TURBO_REGISTER_NEW_PAGE(shell::Page::CreatePlayer, draw_player_creation);

// Players > Bulk edit (2.20 ia-bulk): any field of many players at once.
TURBO_REGISTER_NEW_PAGE(shell::Page::BulkEdit, draw_bulk_edit);

// Team > Players and Team > Transfers (2.20 ia-team): the career side of a club. The Profile's "Open Team > Players" button is pointed at the first in draw_player_edit.
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamPlayers, draw_team_players);
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamTransfers, draw_team_transfers);

// Tactics
TURBO_REGISTER_NEW_PAGE(shell::Page::TacticPresets, draw_tactics_presets_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamSheet, draw_team_sheet_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::GameplayPresets, draw_game_sliders_page);

// Career and Players (Managers & staff)
TURBO_REGISTER_NEW_PAGE(shell::Page::Transfers, draw_career_transfers_money);
TURBO_REGISTER_NEW_PAGE(shell::Page::JobOffer, draw_career_manager_jobs);
TURBO_REGISTER_NEW_PAGE(shell::Page::Staff, draw_career_staff);

// Kits & club: the tidy Kits page, Club name / colours / crest. Stadium & extras keeps its registration in ui_club_page.cpp.
TURBO_REGISTER_NEW_PAGE(shell::Page::Kits, draw_kits_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::Identity, draw_club_identity_page);

// Career > Changes & undo (App::tick calls changes::poll, the top bar has the Undo button and the count)
TURBO_REGISTER_NEW_PAGE(shell::Page::ChangesUndo, changes::draw_changes_page);

// Appearance Editor = the Looks studio (2.17 looks/studio-ui; 2.20 renamed): a page that only exists in the new navigation, registered the ordinary way
TURBO_REGISTER_PAGE(shell::Page::LooksStudio, looks::draw_looks_studio);

}  // namespace turbo
