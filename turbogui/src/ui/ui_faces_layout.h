// 5.0b-C (Cassio 10-29 23:00, "fix cluttered layouts with a better UX/UI", screenshot of Choose a real face):
// the chooser's layout as DATA. The draw code in ui_faces.cpp iterates this table (a switch on Control per entry, in table order), and
// the layout test reads the same table, so a control cannot be added to the first row without the test saying so.
//   Row1      what is always on screen above the tiles: the search box, the Filters button and the scope (All / This save / Hidden game heads)
//   Popover   every filter of the old header, inside the "Filters (n)" popover (nothing dropped)
//   Grid      the tiles (ImGuiListClipper)
//   Selection the bar under the grid for the selected tile: What to copy, Apply, His own head, Close
// Tab order (keyboard) = the table order without the Popover entries: search, Filters, scope, tiles, What to copy, Apply, His own head, Close.
#pragma once
#include <cstring>
#include <string>
#include <vector>

namespace turbo::chooser_layout {

enum class Zone { Row1, Popover, Grid, Selection };
enum class Control {
    Search, FiltersButton, Scope,
    HeadKind, RealOnly, HqOnly, Only3d, Sort, Facets, ClearFilters,
    Tiles,
    WhatToCopy, Apply, HisOwnHead, Close
};
struct Entry {
    Zone zone;
    Control control;
    const char* label;  // the widget's ImGui label as drawn ("Facets": the prefix every facet button's id carries)
    const char* what;   // one line for the test and the tooltips
};

inline const std::vector<Entry>& entries() {
    static const std::vector<Entry> kEntries = {
        {Zone::Row1, Control::Search, "##facesearch", "search by name or ID; focused when the chooser opens"},
        {Zone::Row1, Control::FiltersButton, "###facefilters", "Filters (n): n = filters that narrow the list"},
        {Zone::Row1, Control::Scope, "##facescope", "All / In this save / Hidden game heads"},
        // the Filters popover: all the filters of the old header
        {Zone::Popover, Control::HeadKind, "Player heads|Manager heads", "manager target only: whose heads are listed"},
        {Zone::Popover, Control::RealOnly, "Real faces only", ""},
        {Zone::Popover, Control::HqOnly, "High-quality heads only", ""},
        {Zone::Popover, Control::Only3d, "Only heads with a 3D look", "shown when the list has 3D looks"},
        {Zone::Popover, Control::Sort, "Sort##facesort", ""},
        {Zone::Popover, Control::Facets, "##facet", "one button per facet: gender, skin tone, hair colour, hair, facial hair, ..."},
        {Zone::Popover, Control::ClearFilters, "Clear filters", ""},
        {Zone::Grid, Control::Tiles, "##facegrid", "ImGuiListClipper; a click selects"},
        {Zone::Selection, Control::WhatToCopy, "##whatcopy", "Head only / Head + eyes + skin / Head + eyes + skin + body type"},
        {Zone::Selection, Control::Apply, "Apply##faces", "applies the selected tile (game heads: the confirmation below)"},
        {Zone::Selection, Control::HisOwnHead, "His own head", "brings back the head he had; next to Apply"},
        {Zone::Selection, Control::Close, "Close", "always visible"},
    };
    return kEntries;
}

inline int zone_count(Zone z) {
    int n = 0;
    for (const Entry& e : entries()) n += e.zone == z ? 1 : 0;
    return n;
}
// The Tab order of the controls outside the popover
inline std::vector<Control> tab_order() {
    std::vector<Control> out;
    for (const Entry& e : entries())
        if (e.zone != Zone::Popover) out.push_back(e.control);
    return out;
}
// true when the Filters popover lists this widget: an exact label, a label of an alternatives entry ("a|b"), or a facet button
inline bool popover_lists(const char* imgui_label) {
    const std::string want = imgui_label;
    for (const Entry& e : entries()) {
        if (e.zone != Zone::Popover) continue;
        const std::string have = e.label;
        if (e.control == Control::Facets) {
            if (want.find(have) != std::string::npos) return true;
            continue;
        }
        size_t from = 0;
        while (true) {
            const size_t bar = have.find('|', from);
            const std::string part = have.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
            if (part == want) return true;
            if (bar == std::string::npos) break;
            from = bar + 1;
        }
    }
    return false;
}

}  // namespace turbo::chooser_layout
