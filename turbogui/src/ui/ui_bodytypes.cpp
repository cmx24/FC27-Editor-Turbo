// FC 27 LE Turbo GUI - body type gallery (see ui_bodytypes.h)
#include "ui_bodytypes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "app.h"
#include "imgui.h"

namespace turbo {

namespace fs = std::filesystem;
using bodytype::Catalog;
using bodytype::Entry;
using bodytype::Filter;
using bodytype::Group;
using bodytype::Kind;

namespace {

Catalog g_catalog;
fs::path g_root;
fs::file_time_type g_probe_time{};
bool g_probe_seen = false;
double g_next_check = 0.0;

// gallery state, kept while the popup is open
struct Gallery {
    int group = 0;  // 0 all, 1 generic, 2 specific
    int hmin = 0, hmax = 0, wmin = 0, wmax = 0;
    char text[48] = "";
    bool used_only = false;
    bool allow_risky = false;
    char copy_text[48] = "";
};
Gallery g_gal;
BodyTypeUiState g_ui;

const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);

void reload(App& app) {
    g_root = app.bridge.root();
    g_catalog.clear();
    g_catalog.load_for_root(g_root);
    std::error_code ec;
    const fs::path pf = app.bridge.dir() / "bodytypes_fc27.json";
    g_probe_seen = fs::exists(pf, ec);
    if (g_probe_seen) g_probe_time = fs::last_write_time(pf, ec);
}

int head_class_of(App& app, const Table& t, uint64_t rec) {
    if (!t.has("headclasscode")) return -1;
    const int64_t v = app.db.get_int(t, rec, "headclasscode", -1);
    return v == 0 || v == 1 ? static_cast<int>(v) : -1;
}

std::string range_text(int lo, int hi, const char* unit) {
    if (hi <= 0) return "-";
    char b[48];
    if (lo == hi) std::snprintf(b, sizeof(b), "%d %s", lo, unit);
    else std::snprintf(b, sizeof(b), "%d-%d %s", lo, hi, unit);
    return b;
}

// 0 = off; typed text is clamped to something a person's height or weight can be
void int_box(const char* label, const char* hint, int* v, float w) {
    ImGui::SetNextItemWidth(w);
    ImGui::InputInt(label, v, 0, 0);
    if (*v < 0) *v = 0;
    if (*v > 400) *v = 400;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
}

}  // namespace

Catalog& bodytype_catalog(App& app) {
    std::error_code ec;
    const fs::path pf = app.bridge.dir() / "bodytypes_fc27.json";
    if (g_root != app.bridge.root()) {
        reload(app);
    } else if (app.now >= g_next_check) {  // the probe may be run while the GUI is open: look again every few seconds
        g_next_check = app.now + 3.0;
        const bool there = fs::exists(pf, ec);
        if (there != g_probe_seen || (there && fs::last_write_time(pf, ec) != g_probe_time)) reload(app);
    }
    return g_catalog;
}

std::string bodytype_label(int64_t code) { return g_catalog.name(code); }

const BodyTypeUiState& bodytype_ui_state() { return g_ui; }

bool apply_bodytype(App& app, const Table& t, uint64_t rec, int64_t code, bool allow_risky, std::string* msg) {
    auto say = [&](const std::string& s) {
        if (msg) *msg = s;
        g_ui.last_result = s;
    };
    const Field* f = t.field("bodytypecode");
    if (!f || f->type != FieldType::Int) {
        say("This table has no body type field.");
        return false;
    }
    const Catalog& c = bodytype_catalog(app);
    const std::string nm = c.name(code);
    if (code < f->min || code > f->max()) {
        say(nm + " is outside the range this game accepts for the body type.");
        return false;
    }
    const int hc = head_class_of(app, t, rec);
    if (!allow_risky && bodytype::risky_pairing(c, code, hc)) {
        say(nm + " was not written: a player-specific body on a " + std::string(hc == 1 ? "generic" : "head of unknown kind") +
            " head may not fit its skeleton and kit (untested in game). Tick the box in the gallery to write it anyway.");
        return false;
    }
    if (app.db.get_int(t, rec, "bodytypecode", -1) == code) {
        say("Body type is already " + nm + ".");
        return true;
    }
    if (!app.edit(t, rec, *f, Value::of_int(code))) {
        say("Body type " + nm + " was not written (the database refused it).");
        return false;
    }
    say("Body type set to " + nm);
    return true;
}

