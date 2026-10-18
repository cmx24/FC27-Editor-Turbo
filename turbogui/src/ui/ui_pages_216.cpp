// FC 27 LE Turbo GUI - the 2.16 pages wired into the navigation (TURBO_GUI_REVAMP_PLAN.md section 6, PITWALL\wiring_216.md): one entry per
// page, calling the page builders' draw function. They go into the registry of the NEW pages (ui_shell.h), which App::draw_pages asks first;
// the pages registered before 2.16 and the legacy panels stay exactly where they were, and App::legacy_pages (Settings > Use the old pages)
// brings them back. Nothing is deleted.
#include "app.h"
#include "ui_career_pages.h"
#include "ui_diagnostics_page.h"
#include "ui_kits_page.h"
#include "ui_shell.h"
#include "ui_tactics_page.h"

namespace turbo {

// Tactics
TURBO_REGISTER_NEW_PAGE(shell::Page::TacticPresets, draw_tactics_presets_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::TeamSheet, draw_team_sheet_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::GameplayPresets, draw_game_sliders_page);

// Career and Players (My squad). Managers & staff (draw_career_staff) is NOT wired: its list and editor do file work in every frame (gui perf gate,
// 3.25 calls per frame); the old Managers & staff panel stays until page-career fixes it.
TURBO_REGISTER_NEW_PAGE(shell::Page::Transfers, draw_career_transfers_money);
TURBO_REGISTER_NEW_PAGE(shell::Page::JobOffer, draw_career_manager_jobs);
TURBO_REGISTER_NEW_PAGE(shell::Page::Squad, draw_career_my_squad);

// Kits & club: the tidy Kits page, Club name / colours / crest; the old Kits body with every developer control is a page of its own (Advanced).
// Stadium & extras keeps its registration in ui_club_page.cpp. Kit diagnostics only exists in the new navigation, so it is registered the
// ordinary way: the old-panels switch keeps it.
TURBO_REGISTER_NEW_PAGE(shell::Page::Kits, draw_kits_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::Identity, draw_club_identity_page);
TURBO_REGISTER_PAGE(shell::Page::AdvKitDiagnostics, draw_kit_diagnostics);

// Advanced: Diagnostics & logs, Data export
TURBO_REGISTER_NEW_PAGE(shell::Page::AdvDiagnostics, draw_diagnostics_page);
TURBO_REGISTER_NEW_PAGE(shell::Page::AdvExport, draw_data_export_page);

}  // namespace turbo
