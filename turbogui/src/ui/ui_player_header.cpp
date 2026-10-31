// FC 27 LE Turbo GUI - Players > Player: the header card above the tabs (ui_player_header.h; brief 3.0-shell, track B).
#include "ui_player_header.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "app.h"
#include "core/overall.h"
#include "core/ovr_mask.h"
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

// 3.2 Shown OVR: the box text and what the career file holds, read again when the player or career changes and after Set / Clear
struct ShownOvr {
    int64_t pid = -1;
    std::string career;
    int value = 0;
    char buf[8] = {0};
    std::string note;
};
ShownOvr g_shown;

void shown_ovr_reload(App& app, int64_t pid) {
    const std::string career = app.career_faces_wanted();
    g_shown.pid = pid;
    g_shown.career = career;
    g_shown.value = ovr_mask::get(app.turbo_output(), career, pid);
    const std::string t = g_shown.value > 0 ? std::to_string(g_shown.value) : std::string();
    std::snprintf(g_shown.buf, sizeof g_shown.buf, "%s", t.c_str());
}

}  // namespace

bool shown_ovr_apply(App& app, int64_t pid, const std::string& text_in, std::string* msg) {
    std::string text = text_in;
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    std::string err;
    std::string ok_msg;
    const std::string career = app.career_faces_wanted();
    bool ok = false;
    if (career.empty()) {
        err = "Shown OVR works only inside a career Turbo has identified: nothing written";
    } else if (text.empty()) {
        // an empty (or all-spaces) box clears only what is stored; with nothing stored it says so and writes nothing
        ovr_mask::Map stored;
        if (!ovr_mask::load(ovr_mask::file_for(app.turbo_output(), career), stored, &err)) {
            // unreadable file: err says why, nothing written
        } else if (stored.find(pid) == stored.end()) {
            err = "Nothing to clear: no Shown OVR is stored for this player";
        } else {
            ok = ovr_mask::clear(app.turbo_output(), career, pid, &err);
            ok_msg = "Cleared";
        }
    } else {
        const bool digits = text.size() <= 3 && text.find_first_not_of("0123456789") == std::string::npos;
        const int v = digits ? std::atoi(text.c_str()) : -1;
        ok = ovr_mask::set(app.turbo_output(), career, pid, v, &err);  // refuses anything outside 1..99
        ok_msg = "Shown OVR set to " + std::to_string(v) + " (display only)";
    }
    if (msg) *msg = ok ? ok_msg : err;
    if (ok) shown_ovr_reload(app, pid);  // a refused write keeps what was typed in the box
    return ok;
}

