// FC 27 LE Turbo GUI - the Game settings tab (see ui_game_settings.h, core/game_settings.h).
//
// Frame-loop rules as in the other views: never throws; the settings file (772 KB) is read and checked only at the first look, on
// Refresh and after Save current; the presets folder on the first look and after a change; nothing is created or written by drawing.
#include "ui_game_settings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
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
    // 4.0 sliders-rw (c): the block the last live write found (reused while it still holds what Turbo wrote: the newest file does not
    // until the game saves)
    uint64_t live_addr = 0;
    std::vector<uint8_t> live_bytes;
};
Gv g;
GameSettingsViewState g_state;

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }
fs::path preset_dir(App& app) { return out_dir(app) / presets_dir(); }
fs::path settings_dir() { return g.settings_override.empty() ? mhm::default_save_dir() : g.settings_override; }
// 4.0 sliders-rw (a): the map of the running game's build (map_file_for); "" = no map for that build (first_look refuses it)
std::string game_build(App& app) { return app.hook_report ? app.hook_report_cached().build : std::string(); }
fs::path map_path(App& app) {
    if (!g.map_override.empty()) return g.map_override;
    const std::string f = map_file_for(game_build(app));
    return f.empty() ? fs::path() : app.bridge.root() / "turbo" / map_dir() / f;
}

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

void load_map_now(App& app) {
    const fs::path mp = map_path(app);
    if (mp.empty()) {
        g.map_ok = false;
        g.map = SliderMap();
        g.map_err = "no slider map for game build " + game_build(app) + " (Turbo knows 6AC07E31 and 6AD922D1): nothing is read or written";
    } else {
        g.map_ok = load_map(mp, g.map, &g.map_err);
    }
}

