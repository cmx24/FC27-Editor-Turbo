// FC 27 LE Turbo GUI - "Game editors" section of the Turbo Tools tab (core/edit_unlock.h).
#include "ui_edit_unlock.h"

#include <memory>
#include <string>

#include "app.h"
#include "core/edit_unlock.h"
#include "imgui.h"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
struct Panel {
    const App* app = nullptr;
    fs::path root;
    std::unique_ptr<eu::EditUnlock> svc;
    double next = 0.0;        // next collect / apply (every second)
    std::string applied_sig;  // input_signature of the last apply
    std::string last_summary;
};

Panel& panel(App& app) {
    static std::unique_ptr<Panel> p;
    fs::path root = app.legacy.mods_dir().parent_path().parent_path();  // <Live Editor>
    if (!p || p->app != &app || p->root != root) {
        p = std::make_unique<Panel>();
        p->app = &app;
        p->root = root;
        p->svc = std::make_unique<eu::EditUnlock>(app.legacy, root);
    }
    return *p;
}

const json* settings(const App& app) {
    const json& g = app.gui_settings;
    if (!g.is_object()) return nullptr;
    auto it = g.find("edit_unlock");
    return it != g.end() && it->is_object() ? &*it : nullptr;
}

void save(App& app, bool enabled, const eu::Options& o) {
    json j = o.to_json();
    j["enabled"] = enabled;
    app.gui_settings["edit_unlock"] = j;
    if (!app.save_gui_settings()) app.notify("cannot write turbo_output\\gui_settings.json", true);
}

void log_line(App& app, const std::string& line) {
    app.log(line);
    if (app.log_hook) app.log_hook(line);
}

void run_apply(App& app, Panel& p, const eu::Options& o, bool force) {
    for (const auto& line : p.svc->collect(o)) log_line(app, line);
    std::string sig = p.svc->input_signature(o);
    if (!force && sig == p.applied_sig) return;
    p.applied_sig = sig;
    std::string s = p.svc->apply(o);
    if (force || s != p.last_summary) log_line(app, s);
    p.last_summary = s;
}

ImVec4 state_colour(eu::EditUnlock::State s) {
    using S = eu::EditUnlock::State;
    switch (s) {
        case S::Unlocked: return ImVec4(0.45f, 0.9f, 0.45f, 1);
        case S::Failed: return ImVec4(1, 0.4f, 0.4f, 1);
        case S::Waiting: return ImVec4(1, 0.8f, 0.35f, 1);
        case S::Kept: return ImVec4(1, 0.6f, 0.3f, 1);
        default: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }
}

void file_line(eu::EditUnlock& s, const eu::FileSpec& f) {
    eu::EditUnlock::FileStatus st = s.status(f.path);
    ImGui::TextColored(state_colour(st.state), "%s", eu::EditUnlock::state_name(st.state));
    ImGui::SameLine();
    ImGui::TextWrapped("%s: %s", f.label.c_str(), st.note.c_str());
}
}  // namespace

bool edit_unlock_enabled(const App& app) {
    const json* s = settings(app);
    return !(s && s->contains("enabled") && (*s)["enabled"].is_boolean()) || (*s)["enabled"].get<bool>();
}

eu::Options edit_unlock_options(const App& app) {
    const json* s = settings(app);
    return s ? eu::Options::from_json(*s) : eu::Options();
}

eu::EditUnlock& edit_unlock_service(App& app) { return *panel(app).svc; }

void edit_unlock_tick(App& app) {
    Panel& p = panel(app);
    if (app.now < p.next) return;
    p.next = app.now + 1.0;
    if (!edit_unlock_enabled(app)) return;  // nothing is asked from the game while the switch is off
    run_apply(app, p, edit_unlock_options(app), false);
}

