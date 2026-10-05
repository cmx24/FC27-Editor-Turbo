// FC 27 LE Turbo GUI - Players > Names tab: every name the game shows for the player.
//   SHOWN NAMES  his editedplayernames row: first name, last name, common name and shirt name, prefilled with what the
//                game shows now (shown_name). Save names writes only what changed: a player with a row has it edited in
//                place (App::edit: range-checked, one undo step per field, the same route as the Callname tab's
//                assign_name); a player without one gets it from Turbo's Lua side (features/callnames.lua
//                set_display_name, with the room check; never an insert into a full table). An empty common name box
//                means no common name (a common name is never invented); an empty shirt name box takes the last name
//                (callnames.lua jersey_name), so the shirt is never blank. Restore database names sets his row back to
//                the name ids' texts, in place: rows are never deleted (raw deletes bypass the game's indexes).
//   NAME IDS     players.firstnameid / lastnameid / commonnameid / playerjerseynameid, typed or picked by text. A name
//                id also gives the commentary callname (Callname tab).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "ui_names.h"

namespace turbo {

using json = nlohmann::json;

static NamesTabState g_state;
const NamesTabState& names_tab_state() { return g_state; }

static const ImVec4 kOrange(1, 0.6f, 0.3f, 1);
static const char* const kNameFields[4] = {"firstname", "surname", "commonname", "playerjerseyname"};
static const char* const kIdFields[4] = {"firstnameid", "lastnameid", "commonnameid", "playerjerseynameid"};
static const char* const kLabels[4] = {"First name", "Last name", "Common name", "Shirt name"};
static const char* const kBoxIds[4] = {"##nmfirst", "##nmlast", "##nmcommon", "##nmjersey"};
static const char* const kIdLabels[4] = {"First name id", "Last name id", "Common name id", "Shirt name id"};
// When the game shows a changed row: seen in game (docs/callnames.md section 11), a new editedplayernames row brought the
// name back only once the career was reloaded, while a name id change showed at once
static const char* const kWhenShown =
    "Shown at once in Turbo. The game's screens show edited names after the next career reload, if not before.";

ShownName shown_name(App& app, const Table& t, const PlayerRow& p) {
    ShownName n;
    const auto& names = app.model.names_by_id();
    auto name_of = [&](const char* field) {
        int64_t id = app.db.get_int(t, p.rec, field, 0);
        auto it = names.find(id);
        return id > 0 && it != names.end() ? it->second : std::string();
    };
    n.first = name_of("firstnameid");
    n.last = name_of("lastnameid");
    n.common = name_of("commonnameid");
    if (const Table* e = app.db.table("editedplayernames"); e && e->has("playerid")) {
        uint64_t rec = app.db.find(*e, "playerid", p.playerid);
        if (rec) {
            n.edited_rec = rec;
            Value v;
            if (const Field* f = e->field("firstname"); f && app.db.get(*e, rec, *f, v) && !v.to_string().empty()) n.first = v.to_string();
            if (const Field* f = e->field("surname"); f && app.db.get(*e, rec, *f, v) && !v.to_string().empty()) n.last = v.to_string();
            if (const Field* f = e->field("commonname"); f && app.db.get(*e, rec, *f, v) && !v.to_string().empty()) n.common = v.to_string();
            if (const Field* f = e->field("playerjerseyname"); f && app.db.get(*e, rec, *f, v)) n.jersey = v.to_string();
        }
    }
    if (n.jersey.empty() && t.has("playerjerseynameid")) n.jersey = name_of("playerjerseynameid");
    if (n.jersey.empty()) n.jersey = n.last;
    return n;
}

// The command runs at the next career event, and FC 27 reloads playernamemap full (106 of 106 rows) at every career load
void add_room_check(App& app, json& a, uint32_t capacity) {
    a["room"] = true;
    a["capacity"] = capacity;
    if (app.bridge.state().load_gen >= 0) a["load_gen"] = app.bridge.state().load_gen;
}

// The text of one of his name ids ("" for id 0; also "" with *unknown set when the id's text is not loaded)
static std::string id_text(App& app, const Table& t, const PlayerRow& p, const char* field, bool* unknown = nullptr) {
    const int64_t id = t.has(field) ? app.db.get_int(t, p.rec, field, 0) : 0;
    if (id <= 0) return "";
    const auto& names = app.model.names_by_id();
    auto it = names.find(id);
    if (it == names.end()) {
        if (unknown) *unknown = true;
        return "";
    }
    return it->second;
}

// His row's own text of one field ("" when the table lacks it)
static std::string row_text(App& app, const Table& e, uint64_t rec, const char* field) {
    const Field* f = e.field(field);
    Value v;
    return f && app.db.get(e, rec, *f, v) ? v.to_string() : std::string();
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// "" when the text fits the field: valid UTF-8 without control characters, within the field's length
// (Database::validate, the same check every write makes)
static std::string check_text(const Field& f, const char* label, const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(s[i + k]) >> 6) == 2;
        if (!ok) return std::string(label) + ": not valid text";
        if (n == 1 && (c < 0x20 || c == 0x7F)) return std::string(label) + ": control characters are not allowed";
        i += n;
    }
    const std::string err = Database::validate(f, Value::of_str(s));
    if (err.empty()) return "";
    if (f.type == FieldType::String && f.max_len() > 0 && s.size() >= f.max_len())
        return std::string(label) + " is too long: " + std::to_string(s.size()) + " bytes, at most " + std::to_string(f.max_len() - 1);
    return std::string(label) + ": " + err;
}

