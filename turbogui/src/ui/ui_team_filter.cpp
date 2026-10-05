// FC 27 LE Turbo GUI - club filter combo (ui_team_filter.h).
#include "ui_team_filter.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <vector>

#include "app.h"
#include "geo.h"
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

static int g_geo_open_frame = -1;
bool geo_popup_open() { return g_geo_open_frame == ImGui::GetFrameCount(); }

bool geo_pick_combo(const char* id, const char* any_label, const std::string& preview, int64_t& sel, float width,
                    const std::function<void(std::vector<GeoOption>&)>& fill) {
    const int64_t before = sel;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        g_geo_open_frame = ImGui::GetFrameCount();
        static char search[64] = "";  // one popup is open at a time
        if (ImGui::IsWindowAppearing()) {
            search[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter = ImGui::InputTextWithHint("##geoq", "type to search", search, sizeof(search), ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string q = lower_copy(search);
        std::vector<GeoOption> all, hits;
        fill(all);
        for (const auto& o : all)
            if (q.empty() || lower_copy(o.name).find(q) != std::string::npos) hits.push_back(o);
        if (enter && !q.empty() && !hits.empty()) {
            sel = hits.front().id;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable(any_label, sel < 0)) sel = -1;
        if (hits.empty()) ImGui::TextDisabled("no match");
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(hits.size()));
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const GeoOption& o = hits[static_cast<size_t>(i)];
                char lbl[192];
                std::snprintf(lbl, sizeof(lbl), "%s##geo%lld", o.name.c_str(), static_cast<long long>(o.id));
                if (ImGui::Selectable(lbl, sel == o.id)) sel = o.id;
            }
        }
        ImGui::EndCombo();
    }
    return sel != before;
}

bool country_filter_combo(App& app, const char* id, int64_t& nation, float width, const std::vector<int64_t>* only) {
    const Geo& g = geo(app);
    std::string preview = "Any country";
    if (nation >= 0) {
        preview = g.nation_name(nation);
        if (preview.empty()) preview = "Country " + std::to_string(nation);
    }
    return geo_pick_combo(id, "Any country", preview, nation, width, [&](std::vector<GeoOption>& out) {
        for (const GeoNation* n : g.sorted_nations()) {
            if (only && std::find(only->begin(), only->end(), n->id) == only->end()) continue;
            out.push_back({n->id, n->name});
        }
    });
}

bool league_filter_combo(App& app, const char* id, int64_t& league, int64_t nation, float width) {
    const Geo& g = geo(app);
    std::string preview = "Any league";
    if (league >= 0) {
        preview = g.league_name(league);
        if (preview.empty()) preview = "League " + std::to_string(league);
    }
    return geo_pick_combo(id, "Any league", preview, league, width, [&](std::vector<GeoOption>& out) {
        for (const GeoLeague* l : g.sorted_leagues(nation)) out.push_back({l->id, l->name});
    });
}

bool continent_filter_combo(App& app, const char* id, int64_t& conf, float width) {
    const Geo& g = geo(app);
    const std::string preview = conf >= 0 ? g.continent_name(static_cast<int>(conf)) : std::string("Any continent");
    return geo_pick_combo(id, "Any continent", preview, conf, width, [&](std::vector<GeoOption>& out) {
        for (int c : g.sorted_continents()) out.push_back({c, g.continent_name(c)});
    });
}

bool team_in_geo(App& app, const TeamRow& t, int64_t nation, int64_t league) {
    if (nation < 0 && league < 0) return true;
    const Geo& g = geo(app);
    if (league >= 0 && g.league_of_team(t.teamid) != league) return false;
    if (nation >= 0 && g.nation_of_team(t.teamid) != nation) return false;
    return true;
}

bool team_filter_combo(App& app, const char* id, int64_t& teamid, char* search, size_t search_size, float width, int64_t* nation,
                       int64_t* league) {
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
        if (nation || league) {  // optional country / league narrowing
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (nation) {
                const bool changed = country_filter_combo(app, "##teamcountry", *nation, half);
                const auto lg = geo(app).leagues.find(league ? *league : -1);
                if (changed && lg != geo(app).leagues.end() && *nation >= 0 && lg->second.nation != *nation) *league = -1;
                if (league) ImGui::SameLine();
            }
            if (league) league_filter_combo(app, "##teamleague", *league, nation ? *nation : -1, half);
        }
        std::vector<const TeamRow*> hits;
        for (const auto& t : app.model.teams())
            if (team_matches(t, q) && team_in_geo(app, t, nation ? *nation : -1, league ? *league : -1)) hits.push_back(&t);
        if (enter && !q.empty() && !hits.empty()) {  // Enter with nothing typed keeps the current choice
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
