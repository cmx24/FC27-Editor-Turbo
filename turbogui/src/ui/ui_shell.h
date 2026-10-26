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
#include <string>
#include <vector>

namespace turbo {
class App;
}

namespace turbo::shell {

// 3.0 shell: Home, Players, Looks, Club, Tactics, Career, Settings, Lab (the left rail, top to bottom; Settings and Lab pinned at the bottom).
// The identifiers keep their 2.13 names (Squad = Players, Club = Club, Advanced = Lab) so that no caller changes; only the labels in ui_shell.cpp
// are new. Home is first (3.0: the page the app opens on).
enum class Area : int { Home = 0, Squad, Looks, Club, Tactics, Career, Settings, Advanced, Count };

// One id per page, unique across areas. Order inside an area = order of the page strip (see pages_of).
enum class Page : int {
    None = 0,
    // Home (3.0: the only page of its area, ui_home.cpp)
    Home,
    // Players
    PlayerEdit, Squad, CreatePlayer, BulkEdit, Staff, CommentaryNames,
    // Appearance Editor (2.20: the empty Faces & hair and Body, tattoos & gear signpost pages are gone; the Looks studio is the Appearance Editor)
    LooksStudio,
    // Team (2.20: was Kits & club; Players and Transfers are the career side of a club)
    Kits, Identity, TeamPlayers, TeamTransfers, StadiumExtras,
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
const char* area_label(Area a);  // "Home", "Players", "Looks", "Club", "Tactics", "Career", "Settings", "Lab"
const char* page_label(Page p);  // "Player", "Kits", "Presets", ...

// ---- structure
Area area_of(Page p);                       // Area::Count for Page::None / Count
const std::vector<Page>& pages_of(Area a);  // the page strip of the area, in order
Page default_page(Area a);                  // first of the strip: Home, Player, Appearance Editor, Kits, Presets, Standings, Settings, Database
Place place_of(Page p);                     // {area_of(p), p}

// Edit a player is one page with tabs: 0 Profile, 1 Positions, attributes & PlayStyles, 2 Appearance, 3 All fields. The page follows this value
// when code changes it (a link that opens the Appearance tab, a test) and writes the tab the user clicked back into it.
int& player_edit_tab();

// 3.1 N1: the place the last navigation left (go(), a cross-link, a rail or page-strip click), so a page that was opened from another
// one can offer "< Back to <page>". One level. Place{} (invalid) = nothing to go back to. App::back() goes there and clears it; the App
// constructor clears it (the value outlives an App only in tests).
Place& back_place();

// ---- Simple / Advanced view (A6): Simple hides the whole Advanced area (raw rows, bulk edit, maintenance and the
// not-proven panels); every other page is visible in both views. The Advanced area only exists with the toggle.
bool page_visible(Page p, bool advanced);
bool area_visible(Area a, bool advanced);
// 3.0: every Lab page (the Advanced area) shows the one amber line lab_banner_text() above the page (App::draw_pages)
bool page_unproven(Page p);
const char* lab_banner_text();  // "Lab: not proven in game yet. Nothing here is guaranteed to show up in your match."
// Mixed page: the write is proven, the effect on play is not (GameplayPresets): one muted sentence instead of the banner
bool page_effect_unmeasured(Page p);

// ---- names -> place (4.0 S0a, the dev-service open_page op): the place an {area label, page label} pair names, labels as drawn on the
// rail and the page strip (area_label / page_label; ASCII case ignored), i.e. the strings the native tests click (Ui::open_page). No
// second id map: the labels above are the only names. False with an error naming the unknown area or page (and the valid ones).
bool find_place(const std::string& area, const std::string& page, Place& out, std::string& error);
// The overlay's half of open_page (core/devops.h dev_page_take / dev_page_answer), called first thing in App::draw (hidden or not):
// answers the request it applied on the previous frame with the page drawn since (app.place()) or, while Turbo is hidden, with the
// page it will open on, then takes a new request and opens it with App::go. A Lab page while the Lab is hidden is refused by name.
void dev_page_pump(App& app);

// ---- legacy tab shim: TopTab indices 0 players, 1 teams, 2 managers, 3 competitions, 4 database, 5 tools, 6 status,
// 7 game settings (gui_prefs.h) -> their new home. Out of range -> invalid Place.
//   0 Players/PlayerEdit  1 Lab/Squad (3.0: "My squad (old page)")  2 Players/Staff  3 Career/Standings  4 Advanced/AdvDatabase
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