// The rows of one group: name, code, used by, height, weight, examples, and the Use button
static void draw_rows(App& app, const Table& t, uint64_t rec, const std::vector<int64_t>& codes, int64_t cur, int head_class, bool specific) {
    const Catalog& c = g_catalog;
    if (codes.empty()) {
        ImGui::TextDisabled(specific ? "No player-specific body matches the filters." : "No generic body matches the filters.");
        return;
    }
    if (!ImGui::BeginTable(specific ? "##btspec" : "##btgen", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("Body type", ImGuiTableColumnFlags_WidthStretch, 2.2f);
    ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, S(46.0f));
    ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthFixed, S(86.0f));
    ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, S(86.0f));
    ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, S(76.0f));
    ImGui::TableSetupColumn("Examples", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
    ImGui::TableHeadersRow();
    for (int64_t code : codes) {
        const Entry* e = c.find(code);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(c.name(code).c_str());
        if (code == cur) {
            ImGui::SameLine();
            ImGui::TextDisabled("(current)");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%lld", static_cast<long long>(code));
        ImGui::TableNextColumn();
        if (e && e->probed) {
            ImGui::Text("%lld player%s", static_cast<long long>(e->players), e->players == 1 ? "" : "s");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Counted by the body type probe in the game's players table.");
        } else {
            ImGui::TextDisabled("not probed");
        }
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e ? range_text(e->height_min, e->height_max, "cm").c_str() : "-");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(e ? range_text(e->weight_min, e->weight_max, "kg").c_str() : "-");
        ImGui::TableNextColumn();
        const std::string ex = c.examples_text(code);
        ImGui::TextUnformatted(ex.empty() ? "-" : ex.c_str());
        ImGui::TableNextColumn();
        const bool risky = bodytype::risky_pairing(c, code, head_class) && !g_gal.allow_risky;
        ImGui::BeginDisabled(risky || code == cur);
        const std::string id = "Use##bt" + std::to_string(code);
        if (ImGui::Button(id.c_str())) apply_bodytype(app, t, rec, code, g_gal.allow_risky, nullptr);
        ImGui::EndDisabled();
        if (risky && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("A player-specific body on this head is untested. Tick the box above to allow it.");
        (g_ui.shown_generic += specific ? 0 : 1, g_ui.shown_specific += specific ? 1 : 0);
    }
    ImGui::EndTable();
}

// "Copy body type from player...": pick a player, take his bodytypecode (the same head-class guard applies)
static void draw_copy_popup(App& app, const Table& t, uint64_t rec, int head_class) {
    ImGui::SetNextWindowSize(ImVec2(S(520.0f), S(380.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Copy body type from player##btcopy", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    const Table* pt = app.db.table("players");
    ImGui::TextWrapped("Pick a player: his body type code is copied to this %s (height, weight and the head stay as they are).",
                       t.name == "players" ? "player" : "manager");
    if (ImGui::IsWindowAppearing()) {
        g_gal.copy_text[0] = 0;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##btcopyflt", "player name (at least 2 letters)", g_gal.copy_text, sizeof(g_gal.copy_text));
    std::string q = g_gal.copy_text;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!pt || !pt->has("bodytypecode")) {
        ImGui::TextDisabled("The players table has no body type field.");
    } else if (q.size() >= 2 && ImGui::BeginChild("##btcopylist", ImVec2(0, -S(34.0f)))) {
        int shown = 0;
        for (const auto& p : app.model.players()) {
            std::string n = p.name;
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (n.find(q) == std::string::npos) continue;
            if (shown++ >= 12) break;
            const int64_t code = app.db.get_int(*pt, p.rec, "bodytypecode", -1);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  -  %s", p.name.c_str(), code < 0 ? "no body type" : g_catalog.name(code).c_str());
            ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - S(70.0f));
            const bool risky = code >= 0 && bodytype::risky_pairing(g_catalog, code, head_class) && !g_gal.allow_risky;
            ImGui::BeginDisabled(code < 0 || risky);
            const std::string id = "Copy##btcp" + std::to_string(p.playerid);
            if (ImGui::Button(id.c_str())) {
                apply_bodytype(app, t, rec, code, g_gal.allow_risky, nullptr);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (risky && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("A player-specific body on this head is untested. Tick the box in the gallery to allow it.");
        }
        if (shown == 0) ImGui::TextDisabled("No player with that name.");
        ImGui::EndChild();
    }
    if (ImGui::Button("Close##btcopyclose")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

static void draw_gallery(App& app, const Table& t, uint64_t rec, bool manager) {
    ImGui::SetNextWindowSize(ImVec2(S(860.0f), S(560.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Body types##bt", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        g_ui.open = false;
        return;
    }
    g_ui.open = true;
    g_ui.shown_generic = g_ui.shown_specific = 0;
    const Catalog& c = bodytype_catalog(app);
    const int64_t cur = app.db.get_int(t, rec, "bodytypecode", -1);
    const int hc = head_class_of(app, t, rec);

    ImGui::Text("Current: %s (code %lld)", c.name(cur).c_str(), static_cast<long long>(cur));
    ImGui::SameLine();
    ImGui::TextDisabled("  head: %s", hc == 0 ? "specific (real face)" : hc == 1 ? "generic" : manager ? "manager (kind unknown)" : "unknown");
    if (!c.probe_loaded()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("The body type probe has not been run, so player counts, heights, weights and example players are unknown, and "
                           "the player-specific bodies are not listed. Run bodytypes.lua in the game once (see the Turbo README); it "
                           "writes turbo_output\\bodytypes_fc27.json, which this window reads on its own.");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%zu codes probed%s.", c.probed_count(), c.skipped() ? " (some damaged entries skipped)" : "");
    }
    if (c.localized_count() == 0) ImGui::TextDisabled("Live Editor's body type names were not found: Turbo's built-in names for the generic types are used.");

    // filters
    {
        const char* groups[] = {"All", "Generic only", "Player-specific only"};
        ImGui::SetNextItemWidth(S(150.0f));
        ImGui::Combo("##btgroup", &g_gal.group, groups, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::InputTextWithHint("##btflt", "search name, code or player", g_gal.text, sizeof(g_gal.text));
        ImGui::SameLine();
        ImGui::Checkbox("Used by players only##btused", &g_gal.used_only);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Height cm");
        ImGui::SameLine();
        int_box("##bthmin", "Shortest height to show (0 = off). A body type matches when its probed height range overlaps.", &g_gal.hmin, S(70.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted("to");
        ImGui::SameLine();
        int_box("##bthmax", "Tallest height to show (0 = off).", &g_gal.hmax, S(70.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted("   Weight kg");
        ImGui::SameLine();
        int_box("##btwmin", "Lightest weight to show (0 = off). Body types without probe data are hidden while a filter is on.", &g_gal.wmin, S(70.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted("to");
        ImGui::SameLine();
        int_box("##btwmax", "Heaviest weight to show (0 = off).", &g_gal.wmax, S(70.0f));
    }
    Filter f;
    f.group = g_gal.group == 1 ? Group::Generic : g_gal.group == 2 ? Group::Specific : Group::All;
    f.height_min = g_gal.hmin; f.height_max = g_gal.hmax;
    f.weight_min = g_gal.wmin; f.weight_max = g_gal.wmax;
    f.text = g_gal.text;
    f.used_only = g_gal.used_only;
    const std::vector<int64_t> codes = c.filter(f);
    std::vector<int64_t> gen, spec;
    for (int64_t k : codes) (c.kind(k) == Kind::Generic ? gen : spec).push_back(k);

    // the warning about specific bodies on generic heads, and the way past it
    if (hc != 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("%s A player-specific body model on a %s head may not match its skeleton and kit; this is untested in game, so "
                           "Turbo does not write one unless you allow it here.",
                           "Warning:", hc == 1 ? "generic" : "head of unknown kind");
        ImGui::PopStyleColor();
        ImGui::Checkbox("Allow a player-specific body on this head (untested)##btallow", &g_gal.allow_risky);
    } else {
        g_gal.allow_risky = false;
    }

    ImGui::BeginChild("##btlist", ImVec2(0, -S(36.0f)));
    ImGui::SeparatorText("Generic (named by Live Editor)");
    draw_rows(app, t, rec, gen, cur, hc, false);
    ImGui::SeparatorText("Player-specific (models of individual players)");
    if (!c.probe_loaded() && spec.empty())
        ImGui::TextDisabled("None known yet: they appear here after the probe has run.");
    else
        draw_rows(app, t, rec, spec, cur, hc, true);
    ImGui::EndChild();

    if (ImGui::Button("Copy body type from player...##btcopybtn")) ImGui::OpenPopup("Copy body type from player##btcopy");
    draw_copy_popup(app, t, rec, hc);
    ImGui::SameLine();
    if (ImGui::Button("Reload##btreload")) reload(app);
    ImGui::SameLine();
    if (!g_ui.last_result.empty()) ImGui::TextDisabled("%s", g_ui.last_result.c_str());
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - S(70.0f));
    if (ImGui::Button("Close##btclose")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void bodytype_gallery_button(App& app, const Table& t, uint64_t rec, bool manager) {
    if (!t.has("bodytypecode")) return;
    bodytype_catalog(app);  // loaded before the editors' combos name a code
    ImGui::PushID("##bodytypes");
    if (ImGui::Button("Body types...##btopen")) {
        g_ui.last_result.clear();
        ImGui::OpenPopup("Body types##bt");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Browse generic and player-specific body types, filter by height and weight, or copy one from a player.");
    draw_gallery(app, t, rec, manager);
    ImGui::PopID();
}

}  // namespace turbo
