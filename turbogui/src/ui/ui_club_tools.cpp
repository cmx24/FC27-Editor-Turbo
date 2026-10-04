// FC 27 LE Turbo GUI - Turbo Tools: reopen the club customisation hub (see ui_club_tools.h)
#include "ui_club_tools.h"

#include <ctime>

#include "app.h"
#include "core/hub_customise.h"
#include "imgui.h"

namespace turbo {

using nlohmann::json;

namespace {

ClubToolsState g_state;
mhm::State g_hub;
double g_hub_next = 0.0;
long long g_hub_gen = -1;
bool g_saves_backed_up = false;  // this session: Turbo copied the saves, or the user ticked "my save is backed up"

const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);

void refresh_hub(App& app, bool force) {
    const BridgeState& st = app.bridge.state();
    if (!force && app.now < g_hub_next && g_hub_gen == st.db_gen) return;
    g_hub_next = app.now + 1.0;
    g_hub_gen = st.db_gen;
    if (!st.in_cm) {
        g_state.hub_ready = false;
        g_state.hub_line = "Needs a loaded Manager Career.";
        return;
    }
    std::string err = mhm::locate(app.mem, st.managers, mhm::vtable(app.game_base), st.user_team, g_hub);
    g_state.hub_ready = err.empty();
    g_state.hub_line = err.empty() ? mhm::describe(g_hub) : err;
}

std::string stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &tmv);
    return b;
}

void wrapped(const ImVec4* col, const std::string& text) {
    ImGui::PushTextWrapPos(0.0f);
    if (col) ImGui::TextColored(*col, "%s", text.c_str());
    else ImGui::TextDisabled("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void draw_hub(App& app) {
    refresh_hub(app, false);
    const bool ready = g_state.hub_ready;
    if (ready) ImGui::TextWrapped("%s", g_state.hub_line.c_str());
    else wrapped(&kWarn, g_state.hub_line);
    const bool licensed = ready && g_hub.licensed;
    if (!ready || licensed) ImGui::BeginDisabled();
    if (ImGui::Button("Reopen club customisation")) club_reopen(app, false);
    if (!ready || licensed) ImGui::EndDisabled();
    if (licensed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Your club plays in a licensed stadium: use \"Licensed stadium too...\"");
    ImGui::SameLine();
    if (!ready) ImGui::BeginDisabled();
    if (ImGui::Button("Licensed stadium too...")) ImGui::OpenPopup("Licensed stadium##clubcust");
    if (!ready) ImGui::EndDisabled();
    if (!g_state.hub_result.empty()) wrapped(nullptr, "Last: " + g_state.hub_result);
    wrapped(nullptr,
            "After pressing, leave the hub (for example open Squad) and come back. Your created club gets the kits, crest and "
            "stadium designer at any time; other clubs get the stadium hub. A new season locks it again: press again then. "
            "The Create a Club setup steps (name, rival, squad, budget) cannot be reopened: the Teams and Database tabs cover them.");

    if (ImGui::BeginPopupModal("Licensed stadium##clubcust", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(S(520.0f));
        ImGui::TextColored(kWarn, "Reopening Customise club for a club with a licensed stadium can replace its real stadium with a "
                                  "custom one, possibly for good in this save.");
        ImGui::TextUnformatted("Back up your career save first. Turbo can copy every Manager Career save (CmMgrC*) of the game's "
                               "settings folder to turbo_output\\save_backups.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Back up my career saves now")) {
            mhm::SaveBackup b = mhm::backup_saves(mhm::default_save_dir(), app.bridge.dir() / "save_backups", stamp());
            g_state.backup_result = b.message;
            if (b.ok) g_saves_backed_up = true;
            app.log("club customisation: save backup: " + b.message);
            app.notify(b.message, !b.ok);
        }
        if (!g_state.backup_result.empty()) ImGui::TextWrapped("%s", g_state.backup_result.c_str());
        ImGui::Checkbox("My save is backed up", &g_saves_backed_up);
        if (!g_saves_backed_up) ImGui::BeginDisabled();
        if (ImGui::Button("Reopen, licensed stadium too")) {
            club_reopen(app, true);
            ImGui::CloseCurrentPopup();
        }
        if (!g_saves_backed_up) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace

const ClubToolsState& club_tools_state() { return g_state; }

bool club_reopen(App& app, bool licensed_too) {
    const BridgeState& st = app.bridge.state();
    mhm::Result r = mhm::reopen(app.mem, st.in_cm ? st.managers : 0, mhm::vtable(app.game_base), st.user_team, licensed_too);
    g_state.hub_result = r.message;
    app.log("club customisation: " + r.message);
    app.notify(r.message, !r.ok);
    refresh_hub(app, true);
    return r.ok;
}

void draw_club_tools(App& app) {
    if (ImGui::CollapsingHeader("Your club: customisation hub (kits, crest, stadium)")) draw_hub(app);
}

}  // namespace turbo
