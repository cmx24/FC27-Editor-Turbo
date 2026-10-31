// FC 27 LE Turbo GUI - the player list (draw_classic_player_list, shared by the Profile and Attributes tabs of Edit a player) and the player
// editing sections other pages call (moves, lists, bans, squad role, growth, the archetype bar).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <unordered_map>

#include "app.h"
#include "core/archetypes.h"
#include "table_registry.h"
#include "core/field_labels.h"
#include "core/overall.h"
#include "core/range_filter.h"
#include "core/team_preset.h"
#include "geo.h"
#include "imgui.h"
#include "move_rules.h"
#include "playstyles.h"
#include "ui_visual_picker.h"
#include "ui_archetype.h"
#include "ui_archetype_populate.h"
#include "ui_player_profile.h"
#include "ui_images.h"
#include "ui_team_filter.h"
#include "ui_theme.h"

namespace turbo {

using nlohmann::json;

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
static size_t g_list_rows = 0;  // rows of the last frame (the pages that share this list read it)
size_t classic_player_list_rows() { return g_list_rows; }

// Turbo X: what the empty state asks of the list (ui_player_profile.cpp list_and_editor): focus the search box once, tick My club
static bool g_focus_search = false, g_want_my_club = false;
void classic_player_list_request(bool focus_search, bool my_club) {
    g_focus_search = g_focus_search || focus_search;
    g_want_my_club = g_want_my_club || my_club;
}

// Turbo X: a filter chip: a small pill that lights up when on; wraps to the next line when it does not fit. Returns true when toggled.
static bool filter_chip(const char* label, bool* on, bool enabled = true, bool first = false) {
    const float fs = ImGui::GetFontSize();
    const float w = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + fs * 1.2f;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;  // the cursor is at the start of a new line here
    if (!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w < right) ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, fs);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(fs * 0.6f, ImGui::GetStyle().FramePadding.y * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, *on ? theme::accent_fill() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, *on ? theme::accent_fill_hover() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accent_fill_active());
    if (!enabled) ImGui::BeginDisabled();
    const bool hit = ImGui::Button(label);
    if (!enabled) ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    if (hit) *on = !*on;
    return hit;
}

// Turbo X: the position group of a stored code for the GK / DEF / MID / ATT chips (core/model.cpp kPositions: 0 GK, 1-8 SW..LWB,
// 9-19 RDM..LAM, 20-27 RF..LW); -1 outside the table
static int position_group(int pos) {
    if (pos < 0 || pos >= position_count()) return -1;
    return pos == 0 ? 0 : pos <= 8 ? 1 : pos <= 19 ? 2 : 3;
}

// Turbo X: the row's picture: the miniface when the game (or mods\legacy) has it, else a muted disc (no words at 32 px)
static void row_picture(App& app, int64_t pid, float side) {
    const std::string path = legacy_path::player_miniface(pid);
    std::filesystem::path f;
    const LegacyImages::State st = app.legacy.locate(path, &f, true);
    if (st == LegacyImages::State::Custom || st == LegacyImages::State::Game) {
        draw_legacy_picture(app, path, side, true);
        return;
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + side * 0.5f, p.y + side * 0.5f), side * 0.5f, ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImGui::Dummy(ImVec2(side, side));
}

