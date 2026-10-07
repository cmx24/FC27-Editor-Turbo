// FC 27 LE Turbo GUI - field editors shared by every panel.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "app.h"
#include "geo.h"
#include "core/field_labels.h"
#include "core/hair_catalog.h"
#include "imgui.h"
#include "ui_bodytypes.h"

namespace turbo {

std::string field_label(const std::string& field) { return labels::field_title(field); }


// Editing state for the one field that has keyboard focus. While a field is active its value lives
// here instead of being re-read from game memory each frame.
struct ActiveEdit {
    ImGuiID id = 0;
    long long i = 0;
    float f = 0.0f;
    char s[512] = {0};
};
static ActiveEdit g_edit;

// The game's own field name goes to a dim second line of the tooltip; the first line says what the box holds
static void tooltip_with_raw(const std::string& text, const std::string& raw) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(text.c_str());
    ImGui::TextDisabled("%s", raw.c_str());
    ImGui::EndTooltip();
}

static void range_tooltip(const Field& f) {
    if (!ImGui::IsItemHovered()) return;
    if (f.type == FieldType::Int)
        tooltip_with_raw(labels::describe_range(f.name, f.min, f.max()), f.name);
    else if (f.type == FieldType::String)
        tooltip_with_raw(labels::field_title(f.name) + " (text, up to " + std::to_string(static_cast<int>(f.max_len()) - 1) + " characters)", f.name);
    else
        tooltip_with_raw(labels::field_title(f.name) + " (" + f.type_name() + ")", f.name);
}

// Pickers by name (nation, club) instead of the number; off in the "All fields" view until "Show friendly names" is ticked
static bool g_pickers = true;
enum class RefKind { None, Nation, Team };
static RefKind ref_kind(App& app, const Table& t, const Field& f);
static bool ref_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);

bool field_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width) {
    if (g_pickers && f.type == FieldType::Int && ref_kind(app, t, f) != RefKind::None) return ref_editor(app, t, rec, f, label, width);
    Value cur;
    if (!app.db.get(t, rec, f, cur)) {
        if (label) ImGui::TextDisabled("%s: unreadable", label);
        else ImGui::TextDisabled("unreadable");
        return false;
    }
    bool wrote = false;
    ImGui::PushID(f.name.c_str());
    if (label && std::strncmp(label, "##", 2) != 0) {  // a "##..." label is hidden, as in ImGui
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(S(150.0f));
    }
    ImGui::SetNextItemWidth(width);
    ImGuiID id = ImGui::GetID("##v");
    bool mine = g_edit.id == id;

    if (f.type == FieldType::Int && f.depth == 1 && f.min == 0) {
        bool b = cur.i != 0;
        if (ImGui::Checkbox("##v", &b)) wrote = app.edit(t, rec, f, Value::of_int(b ? 1 : 0));
        range_tooltip(f);
    } else if (f.type == FieldType::Int) {
        long long tmp = cur.i;
        long long* p = mine ? &g_edit.i : &tmp;
        ImGui::InputScalar("##v", ImGuiDataType_S64, p);
        range_tooltip(f);
        if (ImGui::IsItemActivated()) {
            g_edit.id = id;
            g_edit.i = *p;
        }
        if (ImGui::IsItemDeactivated() && g_edit.id == id) {
            long long v = g_edit.i;
            g_edit.id = 0;
            if (ImGui::IsItemDeactivatedAfterEdit() && v != cur.i) wrote = app.edit(t, rec, f, Value::of_int(v));
        }
    } else if (f.type == FieldType::Float) {
        float tmp = cur.f;
        float* p = mine ? &g_edit.f : &tmp;
        ImGui::InputFloat("##v", p, 0.0f, 0.0f, "%.4f");
        range_tooltip(f);
        if (ImGui::IsItemActivated()) {
            g_edit.id = id;
            g_edit.f = *p;
        }
        if (ImGui::IsItemDeactivated() && g_edit.id == id) {
            float v = g_edit.f;
            g_edit.id = 0;
            if (ImGui::IsItemDeactivatedAfterEdit() && v != cur.f) wrote = app.edit(t, rec, f, Value::of_float(v));
        }
    } else if (f.type == FieldType::String) {
        char tmp[512];
        std::snprintf(tmp, sizeof(tmp), "%s", cur.s.c_str());
        char* p = mine ? g_edit.s : tmp;
        size_t cap = std::min<size_t>(sizeof(tmp), f.max_len() > 0 ? f.max_len() : 1);
        ImGui::InputText("##v", p, cap);
        range_tooltip(f);
        if (ImGui::IsItemActivated()) {
            g_edit.id = id;
            std::snprintf(g_edit.s, sizeof(g_edit.s), "%s", p);
        }
        if (ImGui::IsItemDeactivated() && g_edit.id == id) {
            std::string v = g_edit.s;
            g_edit.id = 0;
            if (ImGui::IsItemDeactivatedAfterEdit() && v != cur.s) wrote = app.edit(t, rec, f, Value::of_str(v));
        }
    } else {
        ImGui::TextDisabled("%s", cur.to_string().c_str());  // compressed text (type 13) or unknown: read-only
        range_tooltip(f);
    }
    ImGui::PopID();
    return wrote;
}