void draw_game_editors(App& app) {
    if (!ImGui::CollapsingHeader("Game editors (unlock FC 27's own edit screens)")) return;
    Panel& p = panel(app);
    eu::EditUnlock& s = *p.svc;
    eu::Options o = edit_unlock_options(app);
    bool on = edit_unlock_enabled(app);
    bool changed = false;
    const ImVec4 warn(1, 0.75f, 0.35f, 1);
    ImGui::PushID("gameeditors");
    ImGui::TextWrapped(
        "Turns on the greyed-out fields and shows the hidden ones in FC 27's own editors: Career > Squad > Edit Player "
        "(names, commentary name, kit name and number, nationality, birth date, height, weight, position, role, foot, plus "
        "the Attributes and Brand animations sections), Edit Manager, Create a Club players and the main menu's Edit "
        "Players. Team stays locked (Turbo's moves do transfers properly) and a player's gender stays hidden. The files are "
        "built on this PC from the game's own; Restore the game's originals removes them.");
    if (ImGui::Checkbox("Unlock the game's editors", &on)) {
        if (!on) {
            std::string r = s.restore();
            log_line(app, r);
            app.notify(r);
        }
        save(app, on, o);
        p.next = 0.0;
        p.applied_sig.clear();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!on);
    if (ImGui::Button("Write the unlocked files now")) {
        run_apply(app, p, o, true);
        app.notify(p.last_summary);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool can_restore = s.written_count() > 0;
    if (!can_restore) ImGui::BeginDisabled();
    if (ImGui::Button("Restore the game's originals")) {
        std::string r = s.restore();
        log_line(app, r);
        app.notify(r);
        save(app, false, o);  // the switch goes off, or the next tick would write them again
        on = false;
        p.applied_sig.clear();
    }
    if (!can_restore) ImGui::EndDisabled();
    ImGui::BeginDisabled(!on);
    if (ImGui::Checkbox("Unlock everything (experimental)", &o.experimental)) changed = true;
    ImGui::PushStyleColor(ImGuiCol_Text, warn);
    ImGui::TextWrapped(
        "Not yet checked in game; back up your career save first. Adds the head editor for real players and real managers "
        "(a generic head replaces the real face scan for good), Composure and Defensive awareness, the manager outfit "
        "picker (offline its lists may be short), every goal celebration, your manager's gender and, with career "
        "settings, the squad settings.");
    ImGui::PopStyleColor();
    if (ImGui::Checkbox("Career settings too (advanced)", &o.career_settings)) changed = true;
    ImGui::PushStyleColor(ImGuiCol_Text, warn);
    ImGui::TextWrapped(
        "Unlocks the hub settings EA locks during a career (match setup, training and development rates, transfers, "
        "negotiation, scouting, board expectations, manager market ...). Changing them mid-season changes the "
        "simulation. Competition, currency, deeper simulation, takeover and youth academy stay locked.");
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "The game reads these files each time an editor screen opens: reopen the screen, no game restart needed when Live "
        "Editor's override applies. A field still greyed after reopening and after a game restart means Live Editor did "
        "not apply the override (Turbo has no fallback for that yet).");
    if (!s.manifest_error().empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", s.manifest_error().c_str());
    if (on && !s.ready(o)) {
        ImGui::PushStyleColor(ImGuiCol_Text, warn);
        ImGui::TextWrapped("Waiting for the game's own files: they are exported on the next career-mode event (advance a "
                           "day or open a menu), or at once with lua\\scripts\\turbo_images.lua in Live Editor's Lua Engine.");
        ImGui::PopStyleColor();
    }
    if (!p.last_summary.empty()) ImGui::TextDisabled("%s", p.last_summary.c_str());
    for (const auto& f : eu::files())
        if (f.group != eu::Group::Source && o.file_on(f.path)) file_line(s, f);
    if (ImGui::TreeNode("Details")) {
        ImGui::BeginDisabled(!on);
        ImGui::TextDisabled("Screens");
        changed |= ImGui::Checkbox("Career Edit Player", &o.career_players);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Create-a-Club players", &o.created_players);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Your manager", &o.manager);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Main menu editors", &o.main_menu);
        ImGui::TextDisabled("Files (Live Editor\\mods\\legacy\\...; originals in turbo_output\\edit_unlock\\originals)");
        for (const auto& f : eu::files()) {
            ImGui::PushID(f.path.c_str());
            if (f.group != eu::Group::Source) {
                bool v = !o.files_off.count(f.path);
                if (ImGui::Checkbox("##file", &v)) {
                    if (v) o.files_off.erase(f.path);
                    else o.files_off.insert(f.path);
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(f.path.substr(f.path.rfind('/') + 1).c_str());
            ImGui::Indent();
            file_line(s, f);
            ImGui::Unindent();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
    ImGui::PopID();
    if (changed) {
        save(app, on, o);
        p.next = 0.0;
    }
}

}  // namespace turbo
