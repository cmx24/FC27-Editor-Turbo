// FC 27 LE Turbo GUI - field editors shared by every panel.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "app.h"
#include "geo.h"
#include "core/field_labels.h"
#include "core/range_filter.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui_bodytypes.h"
#include "ui_images.h"

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

static void range_tooltip(const Table& t, const Field& f) {
    if (!ImGui::IsItemHovered()) return;
    if (t.name == "players" && f.name == "overallrating" && f.type == FieldType::Int)
        // App::edit routes a typed overall through App::set_overall
        ImGui::SetTooltip("%s  [1 .. 99]\nFC 27 works the overall out from the attributes (the player card, its own growth): a bare\n"
                          "overall does not last. So a typed overall moves the attributes that weigh most for his position until the\n"
                          "formula gives it (as Overall +1 / -1 in the Attributes tab). Undo / Undo bulk put them back.",
                          f.name.c_str());
    else if (f.type == FieldType::Int)
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
        range_tooltip(t, f);
    } else if (f.type == FieldType::Int) {
        long long tmp = cur.i;
        long long* p = mine ? &g_edit.i : &tmp;
        ImGui::InputScalar("##v", ImGuiDataType_S64, p);
        range_tooltip(t, f);
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
        range_tooltip(t, f);
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
        range_tooltip(t, f);
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
        range_tooltip(t, f);
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
    range_tooltip(t, f);
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

// the text shown for a code value: never empty, never a bare number
std::string code_value_text(const std::string& field, int64_t v, const labels::CodeUsers* live) {
    if (labels::enum_def(field)) {
        if (const char* l = enum_label(field, v)) return l;
        return labels::code_n(v);
    }
    return labels::code_text(field, v, field_label(field), live);
}

// Values of the loaded tables for the code combos: one snapshot per table and model version (taken the first time a
// combo asks), each field counted when first asked
struct InUseCache {
    uint64_t version = ~uint64_t(0);
    int gen = -1;
    std::map<std::string, Snapshot> snaps;                                // table name ->
    std::map<std::string, std::map<int64_t, labels::CodeUsers>> fields;  // "<table>.<field>" ->
};
static InUseCache g_in_use;

const std::map<int64_t, labels::CodeUsers>& values_in_use(App& app, const Table& t, const Field& f) {
    InUseCache& c = g_in_use;
    if (c.version != app.model.version() || c.gen != app.gen) {
        c = InUseCache();
        c.version = app.model.version();
        c.gen = app.gen;
    }
    const std::string key = t.name + "." + f.name;
    if (auto it = c.fields.find(key); it != c.fields.end()) return it->second;
    std::map<int64_t, labels::CodeUsers>& out = c.fields[key];
    if (f.type != FieldType::Int) return out;
    auto sit = c.snaps.find(t.name);
    if (sit == c.snaps.end()) {
        Snapshot s;
        if (!s.load(app.db.memory(), t)) return out;
        sit = c.snaps.emplace(t.name, std::move(s)).first;
    }
    const Snapshot& s = sit->second;
    // players: the three best-rated holders of each value (overall descending, then player id)
    const Field* pid = t.name == "players" ? t.field("playerid") : nullptr;
    const Field* ovr = pid ? t.field("overallrating") : nullptr;
    std::map<int64_t, std::vector<std::pair<int64_t, int64_t>>> best;
    for (uint32_t idx : s.valid) {
        const int64_t v = s.get_int(idx, f);
        ++out[v].players;
        if (!pid || !ovr) continue;
        std::vector<std::pair<int64_t, int64_t>>& b = best[v];
        const std::pair<int64_t, int64_t> e{-s.get_int(idx, *ovr), s.get_int(idx, *pid)};
        if (b.size() < 3) b.push_back(e);
        else if (e < b.back()) b.back() = e;
        else continue;
        std::sort(b.begin(), b.end());
    }
    for (const auto& kv : best) {
        std::string names;
        for (const auto& e : kv.second)
            if (const PlayerRow* p = app.model.player(e.second)) names += (names.empty() ? "" : ", ") + p->name;
        out[kv.first].names = names;
    }
    return out;
}

// who uses a player-specific code the generated tables do not know (nullptr for any other code)
static const labels::CodeUsers* live_users(App& app, const Table& t, const Field& f, int64_t v) {
    if (!labels::is_signature_field(f.name) || labels::find_users(f.name, v)) return nullptr;
    if (const labels::NamedCodes* n = labels::named_codes(f.name); n && labels::find_name(*n, v)) return nullptr;
    const auto& m = values_in_use(app, t, f);
    auto it = m.find(v);
    return it == m.end() ? nullptr : &it->second;
}

std::vector<int64_t> code_value_list(App& app, const Table& t, const Field& f, int64_t current) {
    std::vector<int64_t> codes;
    if (const labels::EnumDef* d = labels::enum_def(f.name)) {
        for (size_t k = 0; k < d->labels.size(); ++k) codes.push_back(d->base + static_cast<int64_t>(k));
        if (std::find(codes.begin(), codes.end(), current) == codes.end()) codes.insert(codes.begin(), current);
        return codes;
    }
    // the codes the loaded table uses and the game's pictures (galleries), besides the ones field_labels.h knows
    std::vector<int64_t> more = gallery_codes(app, f.name);
    const bool pictures = !more.empty();
    for (const auto& kv : values_in_use(app, t, f)) more.push_back(kv.first);
    // a small field nothing names: its whole range, so every value can be picked
    if (!labels::named_codes(f.name) && labels::known_codes(f.name).empty() && !pictures && f.max() - f.min < 128)
        for (int64_t v = f.min; v <= f.max(); ++v) more.push_back(v);
    for (int64_t v : labels::offered_codes(f.name, more, current))
        if ((v >= f.min && v <= f.max()) || v == current) codes.push_back(v);
    return codes;
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
    const std::string shown = code_value_text(f.name, cur.i, live_users(app, t, f, cur.i));
    if (ImGui::BeginCombo("##e", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        // the codes offered: the named ones (stars, roles, colours...), the ones the data describes (unique body types,
        // boots...), the codes the loaded table uses and the game's pictures; the current value always
        std::vector<int64_t> codes = code_value_list(app, t, f, cur.i);
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
        for (int64_t v : codes) {
            std::string txt = code_value_text(f.name, v, live_users(app, t, f, v));
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

// ---------------------------------------------------------------- form rows: label left, control right
static float g_form_w = 0.0f;  // 0 = default

void form_label_width(float w) { g_form_w = w; }
float form_label_width() { return g_form_w > 0.0f ? g_form_w : S(150.0f); }

void form_label(const char* label, const char* tip) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(form_label_width());
}

// ImGui::Checkbox (imgui_widgets.cpp 1.92) with the box drawn at the right end of the row: the ID, the item flags the test
// engine sees and the click behaviour are the same, only the layout differs.
bool checkbox_right(const char* label, bool* v, float label_w) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const char* label_end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, label_end, false);
    const float square_sz = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    // the box sits at the end of the label column, further right when the label is longer than the column (or tight after the text)
    const float col = label_w < 0.0f ? 0.0f : (label_w > 0.0f ? label_w : form_label_width()) - style.ItemSpacing.x;
    const float box_x = std::max(col, label_size.x > 0.0f ? label_size.x + style.ItemInnerSpacing.x : 0.0f);
    const ImRect total_bb(pos, ImVec2(pos.x + box_x + square_sz, pos.y + std::max(label_size.y + style.FramePadding.y * 2.0f, square_sz)));
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    const bool is_visible = ImGui::ItemAdd(total_bb, id);
    if (!is_visible) {
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
        return false;
    }
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !*v;
        ImGui::MarkItemEdited(id);
    }
    const ImRect check_bb(ImVec2(pos.x + box_x, pos.y), ImVec2(pos.x + box_x + square_sz, pos.y + square_sz));
    ImGui::RenderNavCursor(total_bb, id);
    const ImU32 bg_col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : *v ? ImGuiCol_CheckboxSelectedBg : ImGuiCol_FrameBg);
    ImGui::RenderFrame(check_bb.Min, check_bb.Max, bg_col, true, style.FrameRounding);
    if (*v) {
        const float pad = std::max(1.0f, IM_TRUNC(square_sz / 6.0f));
        ImGui::RenderCheckMark(window->DrawList, ImVec2(check_bb.Min.x + pad, check_bb.Min.y + pad), ImGui::GetColorU32(ImGuiCol_CheckMark), square_sz - pad * 2.0f);
    }
    if (label_size.x > 0.0f) ImGui::RenderText(ImVec2(pos.x, pos.y + style.FramePadding.y), label, label_end);
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
    return pressed;
}

