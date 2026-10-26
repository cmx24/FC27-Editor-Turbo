// FC 27 LE Turbo GUI - shell contract: areas, pages, legacy tab shim, page registry, page frame (ui_shell.h).
#include "ui_shell.h"

#include <map>

#include "app.h"
#include "imgui.h"
#include "imgui_internal.h"
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

    // Team (the Club area, renamed): Squad stays Page::Squad (area Club in this tree) and draws draw_teams until S4's page is wired
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

bool page_has_frame(Page p) { return p == Page::AdvExport; }  // ui_diagnostics_page.cpp (page::Frame id "data_export"); add a line per converted page

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
    if (tree40() && p == Page::Squad) fn = draw_teams;  // 4.0: Team > Squad is the squad table until S4's page is wired
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
    if (tree40() && p == Page::Squad) fn = draw_teams;  // 4.0: Team > Squad (the old My squad page stays the Lab's in the old tree)
    if (fn == nullptr) return false;
    fn(app);
    return true;
}

namespace page {

void frame(App& app, DrawFn select_fn, DrawFn editor_fn, void* user, Primary* primary) {
    if (primary) primary->clicked = false;
    const bool narrow = ImGui::GetWindowWidth() < S(theme::kNarrowWindow);
    const float footer = primary && primary->label ? ImGui::GetFrameHeightWithSpacing() + S(theme::kGrid) : 0.0f;
    if (select_fn) {
        const float w = narrow ? S(theme::kRailWidth) : S(theme::kSelectWidth);
        ImGui::BeginChild("##select", ImVec2(w, -footer), ImGuiChildFlags_Borders);
        if (!narrow) select_fn(app, user);
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