void first_look(App& app) {
    if (g.loaded) return;
    g.loaded = true;
    load_map_now(app);
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

// 4.0 sliders-rw (c): writes the preset into the running game's Custom sliders (gsettings::live, route 1 of
// evidence\2026-10-28\SLIDERS_WRITE\PLAN.md). Needs the host's region lister (Turbo inside FC 27); the old staging is left alone.
bool load_preset_live(App& app, const Preset& p, std::string& msg) {
    if (!app.regions_hook) {
        msg = "not inside FC 27 (no memory access)";
        return false;
    }
    if (!g.map_ok || g.map.custom_count <= 0) {
        msg = "no slider map with a Custom block (" + g.map_err + ")";
        return false;
    }
    const Plan plan = plan_preset(p, g.map);
    if (plan.values.empty()) {
        msg = "no slider of \"" + p.name + "\" can be written (" + std::to_string(plan.skipped.size()) + " read-only or not mapped)";
        return false;
    }
    uint64_t addr = 0;
    const size_t n = size_t(g.map.custom_count);
    if (g.live_addr && g.live_bytes.size() == n) {  // the block Turbo wrote last, if it still holds what Turbo wrote
        std::vector<uint8_t> now;
        if (app.mem.read_block(g.live_addr, n, now) && now == g.live_bytes) addr = g.live_addr;
    }
    if (!addr) {
        read_file_now(app);  // the newest file the game wrote
        if (!g.snap.ok || g.snap.custom_slots.size() != n) {
            msg = "the Settings file has no readable Custom block (" + g.snap.why + ")";
            return false;
        }
        std::vector<uint8_t> expect(n);
        for (size_t k = 0; k < n; ++k) expect[k] = uint8_t(g.snap.custom_slots[k].second);
        std::vector<live::Span> spans;
        for (const ::turbo::Region& r : app.regions_hook()) spans.push_back({r.start, r.end});
        const live::Found f = live::find_block(app.mem, spans, expect.data(), n);
        if (!f.ok) {
            msg = f.why;
            return false;
        }
        addr = f.addr;
    }
    const live::Result r = live::write_block(app.mem, addr, g.map, plan.values, app.writable_hook);
    if (!r.ok) {
        msg = r.message;
        return false;
    }
    g.live_addr = addr;
    if (!app.mem.read_block(addr, n, g.live_bytes)) g.live_bytes.clear();
    std::string err;
    const std::string st = stamp("%Y-%m-%dT%H:%M:%S");
    write_text(out_dir(app) / "settings_live_journal.json", live::journal_json(r, p.name, st), &err);
    msg = "Applied in game now: \"" + p.name + "\", " + r.message + ". The game writes them to its Settings file at its next save (open "
          "Customise > Game Settings > Game Type Settings, check, Back). NOT proven in game yet.";
    for (const std::string& l : r.lines) log_line(app, "game settings live: " + l);
    return true;
}

void load_preset(App& app) {
    if (g.preset_sel < 0 || g.preset_sel >= int(g.presets.size())) return;
    const Preset& p = g.presets[size_t(g.preset_sel)];
    std::string why;
    if (load_preset_live(app, p, why)) {
        g.preset_line = why;
        log_line(app, "game settings: " + why);
        app.notify(why);
        return;
    }
    // not applied in the game: say so. Stage for the apply tool only where --apply would take the file: the game's own files hold 00
    // before the Custom block (10-24 8F10EEB9, 10-28 marker_2041) and --apply refuses those (GUI30J), so nothing is staged for them
    if (!g.snap.ok) read_file_now(app);
    const bool apply_refuses = g.snap.ok && g.snap.custom_prefix.size() == sizeof(kCustomPrefix) &&
                               std::memcmp(g.snap.custom_prefix.data(), kCustomPrefix, sizeof(kCustomPrefix)) != 0;
    if (apply_refuses) {
        g.preset_line = "NOT applied in the game: " + why + ". Nothing staged: turbo_settings_apply.exe --apply refuses your Settings file "
                        "(the byte before the Custom block is 00), so today a preset is applied only inside the game.";
        log_line(app, "game settings: " + g.preset_line);
        app.notify(g.preset_line, true);
        return;
    }
    std::string msg;
    const bool ok = stage_pending(out_dir(app), p, g.map, stamp("%Y-%m-%dT%H:%M:%S"), &msg);
    g.preset_line = "NOT applied in the game: " + why + ". " + msg +
                    (ok ? ". It applies only when you close FC 27 and run turbo\\turbo_settings_apply.exe --apply." : "");
    log_line(app, "game settings: " + g.preset_line);
    app.notify(g.preset_line, true);
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
            r.value = (e->index < 0 && e->custom_index < 0) ? "not mapped yet" : (it != g.snap.values.end() && g.snap.ok ? std::to_string(it->second) : "-");
            r.state = (e->index < 0 && e->custom_index < 0) ? "not mapped yet" : e->verified ? "verified" : e->inferred() ? "mapped (game table), not seen on the EA screen: read-only here" : "not verified: read-only";
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
                if ((e->index < 0 && e->custom_index < 0)) ImGui::TextDisabled("%s", r->value.c_str());
                else ImGui::TextUnformatted(r->value.c_str());
                ImGui::TableNextColumn();
                if (e->verified) ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", r->state.c_str());
                else ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", r->state.c_str());  // 2.10: an unverified row is visible, not greyed out
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", e->verified ? ("Proof: " + e->proof).c_str()
                                                         : "Not proven in game: shown read-only. Preset Apply writes verified rows only (a row is "
                                                           "verified when slider_map has verified:true, an index and a proof).");
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
    if (ImGui::Button("Load into the game###gsload")) load_preset(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Writes the preset's sliders into the running game now (use it at the Main Menu or later, not at Press Any Key). The game "
                          "saves them in its Settings file at its next save. If the game's sliders are not found, it says so and changes nothing "
                          "in the game. NOT proven in game yet.");
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

bool game_settings_current(App& app, std::map<std::string, int>& values, std::string& layout, std::string& map_build, std::string& why) {
    first_look(app);
    read_file_now(app);  // the newest values the game saved
    if (!g.map_ok) {
        why = "no slider map: " + g.map_err;
        return false;
    }
    if (!g.snap.ok) {
        why = g.snap.why;
        return false;
    }
    values.clear();
    for (const auto& kv : g.snap.values)
        if (kv.second >= 0 && kv.second <= 100) values[kv.first] = kv.second;
    layout = g.snap.layout;
    map_build = g.map.build;
    why.clear();
    return true;
}

bool game_settings_plan(App& app, const std::map<std::string, int>& values, int& writable, std::vector<std::string>& skipped, std::string& why,
                        bool include_unverified, int* unverified_n) {
    first_look(app);
    writable = 0;
    if (unverified_n) *unverified_n = 0;
    skipped.clear();
    if (!g.map_ok) {
        why = "no slider map: " + g.map_err;
        return false;
    }
    Preset p;
    p.name = "plan";
    p.values = values;
    p.include_unverified = include_unverified;
    const Plan plan = plan_preset(p, g.map);
    writable = int(plan.values.size());
    if (unverified_n) *unverified_n = int(plan.unverified.size());
    skipped = plan.skipped;
    why.clear();
    return true;
}

bool game_settings_stage(App& app, const std::string& name, const std::map<std::string, int>& values, std::string& msg, int& staged,
                         std::vector<std::string>& skipped, bool include_unverified) {
    first_look(app);
    staged = 0;
    skipped.clear();
    if (!g.map_ok) {
        msg = "Nothing staged: no slider map (" + g.map_err + ")";
        return false;
    }
    Preset p;
    p.name = name;
    p.values = values;
    p.include_unverified = include_unverified;
    const Plan plan = plan_preset(p, g.map);
    skipped = plan.skipped;
    const bool ok = stage_pending(out_dir(app), p, g.map, stamp("%Y-%m-%dT%H:%M:%S"), &msg);
    if (ok) staged = int(plan.values.size());
    log_line(app, "game settings: " + msg);
    read_pending(app);
    return ok;
}

bool game_settings_unstage(App& app, const std::string& name, std::string& msg, const std::string& previous) {
    first_look(app);
    read_pending(app);
    Pending prev;
    std::string perr;
    const bool have_prev = !previous.empty() && Pending::parse(previous, prev, &perr);
    if (!g.have_pending) {
        msg = "Nothing staged any more (the apply tool already ran, or the staged file was discarded): the settings file is changed back only by "
              "turbo_settings_apply.exe --undo";
        if (have_prev) msg += " (the staging that was there before the load, \"" + prev.preset + "\", is not put back)";
        return false;
    }
    if (g.pending.preset != name) {
        msg = "The staged settings are another preset's (\"" + g.pending.preset + "\"): left alone";
        return false;
    }
    std::error_code ec;
    fs::remove(out_dir(app) / pending_file(), ec);
    if (ec) {
        msg = "The staged file could not be removed: " + ec.message();
        return false;
    }
    msg = "The staged settings of \"" + name + "\" were discarded";
    if (have_prev) {
        std::string err;
        if (write_text(out_dir(app) / pending_file(), previous, &err))
            msg += "; the staging that was there before the load (\"" + prev.preset + "\", " + std::to_string(prev.values.size()) + " slider(s)) is back";
        else
            msg += "; the staging that was there before the load (\"" + prev.preset + "\") could not be put back: " + err;
    } else {
        msg += " (nothing will be applied)";
    }
    log_line(app, "game settings: " + msg);
    read_pending(app);
    return true;
}

void draw_game_settings(App& app) {
    const int reads = g_state.file_reads;
    g_state = GameSettingsViewState();
    g_state.file_reads = reads;
    g_state.drawn = true;
    try {
        first_look(app);
        ImGui::TextWrapped("FC 27's own game settings (Settings > Game Settings: match, player, and the User / CPU sliders), as the game saved them "
                           "in its settings file. Load writes a preset into the running game (not proven in game yet).");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        ImGui::TextWrapped("Load = applies the VERIFIED rows to the running game now (the game saves them at its next save); when that is not "
                           "possible it says why. A Settings file saved by the game (the byte before the Custom block is 00) is refused by "
                           "turbo_settings_apply.exe --apply, so nothing is staged for it; any other file is staged for --apply with the game closed "
                           "(it backs the file up, checks it, and --undo puts it back). Turbo never touches "
                           "your careers (CmMgrC*), squads (Squads*) or ProfileOptions*. NOT proven in game yet: on 10-24 the game showed 50 after an apply; --apply refuses a Settings file "
                           "it does not recognise and then says the sliders were NOT applied (turbo_settings_apply.exe --status shows why).");
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
        // 2.13: this view is a section of Tactics & Gameplay > Gameplay presets, narrower than the old tab: under the long file line when
        // it does not fit after it (it was drawn past the right edge there, unclickable; P213 triage)
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f) ImGui::NewLine();
        if (ImGui::SmallButton("Refresh##gsrefresh")) {
            load_map_now(app);
            read_file_now(app);
            load_presets_now(app);
            read_pending(app);
        }
        g_state.map_line = g.map_ok ? std::string(map_file()) + ": " + std::to_string(g.map.entries.size()) + " sliders, " + std::to_string(verified_count()) +
                                          " verified in game"
                                    : "No slider map: " + g.map_err;
        if (g.map_ok) g_state.map_line += ", " + std::to_string(int(g.map.entries.size()) - verified_count()) + " unverified (read-only)";
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
