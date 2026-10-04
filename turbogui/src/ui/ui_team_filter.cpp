// FC 27 LE Turbo GUI - club filter combo (ui_team_filter.h).
#include "ui_team_filter.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <vector>

#include "app.h"
#include "imgui.h"

namespace turbo {

static std::string lower_copy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool team_matches(const TeamRow& t, const std::string& q) {
    if (q.empty()) return true;
    if (lower_copy(t.name).find(q) != std::string::npos) return true;
    return std::to_string(t.teamid).rfind(q, 0) == 0;
}

bool team_filter_combo(App& app, const char* id, int64_t& teamid, char* search, size_t search_size, float width) {
    const int64_t before = teamid;
    const std::string preview = teamid > 0 ? app.model.team_name(teamid) : std::string("Any club");
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) {
            if (search_size) search[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter =
            ImGui::InputTextWithHint("##teamq", "club name or ID", search, search_size, ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string q = lower_copy(search);
        std::vector<const TeamRow*> hits;
        for (const auto& t : app.model.teams())
            if (team_matches(t, q)) hits.push_back(&t);
        if (enter && !hits.empty()) {
            teamid = hits.front()->teamid;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable("Any club", teamid == 0)) teamid = 0;
        if (hits.empty()) ImGui::TextDisabled("no club matches");
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(hits.size()));
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const TeamRow* t = hits[static_cast<size_t>(i)];
                char lbl[192];
                std::snprintf(lbl, sizeof(lbl), "%s (%lld)##team%lld", t->name.c_str(), static_cast<long long>(t->teamid),
                              static_cast<long long>(t->teamid));
                if (ImGui::Selectable(lbl, teamid == t->teamid)) teamid = t->teamid;
            }
        }
        ImGui::EndCombo();
    }
    if (teamid > 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("Players of %s (club or national team)", preview.c_str());
    return teamid != before;
}

}  // namespace turbo
