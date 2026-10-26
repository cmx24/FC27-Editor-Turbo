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
#include "ui_player_page.h"
#include "ui_sections.h"
#include "ui_setpiece_card.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

ArchetypeBarFn g_ability_archetype_bar = nullptr;

namespace {

PlayerProfileState g_pstate;
PlayerAbilityState g_astate;
shell::Page g_team_page = shell::Page::Squad;

// ---------------------------------------------------------------- the player list (shared by both pages)
// 2.22: the list is the Classic editor's own (draw_classic_player_list, ui_players.cpp): eight sortable columns and the full filter grid (club,
// nationality, continent, position, PlayStyle, age / overall / potential ranges, gender, loans, retiring, free agents). Its filters and
// the pick (App::sel_player) are shared with the Classic editor and with both pages. A fixed 300 px pane is too narrow for it: the split is as wide
// as the Classic editor's and its divider can be dragged.
size_t g_listed = 0;

void list_and_editor(App& app, void (*editor_fn)(App&, void*), void* user = nullptr) {
    const float list_w = std::min(S(560.0f), std::max(S(430.0f), ImGui::GetContentRegionAvail().x * 0.45f));
    ImGui::BeginChild("##select", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    draw_classic_player_list(app);
    g_listed = classic_player_list_rows();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##editor", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (editor_fn) editor_fn(app, user);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- shared pieces of an editor
// The picked player and his table, or false after drawing the reason (nobody picked, the data changed)
bool picked(App& app, const Table*& t, const PlayerRow*& p) {
    t = app.db.table("players");
    p = app.model.player(app.sel_player);
    if (!t || !p) {
        theme::muted_text("Pick a player on the left.");
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

// The attributes: one heading per category with its average; two columns when the pane is wide
void attribute_sliders(App& app, const Table& t, uint64_t rec) {
    const bool wide = ImGui::GetContentRegionAvail().x >= S(760.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10.0f), S(10.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6.0f), S(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(14.0f), S(3.0f)));
    for (const auto& g : attribute_groups()) {
        long long sum = 0;
        std::vector<const Field*> present;
        for (const char* fn : g.second)
            if (const Field* f = t.field(fn)) {
                present.push_back(f);
                sum += app.db.get_int(t, rec, fn, 0);
            }
        if (present.empty()) continue;
        ImGui::Dummy(ImVec2(0, S(6.0f)));
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "%s  (average %.1f)", g.first, double(sum) / static_cast<double>(present.size()));
        ImGui::SeparatorText(hdr);
        ImGui::PushID(g.first);
        if (ImGui::BeginTable("##sl", wide ? 2 : 1, tbl::kGrid | ImGuiTableFlags_SizingStretchSame)) {
            // 2.22: a header row (G-ia, the three header-less tables): the heading of the group above names the rows
            ImGui::TableSetupColumn(g.first, ImGuiTableColumnFlags_WidthStretch);
            if (wide) ImGui::TableSetupColumn("(continued)", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const Field* f : present) {
                ImGui::TableNextColumn();
                slider_editor_ex(app, t, rec, *f, field_label(f->name).c_str(), S(wide ? 132.0f : 150.0f), -FLT_MIN, S(58.0f));
                ++g_astate.attribute_sliders;
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar(3);
}

// The check boxes of one bit field (PlayStyles, PlayStyles+, Traits, Traits+) with All / None
void playstyle_bits(App& app, const Table& t, uint64_t rec, const char* field, const std::vector<const char*>& names, const char* title,
                    const char* unnamed) {
    const Field* f = t.field(field);
    if (!f) {
        theme::muted_text("Not available in this game version.");
        return;
    }
    Value v;
    if (!app.db.get(t, rec, *f, v)) return;
    ImGui::PushID(field);
    const int bits = std::min(f->depth, 62);
    // 4.0 S6: the ticked boxes in words, above the grid (the grid is long; this is what the player has at a glance)
    const std::string ticked = ticked_names(*f, v, names, unnamed);
    theme::muted_text("Ticked: %s", ticked.c_str());
    if (ImGui::BeginTable("##bits", 3, tbl::kGrid | ImGuiTableFlags_SizingStretchSame)) {
        // 2.22: a header row (the tick boxes below are the ones this field has)
        ImGui::TableSetupColumn(title, ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("(continued)###bits2", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("(continued)###bits3", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int b = 0; b < bits; ++b) {
            ImGui::TableNextColumn();
            bool on = (static_cast<uint64_t>(v.i - f->min) >> b) & 1;
            char name[48];
            if (b < static_cast<int>(names.size()) && names[static_cast<size_t>(b)]) std::snprintf(name, sizeof(name), "%s", names[static_cast<size_t>(b)]);
            else std::snprintf(name, sizeof(name), unnamed, b);
            char lbl[96];
            std::snprintf(lbl, sizeof(lbl), "%s###ab_bit%d", name, b);
            if (ImGui::Checkbox(lbl, &on)) {
                uint64_t raw = static_cast<uint64_t>(v.i - f->min);
                raw = on ? (raw | (uint64_t(1) << b)) : (raw & ~(uint64_t(1) << b));
                app.edit(t, rec, *f, Value::of_int(static_cast<int64_t>(raw) + f->min));
            }
            ++g_astate.playstyle_boxes;
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("All###ab_all")) app.edit(t, rec, *f, Value::of_int(f->max()));
    ImGui::SameLine();
    if (ImGui::SmallButton("None###ab_none")) app.edit(t, rec, *f, Value::of_int(f->min));
    ImGui::PopID();
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
    if (section("Attributes###ab_attrs")) attribute_sliders(app, *t, p->rec);
    if (section("PlayStyles and traits###ab_styles")) {
        struct Set { const char* title; const char* field; bool second; };
        const Set sets[] = {{"PlayStyles", "trait1", false}, {"PlayStyles+", "icontrait1", false}, {"Traits", "trait2", true}, {"Traits+", "icontrait2", true}};
        for (const Set& s : sets) {
            ImGui::SeparatorText(s.title);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", set_help(s.title));
            playstyle_bits(app, *t, p->rec, s.field, s.second ? playstyle2_names() : playstyle1_names(), s.title,
                           s.second ? "Unnamed trait %d" : "Unnamed PlayStyle %d");
        }
    }
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
