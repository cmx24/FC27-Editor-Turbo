// FC 27 LE Turbo GUI - the pages of the 2.16 navigation (plan TURBO_GUI_REVAMP_PLAN.md 4.3, step 4) whose content still lives where it was:
// Create a player, Commentary names and Team sheet & bench. (2.20: the Faces & hair and Body, tattoos & gear signposts are gone: the face chooser,
// the 3D hair and beard, the tattoos, the gear and the miniface stay on Edit a player's Appearance and Miniface tabs, and the Appearance Editor
// is its own area.) Each starts as a signpost: one question, one
// line saying where the feature is today, and a button that goes there, so that nothing disappears while the pages move (steps 5, 6 and 8
// replace each signpost with the real page). No picture, no file, no state: they cost nothing per frame.
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "core/callnames.h"
#include "imgui.h"
#include "table_registry.h"
#include "table_sort.h"
#include "ui_callname_pick.h"
#include "ui_nav_pages.h"
#include "ui_shell.h"
#include "ui_team_filter.h"
#include "ui_theme.h"

namespace turbo {

namespace {

using shell::Page;

void signpost(App& app, const char* question, const char* where, Page target, const char* button) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(question);
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(where);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const std::string label = std::string(button) + "###navgo";
    if (ImGui::Button(label.c_str())) app.go(shell::area_of(target), target);
}

void page_create_player(App& app) {
    signpost(app, "Make a new player", "Today this is the Create player button above the tabs of Edit a player. It also copies a player and builds one from a CMTracker file.",
             Page::PlayerEdit, "Open Edit a player");
}
// ---------------------------------------------------------------- Players > Commentary names (2.22 G-ia: a real page, no signpost)
// Left: every player (search, club, "No callname yet", sortable Player / Club / Callname now). Right: the callname editor of the picked player, the
// same one as on Edit a player > Profile (list, Preview, Apply, Undo). The pick is App::sel_player, so Edit a player shows the same player.
struct CnRow {
    int64_t pid = 0;
    std::string name, club, now;
    bool none = false;  // no applied callname and the game has none for him
};
struct CnPage {
    char search[64] = "";
    char club_search[64] = "";
    int64_t club = 0;
    bool no_callname = false;
    std::vector<CnRow> rows;
    size_t all = 0;
    std::string key;
    uint64_t version = ~uint64_t(0);
    bool rebuilt = false;
    bool ready = false;
    CommentaryNamesState pub;
};
CnPage& cn_page() {
    static CnPage g;
    return g;
}

std::string lower_ascii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// What he is called in matches now: the applied callname, else what the game says (the words of the editor's own status line)
std::string callname_now(App& app, const PlayerRow& p, bool& none) {
    none = false;
    if (const voice::Entry* ve = app.voice_store.find(p.playerid)) return callpick::entry_name(*ve) + " (applied)";
    const CallnameInfo info = app.callnames.resolve(p, app.db);
    const CatalogBank& bank = app.callnames.bank;
    if (info.own && (!bank.loaded() || bank.has_spoken_own(p.playerid))) return "his own recording";
    if (info.source == CallnameSource::None) {
        none = true;
        return "no callname";
    }
    if (info.nameid > 0) {
        auto it = app.model.names_by_id().find(info.nameid);
        if (it != app.model.names_by_id().end() && !it->second.empty()) return "'" + it->second + "'";
    }
    return callname_source_name(info.source);
}

void cn_rebuild(App& app, CnPage& g) {
    // the callname index is built by callname_editor once a player is picked; the column and the filter want it before that
    Callnames& cn = app.callnames;
    if (cn.refreshed && (!cn.index.built || cn.index.model_version != app.model.version()))
        cn.build_index(app.db, app.model, app.model.names_by_id());
    g.ready = cn.refreshed && cn.index.built;
    const std::string q = lower_ascii(g.search);
    // the applied swaps change which rows are "without a callname": part of the key
    const std::string key = q + "|" + std::to_string(g.club) + (g.no_callname ? "|n" : "|-") + (g.ready ? "|r|" : "|w|") +
                            std::to_string(app.voice_store.entries.size()) + "|" + cn.lang;
    if (key == g.key && g.version == app.model.version()) return;
    g.key = key;
    g.version = app.model.version();
    g.rebuilt = true;
    g.rows.clear();
    g.all = app.model.players().size();
    std::unordered_set<int64_t> in_club;
    if (g.club > 0)
        for (const LinkRow& l : app.model.links_of_team(g.club)) in_club.insert(l.playerid);
    for (const PlayerRow& p : app.model.players()) {
        if (g.club > 0 && !in_club.count(p.playerid) && p.club != g.club) continue;
        if (!q.empty() && lower_ascii(p.name).find(q) == std::string::npos && lower_ascii(p.club_name).find(q) == std::string::npos) continue;
        CnRow r;
        r.pid = p.playerid;
        r.name = p.name;
        r.club = p.club_name;
        if (g.ready) r.now = callname_now(app, p, r.none);
        if (g.no_callname && g.ready && !r.none) continue;
        g.rows.push_back(std::move(r));
    }
}

void cn_sort(CnPage& g, ImGuiTableSortSpecs* specs) {
    if (specs->SpecsCount > 0) {
        table_sort(g.rows, [](const CnRow& a, const CnRow& b, int col) {
            switch (col) {
                case 1: return cmp_text(a.club, b.club);
                case 2: return cmp_text(a.now, b.now);
                default: return cmp_text(a.name, b.name);
            }
        });
    } else {
        std::stable_sort(g.rows.begin(), g.rows.end(), [](const CnRow& a, const CnRow& b) { return cmp_text(a.name, b.name) < 0; });
    }
    specs->SpecsDirty = false;
}

void cn_list(App& app) {
    CnPage& g = cn_page();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##cnsearch", "Search by name or club", g.search, sizeof(g.search));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Club");
    ImGui::SameLine();
    team_filter_combo(app, "##cnclub", g.club, g.club_search, sizeof(g.club_search), std::max(S(120.0f), ImGui::GetContentRegionAvail().x));
    cn_rebuild(app, g);
    {
        const bool lit = g.no_callname;
        if (!g.ready) ImGui::BeginDisabled();
        if (lit) ImGui::PushStyleColor(ImGuiCol_Button, theme::accent_fill());
        if (ImGui::SmallButton("No callname yet###cn_none")) g.no_callname = !g.no_callname;
        if (lit) ImGui::PopStyleColor();
        if (!g.ready) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", g.ready ? "Only the players the game has no callname for and who have none applied" : "Waiting for the game's callname list");
    }
    ImGui::SameLine();
    theme::muted_text("%zu of %zu players", g.rows.size(), g.all);

    g.pub.now_of_selected.clear();
    if (ImGui::BeginTable("##players", 3, tbl::kData | ImGuiTableFlags_ScrollY | table_sort_flags(), ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 1.2f);
        ImGui::TableSetupColumn("Club", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Callname now", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        // sorted when a header was clicked or the list was rebuilt (never every frame: the list is the whole game)
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty || g.rebuilt) {
                cn_sort(g, specs);
                g.rebuilt = false;
            }
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g.rows.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const CnRow& r = g.rows[static_cast<size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char lbl[200];
                std::snprintf(lbl, sizeof(lbl), "%s###cn%lld", r.name.c_str(), static_cast<long long>(r.pid));
                if (ImGui::Selectable(lbl, app.sel_player == r.pid, ImGuiSelectableFlags_SpanAllColumns)) app.sel_player = r.pid;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Player ID %lld", static_cast<long long>(r.pid));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.club.c_str());
                ImGui::TableNextColumn();
                // the picked player's callname is read now, so Apply and Undo show in the column at once (the filter follows the next rebuild)
                if (r.pid == app.sel_player && g.ready) {
                    bool none = false;
                    const PlayerRow* p = app.model.player(r.pid);
                    const std::string now = p ? callname_now(app, *p, none) : r.now;
                    ImGui::TextUnformatted(now.c_str());
                    g.pub.now_of_selected = now;
                } else {
                    ImGui::TextUnformatted(r.now.c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    g.pub.first_listed = g.rows.empty() ? 0 : g.rows.front().pid;
    g.pub.now_of_first = g.rows.empty() ? std::string() : g.rows.front().now;
    g.pub.listed = g.rows.size();
    g.pub.all = g.all;
    g.pub.callnames_ready = g.ready;
}

void cn_editor(App& app) {
    CnPage& g = cn_page();
    g.pub.editor_shown = false;
    g.pub.editor_playerid = 0;
    const Table* t = app.db.table("players");
    const PlayerRow* p = app.model.player(app.sel_player);
    if (!t || !p) {
        theme::muted_text("Pick a player on the left to choose what the commentator says for him.");
        return;
    }
    if (!app.db.table_alive(*t, p->rec)) {
        theme::err_text("The game data changed (another career was loaded?). Use Refresh at the top.");
        return;
    }
    ImGui::TextUnformatted(p->name.c_str());
    ImGui::SameLine();
    theme::muted_text("%s%s", p->club_name.empty() ? "no club" : p->club_name.c_str(), p->on_loan ? " (on loan)" : "");
    ImGui::Separator();
    callname_editor(app, *t, *p);
    g.pub.editor_shown = true;
    g.pub.editor_playerid = p->playerid;
}

void page_commentary_names(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    // [list | editor]: the list is as wide as the Edit a player list (up to 560) and its divider can be dragged
    const float list_w = std::min(S(560.0f), std::max(S(380.0f), ImGui::GetContentRegionAvail().x * 0.42f));
    ImGui::BeginChild("##select", ImVec2(list_w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    cn_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##editor", ImVec2(0, 0), ImGuiChildFlags_Borders);
    cn_editor(app);
    ImGui::EndChild();
}
void page_team_sheet(App& app) {
    const bool adv = app.prefs.advanced_mode;
    signpost(app, "Keep my bench? Who takes set pieces?",
             adv ? "Today: set pieces are saved and restored with Tactic presets. The bench option is under Advanced > Tactics (experimental) > My team."
                 : "Today: set pieces are saved and restored with Tactic presets. The bench option is under Advanced > Tactics (experimental) > My team; "
                   "Settings > Show advanced tools shows that section.",
             adv ? Page::AdvTactics : Page::Settings, adv ? "Open Advanced > Tactics" : "Open Settings");
}

// 2.20: the pages that only exist in the new navigation answer, with "Use the old pages" on, with a pointer to where the old panels keep them
void page_team_players(App& app) {
    signpost(app, "How is my squad doing?", "In the old pages the career side of a club (contract, form, growth, squad role) is on Players > My squad.", Page::Squad,
             "Open My squad");
}
void page_team_transfers(App& app) {
    signpost(app, "Move, loan or sign a player", "In the old pages the transfers, loans and signings are on Career > Transfers & money.", Page::Transfers, "Open Transfers & money");
}
void page_bulk_edit(App& app) {
    const bool adv = app.prefs.advanced_mode;
    signpost(app, "Change a field for many players", adv ? "In the old pages the bulk edit is under Advanced > Bulk edit." : "In the old pages the bulk edit is under Advanced > Bulk edit; Settings > Show advanced tools shows it.",
             adv ? Page::AdvBulkEdit : Page::Settings, adv ? "Open Advanced > Bulk edit" : "Open Settings");
}

}  // namespace

const CommentaryNamesState& commentary_names_state() { return cn_page().pub; }

TURBO_REGISTER_PAGE(shell::Page::TeamPlayers, page_team_players);
TURBO_REGISTER_PAGE(shell::Page::TeamTransfers, page_team_transfers);
TURBO_REGISTER_PAGE(shell::Page::BulkEdit, page_bulk_edit);
TURBO_REGISTER_PAGE(shell::Page::CreatePlayer, page_create_player);
TURBO_REGISTER_PAGE(shell::Page::CommentaryNames, page_commentary_names);
TURBO_REGISTER_PAGE(shell::Page::TeamSheet, page_team_sheet);

}  // namespace turbo
