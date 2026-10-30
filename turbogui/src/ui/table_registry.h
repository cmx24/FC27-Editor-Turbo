// FC 27 LE Turbo GUI - the registry of every ImGui table in Turbo's screens and the column policy they share
// (2026-10-11, Cassio: "resizable everything like rows, columns, frames").
//
// Policy: every table (but the one inside a popup, `popup`) has resizable, reorderable and hideable columns (drag a header border / drag a header / right-click
// a header). Dear ImGui keeps column widths, order and visibility per table ID in the layout file
// (turbo_output\turbo_gui_layout_v2.ini, written by core/layout_store.h since 5.0b-B, loaded at start; the overlay sets io.IniFilename = nullptr)
// when the ID is stable across sessions
// and the column count does not change; the Database tab therefore pushes the table's name on the ID stack.
// Row height is not a table flag: it follows the "Row density" setting (gui_settings.json gui.row_density, ui/gui_prefs.h).
//
// The native test test_ux_resize.h walks this list and the sources: every BeginTable in src/ui carries the three column
// flags (or refers to this registry), every ID below exists in the sources, and every literal ID in the sources is below.
#pragma once
#include <vector>

#include "imgui.h"

namespace turbo {
namespace tbl {

constexpr ImGuiTableFlags kColumns = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
constexpr ImGuiTableFlags kData = kColumns | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
constexpr ImGuiTableFlags kGrid = kColumns | ImGuiTableFlags_BordersInnerV;  // a layout grid of editors (no header row)

struct Spec {
    const char* id;      // the BeginTable ID literal (stable across sessions)
    const char* where;   // the screen
    ImGuiTableFlags flags;
    bool scrolls;        // ScrollY: fills its pane, rows clipped (the pane is resizable instead of the table)
    bool popup = false;  // inside an auto-sizing popup (a combo, a dropdown, a tooltip): keeps fixed columns. Saved column settings of a table in
                         // a popup broke its second opening (native "Live standings: groups" case, 2026-10-11)
};

// The sizing policy of a spec, or 0 when the columns decide (TableSetupColumn WidthFixed / WidthStretch)
inline ImGuiTableFlags sizing_of(ImGuiTableFlags f) { return f & ImGuiTableFlags_SizingMask_; }

inline const std::vector<Spec>& all() {
    static const std::vector<Spec> v = {
        {"##players", "Players > list", kData | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, true},
        {"##bits", "Players > Attributes (bit fields)", kGrid | ImGuiTableFlags_SizingStretchSame, false},
        {"##chg", "Players > Archetype changes", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##sl", "Players > Attributes (slider grid)", kGrid | ImGuiTableFlags_SizingStretchSame, false},
        {"##fsearch", "Players > field search", kGrid | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersOuter, false},
        {"##btspec", "Players > Body type (specific)", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##btgen", "Players > Body type (generic)", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##cntable", "Players > Callname", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##vstable", "Players > Callname > Voice swaps", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##cnalltable", "Players > Callname > All callnames", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##cnptable", "Players > Callname", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##cptable", "Players > Callname > club players", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##nmidtable", "Players > Names > name ids", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##teams", "Teams > list", kData | ImGuiTableFlags_ScrollY, true},
        {"##teamsxf", "Teams > list with transfer columns (2.1 parity)", kData | ImGuiTableFlags_ScrollY, true},
        {"##packrows", "Players > Season pack (preview rows)", kData | ImGuiTableFlags_ScrollY, true},
        {"##xfrows", "Teams > club transfers (2.1 parity)", kData | ImGuiTableFlags_ScrollY, true},
        {"##scouts", "Teams > scouts (2.1 parity)", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##squad", "Teams > Squad", kData | ImGuiTableFlags_ScrollY, true},
        {"##clubs", "Teams > club picker", kData | ImGuiTableFlags_ScrollY, true},
        {"##mgrs", "Managers > list", kData | ImGuiTableFlags_ScrollY, true},
        {"##crestfiles", "Teams > Crest files", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##kitset", "Teams > Kits: a kit zip's settings next to the club's (2.3)", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##tpexttbl", "Team > Transfers > Contract extensions", kData | ImGuiTableFlags_ScrollY, true},
        {"##tproletbl", "Team > Transfers > Squad roles", kData | ImGuiTableFlags_ScrollY, true},
        {"##rolepreview", "Teams > Squad roles preview", kData | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, true},
        {"##comptable", "Competitions > picker (combo popup)", ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, false, true},
        {"##league", "Competitions > league table", kData | ImGuiTableFlags_ScrollY, true},
        {"##live", "Competitions > live standings", kData | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Sortable, true},
        {"##live_ha", "Competitions > live standings (home / away)", kData | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Sortable, true},
        {"##dbt", "Database > table (ID pushed per table name)", kData | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, true},
        {"##gvars", "Match > game variables", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##gsvals", "Game settings > values (ID pushed per group: match, player, user, cpu)", kData | ImGuiTableFlags_SizingStretchProp, false},
        {"##tattoos", "Players > Tattoos", kColumns | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit, false},
        {"##galleries", "Players > Appearance galleries", kColumns | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit, false},
        {"##vrows", "Players > Appearance > Pictures and animations (2.20)", kColumns | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, false},
        {"##vrow", "Looks Studio > one picture row (2.20)", kColumns | ImGuiTableFlags_SizingFixedFit, false},
        {"##all", "All fields (widgets.cpp)", kData | ImGuiTableFlags_ScrollY, true},
        // Tables of the 1.2.5 line and the filter grid (merged into 2.0/rc after this list was written; 2.4 removed the per-side
        // gameplay slider table of tactics channel A)
        {"##pfilters", "Players > filter grid (four blocks, no header row)", kGrid | ImGuiTableFlags_SizingStretchProp, false},
        {"##keptlist", "Turbo Tools > Kept player edits", kData | ImGuiTableFlags_ScrollY, true},
        {"##scopelist", "Career tools > Changes & undo > Where kept edits apply (2.15)", kData | ImGuiTableFlags_ScrollY, true},
        {"##chgrows", "Career > Changes and undo > This session (2.16)", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##chgkept", "Career > Changes and undo > Remembered for every career load (2.16)", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##chgelsewhere", "Career > Changes and undo > Undo buttons on other pages (2.16)", kData | ImGuiTableFlags_SizingFixedFit, false},
        {"##clubrows", "Club search dropdown (popup under the box)", ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV, true, true},
        {"##keptplayer", "Players > kept edits tooltip (auto-sizing, cannot be clicked)", ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg, false, true},
        {"##pidtable", "Players > Create player > Use a hidden player's id (auto-sizing modal)",
         ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit, true, true},
    };
    return v;
}

// The spec of an ID, or nullptr
inline const Spec* find(const char* id) {
    for (const Spec& s : all()) {
        const char* a = s.id;
        const char* b = id;
        while (*a && *a == *b) ++a, ++b;
        if (*a == 0 && *b == 0) return &s;
    }
    return nullptr;
}

}  // namespace tbl
}  // namespace turbo
