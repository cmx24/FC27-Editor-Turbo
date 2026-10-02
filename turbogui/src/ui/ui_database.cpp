// FC 27 LE Turbo GUI - Database tab: browse and edit any table of the live database.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "imgui.h"

namespace turbo {

struct DbView {
    std::string table;
    int gen = -1;
    Snapshot snap;
    std::vector<std::string> fields;
    std::vector<uint32_t> rows;  // filtered snapshot indexes
    char filter_field[64] = "";
    char filter_value[64] = "";
    std::string applied_filter;
    uint64_t edit_rec = 0;
    std::string edit_field;
};
static DbView g_view;

static void reload_view(App& app, const Table& t) {
    g_view.table = t.name;
    g_view.gen = app.gen;
    g_view.snap.load(app.db.memory(), t);
    g_view.fields = t.field_names();
    // Primary key-ish fields first
    for (const char* k : {"teamid", "playerid", "artificialkey"}) {
        auto it = std::find(g_view.fields.begin(), g_view.fields.end(), k);
        if (it != g_view.fields.end()) {
            g_view.fields.erase(it);
            g_view.fields.insert(g_view.fields.begin(), k);
        }
    }
    g_view.rows.clear();
    std::string ff = g_view.filter_field, fv = g_view.filter_value;
    const Field* f = ff.empty() ? nullptr : t.field(ff);
    for (uint32_t idx : g_view.snap.valid) {
        if (f && !fv.empty()) {
            Value v = g_view.snap.get(idx, *f);
            if (f->type == FieldType::String) {
                std::string a = v.s, b = fv;
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                if (a.find(b) == std::string::npos) continue;
            } else if (f->type == FieldType::Int) {
                if (v.i != std::atoll(fv.c_str())) continue;
            } else if (f->type == FieldType::Float) {
                if (std::abs(v.f - static_cast<float>(std::atof(fv.c_str()))) > 1e-4f) continue;
            }
        }
        g_view.rows.push_back(idx);
    }
    g_view.applied_filter = ff + "=" + fv;
}

void draw_database(App& app) {
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    auto names = app.db.table_names();
    if (app.db_table.empty() && !names.empty()) app.db_table = std::find(names.begin(), names.end(), "players") != names.end() ? "players" : names[0];

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("Table", app.db_table.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (const auto& n : names) {
            if (ImGui::Selectable(n.c_str(), n == app.db_table)) {
                app.db_table = n;
                g_view.filter_field[0] = 0;
                g_view.filter_value[0] = 0;
            }
        }
        ImGui::EndCombo();
    }
    const Table* t = app.db.table(app.db_table);
    if (!t) return;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##ff", "field", g_view.filter_field, sizeof(g_view.filter_field));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputTextWithHint("##fv", "value", g_view.filter_value, sizeof(g_view.filter_value));
    ImGui::SameLine();
    bool reload = ImGui::Button("Apply / Reload");
    std::string cur_filter = std::string(g_view.filter_field) + "=" + g_view.filter_value;
    if (reload || g_view.table != t->name || g_view.gen != app.gen) reload_view(app, *t);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu rows, %zu fields%s. Double-click a cell to edit.", g_view.rows.size(),
                        g_view.snap.valid.size(), g_view.fields.size(),
                        cur_filter != g_view.applied_filter ? " (filter not applied)" : "");

    int ncols = static_cast<int>(std::min<size_t>(g_view.fields.size(), 500));
    if (ncols == 0) return;
    ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                         ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    bool open_edit = false;
    if (ImGui::BeginTable("##dbt", ncols, fl, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        for (int c = 0; c < ncols; ++c) ImGui::TableSetupColumn(g_view.fields[static_cast<size_t>(c)].c_str());
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_view.rows.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                uint32_t idx = g_view.rows[static_cast<size_t>(r)];
                ImGui::TableNextRow();
                for (int c = 0; c < ncols; ++c) {
                    ImGui::TableNextColumn();
                    const Field* f = t->field(g_view.fields[static_cast<size_t>(c)]);
                    std::string text = g_view.snap.get(idx, *f).to_string();
                    ImGui::PushID(r * 1024 + c);
                    if (ImGui::Selectable(text.empty() ? " " : text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        g_view.edit_rec = g_view.snap.addr(idx);
                        g_view.edit_field = f->name;
                        open_edit = true;
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndTable();
    }
    if (open_edit) ImGui::OpenPopup("##dbedit");
    if (ImGui::BeginPopup("##dbedit")) {
        const Field* ef = t->field(g_view.edit_field);
        if (ef && g_view.edit_rec) {
            ImGui::Text("%s.%s", t->name.c_str(), ef->name.c_str());
            if (field_editor(app, *t, g_view.edit_rec, *ef, nullptr, 220.0f)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace turbo