namespace {
// Turbo X: the "shown as" line inside the OVR tile: the box, Set, Clear (ids kept); the note of the last Set / Clear goes under the tiles
void draw_shown_ovr(App& app, PlayerHeaderState& st) {
    if (g_shown.pid != st.playerid || g_shown.career != app.career_faces_wanted()) {
        shown_ovr_reload(app, st.playerid);
        g_shown.note.clear();
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontSmall);
    ImGui::AlignTextToFramePadding();
    theme::muted_text("shown as");
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shown OVR, display only: a number from 1 to 99 kept for this player in this career only.\n"
                          "Nothing is written to the game's database or memory; OVR above stays what the game stores.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(40.0f));
    const bool enter = ImGui::InputText("###hdr_shown_ovr", g_shown.buf, sizeof g_shown.buf,
                                        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (g_shown.career.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("Only inside a career Turbo has identified");
    ImGui::SameLine();
    const bool set = ImGui::SmallButton("Set###hdr_shown_set");
    ImGui::SameLine();
    const bool clr = ImGui::SmallButton("Clear###hdr_shown_clear");
    if (clr) {
        std::string msg;
        shown_ovr_apply(app, st.playerid, std::string(), &msg);
        g_shown.note = msg;
    } else if (enter || set) {
        std::string msg;
        shown_ovr_apply(app, st.playerid, std::string(g_shown.buf), &msg);
        g_shown.note = msg;
    }
    st.shown_ovr = g_shown.value;
    st.shown_note = g_shown.note;
}

// One small muted line at the foot of a stat tile ("" draws the blank line, so the tiles stay level)
void tile_foot(const char* text) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontSmall);
    ImGui::AlignTextToFramePadding();
    theme::muted_text("%s", text);
    ImGui::PopFont();
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
    // one muted line, middle dots between club, positions, age and nationality (DESIGN_SPEC_X.md "hero header")
    static const char kDot[] = {' ', '\xc2', '\xb7', ' ', 0};  // " . " with a middle dot (U+00B7), as chars: not a path for the word gate
    std::string details = st.club_line;
    const std::string pos = positions_text(*p);
    if (!pos.empty()) details += kDot + pos;
    if (p->age >= 0) details += kDot + std::to_string(p->age);
    const int64_t nat = app.db.get_int(*t, p->rec, "nationality", -1);
    const std::string nation = nat >= 0 ? app.nation_name(nat) : std::string();
    if (!nation.empty()) details += kDot + nation;
    st.details_line = details;
    st.calc_overall = calc_overall(app, *t, *p);

    // The hero: picture 96 px | name 1.6x, the line, three stat tiles (OVR with the "shown as" field, POT, Calc); Undo top right
    ImGui::BeginChild("##pe_header", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    PictureInfo info;
    draw_legacy_picture(app, legacy_path::player_miniface(p->playerid), S(96.0f), true, &info);
    st.picture = info.drawn;
    ImGui::SameLine(0, S(16.0f));
    ImGui::BeginGroup();
    const float x0 = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    theme::push_title_font(1.6f);
    ImGui::TextUnformatted(st.name.c_str());
    theme::pop_title_font();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player ID %lld", static_cast<long long>(p->playerid));
    {
        const size_t n = app.undo_count(p->playerid);
        char ul[48];
        std::snprintf(ul, sizeof ul, "Undo (%zu)###hdr_undo", n);
        const float w = ImGui::CalcTextSize(ul, ImGui::FindRenderedTextEnd(ul)).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(x0 + avail - w);
        if (n == 0) ImGui::BeginDisabled();
        if (ImGui::Button(ul)) app.undo(p->playerid);
        if (n == 0) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Puts back the last change made to this player");
    }
    theme::muted_text("%s", st.details_line.c_str());
    ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
    const float tile_h = theme::stat_tile_height(1);
    const ImVec4 key = theme::accent();
    char num[16];
    std::snprintf(num, sizeof num, "%d", st.overall);
    theme::stat_tile_begin("##hdr_ovr", "OVR", num, S(kShowOvrMaskField ? 176.0f : 96.0f), tile_h, &key);
    if (kShowOvrMaskField) draw_shown_ovr(app, st);  // on since 10-31 (tx-ovr), see kShowOvrMaskField in the header
    else tile_foot("");
    theme::stat_tile_end();
    ImGui::SameLine(0, S(8.0f));
    std::snprintf(num, sizeof num, "%d", st.potential);
    theme::stat_tile_begin("##hdr_pot", "POT", num, S(96.0f), tile_h);
    char foot[32];
    if (st.potential > st.overall) std::snprintf(foot, sizeof foot, "+%d to grow", st.potential - st.overall);
    else foot[0] = 0;
    tile_foot(foot);
    theme::stat_tile_end();
    ImGui::SameLine(0, S(8.0f));
    if (st.calc_overall >= 0) std::snprintf(num, sizeof num, "%d", st.calc_overall);
    else std::snprintf(num, sizeof num, "-");
    theme::stat_tile_begin("##hdr_calc", "Calc", num, S(96.0f), tile_h);
    if (st.calc_overall >= 0) std::snprintf(foot, sizeof foot, "%+d vs OVR", st.calc_overall - st.overall);
    else foot[0] = 0;
    tile_foot(foot);
    theme::stat_tile_end();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The overall the attributes give for his first position (the game shows OVR until it works it out again)");
    if (!st.shown_note.empty()) theme::muted_text("%s", st.shown_note.c_str());
    ImGui::EndGroup();
    ImGui::EndChild();
    return true;
}

}  // namespace turbo
