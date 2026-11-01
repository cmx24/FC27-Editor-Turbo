// FC 27 LE Turbo GUI - club chooser (ui_club_search.h).
#include "ui_club_search.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "geo.h"
#include "imgui.h"

namespace turbo {

namespace {

// The box being typed in (one at a time): its text, the clubs matching it and the highlighted row
struct Edit {
    ImGuiID id = 0;              // the box (0 = none)
    char text[96] = "";
    bool typed = false;          // typed in since it was clicked: the list follows the text (before that: every club)
    bool keyed = false;          // Up / Down moved the highlighted row
    int cursor = -1;             // the highlighted row (-1: put it on the chosen club)
    bool scroll = false;         // bring the highlighted row into view
    ImVec2 drop_min, drop_max;   // the dropdown last frame: a click there is a pick, not leaving the box
    std::string query;           // the text `hits` were found for, with the model version (rows change on a rebuild)
    uint64_t version = ~uint64_t(0);
    std::vector<const TeamRow*> hits;
};
Edit g_edit;

std::string trimmed(const char* s) {
    std::string t = s;
    const size_t a = t.find_first_not_of(" \t"), b = t.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
}

// A club ID as typed (9 digits at most: it stays an int)
bool is_number(const std::string& s) {
    return !s.empty() && s.size() <= 9 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// The clubs matching q (fold_search'ed: case and accents ignored), best first; name order within a rank. Every club when q is empty.
void find_clubs(App& app, const std::string& q, std::vector<const TeamRow*>& out) {
    out.clear();
    const bool num = is_number(q);
    std::vector<std::pair<int, const TeamRow*>> ranked;
    for (const TeamRow& t : app.model.teams()) {
        int rank = q.empty() ? 0 : -1;
        if (rank < 0) {
            const std::string id = std::to_string(t.teamid);
            std::string folded;  // only for a row the model did not fold
            const std::string& nf = t.name_fold.empty() ? (folded = fold_search(t.name)) : t.name_fold;
            const size_t at = nf.find(q);
            if (num && id == q) rank = 0;                    // exactly that ID
            else if (num && id.rfind(q, 0) == 0) rank = 1;   // an ID starting with it
            else if (at == 0) rank = 2;                      // a name starting with it
            else if (at != std::string::npos) rank = 3;      // a name containing it
        }
        if (rank >= 0) ranked.push_back({rank, &t});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& r : ranked) out.push_back(r.second);
}

// The club's domestic league, "national team", or "" when unknown
std::string league_text(App& app, int64_t teamid) {
    if (app.model.is_national_team(teamid)) return "national team";
    const Geo& g = geo(app);
    const int64_t lg = g.league_of_team(teamid);
    if (lg < 0) return "";
    const std::string n = g.league_name(lg);
    return n.empty() ? "league " + std::to_string(lg) : n;
}

// Up / Down in the box move the highlighted row of the dropdown
int history_keys(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        g_edit.cursor += d->EventKey == ImGuiKey_UpArrow ? -1 : 1;
        g_edit.keyed = g_edit.scroll = true;
    }
    return 0;
}

}  // namespace

bool club_search_box(App& app, const char* label, int64_t& teamid, float width, const char* hint) {
    const int64_t before = teamid;
    const ImGuiID box = ImGui::GetID(label);
    // idle, the box shows the chosen club: its name, or the number no club has
    char idle[sizeof(g_edit.text)] = "";
    if (teamid != 0) {
        const TeamRow* t = app.model.team(teamid);
        std::snprintf(idle, sizeof(idle), "%s", t ? t->name.c_str() : std::to_string(teamid).c_str());
    }
    ImGui::SetNextItemWidth(width);
    const bool enter = ImGui::InputTextWithHint(label, hint ? hint : "club name or ID", g_edit.id == box ? g_edit.text : idle, sizeof(idle),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll |
                                                    ImGuiInputTextFlags_CallbackHistory,
                                                history_keys);
    const bool active = ImGui::IsItemActive(), activated = ImGui::IsItemActivated(), edited = ImGui::IsItemEdited();
    const bool left = ImGui::IsItemDeactivatedAfterEdit();
    const ImVec2 box_min = ImGui::GetItemRectMin(), box_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemHovered() && !active) ImGui::SetTooltip("Type a club's name (or part of it) or its ID, then pick it in the list");
    if (activated) {
        g_edit = Edit();
        g_edit.id = box;
        std::snprintf(g_edit.text, sizeof(g_edit.text), "%s", idle);
    }

