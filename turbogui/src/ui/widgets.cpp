// FC 27 LE Turbo GUI - field editors shared by every panel.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "app.h"
#include "core/field_labels.h"
#include "core/hair_catalog.h"
#include "imgui.h"

namespace turbo {

static const std::map<std::string, std::string>& label_map() {
    static const std::map<std::string, std::string> m = {
        {"overallrating", "Overall"}, {"potential", "Potential"}, {"modifier", "OVR modifier"}, {"acceleration", "Acceleration"},
        {"sprintspeed", "Sprint Speed"}, {"positioning", "Att. Position"}, {"finishing", "Finishing"},
        {"shotpower", "Shot Power"}, {"longshots", "Long Shots"}, {"volleys", "Volleys"}, {"penalties", "Penalties"},
        {"vision", "Vision"}, {"crossing", "Crossing"}, {"freekickaccuracy", "FK Accuracy"},
        {"shortpassing", "Short Passing"}, {"longpassing", "Long Passing"}, {"curve", "Curve"}, {"agility", "Agility"},
        {"balance", "Balance"}, {"reactions", "Reactions"}, {"ballcontrol", "Ball Control"}, {"dribbling", "Dribbling"},
        {"composure", "Composure"}, {"interceptions", "Interceptions"}, {"headingaccuracy", "Heading Acc."},
        {"defensiveawareness", "Def. Awareness"}, {"marking", "Marking"}, {"standingtackle", "Standing Tackle"},
        {"slidingtackle", "Sliding Tackle"}, {"jumping", "Jumping"}, {"stamina", "Stamina"}, {"strength", "Strength"},
        {"aggression", "Aggression"}, {"gkdiving", "GK Diving"}, {"gkhandling", "GK Handling"},
        {"gkkicking", "GK Kicking"}, {"gkpositioning", "GK Positioning"}, {"gkreflexes", "GK Reflexes"},
        {"preferredfoot", "Preferred Foot"}, {"weakfootabilitytypecode", "Weak Foot"}, {"skillmoves", "Skill Moves"},
        {"attackingworkrate", "Att. Work Rate"}, {"defensiveworkrate", "Def. Work Rate"}, {"height", "Height (cm)"},
        {"weight", "Weight (kg)"}, {"nationality", "Nationality ID"}, {"birthdate", "Birth Date"},
        {"contractvaliduntil", "Contract Until"}, {"wage", "Wage"}, {"releaseclause", "Release Clause"}, {"isretiring", "Retiring"}, {"playerjointeamdate", "Joined Club"},
        {"internationalrep", "Int. Reputation"}, {"teamname", "Team Name"}, {"jerseynumber", "Jersey"},
        {"headassetid", "Head Asset ID"}, {"hashighqualityhead", "Real Face"}, {"headclasscode", "Head Class"},
        {"trait1", "PlayStyles"}, {"icontrait1", "PlayStyles+"}, {"trait2", "Traits"}, {"icontrait2", "Traits+"},
        {"homewins", "Home wins"}, {"awaywins", "Away wins"}, {"homedraws", "Home draws"}, {"awaydraws", "Away draws"},
        {"homelosses", "Home losses"}, {"awaylosses", "Away losses"}, {"homegf", "Home goals for"},
        {"awaygf", "Away goals for"}, {"homega", "Home goals against"}, {"awayga", "Away goals against"},
        {"points", "Points"}, {"nummatchesplayed", "Played"}, {"currenttableposition", "Table position"},
        {"teamform", "Form"}, {"lastgameresult", "Last result"},
        {"firstnameid", "First name ID"}, {"lastnameid", "Last name ID"}, {"commonnameid", "Common name ID"},
        {"playerjerseynameid", "Jersey name ID"}, {"bodytypecode", "Body type"}, {"gender", "Gender"},
        {"skillmoveslikelihood", "Skill moves likelihood"}, {"gkkickstyle", "GK kick style"}, {"runstylecode", "Run style"},
        {"socklengthcode", "Sock length"}, {"sockstylecode", "Sock style"}, {"shoetypecode", "Boots"}, {"shoecolorcode1", "Boot colour 1"},
        {"shoecolorcode2", "Boot colour 2"}, {"shoedesigncode", "Boot design"}, {"gkglovetypecode", "GK gloves"},
        {"hairtypecode", "Hair"}, {"haircolorcode", "Hair colour"}, {"hairstylecode", "Hair style"},
        {"facialhairtypecode", "Facial hair"}, {"facialhaircolorcode", "Facial hair colour"}, {"eyecolorcode", "Eye colour"},
        {"skintonecode", "Skin tone"}, {"headtypecode", "Head type"}, {"jerseyfit", "Jersey fit"},
        {"jerseysleevelengthcode", "Sleeves"}, {"jerseystylecode", "Jersey style"}, {"shortstyle", "Shorts"},
        {"growthprofile", "Growth profile"}, {"emotion", "Emotion"}, {"personality", "Personality"},
    };
    return m;
}

std::string field_label(const std::string& field) {
    auto it = label_map().find(field);
    if (it != label_map().end()) return it->second;
    std::string out = field;
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// Editing state for the one field that has keyboard focus. While a field is active its value lives
// here instead of being re-read from game memory each frame.
struct ActiveEdit {
    ImGuiID id = 0;
    long long i = 0;
    float f = 0.0f;
    char s[512] = {0};
};
static ActiveEdit g_edit;

static void range_tooltip(const Field& f) {
    if (!ImGui::IsItemHovered()) return;
    if (f.type == FieldType::Int)
        ImGui::SetTooltip("%s  [%lld .. %lld]", f.name.c_str(), static_cast<long long>(f.min), static_cast<long long>(f.max()));
    else if (f.type == FieldType::String)
        ImGui::SetTooltip("%s  [text, max %d bytes]", f.name.c_str(), static_cast<int>(f.max_len()) - 1);
    else
        ImGui::SetTooltip("%s  [%s]", f.name.c_str(), f.type_name());
}

bool field_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width) {
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("year / month / day  (stored as %s = %lld)", f.name.c_str(), static_cast<long long>(cur.i));
    if (ImGui::IsItemActivated()) {
        active = id;
        std::memcpy(buf, p, sizeof(buf));
    }
    if (ImGui::IsItemDeactivated() && active == id) {
        active = 0;
        GameDate nd{buf[0], buf[1], buf[2]};
        if (ImGui::IsItemDeactivatedAfterEdit() && nd.as_int() != d.as_int()) {
            if (!is_real_date(nd)) {
                app.notify(f.name + ": not a valid date", true);
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

// Readable labels for small enumerated fields Live Editor's localization does not name (the ones it names - emotion,
// body type, colours, jersey/sock/short styles, gender, foot, head class, roles, accessories - live in core/field_labels.h)
struct EnumDef {
    const char* field;
    int64_t base;  // value of labels[0]
    std::vector<const char*> labels;
};
static const std::vector<EnumDef>& enum_defs() {
    static const std::vector<EnumDef> d = {
        {"weakfootabilitytypecode", 1, {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}},
        {"skillmoves", 0, {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}},
        {"internationalrep", 1, {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}},
        {"attackingworkrate", 0, {"Low", "Medium", "High"}},
        {"defensiveworkrate", 0, {"Low", "Medium", "High"}},
        {"gkkickstyle", 0, {"Default", "Power", "Precision", "Mixed"}},
        {"skillmoveslikelihood", 0, {"Low", "Medium", "High", "Very high"}},
        {"personality", 1, {"Neutral", "Maverick", "Heartbeat", "Virtuoso"}},
        {"undershortstyle", 0, {"None", "Visible"}},
        {"shoedesigncode", 0, {"Standard", "Laced", "Laceless", "High-cut"}},
        {"muscularitycode", 0, {"Regular", "Muscular"}},
        {"runstylecode", 0, {"Default", "Short step", "Long step", "Smooth", "Upright", "Hunched", "Bouncy", "Mixed"}},
        {"growthprofile", 0, {"Default", "Early", "Normal", "Late", "Very late"}},
    };
    return d;
}

static const EnumDef* enum_def(const std::string& field) {
    for (const auto& d : enum_defs())
        if (field == d.field) return &d;
    return nullptr;
}

bool is_enum_field(const std::string& field) {
    return enum_def(field) != nullptr || labels::is_code_field(field);
}

const char* enum_label(const std::string& field, int64_t v) {
    if (const EnumDef* d = enum_def(field)) {
        int64_t k = v - d->base;
        if (k >= 0 && k < static_cast<int64_t>(d->labels.size())) return d->labels[static_cast<size_t>(k)];
        return nullptr;
    }
    if (const labels::NamedCodes* t = labels::named_codes(field)) return labels::find_name(*t, v);
    return nullptr;
}

// the text shown for a code value: never a bare number
static std::string code_text(const std::string& field, int64_t v) {
    if (enum_def(field)) {
        if (const char* l = enum_label(field, v)) return l;
        char b[48];
        std::snprintf(b, sizeof(b), "Unknown (code %lld)", static_cast<long long>(v));
        return b;
    }
    if (field == "hairtypecode") {  // the hair catalog's look ("Long, curly, headband") from the game's preview pictures
        char b[96];
        std::snprintf(b, sizeof(b), "%s #%lld", hair::describe(hair::lookup(v)).c_str(), static_cast<long long>(v));
        return b;
    }
    return labels::code_text(field, v, field_label(field));
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
        if (const EnumDef* d = enum_def(f.name)) {
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
            ImGui::PushID(static_cast<int>(v));
            if (ImGui::Selectable(txt.c_str(), v == cur.i) && v != cur.i) wrote = app.edit(t, rec, f, Value::of_int(v));
            if (v == cur.i && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("code %lld", static_cast<long long>(v));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s = code %lld  [%lld .. %lld]", f.name.c_str(), static_cast<long long>(cur.i), static_cast<long long>(f.min),
                          static_cast<long long>(f.max()));
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
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            if (f->type == FieldType::Int)
                ImGui::TextDisabled("%lld..%lld", static_cast<long long>(f->min), static_cast<long long>(f->max()));
            else if (f->type == FieldType::String)
                ImGui::TextDisabled("text (%d)", static_cast<int>(f->max_len()) - 1);
            else
                ImGui::TextDisabled("%s", f->type_name());
            ImGui::TableNextColumn();
            field_editor(app, t, rec, *f, nullptr, -FLT_MIN);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

}  // namespace turbo
