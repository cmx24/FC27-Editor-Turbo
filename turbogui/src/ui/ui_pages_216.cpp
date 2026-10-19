// FC 27 LE Turbo GUI - the 2.16 pages wired into the navigation (TURBO_GUI_REVAMP_PLAN.md section 6, PITWALL\wiring_216.md): one entry per
// page, calling the page builders' draw function. They go into the registry of the NEW pages (ui_shell.h), which App::draw_pages asks first;
// the pages registered before 2.16 and the legacy panels stay exactly where they were, and App::legacy_pages (Settings > Use the old pages)
// brings them back. Nothing is deleted.
#include "app.h"
#include "imgui.h"
#include "ui_career_pages.h"
#include "ui_changes.h"
#include "ui_diagnostics_page.h"
#include "ui_kits_page.h"
#include "ui_player_creation.h"
#include "ui_bulk_edit.h"
#include "ui_player_page.h"
#include "ui_player_profile.h"
#include "ui_team_pages.h"
#include "ui_shell.h"
#include "ui_tactics_page.h"
#include "ui_looks_studio.h"

namespace turbo {

// R3 archetypes (2.20): the Populate panel of ui_archetype_populate.h, drawn under the Positions, attributes & PlayStyles tab. Weak, so this file
// links with or without that track's .cpp (null = nothing is drawn); the Create player entry (archetype_populate_create) lives in the creation form.
TURBO_WEAK void archetype_populate_panel(App& app, const Table& players, const PlayerRow& p);

// Players: Edit a player. 2.20: one page with four tabs, as the Pit wall decided (13:02): Profile | Positions, attributes & PlayStyles |
// Appearance | All fields. The first three are the new pages (ui_player_profile.cpp) and the Appearance Editor itself for the selected player;
// "All fields" is the full editor of 2.19 (draw_players: the filters, the resizable list / rows / frames, the archetype attribute editor),
// last, with the 2.16 card page as its opt-in ("Card view (beta)", gui.player_card_view). Nothing is deleted; Settings > Use the old pages
// still draws the old panel.
static void draw_player_edit(App& app) {
    // the Profile's "Open Team > Players" button goes to the new page. Set where the page is drawn (not at start-up) so that the Profile's own test, which draws
    // it without the shell, still sees its default (the squad page)
    profile_team_players_page() = shell::Page::TeamPlayers;
    if (!ImGui::BeginTabBar("##pe_tabs")) return;
    static int s_shown = -1;  // the tab the bar showed last frame; a different shell::player_edit_tab() (set by code) is selected once
    int& want = shell::player_edit_tab();
    const auto flag = [&](int i) { return (want != s_shown && want == i) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
    int now = s_shown < 0 ? want : s_shown;
    if (ImGui::BeginTabItem("Profile###pe_profile", nullptr, flag(0))) {
        now = 0;
        draw_player_profile(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Positions, attributes & PlayStyles###pe_ability", nullptr, flag(1))) {
        now = 1;
        draw_player_ability(app);
        if (archetype_populate_panel != nullptr && app.sel_player > 0) {
            const Table* pt = app.db.table("players");
            const PlayerRow* pr = app.model.player(app.sel_player);
            if (pt && pr) archetype_populate_panel(app, *pt, *pr);
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Appearance###pe_appearance", nullptr, flag(2))) {
        now = 2;
        looks::draw_looks_studio(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("All fields###pe_allfields", nullptr, flag(3))) {
        now = 3;
        if (ImGui::Checkbox("Card view (beta)###pcardview", &app.player_card_view)) app.save_gui_settings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The 2.16 card page instead of the full editor (filters, resizable list, archetype editor)");
        if (app.player_card_view) draw_player_page(app);
        else draw_players(app);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    s_shown = want = now;
}
TURBO_REGISTER_NEW_PAGE(shell::Page::PlayerEdit, draw_player_edit);

// Players > Player creation (2.20 ia-creation): create / clone / import / export / delete in one page. The signpost of ui_nav_pages.cpp stays
// registered the old way and answers when Settings > Use the old pages is on.
TURBO_REGISTER_NEW_PAGE(shell::Page::CreatePlayer, draw_player_creation);

// Players > Bulk edit (2.20 ia-bulk): any field of many players at once. Advanced > Bulk edit (the old panel) stays registered as before.
TURBO_REGISTER_NEW_PAGE(shell::Page::BulkEdit, draw_bulk_edit);

// Team > Players and Team > Transfers (2.20 ia-team): the career side of a club. The Profile's "Open Team > Players" button is pointed at the first in draw_player_edit.
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamPlayers, draw_team_players);
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamTransfers, draw_team_transfers);

// Tactics
TURBO_REGISTER_NEW_PAGE(shell::Page::TacticPresets, draw_tactics_presets_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamSheet, draw_team_sheet_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::GameplayPresets, draw_game_sliders_page);

// Career and Players (My squad, Managers & staff)
TURBO_REGISTER_NEW_PAGE(shell::Page::Transfers, draw_career_transfers_money);
TURBO_REGISTER_NEW_PAGE(shell::Page::JobOffer, draw_career_manager_jobs);
TURBO_REGISTER_NEW_PAGE(shell::Page::Squad, draw_career_my_squad);
TURBO_REGISTER_NEW_PAGE(shell::Page::Staff, draw_career_staff);

// Kits & club: the tidy Kits page, Club name / colours / crest; the old Kits body with every developer control is a page of its own (Advanced).
// Stadium & extras keeps its registration in ui_club_page.cpp. Kit diagnostics only exists in the new navigation, so it is registered the
// ordinary way: the old-panels switch keeps it.
TURBO_REGISTER_NEW_PAGE(shell::Page::Kits, draw_kits_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::Identity, draw_club_identity_page);
TURBO_REGISTER_PAGE(shell::Page::AdvKitDiagnostics, draw_kit_diagnostics);

// Career > Changes & undo (App::tick calls changes::poll, the top bar has the Undo button and the count)
TURBO_REGISTER_NEW_PAGE(shell::Page::ChangesUndo, changes::draw_changes_page);

// Advanced: Diagnostics & logs, Data export
TURBO_REGISTER_NEW_PAGE(shell::Page::AdvDiagnostics, draw_diagnostics_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::AdvExport, draw_data_export_page);

// Appearance Editor = the Looks studio (2.17 looks/studio-ui; 2.20 renamed): a page that only exists in the new navigation, registered the ordinary way so the
// old-panels switch keeps it
TURBO_REGISTER_PAGE(shell::Page::LooksStudio, looks::draw_looks_studio);

}  // namespace turbo
