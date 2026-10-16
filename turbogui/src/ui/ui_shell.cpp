// FC 27 LE Turbo GUI - shell contract: areas, pages, legacy tab shim, page registry, page frame (ui_shell.h).
#include "ui_shell.h"

#include <map>

#include "app.h"
#include "imgui.h"
#include "ui_theme.h"

namespace turbo::shell {

namespace {

struct PageInfo {
    Page page;
    Area area;
    const char* label;
    int legacy_tab;  // the TopTab the panel lived in before the revamp (-1: none)
};

// Order = order in kPages; the page strip of an area lists its pages in this order
const PageInfo kPages[] = {
    {Page::PlayerEdit, Area::Squad, "Player edit", 0},
    {Page::Squad, Area::Squad, "Squad", 1},
    {Page::Staff, Area::Squad, "Staff", 2},

    {Page::Kits, Area::Club, "Kits", 1},
    {Page::Identity, Area::Club, "Identity", 1},
    {Page::StadiumExtras, Area::Club, "Stadium & extras", 5},

    {Page::TacticPresets, Area::Tactics, "Tactic presets", 1},
    {Page::GameplayPresets, Area::Tactics, "Gameplay presets", 7},
    {Page::Standings, Area::Tactics, "Standings", 3},

    {Page::Transfers, Area::Career, "Transfers", 1},
    {Page::JobOffer, Area::Career, "Job offer", 1},
    {Page::Scouting, Area::Career, "Scouting", 5},
    {Page::Budget, Area::Career, "Budget", 5},
    {Page::Exports, Area::Career, "Exports", 5},
    {Page::Bans, Area::Career, "Bans", 5},
    {Page::CMTracker, Area::Career, "CMTracker", 5},
    {Page::ChangesUndo, Area::Career, "Changes & undo", 5},
    {Page::CareerMaintenance, Area::Career, "Maintenance", 3},

    {Page::Settings, Area::Settings, "Settings", 6},

    {Page::AdvDatabase, Area::Advanced, "Database", 4},
    {Page::AdvRawRows, Area::Advanced, "Raw rows", 0},
    {Page::AdvBulkEdit, Area::Advanced, "Bulk edit", 5},
    {Page::AdvMaintenance, Area::Advanced, "Maintenance", 5},
    {Page::AdvTactics, Area::Advanced, "Tactics (experimental)", 1},
    {Page::AdvMatchSetup, Area::Advanced, "Match setup", 3},
    {Page::AdvGameplayTuning, Area::Advanced, "Gameplay tuning", 3},
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

}  // namespace

const char* area_label(Area a) {
    switch (a) {
        case Area::Squad: return "Squad & Players";
        case Area::Club: return "Club & Kits";
        case Area::Tactics: return "Tactics & Gameplay";
        case Area::Career: return "Career tools";
        case Area::Settings: return "Settings";
        case Area::Advanced: return "Advanced";
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

Place place_of(Page p) {
    Place pl;
    pl.area = area_of(p);
    pl.page = pl.area == Area::Count ? Page::None : p;
    return pl;
}

bool area_visible(Area a, bool advanced) {
    if (a == Area::Count) return false;
    return a != Area::Advanced || advanced;
}

bool page_visible(Page p, bool advanced) {
    const Area a = area_of(p);
    return a != Area::Count && area_visible(a, advanced);
}

bool page_unproven(Page p) { return p == Page::AdvTactics || p == Page::AdvMatchSetup || p == Page::AdvGameplayTuning; }
bool page_effect_unmeasured(Page p) { return p == Page::GameplayPresets; }

Place area_for(int legacy_tab) {
    switch (legacy_tab) {
        case 0: return place_of(Page::PlayerEdit);
        case 1: return place_of(Page::Squad);
        case 2: return place_of(Page::Staff);
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

bool draw_page(App& app, Page p, bool advanced) {
    if (!page_visible(p, advanced)) return false;
    const PageFn fn = registered_page(p);
    if (fn == nullptr) return false;
    fn(app);
    return true;
}

void clear_registry_for_tests() { registry().clear(); }

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