bool date_field_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label) {
    Value cur;
    if (!app.db.get(t, rec, f, cur) || cur.type != FieldType::Int) return false;
    GameDate d = date_from_gregorian_days(cur.i);
    bool wrote = false;
    ImGui::PushID(f.name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(S(150.0f));
    int ymd[3] = {d.year, d.month, d.day};
    ImGuiID id = ImGui::GetID("##date");
    static ImGuiID active = 0;
    static int buf[3] = {0, 0, 0};
    int* p = active == id ? buf : ymd;
    ImGui::SetNextItemWidth(S(170.0f));
    ImGui::InputInt3("##date", p);
    if (ImGui::IsItemHovered()) tooltip_with_raw("Year / month / day. The game stores it as a day count.", f.name);
    if (ImGui::IsItemActivated()) {
        active = id;
        std::memcpy(buf, p, sizeof(buf));
    }
    if (ImGui::IsItemDeactivated() && active == id) {
        active = 0;
        GameDate nd{buf[0], buf[1], buf[2]};
        if (ImGui::IsItemDeactivatedAfterEdit() && nd.as_int() != d.as_int()) {
            if (!is_real_date(nd)) {
                app.notify(labels::field_title(f.name) + ": not a valid date", true);
            } else {
                wrote = app.edit(t, rec, f, Value::of_int(gregorian_days_from_date(nd)));
            }
        }
    }
    if (f.name == "birthdate") {
        ImGui::SameLine();
        int age = age_on(d, app.today());
        if (age >= 0) ImGui::TextDisabled("age %d", age);
    }
    ImGui::PopID();
    return wrote;
}

bool slider_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width) {
    return slider_editor_ex(app, t, rec, f, label, S(150.0f), width, S(52.0f));
}

bool slider_editor_ex(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float label_w, float width,
                      float box_w) {
    Value cur;
    if (!app.db.get(t, rec, f, cur) || f.type != FieldType::Int || f.depth > 30) return field_editor(app, t, rec, f, label, width);
    bool wrote = false;
    ImGui::PushID(f.name.c_str());
    if (label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(label_w);
    }
    ImGui::SetNextItemWidth(width < 0.0f ? -(box_w + ImGui::GetStyle().ItemSpacing.x + S(8.0f)) : width);
    ImGuiID id = ImGui::GetID("##s");
    bool mine = g_edit.id == id;
    int tmp = static_cast<int>(cur.i);
    int tmpe = static_cast<int>(g_edit.i);
    int* p = mine ? &tmpe : &tmp;
    // the slider itself stays inside the field's range; a value typed with Ctrl+click is checked by Database::set
    ImGui::SliderInt("##s", p, static_cast<int>(f.min), static_cast<int>(f.max()), "%d");
    range_tooltip(f);
    if (ImGui::IsItemActivated()) {
        g_edit.id = id;
        g_edit.i = *p;
    }
    if (mine) g_edit.i = *p;
    if (ImGui::IsItemDeactivated() && g_edit.id == id) {
        long long v = g_edit.i;
        g_edit.id = 0;
        if (ImGui::IsItemDeactivatedAfterEdit() && v != cur.i) wrote = app.edit(t, rec, f, Value::of_int(v));
    }
    ImGui::PopID();
    // a small number box next to the slider (typed values go through the same range check)
    ImGui::SameLine();
    if (field_editor(app, t, rec, f, nullptr, box_w)) wrote = true;
    return wrote;
}

// Readable labels for enumerated fields: the small tables Live Editor's localization does not name (stars, work rates...)
// and the named codes (emotion, body type, colours, styles, roles, accessories...) both live in core/field_labels.h
bool is_enum_field(const std::string& field) {
    return labels::enum_def(field) != nullptr || labels::is_code_field(field);
}

const char* enum_label(const std::string& field, int64_t v) { return labels::enum_label(field, v); }