// 2.22: the one player list of Edit a player (its Profile and Positions tabs draw it too: one selection, one set of filters).
// Turbo X (DESIGN_SPEC_X.md): search, chips, the form behind a "Filters (n)" fold, then rows that are people (theme::person_row).
void draw_classic_player_list(App& app) {
    static char search[64] = "";
    static bool my_club = false;
    static int f_group = -1;  // chips GK / DEF / MID / ATT (position_group), -1 = any
    static int sort_by = 0;   // the Sort combo: kSorts below
    // Filters, in four blocks (widgets.cpp form rows: label left, control right). Who: club, nationality, continent (gender: the search line). Position &
    // style: any of the 7 preferred positions (core/model.h player_passes_position: a stored internal code counts as the visible
    // position it plays as, RCB as CB, unless the combo's "Show all internal positions" is on; then the code must match exactly),
    // PlayStyle / PlayStyle+. Numbers: age, overall and potential as two-sided ranges (core/range_filter.h: 0 = no bound).
    // Status: my club, retiring, free agent.
    static int f_pos = -1, f_style = -1;
    static IntRange f_age, f_ovr, f_pot;
    static bool f_plus = false, f_retiring = false, f_free = false;
    static int64_t f_team = 0;  // club filter (0 = any)
    static int64_t f_nat = -1, f_cont = -1;  // nationality (nations.nationid) and continent (nations.confederation) filters, -1 = any
    static int f_gender = -1;                 // players.gender: -1 any, 0 men, 1 women
    static int f_loan = LoanAny;              // playerloans (core/model.h player_passes_loan): any, on loan, loaned in, loaned out
    static char f_team_search[64] = "";
    static std::unordered_set<int64_t> team_players;  // players linked to f_team (rebuilt with the rows)
    static std::vector<const PlayerRow*> rows;
    static std::string last_key;
    static uint64_t last_version = ~uint64_t(0);
    static bool need_sort = true;

    const bool can_my_club = app.bridge.state().user_team > 0;
    if (g_want_my_club) {
        g_want_my_club = false;
        if (can_my_club) {
            my_club = true;
            f_team = 0;
        }
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (g_focus_search) {
        g_focus_search = false;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##psearch", "name, ID or club", search, sizeof(search));
    // the chips: My club, Free agents, and one position group; a chip is the same filter as its row in the form below
    if (filter_chip("My club###pl_chip_mine", &my_club, can_my_club, true) && my_club) f_team = 0;
    if (!can_my_club) my_club = false;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Only the players of your club (needs a career)");
    filter_chip("Free agents###pl_chip_free", &f_free);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only players without a club");
    {
        static const char* const kGroups[] = {"GK###pl_chip_gk", "DEF###pl_chip_def", "MID###pl_chip_mid", "ATT###pl_chip_att"};
        static const char* const kGroupTips[] = {"Goalkeepers", "Defenders (SW, RB, CB, LB, wing-backs)", "Midfielders (CDM, CM, CAM, wide midfielders)",
                                                 "Attackers (ST, CF, wingers)"};
        for (int k = 0; k < 4; ++k) {
            bool on = f_group == k;
            if (filter_chip(kGroups[k], &on)) f_group = on ? k : -1;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kGroupTips[k]);
        }
    }

    // The whole filter form, folded; the count says how many of its filters are set (the chips show their own state)
    const int active = (f_pos >= 0) + (f_style >= 0) + f_plus + f_retiring + f_age.active() + f_ovr.active() + f_pot.active() + (f_team > 0) +
                       (f_nat >= 0) + (f_cont >= 0) + (f_gender >= 0) + (f_loan != LoanAny);
    char fold[48];
    if (active > 0) std::snprintf(fold, sizeof(fold), "Filters (%d)###pl_filters", active);
    else std::snprintf(fold, sizeof(fold), "Filters###pl_filters");
    if (ImGui::TreeNodeEx(fold, ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        if (ImGui::SmallButton("Clear##filters")) {
            f_pos = f_style = -1;
            f_age.clear();
            f_ovr.clear();
            f_pot.clear();
            f_plus = f_retiring = f_free = false;
            f_team = 0;
            f_nat = f_cont = -1;
            f_gender = -1;
            f_loan = LoanAny;
            f_group = -1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear the filters and the position chips (the search box and My club stay)");
        // gender (players.gender; 1.2.5) on the Clear line, so the filter grid keeps its three rows per block
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
        // loans (playerloans, read with the rows): on the Clear line too; In / Out are seen from the Club filter's club, else
        // from your club; with neither, both mean "on loan"
        {
            static const char* const kLoans[] = {"Any loan status", "On loan", "Loaned in", "Loaned out"};
            const bool known = app.model.loans_known();
            if (!known) f_loan = LoanAny;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(130.0f));
            if (!known) ImGui::BeginDisabled();
            if (ImGui::BeginCombo("##floan", kLoans[f_loan])) {
                for (int k = LoanAny; k <= LoanOut; ++k)
                    if (ImGui::Selectable(kLoans[k], f_loan == k)) f_loan = k;
                ImGui::EndCombo();
            }
            if (!known) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", known ? "Players on loan (playerloans). Loaned in: playing on loan for the club picked in Club, else for your "
                                                "club. Loaned out: lent out by that club (they play for another club). Without a club both "
                                                "mean every player on loan."
                                              : "This database has no playerloans table");
        }

        // The filter grid: four blocks, a small header each, label left / control right (widgets.cpp form rows). Three rows per block.
        // Tab moves down a block, then on to the next block. A table cell clips what does not fit, so the Numbers block shortens its
        // labels and boxes in a narrow column. Turbo X: in the 320 px pane the blocks go two by two (TableNextColumn wraps to the next row).
        form_label_width(S(76.0f));
        const auto& styles = playstyle1_names();
        auto fill_w = [] { return std::max(S(60.0f), ImGui::GetContentRegionAvail().x); };
        const int blocks = ImGui::GetContentRegionAvail().x < S(620.0f) ? 2 : 4;
        if (ImGui::BeginTable("##pfilters", blocks, tbl::kGrid | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Position & style", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            if (blocks == 4) {
                ImGui::TableSetupColumn("Numbers", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.7f);
            }
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
            form_label("Position", "Any of the player's 7 preferred positions. A player stored as RCB or LCB counts as CB (RDM/LDM as CDM, RCM/LCM as "
                                   "CM, RAM/LAM as CAM, RS/LS as ST) unless \"Show all internal positions\" is ticked in the list; then the code "
                                   "must match exactly");
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
    }

    const std::unordered_map<int64_t, int64_t>& nat_of = player_nationalities(app);
    char fkey[200];
    const bool pos_exact = position_show_all();  // the combo's toggle also switches the filter to exact codes
    std::snprintf(fkey, sizeof(fkey), "|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%lld|%lld|%lld|%d|%d|%d|%d", f_pos, pos_exact ? 1 : 0, f_style,
                  f_plus ? 1 : 0, f_retiring ? 1 : 0, f_free ? 1 : 0, f_age.from, f_age.to, f_ovr.from, f_ovr.to, f_pot.from, f_pot.to,
                  static_cast<long long>(f_team), static_cast<long long>(f_nat), static_cast<long long>(f_cont), f_gender, f_loan, f_group, sort_by);
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
        // Loaned out: the player plays for another club, so "his club" (My club, the Club filter) is the club that lent him
        const bool out_view = f_loan == LoanOut;
        const int64_t loan_club = f_team > 0 ? f_team : club;
        for (const auto& p : app.model.players()) {
            if (my_club && !(out_view ? p.on_loan && p.loaned_from == club : p.club == club)) continue;
            if (f_team > 0 && !(out_view ? p.on_loan && p.loaned_from == f_team : team_players.count(p.playerid) > 0)) continue;
            if (f_loan != LoanAny && !player_passes_loan(p, f_loan, loan_club)) continue;
            if (f_pos >= 0 && !player_passes_position(p, f_pos, pos_exact)) continue;
            if (f_group >= 0 && position_group(p.position) != f_group) continue;
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

    g_list_rows = rows.size();
    const Geo& g = geo(app);
    static const std::string no_name;
    auto nat_name = [&](const PlayerRow* r) -> const std::string& {
        auto n = nat_of.find(r->playerid);
        if (n == nat_of.end()) return no_name;
        auto it = g.nations.find(n->second);
        return it == g.nations.end() ? no_name : it->second.name;
    };
    // the count and the Sort combo on one line (the 2.22 table's sortable headers: names and clubs case-insensitively, numbers best first)
    static const char* const kSorts[] = {"Name", "OVR", "POT", "Age", "Calc OVR", "Delta", "Club", "ID"};
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%zu players", rows.size());
    ImGui::SameLine();
    const float sort_w = S(118.0f);
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - sort_w));
    ImGui::SetNextItemWidth(sort_w);
    if (ImGui::BeginCombo("##psort", kSorts[sort_by])) {
        for (int k = 0; k < 8; ++k)
            if (ImGui::Selectable(kSorts[k], sort_by == k)) {
                sort_by = k;
                need_sort = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sort_by == 4 || sort_by == 5 ? kCalcOvrHelp : "The order of the list (numbers: best first)");
    if (need_sort) {
        std::stable_sort(rows.begin(), rows.end(), [&](const PlayerRow* a, const PlayerRow* b) {
            int c = 0;
            switch (sort_by) {
                case 1: c = b->overall - a->overall; break;
                case 2: c = b->potential - a->potential; break;
                case 3: c = a->age - b->age; break;
                case 4: c = b->calc_overall - a->calc_overall; break;  // -1 (not computable) sorts last
                case 5: c = calc_delta(*b) - calc_delta(*a); break;
                case 6: c = ci_compare(a->club_name, b->club_name); break;
                case 7: c = (a->playerid < b->playerid) ? -1 : (a->playerid > b->playerid); break;
                default: c = ci_compare(a->name, b->name); break;
            }
            return c < 0;
        });
        need_sort = false;
    }

    // The rows: people, 44 px each (theme::person_row); "##players" stays the list's id. Up / Down move the pick when the list has
    // the keyboard; the picked row is scrolled into view then.
    const float row_h = S(44.0f), pic = S(32.0f);
    static const char* const kDot = " \xc2\xb7 ";
    if (ImGui::BeginChild("##players", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        int scroll_to = -1;
        if (!rows.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
            const int step = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 0;
            if (step != 0) {
                int at = -1;
                for (size_t i = 0; i < rows.size(); ++i)
                    if (rows[i]->playerid == app.sel_player) at = static_cast<int>(i);
                const int to = std::max(0, std::min(static_cast<int>(rows.size()) - 1, at < 0 ? 0 : at + step));
                app.sel_player = rows[static_cast<size_t>(to)]->playerid;
                scroll_to = to;
            }
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()), row_h + ImGui::GetStyle().ItemSpacing.y);
        if (scroll_to >= 0) clipper.IncludeItemByIndex(scroll_to);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PlayerRow* p = rows[static_cast<size_t>(i)];
                char idbuf[32];
                std::snprintf(idbuf, sizeof(idbuf), "%lld", static_cast<long long>(p->playerid));
                std::string line = p->club_name.empty() ? std::string("Free agent") : p->club_name + (p->on_loan ? " (loan)" : "");
                line += kDot;
                line += position_name(p->position);
                if (p->age >= 0) line += kDot + std::to_string(p->age);
                bool hovered = false;
                const bool selected = app.sel_player == p->playerid;
                if (theme::person_row(idbuf, p->name.c_str(), line.c_str(), p->overall, selected, row_h, pic, [&] { row_picture(app, p->playerid, pic); }, &hovered))
                    app.sel_player = p->playerid;
                if (selected && scroll_to == i) ImGui::SetScrollHereY(0.5f);
                if (hovered) {
                    // the 2.22 table's other columns live here: ID, nationality, potential, what the attributes give, the modifier
                    std::string tip = "ID " + std::string(idbuf);
                    const std::string& nat = nat_name(p);
                    if (!nat.empty()) tip += "\n" + nat;
                    if (!position_visible(p->position)) tip += "\n" + position_descriptor(p->position);
                    if (p->on_loan) tip += "\nOn loan from " + (p->loaned_from > 0 ? app.model.team_name(p->loaned_from) : std::string("an unknown club"));
                    tip += "\nPotential " + std::to_string(p->potential);
                    if (p->calc_overall >= 0) {
                        char c[64];
                        std::snprintf(c, sizeof(c), "\nCalc OVR %d (%+d)", p->calc_overall, p->calc_overall - p->overall);
                        tip += c;
                    }
                    if (p->ovr_modifier != kNoOvrModifier) {
                        char m[48];
                        std::snprintf(m, sizeof(m), "\nOVR modifier %+d", p->ovr_modifier);
                        tip += m;
                    }
                    ImGui::SetTooltip("%s", tip.c_str());
                }
            }
        }
    }
    ImGui::EndChild();
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
void list_buttons(App& app, const PlayerRow& p, bool with_status) {
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
void squad_role_section(App& app, const PlayerRow& p) {
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

void career_dev_tab(App& app, const PlayerRow& p) {
    static int delta = 1, potential = 0, weekly = 1;
    static bool no_decline = teampreset::growth_default_no_decline(app.gui_settings);  // OFF; a saved value never turns it on
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
    if (ImGui::Checkbox("Grow him weekly to his potential##dvw", &listed)) {
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

// Write the attributes that differ (and `ovr_to` into overallrating, `pos_to` into preferredposition1, when >= 0) through
// App::edit_field: the same validated path as the sliders (range check, undo steps, cached rows, kept edits, development
// plan); the overall as given, the attributes already moved. Keeps a bulk undo entry. Returns the number of fields
// written, or -1 when a write was refused (edit_field's toast says why): what this change wrote is put back then.
static int write_bulk(App& app, const Table& t, const PlayerRow& p, const overall::Attrs& before, const overall::Attrs& after,
                      int ovr_to, const std::string& label, int pos_to = -1) {
    App::BulkUndo u;
    u.playerid = p.playerid;
    u.label = label;
    int n = 0;
    bool failed = false;
    auto put = [&](const Field* f, int64_t was, int64_t v) {
        if (failed || !f || was == v) return;
        if (!app.edit_field(t, p.rec, *f, Value::of_int(v))) {
            failed = true;
            return;
        }
        u.before.push_back({f->name, was});
        ++n;
    };
    if (pos_to >= 0) put(t.field("preferredposition1"), app.db.get_int(t, p.rec, "preferredposition1", 0), pos_to);
    for (int i = 0; i < overall::kAttrCount; ++i)
        put(t.field(overall::kAttrNames[i]), before[static_cast<size_t>(i)], after[static_cast<size_t>(i)]);
    if (ovr_to >= 0) put(t.field("overallrating"), app.db.get_int(t, p.rec, "overallrating", 0), ovr_to);
    if (failed) {
        for (auto it = u.before.rbegin(); it != u.before.rend(); ++it)
            if (const Field* f = t.field(it->first)) app.edit_field(t, p.rec, *f, Value::of_int(it->second), false);
        return -1;
    }
    app.push_bulk_undo(std::move(u));
    return n;
}

// ---- the overall follows an attribute edit (2.1). App::edit sends every players attribute / preferredposition1 write
// here (sliders, number boxes, All fields, field search, the Database tab): the field is written as before
// (App::edit_field: range check, undo step, kept edits, development plan of your players), then the stored overall moves
// by overall::follow() through the same path a typed overall uses (kept edits; the attributes go into the plan, the
// overall is no plan field). The overall joins the attribute's undo step, so one Undo puts both back.
// Kill switch: turbo_output\overall_follow_off.txt (the attribute alone, as in 2.0).
bool overall_follow_off(App& app) {
    std::error_code ec;
    return std::filesystem::exists(app.bridge.root() / "turbo_output" / "overall_follow_off.txt", ec);
}

bool overall_follow_edit(App& app, const Table& t, uint64_t rec, const Field& f, const Value& v) {
    const Field* of = t.field("overallrating");
    overall::Attrs before{};
    if (t.name != "players" || f.type != FieldType::Int || v.type != FieldType::Int || !overall::follows_field(f.name) || !of ||
        of->type != FieldType::Int || !read_attrs(app, t, rec, before) || overall_follow_off(app))
        return app.edit_field(t, rec, f, v);
    const int pos0 = static_cast<int>(app.db.get_int(t, rec, "preferredposition1", 14));
    const int64_t stored = app.db.get_int(t, rec, "overallrating", 0);
    const int64_t pid = app.db.get_int(t, rec, "playerid", 0);
    if (!app.edit_field(t, rec, f, v)) return false;
    overall::Attrs after{};
    if (!read_attrs(app, t, rec, after)) return true;
    const int pos1 = static_cast<int>(app.db.get_int(t, rec, "preferredposition1", 14));
    const int want = overall::follow(pos0, before, pos1, after, static_cast<int>(stored));
    if (want == stored) return true;
    // the attribute stays written when the overall is refused (edit_field's toast says why)
    if (!app.edit_field(t, rec, *of, Value::of_int(want), false)) return true;
    auto it = app.undo_.find(pid);
    if (it != app.undo_.end() && !it->second.empty()) {
        App::UndoStep& s = it->second.back();
        if (s.table == t.name && s.rec == rec && s.field == f.name && s.also.empty()) s.also.push_back({"overallrating", Value::of_int(stored)});
    }
    char msg[200];
    std::snprintf(msg, sizeof(msg), "Overall %lld -> %d: follows the attributes for his position (%s formula; Undo puts both back)",
                  static_cast<long long>(stored), want, position_name(pos1));
    app.notify(msg);
    return true;
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

// Table of the attributes that differ between two sets (tooltips, the archetype preview, Create player): ui_archetype.h
void attribute_changes_table(const overall::Attrs& before, const overall::Attrs& after, const archetypes::Archetype* arch) {
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
            if (arch && !archetypes::in_profile(*arch, i)) ImGui::Text("%s *", attr_label(i));
            else ImGui::TextUnformatted(attr_label(i));
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
static void changes_table(const overall::Attrs& before, const overall::Attrs& after) { attribute_changes_table(before, after, nullptr); }

bool archetype_combo(const char* id, int& sel, int position, float width, const char* none_label) {
    using namespace overall;
    const std::vector<archetypes::Archetype>& all = archetypes::all();
    if (sel >= static_cast<int>(all.size())) sel = -1;
    const Group mine = group_of_position(position);
    const char* shown = sel >= 0 ? all[static_cast<size_t>(sel)].name : none_label ? none_label : "Choose a role...";
    bool picked = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, shown, ImGuiComboFlags_HeightLargest)) {
        if (none_label && ImGui::Selectable(none_label, sel < 0) && sel >= 0) {
            sel = -1;
            picked = true;
        }
        // the position's own group first, the others below
        for (int pass = 0; pass < 2; ++pass) {
            for (int g = 0; g < kGroupCount; ++g) {
                if ((pass == 0) != (g == mine)) continue;
                char hdr[96];
                std::snprintf(hdr, sizeof(hdr), "%s%s", group_name(static_cast<Group>(g)), g == mine ? " (this position)" : "");
                ImGui::SeparatorText(hdr);
                for (size_t i = 0; i < all.size(); ++i) {
                    if (all[i].group != g) continue;
                    if (ImGui::Selectable(all[i].name, sel == static_cast<int>(i)) && sel != static_cast<int>(i)) {
                        sel = static_cast<int>(i);
                        picked = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", all[i].blurb);
                }
            }
        }
        ImGui::EndCombo();
    }
    return picked;
}

int archetype_result_lines(const archetypes::TargetResult& r, int position) {
    if (!r.ok) {
        ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "Overall %d cannot be reached as %s: %s.", r.target, position_name(position), r.why.c_str());
        ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "The closest overall this position can have is %d (it reaches %d..%d).", r.closest, r.lo, r.hi);
        return r.closest;
    }
    ImGui::Text("Resulting overall (formula for %s): %d -> %d (unrounded %.2f)", position_name(position), r.overall_before, r.overall_after,
                r.raw_after);
    if (!r.outside_moved.empty()) {
        std::string names;
        for (int a : r.outside_moved) names += std::string(names.empty() ? "" : ", ") + attr_label(a);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(460.0f));
        ImGui::TextColored(ImVec4(1, 0.75f, 0.35f, 1),
                           "The role's own attributes give %d..%d here (clamped at 1..99), so %zu attributes outside the role moved too "
                           "(marked *): %s.", r.profile_lo, r.profile_hi, r.outside_moved.size(), names.c_str());
        ImGui::PopTextWrapPos();
    }
    if (r.at_limit) ImGui::TextDisabled("%d of the role's attributes sit at the 1..99 limit (the profile is clamped there).", r.at_limit);
    return 0;
}

// The Attributes tab's archetype row
static ArchetypeUiState& arch_state() {
    static ArchetypeUiState st;
    return st;
}
const ArchetypeUiState& archetype_ui_state() { return arch_state(); }

// The overall Apply writes into overallrating: the target when the target's position is (or becomes) his; for another
// position kept as it is, his own position's overall follows what its formula moved (overall::follow, the 2.1 rule;
// unchanged with the follow switch off)
static int archetype_overall_to(App& app, const ArchetypeUiState& st, bool adopt) {
    if (st.pend_position == st.pend_his_position || adopt) return st.pend.target;
    if (overall_follow_off(app)) return static_cast<int>(st.pend_stored);
    return overall::follow(st.pend_his_position, st.pend_before, st.pend_his_position, st.pend.attrs, static_cast<int>(st.pend_stored));
}

// Apply the previewed archetype: attributes (+ position, + overall) through write_bulk (validated writes, single-field
// undo steps, one bulk undo entry, kept edits / development plan), then every written field is read back.
static void archetype_apply(App& app, const Table& t, const PlayerRow& p, ArchetypeUiState& st) {
    using namespace overall;
    const archetypes::Archetype& arch = archetypes::all()[static_cast<size_t>(st.pend_arch)];
    Attrs now{};
    if (!read_attrs(app, t, p.rec, now) || now != st.pend_before ||
        app.db.get_int(t, p.rec, "preferredposition1", 14) != st.pend_his_position ||
        app.db.get_int(t, p.rec, "overallrating", 0) != st.pend_stored) {
        app.notify("Player type not applied: his attributes changed since the preview (see the changes again)", true);
        return;
    }
    const bool adopt = st.pend_position != st.pend_his_position && st.adopt_position;
    const int ovr_to = archetype_overall_to(app, st, adopt);
    char label[160];
    std::snprintf(label, sizeof(label), "archetype %s at overall %d (%s)", arch.name, st.pend.target, position_name(st.pend_position));
    const int n = write_bulk(app, t, p, now, st.pend.attrs, ovr_to, label, adopt ? st.pend_position : -1);
    if (n < 0) {
        app.notify(std::string("Player type ") + arch.name + ": nothing written (a field was refused; what was written is back)", true);
        return;
    }
    // read-back: the attributes, the position and the overall as written, and the formula gives the target
    Attrs back{};
    const bool attrs_ok = read_attrs(app, t, p.rec, back) && back == st.pend.attrs;
    const int64_t ovr_back = app.db.get_int(t, p.rec, "overallrating", -1);
    const int pos_back = static_cast<int>(app.db.get_int(t, p.rec, "preferredposition1", -1));
    const bool pos_ok = !adopt || pos_back == st.pend_position;
    const bool formula_ok = compute_overall(st.pend_position, back) == st.pend.target;
    char msg[320];
    if (!attrs_ok || ovr_back != ovr_to || !pos_ok || !formula_ok) {
        std::snprintf(msg, sizeof(msg), "Player type %s: the read-back differs (attributes %s, overall %lld for %d, position %s): Undo puts it back",
                      arch.name, attrs_ok ? "ok" : "differ", static_cast<long long>(ovr_back), ovr_to, pos_ok ? "ok" : "differs");
        app.notify(msg, true);
    } else {
        const std::string now_pos = adopt ? std::string(", now a ") + position_name(st.pend_position) : std::string();
        std::snprintf(msg, sizeof(msg), "Player type %s at overall %d: %d fields written, overall %lld -> %d%s (read back; Undo puts them back)",
                      arch.name, st.pend.target, n, static_cast<long long>(st.pend_stored), ovr_to, now_pos.c_str());
        app.notify(msg);
    }
    st.applied_playerid = p.playerid;
    st.applied_label = label;
    st.applied_note = msg;
}

// Overall +1 / -1 and the archetype row (role, position, Target overall, preview, Apply, Undo) above the sliders
// shared = nullptr: the Classic editor's bar, unchanged. Non-null (2.22: the "Player type & tier" card of Edit a player): the Player type and the
// Wanted overall are the card's own (the Populate controls above), so the bar draws neither box and reshapes with those two
static void archetype_bar_impl(App& app, const Table& t, const PlayerRow& p, const PopulateUiState* shared) {
    using namespace overall;
    ArchetypeUiState& st = arch_state();

    Attrs cur{};
    ImGui::AlignTextToFramePadding();
    if (!read_attrs(app, t, p.rec, cur)) {
        ImGui::TextDisabled("Player types and Overall +1 / -1 need all 34 attribute fields; this database lacks some.");
        return;
    }
    const int pos = p.position;
    const Field* of = t.field("overallrating");
    const int64_t stored = app.db.get_int(t, p.rec, "overallrating", 0);
    ImGui::Text("Overall %lld", static_cast<long long>(stored));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 works the overall out from the attributes (the player card, its own growth): a bare overall does not last.\n"
                          "So Overall +1 / -1 and an overall typed into the Overall box (Profile, All fields) move the attributes that\n"
                          "weigh most for his position until the formula gives it; Undo / Undo last big change put them back.\n"
                          "The other way round too: an attribute (or his position) you edit moves the stored overall by what the\n"
                          "formula moves, so the game shows it at once; one Undo puts both back.\n"
                          "Calc OVR (the formula line) is what the attributes give; the stored overall is what the game shows.\n%s\n%s",
                          kOverallModifierLine,
                          overall_follow_off(app) ? "OFF now: turbo_output\\overall_follow_off.txt exists."
                                                  : "Off switch: create turbo_output\\overall_follow_off.txt.");
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
            if (n < 0) app.notify(std::string("Overall ") + (delta > 0 ? "+1" : "-1") + ": nothing written (a field was refused)", true);
            else app.notify(std::string("Overall ") + (delta > 0 ? "+1" : "-1") + ": " + std::to_string(std::max(0, n - 1)) + " attributes changed");
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
    char ulbl[64];
    std::snprintf(ulbl, sizeof(ulbl), "Undo last big change (%zu)###bulkundo", mine);
    if (ImGui::Button(ulbl)) undo_bulk(app, t, p);
    if (!mine) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Puts back everything the last Overall +1/-1 click or archetype changed (the Undo button above is for single fields)");

    // ---- archetype with a target overall (2.3): role, position (his own unless picked), Target overall (his overall unless
    // typed); the preview lists every attribute that changes and the resulting overall, Apply writes them, Undo puts them back
    const std::vector<archetypes::Archetype>& all = archetypes::all();
    if (st.playerid != p.playerid) {
        st.playerid = p.playerid;
        st.position_picked = st.target_typed = false;
        st.adopt_position = true;
    }
    if (st.arch >= static_cast<int>(all.size())) st.arch = -1;
    if (!st.position_picked || st.position < 0) st.position = pos;
    const int formula_at = compute_overall(st.position, cur);
    if (!st.target_typed) st.target = st.position == pos ? archetypes::default_target(static_cast<int>(stored), formula_at) : formula_at;
    if (shared) {
        st.arch = shared->arch;
        if (st.arch >= static_cast<int>(all.size())) st.arch = -1;
        st.target = std::max(1, std::min(99, shared->target));
        st.target_typed = true;
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Player type");
        ImGui::SameLine();
        archetype_combo("##arch", st.arch, st.position, S(220.0f));
        ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Position");
    ImGui::SameLine();
    int pick = st.position;
    if (position_combo("##archpos", pick, S(90.0f))) {
        st.position = pick;
        st.position_picked = pick != pos;  // his own position again: it follows him again
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The position whose overall formula the target is for (his own: %s)", position_name(pos));
    // second row (the pane is often narrow): the target and the preview
    if (shared) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Wanted overall %d", st.target);
        ImGui::SameLine();
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Wanted overall");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(70.0f));
        int tv = st.target;
        if (ImGui::InputInt("##archtarget", &tv, 0)) {
            st.target = std::max(1, std::min(99, tv));
            st.target_typed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("1..99. Starts at his overall (stored %lld, formula for %s %d) and keeps a typed value until another player is selected.",
                              static_cast<long long>(stored), position_name(st.position), formula_at);
        ImGui::SameLine();
    }
    const bool can_preview = st.arch >= 0;
    if (!can_preview) ImGui::BeginDisabled();
    if (ImGui::Button("See changes...###archpreview") && can_preview) {
        st.pend_playerid = p.playerid;
        st.pend_arch = st.arch;
        st.pend_before = cur;
        st.pend_position = st.position;
        st.pend_his_position = pos;
        st.pend_stored = stored;
        archetypes::apply_target(all[static_cast<size_t>(st.arch)], st.position, cur, st.target, st.pend);
        ImGui::OpenPopup("Apply archetype");
    }
    if (!can_preview) ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Reshapes the attributes to the role so that the overall (the %s formula) becomes the target.", position_name(st.position));
    ImGui::PopTextWrapPos();
    // the last Apply: one click puts it back while it is his newest bulk change
    if (st.applied_playerid == p.playerid && !st.applied_label.empty()) {
        const App::BulkUndo* last = nullptr;
        for (const auto& u : app.bulk_undo)
            if (u.playerid == p.playerid) last = &u;
        if (last && last->label == st.applied_label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Applied: %s.", st.applied_label.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Undo last big change###archundo")) undo_bulk(app, t, p);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st.applied_note.c_str());
        }
    }

    if (ImGui::BeginPopupModal("Apply archetype", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (st.pend_playerid != p.playerid || st.pend_arch < 0 || st.pend_arch >= static_cast<int>(all.size())) {
            ImGui::TextDisabled("Another player is selected.");
        } else {
            const archetypes::Archetype& arch = all[static_cast<size_t>(st.pend_arch)];
            const archetypes::TargetResult& r = st.pend;
            ImGui::Text("%s as %s, target overall %d (%s)", p.name.c_str(), arch.name, r.target, position_name(st.pend_position));
            ImGui::TextDisabled("%s", arch.blurb);
            if (arch.group != group_of_position(st.pend_position))
                ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "This role is from another position group (%s); the target is reached by the %s formula.",
                                   group_name(arch.group), position_name(st.pend_position));
            // both overalls: the stored one the game shows and the formula's (the target is the formula's result)
            ImGui::Text("Now: stored overall %lld, formula for %s %d", static_cast<long long>(st.pend_stored), position_name(st.pend_position),
                        r.overall_before);
            if (st.pend_position == st.pend_his_position && st.pend_stored != r.overall_before)
                ImGui::TextDisabled("(they differ: the target is what the formula gives; Apply writes it into the stored overall too)");
            if (st.pend_position != st.pend_his_position) {
                char cb[128];
                std::snprintf(cb, sizeof(cb), "Make %s his main position (now %s)##archadopt", position_name(st.pend_position),
                              position_name(st.pend_his_position));
                ImGui::Checkbox(cb, &st.adopt_position);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("On: preferredposition1 becomes %s and the stored overall the target.\n"
                                      "Off: he stays a %s; his stored overall follows what the %s formula moved.",
                                      position_name(st.pend_position), position_name(st.pend_his_position), position_name(st.pend_his_position));
            }
            const bool adopt = st.pend_position != st.pend_his_position && st.adopt_position;
            const int closest = archetype_result_lines(r, st.pend_position);
            int ovr_to = static_cast<int>(st.pend_stored);
            if (closest > 0) {
                char ub[48];
                std::snprintf(ub, sizeof(ub), "Use %d##archclosest", closest);
                if (ImGui::Button(ub)) {
                    st.target = closest;
                    st.target_typed = true;
                    archetypes::apply_target(arch, st.pend_position, st.pend_before, closest, st.pend);
                }
            } else {
                ovr_to = archetype_overall_to(app, st, adopt);
                ImGui::Text("Stored overall after Apply: %lld -> %d%s", static_cast<long long>(st.pend_stored), ovr_to,
                            ovr_to == r.target ? "" : " (his own position's formula moved by that)");
                const int64_t pot = app.db.get_int(t, p.rec, "potential", 0);
                if (pot > 0 && pot < r.target)
                    ImGui::TextDisabled("His potential is %lld, below the new overall (not changed here; the game's growth aims at it).",
                                        static_cast<long long>(pot));
                ImGui::BeginChild("##archchg", ImVec2(S(440.0f), S(360.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY);
                if (r.attrs == st.pend_before) ImGui::TextDisabled("No attribute changes.");
                else attribute_changes_table(st.pend_before, r.attrs, &arch);
                ImGui::EndChild();
            }
            const bool can = st.pend.ok && (st.pend.attrs != st.pend_before || ovr_to != st.pend_stored || adopt);
            if (!can) ImGui::BeginDisabled();
            if (ImGui::Button("Apply##arch")) {
                archetype_apply(app, t, p, st);
                ImGui::CloseCurrentPopup();
            }
            if (!can) ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel##arch")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// the card's bar: the Player type and the Wanted overall come from the Populate controls of the same card
static void archetype_bar_card(App& app, const Table& t, const PlayerRow& p) { archetype_bar_impl(app, t, p, &populate_ui_state()); }

// 2.20 IA (ui_player_profile.h): the Profile page draws the full player-type editor through this pointer
static const bool s_ability_hook = (g_ability_archetype_bar = archetype_bar_card, true);

}  // namespace turbo
