// FC 27 LE Turbo GUI - the shell contract of the revamped GUI (GUI_REVAMP_SPEC.md sections 1, 2.1 and 4).
//
// Six areas (+ Advanced, gated by GuiPrefs::advanced_mode), each a strip of pages. A page is drawn by a plain function
// registered here from its own .cpp, so the tracks never edit app.cpp (only the shell track T1 does). The eight legacy
// top-level tabs (TopTab in gui_prefs.h, App::request_tab 0..7) keep working through area_for().
//
// Use from a page .cpp:
//     #include "ui_shell.h"
//     static void draw_kits_page(turbo::App& app) { ... }
//     TURBO_REGISTER_PAGE(turbo::shell::Page::Kits, draw_kits_page);
// Navigation: T1 adds App::go(Area, Page) on top of Place; until then the legacy shim App::request_tab = N lands on
// area_for(N). The registry lives in ui_shell.cpp; the enums and the tables are inline there too (no ImGui state here).
#pragma once
#include <vector>

namespace turbo {
class App;
}

namespace turbo::shell {

// 2.16 step 4 (plan 4.2): Players, Faces & looks, Kits & club, Tactics, Career, Settings, Advanced. The identifiers keep their 2.13 names
// (Squad = Players, Club = Kits & club) so that no caller changes; only the labels in ui_shell.cpp are new.
enum class Area : int { Squad = 0, Looks, Club, Tactics, Career, Settings, Advanced, Count };

// One id per page, unique across areas. Order inside an area = order of the page strip (see pages_of).
enum class Page : int {
    None = 0,
    // Players
    PlayerEdit, Squad, CreatePlayer, Staff, CommentaryNames,
    // Faces & looks
    FacesHair, BodyGear,
    // Kits & club
    Kits, Identity, StadiumExtras,
    // Tactics
    TacticPresets, TeamSheet, GameplayPresets,
    // Career
    Standings, Transfers, JobOffer, CMTracker, ChangesUndo,
    // Settings
    Settings,
    // Advanced (gated)
    AdvDatabase, AdvRawRows, AdvBulkEdit, AdvMaintenance, AdvTactics, AdvMatchSetup, AdvGameplayTuning, AdvExport, AdvDiagnostics, AdvKitDiagnostics,
    Count
};

struct Place {
    Area area = Area::Count;  // Count = invalid
    Page page = Page::None;
    bool valid() const { return area != Area::Count && page != Page::None; }
    bool operator==(const Place& o) const { return area == o.area && page == o.page; }
};

constexpr int kAreaCount = static_cast<int>(Area::Count);
constexpr int kPageCount = static_cast<int>(Page::Count);

// ---- labels (sentence case, as drawn on the tab and the page strip)
const char* area_label(Area a);  // "Players", "Faces & looks", "Kits & club", "Tactics", "Career", "Settings", "Advanced"
const char* page_label(Page p);  // "Edit a player", "Kits", "Tactic presets", ...

// ---- structure
Area area_of(Page p);                       // Area::Count for Page::None / Count
const std::vector<Page>& pages_of(Area a);  // the page strip of the area, in order
Page default_page(Area a);                  // first of the strip: Edit a player, Faces & hair, Kits, Tactic presets, Standings, Settings, Database
Place place_of(Page p);                     // {area_of(p), p}

// ---- Simple / Advanced view (A6): Simple hides the whole Advanced area (raw rows, bulk edit, maintenance and the
// not-proven panels); every other page is visible in both views. The Advanced area only exists with the toggle.
bool page_visible(Page p, bool advanced);
bool area_visible(Area a, bool advanced);
// Pages that show the one amber "Not proven in game yet" line in the Advanced view: AdvTactics, AdvMatchSetup, AdvGameplayTuning
bool page_unproven(Page p);
// Mixed page: the write is proven, the effect on play is not (GameplayPresets): one muted sentence instead of the banner
bool page_effect_unmeasured(Page p);

// ---- legacy tab shim: TopTab indices 0 players, 1 teams, 2 managers, 3 competitions, 4 database, 5 tools, 6 status,
// 7 game settings (gui_prefs.h) -> their new home. Out of range -> invalid Place.
//   0 Players/PlayerEdit  1 Players/Squad  2 Players/Staff  3 Career/Standings  4 Advanced/AdvDatabase
//   5 Career/Transfers  6 Settings/Settings  7 Tactics/GameplayPresets
Place area_for(int legacy_tab);
// The legacy tab a page used to live in (-1 when it had none): inverse of area_for for the pages above, plus the pages
// that moved out of Teams / Competitions / Tools (Kits, Identity, TacticPresets, ... see ui_shell.cpp)
int legacy_tab_of(Page p);

// ---- page registration: pages register a draw function from their own .cpp (static init, once per page)
using PageFn = void (*)(App&);
bool register_page(Page p, PageFn fn);  // false (and ignored) for Page::None / Count, a null fn or a second registration
PageFn registered_page(Page p);         // nullptr when no page .cpp registered it (yet)
// Draws the page with its function. False when nothing is registered or the page is hidden in this view.
bool draw_page(App& app, Page p, bool advanced);
// Test hook: forget all registrations
void clear_registry_for_tests();

// ---- the 2.16 pages (wiring): a second registry that is asked first. A page here replaces the page of the same id until the person (or a
// test) asks for the old panels (App::legacy_pages). Registered from ui_pages_216.cpp; the old registry and the legacy panels are untouched.
bool register_new_page(Page p, PageFn fn);  // false (and ignored) for Page::None / Count, a null fn or a second registration
PageFn registered_new_page(Page p);
bool draw_new_page(App& app, Page p, bool advanced);  // false when no new page is registered or the page is hidden in this view
#define TURBO_REGISTER_NEW_PAGE(page, fn) \
    static const ::turbo::shell::PageRegistrar TURBO_SHELL_CAT(turbo_new_page_reg_, __LINE__)((page), (fn), true)

struct PageRegistrar {
    PageRegistrar(Page p, PageFn fn, bool new_page = false) {
        if (new_page) register_new_page(p, fn);
        else register_page(p, fn);
    }
};
#define TURBO_SHELL_CAT2(a, b) a##b
#define TURBO_SHELL_CAT(a, b) TURBO_SHELL_CAT2(a, b)
#define TURBO_REGISTER_PAGE(page, fn) \
    static const ::turbo::shell::PageRegistrar TURBO_SHELL_CAT(turbo_page_reg_, __LINE__)((page), (fn))

// ---- page pattern (spec 2.1): [select | editor] with ONE primary action
namespace page {
struct Primary {
    const char* label = nullptr;  // "Apply preset": a verb; nullptr = no primary action
    bool enabled = true;
    bool clicked = false;         // out: true on the frame the button was pressed
};
using DrawFn = void (*)(App&, void* user);
// Child ids "##select" and "##editor" (stable in imgui.ini). select_fn may be nullptr (single-object page: editor only).
// The select panel is theme::kSelectWidth wide (scaled) and collapses to theme::kRailWidth under theme::kNarrowWindow.
// Must be called inside the main window, after the page strip.
void frame(App& app, DrawFn select_fn, DrawFn editor_fn, void* user, Primary* primary);
}  // namespace page

}  // namespace turbo::shell
