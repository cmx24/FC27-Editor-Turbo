// FC 27 LE Turbo GUI - the Game settings tab (see ui_game_settings.h, core/game_settings.h).
//
// Frame-loop rules as in the other views: never throws; the settings file (772 KB) is read and checked only at the first look, on
// Refresh and after Save current; the presets folder on the first look and after a change; nothing is created or written by drawing.
#include "ui_game_settings.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>

#include "app.h"
#include "core/game_settings.h"
#include "core/hub_customise.h"
#include "file_picker.h"
#include "imgui.h"
#include "table_registry.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;
using namespace gsettings;

struct Gv {
    bool loaded = false;
    fs::path settings_override, map_override;
    SliderMap map;
    bool map_ok = false;
    std::string map_err;
    gsettings::Snapshot snap;
    std::vector<Preset> presets;
    std::vector<std::string> preset_errors;
    int preset_sel = -1;
    char preset_name[64] = "";
    FilePicker export_fp, import_fp;
    char rename_buf[64] = "";
    bool rename_request = false, delete_request = false;
    std::string preset_line;
    bool have_pending = false;
    Pending pending;
    std::string pending_err;
};
Gv g;
GameSettingsViewState g_state;

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }
fs::path preset_dir(App& app) { return out_dir(app) / presets_dir(); }
fs::path settings_dir() { return g.settings_override.empty() ? mhm::default_save_dir() : g.settings_override; }
fs::path map_path(App& app) { return g.map_override.empty() ? app.bridge.root() / "turbo" / map_dir() / map_file() : g.map_override; }

std::string stamp(const char* f) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[40];
    std::strftime(b, sizeof(b), f, &tm);
    return b;
}

void log_line(App& app, const std::string& s) {
    app.log(s);
    if (app.log_hook) app.log_hook(s);
}

void read_file_now(App& app) {
    g.snap = g.map_ok ? read_current(settings_dir(), g.map) : gsettings::Snapshot();
    if (!g.map_ok) g.snap.why = "no slider map: " + g.map_err;
    ++g_state.file_reads;
}

void read_pending(App& app) {
    g.have_pending = false;
    g.pending_err.clear();
    std::vector<uint8_t> b;
    const fs::path p = out_dir(app) / pending_file();
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    if (!read_bytes(p, b, 1u << 20) || !Pending::parse(std::string(b.begin(), b.end()), g.pending, &g.pending_err)) {
        if (g.pending_err.empty()) g.pending_err = "cannot read it";
        return;
    }
    g.have_pending = true;
}

void load_presets_now(App& app) {
    g.preset_errors.clear();
    g.presets = load_presets(preset_dir(app), &g.preset_errors);
    if (g.preset_sel >= int(g.presets.size())) g.preset_sel = -1;
}

void select_named(const std::string& name) {
    g.preset_sel = -1;
    for (size_t i = 0; i < g.presets.size(); ++i)
        if (same_name(g.presets[i].name, name)) g.preset_sel = int(i);
}

void first_look(App& app) {
    if (g.loaded) return;
    g.loaded = true;
    g.map_ok = load_map(map_path(app), g.map, &g.map_err);
    read_file_now(app);
    load_presets_now(app);
    read_pending(app);
}

int verified_count() {
    int n = 0;
    for (const Entry& e : g.map.entries)
        if (e.writable()) ++n;
    return n;
}

