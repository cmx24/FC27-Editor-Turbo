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
#include "ui_player_header.h"
#include "ui_player_profile.h"
#include "ui_team_pages.h"
#include "ui_shell.h"
#include "ui_tactics_page.h"
#include "ui_looks_studio.h"
#include "ui_theme.h"

namespace turbo {

// (2.22: the Populate panel moved into the "Player type & tier" card, the first card of the Positions tab: ui_player_profile.cpp)

// Players > Player. 2.20: one page with tabs, as the Pit wall decided (13:02). 3.0 (brief 3.0-shell, track B): a read-only header card for the
// selected player above the tab bar (ui_player_header.cpp) and five tabs, ids kept: Profile | Attributes & roles | Looks | Callname (NEW) |
// Everything (classic). The first two are the new pages (ui_player_profile.cpp), Looks is the Appearance Editor itself for the selected player,
// Callname is the Callnames page's right pane (callname_editor: list, Preview, Apply, Undo) for the selected player, and "Everything (classic)"
// is the full editor of 2.19 (draw_players: the filters, the resizable list / rows / frames, the archetype attribute editor), last, with the
// 2.16 card page as its opt-in ("Card view (beta)", gui.player_card_view). shell::player_edit_tab(): 0..4 (Callname = 3, classic = 4).
// Nothing is deleted; Settings > Use the old pages still draws the old panel.
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

// 4.0 menu preview (shell::tree40(), brief 4.0-gui-pages S3 + P2): the Players page is a Players / Managers switch. Players = the same card
// with the four 4.0 tabs: Profile | Positions, attributes & playstyles | Appearance | Player creation (the page functions are the ones the
// tabs above call; Callname and Everything (classic) are not in this tree). shell::player_edit_tab(): 0, 1, 2 as before, 5 = Player creation;
// 3 and 4 (the old Callname / classic tabs) show Profile. Managers = draw_managers (ui_teams.cpp), as far as it goes: its tabs are Details /
// Appearance / Miniface (a FEATURE GAP against "Profile + Appearance": the first is not called Profile, the third is extra).
// The card's contract actions (S8) are not here: draw_player_contract_actions does not exist on this base.
static void draw_player_card_40(App& app) {
    profile_team_players_page() = shell::Page::TeamPlayers;
    draw_player_header(app);
    if (!ImGui::BeginTabBar("##pe_tabs")) return;
    static int s_shown = -1;
    int& want = shell::player_edit_tab();
    if (want == 3 || want == 4) want = 0;
    const auto flag = [&](int i) { return (want != s_shown && want == i) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
    int now = s_shown < 0 ? want : s_shown;
    if (ImGui::BeginTabItem("Profile###pe40_profile", nullptr, flag(0))) {
        now = 0;
        draw_player_profile(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Positions, attributes & playstyles###pe40_ability", nullptr, flag(1))) {
        now = 1;
        draw_player_ability(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Appearance###pe40_appearance", nullptr, flag(2))) {
        now = 2;
        looks::draw_looks_studio(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Player creation###pe40_creation", nullptr, flag(5))) {
        now = 5;
        draw_player_creation(app);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    if (now != s_shown) {
        const char* name = now == 0 ? "Profile" : now == 1 ? "Positions, attributes & playstyles" : now == 2 ? "Appearance" : now == 5 ? "Player creation" : nullptr;
        if (name) app.log(std::string("Edit a player tab: ") + name);
    }
    s_shown = want = now;
}

static void draw_player_edit_40(App& app) {
    if (!ImGui::BeginTabBar("##pe_who")) return;
    if (ImGui::BeginTabItem("Players###pe_who_players")) {
        draw_player_card_40(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Managers###pe_who_managers")) {
        draw_managers(app);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static void draw_player_edit(App& app) {
    if (shell::tree40()) {
        draw_player_edit_40(app);
        return;
    }
    // the Profile's "Open Team > Players" button goes to the new page. Set where the page is drawn (not at start-up) so that the Profile's own test, which draws
    // it without the shell, still sees its default (the squad page)
    profile_team_players_page() = shell::Page::TeamPlayers;
    draw_player_header(app);  // nothing when no player is picked
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
    if (ImGui::BeginTabItem("Attributes & roles###pe_ability", nullptr, flag(1))) {
        now = 1;
        draw_player_ability(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Looks###pe_appearance", nullptr, flag(2))) {
        now = 2;
        looks::draw_looks_studio(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Callname###pe_callname", nullptr, flag(3))) {
        now = 3;
        draw_player_callname_tab(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Everything (classic)###pe_allfields", nullptr, flag(4))) {
        now = 4;
        if (ImGui::Checkbox("Card view (beta)###pcardview", &app.player_card_view)) app.save_gui_settings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The 2.16 card page instead of the full editor (filters, resizable list, archetype editor)");
        if (app.player_card_view) draw_player_page(app);
        else draw_players(app);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    if (now != s_shown) {  // one line in turbo_gui.log per change of tab (the live check of the wiring reads which tabs were really shown)
        static const char* const names[] = {"Profile", "Attributes & roles", "Looks", "Callname", "Everything (classic)"};
        if (now >= 0 && now < 5) app.log(std::string("Edit a player tab: ") + names[now]);
    }
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
