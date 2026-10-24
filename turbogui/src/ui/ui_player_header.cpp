// FC 27 LE Turbo GUI - Players > Player: the header card above the tabs (ui_player_header.h; brief 3.0-shell, track B).
#include "ui_player_header.h"

#include <cstdio>
#include <string>

#include "app.h"
#include "core/overall.h"
#include "imgui.h"
#include "ui_images.h"
#include "ui_theme.h"

namespace turbo {

namespace {

PlayerHeaderState g_state;

// "ST, CF, RW": the player's preferred positions in order (the main position alone when the fields are absent)
std::string positions_text(const PlayerRow& p) {
    std::string s;
    for (int pos : p.positions) {
        if (pos < 0) continue;
        if (!s.empty()) s += ", ";
        s += position_name(pos);
    }
    if (s.empty() && p.position >= 0) s = position_name(p.position);
    return s;
}

// The overall the formula gives for the first position (-1 when an attribute could not be read)
int calc_overall(App& app, const Table& t, const PlayerRow& p) {
    const int pos = p.positions[0] >= 0 ? p.positions[0] : p.position;
    if (pos < 0) return -1;
    overall::Attrs a{};
    for (int i = 0; i < overall::kAttrCount; ++i) {
        const int64_t v = app.db.get_int(t, p.rec, overall::kAttrNames[i], -1);
        if (v < 0) return -1;
        a[static_cast<size_t>(i)] = static_cast<int>(v);
    }
    return overall::compute_overall(pos, a);
}

// A small muted caption above a big number
void big_number(const char* caption, int value, bool muted) {
    ImGui::BeginGroup();
    theme::muted_text("%s", caption);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * (muted ? 1.4f : 2.0f));
    if (value < 0) theme::muted_text("-");
    else if (muted) theme::muted_text("%d", value);
    else ImGui::Text("%d", value);
    ImGui::PopFont();
    ImGui::EndGroup();
}

}  // namespace

const PlayerHeaderState& player_header_state() { return g_state; }

bool draw_player_header(App& app) {
    g_state = PlayerHeaderState{};
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!t || !p || !app.db.table_alive(*t, p->rec)) return false;

    PlayerHeaderState& st = g_state;
    st.playerid = p->playerid;
    st.name = p->name;
    st.overall = p->overall;
    st.potential = p->potential;
    if (p->club_name.empty()) {
        st.club_line = "No club (free agent)";
    } else {
        st.club_line = p->club_name;
        if (p->on_loan) {
            const std::string from = p->loaned_from > 0 ? app.model.team_name(p->loaned_from) : std::string();
            st.club_line += from.empty() ? " (on loan)" : " (on loan from " + from + ")";
        }
    }
    std::string details;
    if (p->age >= 0) details = "Age " + std::to_string(p->age);
    const std::string pos = positions_text(*p);
    if (!pos.empty()) details += (details.empty() ? "" : "  |  ") + pos;
    const int64_t nat = app.db.get_int(*t, p->rec, "nationality", -1);
    const std::string nation = nat >= 0 ? app.nation_name(nat) : std::string();
    if (!nation.empty()) details += (details.empty() ? "" : "  |  ") + nation;
    st.details_line = details;
    st.calc_overall = calc_overall(app, *t, *p);

    const float h = S(84.0f);
    ImGui::BeginChild("##pe_header", ImVec2(0, h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    PictureInfo info;
    draw_legacy_picture(app, legacy_path::player_miniface(p->playerid), S(64.0f), true, &info);
    st.picture = info.drawn;
    ImGui::SameLine(0, S(12.0f));
    const float x0 = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;

    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(st.name.c_str());
    ImGui::PopFont();
    theme::muted_text("%s", st.club_line.c_str());
    if (!st.details_line.empty()) ImGui::TextUnformatted(st.details_line.c_str());
    ImGui::EndGroup();

    // OVR / POT / Calc right-aligned when there is room, else just after the text
    const float right_w = S(170.0f);
    if (avail > right_w + S(220.0f)) ImGui::SameLine(x0 + avail - right_w);
    else ImGui::SameLine(0, S(24.0f));
    big_number("OVR", st.overall, false);
    ImGui::SameLine(0, S(16.0f));
    big_number("POT", st.potential, false);
    ImGui::SameLine(0, S(16.0f));
    big_number("Calc", st.calc_overall, true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The overall the attributes give for his first position (the game shows OVR until it works it out again)");
    ImGui::EndChild();
    return true;
}

}  // namespace turbo
