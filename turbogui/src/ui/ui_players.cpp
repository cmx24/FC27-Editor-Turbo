// FC 27 LE Turbo GUI - Players tab: searchable list on the left, editor on the right.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <unordered_map>

#include "app.h"
#include "core/archetypes.h"
#include "core/field_labels.h"
#include "core/overall.h"
#include "core/range_filter.h"
#include "geo.h"
#include "imgui.h"
#include "move_rules.h"
#include "playstyles.h"
#include "ui_bodytypes.h"
#include "ui_club_search.h"
#include "ui_images.h"
#include "ui_names.h"
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
    // overall, potential and the OVR modifier have their own row above this grid
    static const std::vector<std::string> f = {"preferredfoot", "weakfootabilitytypecode",
                                               "skillmoves", "attackingworkrate", "defensiveworkrate", "height",
                                               "weight", "nationality", "internationalrep", "isretiring"};
    return f;
}

static bool is_appearance_field(const std::string& n) {
    static const char* prefixes[] = {"head", "hair", "facial", "eye", "skin", "tattoo", "sock", "shoe", "jersey",
                                     "glove", "gkglove", "accessory", "body", "sideburns", "lip", "nose", "ear",
                                     "hasseasonal", "hashigh", "animfreekick", "animpenalties", "runningcode",
                                     "faceposer", "smallsided", "shortstyle", "undershortstyle", "muscularity"};
    for (const char* p : prefixes) {
        if (n.compare(0, std::strlen(p), p) == 0) return true;
    }
    return false;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Case-insensitive text compare for the sortable columns (<0, 0, >0)
static int ci_compare(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
}

// playerid -> players.nationality for the list (the model's rows do not carry it): one bulk read per model rebuild; the
// selected player is re-read every frame so an edit in the Profile tab shows at once
static const std::unordered_map<int64_t, int64_t>& player_nationalities(App& app) {
    static std::unordered_map<int64_t, int64_t> m;
    static uint64_t version = ~uint64_t(0);
    const Table* t = app.db.table("players");
    if (!t || !t->has("nationality")) {
        m.clear();
        return m;
    }
    if (version != app.model.version()) {
        version = app.model.version();
        m.clear();
        Snapshot s;
        if (s.load(app.db.memory(), *t)) {
            m.reserve(s.valid.size());
            for (uint32_t i : s.valid) m[s.get_int(i, "playerid")] = s.get_int(i, "nationality", -1);
        }
    }
    if (const PlayerRow* p = app.model.player(app.sel_player))
        if (app.db.table_alive(*t, p->rec)) m[p->playerid] = app.db.get_int(*t, p->rec, "nationality", -1);
    return m;
}

// ---------------------------------------------------------------- list
static void player_list(App& app) {
    static char search[64] = "";
    static bool my_club = false;
    // Filters, in four blocks (widgets.cpp form rows: label left, control right). Who: club, nationality, continent (gender: the search line). Position &
    // style: any of the 7 preferred positions, PlayStyle / PlayStyle+. Numbers: age, overall and potential as two-sided ranges
    // (core/range_filter.h: 0 = no bound). Status: my club, retiring, free agent.
    static int f_pos = -1, f_style = -1;
    static IntRange f_age, f_ovr, f_pot;
    static bool f_plus = false, f_retiring = false, f_free = false;
    static int64_t f_team = 0;  // club filter (0 = any)
    static int64_t f_nat = -1, f_cont = -1;  // nationality (nations.nationid) and continent (nations.confederation) filters, -1 = any
    static int f_gender = -1;                 // players.gender: -1 any, 0 men, 1 women
    static char f_team_search[64] = "";
    static std::unordered_set<int64_t> team_players;  // players linked to f_team (rebuilt with the rows)
    static std::vector<const PlayerRow*> rows;
    static std::string last_key;
    static uint64_t last_version = ~uint64_t(0);
    static bool need_sort = true;

    ImGui::SetNextItemWidth(S(180.0f));
    ImGui::InputTextWithHint("##psearch", "name or ID", search, sizeof(search));
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##filters")) {
        f_pos = f_style = -1;
        f_age.clear();
        f_ovr.clear();
        f_pot.clear();
        f_plus = f_retiring = f_free = false;
        f_team = 0;
        f_nat = f_cont = -1;
        f_gender = -1;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear every filter below (the search box stays)");
    // gender (players.gender; 1.2.5) on the search line, so the filter grid keeps its three rows per block
    {
        static const char* const kGenders[] = {"Any gender", "Men", "Women"};
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(110.0f));
        if (ImGui::BeginCombo("##fgender", kGenders[f_gender + 1])) {
            for (int k = -1; k <= 1; ++k)
                if (ImGui::Selectable(kGenders[k + 1], f_gender == k)) f_gender = k;
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Men, women or both (players.gender)");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu players", rows.size());  // the list built below (for one frame after a change: the previous count)

    // The filter grid: four blocks side by side, a small header each, label left / control right (widgets.cpp form rows). Three
    // rows per block, so it is as tall as the old filter lines. Tab moves down a block, then on to the next block. A table cell
    // clips what does not fit, so the Numbers block shortens its labels and boxes in a narrow column.
    form_label_width(S(76.0f));
    const bool can_my_club = app.bridge.state().user_team > 0;
    const auto& styles = playstyle1_names();
    auto fill_w = [] { return std::max(S(60.0f), ImGui::GetContentRegionAvail().x); };
    if (ImGui::BeginTable("##pfilters", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Position & style", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Numbers", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableNextRow();
        // ---- Who
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Who");
        form_label("Club", "A club or a national team; picking one unticks My club (Status block)");
        if (team_filter_combo(app, "##fteam", f_team, f_team_search, sizeof(f_team_search), fill_w()) && f_team > 0) my_club = false;
        // nationality / continent (geo.h: a nation's continent is its confederation); the nationality list narrows to the continent
        {
            const Geo& g = geo(app);
            std::string npreview = "Any nationality";
            if (f_nat >= 0) {
                npreview = g.nation_name(f_nat);
                if (npreview.empty()) npreview = "Nation " + std::to_string(f_nat);
            }
            form_label("Nationality");
            if (geo_pick_combo("##fnat", "Any nationality", npreview, f_nat, fill_w(), [&](std::vector<GeoOption>& out) {
                    for (const GeoNation* n : g.sorted_nations())
                        if (f_cont < 0 || n->conf == f_cont) out.push_back({n->id, n->name});
                }) &&
                f_nat >= 0 && f_cont >= 0 && g.continent_of_nation(f_nat) != f_cont)
                f_cont = -1;
            form_label("Continent");
            if (continent_filter_combo(app, "##fcont", f_cont, fill_w()) && f_nat >= 0 && f_cont >= 0 && g.continent_of_nation(f_nat) != f_cont)
                f_nat = -1;
        }
        // ---- Position & style
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Position & style");
        form_label("Position", "Any of the player's 7 preferred positions");
        position_combo("##fpos", f_pos, fill_w(), "Any position");
        form_label("PlayStyle");
        ImGui::SetNextItemWidth(fill_w());
        if (ImGui::BeginCombo("##fstyle", f_style < 0 || !styles[static_cast<size_t>(f_style)] ? "Any PlayStyle"
                                                                                                 : styles[static_cast<size_t>(f_style)],
                              ImGuiComboFlags_HeightLargest)) {
            if (ImGui::Selectable("Any PlayStyle", f_style < 0)) f_style = -1;
            for (size_t b = 0; b < styles.size(); ++b)
                if (styles[b] && ImGui::Selectable(styles[b], f_style == static_cast<int>(b))) f_style = static_cast<int>(b);
            ImGui::EndCombo();
        }
        checkbox_right("+ only", &f_plus);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only players with the PlayStyle+ (icontrait1)");
        // ---- Numbers
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Numbers");
        {
            const float avail = ImGui::GetContentRegionAvail().x;
            const bool compact = avail < S(200.0f);  // short labels and smaller boxes keep both boxes of a range in view
            const float label_w = compact ? S(38.0f) : S(76.0f);
            const float box_w = std::max(S(28.0f), std::min(S(46.0f), (avail - label_w - ImGui::CalcTextSize("to").x - S(12.0f)) * 0.5f));
            form_label_width(label_w);
            range_filter_row("Age", "age", f_age.from, f_age.to, 99, box_w,
                             "Age, from .. to (0 = no bound). Players without a birth date are left out when a bound is set");
            range_filter_row(compact ? "OVR" : "Overall", "ovr", f_ovr.from, f_ovr.to, 99, box_w, "Overall rating, from .. to (0 = no bound)");
            range_filter_row(compact ? "POT" : "Potential", "pot", f_pot.from, f_pot.to, 99, box_w, "Potential, from .. to (0 = no bound)");
            form_label_width(S(76.0f));
        }
        // ---- Status: the three toggles (My club sits here so every block has three rows; ticking it drops the club pick)
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Status");
        if (!can_my_club) ImGui::BeginDisabled();
        if (checkbox_right("My club", &my_club) && my_club) f_team = 0;
        if (!can_my_club) {
            ImGui::EndDisabled();
            my_club = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only the players of your club (needs a career)");
        checkbox_right("Retiring", &f_retiring);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only players who retire at the end of the season (isretiring)");
        checkbox_right("Free agent", &f_free);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only players without a club");
        ImGui::EndTable();
    }
    form_label_width(0.0f);

    const std::unordered_map<int64_t, int64_t>& nat_of = player_nationalities(app);
    char fkey[200];
    std::snprintf(fkey, sizeof(fkey), "|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%lld|%lld|%lld|%d", f_pos, f_style, f_plus ? 1 : 0, f_retiring ? 1 : 0,
                  f_free ? 1 : 0, f_age.from, f_age.to, f_ovr.from, f_ovr.to, f_pot.from, f_pot.to, static_cast<long long>(f_team),
                  static_cast<long long>(f_nat), static_cast<long long>(f_cont), f_gender);
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
            if (f_gender >= 0 && p.gender != f_gender) continue;
            if (f_free && p.club != 0) continue;
            if (f_nat >= 0 || f_cont >= 0) {
                auto n = nat_of.find(p.playerid);
                const int64_t nat = n == nat_of.end() ? -1 : n->second;
                if (f_nat >= 0 && nat != f_nat) continue;
                if (f_cont >= 0 && geo(app).continent_of_nation(nat) != f_cont) continue;
            }
            if (!range_passes(f_ovr, p.overall)) continue;
            if (!range_passes(f_pot, p.potential)) continue;
            if (!range_passes(f_age, p.age, p.age >= 0)) continue;
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

    ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                         ImGuiTableFlags_Hideable;
    if (ImGui::BeginTable("##players", 8, fl, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(62.0f));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Club", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Nationality", ImGuiTableColumnFlags_WidthFixed, S(96.0f));
        ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, S(38.0f));
        ImGui::TableSetupColumn("OVR", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, S(36.0f));
        ImGui::TableSetupColumn("POT", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, S(36.0f));
        ImGui::TableSetupColumn("Age", ImGuiTableColumnFlags_WidthFixed, S(34.0f));
        ImGui::TableHeadersRow();

        const Geo& g = geo(app);
        static const std::string no_name;
        auto nat_name = [&](const PlayerRow* r) -> const std::string& {
            auto n = nat_of.find(r->playerid);
            if (n == nat_of.end()) return no_name;
            auto it = g.nations.find(n->second);
            return it == g.nations.end() ? no_name : it->second.name;
        };
        // Header clicks sort the filtered list (stable, so equal rows keep the order they had; text case-insensitively,
        // numbers numerically). The sort is redone when the filtered list is rebuilt.
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if ((specs->SpecsDirty || need_sort) && specs->SpecsCount > 0) {
                const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
                bool asc = s.SortDirection == ImGuiSortDirection_Ascending;
                int col = s.ColumnIndex;
                std::stable_sort(rows.begin(), rows.end(), [&](const PlayerRow* a, const PlayerRow* b) {
                    int c = 0;
                    switch (col) {
                        case 0: c = (a->playerid < b->playerid) ? -1 : (a->playerid > b->playerid); break;
                        case 1: c = ci_compare(a->name, b->name); break;
                        case 2: c = ci_compare(a->club_name, b->club_name); break;
                        case 3: c = ci_compare(nat_name(a), nat_name(b)); break;
                        case 4: c = a->position - b->position; break;
                        case 5: c = a->overall - b->overall; break;
                        case 6: c = a->potential - b->potential; break;
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
                ImGui::TextUnformatted(nat_name(p).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(position_name(p->position));
                if (!position_visible(p->position) && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", position_descriptor(p->position).c_str());
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
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "Position %d", k);
        form_label(lbl, k == 1 ? "The main position (the overall formula follows it)" : "A further preferred position, or None");
        // the game's visible positions (core/model.h); a stored internal code is shown with its descriptor and stays selectable
        int cur = static_cast<int>(v.i);
        if (position_combo("##pos", cur, S(190.0f), f->min <= -1 ? "None" : nullptr, static_cast<int>(std::max<int64_t>(0, f->min)),
                           static_cast<int>(f->max())))
            app.edit(t, rec, *f, Value::of_int(cur));
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
    if (ImGui::BeginTable("##bits", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        for (int b = 0; b < bits; ++b) {
            ImGui::TableNextColumn();
            bool on = (static_cast<uint64_t>(v.i - f->min) >> b) & 1;
            char lbl[64];
            if (b < static_cast<int>(names.size()) && names[static_cast<size_t>(b)])
                std::snprintf(lbl, sizeof(lbl), "%s##%d", names[static_cast<size_t>(b)], b);
            else
                std::snprintf(lbl, sizeof(lbl), "Unnamed bit %d##%d", b, b);
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
        else if (app.bridge.state().game_moves && (std::string(key) == "move_transfer" || std::string(key) == "move_release"))
            ImGui::SetTooltip("Done by the game itself (Turbo.dll): the squad screens show it at once. A loaned player's move "
                              "ends his loan in the career database instead (shows after saving and loading). Back up your save first.");
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

// The squad role of one player of YOUR club (features/squad_role.lua with a playerid: through the game's own PlayerStatusManager
// calls, Turbo.dll squad_role op 14, else in memory; a player without a squad status entry gets one). Read = the role the game
// holds now. The game has squad roles for the user's club only; the Squad screen reads them when it is built.
static void squad_role_section(App& app, const PlayerRow& p) {
    const BridgeState& st = app.bridge.state();
    if (!st.in_cm || st.user_team <= 0 || p.club != st.user_team) return;
    ImGui::SeparatorText("Squad role (your club)");
    ImGui::PushID("sqrole");
    static int role = 2;  // combo index: 2 = "3 Rotation"
    const char* roles[] = {"1 Crucial", "2 Important", "3 Rotation", "4 Sporadic", "5 Prospect"};
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::Combo("##role", &role, [](void* d, int i) { return static_cast<const char**>(d)[i]; }, roles, 5);
    if (role < 0 || role > 4) role = 2;
    const bool off = app.busy() || !app.mailbox;
    if (off) ImGui::BeginDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Set squad role"))
        app.send({{"op", "run"}, {"module", "squad_role"}, {"overrides", {{"playerid", p.playerid}, {"role", role + 1}}}}, "Squad role " + p.name);
    ImGui::SameLine();
    if (ImGui::Button("Read squad role"))
        app.send({{"op", "run"}, {"module", "squad_role"}, {"overrides", {{"playerid", p.playerid}, {"mode", "check"}}}}, "Squad role " + p.name);
    if (off) ImGui::EndDisabled();
    ImGui::TextDisabled("Open the Squad screen again to see it. The game recomputes every role at the start of a season.");
    ImGui::PopID();
}

static void moves_popup(App& app, const PlayerRow& p) {
    static int to_team = 0, fee = 0, wage = 0, months = 60, loan_months = 12;
    if (ImGui::BeginPopup("##moves")) {
        const int64_t pid = p.playerid;
        const move_rules::Facts facts = move_facts(app, p);
        ImGui::TextDisabled("%s", app.bridge.state().game_moves ? "Transfer: the game makes the move at once. Loan: written into the career database"
                                                                 : "Written into the career database by Turbo's Lua side");
        club_search_box(app, "To club", to_team, S(220.0f));  // name or ID (ui_club_search.h); loans go there too
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Fee", &fee, 0);
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Wage", &wage, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = he keeps his current wage");
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Contract months", &months, 0);
        const bool exists = app.model.team(to_team) != nullptr;
        const bool national = exists && app.model.is_national_team(to_team);
        const int to_squad = exists ? static_cast<int>(app.model.links_of_team(to_team).size()) : -1;
        const std::string why_t = move_rules::why_not_to("transfer", facts, to_team, exists, national, to_squad);
        const std::string why_l = move_rules::why_not_to("loan", facts, to_team, exists, national, to_squad);
        const int64_t user = facts.user_team;
        if (user > 0 && (to_team == user || facts.club == user))
            ImGui::TextWrapped("%s", move_rules::own_club_note(app.bridge.state().game_moves));
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

// ---------------------------------------------------------------- attributes: archetypes and overall +1 / -1
// Changes made by one click of "Overall +1/-1" or by applying an archetype touch many fields; App::edit keeps only the last
// App::kUndoSteps single-field steps, so these bulk changes keep their own undo (App::bulk_undo, newest last; an overall
// typed into a field box adds one too, App::set_overall)

// All 34 attributes of the player; false when the players table lacks one of them
static bool read_attrs(App& app, const Table& t, uint64_t rec, overall::Attrs& a) {
    for (int i = 0; i < overall::kAttrCount; ++i) {
        if (!t.has(overall::kAttrNames[i])) return false;
        a[static_cast<size_t>(i)] = static_cast<int>(app.db.get_int(t, rec, overall::kAttrNames[i], 0));
    }
    return true;
}

// Write the attributes that differ (and `ovr_to` into overallrating when >= 0) through App::edit_field: the same validated
// path as the sliders (range check, undo steps, cached rows, kept edits, development plan); the overall as given, the
// attributes already moved. Keeps a bulk undo entry. Returns the number of fields written.
static int write_bulk(App& app, const Table& t, const PlayerRow& p, const overall::Attrs& before, const overall::Attrs& after,
                      int ovr_to, const std::string& label) {
    App::BulkUndo u;
    u.playerid = p.playerid;
    u.label = label;
    int n = 0;
    for (int i = 0; i < overall::kAttrCount; ++i) {
        if (before[static_cast<size_t>(i)] == after[static_cast<size_t>(i)]) continue;
        const Field* f = t.field(overall::kAttrNames[i]);
        if (!f) continue;
        if (app.edit_field(t, p.rec, *f, Value::of_int(after[static_cast<size_t>(i)]))) {
            u.before.push_back({f->name, before[static_cast<size_t>(i)]});
            ++n;
        }
    }
    if (ovr_to >= 0) {
        if (const Field* f = t.field("overallrating")) {
            const int64_t was = app.db.get_int(t, p.rec, "overallrating", 0);
            if (was != ovr_to && app.edit_field(t, p.rec, *f, Value::of_int(ovr_to))) {
                u.before.push_back({f->name, was});
                ++n;
            }
        }
    }
    app.push_bulk_undo(std::move(u));
    return n;
}

static void undo_bulk(App& app, const Table& t, const PlayerRow& p) {
    auto& st = app.bulk_undo;
    for (size_t i = st.size(); i-- > 0;) {
        if (st[i].playerid != p.playerid) continue;
        App::BulkUndo u = st[i];
        st.erase(st.begin() + static_cast<long>(i));
        // every field as it was (the overall too: its attributes go back with it)
        for (const auto& b : u.before)
            if (const Field* f = t.field(b.first)) app.edit_field(t, p.rec, *f, Value::of_int(b.second));
        app.notify("undone: " + u.label);
        return;
    }
}

static const char* attr_label(int i) {
    static std::string labels[overall::kAttrCount];
    if (labels[i].empty()) labels[i] = field_label(overall::kAttrNames[i]);
    return labels[i].c_str();
}

// Table of the attributes that differ between two sets (tooltips and the archetype preview)
static void changes_table(const overall::Attrs& before, const overall::Attrs& after) {
    if (ImGui::BeginTable("##chg", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Attribute", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, S(50.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(24.0f));
        ImGui::TableSetupColumn("New", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableHeadersRow();
        for (int i = 0; i < overall::kAttrCount; ++i) {
            const int b = before[static_cast<size_t>(i)], a = after[static_cast<size_t>(i)];
            if (a == b) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(attr_label(i));
            ImGui::TableNextColumn();
            ImGui::Text("%d", b);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("->");
            ImGui::TableNextColumn();
            ImGui::TextColored(a > b ? ImVec4(0.45f, 0.9f, 0.5f, 1) : ImVec4(1.0f, 0.6f, 0.45f, 1), "%d (%+d)", a, a - b);
        }
        ImGui::EndTable();
    }
}

// Overall +1 / -1 and the archetype picker above the sliders
static void archetype_bar(App& app, const Table& t, const PlayerRow& p) {
    using namespace overall;
    static int sel_arch = -1;
    static archetypes::Result pend;
    static const archetypes::Archetype* pend_arch = nullptr;
    static Attrs pend_before{};
    static int64_t pend_pid = 0;

    Attrs cur{};
    ImGui::AlignTextToFramePadding();
    if (!read_attrs(app, t, p.rec, cur)) {
        ImGui::TextDisabled("Archetypes and Overall +1 / -1 need all 34 attribute fields; this database lacks some.");
        return;
    }
    const int pos = p.position;
    const Field* of = t.field("overallrating");
    const int64_t stored = app.db.get_int(t, p.rec, "overallrating", 0);
    ImGui::Text("Overall %lld", static_cast<long long>(stored));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 works the overall out from the attributes (the player card, its own growth): a bare overall does not last.\n"
                          "So Overall +1 / -1 and an overall typed into the Overall box (Profile, All fields) move the attributes that\n"
                          "weigh most for his position until the formula gives it; Undo / Undo bulk put them back.");
    ImGui::SameLine();
    ImGui::TextDisabled("(formula for %s: %d)", position_name(pos), compute_overall(pos, cur));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC's overall is a position-weighted sum of the attributes (%s group). The formula here matches the stored "
                          "overall of ~91%% of real players exactly and is within 1 for ~99.9%%.", group_name(group_of_position(pos)));
    // +1 / -1: the attributes that weigh most for his position move, so the overall changes by exactly one
    for (int delta : {-1, 1}) {
        ImGui::SameLine();
        Attrs trial = cur;
        std::vector<Move> moves;
        // 1..99 (the field itself holds up to 128): a stored 99 whose formula says 98 must not become 100
        const bool in_range = of && stored + delta >= std::max<int64_t>(1, of->min) && stored + delta <= std::min<int64_t>(99, of->max());
        const bool ok = in_range && adjust(pos, trial, delta, &moves);
        if (!ok) ImGui::BeginDisabled();
        if (ImGui::Button(delta > 0 ? "Overall +1" : "Overall -1")) {
            const int n = write_bulk(app, t, p, cur, trial, static_cast<int>(stored + delta), delta > 0 ? "Overall +1" : "Overall -1");
            app.notify(std::string("Overall ") + (delta > 0 ? "+1" : "-1") + ": " + std::to_string(std::max(0, n - 1)) + " attributes changed");
        }
        if (!ok) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!ok) {
                ImGui::SetTooltip("%s", !in_range ? "The overall cannot go beyond 1..99" : "No attribute of his position can move any more");
            } else if (ImGui::BeginTooltip()) {
                ImGui::Text("Overall %lld -> %lld by moving %zu attributes:", static_cast<long long>(stored),
                            static_cast<long long>(stored + delta), moves.size());
                changes_table(cur, trial);
                ImGui::EndTooltip();
            }
        }
    }
    size_t mine = 0;
    for (const auto& u : app.bulk_undo) mine += u.playerid == p.playerid;
    ImGui::SameLine();
    if (!mine) ImGui::BeginDisabled();
    char ulbl[48];
    std::snprintf(ulbl, sizeof(ulbl), "Undo bulk (%zu)##bulkundo", mine);
    if (ImGui::Button(ulbl)) undo_bulk(app, t, p);
    if (!mine) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Puts back everything the last Overall +1/-1 click or archetype changed (the Undo button above is for single fields)");

    // archetype picker: his own position group first, the others below
    const std::vector<archetypes::Archetype>& all = archetypes::all();
    const Group mygroup = group_of_position(pos);
    if (sel_arch >= static_cast<int>(all.size())) sel_arch = -1;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Archetype");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(240.0f));
    if (ImGui::BeginCombo("##arch", sel_arch < 0 ? "Choose a role..." : all[static_cast<size_t>(sel_arch)].name, ImGuiComboFlags_HeightLargest)) {
        for (int pass = 0; pass < 2; ++pass) {
            for (int g = 0; g < kGroupCount; ++g) {
                if ((pass == 0) != (g == mygroup)) continue;
                char hdr[96];
                std::snprintf(hdr, sizeof(hdr), "%s%s", group_name(static_cast<Group>(g)), g == mygroup ? " (his position)" : "");
                ImGui::SeparatorText(hdr);
                for (size_t i = 0; i < all.size(); ++i) {
                    if (all[i].group != g) continue;
                    if (ImGui::Selectable(all[i].name, sel_arch == static_cast<int>(i))) sel_arch = static_cast<int>(i);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", all[i].blurb);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (sel_arch < 0) ImGui::BeginDisabled();
    if (ImGui::Button("Preview / apply...") && sel_arch >= 0) {
        pend_arch = &all[static_cast<size_t>(sel_arch)];
        pend_before = cur;
        pend_pid = p.playerid;
        archetypes::apply(*pend_arch, pos, cur, pend);
        ImGui::OpenPopup("Apply archetype");
    }
    if (sel_arch < 0) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("reshapes the attributes, keeps the overall");

    if (ImGui::BeginPopupModal("Apply archetype", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!pend_arch || pend_pid != p.playerid) {
            ImGui::TextDisabled("Another player is selected.");
        } else {
            ImGui::Text("%s as %s", p.name.c_str(), pend_arch->name);
            ImGui::TextDisabled("%s", pend_arch->blurb);
            if (pend_arch->group != mygroup)
                ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "This role is from another position group (%s); the overall is kept by his own position formula.",
                                   group_name(pend_arch->group));
            ImGui::Text("Overall (formula, %s): %d -> %d", position_name(pos), pend.overall_before, pend.overall_after);
            if (!pend.exact)
                ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "This profile cannot reach exactly his overall (at the edge of the 1..99 range).");
            ImGui::BeginChild("##archchg", ImVec2(S(440.0f), S(380.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY);
            const bool same = pend.attrs == pend_before;
            if (same) ImGui::TextDisabled("Nothing would change.");
            else changes_table(pend_before, pend.attrs);
            ImGui::EndChild();
            if (same) ImGui::BeginDisabled();
            if (ImGui::Button("Apply")) {
                Attrs now{};
                if (read_attrs(app, t, p.rec, now)) {
                    const int n = write_bulk(app, t, p, now, pend.attrs, -1, std::string("archetype ") + pend_arch->name);
                    app.notify(std::string("Archetype ") + pend_arch->name + ": " + std::to_string(n) + " attributes changed");
                }
                ImGui::CloseCurrentPopup();
            }
            if (same) ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel##arch")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The attribute sliders: one heading per category with its average, roomy rows (two columns when the pane is wide)
static void attribute_sliders(App& app, const Table& t, uint64_t rec) {
    const bool wide = ImGui::GetContentRegionAvail().x >= S(760.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10.0f), S(10.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6.0f), S(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(14.0f), S(3.0f)));
    for (const auto& g : attribute_groups()) {
        // group average over the fields present
        long long sum = 0;
        int cnt = 0;
        for (const auto& fn : g.second)
            if (t.has(fn)) { sum += app.db.get_int(t, rec, fn, 0); ++cnt; }
        std::vector<const Field*> present;
        for (const auto& fn : g.second)
            if (const Field* f = t.field(fn)) present.push_back(f);
        if (present.empty()) continue;
        ImGui::Dummy(ImVec2(0, S(6.0f)));
        char hdr[96];
        if (cnt) std::snprintf(hdr, sizeof(hdr), "%s  (average %.1f)", g.first, double(sum) / cnt);
        else std::snprintf(hdr, sizeof(hdr), "%s", g.first);
        ImGui::SeparatorText(hdr);
        ImGui::PushID(g.first);
        if (ImGui::BeginTable("##sl", wide ? 2 : 1, ImGuiTableFlags_SizingStretchSame)) {
            for (const Field* f : present) {
                ImGui::TableNextColumn();
                slider_editor_ex(app, t, rec, *f, field_label(f->name).c_str(), S(wide ? 132.0f : 150.0f), -FLT_MIN, S(58.0f));
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar(3);
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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", move_rules::own_club_note(app.bridge.state().game_moves));
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
            if (!hits.empty() && ImGui::BeginTable("##fsearch", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersOuter)) {
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
    // ten tabs: let them shrink further than the default 80 px before the bar scrolls, so Growth stays visible in a normal window
    ImGui::PushStyleVar(ImGuiStyleVar_TabMinWidthShrink, S(40.0f));
    if (ImGui::BeginTabBar("##ptabs")) {
        if (ImGui::BeginTabItem("Profile")) {
            ImGui::BeginChild("##prof");
            field_grid(app, *t, p->rec, {"overallrating", "potential", "modifier"}, "##ovrgrid", 3);
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
                                          "runstylecode", "usercaneditname", "iscustomized"}, "##rolegrid", 2);
            bodytype_gallery_button(app, *t, p->rec, false);  // ui_bodytypes.cpp
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Names")) {
            ImGui::BeginChild("##pnames");
            names_editor(app, *t, *p);  // ui_names.cpp
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Attributes")) {
            ImGui::BeginChild("##attr");
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Stored overall %lld, potential %lld,", static_cast<long long>(app.db.get_int(*t, p->rec, "overallrating", 0)),
                                static_cast<long long>(app.db.get_int(*t, p->rec, "potential", 0)));
            if (const Field* mf = t->field("modifier")) {
                ImGui::SameLine();
                ImGui::TextDisabled("OVR modifier");
                ImGui::SameLine();
                field_editor(app, *t, p->rec, *mf, nullptr, S(70.0f));
            }
            ImGui::TextDisabled("(the game recalculates the shown overall from the attributes when the player card is drawn)");
            reapply_player_line(app, *p);  // kept for every career load, his development plan (ui_reapply.cpp)
            archetype_bar(app, *t, *p);
            attribute_sliders(app, *t, p->rec);
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
            ImGui::TextDisabled("head asset %lld, head class %s", static_cast<long long>(app.db.get_int(*t, p->rec, "headassetid", 0)),
                                code_value_text("headclasscode", app.db.get_int(*t, p->rec, "headclasscode", 0)).c_str());
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
            squad_role_section(app, *p);
            transfer_ban_section(app, "player", p->playerid);
            ImGui::SeparatorText("Team links (teamplayerlinks)");
            const Table* lt = app.db.table("teamplayerlinks");
            if (lt) {
                for (const auto& l : app.model.links_of_player(p->playerid)) {
                    ImGui::PushID(static_cast<int>(l.rec & 0x7FFFFFFF));
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s%s", app.model.team_name(l.teamid).c_str(), app.model.is_national_team(l.teamid) ? " [national team]" : "");
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Club ID %lld", static_cast<long long>(l.teamid));
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
        ImGui::PopStyleVar();  // (TabMinWidthShrink)
    } else {
        ImGui::PopStyleVar();
    }
}

void draw_players(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    // list width: up to 560 (room for the Nationality column) but never more than ~45% of the window, so the editor's tabs keep their room
    const float list_w = std::min(S(560.0f), std::max(S(430.0f), ImGui::GetContentRegionAvail().x * 0.45f));
    ImGui::BeginChild("##plist", ImVec2(list_w, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    player_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pedit", ImVec2(0, 0));
    player_editor(app);
    ImGui::EndChild();
}

}  // namespace turbo