// ------------------------------------------------------------------------------------------------ preset actions
void save_current(App& app) {
    std::string name = g.preset_name;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) {
        app.notify("Type a name for the preset first", true);
        return;
    }
    read_file_now(app);  // the newest values the game saved
    if (!g.snap.ok) {
        app.notify("Not saved: " + g.snap.why, true);
        return;
    }
    Preset p = preset_from_snapshot(name, g.snap, g.map, stamp("%Y-%m-%dT%H:%M"));
    if (p.values.empty()) {
        g.preset_line = "Nothing to save: no slider of the map is verified in game yet (they are read-only until the marker launch proves them)";
        app.notify(g.preset_line, true);
        return;
    }
    std::string err;
    if (!write_preset_file(preset_dir(app), p, &err)) {
        app.notify("The preset could not be written: " + err, true);
        return;
    }
    g.preset_line = "Saved \"" + p.name + "\" (" + std::to_string(p.values.size()) + " slider(s) from " + g.snap.name +
                    ") to turbo_output\\" + presets_dir() + "\\" + p.file.filename().u8string() +
                    ". The file is what the game saved last: a change made in the game's menus counts once the game has saved it.";
    log_line(app, "game settings: " + g.preset_line);
    load_presets_now(app);
    select_named(p.name);
}

void load_preset(App& app) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    const Preset& p = g.presets[size_t(g.preset_sel)];
    std::string msg;
    const bool ok = stage_pending(out_dir(app), p, g.map, stamp("%Y-%m-%dT%H:%M:%S"), &msg);
    g.preset_line = msg;
    log_line(app, "game settings: " + msg);
    if (!ok) app.notify(msg, true);
    else app.notify("Staged: close FC 27 and run turbo_settings_apply.exe --apply");
    read_pending(app);
}

void discard_pending(App& app) {
    std::error_code ec;
    fs::remove(out_dir(app) / pending_file(), ec);
    g.preset_line = ec ? "The staged file could not be removed: " + ec.message() : std::string("The staged preset was discarded (nothing will be applied)");
    log_line(app, "game settings: " + g.preset_line);
    read_pending(app);
}

void import_preset(App& app, const fs::path& from) {
    std::vector<uint8_t> b;
    Preset p;
    std::string err;
    if (!read_bytes(from, b, 1u << 20) || !Preset::parse(std::string(b.begin(), b.end()), p, &err)) {
        app.notify("Not imported: " + (err.empty() ? std::string("cannot read the file") : err), true);
        return;
    }
    std::error_code ec;
    const bool replaced = fs::exists(preset_dir(app) / fs::u8path(preset_file_name(p)), ec);
    if (!write_preset_file(preset_dir(app), p, &err)) {
        app.notify("Not imported: " + err, true);
        return;
    }
    g.preset_line = std::string(replaced ? "Imported (replaced) \"" : "Imported \"") + p.name + "\" into turbo_output\\" + presets_dir();
    log_line(app, "game settings: " + g.preset_line + " from " + from.u8string());
    load_presets_now(app);
    select_named(p.name);
}

void export_preset(App& app, const fs::path& to) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    const Preset& p = g.presets[size_t(g.preset_sel)];
    std::string err;
    if (!write_text(to, p.to_json(), &err)) {
        app.notify("Not exported: " + err, true);
        return;
    }
    g.preset_line = "Exported \"" + p.name + "\" to " + to.u8string();
    log_line(app, "game settings: " + g.preset_line);
}

void duplicate_preset(App& app) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    Preset p = g.presets[size_t(g.preset_sel)];
    const std::string source = p.name;
    p.name = unique_preset_name(g.presets, p.name + " copy");
    p.note = "Copy of \"" + source + "\"";
    std::string err;
    if (!write_preset_file(preset_dir(app), p, &err)) {
        app.notify("The preset could not be copied: " + err, true);
        return;
    }
    g.preset_line = "Copied to \"" + p.name + "\"";
    log_line(app, "game settings: " + g.preset_line);
    load_presets_now(app);
    select_named(p.name);
}

