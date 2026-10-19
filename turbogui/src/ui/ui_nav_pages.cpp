// FC 27 LE Turbo GUI - the pages of the 2.16 navigation (plan TURBO_GUI_REVAMP_PLAN.md 4.3, step 4) whose content still lives where it was:
// Create a player, Commentary names and Team sheet & bench. (2.20: the Faces & hair and Body, tattoos & gear signposts are gone: the face chooser,
// the 3D hair and beard, the tattoos, the gear and the miniface stay on Edit a player's Appearance and Miniface tabs, and the Appearance Editor
// is its own area.) Each starts as a signpost: one question, one
// line saying where the feature is today, and a button that goes there, so that nothing disappears while the pages move (steps 5, 6 and 8
// replace each signpost with the real page). No picture, no file, no state: they cost nothing per frame.
#include "app.h"
#include "imgui.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace {

using shell::Page;

void signpost(App& app, const char* question, const char* where, Page target, const char* button) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(question);
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(where);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const std::string label = std::string(button) + "###navgo";
    if (ImGui::Button(label.c_str())) app.go(shell::area_of(target), target);
}

void page_create_player(App& app) {
    signpost(app, "Make a new player", "Today this is the Create player button above the tabs of Edit a player. It also copies a player and builds one from a CMTracker file.",
             Page::PlayerEdit, "Open Edit a player");
}
void page_commentary_names(App& app) {
    signpost(app, "What does the commentator say?", "Today: pick the player on Edit a player, then open his Callname tab.", Page::PlayerEdit, "Open Edit a player");
}
void page_team_sheet(App& app) {
    const bool adv = app.prefs.advanced_mode;
    signpost(app, "Keep my bench? Who takes set pieces?",
             adv ? "Today: set pieces are saved and restored with Tactic presets. The bench option is under Advanced > Tactics (experimental) > My team."
                 : "Today: set pieces are saved and restored with Tactic presets. The bench option is under Advanced > Tactics (experimental) > My team; "
                   "Settings > Show advanced tools shows that section.",
             adv ? Page::AdvTactics : Page::Settings, adv ? "Open Advanced > Tactics" : "Open Settings");
}

}  // namespace

TURBO_REGISTER_PAGE(shell::Page::CreatePlayer, page_create_player);
TURBO_REGISTER_PAGE(shell::Page::CommentaryNames, page_commentary_names);
TURBO_REGISTER_PAGE(shell::Page::TeamSheet, page_team_sheet);

}  // namespace turbo
