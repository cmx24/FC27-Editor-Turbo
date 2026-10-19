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
#include "ui_player_page.h"
#include "ui_shell.h"
#include "ui_tactics_page.h"
#include "ui_looks_studio.h"

namespace turbo {

// Players: Edit a player. 2.19: the full editor (draw_players: the filters, the resizable list / rows / frames, the archetype attribute
// editor) is the page in both views, as before 2.16; the 2.16 card page (draw_player_page) is an opt-in ("Card view (beta)",
// gui.player_card_view). Nothing is deleted; Settings > Use the old pages still draws the old panel.
static void draw_player_edit(App& app) {
    if (ImGui::Checkbox("Card view (beta)###pcardview", &app.player_card_view)) app.save_gui_settings();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The 2.16 card page instead of the full editor (filters, resizable list, archetype editor)");
    if (app.player_card_view) draw_player_page(app);
    else draw_players(app);
}
TURBO_REGISTER_NEW_PAGE(shell::Page::PlayerEdit, draw_player_edit);

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

// Faces & looks > Looks studio (2.17 looks/studio-ui): a page that only exists in the new navigation, registered the ordinary way so the
// old-panels switch keeps it
TURBO_REGISTER_PAGE(shell::Page::LooksStudio, looks::draw_looks_studio);

}  // namespace turbo
