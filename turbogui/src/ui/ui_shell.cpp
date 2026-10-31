// FC 27 LE Turbo GUI - shell contract: areas, pages, legacy tab shim, page registry, page frame (ui_shell.h).
#include "ui_shell.h"

#include <map>

#include "app.h"
#include "core/devops.h"
#include "imgui.h"
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
// legacy tab (-1) is new. Scouting, Budget and Bans are sections of the Transfers & money page (the old Turbo Tools panel); Game editors
// stay on Transfers & money.
const PageInfo kPages[] = {
    {Page::Home, Area::Home, "Home", -1},  // 3.0: search, squad table, presets strip, recent changes, the area tiles (ui_home.cpp)

    {Page::PlayerEdit, Area::Squad, "Player", 0},
    {Page::CreatePlayer, Area::Squad, "Create & clone", -1},  // 2.20: create / clone / import / export / delete (ui_player_creation.cpp)
    {Page::BulkEdit, Area::Squad, "Many players at once", -1},  // 2.20: any field of many players at once (ui_bulk_edit.cpp)
    {Page::Staff, Area::Squad, "Staff", 2},

    {Page::LooksStudio, Area::Looks, "Appearance Editor", -1},  // 2.17 ui_looks_studio.cpp (the Looks studio); 2.20: renamed, and the only page of its area

    {Page::Kits, Area::Club, "Kits", 1},
    {Page::Identity, Area::Club, "Name, colours & crest", 1},
    {Page::TeamPlayers, Area::Club, "Squad", -1},        // 2.20: career side of a club: contract, form, growth, squad role, team preset (ui_team_players.cpp)
    {Page::TeamTransfers, Area::Club, "Transfers", -1},  // 2.20: moves, loans, signings, contract extensions (ui_team_transfers.cpp)
    {Page::StadiumExtras, Area::Club, "Stadium & extras", 5},

    {Page::TacticPresets, Area::Tactics, "Presets", 1},
    {Page::TeamSheet, Area::Tactics, "Team sheet & bench", -1},

    // 10-28 findability (game-gui-architect override 6, briefs/NEXT_GUI_FINDABILITY.md step 1): Game Type Settings + CPU Sliders + presets on
    // their own area and page, never under Tactics.
    {Page::GameplayPresets, Area::GameSettings, "Game settings & CPU sliders", 7},

    {Page::Standings, Area::Career, "Standings", 3},
    {Page::Transfers, Area::Career, "Money & transfers", 5},
    {Page::JobOffer, Area::Career, "Manager jobs", 1},
    {Page::CMTracker, Area::Career, "Season pack", 5},
    {Page::ChangesUndo, Area::Career, "Changes & undo", 5},

    {Page::Settings, Area::Settings, "Settings", 6},
};

const PageInfo* info_of(Page p) {
    for (const PageInfo& i : kPages)
        if (i.page == p) return &i;
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

// 4.0 integration (S3 merge, ask (4)): every page that draws page::Frame's bar. The list is the set of pages the native cases
// "Shell 4.0: every page ..." and "Shell 4.0: page_has_frame ..." found drawing ###pg_back (fail-old log
// evidence/2026-10-26/RH/s3_old_1.log). Add a line per converted page.
bool page_has_frame(Page p) {
    switch (p) {
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
        default:
            return false;
    }
}

const char* area_label(Area a) {
    switch (a) {
        case Area::Home: return "Home";
        case Area::Squad: return "Players";
        case Area::Looks: return "Looks";
        case Area::Club: return "Club";
        case Area::Tactics: return "Tactics";
        case Area::GameSettings: return "Game settings";
        case Area::Career: return "Career";
        case Area::Settings: return "Turbo settings";  // 10-28: was "Settings", clashed with the game's own settings
        default: return "";
    }
}

const char* page_label(Page p) {
    const PageInfo* i = info_of(p);
    return i ? i->label : "";
}

Area area_of(Page p) {
    const PageInfo* i = info_of(p);
    return i ? i->area : Area::Count;
}

const std::vector<Page>& pages_of(Area a) {
    static const std::vector<std::vector<Page>> strips = [] {
        std::vector<std::vector<Page>> s(kAreaCount);
        for (const PageInfo& i : kPages) s[static_cast<int>(i.area)].push_back(i.page);
        return s;
    }();
    static const std::vector<Page> none;
    const int k = static_cast<int>(a);
    return (k >= 0 && k < kAreaCount) ? strips[static_cast<size_t>(k)] : none;
}

Page default_page(Area a) {
    const auto& v = pages_of(a);
    return v.empty() ? Page::None : v.front();
}

int& player_edit_tab() {
    static int tab = 0;
    return tab;
}

int& manager_edit_tab() {
    static int tab = 0;
    return tab;
}

bool& miniface_3d_request() {
    static bool req = false;
    return req;
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
    app.go(target.area, target.page);
    g_dev_page_ticket = rq.ticket;
    g_dev_page_target = target;
    g_dev_page_drawn = app.visible;
}

bool area_visible(Area a) { return a != Area::Count; }

bool page_visible(Page p) { return area_of(p) != Area::Count; }

bool page_effect_unmeasured(Page p) { return p == Page::GameplayPresets; }

Place area_for(int legacy_tab) {
    switch (legacy_tab) {
        case 0: return place_of(Page::PlayerEdit);
        case 2: return place_of(Page::Staff);
        case 3: return place_of(Page::Standings);
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

bool draw_page(App& app, Page p) {
    if (!page_visible(p)) return false;
    PageFn fn = registered_page(p);
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

bool draw_new_page(App& app, Page p) {
    if (!page_visible(p)) return false;
    PageFn fn = registered_new_page(p);
    if (fn == nullptr) return false;
    fn(app);
    return true;
}

const char* page_key(Page p) {
    // The enumerator names, never the labels or the numbers: a renamed page or a page inserted by another track keeps the widths people chose
    switch (p) {
#define TURBO_PK(x) case Page::x: return #x;
        TURBO_PK(Home) TURBO_PK(PlayerEdit) TURBO_PK(CreatePlayer) TURBO_PK(BulkEdit) TURBO_PK(Staff)
        TURBO_PK(LooksStudio)
        TURBO_PK(Kits) TURBO_PK(Identity) TURBO_PK(TeamPlayers) TURBO_PK(TeamTransfers) TURBO_PK(StadiumExtras)
        TURBO_PK(TacticPresets) TURBO_PK(TeamSheet) TURBO_PK(GameplayPresets)
        TURBO_PK(Standings) TURBO_PK(Transfers) TURBO_PK(JobOffer) TURBO_PK(CMTracker) TURBO_PK(ChangesUndo)
        TURBO_PK(Settings)
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