void rename_preset(App& app, const std::string& wanted) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    Preset p = g.presets[size_t(g.preset_sel)];
    std::string name = wanted;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) {
        app.notify("Type a name for the preset", true);
        return;
    }
    if (name == p.name) return;
    std::vector<Preset> others;
    for (size_t i = 0; i < g.presets.size(); ++i)
        if (int(i) != g.preset_sel) others.push_back(g.presets[i]);
    if (unique_preset_name(others, name) != name) {
        app.notify("A preset with that name exists: pick another name", true);
        return;
    }
    const Preset old = p;
    p.name = name;
    std::string err;
    if (!write_preset_file(preset_dir(app), p, &err)) {
        app.notify("The preset was not renamed: " + err, true);
        return;
    }
    std::error_code ec;
    if (!fs::equivalent(old.file, p.file, ec) && !delete_preset_file(old, &err)) log_line(app, "game settings: the old preset file was not removed: " + err);
    g.preset_line = "Renamed \"" + old.name + "\" to \"" + p.name + "\"";
    log_line(app, "game settings: " + g.preset_line);
    load_presets_now(app);
    select_named(p.name);
}

void delete_preset(App& app) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    const Preset p = g.presets[size_t(g.preset_sel)];
    std::string err;
    if (!delete_preset_file(p, &err)) {
        app.notify("The preset was not deleted: " + err, true);
        return;
    }
    g.preset_line = "Deleted \"" + p.name + "\"";
    log_line(app, "game settings: " + g.preset_line);
    g.preset_sel = -1;
    load_presets_now(app);
}