    bool close = false;
    std::string q;
    if (g_edit.id == box) {
        if (edited) {
            g_edit.typed = g_edit.scroll = true;
            g_edit.keyed = false;
            g_edit.cursor = 0;
        }
        q = g_edit.typed ? fold_search(trimmed(g_edit.text)) : std::string();
        if (q != g_edit.query || g_edit.version != app.model.version()) {
            find_clubs(app, q, g_edit.hits);
            g_edit.query = q;
            g_edit.version = app.model.version();
        }
        const std::vector<const TeamRow*>& hits = g_edit.hits;
        if (g_edit.cursor < 0) {  // just clicked: the chosen club's row
            for (size_t i = 0; i < hits.size() && g_edit.cursor < 0; ++i)
                if (hits[i]->teamid == teamid) g_edit.cursor = static_cast<int>(i);
            g_edit.scroll = true;
        }
        g_edit.cursor = std::max(0, std::min(g_edit.cursor, static_cast<int>(hits.size()) - 1));
        // Enter, or leaving the box after typing (not a click on the dropdown, not Escape): the highlighted club; an
        // emptied box clears the choice; Enter on an untouched box keeps it
        const bool on_drop = ImGui::IsMouseHoveringRect(g_edit.drop_min, g_edit.drop_max, false);
        if (enter || (left && !on_drop && !ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            if (g_edit.keyed || (g_edit.typed && !q.empty())) {
                if (!hits.empty()) teamid = hits[static_cast<size_t>(g_edit.cursor)]->teamid;
                else if (is_number(q)) teamid = std::atoll(q.c_str());
            } else if (g_edit.typed) {
                teamid = 0;
            }
            close = true;
        }
    }

    // The dropdown: a child of this window, so the box keeps the keyboard while it is open (Dear ImGui issue #718)
    ImGui::PushID(label);
    if (g_edit.id == box && active && !close && !ImGui::IsPopupOpen("##clubdrop")) ImGui::OpenPopup("##clubdrop");
    const float drop_w = std::max(width, S(380.0f));
    const float row_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const int rows = g_edit.id == box ? static_cast<int>(std::min<size_t>(g_edit.hits.size(), 10)) : 0;
    const float table_h = (rows + 1) * row_h + S(4.0f);
    const float drop_h = table_h + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
    // below the box, or above it when the screen has no room below
    const bool above = box_max.y + drop_h > ImGui::GetIO().DisplaySize.y && box_min.y - drop_h >= 0.0f;
    ImGui::SetNextWindowPos(ImVec2(box_min.x, above ? box_min.y : box_max.y), ImGuiCond_Always, ImVec2(0.0f, above ? 1.0f : 0.0f));
    if (ImGui::BeginPopup("##clubdrop", ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav)) {
        if (g_edit.id != box) {
            ImGui::CloseCurrentPopup();
        } else {
            const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
            g_edit.drop_min = pos;
            g_edit.drop_max = ImVec2(pos.x + size.x, pos.y + size.y);
            const std::vector<const TeamRow*>& hits = g_edit.hits;
            if (hits.empty()) {
                ImGui::TextDisabled(is_number(q) ? "No club has ID %s: Enter keeps the number" : "No club matches \"%s\"", trimmed(g_edit.text).c_str());
            } else if (ImGui::BeginTable("##clubrows", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                                         ImVec2(drop_w, table_h))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Club", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
                ImGui::TableSetupColumn("League", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableHeadersRow();
                ImGuiListClipper clip;
                clip.Begin(static_cast<int>(hits.size()));
                if (g_edit.scroll) clip.IncludeItemByIndex(g_edit.cursor);
                while (clip.Step()) {
                    for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                        const TeamRow* t = hits[static_cast<size_t>(i)];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        char lbl[192];
                        std::snprintf(lbl, sizeof(lbl), "%s##club%lld", t->name.c_str(), static_cast<long long>(t->teamid));
                        if (ImGui::Selectable(lbl, i == g_edit.cursor, ImGuiSelectableFlags_SpanAllColumns)) {
                            teamid = t->teamid;
                            close = true;
                        }
                        if (i == g_edit.cursor && g_edit.scroll) {
                            ImGui::SetScrollHereY(0.5f);
                            g_edit.scroll = false;
                        }
                        ImGui::TableNextColumn();
                        ImGui::Text("%lld", static_cast<long long>(t->teamid));
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", league_text(app, t->teamid).c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("%zu club%s. Up / Down, Enter picks; Esc keeps the club chosen before.", hits.size(), hits.size() == 1 ? "" : "s");
            // open while the box has the keyboard, or while a row is pressed (the click left the box, the pick comes on release)
            const bool pressing_row = ImGui::IsAnyItemActive() && ImGui::IsMouseHoveringRect(g_edit.drop_min, g_edit.drop_max, false);
            if (close || (!active && !pressing_row)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // the typing is over (a pick, Enter, Escape, or a click somewhere else): the box shows the chosen club again
    if (g_edit.id == box && (close || (!active && !ImGui::IsPopupOpen("##clubdrop")))) g_edit.id = 0;
    ImGui::PopID();

    // after the box: the chosen club's ID and league (while typing, the club still chosen)
    if (teamid != 0) {
        ImGui::SameLine();
        if (const TeamRow* t = app.model.team(teamid)) {
            const std::string lg = league_text(app, teamid);
            if (g_edit.id == box) ImGui::TextDisabled("now: %s (ID %lld)", t->name.c_str(), static_cast<long long>(teamid));
            else ImGui::TextDisabled("ID %lld%s%s", static_cast<long long>(teamid), lg.empty() ? "" : ", ", lg.c_str());
        } else {
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "no club has ID %lld", static_cast<long long>(teamid));
        }
    }
    return teamid != before;
}

bool club_search_box(App& app, const char* label, int& teamid, float width, const char* hint) {
    int64_t id = teamid;
    const bool changed = club_search_box(app, label, id, width, hint);
    teamid = static_cast<int>(id);
    return changed;
}

}  // namespace turbo