// The four boxes, the values they were filled with, and whose they are
static char g_box[4][128];
static std::string g_loaded[4];
static int64_t g_pid = 0;
static int g_gen = -1;
static bool g_force = false;  // fill the boxes again next frame (after a write)

static void fill_boxes(const std::string (&v)[4]) {
    for (int i = 0; i < 4; ++i) {
        std::snprintf(g_box[i], sizeof(g_box[i]), "%s", v[i].c_str());
        g_loaded[i] = g_box[i];
    }
}
static bool boxes_are(const std::string (&v)[4]) {
    for (int i = 0; i < 4; ++i)
        if (v[i] != g_box[i]) return false;
    return true;
}

static std::string list_fields(const std::vector<int>& idx) {
    std::string s;
    for (int i : idx) s += (s.empty() ? "" : ", ") + std::string(kLabels[i]);
    return s;
}

// Save names: only what changed. The four values are checked before the first write.
static void save_names(App& app, const Table& t, const PlayerRow& p) {
    g_state.error.clear();
    const Table* e = app.db.table("editedplayernames");
    if (!e || !e->has("playerid")) {
        g_state.error = "This database has no editedplayernames table: only the name ids below can be changed.";
        return;
    }
    std::string v[4];
    for (int i = 0; i < 4; ++i) v[i] = trim(g_box[i]);
    if (v[3].empty()) v[3] = v[1];  // callnames.lua jersey_name: the shirt name is never written empty
    if (v[3].empty()) {
        g_state.error = "The shirt name cannot be empty: type it, or a last name.";
        return;
    }
    for (int i = 0; i < 4; ++i)
        if (const Field* f = e->field(kNameFields[i])) {
            const std::string err = check_text(*f, kLabels[i], v[i]);
            if (!err.empty()) {
                g_state.error = err + ". Nothing written.";
                return;
            }
        }
    const ShownName cur = shown_name(app, t, p);
    if (cur.edited_rec) {
        // his row, in place: the fields whose box changed (and a shirt name the row left empty, never kept blank)
        std::vector<int> todo;
        for (int i = 0; i < 4; ++i) {
            if (!e->has(kNameFields[i])) continue;
            const std::string raw = row_text(app, *e, cur.edited_rec, kNameFields[i]);
            const bool changed = v[i] != g_loaded[i] || (i == 3 && raw.empty());
            if (changed && v[i] != raw) todo.push_back(i);
        }
        if (todo.empty()) {
            app.notify(p.name + ": names unchanged");
            return;
        }
        for (int i : todo)
            if (!app.edit(*e, cur.edited_rec, *e->field(kNameFields[i]), Value::of_str(v[i]))) {
                g_state.error = std::string(kLabels[i]) + " not written (see the message).";
                g_force = true;
                return;
            }
        app.notify(p.name + ": " + list_fields(todo) + " written (editedplayernames). " + kWhenShown);
        g_force = true;
        return;
    }
    // no row: Turbo's Lua side adds it with all four names (an empty one would stay empty), after counting the rows again
    std::vector<int> changed;
    for (int i = 0; i < 4; ++i)
        if (v[i] != g_loaded[i]) changed.push_back(i);
    if (changed.empty()) {
        app.notify(p.name + ": names unchanged");
        return;
    }
    uint32_t used = 0, cap = 0;
    if (!app.db.rows_in_use(*e, used, cap) || used >= cap) {
        g_state.error = (cap ? "Nothing written: the editedplayernames table is full (" + std::to_string(used) + " of " + std::to_string(cap) + " rows)"
                             : std::string("Nothing written: the editedplayernames row count cannot be read")) +
                        ", and Turbo never adds a row to a full table. Change the name ids below instead.";
        app.notify(p.name + ": " + g_state.error, true);
        return;
    }
    json a = {{"action", "set_display_name"}, {"playerid", p.playerid}};
    for (int i = 0; i < 4; ++i)
        if (e->has(kNameFields[i])) a[kNameFields[i]] = v[i];
    add_room_check(app, a, cap);
    if (!app.send({{"op", "run"}, {"module", "callnames"}, {"overrides", {{"actions", json::array({a})}}}}, "Names " + p.name)) {
        g_state.error = "Not sent: Turbo's command channel is busy or not available. Try again in a moment.";
        return;
    }
    app.notify(p.name + ": " + list_fields(changed) + " sent to Turbo's Lua side (a new editedplayernames row, next career event)");
}

