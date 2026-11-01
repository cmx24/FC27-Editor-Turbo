// FC 27 LE Turbo GUI - Players > Edit player > Profile and Positions, attributes & PlayStyles (see ui_player_profile.h; brief 2.20-IA-pages).
// New file; every editor on these pages is one that already exists. The two pages replace the Profile, Names, Attributes, PlayStyles and
// Callname tabs of the old Edit a player panel (ui_players.cpp, untouched). Words, not codes: an ID shows on hover only.
#include "ui_player_profile.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "core/field_labels.h"
#include "core/model.h"
#include "imgui.h"
#include "playstyles.h"
#include "table_registry.h"
#include "ui_archetype_populate.h"
#include "ui_names.h"
#include "ui_sections.h"
#include "ui_setpiece_card.h"
#include "ui_shell.h"
#include "ui_split.h"
#include "ui_theme.h"
#include "ui_club_search.h"

namespace turbo {

ArchetypeBarFn g_ability_archetype_bar = nullptr;

namespace {

PlayerProfileState g_pstate;
PlayerAbilityState g_astate;
shell::Page g_team_page = shell::Page::TeamPlayers;

struct ProfileMoveState {
    int64_t to_team = 0;
    int fee = 0;
    int wage = 0;
    int months = 36;
    int loan_months = 12;

