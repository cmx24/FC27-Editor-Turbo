// FC 27 LE Turbo GUI - Players tab: searchable list on the left, editor on the right.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "imgui.h"
#include "playstyles.h"

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
    static std::vector<const PlayerRow*> rows;
    static std::string last_key;
    static uint64_t last_version = ~uint64_t(0);
    static bool need_sort = true;

    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##psearch", "name or ID", search, sizeof(search));
    ImGui::SameLine();
    bool can_my_club = app.bridge.state().user_team > 0;
    if (!can_my_club) ImGui::BeginDisabled();
    ImGui::Checkbox("My club", &my_club);
    if (!can_my_club) {
        ImGui::EndDisabled();
        my_club = false;
    }

    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::BeginCombo("##fpos", f_pos < 0 ? "Any position" : position_name(f_pos), ImGuiComboFlags_HeightLargest)) {
        if (ImGui::Selectable("Any position", f_pos < 0)) f_pos = -1;
        for (int p = 0; p < position_count(); ++p)
            if (ImGui::Selectable(position_name(p), f_pos == p)) f_pos = p;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const auto& styles = playstyle1_names();
    ImGui::SetNextItemWidth(170.0f);
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
        ImGui::SetNextItemWidth(46.0f);
        ImGui::InputInt(lbl, v, 0);
        *v = std::max(0, std::min(*v, 99));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##filters")) {
        f_pos = f_style = -1;
        f_min_ovr = f_min_pot = f_max_age = 0;
        f_plus = f_retiring = false;
    }

    char fkey[96];
    std::snprintf(fkey, sizeof(fkey), "|%d|%d|%d|%d|%d|%d|%d", f_pos, f_style, f_plus ? 1 : 0, f_retiring ? 1 : 0, f_min_ovr,
                  f_min_pot, f_max_age);
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
        for (const auto& p : app.model.players()) {
            if (my_club && p.club != club) continue;
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
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Club", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 38.0f);
        ImGui::TableSetupColumn("OVR", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 36.0f);
        ImGui::TableSetupColumn("POT", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 36.0f);
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, 34.0f);
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
        ImGui::SameLine(150.0f);
        ImGui::SetNextItemWidth(110.0f);
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

static void moves_popup(App& app, int64_t pid) {
    static int to_team = 0, fee = 0, wage = 10000, months = 36, loan_months = 12;
    if (ImGui::BeginPopup("##moves")) {
        ImGui::TextDisabled("Runs through Live Editor on the next career-mode event");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("To team ID", &to_team, 0);
        if (to_team > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", app.model.team_name(to_team).c_str());
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Fee", &fee, 0);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Wage", &wage, 0);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Contract months", &months, 0);
        bool valid_team = app.model.team(to_team) != nullptr;
        if (!valid_team) ImGui::BeginDisabled();
        if (ImGui::Button("Transfer")) {
            json a = {{"action", "transfer"}, {"playerid", pid}, {"to_teamid", to_team}, {"fee", fee}, {"wage", wage}, {"months", months}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, "Transfer");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##loanm", &loan_months, 0);
        ImGui::SameLine();
        if (ImGui::Button("Loan (months)")) {
            json a = {{"action", "loan"}, {"playerid", pid}, {"to_teamid", to_team}, {"months", loan_months}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, "Loan");
            ImGui::CloseCurrentPopup();
        }
        if (!valid_team) ImGui::EndDisabled();
        ImGui::EndPopup();
    }
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
    if (!cm) ImGui::BeginDisabled();
    if (ImGui::Button("Transfer / Loan...")) ImGui::OpenPopup("##moves");
    ImGui::SameLine();
    auto simple = [&](const char* label, const char* action) {
        if (ImGui::Button(label)) {
            json a = {{"action", action}, {"playerid", p->playerid}};
            app.send({{"op", "run"}, {"module", "player_moves"}, {"overrides", {{"actions", json::array({a})}}}}, label);
        }
        ImGui::SameLine();
    };
    simple("Release", "release");
    simple("Terminate loan", "terminate_loan");
    simple("Transfer list", "transfer_list");
    simple("Loan list", "loan_list");
    simple("Remove from lists", "unlist");
    ImGui::NewLine();
    if (ImGui::Button("Delete player...")) ImGui::OpenPopup("##delplayer");
    if (!cm) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(moves need a loaded career)");
    }
    moves_popup(app, p->playerid);
    if (ImGui::BeginPopupModal("##delplayer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %s (ID %lld)?", p->name.c_str(), static_cast<long long>(p->playerid));
        ImGui::TextDisabled("Live Editor moves the player to Free Agents, then deletes his players and teamplayerlinks records.");
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
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Attributes")) {
            ImGui::BeginChild("##attr");
            for (const auto& g : attribute_groups()) {
                ImGui::SeparatorText(g.first);
                field_grid(app, *t, p->rec, g.second, g.first, 3);
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
            std::vector<std::string> names;
            for (const auto& n : t->field_names()) if (is_appearance_field(n)) names.push_back(n);
            field_grid(app, *t, p->rec, names, "##appgrid", 2);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Contract & Clubs")) {
            ImGui::BeginChild("##con");
            field_grid(app, *t, p->rec, {"contractvaliduntil", "isretiring"}, "##congrid", 2);
            ImGui::SeparatorText("Team links (teamplayerlinks)");
            const Table* lt = app.db.table("teamplayerlinks");
            if (lt) {
                for (const auto& l : app.model.links_of_player(p->playerid)) {
                    ImGui::PushID(static_cast<int>(l.rec & 0x7FFFFFFF));
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s (ID %lld)%s", app.model.team_name(l.teamid).c_str(), static_cast<long long>(l.teamid),
                                app.model.is_national_team(l.teamid) ? " [national team]" : "");
                    if (const Field* jf = lt->field("jerseynumber")) field_editor(app, *lt, l.rec, *jf, "  Jersey", 80.0f);
                    if (const Field* pf = lt->field("position")) field_editor(app, *lt, l.rec, *pf, "  Line-up slot", 80.0f);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All fields")) {
            all_fields(app, *t, p->rec, "##pall");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void draw_players(App& app) {
    if (!app.connected()) {
        ImGui::TextDisabled("Not connected to the game database yet. See the Status tab.");
        return;
    }
    ImGui::BeginChild("##plist", ImVec2(470.0f, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    player_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pedit", ImVec2(0, 0));
    player_editor(app);
    ImGui::EndChild();
}

}  // namespace turbo
