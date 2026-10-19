// FC 27 LE Turbo GUI - "Use my crest on the shirt" (see ui_crest_shirt.h). Writes only turbo_output\kits\crest2d.json; the game
// picks the list up at its next start (session cache of the crest pictures, FCEDITOR_PARITY.md risk 7), so the text says so.
#include "ui_crest_shirt.h"

#include "app.h"
#include "core/faces3d.h"
#include "imgui.h"
#include "ui_theme.h"

namespace turbo {

namespace {

namespace f3 = faces3d;

struct Cache {
    std::string root;
    double at = -100.0;  // ImGui time of the last read (the file is read at most once a second, never every frame)
    f3::CrestList list;
    std::string error;  // the file is there but unreadable
};
Cache g_cache;
bool g_have_undo = false;
f3::CrestList g_before;  // the list before the last change
std::string g_note;      // what the last change or undo did, shown under the checkbox

}  // namespace

namespace crest_shirt {

bool is_on(const std::string& root, int64_t teamid) {
    f3::CrestList c;
    std::string err;
    return f3::load_crest2d(root, c, &err) && c.count(teamid) > 0;
}

bool set(const std::string& root, int64_t teamid, bool on, std::string& message) {
    if (teamid <= 0) {
        message = "No club chosen.";
        return false;
    }
    f3::CrestList c;
    std::string err;
    if (!f3::load_crest2d(root, c, &err)) {
        message = "Nothing changed: the saved list of clubs could not be read.";
        return false;
    }
    const f3::CrestList before = c;
    if (on) c.insert(teamid);
    else c.erase(teamid);
    if (c == before) {
        message = on ? "Already on." : "Already off.";
        return true;
    }
    if (!f3::save_crest2d(root, c, &err)) {
        message = "Nothing changed: " + err;
        return false;
    }
    g_before = before;
    g_have_undo = true;
    g_cache.at = -100.0;
    message = on ? "Club " + std::to_string(teamid) + " will wear its crest on the shirt from the next game start."
                 : "Club " + std::to_string(teamid) + " goes back to its own shirt crest from the next game start.";
    return true;
}

bool can_undo() { return g_have_undo; }

bool undo(const std::string& root, std::string& message) {
    if (!g_have_undo) {
        message = "Nothing to undo.";
        return false;
    }
    std::string err;
    if (!f3::save_crest2d(root, g_before, &err)) {
        message = "Undo failed: " + err;
        return false;
    }
    g_have_undo = false;
    g_cache.at = -100.0;
    message = "Undone: the list of clubs is back as it was (from the next game start).";
    return true;
}

void reset_for_tests() {
    g_have_undo = false;
    g_before.clear();
    g_cache = Cache{};
    g_note.clear();
}

}  // namespace crest_shirt

void draw_crest_shirt_toggle(App& app, int64_t teamid) {
    if (teamid <= 0) return;
    const std::string root = app.turbo_output().u8string();
    const double now = ImGui::GetTime();
    if (g_cache.root != root || now - g_cache.at > 1.0) {
        g_cache.root = root;
        g_cache.at = now;
        g_cache.error.clear();
        if (!f3::load_crest2d(root, g_cache.list, &g_cache.error)) g_cache.list.clear();
    }
    bool on = g_cache.list.count(teamid) > 0;
    const bool broken = !g_cache.error.empty();
    if (broken) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Use my crest on the shirt##kpcrestshirt", &on)) crest_shirt::set(root, teamid, on, g_note);
    if (broken) ImGui::EndDisabled();
    if (crest_shirt::can_undo()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Undo##kpcrestshirtundo")) crest_shirt::undo(root, g_note);
    }
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("The shirt and shorts show this club's crest picture instead of the stitched badge, in every career and in "
                      "Kick Off. Shows after the next game start.");
    if (broken) theme::muted_text("The saved list of clubs is damaged, so it is left as it is.");
    if (!g_note.empty()) theme::muted_text("%s", g_note.c_str());
    ImGui::PopTextWrapPos();
}

}  // namespace turbo