// ------------------------------------------------------------------------------------------------ drawing
void draw_values() {
    for (const std::string& grp : groups()) {
        std::vector<const Entry*> es;
        for (const Entry& e : g.map.entries)
            if (e.group == grp) es.push_back(&e);
        for (const Entry* e : es) {
            GameSettingsViewState::Row r;
            r.id = e->id;
            r.label = e->label;
            r.group = grp;
            auto it = g.snap.values.find(e->id);
            r.value = e->index < 0 ? "not mapped yet" : (it != g.snap.values.end() && g.snap.ok ? std::to_string(it->second) : "-");
            r.state = e->index < 0 ? "not mapped yet" : e->verified ? "verified" : "not verified: read-only";
            g_state.rows.push_back(r);
        }
        const std::string head = std::string(group_label(grp)) + " (" + std::to_string(es.size()) + ")##gs_" + grp;
        if (!ImGui::CollapsingHeader(head.c_str(), grp == "user" || grp == "cpu" ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
        if (es.empty()) {
            ImGui::TextDisabled("Nothing in the slider map for this group.");
            continue;
        }
        ImGui::PushID(grp.c_str());  // one registered table ID (table_registry.h "##gsvals"), one table per group
        if (ImGui::BeginTable("##gsvals", 3, tbl::kData | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("In the file now", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableHeadersRow();
            for (const Entry* e : es) {
                const GameSettingsViewState::Row* r = nullptr;
                for (const auto& x : g_state.rows)
                    if (x.id == e->id) r = &x;
                if (!r) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e->label.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s%s", e->id.c_str(), e->note.empty() ? "" : "\n", e->note.c_str());
                ImGui::TableNextColumn();
                if (e->index < 0) ImGui::TextDisabled("%s", r->value.c_str());
                else ImGui::TextUnformatted(r->value.c_str());
                ImGui::TableNextColumn();
                if (e->verified) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", r->state.c_str());
                else ImGui::TextDisabled("%s", r->state.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    g_state.slots = int(g.snap.slots.size());
    if (!g.map.regions.empty() &&
        ImGui::CollapsingHeader("Candidate slots (unidentified, read-only)##gs_slots")) {
        ImGui::TextWrapped("The raw values of the blocks the assessment found (which slider each one is will be read from the marker launch). Read-only.");
        for (const gsettings::Region& rg : g.map.regions) {
            std::string line = rg.label + (rg.candidate.empty() ? "" : " - " + rg.candidate) + ":";
            for (const auto& s : g.snap.slots)
                if (s.first >= rg.first && s.first < rg.first + rg.count) line += " " + std::to_string(s.second);
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
}

void draw_presets(App& app) {
    ImGui::SeparatorText("Presets");
    ImGui::TextDisabled("A preset is a named set of these values (turbo_output\\%s). Only sliders verified in game are saved and staged.", presets_dir());
    for (const Preset& p : g.presets) g_state.presets.push_back(p.name);
    const std::string cur = g.preset_sel >= 0 && g.preset_sel < int(g.presets.size()) ? g.presets[size_t(g.preset_sel)].name : std::string("(pick a preset)");
    ImGui::SetNextItemWidth(S(260.0f));
    if (ImGui::BeginCombo("##gspreset", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (size_t i = 0; i < g.presets.size(); ++i) {
            if (ImGui::Selectable((g.presets[i].name + "##gsp" + std::to_string(i)).c_str(), int(i) == g.preset_sel)) g.preset_sel = int(i);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%zu value(s), saved %s from %s", g.presets[i].note.c_str(), g.presets[i].values.size(), g.presets[i].created.c_str(),
                                  g.presets[i].src_file.c_str());
        }
        ImGui::EndCombo();
    }
    const bool have = g.preset_sel >= 0 && g.preset_sel < int(g.presets.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(!have);
    if (ImGui::Button("Load: apply when the game is closed (stage)##gsload")) load_preset(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Writes turbo_output\\%s (the verified sliders of the preset). Nothing changes in the game until you close it and run "
                          "turbo\\turbo_settings_apply.exe --apply.",
                          pending_file());
    if (have) {
        const Plan pl = plan_preset(g.presets[size_t(g.preset_sel)], g.map);
        ImGui::TextDisabled("%s: %zu value(s), %zu would be staged, %zu read-only or not mapped", g.presets[size_t(g.preset_sel)].name.c_str(),
                            g.presets[size_t(g.preset_sel)].values.size(), pl.values.size(), pl.skipped.size());
    }
    ImGui::BeginDisabled(!have);
    if (ImGui::SmallButton("Duplicate##gsdup")) duplicate_preset(app);
    ImGui::SameLine();
    if (ImGui::SmallButton("Rename...##gsren")) {
        std::snprintf(g.rename_buf, sizeof(g.rename_buf), "%s", have ? g.presets[size_t(g.preset_sel)].name.c_str() : "");
        g.rename_request = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete...##gsdel")) g.delete_request = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Export...##gsexp")) {
        FilePicker& fp = g.export_fp;
        fp.mode = PickMode::Save;
        fp.title = "Export the game settings preset to";
        fp.exts = {".json"};
        fp.key = "gamesettings.export";
        fp.start = out_dir(app);
        if (have) std::snprintf(fp.name, sizeof(fp.name), "%s", preset_file_name(g.presets[size_t(g.preset_sel)]).c_str());
        ImGui::OpenPopup("##gsexppick");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Import...##gsimp")) {
        FilePicker& fp = g.import_fp;
        fp.mode = PickMode::Open;
        fp.title = "Choose a Turbo game settings preset";
        fp.exts = {".json"};
        fp.key = "gamesettings.import";
        fp.start = out_dir(app);
        ImGui::OpenPopup("##gsimppick");
    }
    fs::path chosen;
    if (file_picker_modal("##gsexppick", g.export_fp, chosen)) export_preset(app, chosen);
    if (file_picker_modal("##gsimppick", g.import_fp, chosen)) import_preset(app, chosen);
    if (g.rename_request) {
        ImGui::OpenPopup("Rename preset##gsrenamepop");
        g.rename_request = false;
    }
    if (ImGui::BeginPopupModal("Rename preset##gsrenamepop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(S(240.0f));
        ImGui::InputText("##gsrenamebox", g.rename_buf, sizeof(g.rename_buf));
        if (ImGui::Button("Rename##gsrenameok")) {
            rename_preset(app, g.rename_buf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##gsrenameno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g.delete_request) {
        ImGui::OpenPopup("Delete preset?##gsdeletepop");
        g.delete_request = false;
    }
    if (ImGui::BeginPopupModal("Delete preset?##gsdeletepop", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"? Only the preset file is removed.", have ? g.presets[size_t(g.preset_sel)].name.c_str() : "?");
        if (ImGui::Button("Delete##gsdeleteok")) {
            delete_preset(app);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep it##gsdeleteno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    form_label("Save current as", "A preset of your own from the settings file as the game saved it last (the verified sliders only). The same name replaces "
                                  "your earlier preset of that name.");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputText("##gsname", g.preset_name, sizeof(g.preset_name));
    ImGui::SameLine();
    if (ImGui::Button("Save current##gssave")) save_current(app);
    for (const std::string& e : g.preset_errors) ImGui::TextDisabled("A preset file was skipped: %s", e.c_str());
    if (!g.preset_line.empty()) ImGui::TextWrapped("%s", g.preset_line.c_str());
    // the staged file
    if (g.have_pending) {
        g_state.pending_line = "Staged for the next launch: \"" + g.pending.preset + "\", " + std::to_string(g.pending.values.size()) + " slider(s) (" +
                               g.pending.staged + ")";
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", g_state.pending_line.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Discard staged##gsdiscard")) discard_pending(app);
    } else if (!g.pending_err.empty()) {
        g_state.pending_line = std::string("turbo_output\\") + pending_file() + " is not used: " + g.pending_err;
        ImGui::TextDisabled("%s", g_state.pending_line.c_str());
    }
}

}  // namespace

const GameSettingsViewState& game_settings_view_state() { return g_state; }

void game_settings_reset() {
    const fs::path so = g.settings_override, mo = g.map_override;
    g = Gv();
    g.settings_override = so;
    g.map_override = mo;
    g_state = GameSettingsViewState();
}

void game_settings_set_paths(const fs::path& settings_dir, const fs::path& map_file) {
    g.settings_override = settings_dir;
    g.map_override = map_file;
    g.loaded = false;
}

void draw_game_settings(App& app) {
    const int reads = g_state.file_reads;
    g_state = GameSettingsViewState();
    g_state.file_reads = reads;
    g_state.drawn = true;
    try {
        first_look(app);
        ImGui::TextWrapped("FC 27's own game settings (Settings > Game Settings: match, player, and the User / CPU sliders), as the game saved them "
                           "in its settings file. Read-only here: presets of these values are applied OUTSIDE the game.");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        ImGui::TextWrapped("The game must be CLOSED to apply a preset, and relaunched to use it: Load stages it, then close FC 27 and run "
                           "turbo\\turbo_settings_apply.exe --apply (it backs the file up, checks it, and --undo puts it back). Turbo never touches "
                           "your careers (CmMgrC*), squads (Squads*) or ProfileOptions*.");
        ImGui::PopStyleColor();
        g_state.closed_note = true;
        // the file
        if (g.snap.ok) {
            g_state.file_ok = true;
            g_state.file_line = g.snap.name + ": " + g.snap.profile + ", layout " + g.snap.layout + ", CRC ok, md5 " + g.snap.md5;
            ImGui::TextDisabled("%s", g_state.file_line.c_str());
        } else {
            g_state.file_line = "The settings file is not read: " + g.snap.why;
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", g_state.file_line.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh##gsrefresh")) {
            g.map_ok = load_map(map_path(app), g.map, &g.map_err);
            read_file_now(app);
            load_presets_now(app);
            read_pending(app);
        }
        g_state.map_line = g.map_ok ? std::string(map_file()) + ": " + std::to_string(g.map.entries.size()) + " sliders, " + std::to_string(verified_count()) +
                                          " verified in game"
                                    : "No slider map: " + g.map_err;
        ImGui::TextDisabled("%s", g_state.map_line.c_str());
        if (g.map_ok && verified_count() == 0)
            ImGui::TextWrapped("No slider is verified in game yet: every value is read-only and a preset stages nothing until the marker launch "
                               "(turbo_settings_apply.exe --write-markers) proves which number is which slider.");
        // the presets first (short), then the long list of values
        draw_presets(app);
        g_state.preset_line = g.preset_line;
        g_state.preset_errors = g.preset_errors;
        if (g.map_ok) {
            ImGui::SeparatorText("In the settings file now");
            draw_values();
        }
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game settings: %s", e.what());
    }
}

}  // namespace turbo