bool range_filter_row(const char* label, const char* id, int& from, int& to, int hi, float box_w, const char* tip) {
    form_label(label, tip);
    char fid[64], tid[64];
    std::snprintf(fid, sizeof(fid), "##%s_from", id);
    std::snprintf(tid, sizeof(tid), "##%s_to", id);
    const int f0 = from, t0 = to;
    ImGui::SetNextItemWidth(box_w);
    ImGui::InputInt(fid, &from, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s from (0 = no lower bound)", label);
    ImGui::SameLine(0.0f, S(3.0f));
    ImGui::TextDisabled("to");
    ImGui::SameLine(0.0f, S(3.0f));
    ImGui::SetNextItemWidth(box_w);
    ImGui::InputInt(tid, &to, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s to (0 = no upper bound)", label);
    range_normalize(from, to, hi, to != t0);
    return from != f0 || to != t0;
}

bool& position_show_all() {
    static bool all = false;
    return all;
}

bool position_combo(const char* id, int& pos, float width, const char* none_label, int lo, int hi) {
    if (hi < 0) hi = position_count() - 1;
    bool picked = false;
    const std::string cur = pos < 0 && none_label ? none_label : position_descriptor(pos);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, cur.c_str(), ImGuiComboFlags_HeightLargest)) {
        if (none_label && ImGui::Selectable(none_label, pos < 0) && pos >= 0) {
            pos = -1;
            picked = true;
        }
        bool& all = position_show_all();
        std::vector<int> codes;
        if (all) {
            for (int p = lo; p <= hi && p < position_count(); ++p) codes.push_back(p);
        } else {
            for (int p : visible_positions())
                if (p >= lo && p <= hi) codes.push_back(p);
            // the stored value, when internal, stays selectable (shown with its descriptor)
            if (pos >= 0 && std::find(codes.begin(), codes.end(), pos) == codes.end()) codes.push_back(pos);
        }
        for (int p : codes) {
            const std::string txt = position_visible(p) ? std::string(position_name(p)) : position_descriptor(p);
            if (ImGui::Selectable(txt.c_str(), pos == p) && pos != p) {
                pos = p;
                picked = true;
            }
            if (pos == p && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        }
        ImGui::Separator();
        ImGui::Checkbox("Show all internal positions", &all);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The 16 codes the game stores but never names in its menus (SW, RWB, LWB, RCB, LCB, RDM, LDM, RCM, LCM, RAM, LAM, RF, CF, LF, RS, LS)");
        ImGui::EndCombo();
    }
    return picked;
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
    if (ImGui::BeginTable(id, columns, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
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
    if (ImGui::BeginTable("##all", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable,
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
