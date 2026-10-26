// FC 27 LE Turbo GUI - shell contract: areas, pages, legacy tab shim, page registry, page frame (ui_shell.h).
#include "ui_shell.h"

#include <map>

#include "app.h"
#include "core/devops.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui_team_squad_page.h"
#include "ui_split.h"
#include "ui_theme.h"

namespace turbo::shell {

namespace {

struct PageInfo {
    Page page;
    Area area;
    const char* label;
    int legacy_tab;  // the TopTab the panel lived in before the revamp (-1: none)
};

// Order = order in kPages; the page strip of an area lists its pages in this order. 2.16 step 4 (plan 4.3): the new map; a page with no
// legacy tab (-1) is new. Scouting, Budget and Bans are sections of the Transfers & money page (the old Turbo Tools panel); Exports is
// Advanced > Data export; Career tools > Maintenance is Advanced > Maintenance (Game editors stay on Transfers & money).
const PageInfo kPages[] = {
    {Page::Home, Area::Home, "Home", -1},  // 3.0: search, squad table, presets strip, recent changes, the area tiles (ui_home.cpp)

    {Page::PlayerEdit, Area::Squad, "Player", 0},
    {Page::CreatePlayer, Area::Squad, "Create & clone", -1},  // 2.20: create / clone / import / export / delete (ui_player_creation.cpp)
    {Page::BulkEdit, Area::Squad, "Many players at once", -1},  // 2.20: any field of many players at once (ui_bulk_edit.cpp); Lab > Bulk edit stays until the capability check passes
    {Page::Staff, Area::Squad, "Staff", 2},
    {Page::CommentaryNames, Area::Squad, "Callnames", -1},

    {Page::LooksStudio, Area::Looks, "Appearance Editor", -1},  // 2.17 ui_looks_studio.cpp (the Looks studio); 2.20: renamed, and the only page of its area

    {Page::Kits, Area::Club, "Kits", 1},
    {Page::Identity, Area::Club, "Name, colours & crest", 1},
    {Page::TeamPlayers, Area::Club, "Squad", -1},        // 2.20: career side of a club: contract, form, growth, squad role, team preset (ui_team_players.cpp)
    {Page::TeamTransfers, Area::Club, "Transfers", -1},  // 2.20: moves, loans, signings, contract extensions (ui_team_transfers.cpp)
    {Page::StadiumExtras, Area::Club, "Stadium & extras", 5},

    {Page::TacticPresets, Area::Tactics, "Presets", 1},
    {Page::TeamSheet, Area::Tactics, "Team sheet & bench", -1},
    {Page::GameplayPresets, Area::Tactics, "Game sliders", 7},

    {Page::Standings, Area::Career, "Standings", 3},
    {Page::Transfers, Area::Career, "Money & transfers", 5},
    {Page::JobOffer, Area::Career, "Manager jobs", 1},
    {Page::CMTracker, Area::Career, "Season pack", 5},
    {Page::ChangesUndo, Area::Career, "Changes & undo", 5},

    {Page::Settings, Area::Settings, "Settings", 6},

    {Page::AdvDatabase, Area::Advanced, "Database", 4},
    {Page::AdvRawRows, Area::Advanced, "Row editor", 0},
    {Page::AdvBulkEdit, Area::Advanced, "Bulk edit", 5},
    {Page::AdvMaintenance, Area::Advanced, "Maintenance", 5},
    {Page::AdvTactics, Area::Advanced, "Tactics (experimental)", 1},
    {Page::AdvMatchSetup, Area::Advanced, "Match setup", 3},
    {Page::AdvGameplayTuning, Area::Advanced, "Gameplay tuning", 3},
    {Page::AdvExport, Area::Advanced, "Data export", 5},
    // health, hooks, switches and the log: until its own page is registered the legacy Status panel stands in (draw_legacy_page)
    {Page::AdvDiagnostics, Area::Advanced, "Diagnostics & logs", 6},
    // the old Kits body with every developer control (page-kits): Lab only, it lived in the Teams tab
    {Page::AdvKitDiagnostics, Area::Advanced, "Kit diagnostics", 1},
    // 3.0: Career > My squad (draw_career_my_squad) repeated Club > Squad: kept, in the Lab, last
    {Page::Squad, Area::Advanced, "My squad (old page)", 1},
};

const PageInfo* info_of(Page p) {
    for (const PageInfo& i : kPages)
        if (i.page == p) return &i;
    return nullptr;
}

// ---- the 4.0 tree (brief 4.0-gui-pages S3): the second table, used while tree40() is on. Three fields per row on purpose (the NAVCAP scan reads
// the four-field rows of kPages above; this table is a preview of the target, not yet the default). A page missing here is not in the 4.0 menus.
// NAVCAP (tools/nav_capability_check.py) reads kPages only and cannot see this table: the list of controls the 4.0 tree has NO home for is
// kept by hand in turbo_dev/PITWALL/board/gui-s3.md ("UNREACHABLE IN THE 4.0 TREE") and in the S3 final report. Update it with every row change.
struct Row40 {
    Page page;
    Area area;
    const char* label;
};
const Row40 kTree40[] = {
    {Page::Home, Area::Home, "Home"},

    // Players: the list + the card (tabs Profile | Positions, attributes & playstyles | Appearance | Player creation, ui_pages_216.cpp) and Players / Managers
    {Page::PlayerEdit, Area::Squad, "Player"},
    {Page::BulkEdit, Area::Squad, "Bulk edit"},

    // Team (the Club area, renamed): Squad stays Page::Squad (area Club in this tree) and draws S4's draw_team_squad_page (4.0 integration, S3 merge)
    {Page::Squad, Area::Club, "Squad"},
    {Page::TeamPlayers, Area::Club, "Players"},
    {Page::TeamTransfers, Area::Club, "Transfers"},
    {Page::Kits, Area::Club, "Kits"},
    {Page::Identity, Area::Club, "Club & crest"},

    {Page::TacticPresets, Area::Tactics, "Presets"},
    {Page::TeamSheet, Area::Tactics, "Team sheet & bench"},
    {Page::GameplayPresets, Area::Tactics, "Game sliders"},

    {Page::Standings, Area::Career, "Standings"},
    {Page::Transfers, Area::Career, "Money"},
    {Page::JobOffer, Area::Career, "Manager jobs"},
    {Page::CMTracker, Area::Career, "Season pack"},
    {Page::ChangesUndo, Area::Career, "Changes & undo"},

    {Page::Settings, Area::Settings, "Settings"},

    {Page::AdvDatabase, Area::Advanced, "Database"},
    {Page::AdvRawRows, Area::Advanced, "Row editor"},
    {Page::AdvMaintenance, Area::Advanced, "Maintenance"},
    {Page::AdvMatchSetup, Area::Advanced, "Match setup"},
    {Page::AdvGameplayTuning, Area::Advanced, "Gameplay tuning"},
    {Page::AdvExport, Area::Advanced, "Data export"},
    {Page::AdvDiagnostics, Area::Advanced, "Diagnostics & logs"},
    {Page::AdvKitDiagnostics, Area::Advanced, "Kit diagnostics"},  // q5: under Diagnostics & logs

    // 4.0 S3 fix round 2: the controls with no named home in the 4.0 tree stay reachable from the Lab (no silent regression: the Pit wall default
    // for unreachable rows 5, 7, 8, see the board gui-s3.md). Last in the strip, so the Lab still opens on Database. Each is the same page function
    // the old tree registers; the club_stadium_section is not moved (S9 owns Club & crest) and "More for your club" (draw_club_tools) lives here.
    {Page::AdvBulkEdit, Area::Advanced, "Bulk edit"},                     // the Lua tool page_adv_bulk (ui_tools.cpp:756), not Players > Bulk edit
    {Page::AdvTactics, Area::Advanced, "Tactics (experimental)"},         // the live tactics controls + the offline slider file; also the target of the "Open Lab > Tactics" button on Game sliders
    {Page::StadiumExtras, Area::Advanced, "Stadium & extras"},            // club_stadium_section + "More for your club" (ui_club_page.cpp:109-119)
};

const Row40* row40_of(Page p) {
    for (const Row40& r : kTree40)
        if (r.page == p) return &r;
    return nullptr;
}

std::map<int, PageFn>& registry() {
    static std::map<int, PageFn> r;
    return r;
}

std::map<int, PageFn>& new_registry() {
    static std::map<int, PageFn> r;
    return r;
}

}  // namespace

bool& tree40() {
    static bool on = false;
    return on;
}

// 4.0 integration (S3 merge, ask (4)): every page that draws page::Frame's bar, in both trees. The list is the set of pages the native cases
// "Shell 4.0: every page of the 4.0 tree ..." and "Shell 4.0: flag off, page_has_frame ..." found drawing ###pg_back (fail-old log
// evidence/2026-10-26/RH/s3_old_1.log). Add a line per converted page.
bool page_has_frame(Page p) {
    switch (p) {
        case Page::AdvExport:        // ui_diagnostics_page.cpp, "data_export"
        case Page::CreatePlayer:     // ui_player_creation.cpp (S7)
        case Page::BulkEdit:         // ui_bulk_edit.cpp (S7)
        case Page::TeamPlayers:      // ui_team_players.cpp, "team_players" (S8)
        case Page::TeamTransfers:    // ui_team_transfers.cpp, "team_transfers" (S8)
        case Page::Kits:             // ui_kits_page.cpp, "kits" (S9)
        case Page::Identity:         // ui_kits_page.cpp, "club_crest" (S9)
        case Page::TacticPresets:    // ui_tactics_page.cpp, "tactics_presets" (S10)
        case Page::TeamSheet:        // ui_tactics_page.cpp, "team_sheet" (S10)
        case Page::GameplayPresets:  // ui_tactics_page.cpp, "game_sliders" (S10)
            return true;
        case Page::Squad:  // the 4.0 tree draws S4's draw_team_squad_page ("team_squad"); the old tree's Lab "My squad (old page)" has no frame
            return tree40();
        default:
            return false;
    }
}

const char* area_label(Area a) {
    if (tree40()) {
        if (a == Area::Club) return "Team";
        if (a == Area::Looks) return "";  // no Looks area in the 4.0 tree (Appearance is a card tab)
    }
    switch (a) {
        case Area::Home: return "Home";
        case Area::Squad: return "Players";
        case Area::Looks: return "Looks";
        case Area::Club: return "Club";
        case Area::Tactics: return "Tactics";
        case Area::Career: return "Career";
        case Area::Settings: return "Settings";
        case Area::Advanced: return "Lab";
        default: return "";
    }
}

const char* page_label(Page p) {
    if (tree40())
        if (const Row40* r = row40_of(p)) return r->label;
    const PageInfo* i = info_of(p);
    return i ? i->label : "";
}

Area area_of(Page p) {
    if (tree40()) {
        const Row40* r = row40_of(p);
        return r ? r->area : Area::Count;
    }
    const PageInfo* i = info_of(p);
    return i ? i->area : Area::Count;
}

const std::vector<Page>& pages_of(Area a) {
    static const std::vector<std::vector<Page>> strips40 = [] {
        std::vector<std::vector<Page>> s(kAreaCount);
        for (const Row40& r : kTree40) s[static_cast<int>(r.area)].push_back(r.page);
        return s;
    }();
    static const std::vector<std::vector<Page>> strips = [] {
        std::vector<std::vector<Page>> s(kAreaCount);
        for (const PageInfo& i : kPages) s[static_cast<int>(i.area)].push_back(i.page);
        return s;
    }();
    static const std::vector<Page> none;
    const int k = static_cast<int>(a);
    return (k >= 0 && k < kAreaCount) ? (tree40() ? strips40 : strips)[static_cast<size_t>(k)] : none;
}

Page default_page(Area a) {
    const auto& v = pages_of(a);
    return v.empty() ? Page::None : v.front();
}

int& player_edit_tab() {
    static int tab = 0;
    return tab;
}

Place& back_place() {
    static Place p;
    return p;
}

Place place_of(Page p) {
    Place pl;
    pl.area = area_of(p);
    pl.page = pl.area == Area::Count ? Page::None : p;
    return pl;
}

// ---- 4.0 S0a: names -> place, and the overlay's half of the dev-service open_page op (ui_shell.h)
namespace {
bool same_label(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return i == a.size() && b[i] == 0;
}

nlohmann::json place_json(const Place& p) {
    if (!p.valid()) return nullptr;
    return nlohmann::json{{"area", area_label(p.area)}, {"page", page_label(p.page)}};
}

// the request applied on the previous frame, answered at the start of this one (overlay thread only)
uint64_t g_dev_page_ticket = 0;
Place g_dev_page_target;
bool g_dev_page_drawn = false;  // Turbo was shown in the frame that applied it (the pump runs before that frame's draw_pages)
}  // namespace

bool find_place(const std::string& area, const std::string& page, Place& out, std::string& error) {
    out = Place{};
    for (int ai = 0; ai < kAreaCount; ++ai) {
        const Area a = static_cast<Area>(ai);
        if (!same_label(area, area_label(a))) continue;
        std::string names;
        for (Page p : pages_of(a)) {
            if (same_label(page, page_label(p))) {
                out = Place{a, p};
                return true;
            }
            names += names.empty() ? "" : ", ";
            names += page_label(p);
        }
        error = "unknown page '" + page + "' in " + area_label(a) + " (its pages: " + names + ")";
        return false;
    }
    std::string names;
    for (int ai = 0; ai < kAreaCount; ++ai) {
        names += ai ? ", " : "";
        names += area_label(static_cast<Area>(ai));
    }
    error = "unknown area '" + area + "' (areas: " + names + ")";
    return false;
}

void dev_page_pump(App& app) {
    using nlohmann::json;
    if (g_dev_page_ticket != 0) {
        // the go() of the previous frame was taken by its draw_pages() when Turbo was shown; hidden, it waits in App::go_to_ until
        // Turbo is shown. Judged on the frame that ran: shown this frame but hidden then = not drawn yet = pending.
        json a = json::object();
        a["requested"] = place_json(g_dev_page_target);
        a["overlay_visible"] = g_dev_page_drawn;
        if (g_dev_page_drawn) {
            const Place shown = app.place();
            a["shown"] = place_json(shown);
            a["pending"] = false;
            a["ok"] = shown == g_dev_page_target;
            if (!(shown == g_dev_page_target))
                a["error"] = std::string("the overlay shows ") + area_label(shown.area) + " > " + page_label(shown.page) + " instead";
        } else {
            a["shown"] = nullptr;
            a["pending"] = true;  // opens when the overlay is shown (the op never shows it)
            a["ok"] = true;
        }
        turbo::dev_page_answer(g_dev_page_ticket, std::move(a));
        g_dev_page_ticket = 0;
        g_dev_page_target = Place{};
        g_dev_page_drawn = false;
    }
    turbo::DevPageRequest rq;
    if (!turbo::dev_page_take(rq)) return;
    Place target;
    std::string error;
    json a = json::object();
    a["requested"] = json{{"area", rq.area}, {"page", rq.page}};
    a["overlay_visible"] = app.visible;
    a["shown"] = nullptr;
    a["ok"] = false;
    if (!find_place(rq.area, rq.page, target, error)) {
        a["error"] = error;
        turbo::dev_page_answer(rq.ticket, std::move(a));
        return;
    }
    // App::go refuses a page hidden in this view without a word: say so here
    if (!page_visible(target.page, app.prefs.advanced_mode)) {
        a["requested"] = place_json(target);
        a["error"] = std::string(area_label(target.area)) + " > " + page_label(target.page) +
                     " is hidden in the Simple view: turn on the Advanced view (Settings) to open it";
        turbo::dev_page_answer(rq.ticket, std::move(a));
        return;
    }
    app.go(target.area, target.page);
    g_dev_page_ticket = rq.ticket;
    g_dev_page_target = target;
    g_dev_page_drawn = app.visible;
}

bool area_visible(Area a, bool advanced) {
    if (a == Area::Count) return false;
    if (a == Area::Looks && tree40()) return false;
    return a != Area::Advanced || advanced;
}

bool page_visible(Page p, bool advanced) {
    const Area a = area_of(p);
    return a != Area::Count && area_visible(a, advanced);
}

bool page_unproven(Page p) { return area_of(p) == Area::Advanced; }
const char* lab_banner_text() { return "Lab: not proven in game yet. Nothing here is guaranteed to show up in your match."; }
bool page_effect_unmeasured(Page p) { return p == Page::GameplayPresets; }

Place area_for(int legacy_tab) {
    switch (legacy_tab) {
        case 0: return place_of(Page::PlayerEdit);
        case 1: return place_of(Page::Squad);
        case 2: return place_of(tree40() ? Page::PlayerEdit : Page::Staff);  // 4.0: managers are the Managers tab of the Players page
        case 3: return place_of(Page::Standings);
        case 4: return place_of(Page::AdvDatabase);
        case 5: return place_of(Page::Transfers);
        case 6: return place_of(Page::Settings);
        case 7: return place_of(Page::GameplayPresets);
        default: return Place{};
    }
}

int legacy_tab_of(Page p) {
    const PageInfo* i = info_of(p);
    return i ? i->legacy_tab : -1;
}

bool register_page(Page p, PageFn fn) {
    if (fn == nullptr || info_of(p) == nullptr) return false;
    return registry().emplace(static_cast<int>(p), fn).second;
}

PageFn registered_page(Page p) {
    auto it = registry().find(static_cast<int>(p));
    return it == registry().end() ? nullptr : it->second;
}

namespace {

// 4.0 S3: the switch of the preview, drawn once per ImGui frame above every Lab page (App::draw_pages calls draw_new_page and/or draw_page for
// the page; whichever runs first draws it). The rail itself is app.cpp's, so the Lab is where the person who asked for the Lab finds it. On a
// change the Lab opens on its first page (the page that was open may not exist in the other tree). The back place is NOT cleared here: a page
// draw cannot clear it (App::draw_pages note_leave overwrites it after the page draws), so Back after a toggle returns to the Lab page the
// person came from. The checkbox is also drawn above the old tree's Lab pages, on purpose: that is the only way into the preview. True when
// it changed: the caller draws nothing else this frame.
bool draw_preview_switch(App& app, Page p) {
    if (area_of(p) != Area::Advanced) return false;
    const ImGuiContext* g = ImGui::GetCurrentContext();  // the pure shell tests call draw_page outside a frame / window: no widget then
    if (!g || !g->WithinFrameScope || !g->CurrentWindow || g->CurrentWindow->SkipItems) return false;
    static int s_frame = -1;
    const int frame = ImGui::GetFrameCount();
    if (frame == s_frame) return false;
    s_frame = frame;
    bool on = tree40();
    const bool changed = ImGui::Checkbox("Preview the 4.0 menus###tree40_preview", &on);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Try the 4.0 menu layout. Not saved: the menus are back as they were after a restart.");
    if (!changed) return false;
    tree40() = on;
    app.go(Area::Advanced, default_page(Area::Advanced));
    return true;
}

}  // namespace

bool draw_page(App& app, Page p, bool advanced) {
    if (!page_visible(p, advanced)) return false;
    if (draw_preview_switch(app, p)) return true;
    PageFn fn = registered_page(p);
    if (tree40() && p == Page::Squad) fn = draw_team_squad_page;  // 4.0: Team > Squad is S4's page (ui_team_squad_page.h; 4.0 integration, S3 merge)
    if (fn == nullptr) return false;
    fn(app);
    return true;
}

void clear_registry_for_tests() { registry().clear(); }

bool register_new_page(Page p, PageFn fn) {
    if (fn == nullptr || info_of(p) == nullptr) return false;
    return new_registry().emplace(static_cast<int>(p), fn).second;
}

PageFn registered_new_page(Page p) {
    auto it = new_registry().find(static_cast<int>(p));
    return it == new_registry().end() ? nullptr : it->second;
}

bool draw_new_page(App& app, Page p, bool advanced) {
    if (!page_visible(p, advanced)) return false;
    if (draw_preview_switch(app, p)) return true;
    PageFn fn = registered_new_page(p);
    if (tree40() && p == Page::Squad) fn = draw_team_squad_page;  // 4.0: Team > Squad is S4's page (the old My squad page stays the Lab's in the old tree)
    if (fn == nullptr) return false;
    fn(app);
    return true;
}

const char* page_key(Page p) {
    // The enumerator names, never the labels or the numbers: a renamed page or a page inserted by another track keeps the widths people chose
    switch (p) {
#define TURBO_PK(x) case Page::x: return #x;
        TURBO_PK(Home) TURBO_PK(PlayerEdit) TURBO_PK(Squad) TURBO_PK(CreatePlayer) TURBO_PK(BulkEdit) TURBO_PK(Staff) TURBO_PK(CommentaryNames)
        TURBO_PK(LooksStudio)
        TURBO_PK(Kits) TURBO_PK(Identity) TURBO_PK(TeamPlayers) TURBO_PK(TeamTransfers) TURBO_PK(StadiumExtras)
        TURBO_PK(TacticPresets) TURBO_PK(TeamSheet) TURBO_PK(GameplayPresets)
        TURBO_PK(Standings) TURBO_PK(Transfers) TURBO_PK(JobOffer) TURBO_PK(CMTracker) TURBO_PK(ChangesUndo)
        TURBO_PK(Settings)
        TURBO_PK(AdvDatabase) TURBO_PK(AdvRawRows) TURBO_PK(AdvBulkEdit) TURBO_PK(AdvMaintenance) TURBO_PK(AdvTactics) TURBO_PK(AdvMatchSetup)
        TURBO_PK(AdvGameplayTuning) TURBO_PK(AdvExport) TURBO_PK(AdvDiagnostics) TURBO_PK(AdvKitDiagnostics)
#undef TURBO_PK
        default: return "Page";
    }
}

namespace page {

void frame(App& app, DrawFn select_fn, DrawFn editor_fn, void* user, Primary* primary) {
    if (primary) primary->clicked = false;
    const bool narrow = ImGui::GetWindowWidth() < S(theme::kNarrowWindow);
    const float footer = primary && primary->label ? ImGui::GetFrameHeightWithSpacing() + S(theme::kGrid) : 0.0f;
    if (select_fn) {
        // 2.22 G-resize: the select panel is resizable (ui_split.h: ResizeX, remembered per page in gui_settings.json gui.splits, 180 px .. 70 %).
        // The ID "##select" is unchanged (the tables inside keep their saved layout); the narrow rail is a plain child of its own ID, so its
        // 36 px never reaches the saved window size of "##select".
        if (narrow) {
            ImGui::BeginChild("##selectrail", ImVec2(S(theme::kRailWidth), -footer), ImGuiChildFlags_Borders);
        } else {
            split::pane_x("##select", theme::kSelectWidth, kSplitMinW, kSplitMaxFrac, -footer);
            select_fn(app, user);
        }
        ImGui::EndChild();
        ImGui::SameLine();
    }
    ImGui::BeginChild("##editor", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    if (editor_fn) editor_fn(app, user);
    ImGui::EndChild();
    if (primary && primary->label) {
        // right-aligned, under the editor
        const float bw = ImGui::CalcTextSize(primary->label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float room = ImGui::GetContentRegionAvail().x - bw;
        if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
        if (!primary->enabled) ImGui::BeginDisabled();
        if (theme::primary_button(primary->label)) primary->clicked = true;
        if (!primary->enabled) ImGui::EndDisabled();
    }
}

}  // namespace page

}  // namespace turbo::shell
