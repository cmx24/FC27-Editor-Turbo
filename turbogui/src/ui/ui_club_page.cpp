// FC 27 LE Turbo GUI - Club & Kits area (see ui_club_page.h): Kits, Identity, Stadium & extras.
#include "ui_club_page.h"

#include <string>

#include "app.h"
#include "imgui.h"
#include "ui_club_search.h"
#include "ui_club_tools.h"
#include "ui_identity.h"
#include "ui_images.h"
#include "ui_kits.h"
#include "ui_theme.h"

namespace turbo {

namespace {

ClubPageState g_state;
shell::Page g_request = shell::Page::None;

// The teams row of the selected club, or false (with a hint drawn) when it is not there
bool club_ctx(App& app, const Table*& t, const TeamRow*& tr) {
    t = app.db.table("teams");
    tr = app.model.team(app.sel_team);
    if (!t || !tr) {
        theme::muted_text("Pick a club above.");
        return false;
    }
    if (!app.db.table_alive(*t, tr->rec)) {
        theme::err_text("The club list changed (another save loaded?). Use Reload from game in Settings.");
        return false;
    }
    return true;
}

// The one row every page of the area starts with: the club (name or ID), "My club", and what is shown
bool page_header(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return false;
    }
    const int64_t team = club_page_team(app);
    g_state.teamid = team;
    ImGui::AlignTextToFramePadding();
    theme::muted_text("Club");
    ImGui::SameLine();
    club_search_box(app, "##clubpick", app.sel_team, S(260.0f));
    if (app.bridge.state().user_team > 0) {
        ImGui::SameLine();
        if (ImGui::Button("My club##clubmine")) app.sel_team = app.bridge.state().user_team;
    }
    ImGui::SameLine();
    const TeamRow* tr = app.model.team(app.sel_team);
    if (tr) theme::muted_text("%s", tr->name.c_str());
    ImGui::Separator();
    return app.model.team(app.sel_team) != nullptr;
}

// ---------------------------------------------------------------- pages
void kits_page_fn(App& app) {
    if (!page_header(app)) return;
    kits_page(app, app.sel_team);
}

// A section of the Identity page: a bordered child of a fixed height (the editors scroll inside)
void identity_box(App& app, const char* child, float height, void (*fn)(App&)) {
    ImGui::BeginChild(child, ImVec2(0, S(height)), ImGuiChildFlags_Borders);
    fn(app);
    ImGui::EndChild();
}
void name_box(App& app) { identity_box(app, "##tname", 230.0f, club_name_section); }
void colours_box(App& app) {
    ImGui::BeginChild("##tcolours", ImVec2(0, S(190.0f)), ImGuiChildFlags_Borders);
    club_colours_section(app, false);
    ImGui::EndChild();
}
void crest_box(App& app) { identity_box(app, "##tcrest", 440.0f, club_crest_section); }

void identity_page_fn(App& app) {
    if (!page_header(app)) return;
    // name + colours | crest side by side; one under the other when the window is narrow
    g_state.wide = ImGui::GetContentRegionAvail().x >= S(760.0f);
    if (g_state.wide) {
        ImGui::Columns(2, "##idcols", false);
        theme::muted_text("Name");
        name_box(app);
        theme::muted_text("Colours");
        colours_box(app);
        ImGui::NextColumn();
        theme::muted_text("Crest");
        crest_box(app);
        ImGui::Columns(1);
    } else {
        theme::muted_text("Name");
        name_box(app);
        theme::muted_text("Colours");
        colours_box(app);
        theme::muted_text("Crest");
        crest_box(app);
    }
    theme::muted_text("Each part has its own Save; every change can be undone from the status line (Undo last).");
}

void stadium_page_fn(App& app) {
    if (!page_header(app)) return;
    ImGui::BeginChild("##stadium", ImVec2(0, 0), ImGuiChildFlags_Borders);
    club_stadium_section(app);
    ImGui::Spacing();
    ImGui::SeparatorText("More for your club");
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);  // the extras' own header
    draw_club_tools(app);
    ImGui::EndChild();
}

}  // namespace

TURBO_REGISTER_PAGE(shell::Page::Kits, kits_page_fn);
TURBO_REGISTER_PAGE(shell::Page::Identity, identity_page_fn);
TURBO_REGISTER_PAGE(shell::Page::StadiumExtras, stadium_page_fn);

int64_t club_page_team(App& app) {
    if (!app.connected()) return 0;
    if (app.model.team(app.sel_team)) return app.sel_team;
    const int64_t mine = app.bridge.state().user_team;
    if (mine > 0 && app.model.team(mine)) {
        app.sel_team = mine;
        return mine;
    }
    return 0;
}

void club_name_section(App& app) {
    const Table* t = nullptr;
    const TeamRow* tr = nullptr;
    if (club_ctx(app, t, tr)) team_name_editor(app, *t, tr->rec, tr->teamid);
}

void club_crest_section(App& app) {
    const Table* t = nullptr;
    const TeamRow* tr = nullptr;
    if (club_ctx(app, t, tr)) crest_editor(app, tr->teamid);
}

void club_colours_section(App& app, bool legacy) {
    const Table* t = nullptr;
    const TeamRow* tr = nullptr;
    if (!club_ctx(app, t, tr)) return;
    team_colours_editor(app, *t, tr->rec, tr->teamid);
    if (legacy) {
        team_stadium_editor(app, *t, tr->rec);
        club_kit_colours_section(app, tr->teamid);
    }
}

void club_stadium_section(App& app) {
    const Table* t = nullptr;
    const TeamRow* tr = nullptr;
    if (club_ctx(app, t, tr)) team_stadium_editor(app, *t, tr->rec);
}

void club_kit_colours_section(App& app, int64_t teamid) { team_kit_colours_editor(app, teamid); }

void club_request_page(shell::Page p) { g_request = p; }
shell::Page club_take_page_request() {
    const shell::Page p = g_request;
    g_request = shell::Page::None;
    return p;
}

const ClubPageState& club_page_state() { return g_state; }

}  // namespace turbo
