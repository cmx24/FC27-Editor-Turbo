// FC 27 LE Turbo GUI - Players tab: searchable list on the left, editor on the right.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cfloat>

#include "app.h"
#include "imgui.h"
#include "move_rules.h"
#include "playstyles.h"
#include "ui_images.h"
#include "ui_presets.h"
#include "ui_team_filter.h"

namespace turbo {

using nlohmann::json;

static const std::vector<std::pair<const char*, std::vector<std::string>>>& attribute_groups() {
    static const std::vector<std::pair<const char*, std::vector<std::string>>> g = {
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

static const std::vector<std::string>& profile_fields() {
    static const std::vector<std::string> f = {"overallrating", "potential", "preferredfoot", "weakfootabilitytypecode",
                                               "skillmoves", "attackingworkrate", "defensiveworkrate", "height",
                                               "weight", "nationality", "internationalrep", "isretiring"};
    return f;
}

static bool is_appearance_field(const std::string& n) {
    static const char* prefixes[] = {"head", "hair", "facial", "eye", "skin", "tattoo", "sock", "shoe", "jersey",
                                     "glove", "gkglove", "accessory", "body", "sideburns", "lip", "nose", "ear",
                                     "hasseasonal", "hashigh", "animfreekick", "animpenalties", "runningcode",
                                     "faceposer", "smallsided"};
    for (const char* p : prefixes) {
        if (n.compare(0, std::strlen(p), p) == 0) return true;
    }
    return false;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// ---------------------------------------------------------------- list
static void player_list(App& app) {
    static char search[64] = "";
    static bool my_club = false;
    // Filters (FC 26 LE v26.3.2): position (any of the 7 preferred positions), PlayStyle / PlayStyle+, retiring,
    // minimum overall / potential, maximum age
    static int f_pos = -1, f_style = -1, f_min_ovr = 0, f_min_pot = 0, f_max_age = 0;
    static bool f_plus = false, f_retiring = false;
    static int64_t f_team = 0;  // club filter (0 = any)
    static char f_team_search[64] = "";
    static std::unordered_set<int64_t> team_players;  // players linked to f_team (rebuilt with the rows)
    static std::vector<const PlayerRow*> rows;
    static std::string last_key;
    static uint64_t last_version = ~uint64_t(0);
    static bool need_sort = true;

    ImGui::SetNextItemWidth(S(180.0f));
    ImGui::InputTextWithHint("##psearch", "name or ID", search, sizeof(search));
    ImGui::SameLine();
    bool can_my_club = app.bridge.state().user_team > 0;
    if (!can_my_club) ImGui::BeginDisabled();
    if (ImGui::Checkbox("My club", &my_club) && my_club) f_team = 0;
    if (!can_my_club) {
        ImGui::EndDisabled();
        my_club = false;
    }
    // Club filter (club or national team, ui_team_filter.h): picking one unticks "My club"
    ImGui::SameLine();
    const float team_w = std::max(S(140.0f), ImGui::GetContentRegionAvail().x);
    if (team_filter_combo(app, "##fteam", f_team, f_team_search, sizeof(f_team_search), team_w) && f_team > 0) my_club = false;

    ImGui::SetNextItemWidth(S(130.0f));
    if (ImGui::BeginCombo("##fpos", f_pos < 0 ? "Any position" : position_name(f_pos), ImGuiComboFlags_HeightLargest)) {
        if (ImGui::Selectable("Any position", f_pos < 0)) f_pos = -1;
        for (int p = 0; p < position_count(); ++p)
            if (ImGui::Selectable(position_name(p), f_pos == p)) f_pos = p;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const auto& styles = playstyle1_names();
    ImGui::SetNextItemWidth(S(170.0f));
    if (ImGui::BeginCombo("##fstyle", f_style < 0 || !styles[static_cast<size_t>(f_style)] ? "Any PlayStyle"
                                                                                             : styles[static_cast<size_t>(f_style)],
                          ImGuiComboFlags_HeightLargest)) {
        if (ImGui::Selectable("Any PlayStyle", f_style < 0)) f_style = -1;
        for (size_t b = 0; b < styles.size(); ++b)
            if (styles[b] && ImGui::Selectable(styles[b], f_style == static_cast<int>(b))) f_style = static_cast<int>(b);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("+ only", &f_plus);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only players with the PlayStyle+ (icontrait1)");
    ImGui::Checkbox("Retiring", &f_retiring);
    for (auto* v : {&f_min_ovr, &f_min_pot, &f_max_age}) {
        const char* lbl = v == &f_min_ovr ? "Min OVR" : v == &f_min_pot ? "Min POT" : "Max age";
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(46.0f));
        ImGui::InputInt(lbl, v, 0);
        *v = std::max(0, std::min(*v, 99));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##filters")) {
        f_pos = f_style = -1;
        f_min_ovr = f_min_pot = f_max_age = 0;
        f_plus = f_retiring = false;
        f_team = 0;
    }

    char fkey[128];
    std::snprintf(fkey, sizeof(fkey), "|%d|%d|%d|%d|%d|%d|%d|%lld", f_pos, f_style, f_plus ? 1 : 0, f_retiring ? 1 : 0, f_min_ovr,
                  f_min_pot, f_max_age, static_cast<long long>(f_team));
    std::string key = lower(search) + (my_club ? "|1" : "|0") + fkey;
    if (key != last_key || last_version != app.model.version()) {
        last_key = key;
        last_version = app.model.version();
        rows.clear();
        std::string q = lower(search);
        int64_t qid = 0;
        bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), ::isdigit);
        if (numeric) qid = std::atoll(q.c_str());
        int64_t club = app.bridge.state().user_team;
        team_players.clear();
        if (f_team > 0)
            for (const LinkRow& l : app.model.links_of_team(f_team)) team_players.insert(l.playerid);
        for (const auto& p : app.model.players()) {
            if (my_club && p.club != club) continue;
            if (f_team > 0 && !team_players.count(p.playerid)) continue;
            if (f_pos >= 0 && std::none_of(std::begin(p.positions), std::end(p.positions), [&](int x) { return x == f_pos; }))
                continue;
            if (f_style >= 0 && !(((f_plus ? p.playstyles_plus : p.playstyles) >> f_style) & 1)) continue;
            if (f_retiring && !p.retiring) continue;
            if (f_min_ovr > 0 && p.overall < f_min_ovr) continue;
            if (f_min_pot > 0 && p.potential < f_min_pot) continue;
            if (f_max_age > 0 && (p.age < 0 || p.age > f_max_age)) continue;
            if (!q.empty()) {
                if (numeric) {
                    if (p.playerid != qid && std::to_string(p.playerid).find(q) != 0) continue;
                } else if (lower(p.name).find(q) == std::string::npos && lower(p.club_name).find(q) == std::string::npos) {
                    continue;
                }
            }
            rows.push_back(&p);
        }
        need_sort = true;
        app.list_player_ids.clear();
        for (const PlayerRow* r : rows) app.list_player_ids.push_back(r->playerid);
    }
    ImGui::TextDisabled("%zu players", rows.size());

    ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##players", 7, fl, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Club", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, S(38.0f));
        ImGui::TableSetupColumn("OVR", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, S(36.0f));
        ImGui::TableSetupColumn("POT", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, S(36.0f));
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, S(34.0f));
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if ((specs->SpecsDirty || need_sort) && specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                bool asc = s.SortDirection == ImGuiSortDirection_Ascending;
                int col = s.ColumnIndex;
                std::stable_sort(rows.begin(), rows.end(), [&](const PlayerRow* a, const PlayerRow* b) {
                    int c = 0;
                    switch (col) {
                        case 0: c = (a->playerid < b->playerid) ? -1 : (a->playerid > b->playerid); break;
                        case 1: c = a->name.compare(b->name); break;
                        case 2: c = a->club_name.compare(b->club_name); break;
                        case 3: c = a->position - b->position; break;
                        case 4: c = a->overall - b->overall; break;
                        case 5: c = a->potential - b->potential; break;
                        default: c = a->age - b->age; break;
                    }
                    return asc ? c < 0 : c > 0;
                });
                specs->SpecsDirty = false;
                need_sort = false;
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PlayerRow* p = rows[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char idbuf[32];
                std::snprintf(idbuf, sizeof(idbuf), "%lld", static_cast<long long>(p->playerid));
                if (ImGui::Selectable(idbuf, app.sel_player == p->playerid, ImGuiSelectableFlags_SpanAllColumns))
                    app.sel_player = p->playerid;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p->name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(p->club_name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(position_name(p->position));
                ImGui::TableNextColumn();
                ImGui::Text("%d", p->overall);
                ImGui::TableNextColumn();
                ImGui::Text("%d", p->potential);
                ImGui::TableNextColumn();
                if (p->age >= 0) ImGui::Text("%d", p->age);
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------- editor tabs
static void positions_editor(App& app, const Table& t, uint64_t rec) {
    for (int k = 1; k <= 7; ++k) {
        std::string fname = "preferredposition" + std::to_string(k);
        const Field* f = t.field(fname);
        if (!f) continue;
        Value v;
        if (!app.db.get(t, rec, *f, v)) continue;
        ImGui::PushID(k);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Position %d", k);
        ImGui::SameLine(S(150.0f));
        ImGui::SetNextItemWidth(S(110.0f));
        const char* cur = v.i < 0 ? "None" : position_name(static_cast<int>(v.i));
        if (ImGui::BeginCombo("##pos", cur)) {
            if (f->min <= -1 && ImGui::Selectable("None", v.i < 0)) app.edit(t, rec, *f, Value::of_int(-1));
            for (int p = 0; p < position_count(); ++p) {
                if (p < f->min || p > f->max()) continue;
                if (ImGui::Selectable(position_name(p), v.i == p)) app.edit(t, rec, *f, Value::of_int(p));
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}

static void playstyle_bits(App& app, const Table& t, uint64_t rec, const char* field, const std::vector<const char*>& names) {
    const Field* f = t.field(field);
    if (!f) {
        ImGui::TextDisabled("%s: not in FC 27's players table", field);
        return;
    }
    Value v;
    if (!app.db.get(t, rec, *f, v)) return;
    ImGui::PushID(field);
    int bits = std::min(f->depth, 62);
    if (ImGui::BeginTable("##bits", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (int b = 0; b < bits; ++b) {
            ImGui::TableNextColumn();
            bool on = (static_cast<uint64_t>(v.i - f->min) >> b) & 1;
            char lbl[64];
            if (b < static_cast<int>(names.size()) && names[static_cast<size_t>(b)])
                std::snprintf(lbl, sizeof(lbl), "%s##%d", names[static_cast<size_t>(b)], b);
            else
                std::snprintf(lbl, sizeof(lbl), "Bit %d##%d", b, b);
            if (ImGui::Checkbox(lbl, &on)) {
                uint64_t raw = static_cast<uint64_t>(v.i - f->min);
                raw = on ? (raw | (uint64_t(1) << b)) : (raw & ~(uint64_t(1) << b));
                app.edit(t, rec, *f, Value::of_int(static_cast<int64_t>(raw) + f->min));
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("All")) app.edit(t, rec, *f, Value::of_int(f->max()));
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) app.edit(t, rec, *f, Value::of_int(f->min));
    ImGui::PopID();
}

// The database-only moves Turbo makes itself in this Live Editor build (v27.1.2 has no native for them; a build with
// the native does them through Live Editor)
static bool turbo_made_move(App& app, const char* key) { return app.bridge.state().is_turbo_made(key); }

// What the window knows about a player for the move rules (move_rules.h; Lua core/moves.lua checks again)
static move_rules::Facts move_facts(App& app, const PlayerRow& p) {
    move_rules::Facts f;
    f.user_team = app.bridge.state().user_team;
    f.club = p.club;
    if (const Table* lt = app.db.table("playerloans")) {
        if (uint64_t rec = app.db.find(*lt, "playerid", p.playerid)) {
            f.on_loan = true;
            f.loaned_from = app.db.get_int(*lt, rec, "teamidloanedfrom", 0);
        }
    } else {
        f.loans_known = false;   // no playerloans table in this database: Lua decides
    }
    if (p.club > 0) {
        const auto squad = app.model.links_of_team(p.club);
        f.squad = static_cast<int>(squad.size());
        if (p.positions[0] == 0) {   // a goalkeeper: is there another one?
            f.last_keeper = true;
            for (const auto& l : squad) {
                const PlayerRow* m = l.playerid != p.playerid ? app.model.player(l.playerid) : nullptr;
                if (m && m->positions[0] == 0) {
                    f.last_keeper = false;
                    break;
                }
            }
        }
    }
    return f;
}

// A button for a player move. Greyed out (with the reason as tooltip) only when this Live Editor / Turbo build cannot
// run the tool at all (key: Lua core/caps.lua, e.g. "move_transfer"). When the move cannot run for THIS player (`why`,
// move_rules.h) the button stays enabled, its label is dimmed, hovering says why and a click shows the reason instead
// of sending a command that can only fail.
static bool move_button(App& app, const char* label, const char* key, const std::string& why = std::string(),
                        const char* tip = nullptr) {
    const std::string* missing = app.bridge.state().unavailable_reason(key);
    if (missing) {
        ImGui::BeginDisabled();
        ImGui::Button(label);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", missing->c_str());
        return false;
    }
    if (!why.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool clicked = ImGui::Button(label);
    if (!why.empty()) {
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not possible for him: %s", why.c_str());
        if (clicked) app.notify(std::string(label) + ": " + why, true);
        return false;
    }
    if (ImGui::IsItemHovered()) {
        if (tip) ImGui::SetTooltip("%s", tip);
        else if (turbo_made_move(app, key))
            ImGui::SetTooltip("Done by Turbo in the career database (back up your save first; the squad screens show it "
                              "after saving and loading the career)");
    }
    return clicked;
}

// The transfer / loan list actions are the game's own Transfer Hub actions (Turbo.dll game call, Lua core/moves.lua
// M.list): the game lists on YOUR club whoever the player is, so for another club's player they say why instead
// (move_rules::why_not); List status runs for anyone.
// "Transfer list" / "Loan list" / "Remove from lists" (+ "List status" with_status) for one player (player_moves module)
static void list_buttons(App& app, const PlayerRow& p, bool with_status) {
    const int64_t pid = p.playerid;
    const move_rules::Facts facts = move_facts(app, p);
    struct B { const char* label; const char* action; const char* key; const char* tip; };
    const B buttons[] = {
        {"Transfer list", "transfer_list", "move_transfer_list",
         "Adds him to your club's transfer list through the game's own Transfer Hub action (AI clubs then make offers; "
         "FC 27 sets no asking price)"},
        {"Loan list", "loan_list", "move_loan_list", "Adds him to your club's loan list through the game's own Transfer Hub action"},
        {"Remove from lists", "unlist", "move_unlist", "Takes him off the transfer list and the loan list (the game's remove clears both)"},
        {"List status", "list_status", "move_list_status", "Reads whether the game has him on the transfer / loan list"},
    };
    bool first = true;
    for (const B& b : buttons) {
        const bool status_only = std::string(b.action) == "list_status";
        if (status_only && !with_status) continue;
        if (!first) ImGui::SameLine();
        first = false;
        const std::string why = status_only ? std::string() : move_rules::why_not(b.action, facts);
        if (move_button(app, b.label, b.key, why, b.tip)) {
            json a = {{"action", b.action}, {"playerid", pid}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, b.label);
        }
    }
}

// Transfer bans for a club or a player (features/transfer_bans.lua modes ban_team / unban_team / ban_player /
// unban_player). FC 27 has no ban list Turbo could call (docs/re/transfer_lists.md section 6): the section says so and
// stays greyed out, and lights up only with a Live Editor build that ships the ban natives.
void transfer_ban_section(App& app, const char* what, int64_t id) {
    const BridgeState& st = app.bridge.state();
    const bool team = std::string(what) == "team";
    ImGui::PushID(team ? "tbteam" : "tbplayer");
    ImGui::SeparatorText("Transfer bans");
    const std::string* missing = st.unavailable_reason("transfer_bans");
    if (missing) ImGui::TextWrapped("Not available: %s", missing->c_str());
    static int until = 20990101;
    const bool off = missing || !st.in_cm || id <= 0;
    if (off) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(S(110.0f));
    ImGui::InputInt("Banned until (YYYYMMDD)", &until, 0);
    if (ImGui::Button(team ? "Ban this club" : "Ban this player")) {
        json o = {{"mode", team ? "ban_team" : "ban_player"}, {"id", id}, {"ban_until", until}};
        app.send({{"op", "run"}, {"module", "transfer_bans"}, {"overrides", o}}, team ? "Ban club" : "Ban player");
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove ban")) {
        json o = {{"mode", team ? "unban_team" : "unban_player"}, {"id", id}};
        app.send({{"op", "run"}, {"module", "transfer_bans"}, {"overrides", o}}, "Remove ban");
    }
    ImGui::SameLine();
    if (ImGui::Button("List bans")) app.send({{"op", "run"}, {"module", "transfer_bans"}, {"overrides", {{"mode", "list"}}}}, "List bans");
    if (off) {
        ImGui::EndDisabled();
        if (!missing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Needs a loaded career");
    }
    ImGui::PopID();
}

static void moves_popup(App& app, const PlayerRow& p) {
    static int to_team = 0, fee = 0, wage = 10000, months = 36, loan_months = 12;
    if (ImGui::BeginPopup("##moves")) {
        const int64_t pid = p.playerid;
        const move_rules::Facts facts = move_facts(app, p);
        ImGui::TextDisabled("Runs through Live Editor on the next career-mode event");
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("To team ID", &to_team, 0);
        if (to_team > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", app.model.team_name(to_team).c_str());
        }
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Fee", &fee, 0);
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Wage", &wage, 0);
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Contract months", &months, 0);
        const bool exists = app.model.team(to_team) != nullptr;
        const bool national = exists && app.model.is_national_team(to_team);
        const int to_squad = exists ? static_cast<int>(app.model.links_of_team(to_team).size()) : -1;
        const std::string why_t = move_rules::why_not_to("transfer", facts, to_team, exists, national, to_squad);
        const std::string why_l = move_rules::why_not_to("loan", facts, to_team, exists, national, to_squad);
        const int64_t user = facts.user_team;
        if (user > 0 && (to_team == user || facts.club == user))
            ImGui::TextWrapped("%s", move_rules::own_club_note());
        if (facts.on_loan && why_t.empty())
            ImGui::TextDisabled("He is on loan: a transfer ends the loan (his parent club sells him).");
        if (move_button(app, "Transfer", "move_transfer", why_t)) {
            json a = {{"action", "transfer"}, {"playerid", pid}, {"to_teamid", to_team}, {"fee", fee}, {"wage", wage}, {"months", months}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, "Transfer");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(80.0f));
        ImGui::InputInt("##loanm", &loan_months, 0);
        ImGui::SameLine();
        if (move_button(app, "Loan (months)", "move_loan", why_l)) {
            json a = {{"action", "loan"}, {"playerid", pid}, {"to_teamid", to_team}, {"months", loan_months}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, "Loan");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Scouting and development of one player (Manager Career): features/reveal.lua (the game's own PlayerDataRevealManager
// through Turbo.dll, caps key "reveal") and features/development.lua (players table + the game's development plan,
// caps key "development")
static bool career_button(App& app, const char* label, const char* key, const char* tip) {
    const BridgeState& st = app.bridge.state();
    const std::string* missing = st.unavailable_reason(key);
    const bool off = app.busy() || !app.mailbox || !st.in_cm || missing;
    if (off) ImGui::BeginDisabled();
    const bool clicked = ImGui::Button(label);
    if (off) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", missing ? missing->c_str() : !st.in_cm ? "Needs a loaded Manager Career" : tip);
    return clicked && !off;
}

static void career_dev_tab(App& app, const PlayerRow& p) {
    static int delta = 1, potential = 0, weekly = 1;
    static bool no_decline = true;
    ImGui::TextDisabled("Manager Career: these run through Turbo's Lua side on the next career-mode event.");
    ImGui::SeparatorText("Scouting");
    if (career_button(app, "Reveal data", "reveal",
                      "The game marks him fully scouted: his Player Bio shows the true attributes and potential")) {
        json o = {{"scope", {{"playerid", p.playerid}}}};
        app.send({{"op", "run"}, {"module", "reveal"}, {"overrides", o}}, "Reveal " + p.name);
    }
    if (p.club > 0) {
        ImGui::SameLine();
        if (career_button(app, "Reveal his club", "reveal", "Every player of his club fully scouted")) {
            json o = {{"scope", {{"teamid", p.club}}}};
            app.send({{"op", "run"}, {"module", "reveal"}, {"overrides", o}}, "Reveal " + p.club_name);
        }
    }
    ImGui::SeparatorText("Development");
    char lbl[64];
    std::snprintf(lbl, sizeof(lbl), "Develop to potential (%d)", p.potential);
    if (career_button(app, lbl, "development",
                      "Every attribute of his group rises by potential - overall and his overall becomes his potential; "
                      "your players' development plans follow")) {
        json o = {{"scope", {{"playerid", p.playerid}}}, {"mode", "to_potential"}};
        app.send({{"op", "run"}, {"module", "development"}, {"overrides", o}}, "Develop " + p.name);
    }
    ImGui::SetNextItemWidth(S(120.0f));
    ImGui::InputInt("Points per attribute##dvd", &delta, 1);
    delta = std::max(-20, std::min(delta, 20));
    ImGui::SameLine();
    if (career_button(app, "Add now##dvd", "development", "Adds the points to every attribute of his group (and to his overall)") && delta != 0) {
        json o = {{"scope", {{"playerid", p.playerid}}}, {"mode", "add"}, {"delta", delta}};
        app.send({{"op", "run"}, {"module", "development"}, {"overrides", o}}, "Growth " + p.name);
    }
    ImGui::SetNextItemWidth(S(120.0f));
    ImGui::InputInt("New potential##dvp", &potential, 1);
    potential = std::max(0, std::min(potential, 99));
    ImGui::SameLine();
    if (career_button(app, "Set potential##dvp", "development", "players.potential (the game's growth aims at it)") && potential > 0) {
        json o = {{"scope", {{"playerid", p.playerid}}}, {"mode", "none"}, {"potential", potential}};
        app.send({{"op", "run"}, {"module", "development"}, {"overrides", o}}, "Potential " + p.name);
    }
    ImGui::SeparatorText("Weekly forced growth (auto)");
    json& a = app.gui_settings["auto"]["development"];
    if (!a.is_object()) a = json::object();
    const json players = a.contains("players") && a["players"].is_array() ? a["players"] : json::array();
    bool listed = std::find(players.begin(), players.end(), json(p.playerid)) != players.end();
    ImGui::SetNextItemWidth(S(120.0f));
    ImGui::SliderInt("Points per week##dvw", &weekly, 1, 5);
    ImGui::SameLine();
    ImGui::Checkbox("No decline##dvw", &no_decline);
    if (ImGui::Checkbox("Grow him every week while below his potential##dvw", &listed)) {
        json out = json::array();
        for (const auto& x : players)
            if (x != json(p.playerid)) out.push_back(x);
        if (listed) out.push_back(p.playerid);
        a["players"] = out;
        a["weekly"] = weekly;
        a["no_decline"] = no_decline;
        a["enabled"] = !out.empty() || a.value("user_team", false);
        if (app.save_gui_settings()) app.send({{"op", "boot"}}, "Weekly growth");
        else app.notify("cannot write turbo_output\\gui_settings.json", true);
    }
    const size_t n = a.contains("players") && a["players"].is_array() ? a["players"].size() : 0;
    ImGui::TextDisabled("%zu players listed for weekly growth%s", n, a.value("user_team", false) ? " (plus your whole squad)" : "");
    ImGui::TextDisabled("Potential, growth profile and every attribute are also in the Attributes and All fields tabs.");
}

static void player_editor(App& app) {
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!t || !p) {
        ImGui::TextDisabled("Select a player on the left.");
        return;
    }
    if (!app.db.table_alive(*t, p->rec)) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "The database changed (another save loaded?). Press Refresh.");
        return;
    }
    ImGui::Text("%s", p->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("ID %lld | %s | %s | OVR %d POT %d", static_cast<long long>(p->playerid),
                        p->club_name.empty() ? "no club" : p->club_name.c_str(), position_name(p->position), p->overall,
                        p->potential);

    bool cm = app.bridge.state().in_cm;
    // every club, yours included: the same checks as Lua core/moves.lua (move_rules.h); a move that cannot run for
    // this player says why on hover and on click
    const move_rules::Facts facts = move_facts(app, *p);
    if (!cm) ImGui::BeginDisabled();
    if (ImGui::Button("Transfer / Loan...")) ImGui::OpenPopup("##moves");
    ImGui::SameLine();
    auto simple = [&](const char* label, const char* action) {
        std::string key = std::string("move_") + action;
        if (move_button(app, label, key.c_str(), move_rules::why_not(action, facts))) {
            json a = {{"action", action}, {"playerid", p->playerid}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, label);
        }
        ImGui::SameLine();
    };
    simple("Release", "release");
    simple("Terminate loan", "terminate_loan");
    list_buttons(app, *p, false);  // ends the row (no SameLine after the last button); List status: Contract & Clubs
    if (move_button(app, "Delete player...", "delete_players", move_rules::why_not("delete", facts)))
        ImGui::OpenPopup("##delplayer");
    if (move_rules::at_user_club(facts) && turbo_made_move(app, "move_release")) {
        ImGui::SameLine();
        ImGui::TextDisabled("(your player: back up your save; hover a button for details)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", move_rules::own_club_note());
    }
    if (cm && app.bridge.state().unavailable_reason("move_transfer") && app.bridge.state().unavailable_reason("move_release")) {
        ImGui::SameLine();
        ImGui::TextDisabled("(player moves need Live Editor natives this build does not have: hover a button for details)");
    }
    if (!cm) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(moves need a loaded career)");
    }
    moves_popup(app, *p);
    player_preset_buttons(app, *p);   // Export... / Import... / Clone... / Create player... (ui_presets.cpp)
    if (ImGui::BeginPopupModal("##delplayer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s (ID %lld)?", p->name.c_str(), static_cast<long long>(p->playerid));
        ImGui::TextDisabled("Turbo releases him (same checks as Release), then deletes his players, teamplayerlinks and loan records.");
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "This cannot be undone.");
        if (ImGui::Button("Delete player")) {
            json a = {{"action", "delete"}, {"playerid", p->playerid}, {"confirm", true}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, "Delete player");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##delplayer")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();

    // undo of the last direct edits of this player (at most App::kUndoSteps)
    {
        size_t n = app.undo_count(p->playerid);
        if (n == 0) ImGui::BeginDisabled();
        char ulbl[48];
        std::snprintf(ulbl, sizeof(ulbl), "Undo (%zu)##pundo", n);
        if (ImGui::Button(ulbl)) app.undo(p->playerid);
        if (n == 0) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Puts back the previous value of the last field edited here (%zu steps per player)", App::kUndoSteps);
        ImGui::SameLine();
        static char fsearch[48] = "";
        ImGui::SetNextItemWidth(S(180.0f));
        ImGui::InputTextWithHint("##fieldsearch", "find a field", fsearch, sizeof(fsearch));
        if (fsearch[0]) {
            std::string q = lower(fsearch);
            std::vector<std::string> hits;
            for (const auto& n2 : t->field_names())
                if (n2.find(q) != std::string::npos || lower(field_label(n2)).find(q) != std::string::npos) hits.push_back(n2);
            ImGui::SameLine();
            ImGui::TextDisabled("%zu fields", hits.size());
            if (!hits.empty() && ImGui::BeginTable("##fsearch", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersOuter)) {
                size_t shown = 0;
                for (const auto& n2 : hits) {
                    if (shown++ >= 8) break;
                    ImGui::TableNextColumn();
                    const Field* f = t->field(n2);
                    if (is_enum_field(n2)) enum_editor(app, *t, p->rec, *f, field_label(n2).c_str(), S(130.0f));
                    else field_editor(app, *t, p->rec, *f, field_label(n2).c_str(), f->type == FieldType::String ? -1.0f : S(100.0f));
                }
                ImGui::EndTable();
                if (hits.size() > 8) ImGui::TextDisabled("(first 8 shown: type more of the name)");
            }
        }
    }
    if (ImGui::BeginTabBar("##ptabs")) {
        if (ImGui::BeginTabItem("Profile")) {
            ImGui::BeginChild("##prof");
            field_grid(app, *t, p->rec, profile_fields(), "##profgrid", 2);
            ImGui::SeparatorText("Positions");
            positions_editor(app, *t, p->rec);
            ImGui::SeparatorText("Dates");
            for (const char* dn : {"birthdate", "playerjointeamdate"}) {
                if (const Field* f = t->field(dn)) date_field_editor(app, *t, p->rec, *f, field_label(dn).c_str());
            }
            ImGui::SeparatorText("Roles and body");
            field_grid(app, *t, p->rec, {"role1", "role2", "role3", "role4", "role5", "role6", "role7", "role8", "role9", "bodytypecode",
                                          "gender", "personality", "emotion", "growthprofile", "skillmoveslikelihood", "gkkickstyle",
                                          "runstylecode", "usercaneditname", "iscustomized"}, "##rolegrid", 3);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Attributes")) {
            ImGui::BeginChild("##attr");
            ImGui::TextDisabled("Stored overall %lld, potential %lld (the game recalculates the shown overall from the attributes when "
                                "the player card is drawn).", static_cast<long long>(app.db.get_int(*t, p->rec, "overallrating", 0)),
                                static_cast<long long>(app.db.get_int(*t, p->rec, "potential", 0)));
            for (const auto& g : attribute_groups()) {
                // group average over the fields present
                long long sum = 0;
                int cnt = 0;
                for (const auto& fn : g.second)
                    if (t->has(fn)) { sum += app.db.get_int(*t, p->rec, fn, 0); ++cnt; }
                char hdr[96];
                if (cnt) std::snprintf(hdr, sizeof(hdr), "%s  (average %.1f)", g.first, double(sum) / cnt);
                else std::snprintf(hdr, sizeof(hdr), "%s", g.first);
                ImGui::SeparatorText(hdr);
                std::vector<const Field*> present;
                for (const auto& fn : g.second)
                    if (const Field* f = t->field(fn)) present.push_back(f);
                ImGui::PushID(g.first);
                if (!present.empty() && ImGui::BeginTable("##sl", 3, ImGuiTableFlags_SizingStretchSame)) {
                    for (const Field* f : present) {
                        ImGui::TableNextColumn();
                        slider_editor(app, *t, p->rec, *f, field_label(f->name).c_str(), -FLT_MIN);
                    }
                    ImGui::EndTable();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("PlayStyles")) {
            ImGui::BeginChild("##ps");
            ImGui::SeparatorText("PlayStyles (trait1)");
            playstyle_bits(app, *t, p->rec, "trait1", playstyle1_names());
            ImGui::SeparatorText("PlayStyles+ (icontrait1)");
            playstyle_bits(app, *t, p->rec, "icontrait1", playstyle1_names());
            ImGui::SeparatorText("Traits (trait2)");
            playstyle_bits(app, *t, p->rec, "trait2", playstyle2_names());
            ImGui::SeparatorText("Traits+ (icontrait2)");
            playstyle_bits(app, *t, p->rec, "icontrait2", playstyle2_names());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Appearance")) {
            ImGui::BeginChild("##app");
            ImGui::SeparatorText("Head");
            if (ImGui::Button("Choose a real face...")) ImGui::OpenPopup("Choose a real face");
            ImGui::SameLine();
            ImGui::TextDisabled("head asset %lld, head class %lld", static_cast<long long>(app.db.get_int(*t, p->rec, "headassetid", 0)),
                                static_cast<long long>(app.db.get_int(*t, p->rec, "headclasscode", 0)));
            real_face_picker(app, p->playerid);
            ImGui::SeparatorText("Tattoos");
            tattoo_editor(app, *t, p->rec);
            ImGui::SeparatorText("Hair, boots, gloves and accessories");
            item_galleries(app, *t, p->rec);
            ImGui::SeparatorText("Every appearance field");
            std::vector<std::string> names;
            for (const auto& n : t->field_names()) if (is_appearance_field(n)) names.push_back(n);
            field_grid(app, *t, p->rec, names, "##appgrid", 2);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Miniface")) {
            ImGui::BeginChild("##mface");
            MinifaceTarget mt;
            mt.path = legacy_path::player_miniface(p->playerid);
            mt.headassetid = app.db.get_int(*t, p->rec, "headassetid", 0);
            mt.id = p->playerid;
            mt.teamid = p->club;
            miniface_editor(app, mt);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Contract & Clubs")) {
            ImGui::BeginChild("##con");
            field_grid(app, *t, p->rec, {"contractvaliduntil", "wage", "releaseclause", "isretiring"}, "##congrid", 2);
            ImGui::SeparatorText("Transfer / loan lists (the game's own Transfer Hub actions)");
            ImGui::PushID("conlists");
            if (!app.bridge.state().in_cm) ImGui::BeginDisabled();
            list_buttons(app, *p, true);
            if (!app.bridge.state().in_cm) ImGui::EndDisabled();
            ImGui::PopID();
            transfer_ban_section(app, "player", p->playerid);
            ImGui::SeparatorText("Team links (teamplayerlinks)");
            const Table* lt = app.db.table("teamplayerlinks");
            if (lt) {
                for (const auto& l : app.model.links_of_player(p->playerid)) {
                    ImGui::PushID(static_cast<int>(l.rec & 0x7FFFFFFF));
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s (ID %lld)%s", app.model.team_name(l.teamid).c_str(), static_cast<long long>(l.teamid),
                                app.model.is_national_team(l.teamid) ? " [national team]" : "");
                    if (const Field* jf = lt->field("jerseynumber")) field_editor(app, *lt, l.rec, *jf, "  Jersey", S(80.0f));
                    if (const Field* pf = lt->field("position")) field_editor(app, *lt, l.rec, *pf, "  Line-up slot", S(80.0f));
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Callname")) {
            ImGui::BeginChild("##cname");
            callname_editor(app, *t, *p);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All fields")) {
            all_fields(app, *t, p->rec, "##pall");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Growth")) {
            ImGui::BeginChild("##pgrowth");
            career_dev_tab(app, *p);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void draw_players(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    ImGui::BeginChild("##plist", ImVec2(S(470.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    player_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pedit", ImVec2(0, 0));
    player_editor(app);
    ImGui::EndChild();
}

}  // namespace turbo