// Restore database names: his row back to the name ids' texts, in place (no delete). The common name is empty when
// commonnameid is 0, the shirt name is his playerjerseynameid's text, else the last name.
static void restore_names(App& app, const Table& t, const PlayerRow& p) {
    g_state.error.clear();
    const Table* e = app.db.table("editedplayernames");
    const ShownName cur = shown_name(app, t, p);
    if (!e || !cur.edited_rec) return;
    bool unknown = false;
    std::string v[4] = {id_text(app, t, p, kIdFields[0], &unknown), id_text(app, t, p, kIdFields[1], &unknown),
                        id_text(app, t, p, kIdFields[2], &unknown), id_text(app, t, p, kIdFields[3])};
    if (unknown) {
        g_state.error = "The name texts are not loaded yet: nothing written. Press Refresh and try again.";
        return;
    }
    if (v[3].empty()) v[3] = v[1];
    if (v[3].empty()) {
        g_state.error = "His last name id has no text: nothing written (the shirt name would be empty).";
        return;
    }
    for (int i = 0; i < 4; ++i)
        if (const Field* f = e->field(kNameFields[i])) {
            const std::string err = check_text(*f, kLabels[i], v[i]);
            if (!err.empty()) {
                g_state.error = err + ". Nothing written.";
                return;
            }
        }
    std::vector<int> todo;
    for (int i = 0; i < 4; ++i)
        if (e->has(kNameFields[i]) && row_text(app, *e, cur.edited_rec, kNameFields[i]) != v[i]) todo.push_back(i);
    g_force = true;
    if (todo.empty()) {
        app.notify(p.name + ": his names are already the database names");
        return;
    }
    for (int i : todo)
        if (!app.edit(*e, cur.edited_rec, *e->field(kNameFields[i]), Value::of_str(v[i]))) {
            g_state.error = std::string(kLabels[i]) + " not written (see the message).";
            return;
        }
    app.notify(p.name + ": database names restored (" + list_fields(todo) + "). " + kWhenShown);
}

// A row the 1.2.0 preset import damaged: first name, surname and shirt name empty, the common name his full name
static bool import_damaged(App& app, const Table& t, const PlayerRow& p, const Table& e, uint64_t rec) {
    if (!rec) return false;
    const std::string common = row_text(app, e, rec, "commonname");
    if (common.empty() || !row_text(app, e, rec, "firstname").empty() || !row_text(app, e, rec, "surname").empty() ||
        (e.has("playerjerseyname") && !row_text(app, e, rec, "playerjerseyname").empty()))
        return false;
    const std::string first = id_text(app, t, p, "firstnameid"), last = id_text(app, t, p, "lastnameid");
    return !first.empty() && !last.empty() && common == first + " " + last;
}

