// FC 27 LE Turbo GUI - the 3.0 Home page (ui_home.h). Navigation only: every button goes to an existing page with App::go; nothing here
// writes to the game. The squad table is the same squad_table Club > Squad draws (ui_teams.cpp), the Undo button the one of the top bar.
#include "ui_home.h"

#include <cstdio>
#include <string>

#include "app.h"
#include "imgui.h"
#include "ui_changes.h"
#include "ui_shell.h"
#include "ui_team_pages.h"
#include "ui_theme.h"

namespace turbo {

namespace {

HomeState g_home;

// A rounded, filled label that is not a button (the status chip)
void chip(const char* text, const ImVec4& col) {
    const ImVec2 pad(S(10.0f), S(4.0f));
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + ts.x + pad.x * 2.0f, p0.y + ts.y + pad.y * 2.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.18f)), S(10.0f));
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.7f)), S(10.0f));
    dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), ImGui::GetColorU32(col), text);
    ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));
}

void section_title(const char* text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
    ++g_home.sections;
}

struct Tile {
    shell::Area area;
    const char* line;
};
const Tile kTiles[] = {
    {shell::Area::Squad, "Edit a player, create and clone, callnames"},
    {shell::Area::Looks, "Faces, hair, boots and the rest of the look"},
    {shell::Area::Club, "Kits, name and crest, squad and transfers"},
    {shell::Area::Tactics, "Tactic presets, team sheet and game sliders"},
    {shell::Area::Career, "Standings, money, manager jobs, season pack"},
    {shell::Area::Settings, "UI size, pictures, the show / hide key"},
};

}  // namespace

const HomeState& home_state() { return g_home; }

void draw_home(App& app) {
    g_home = HomeState{};
    const auto& st = app.bridge.state();
    const bool career = app.connected() && st.in_cm && st.user_team > 0;
    g_home.in_career = career;
    g_home.club = career ? st.user_team : 0;

    // (a) title + status chip
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted("FC 27 LE Turbo");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, S(16.0f));
    {
        std::string text;
        if (!app.connected()) text = "Waiting for your career";
        else if (career) text = app.model.team_name(st.user_team) + "  |  career loaded";
        else text = "No career loaded";
        if (app.connected() && st.date.valid()) {
            char d[32];
            std::snprintf(d, sizeof(d), "  |  %04d-%02d-%02d", st.date.year, st.date.month, st.date.day);
            text += d;
        }
        chip(text.c_str(), career ? theme::ok() : theme::warn());
        g_home.status_chip = true;
        ++g_home.sections;
    }

    // (b) My squad
    section_title("My squad");
    g_home.squad = true;
    if (career) {
        // as tall as the page allows while Presets, Recent changes and the tiles stay on screen (S(140) .. S(300))
        const float line = ImGui::GetFrameHeightWithSpacing();
        const float below = line * 3.0f + (ImGui::GetTextLineHeight() * 2.0f + S(24.0f)) * 2.0f + line * 4.0f + S(24.0f);
        float sh = ImGui::GetContentRegionAvail().y - below;
        sh = sh < S(140.0f) ? S(140.0f) : (sh > S(300.0f) ? S(300.0f) : sh);
        ImGui::BeginChild("##home_squad", ImVec2(0.0f, sh), ImGuiChildFlags_Borders);
        squad_table(app, st.user_team);
        ImGui::EndChild();
        g_home.squad_table = true;
    } else {
        theme::muted_text("Load your career in the game to see your squad here.");
    }

    // (c) Presets
    section_title("Presets");
    g_home.presets = true;
    if (theme::primary_button("My team preset###home_preset_team")) {
        if (career) app.sel_team = st.user_team;
        app.go(shell::Page::TeamPlayers);
    }
    ImGui::SameLine();
    if (theme::primary_button("Tactic presets###home_preset_tactics")) app.go(shell::Page::TacticPresets);
    ImGui::SameLine();
    if (theme::primary_button("Game sliders###home_preset_sliders")) app.go(shell::Page::GameplayPresets);
    ImGui::SameLine();
    if (theme::primary_button("Season pack###home_preset_season")) app.go(shell::Page::CMTracker);

    // (d) Recent changes
    section_title("Recent changes");
    g_home.recent = true;
    g_home.changes = changes::count();
    ImGui::AlignTextToFramePadding();
    if (g_home.changes == 0) theme::muted_text("No changes yet.");
    else ImGui::Text("%zu change%s in the list", g_home.changes, g_home.changes == 1 ? "" : "s");
    ImGui::SameLine(0.0f, S(16.0f));
    changes::draw_status_undo(app);
    ImGui::SameLine();
    if (ImGui::Button("See all changes###home_changes")) app.go(shell::Page::ChangesUndo);

    // (e) one tile per area
    section_title("Go to");
    {
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float w = (ImGui::GetContentRegionAvail().x - sp * 2.0f) / 3.0f;
        const float h = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().FramePadding.y * 2.0f + S(12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        int col = 0;
        for (const Tile& t : kTiles) {
            const char* name = shell::area_label(t.area);
            char lbl[160];
            std::snprintf(lbl, sizeof(lbl), "%s\n%s###home_tile_%d", name, t.line, static_cast<int>(t.area));
            if (col > 0) ImGui::SameLine();
            if (ImGui::Button(lbl, ImVec2(w > S(120.0f) ? w : S(120.0f), h))) app.go(t.area, shell::default_page(t.area));
            ++g_home.tiles;
            col = (col + 1) % 3;
        }
        ImGui::PopStyleVar();
    }
}

// Registered the ordinary way (as the Appearance Editor is): the page also draws while Settings > Use the old pages is on, Home has no old panel
TURBO_REGISTER_PAGE(shell::Page::Home, draw_home);

}  // namespace turbo
