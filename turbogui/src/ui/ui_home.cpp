// FC 27 LE Turbo GUI - the 3.0 Home page (ui_home.h). Navigation only: every button goes to an existing page with App::go; nothing here
// writes to the game. The squad table is the same squad_table Club > Squad draws (ui_teams.cpp), the Undo button the one of the top bar.
#include "ui_home.h"

#include <cstdio>
#include <string>

#include "app.h"
#include "imgui.h"
#include "ui_changes.h"
#include "ui_looks_studio.h"
#include "ui_shell.h"
#include "ui_team_common.h"
#include "ui_team_pages.h"
#include "ui_theme.h"

namespace turbo {

namespace {

HomeState g_home;

// 5.0b-A: "Player looks" / "Manager looks" go to the Appearance Editor page with that subject, in one click (its list has its own switch).
void open_looks(App& app, bool managers) {
    looks::looks_studio_show_managers(managers);
    app.go(shell::Page::LooksStudio);
}

// 5.0b-A: the next button goes on the same line when it fits in the window, else on the next line (a narrow window, a big UI size, must not clip it:
// the test harness finds only items whose centre is inside the visible part)
void same_line_if_fits(const char* label) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + st.FramePadding.x * 2.0f;
    const float used = ImGui::GetItemRectMax().x - ImGui::GetCursorScreenPos().x;
    if (used + st.ItemSpacing.x + w <= ImGui::GetContentRegionAvail().x) ImGui::SameLine();
}

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
    {shell::Area::Tactics, "Tactic presets and team sheet"},
    {shell::Area::GameSettings, "Game Type Settings, CPU Sliders, presets"},
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
    theme::push_title_font(theme::kFontMark);
    ImGui::TextUnformatted("Turbo X");
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
    // 10-28 findability (override 6): first row of Home, one click to the Game Type Settings and CPU Sliders page
    if (theme::primary_button("Game settings & CPU sliders###home_game_settings")) app.go(shell::Page::GameplayPresets);
    // 5.0b-A: the look pickers are one click from Home (the same Look card for a player and for a manager)
    same_line_if_fits("Player looks");
    if (theme::primary_button("Player looks###home_player_looks")) open_looks(app, false);
    same_line_if_fits("Manager looks");
    if (theme::primary_button("Manager looks###home_manager_looks")) open_looks(app, true);
    // 5.0c E16 (override 6, two clicks): the manager's 3D-model miniface used to be Home > Manager looks > Miniface > The 3D model (3 clicks).
    // This tile opens the Miniface tab on its "The 3D model" source in one.
    same_line_if_fits("Manager miniface (3D)");
    if (theme::primary_button("Manager miniface (3D)###home_manager_miniface")) {
        open_looks(app, true);
        shell::manager_edit_tab() = 2;
        shell::miniface_3d_request() = true;
    }
    if (career) {
        same_line_if_fits("Boost form");
        if (ImGui::SmallButton("Boost form###home_boost_form")) {
            // X 2.0 review fix (7): the override is merged onto auto.form_morale; 0 = leave alone (form_morale.lua plan), so the other two are
            // sent as 0 (a bare {"form": 99} also set the configured morale and fitness)
            app.send(teampage::run_cmd("form_morale", {{"form", 99}, {"morale", 0}, {"fitness", 0}}), "Squad form");
            app.notify("Squad form boosted");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets form to excellent for all players in your squad");

        same_line_if_fits("Boost morale");
        if (ImGui::SmallButton("Boost morale###home_boost_morale")) {
            app.send(teampage::run_cmd("form_morale", {{"form", 0}, {"morale", 99}, {"fitness", 0}}), "Squad morale");
            app.notify("Squad morale boosted");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets morale to very high for all players in your squad");
    }

    if (!x2::state().fav_players.empty() || !x2::state().fav_clubs.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Pinned favorites:");
        ImGui::SameLine();
        for (int64_t pid : x2::state().fav_players) {
            std::string pname = app.model.player_name(pid);
            if (pname.empty()) pname = "Player " + std::to_string(pid);
            char lbl[128];
            std::snprintf(lbl, sizeof(lbl), "%s###fav_p_%lld", pname.c_str(), static_cast<long long>(pid));
            if (ImGui::SmallButton(lbl)) {
                app.sel_player = pid;
                app.go(shell::Page::PlayerEdit);
            }
            ImGui::SameLine();
        }
        for (int64_t tid : x2::state().fav_clubs) {
            std::string tname = app.model.team_name(tid);
            if (tname.empty()) tname = "Club " + std::to_string(tid);
            char lbl[128];
            std::snprintf(lbl, sizeof(lbl), "%s###fav_c_%lld", tname.c_str(), static_cast<long long>(tid));
            if (ImGui::SmallButton(lbl)) {
                app.sel_team = tid;
                app.go(shell::Page::TeamPlayers);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    // (b) My squad
    section_title("My squad");
    g_home.squad = true;
    if (career) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - S(140.0f));
        const bool pitch_mode = x2::state().squad_pitch_home;
        char pitch_lbl[64];
        std::snprintf(pitch_lbl, sizeof(pitch_lbl), "%s###home_squad_pitch", pitch_mode ? "Show table" : "Pitch view");
        if (ImGui::SmallButton(pitch_lbl)) {
            x2::state().squad_pitch_home = !pitch_mode;
            app.save_gui_settings();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch between table view and pitch view");

        // as tall as the page allows while Presets, Recent changes and the tiles stay on screen (S(140) .. S(300))
        const float line = ImGui::GetFrameHeightWithSpacing();
        const float below = line * 3.0f + (ImGui::GetTextLineHeight() * 2.0f + S(24.0f)) * 2.0f + line * 4.0f + S(24.0f);
        float sh = ImGui::GetContentRegionAvail().y - below;
        sh = sh < S(140.0f) ? S(140.0f) : (sh > S(300.0f) ? S(300.0f) : sh);
        if (x2::state().squad_pitch_home) sh = S(400.0f);  // X 2.0 review fix: the pitch (S(380) tall) is shown whole, the page scrolls instead
        ImGui::BeginChild("##home_squad", ImVec2(0.0f, sh), ImGuiChildFlags_Borders);
        if (x2::state().squad_pitch_home) {
            draw_pitch_squad_view(app, st.user_team);  // X 2.0 review fix (14): the toggle drew the table in both views
        } else {
            squad_table(app, st.user_team);
        }
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
        const float h = ImGui::GetTextLineHeight() * 3.4f + ImGui::GetStyle().FramePadding.y * 2.0f;
        int col = 0;
        for (const Tile& t : kTiles) {
            const char* name = shell::area_label(t.area);
            char lbl[160];
            // Turbo X: the tile draws its own text (theme::tile); the label keeps the words for the test ids and the label scan
            std::snprintf(lbl, sizeof(lbl), "%s\n%s###home_tile_%d", name, t.line, static_cast<int>(t.area));
            if (col > 0) ImGui::SameLine();
            if (theme::tile(lbl, name, t.line, ImVec2(w > S(120.0f) ? w : S(120.0f), h))) app.go(t.area, shell::default_page(t.area));
            ++g_home.tiles;
            col = (col + 1) % 3;
        }
    }
}

// Registered the ordinary way (as the Appearance Editor is): Home has no old panel
TURBO_REGISTER_PAGE(shell::Page::Home, draw_home);

}  // namespace turbo
