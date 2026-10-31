// FC 27 LE Turbo GUI - body type gallery (see ui_bodytypes.h)
#include "ui_bodytypes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "app.h"
#include "core/body_shape.h"
#include "imgui.h"
#include "ui_visual_picker.h"

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

// State of the picker's "allow a player-specific body" box and of the copy popup (5.0c E8-body: the modal gallery is gone, the visual
// picker window of ui_visual_picker.h is the one place that lists the body types)
struct Gallery {
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
    const bool is_manager = t.name == "manager";
    const std::string plain = is_manager ? bodyshape::body_name(true, code, &c) : std::string();
    const std::string nm = plain.empty() ? c.name(code) : plain;  // 5.0c E8-body: a manager reads "Manager, tall, slim", never "Manager body 82"
    if (code < f->min || code > f->max()) {
        say(nm + " is outside the range this game accepts for the body type.");
        return false;
    }
    const int hc = head_class_of(app, t, rec);
    if (!allow_risky && bodyshape::risky_for(is_manager, c, code, hc)) {
        say(nm + " was not written: a player-specific body on a " + std::string(hc == 1 ? "generic" : "head of unknown kind") +
            " head may not fit its skeleton and kit (untested in game). Tick the box in the Body type window to write it anyway.");
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
    } else if (q.size() >= 2 && ImGui::BeginChild("##btcopylist", ImVec2(0, -S(34.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
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
                ImGui::SetTooltip("A player-specific body on this head is untested. Tick the box in the Body type window to allow it.");
        }
        if (shown == 0) ImGui::TextDisabled("No player with that name.");
        ImGui::EndChild();
    }
    if (ImGui::Button("Close##btcopyclose")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool bodytype_allow_risky() { return g_gal.allow_risky; }
void bodytype_reset_risky() { g_gal.allow_risky = false; }

// The warning and the box of the Body type window (ui_visual_picker.cpp draw_picker_body): shown while the head is not a specific
// (real-face) one, because only then can a player-specific body not fit the skeleton and kit
void bodytype_risky_box(App& app, const Table& t, uint64_t rec) {
    const int hc = head_class_of(app, t, rec);
    if (hc == 0) {
        g_gal.allow_risky = false;
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("A player-specific body model on a %s head may not match its skeleton and kit; this is untested in game, so Turbo "
                       "does not write one unless you allow it here.",
                       hc == 1 ? "generic" : "head of unknown kind");
    ImGui::PopStyleColor();
    checkbox_right("Allow a player-specific body on this head (untested)##btallow", &g_gal.allow_risky);
}

// ONE entry point per page (5.0c E8-body, RV1): "Body type..." opens the visual picker window (figures, plain names), drawn from
// here so every page that had the old button shows it; "Copy body type from player..." stays beside it.
void bodytype_gallery_button(App& app, const Table& t, uint64_t rec, bool manager) {
    if (!t.has("bodytypecode")) return;
    bodytype_catalog(app);  // loaded before the editors' combos name a code
    ImGui::PushID("##bodytypes");
    if (ImGui::Button("Body type...##btopen")) {
        g_ui.last_result.clear();
        vp::open(app, t, rec, "bodytypecode", manager);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a body type from the figures: shorter, taller, leaner or stockier.");
    ImGui::SameLine();
    if (ImGui::Button("Copy body type from player...##btcopybtn")) ImGui::OpenPopup("Copy body type from player##btcopy");
    draw_copy_popup(app, t, rec, head_class_of(app, t, rec));
    if (!g_ui.last_result.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g_ui.last_result.c_str());
    }
    vp::window(app, t, rec);
    ImGui::PopID();
}

}  // namespace turbo
