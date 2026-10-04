// FC 27 LE Turbo GUI - Turbo Tools: reopen the club customisation hub, unlock the career settings (see ui_club_tools.h)
#include "ui_club_tools.h"

#include <ctime>
#include <memory>

#include "app.h"
#include "core/career_settings.h"
#include "core/hub_customise.h"
#include "imgui.h"

namespace turbo {

using nlohmann::json;

namespace {

ClubToolsState g_state;
mhm::State g_hub;
double g_hub_next = 0.0;
long long g_hub_gen = -1;
std::unique_ptr<csu::Store> g_store;
const App* g_store_app = nullptr;
csu::Store::Status g_cs;
double g_cs_next = 0.0;
bool g_saves_backed_up = false;  // this session: Turbo copied the saves, or the user ticked "my save is backed up"

const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);

csu::Store& store(App& app) {
    if (!g_store || g_store_app != &app) {
        g_store = std::make_unique<csu::Store>(app.legacy, app.bridge.root());
        g_store_app = &app;
        g_cs_next = 0.0;
    }
    return *g_store;
}

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

void refresh_settings(App& app, bool force) {
    if (!force && app.now < g_cs_next) return;
    g_cs_next = app.now + 1.0;
    g_cs = store(app).status();
    g_state.settings_line = g_cs.line;
    g_state.settings_on = g_cs.written;
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

void draw_settings(App& app) {
    refresh_settings(app, false);
    wrapped(&kWarn,
            "Advanced. EA locks these settings once a career has started because they change the simulation mid-career "
            "(development rates, transfers and negotiations, scouting, board expectations, manager market, unexpected events, "
            "pitch wear, points deduction). Competition, currency, deeper simulation, financial takeover and youth academy stay "
            "locked. Applies to every career on this PC.");
    ImGui::TextWrapped("%s", g_state.settings_line.c_str());
    json& cfg = app.gui_settings["career_settings"];
    if (!cfg.is_object()) cfg = json::object();
    bool squad = cfg.value("squad_settings", false);
    bool on = g_cs.written;
    if (g_cs.foreign) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Unlock the locked career settings", &on)) {
        if (on) ImGui::OpenPopup("Unlock career settings?##cs");
        else career_settings_set(app, false, squad);
    }
    if (ImGui::Checkbox("Also add the Squad settings: edit injuries, edit suspensions, release players (experimental)", &squad)) {
        cfg["squad_settings"] = squad;
        app.save_gui_settings();
        if (g_cs.written) career_settings_set(app, true, squad);
    }
    if (g_cs.foreign) ImGui::EndDisabled();
    if (!g_state.settings_result.empty()) wrapped(nullptr, "Last: " + g_state.settings_result);
    wrapped(nullptr, "Takes effect the next time the hub's Settings screen opens; restart the game if it does not. Unticking puts "
                     "the game's own file back.");
    if (ImGui::BeginPopupModal("Unlock career settings?##cs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(S(520.0f));
        ImGui::TextColored(kWarn, "This changes the simulation of a running career: development, transfers and the board's "
                                  "expectations follow the new values from the next day on. Keep a save backup.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Unlock")) {
            career_settings_set(app, true, squad);
            ImGui::CloseCurrentPopup();
        }
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

bool career_settings_set(App& app, bool on, bool squad_settings) {
    csu::RecipeOptions opt;
    opt.squad_settings = squad_settings;
    std::string msg;
    const bool ok = on ? store(app).apply(opt, msg) : store(app).restore(msg);
    g_state.settings_result = msg;
    app.log("career settings: " + msg);
    app.notify(msg, !ok);
    refresh_settings(app, true);
    return ok;
}

void draw_club_tools(App& app) {
    if (ImGui::CollapsingHeader("Your club: customisation hub (kits, crest, stadium)")) draw_hub(app);
    if (ImGui::CollapsingHeader("Career settings unlock (advanced)")) draw_settings(app);
}

}  // namespace turbo
