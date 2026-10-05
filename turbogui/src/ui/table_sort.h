// FC 27 LE Turbo GUI - click-to-sort column headers for the list tables (teams, squad, managers, club pickers, league table).
// Use: give the table ImGuiTableFlags_Sortable | SortTristate (table_sort_flags), build the row list in its default order, then
//   table_sort(rows, [&](const Row& a, const Row& b, int column) { switch (column) { case 0: return cmp_num(a.id, b.id); ... } });
// before drawing it. The sort is stable and only active while a header is clicked (a third click returns to the default
// order), so the list keeps its own order until the user asks for another. Rows are copied or pointed to by the caller,
// so ScrollY and ImGuiListClipper work as before.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "imgui.h"

namespace turbo {

constexpr ImGuiTableFlags table_sort_flags() { return ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate; }

// Comparators for the callback: negative / 0 / positive
template <class T>
int cmp_num(const T& a, const T& b) {
    return a < b ? -1 : (b < a ? 1 : 0);
}
inline int cmp_text(const std::string& a, const std::string& b) {  // case-insensitive
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int x = std::tolower(static_cast<unsigned char>(a[i])), y = std::tolower(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
}

// Sorts `rows` by the open table's sort specs (call between TableHeadersRow and the first row). cmp(a, b, column) compares
// two rows on one column. Nothing happens while no header is sorted.
template <class Row, class Cmp>
void table_sort(std::vector<Row>& rows, Cmp cmp) {
    ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (!specs) return;
    specs->SpecsDirty = false;
    if (specs->SpecsCount <= 0 || rows.size() < 2) return;
    const ImGuiTableColumnSortSpecs* sp = specs->Specs;
    const int n = specs->SpecsCount;
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        for (int i = 0; i < n; ++i) {
            const int c = cmp(a, b, static_cast<int>(sp[i].ColumnIndex));
            if (c != 0) return sp[i].SortDirection == ImGuiSortDirection_Ascending ? c < 0 : c > 0;
        }
        return false;
    });
}

}  // namespace turbo