// Names picked by text for the name ids: cached per query and model version
static void name_id_search(App& app, const Table& t, const PlayerRow& p) {
    static char q[64] = "";
    static std::string last_q;
    static uint64_t last_version = ~uint64_t(0);
    static std::vector<std::pair<int64_t, std::string>> hits;
    static size_t total = 0;
    ImGui::SetNextItemWidth(S(240.0f));
    ImGui::InputTextWithHint("##nmidsearch", "find a name id by its text", q, sizeof(q));
    std::string lq = trim(q);
    std::transform(lq.begin(), lq.end(), lq.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lq.size() < 2) return;
    if (lq != last_q || last_version != app.model.version()) {
        last_q = lq;
        last_version = app.model.version();
        hits.clear();
        total = 0;
        for (const auto& kv : app.model.names_by_id()) {
            std::string ln = kv.second;
            std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ln.find(lq) == std::string::npos && std::to_string(kv.first) != lq) continue;
            ++total;
            hits.push_back(kv);
        }
        std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
        if (hits.size() > 60) hits.resize(60);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu found", total);
    if (hits.empty()) return;
    ImGui::BeginChild("##nmids", ImVec2(0, S(150.0f)), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##nmidtable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name ID", ImGuiTableColumnFlags_WidthFixed, S(70.0f));
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Use as", ImGuiTableColumnFlags_WidthFixed, S(230.0f));
        ImGui::TableHeadersRow();
        for (const auto& h : hits) {
            ImGui::PushID(static_cast<int>(h.first & 0x7FFFFFFF));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(h.first));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(h.second.c_str());
            ImGui::TableNextColumn();
            static const char* const kUse[4] = {"First", "Last", "Common", "Shirt"};
            for (int k = 0; k < 4; ++k) {
                const Field* f = t.field(kIdFields[k]);
                if (!f) continue;
                if (k) ImGui::SameLine();
                if (ImGui::SmallButton(kUse[k]) && app.edit(t, p.rec, *f, Value::of_int(h.first)))
                    app.notify(p.name + ": " + kIdLabels[k] + " = " + std::to_string(h.first) + " (" + h.second + ")");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (total > hits.size()) ImGui::TextDisabled("%zu more: type more of the name", total - hits.size());
    ImGui::EndChild();
}

static void name_ids_section(App& app, const Table& t, const PlayerRow& p) {
    ImGui::SeparatorText("Name ids (players table)");
    ImGui::TextDisabled("A name id also picks his commentary callname: for callnames use the Callname tab.");
    for (int i = 0; i < 4; ++i) {
        const Field* f = t.field(kIdFields[i]);
        if (!f) continue;
        field_editor(app, t, p.rec, *f, kIdLabels[i], S(90.0f));
        ImGui::SameLine();
        bool unknown = false;
        const std::string text = id_text(app, t, p, kIdFields[i], &unknown);
        if (unknown) ImGui::TextDisabled("(text not loaded)");
        else if (text.empty()) ImGui::TextDisabled("(none)");
        else ImGui::TextUnformatted(text.c_str());
    }
    name_id_search(app, t, p);
    ImGui::TextDisabled("The game shows a new name id at once, until the career is reloaded; edited names above win over it.");
}

void names_editor(App& app, const Table& t, const PlayerRow& p) {
    const ShownName cur = shown_name(app, t, p);
    const std::string now[4] = {cur.first, cur.last, cur.common, cur.jersey};
    // fill the boxes for a newly picked player, after a write, and when the database changed under boxes not being edited
    if (g_pid != p.playerid) g_state.error.clear();
    if (g_pid != p.playerid || g_force || (g_gen != app.gen && (boxes_are(g_loaded) || boxes_are(now)))) {
        fill_boxes(now);
        g_pid = p.playerid;
        g_force = false;
    }
    g_gen = app.gen;
    g_state.playerid = p.playerid;
    g_state.has_row = cur.edited_rec != 0;
    g_state.import_hint.clear();
    g_state.common_note.clear();
    const Table* e = app.db.table("editedplayernames");

    ImGui::SeparatorText("Shown names (editedplayernames)");
    ImGui::TextDisabled("%s", !e ? "This database has no editedplayernames table."
                              : cur.edited_rec ? "He has edited names: Save names changes them in place."
                                               : "No edited names yet: Save names asks Turbo's Lua side to add them (next career event).");
    for (int i = 0; i < 4; ++i) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(kLabels[i]);
        ImGui::SameLine(S(150.0f));
        ImGui::SetNextItemWidth(S(260.0f));
        ImGui::InputText(kBoxIds[i], g_box[i], sizeof(g_box[i]));
        if (ImGui::IsItemHovered()) {
            if (i == 2) ImGui::SetTooltip("Empty = no common name: the game shows first and last name");
            else if (i == 3) ImGui::SetTooltip("Empty = the last name (the shirt is never left blank)");
        }
    }
    g_state.first = g_box[0];
    g_state.last = g_box[1];
    g_state.common = g_box[2];
    g_state.jersey = g_box[3];
    if (trim(g_box[2]).empty() && t.has("commonnameid") && app.db.get_int(t, p.rec, "commonnameid", 0) > 0) {
        g_state.common_note = "His common name id still gives a common name: set Common name id to 0 below to drop it (that also changes his callname).";
        ImGui::TextWrapped("%s", g_state.common_note.c_str());
    }
    if (e && import_damaged(app, t, p, *e, cur.edited_rec)) {
        g_state.import_hint = "These names look changed by a preset import: Restore database names fixes them.";
        ImGui::TextColored(kOrange, "%s", g_state.import_hint.c_str());
    }
    if (!g_state.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
        ImGui::TextWrapped("%s", g_state.error.c_str());
        ImGui::PopStyleColor();
    }
    if (!e) ImGui::BeginDisabled();
    if (ImGui::Button("Save names##nm")) save_names(app, t, p);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Writes only the names you changed. Undo (above the tabs) puts back an in-place change one field at a time.");
    ImGui::SameLine();
    const bool no_row = !e || !cur.edited_rec;
    if (no_row && e) ImGui::BeginDisabled();
    if (ImGui::Button("Restore database names##nm")) restore_names(app, t, p);
    if (no_row && e) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", no_row ? "He has no edited names: the game already shows his database names"
                                       : "Sets his edited names back to the texts of his name ids (no common name when his common name id is 0)");
    if (!e) ImGui::EndDisabled();
    ImGui::TextDisabled("%s", kWhenShown);
    name_ids_section(app, t, p);
}

}  // namespace turbo
