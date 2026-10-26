// FC 27 LE Turbo GUI - the one page frame (see ui_page.h).
#include "ui_page.h"

#include <map>
#include <string>

#include "app.h"
#include "imgui.h"
#include "ui_changes.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo::page {

namespace {

std::map<std::string, std::string>& statuses() {
    static std::map<std::string, std::string> m;
    return m;
}
// Each stable id appears as a literal exactly once in this file (the id-manifest guard counts every ### literal).
constexpr const char* kApplyId = "###pg_apply";
constexpr const char* kUndoId = "###pg_undo";
constexpr const char* kBackId = "###pg_back";
const std::string kBackLabelStr = std::string("Back") + kBackId;
const char* const kBackLabel = kBackLabelStr.c_str();
std::map<std::string, bool>& status_is_error() {
    static std::map<std::string, bool> m;
    return m;
}
std::map<std::string, Record>& records() {
    static std::map<std::string, Record> m;
    return m;
}

// A 2 px outline around the item just drawn while it has keyboard / gamepad focus (not colour alone: the outline is a shape).
void focus_outline() {
    if (!ImGui::IsItemFocused()) return;
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 255),
                                        ImGui::GetStyle().FrameRounding, 2.0f, ImDrawFlags_None);
}

}  // namespace

void done(const char* page_id, const std::string& what) {
    statuses()[page_id] = "Done: " + what;
    status_is_error()[page_id] = false;
}
void couldnt(App& app, const char* page_id, const std::string& what, const std::string& why) {
    statuses()[page_id] = "Couldn't " + what + ": " + why;
    status_is_error()[page_id] = true;
    // Parity with App::notify(text, true), which the pages used before the template: an error also goes to the log as "ERROR: ...".
    app.log("ERROR: " + statuses()[page_id]);
}
const std::string& status_text(const char* page_id) {
    static const std::string none;
    auto it = statuses().find(page_id);
    return it == statuses().end() ? none : it->second;
}
const Record* last_record(const char* page_id) {
    auto it = records().find(page_id);
    return it == records().end() ? nullptr : &it->second;
}
void reset_for_tests() {
    statuses().clear();
    records().clear();
    status_is_error().clear();
}

Frame::Frame(App& app, const Spec& spec) : app_(app), spec_(spec) {
    ImGui::BeginChild((std::string("##page_") + spec_.id).c_str(), ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(spec_.title);
    ImGui::PopFont();
    if (spec_.help && *spec_.help) theme::muted_text("%s", spec_.help);
    // the body scrolls on its own; the status line and the bar below it stay on screen
    const float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + S(theme::kGrid);
    ImGui::BeginChild("##pg_body", ImVec2(0, -footer), ImGuiChildFlags_Borders);
}

Frame::~Frame() {
    if (!ended_) end();
}

bool Frame::end() {
    ended_ = true;
    ImGui::EndChild();  // ##pg_body

    // ---- the one status line (the words carry the meaning; the colour only backs them up)
    const std::string& st = status_text(spec_.id);
    if (st.empty()) ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    else if (status_is_error()[spec_.id]) theme::err_text("%s", st.c_str());
    else theme::ok_text("%s", st.c_str());

    // ---- the action bar: Apply (optional), Undo, Back
    Record rec;
    rec.id = spec_.id;
    rec.title = spec_.title;
    rec.has_apply = spec_.apply_label != nullptr;
    rec.frame = ImGui::GetFrameCount();
    bool apply = false;
    if (spec_.apply_label) {
        const std::string label = std::string(spec_.apply_label) + kApplyId;
        ImGui::BeginDisabled(!spec_.apply_enabled);
        apply = theme::primary_button(label.c_str());
        ImGui::EndDisabled();
        focus_outline();
        rec.bar.push_back(kApplyId);
        ImGui::SameLine();
    }
    {
        const changes::Change* c = changes::last_undoable(app_);
        const std::string label = changes::undo_label(app_) + kUndoId;
        ImGui::BeginDisabled(!c);
        if (ImGui::Button(label.c_str())) {
            std::string msg;
            if (changes::undo_last(app_, &msg)) done(spec_.id, msg);
            else couldnt(app_, spec_.id, "undo", msg);
        }
        ImGui::EndDisabled();
        focus_outline();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", c ? ("Takes back: " + c->what).c_str() : "Nothing you changed can be taken back from here yet.");
        rec.bar.push_back(kUndoId);
        rec.undo_enabled = c != nullptr;
        ImGui::SameLine();
    }
    {
        const shell::Place bp = shell::back_place();
        const bool can = bp.valid() && bp.page != app_.place().page && shell::page_visible(bp.page, app_.prefs.advanced_mode);
        ImGui::BeginDisabled(!can);
        if (ImGui::Button(kBackLabel)) app_.back();
        ImGui::EndDisabled();
        focus_outline();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", can ? (std::string("Return to ") + shell::page_label(bp.page)).c_str()
                                        : "You opened this page directly; use the page strip.");
        rec.bar.push_back(kBackId);
        rec.back_enabled = can;
    }
    records()[spec_.id] = rec;
    ImGui::EndChild();  // ##page_<id>
    return apply;
}

}  // namespace turbo::page