    bool ask_open = false;
    std::string ask_title;
    std::string ask_body;
    nlohmann::json ask_cmd;
};
static ProfileMoveState s_pmove;

// ---------------------------------------------------------------- the player list (shared by both pages)
// 2.22: the list is the Classic editor's own (draw_classic_player_list, ui_players.cpp): the full filter grid (club, nationality, continent,
// position, PlayStyle, age / overall / potential ranges, gender, loans, retiring, free agents) and, since Turbo X, search + chips above
// rows that are people. Its filters and the pick (App::sel_player) are shared by both pages. The divider can be dragged.
size_t g_listed = 0;
bool g_in_pane = false;  // the editor is drawn in list_and_editor's pane (the pane draws the empty state, picked() stays quiet)

// Turbo X (DESIGN_SPEC_X.md): nobody picked: the editor pane says what to do in one line and offers the two likeliest actions. The search
// box (in the list pane) is focused once, the first time the state shows (classic_player_list_request, ui_players.cpp).
void empty_editor(App& app) {
    static bool s_shown = false;
    if (!s_shown) {
        s_shown = true;
        classic_player_list_request(true, false);
    }
    theme::empty_state("Pick a player", "Search on the left, or start from one of these");
    const float bw = S(140.0f);
    theme::center_x(bw * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("My club###pe_empty_club", ImVec2(bw, 0.0f))) classic_player_list_request(false, true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lists the players of your club (needs a career)");
    ImGui::SameLine();
    if (theme::primary_button("Create & clone###pe_empty_create", ImVec2(bw, 0.0f))) app.go(shell::Page::CreatePlayer);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Players > Create & clone: a new player, or a copy of one");
}

void list_and_editor(App& app, void (*editor_fn)(App&, void*), void* user = nullptr) {
    // 5.0c base-32-red: the divider goes through split::pane_x like every other list / editor divider, so its width is remembered per page in
    // gui_settings.json gui.splits ("PlayerEdit/##select") and committed when the drag is released (E6); it was a raw ResizeX child that only
    // the ImGui layout file kept, which left the Edit a player divider outside the "saved once, per page" contract (test_ux_resize.h).
    // The first-use width is the spec's 320 px (unscaled for the store; Turbo X: people rows, not an 11-column table); the child id "##select" is unchanged.
    split::pane_x("##select", 320.0f);
    draw_classic_player_list(app);
    g_listed = classic_player_list_rows();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##editor", ImVec2(0, 0), ImGuiChildFlags_Borders);
    g_in_pane = true;
    if (editor_fn) editor_fn(app, user);  // nobody picked: it resets its state and draws nothing (picked), the empty state below speaks
    g_in_pane = false;
    if (!app.model.player(app.sel_player)) empty_editor(app);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- shared pieces of an editor
// The picked player and his table, or false after drawing the reason (nobody picked, the data changed)
bool picked(App& app, const Table*& t, const PlayerRow*& p) {
    t = app.db.table("players");
    p = app.model.player(app.sel_player);
    if (!t || !p) {
        if (!g_in_pane) theme::muted_text("Pick a player on the left.");
        return false;
    }
    if (!app.db.table_alive(*t, p->rec)) {
        theme::err_text("The game data changed (another career was loaded?). Use Refresh at the top.");
        return false;
    }
    return true;
}

std::string headline_of(const PlayerRow& p) {
    char head[200];
    std::snprintf(head, sizeof(head), "%s, %s, overall %d, potential %d", position_name(p.position),
                  p.club_name.empty() ? "no club" : p.club_name.c_str(), p.overall, p.potential);
    return head;
}

// name, one line of facts (the ID on hover), Undo of his last field edit
void editor_header(App& app, const PlayerRow& p, const std::string& head, const char* undo_id) {
    ImGui::TextUnformatted(p.name.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player ID %lld", static_cast<long long>(p.playerid));
    ImGui::SameLine();
    theme::muted_text("%s", head.c_str());
    const size_t n = app.undo_count(p.playerid);
    char ul[64];
    std::snprintf(ul, sizeof(ul), "Undo (%zu)%s", n, undo_id);
    const float w = ImGui::CalcTextSize(ul).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x - w;
    if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
    if (n == 0) ImGui::BeginDisabled();
    if (ImGui::SmallButton(ul)) app.undo(p.playerid);
    if (n == 0) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Puts back the last change made to this player");
    ImGui::Separator();
}

// One section: a header that is open at first; the caller draws the content when this returns true
bool section(const char* label) { return ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen); }

// A field's value in words ("" when the table has no such field)
std::string shown(App& app, const Table& t, uint64_t rec, const char* field) {
    const Field* f = t.field(field);
    if (!f) return "";
    Value v;
    if (!app.db.get(t, rec, *f, v)) return "";
    return app.describe_value(t, *f, v);
}

std::string with_commas(long long n) {
    std::string s = std::to_string(n < 0 ? -n : n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return n < 0 ? "-" + s : s;
}

// ---------------------------------------------------------------- Profile
void profile_club_summary(App& app, const Table& t, const PlayerRow& p) {
    // the club (and loan) in words; every team he is linked to with his shirt number
    std::string club = p.club_name.empty() ? "none (free agent)" : p.club_name;
    if (p.on_loan && p.loaned_from > 0) club += " (on loan from " + app.model.team_name(p.loaned_from) + ")";
    else if (p.on_loan) club += " (on loan)";
    g_pstate.club_line = "Club: " + club;
    ImGui::TextUnformatted(g_pstate.club_line.c_str());
    if (ImGui::IsItemHovered() && p.club > 0) ImGui::SetTooltip("Club ID %lld", static_cast<long long>(p.club));

    std::string contract;
    if (t.field("contractvaliduntil")) contract = "Contract until " + shown(app, t, p.rec, "contractvaliduntil");
    if (t.field("wage")) {
        const long long wage = static_cast<long long>(app.db.get_int(t, p.rec, "wage", 0));
        contract += std::string(contract.empty() ? "Wage " : ", wage ") + with_commas(wage);
    }
    g_pstate.contract_line = contract;
    if (!contract.empty()) ImGui::TextUnformatted(contract.c_str());
    if (t.field("releaseclause")) {
        const long long rc = static_cast<long long>(app.db.get_int(t, p.rec, "releaseclause", 0));
        if (rc > 0) ImGui::Text("Release clause %s", with_commas(rc).c_str());
    }
    if (const Table* lt = app.db.table("teamplayerlinks")) {
        for (const auto& l : app.model.links_of_player(p.playerid)) {
            std::string line = app.model.team_name(l.teamid);
            if (app.model.is_national_team(l.teamid)) line += " (national team)";
            const int64_t shirt = app.db.get_int(*lt, l.rec, "jerseynumber", -1);
            if (shirt >= 0) line += ", shirt number " + std::to_string(shirt);
            theme::muted_text("%s", line.c_str());
        }
    }
    if (const Field* jf = t.field("playerjointeamdate")) date_field_editor(app, t, p.rec, *jf, "Joined his club");
    theme::muted_text("Contract, morale, growth and squad role are edited under Team > Players.");
    if (ImGui::Button("Open Team > Players###pf_team")) app.go(g_team_page);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Contract, morale, growth and squad role of the players of one club");
    g_pstate.team_link_shown = true;

    ImGui::Separator();
    theme::push_title_font(1.1f);
    ImGui::TextUnformatted("Direct move and contract");
    theme::pop_title_font();

    club_search_box(app, "To club##pf_dest_club", s_pmove.to_team, S(240.0f), "Search club");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(90.0f));
    ImGui::InputInt("Transfer fee##pf_fee", &s_pmove.fee, 0);
    if (s_pmove.fee < 0) s_pmove.fee = 0;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Transfer fee");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(70.0f));
    ImGui::InputInt("Weekly wage##pf_wage", &s_pmove.wage, 0);
    if (s_pmove.wage < 0) s_pmove.wage = 0;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Weekly wage (0 = keep current)");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(60.0f));
    ImGui::InputInt("Contract length##pf_months", &s_pmove.months, 0);
    if (s_pmove.months < 1) s_pmove.months = 1;
    if (s_pmove.months > 120) s_pmove.months = 120;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Contract months");

    const TeamRow* dest = app.model.team(s_pmove.to_team);
    const bool can_move = (s_pmove.to_team > 0 && dest != nullptr);

    if (!can_move) ImGui::BeginDisabled();
    if (ImGui::Button("Transfer###pf_btn_transfer")) {
        s_pmove.ask_open = true;
        s_pmove.ask_title = "Confirm move###prof_move_ask";
        s_pmove.ask_body = "Transfer " + p.name + " to " + (dest ? dest->name : "destination") + "?";
        nlohmann::json action = {
            {"action", "transfer"},
            {"playerid", p.playerid},
            {"to_teamid", s_pmove.to_team},
            {"fee", s_pmove.fee},
            {"wage", s_pmove.wage},
            {"months", s_pmove.months}
        };
        s_pmove.ask_cmd = {
            {"op", "run"},
            {"module", "player_moves"},
            {"overrides", {{"actions", nlohmann::json::array({action})}}}
        };
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Transfer player to selected club");

    ImGui::SameLine();
    if (ImGui::Button("Loan###pf_btn_loan")) {
        s_pmove.ask_open = true;
        s_pmove.ask_title = "Confirm move###prof_move_ask";
        s_pmove.ask_body = "Loan " + p.name + " to " + (dest ? dest->name : "destination") + " for " + std::to_string(s_pmove.loan_months) + " months?";
        nlohmann::json action = {
            {"action", "loan"},
            {"playerid", p.playerid},
            {"to_teamid", s_pmove.to_team},
            {"months", s_pmove.loan_months}
        };
        s_pmove.ask_cmd = {
            {"op", "run"},
            {"module", "player_moves"},
            {"overrides", {{"actions", nlohmann::json::array({action})}}}
        };
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Send player on loan to selected club");
    if (!can_move) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Release to free agent###pf_btn_release")) {
        s_pmove.ask_open = true;
        s_pmove.ask_title = "Confirm move###prof_move_ask";
        s_pmove.ask_body = "Release " + p.name + " to Free Agents?";
        nlohmann::json action = {
            {"action", "release"},
            {"playerid", p.playerid}
        };
        s_pmove.ask_cmd = {
            {"op", "run"},
            {"module", "player_moves"},
            {"overrides", {{"actions", nlohmann::json::array({action})}}}
        };
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Terminate contract and release player to free agents");

    if (s_pmove.ask_open) {
        ImGui::OpenPopup("Confirm move###prof_move_ask");
        s_pmove.ask_open = false;
    }
    if (ImGui::BeginPopupModal("Confirm move###prof_move_ask", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(s_pmove.ask_body.c_str());
        ImGui::Spacing();
        if (theme::primary_button("Confirm###pf_confirm_btn")) {
            app.send(s_pmove.ask_cmd, "Transfer");
            app.notify("Move requested.", false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###pf_cancel_btn")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void profile_editor(App& app, void*) {
    g_pstate.team_link_shown = false;
    g_pstate.callname_shown = false;
    const Table* t = nullptr;
    const PlayerRow* p = nullptr;
    if (!picked(app, t, p)) {
        g_pstate.playerid = 0;
        return;
    }
    g_pstate.playerid = p->playerid;
    g_pstate.listed = g_listed;
    g_pstate.headline = headline_of(*p);
    editor_header(app, *p, g_pstate.headline, "###pf_undo");

    if (section("Name shown in game###pf_names")) names_editor(app, *t, *p);

    if (section("What the commentator says###pf_callname")) {
        g_pstate.callname_shown = true;
        callname_editor(app, *t, *p);
    }

    if (section("Personal details###pf_details")) {
        if (p->age >= 0) theme::muted_text("Age %d", p->age);
        if (const Field* bf = t->field("birthdate")) date_field_editor(app, *t, p->rec, *bf, "Date of birth");
        field_grid(app, *t, p->rec, {"nationality", "internationalrep", "height", "weight", "gender", "personality", "emotion", "isretiring"},
                   "##pf_detailgrid", 2);
        if (ImGui::TreeNodeEx("Name editing and custom flags###pf_flags", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            field_grid(app, *t, p->rec, {"usercaneditname", "iscustomized"}, "##pf_flaggrid", 2);
            ImGui::TreePop();
        }
    }

    if (section("Club and contract###pf_club")) profile_club_summary(app, *t, *p);
}

// ---------------------------------------------------------------- Positions, attributes & PlayStyles
const std::vector<std::pair<const char*, std::vector<const char*>>>& attribute_groups() {
    static const std::vector<std::pair<const char*, std::vector<const char*>>> g = {
        {"Pace", {"acceleration", "sprintspeed"}},
        {"Shooting", {"positioning", "finishing", "shotpower", "longshots", "volleys", "penalties"}},
        {"Passing", {"vision", "crossing", "freekickaccuracy", "shortpassing", "longpassing", "curve"}},
        {"Dribbling", {"agility", "balance", "reactions", "ballcontrol", "dribbling", "composure"}},
        {"Defending", {"interceptions", "headingaccuracy", "defensiveawareness", "marking", "standingtackle", "slidingtackle"}},
        {"Physical", {"jumping", "stamina", "strength", "aggression"}},
        {"Goalkeeping", {"gkdiving", "gkhandling", "gkkicking", "gkpositioning", "gkreflexes"}},
    };
    return g;
}

// The ticked boxes of a bit field in words: "Finesse Shot, Rapid"; "none ticked" when nothing is on. A box past the name list reads
// "Unnamed trait 15" (`unnamed` is "Unnamed trait %d" or "Unnamed PlayStyle %d"): no new catalogue, the number is the box.
std::string ticked_names(const Field& f, const Value& v, const std::vector<const char*>& names, const char* unnamed) {
    const int bits = std::min(f.depth, 62);
    std::string out;
    for (int b = 0; b < bits; ++b) {
        if (!((static_cast<uint64_t>(v.i - f.min) >> b) & 1)) continue;
        char name[48];
        if (b < static_cast<int>(names.size()) && names[static_cast<size_t>(b)]) std::snprintf(name, sizeof(name), "%s", names[static_cast<size_t>(b)]);
        else std::snprintf(name, sizeof(name), unnamed, b);
        if (!out.empty()) out += ", ";
        out += name;
    }
    return out.empty() ? "none ticked" : out;
}

// One line of help per PlayStyle / Trait set (a tooltip on its heading); in words, no field name
const char* set_help(const std::string& title) {
    if (title == "PlayStyles") return "Special skills shown on his card; tick the ones he has";
    if (title == "PlayStyles+") return "The stronger version of a PlayStyle; tick the ones he has";
    if (title == "Traits") return "Goalkeeper skills, CPU AI habits and others such as Leadership or Injury Prone";
    return "The stronger version of a Traits box; tick the ones he has";
}

void positions_block(App& app, const Table& t, uint64_t rec) {
    std::string in_words;  // "ST, also RCB (internal, plays as CB)": every position set, named from the same table as the combos below
    for (int k = 1; k <= 7; ++k) {
        const Field* f = t.field("preferredposition" + std::to_string(k));
        if (!f) continue;
        Value v;
        if (!app.db.get(t, rec, *f, v)) continue;
        ImGui::PushID(k);
        char lbl[32];
        if (k == 1) std::snprintf(lbl, sizeof(lbl), "Main position");
        else std::snprintf(lbl, sizeof(lbl), "Position %d", k);
        form_label(lbl, k == 1 ? "The main position (the overall formula follows it)" : "A further preferred position, or None");
        int cur = static_cast<int>(v.i);
        if (position_combo("##ab_pos", cur, S(190.0f), f->min <= -1 ? "None" : nullptr, static_cast<int>(std::max<int64_t>(0, f->min)),
                           static_cast<int>(f->max())))
            app.edit(t, rec, *f, Value::of_int(cur));
        ImGui::PopID();
        ++g_astate.position_rows;
        if (cur >= 0) in_words += (in_words.empty() ? "" : (k == 2 ? ", also " : ", ")) + labels::position_text(cur);
    }
    // the same positions as one line: it stays readable when the combos are narrow
    if (in_words.empty()) in_words = "none set";
    theme::muted_text("Plays as: %s", in_words.c_str());
}

// ---- cards (Turbo X, docs/turbo-x/DESIGN_SPEC_X.md "Attributes & roles")
// A card is a cell of a layout table (##sl, ##bits), not a child window: a child scrolled out of view skips its items (imgui.cpp Begin,
// HiddenFramesCanSkipItems), and a slider below the fold is reached by scrolling to it (tests/native Ui::reach). The cell padding is the
// card's inner padding plus half the gap to its neighbours; the background goes under the content when the card is closed (two draw-list
// channels), so a card is as tall as what it holds.
constexpr float kCardPad = 12.0f;  // unscaled
constexpr float kCardGap = 8.0f;
constexpr float kChipPadX = 10.0f;
constexpr ImVec4 kPillBg{0.20f, 0.25f, 0.32f, 1.0f};  // slate: the accent is for selection and the one key number (principle 8)

struct Card {
    ImVec2 p0;
    float w;
};

void cards_push_style() {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(kCardPad + kCardGap * 0.5f), S(kCardPad + kCardGap * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(8.0f)));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));  // kGrid's inner border would run through the gap
}
void cards_pop_style() {
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

Card card_begin() {
    ImGui::TableNextColumn();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    return {ImVec2(p.x - S(kCardPad), p.y - S(kCardPad)), ImGui::GetContentRegionAvail().x + 2.0f * S(kCardPad)};
}
void card_end(const Card& c) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(c.p0.x + c.w, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + S(kCardPad));
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(c.p0, p1, ImGui::ColorConvertFloat4ToU32(theme::kCardBg), S(10.0f));
    dl->AddRect(c.p0, p1, ImGui::ColorConvertFloat4ToU32(theme::kRule), S(10.0f));
    dl->ChannelsMerge();
}

// The title line of a card: the accent bar and the semibold title as theme::card_title draws them, a pill after the title (the group's
// average, the ticked count), the thin rule under both. `help` shows with the mouse on the title (principle 6: help is a tooltip).
void card_head(const char* title, const char* pill, const char* help) {
    theme::push_title_font(1.12f);
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.14f), ImVec2(p.x + fs * 0.18f, p.y + fs * 0.96f), ImGui::ColorConvertFloat4ToU32(theme::accent()), fs * 0.09f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fs * 0.55f);
    ImGui::TextUnformatted(title);
    theme::pop_title_font();
    if (help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
    if (pill) {
        const float title_h = ImGui::GetItemRectSize().y;
        ImGui::SameLine(0.0f, fs * 0.5f);
        const float pill_h = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 0.8f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (title_h - pill_h) * 0.5f);
        theme::chip(pill, kPillBg);
    }
    const ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(q.x, q.y + 1.0f), ImVec2(q.x + ImGui::GetContentRegionAvail().x, q.y + 1.0f), ImGui::ColorConvertFloat4ToU32(theme::kRule), 1.0f);
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
}

// A chip that toggles: accent fill when on, slate when off. The label keeps its ### id. True when clicked.
bool chip_toggle(const char* label, bool on) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? theme::accent_fill() : ImVec4(0.13f, 0.16f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? theme::accent_fill_hover() : ImVec4(0.17f, 0.21f, 0.27f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? theme::accent_fill_active() : theme::accent_alpha(0.25f));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : theme::muted());
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(kChipPadX), ImGui::GetStyle().FramePadding.y));
    const bool hit = ImGui::Button(label);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return hit;
}

// The attributes: six group cards in two columns (Goalkeeping in place of Shooting for a keeper), the group average as a pill, one
// slider per attribute: label 150 px, slider, value 40 px (slider_editor_ex: the same writes, range check and undo as before)
void attribute_cards(App& app, const Table& t, const PlayerRow& p) {
    const bool keeper = p.positions[0] == 0;
    cards_push_style();
    if (ImGui::BeginTable("##sl", 2, tbl::kGrid | ImGuiTableFlags_SizingStretchSame)) {
        for (const auto& g : attribute_groups()) {
            if (std::strcmp(g.first, keeper ? "Shooting" : "Goalkeeping") == 0) continue;
            long long sum = 0;
            std::vector<const Field*> present;
            for (const char* fn : g.second)
                if (const Field* f = t.field(fn)) {
                    present.push_back(f);
                    sum += app.db.get_int(t, p.rec, fn, 0);
                }
            if (present.empty()) continue;
            const Card card = card_begin();
            const long long n = static_cast<long long>(present.size());
            char pill[16];
            std::snprintf(pill, sizeof(pill), "%lld", (sum + n / 2) / n);
            ImGui::PushID(g.first);
            card_head(g.first, pill, nullptr);
            for (const Field* f : present) {
                slider_editor_ex(app, t, p.rec, *f, field_label(f->name).c_str(), S(150.0f), -FLT_MIN, S(40.0f));
                ++g_astate.attribute_sliders;
            }
            ImGui::PopID();
            card_end(card);
        }
        ImGui::EndTable();
    }
    cards_pop_style();
}

// One bit field (PlayStyles, PlayStyles+, Traits, Traits+) as a card: the ticked boxes in words (4.0 S6), a chip per box that wraps to
// the width, All / None. The same bit write as the old check boxes; the ids stay (###ab_bit<n>, All###ab_all, None###ab_none under the field).
void playstyle_card(App& app, const Table& t, uint64_t rec, const char* field, const std::vector<const char*>& names, const char* title,
                    const char* unnamed) {
    const Card card = card_begin();
    ImGui::PushID(field);
    const Field* f = t.field(field);
    Value v;
    if (!f || !app.db.get(t, rec, *f, v)) {
        card_head(title, nullptr, set_help(title));
        theme::muted_text("Not available in this game version.");
    } else {
        const int bits = std::min(f->depth, 62);
        const uint64_t raw = static_cast<uint64_t>(v.i - f->min);
        int on_count = 0;
        for (int b = 0; b < bits; ++b) on_count += static_cast<int>((raw >> b) & 1);
        char pill[24];
        std::snprintf(pill, sizeof(pill), "%d on", on_count);
        card_head(title, pill, set_help(title));
        const std::string ticked = ticked_names(*f, v, names, unnamed);
        theme::muted_text("Ticked: %s", ticked.c_str());
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (int b = 0; b < bits; ++b) {
            char name[48];
            if (b < static_cast<int>(names.size()) && names[static_cast<size_t>(b)]) std::snprintf(name, sizeof(name), "%s", names[static_cast<size_t>(b)]);
            else std::snprintf(name, sizeof(name), unnamed, b);
            char lbl[96];
            std::snprintf(lbl, sizeof(lbl), "%s###ab_bit%d", name, b);
            const float w = ImGui::CalcTextSize(name).x + 2.0f * S(kChipPadX);
            if (b > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= right) ImGui::SameLine();
            const bool on = (raw >> b) & 1;
            if (chip_toggle(lbl, on)) {
                const uint64_t next = on ? (raw & ~(uint64_t(1) << b)) : (raw | (uint64_t(1) << b));
                app.edit(t, rec, *f, Value::of_int(static_cast<int64_t>(next) + f->min));
            }
            ++g_astate.playstyle_boxes;
        }
        if (ImGui::SmallButton("All###ab_all")) app.edit(t, rec, *f, Value::of_int(f->max()));
        ImGui::SameLine();
        if (ImGui::SmallButton("None###ab_none")) app.edit(t, rec, *f, Value::of_int(f->min));
    }
    ImGui::PopID();
    card_end(card);
}

// The four sets, one card under the other (a set's heading sits between the None button of the set above and its own first chip)
void playstyle_cards(App& app, const Table& t, uint64_t rec) {
    struct Set { const char* title; const char* field; bool second; };
    const Set sets[] = {{"PlayStyles", "trait1", false}, {"PlayStyles+", "icontrait1", false}, {"Traits", "trait2", true}, {"Traits+", "icontrait2", true}};
    cards_push_style();
    if (ImGui::BeginTable("##bits", 1, tbl::kGrid | ImGuiTableFlags_SizingStretchSame)) {
        for (const Set& s : sets)
            playstyle_card(app, t, rec, s.field, s.second ? playstyle2_names() : playstyle1_names(), s.title, s.second ? "Unnamed trait %d" : "Unnamed PlayStyle %d");
        ImGui::EndTable();
    }
    cards_pop_style();
}

// While the full player-type editor (ui_players.cpp archetype_bar) is not hooked up: Overall +1 / -1 and Undo last big change, which use the
// same shared writes (App::set_overall, App::bulk_undo) as the editor does
void archetype_stand_in(App& app, const Table& t, const PlayerRow& p) {
    g_astate.stand_in_shown = true;
    const Field* of = t.field("overallrating");
    const int64_t stored = app.db.get_int(t, p.rec, "overallrating", 0);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Overall %lld", static_cast<long long>(stored));
    for (int delta : {-1, 1}) {
        ImGui::SameLine();
        const bool in_range = of && stored + delta >= std::max<int64_t>(1, of->min) && stored + delta <= std::min<int64_t>(99, of->max());
        if (!in_range) ImGui::BeginDisabled();
        if (ImGui::Button(delta > 0 ? "Overall +1###ab_ovr_up" : "Overall -1###ab_ovr_down")) {
            if (!app.set_overall(t, p.rec, stored + delta)) app.notify("The overall could not be changed this way.", true);
        }
        if (!in_range) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Moves the attributes that count most for his position");
    }
    size_t mine = 0;
    for (const auto& u : app.bulk_undo) mine += u.playerid == p.playerid;
    ImGui::SameLine();
    if (!mine) ImGui::BeginDisabled();
    char ulbl[64];
    std::snprintf(ulbl, sizeof(ulbl), "Undo last big change (%zu)###ab_bulkundo", mine);
    if (ImGui::Button(ulbl)) {
        auto& st = app.bulk_undo;
        for (size_t i = st.size(); i-- > 0;) {
            if (st[i].playerid != p.playerid) continue;
            const App::BulkUndo u = st[i];
            st.erase(st.begin() + static_cast<long>(i));
            for (const auto& b : u.before)
                if (const Field* f = t.field(b.first)) app.edit_field(t, p.rec, *f, Value::of_int(b.second));
            app.notify("undone: " + u.label);
            break;
        }
    }
    if (!mine) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Puts back everything the last Overall +1 / -1 click changed (the Undo button at the top is for single fields)");
    const char* more = "Reshaping to a player type is not available here; Overall +1 / -1 still works.";
    theme::muted_text("%s", more);
}

void ability_editor(App& app, void*) {
    g_astate.archetype_bar_external = false;
    g_astate.stand_in_shown = false;
    g_astate.type_tier_shown = false;
    g_astate.bar_calls = 0;
    g_astate.position_rows = 0;
    g_astate.attribute_sliders = 0;
    g_astate.playstyle_boxes = 0;
    const Table* t = nullptr;
    const PlayerRow* p = nullptr;
    if (!picked(app, t, p)) {
        g_astate.playerid = 0;
        return;
    }
    g_astate.playerid = p->playerid;
    g_astate.listed = g_listed;
    g_astate.headline = headline_of(*p);
    editor_header(app, *p, g_astate.headline, "###ab_undo");

    // 2.22 (Cassio: "the archetype/player tier menu is HIDDEN at the bottom of a very long menu"): the FIRST thing on the tab. One card with the Player
    // type, the Tier, the Wanted overall and the Side, the preview and Populate / Undo (archetype_populate_panel), then the Overall +1 / -1 and the
    // attributes-only reshape of the Classic editor's bar (it takes the type and the wanted overall from the controls above: one picker).
    // Open at first, collapsible, remembered (gui.sections_open "ability.type_tier").
    g_astate.type_tier_shown = false;
    g_astate.bar_calls = 0;
    if (collapsing_section(app, "ability.type_tier", "Player type & tier###ab_typetier", true)) {
        g_astate.type_tier_shown = true;
        archetype_populate_panel(app, *t, *p);
        ImGui::Spacing();
        if (g_ability_archetype_bar) {
            g_astate.archetype_bar_external = true;
            ++g_astate.bar_calls;
            g_ability_archetype_bar(app, *t, *p);
        } else {
            archetype_stand_in(app, *t, *p);
        }
    }
    if (section("Overall and potential###ab_ovr")) {
        field_grid(app, *t, p->rec, {"overallrating", "potential", "modifier"}, "##ab_ovrgrid", 3);
        theme::muted_text("Typing an overall moves the attributes that count most for his position.");
        reapply_player_line(app, *p);  // kept for every career load, his development plan
    }
    if (section("Positions###ab_positions")) positions_block(app, *t, p->rec);
    if (section("Playing style and roles###ab_style")) {
        field_grid(app, *t, p->rec,
                   {"preferredfoot", "weakfootabilitytypecode", "skillmoves", "attackingworkrate", "defensiveworkrate", "skillmoveslikelihood",
                    "gkkickstyle", "runstylecode", "growthprofile"},
                   "##ab_stylegrid", 2);
        // the roles in words, outside the tree (it is closed at first): every role slot that is set, named by the game's role table
        if (t->field("role1")) {
            std::string roles;
            for (int k = 1; k <= 9; ++k) {
                const int64_t rv = app.db.get_int(*t, p->rec, ("role" + std::to_string(k)).c_str(), 0);
                if (rv != 0) roles += (roles.empty() ? "" : ", ") + labels::describe("players", "role" + std::to_string(k), rv);
            }
            if (roles.empty()) roles = "none set";
            theme::muted_text("Roles: %s", roles.c_str());
        }
        if (ImGui::TreeNodeEx("Roles###ab_roles", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            field_grid(app, *t, p->rec, {"role1", "role2", "role3", "role4", "role5", "role6", "role7", "role8", "role9"}, "##ab_rolegrid", 2);
            ImGui::TreePop();
        }
    }
    // 2.22: G-pickers' Set-pieces card (penalty and free-kick takers), ui_setpiece_card.cpp
    setpiece_card::draw(app, p->playerid);
    // Turbo X: six group cards in two columns, then the four PlayStyle / Trait sets as chip cards (DESIGN_SPEC_X.md "Attributes & roles")
    if (section("Attributes###ab_attrs")) attribute_cards(app, *t, *p);
    if (section("PlayStyles and traits###ab_styles")) playstyle_cards(app, *t, p->rec);
}

// The editor alone, framed so that it scrolls like a page
void editor_scroll(App& app, void (*fn)(App&, void*)) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    fn(app, nullptr);
}

}  // namespace

shell::Page& profile_team_players_page() { return g_team_page; }

const PlayerProfileState& player_profile_state() { return g_pstate; }
const PlayerAbilityState& player_ability_state() { return g_astate; }

void draw_player_profile(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    list_and_editor(app, profile_editor);
}
void draw_player_profile_editor(App& app) { editor_scroll(app, profile_editor); }
void draw_player_empty_editor(App& app) { empty_editor(app); }

// 4.0 integration (S3 merge, brief 4.0-gui-pages :77-80, q3): the card of the 4.0 tree draws the host's controls for the picked player at
// the end of the Profile editor (inside its scrolling pane, so they are reachable). Nobody picked: the editor says so and nothing more is drawn.
namespace {
struct ProfileBelow {
    void (*fn)(App&);
};
void profile_editor_below(App& app, void* user) {
    profile_editor(app, nullptr);
    const auto* b = static_cast<const ProfileBelow*>(user);
    if (b && b->fn && g_pstate.playerid != 0) b->fn(app);
}
}  // namespace

void draw_player_profile_with(App& app, void (*below)(App&)) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    ProfileBelow b{below};
    list_and_editor(app, profile_editor_below, &b);
}

void draw_player_ability(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    list_and_editor(app, ability_editor);
}
void draw_player_ability_editor(App& app) { editor_scroll(app, ability_editor); }

}  // namespace turbo