// The text shown for a code value: never a bare number. Hair and facial hair styles keep their style number (a combo of
// hundreds of styles has many with the same look, and the number is what the search takes).
static std::string code_text(const std::string& field, int64_t v) {
    if (field == "bodytypecode") return bodytype_label(v);  // Live Editor / field_labels.h names, else "Specific body #N"
    if (field == "hairtypecode" || field == "facialhairtypecode")
        return labels::describe("players", field, v) + " (style " + std::to_string(v) + ")";
    return labels::describe("players", field, v);
}

static bool contains_ci(const std::string& hay, const char* needle) {
    if (!needle[0]) return true;
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

bool enum_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width) {
    if (!is_enum_field(f.name) || f.type != FieldType::Int) return field_editor(app, t, rec, f, label, width);
    Value cur;
    if (!app.db.get(t, rec, f, cur)) return false;
    bool wrote = false;
    ImGui::PushID(f.name.c_str());
    if (label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(S(150.0f));
    }
    ImGui::SetNextItemWidth(width);
    const std::string shown = code_text(f.name, cur.i);
    if (ImGui::BeginCombo("##e", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        // the codes offered: the named ones (stars, roles, colours...) or, for item codes without names, the whole range
        std::vector<int64_t> codes;
        if (const labels::EnumDef* d = labels::enum_def(f.name)) {
            for (size_t k = 0; k < d->labels.size(); ++k) codes.push_back(d->base + static_cast<int64_t>(k));
        } else if (const labels::NamedCodes* nt = labels::named_codes(f.name)) {
            for (const auto& e : *nt) codes.push_back(e.first);
        } else {
            for (int64_t v = f.min; v <= f.max() && v < f.min + 4096; ++v) codes.push_back(v);
        }
        static char filt[48] = "";
        if (ImGui::IsWindowAppearing()) filt[0] = 0;
        const bool many = codes.size() > 12;
        if (many) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##flt", "search (name or code)", filt, sizeof(filt));
        }
        // a typed number reaches any code in the field's range, listed or not
        char* endp = nullptr;
        long long typed = filt[0] ? std::strtoll(filt, &endp, 10) : 0;
        bool is_num = filt[0] && endp && *endp == 0;
        if (is_num && typed >= f.min && typed <= f.max() && std::find(codes.begin(), codes.end(), typed) == codes.end())
            codes.insert(codes.begin(), typed);
        if (std::find(codes.begin(), codes.end(), cur.i) == codes.end()) codes.insert(codes.begin(), cur.i);
        for (int64_t v : codes) {
            if (v < f.min || v > f.max()) continue;
            std::string txt = code_text(f.name, v);
            char num[24];
            std::snprintf(num, sizeof(num), "%lld", static_cast<long long>(v));
            if (filt[0] && !contains_ci(txt, filt) && std::strcmp(num, filt) != 0) continue;
            const std::string item = txt + "##" + num;  // "Left##2": the code is the item's ID
            if (ImGui::Selectable(item.c_str(), v == cur.i) && v != cur.i) wrote = app.edit(t, rec, f, Value::of_int(v));
            if (v == cur.i && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())  // "Body type: Tall and Normal", the game's field name and value on a dim second line
        tooltip_with_raw(labels::field_title(f.name) + ": " + shown,
                         f.name + " = " + std::to_string(cur.i) + " (" + std::to_string(f.min) + "-" + std::to_string(f.max()) + ")");
    ImGui::PopID();
    return wrote;
}

// ---- pickers by name: a nation or a club is chosen from a searchable list, not typed as a number. Without the names (the
// nations table not readable, the lists not built) the plain number box stays.
static RefKind ref_kind(App& app, const Table& t, const Field& f) {
    if (f.type != FieldType::Int) return RefKind::None;
    if (f.name == "nationality" && (t.name == "players" || t.name == "manager"))
        return geo(app).nations.empty() ? RefKind::None : RefKind::Nation;
    if ((f.name == "rivalteam" && t.name == "teams") || (f.name == "teamid" && t.name == "manager"))
        return app.model.teams().empty() ? RefKind::None : RefKind::Team;
    return RefKind::None;
}

static std::string ref_name(App& app, RefKind kind, int64_t id) {
    if (kind == RefKind::Nation) {
        const std::string n = geo(app).nation_name(id);
        return n.empty() ? "Nation " + std::to_string(id) : n;
    }
    return app.model.team_name(id);  // "Team N" for a club the lists do not know
}

static bool ref_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width) {
    Value cur;
    if (!app.db.get(t, rec, f, cur)) {
        if (label) ImGui::TextDisabled("%s: unreadable", label);
        else ImGui::TextDisabled("unreadable");
        return false;
    }
    const RefKind kind = ref_kind(app, t, f);
    bool wrote = false;
    ImGui::PushID(f.name.c_str());
    if (label && std::strncmp(label, "##", 2) != 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(S(150.0f));
    }
    ImGui::SetNextItemWidth(std::max(width, S(170.0f)));
    const std::string shown = ref_name(app, kind, cur.i);
    if (ImGui::BeginCombo("##ref", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        struct Item {
            int64_t id;
            std::string name;
        };
        std::vector<Item> items;
        if (kind == RefKind::Nation)
            for (const GeoNation* n : geo(app).sorted_nations()) items.push_back({n->id, n->name});
        else {
            for (const TeamRow& tr : app.model.teams()) items.push_back({tr.teamid, tr.name});
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.name < b.name; });
        }
        static char filt[48] = "";
        if (ImGui::IsWindowAppearing()) {
            filt[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##flt", kind == RefKind::Nation ? "search nations" : "search clubs", filt, sizeof(filt));
        bool listed = false;
        for (const Item& it : items) listed = listed || it.id == cur.i;
        if (!listed) items.insert(items.begin(), {cur.i, shown});  // a value the list does not know stays selectable
        for (const Item& it : items) {
            if (it.id < f.min || it.id > f.max()) continue;
            if (filt[0] && !contains_ci(it.name, filt) && std::to_string(it.id) != filt) continue;
            const std::string row = it.name + "##" + std::to_string(it.id);
            if (ImGui::Selectable(row.c_str(), it.id == cur.i) && it.id != cur.i) wrote = app.edit(t, rec, f, Value::of_int(it.id));
            if (it.id == cur.i && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s ID %lld", kind == RefKind::Nation ? "Nation" : "Club", static_cast<long long>(it.id));
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) tooltip_with_raw(labels::field_title(f.name) + ": " + shown, f.name + " = " + std::to_string(cur.i));
    ImGui::PopID();
    return wrote;
}

void not_connected_hint() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Not connected to the game database yet. Turbo connects when you enter a career, or at once when you run "
                       "lua\\scripts\\turbo_gui_load.lua in Live Editor's Lua Engine.");
    ImGui::PopStyleColor();
}

void field_grid(App& app, const Table& t, uint64_t rec, const std::vector<std::string>& names, const char* id, int columns) {
    std::vector<const Field*> present;
    for (const auto& n : names) {
        if (const Field* f = t.field(n)) present.push_back(f);
    }
    if (present.empty()) {
        ImGui::TextDisabled("None of these fields exist in FC 27's %s table.", t.name.c_str());
        return;
    }
    if (ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame)) {
        for (const Field* f : present) {
            ImGui::TableNextColumn();
            std::string lbl = field_label(f->name);
            // text fills its column; long numbers (budgets, wages, release clauses) get more room
            float w = f->type == FieldType::String ? -1.0f : (f->depth > 20 ? S(130.0f) : S(90.0f));
            if (is_enum_field(f->name)) enum_editor(app, t, rec, *f, lbl.c_str(), S(130.0f));
            else field_editor(app, t, rec, *f, lbl.c_str(), w);
        }
        ImGui::EndTable();
    }
}

void all_fields(App& app, const Table& t, uint64_t rec, const char* id) {
    ImGui::PushID(id);
    static char filter[64] = "";
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##filter", "filter fields", filter, sizeof(filter));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu fields in %s", t.fields.size(), t.name.c_str());
    // the game's field names and numbers by default (what the other tools of the community use); named values and pickers on request
    static bool friendly = false;
    ImGui::SameLine();
    ImGui::Checkbox("Show friendly names", &friendly);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Titles instead of field names, and names instead of numbers where the game has them.");
    std::string flt = filter;
    std::transform(flt.begin(), flt.end(), flt.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ImGui::BeginTable("##all", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, S(220.0f));
        ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, S(170.0f));
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        for (const auto& name : t.field_names()) {
            if (!flt.empty() && name.find(flt) == std::string::npos) continue;
            const Field* f = t.field(name);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (friendly) {
                ImGui::TextUnformatted(labels::field_title(name).c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
            } else {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::TableNextColumn();
            if (f->type == FieldType::Int)
                ImGui::TextDisabled("%lld..%lld", static_cast<long long>(f->min), static_cast<long long>(f->max()));
            else if (f->type == FieldType::String)
                ImGui::TextDisabled("text (%d)", static_cast<int>(f->max_len()) - 1);
            else
                ImGui::TextDisabled("%s", f->type_name());
            ImGui::TableNextColumn();
            g_pickers = friendly;
            if (friendly && is_enum_field(name) && f->type == FieldType::Int) enum_editor(app, t, rec, *f, nullptr, -FLT_MIN);
            else field_editor(app, t, rec, *f, nullptr, -FLT_MIN);
            g_pickers = true;
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

}  // namespace turbo
