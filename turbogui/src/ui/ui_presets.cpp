// FC 27 LE Turbo GUI - player presets (Players tab): export the selected player (or the shown list) to Live Editor's
// preset CSV and Turbo's player JSON, import a preset file onto the player or as a new player, clone the player into
// a club, create a player from the selected one as a template. Everything runs through Turbo's Lua side
// (features/player_presets.lua, features/create_player.lua) with every value range-checked there.
#include "ui_presets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "app.h"
#include "file_picker.h"
#include "imgui.h"
#include "move_rules.h"
#include "core/archetypes.h"
#include "ui_archetype.h"
#include "ui_club_search.h"
#include "ui_images.h"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// One CSV line (RFC 4180 quoting) into cells
static std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cell += '"'; ++i; }
                else quoted = false;
            } else cell += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') { out.push_back(cell); cell.clear(); }
        else if (c != '\r') cell += c;
    }
    out.push_back(cell);
    return out;
}

PresetPreview preview_preset_file(const fs::path& file) {
    PresetPreview pv;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        pv.error = "no such file";
        return pv;
    }
    if (fs::file_size(file, ec) > 64u * 1024u * 1024u) {
        pv.error = "file too large";
        return pv;
    }
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && text[first] == '{') {
        json d = json::parse(text, nullptr, false);
        if (d.is_discarded() || !d.is_object() || d.value("format", "") != "turbo-player-preset" || !d.contains("players")) {
            pv.error = "not a Turbo player file";
            return pv;
        }
        pv.kind = "Turbo player JSON";
        pv.rows = 1;
        pv.columns = static_cast<int>(d["players"].size());
        pv.name = d.value("name", "");
        auto val = [&](const char* k) { return d["players"].contains(k) ? d["players"][k].dump() : std::string("?"); };
        pv.playerid = d.contains("playerid") ? d["playerid"].dump() : "?";
        pv.overall = val("overallrating");
        pv.potential = val("potential");
        pv.position = val("preferredposition1");
        pv.miniface = d.value("miniface", "");
        pv.ok = true;
        return pv;
    }
    // CSV: header + last non-empty line
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { if (!cur.empty()) lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() < 2) {
        pv.error = "the CSV has no player rows";
        return pv;
    }
    std::vector<std::string> header = split_csv_line(lines[0]);
    std::vector<std::string> last = split_csv_line(lines.back());
    std::map<std::string, std::string> row;
    for (size_t i = 0; i < header.size() && i < last.size(); ++i) row[lower(header[i])] = last[i];
    if (!row.count("playerid") || !(row.count("overallrating") || row.count("potential") || row.count("preferredposition1"))) {
        pv.error = "not a Live Editor player preset (needs playerid and players fields in the header)";
        return pv;
    }
    pv.kind = row.count("uid") ? "Live Editor cards CSV" : "Live Editor preset CSV";
    pv.rows = static_cast<int>(lines.size() - 1);
    pv.columns = static_cast<int>(header.size());
    auto get = [&](const char* k) { auto it = row.find(k); return it == row.end() ? std::string("?") : it->second; };
    pv.name = get("commonname");
    if (pv.name.empty() || pv.name == "?") pv.name = get("firstname") + " " + get("surname");
    if (pv.name == "? ?" || pv.name == " ") pv.name = get("name");
    pv.playerid = get("playerid");
    pv.overall = get("overallrating");
    pv.potential = get("potential");
    pv.position = get("preferredposition1");
    pv.ok = true;
    return pv;
}

// Club picker: one box for the club's name or ID with a dropdown of the matching clubs (ui_club_search.h; national teams
// are listed and marked, Lua refuses them)
void club_picker(App& app, int& teamid) { club_search_box(app, "Club", teamid, S(220.0f)); }

static const char* kOwnClubCreate =
    "Your club: he joins your squad as a reserve and your team sheet (back up your save; the squad screens show him "
    "after saving and loading the career)";

static bool own_club(App& app, int teamid) {
    int64_t user = app.bridge.state().user_team;
    return user > 0 && teamid == user;
}

// The club notes shown before the click (1.1.1: any club, yours included; Lua create_player checks the same squad
// rule and refuses with the same reason)
void club_notes(App& app, int teamid) {
    if (own_club(app, teamid)) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", kOwnClubCreate);
    if (teamid != move_rules::kFreeAgents && app.model.team(teamid)) {
        const size_t n = app.model.links_of_team(teamid).size();
        if (n >= static_cast<size_t>(move_rules::kMaxSquad))
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Not possible: this club has %zu players, the most a squad holds", n);
    }
}

static json names_json(const char* first, const char* last, const char* common, const char* jersey) {
    json n = json::object();
    if (first[0]) n["firstname"] = first;
    if (last[0]) n["surname"] = last;
    if (common[0]) n["commonname"] = common;
    if (jersey[0]) n["playerjerseyname"] = jersey;
    return n;
}

// ---------------------------------------------------------------- dialogs
// Folders: the export goes to turbo_output\players (Turbo JSON + miniface) and Live Editor's extensions\player_presets
// (preset CSV) unless the user picks others; every Browse... is Turbo's in-overlay picker (file_picker.cpp) and the
// folders are created here, in this process: Lua's fallback is cmd.exe "mkdir", whose console window took the game out
// of full screen on every export (1.1.0 playtest).
static fs::path players_folder(App& app) { return app.bridge.dir() / "players"; }
static fs::path presets_folder(App& app) { return app.bridge.root() / "extensions" / "player_presets"; }

static std::vector<std::pair<std::string, fs::path>> preset_places(App& app) {
    std::vector<std::pair<std::string, fs::path>> v = {
        {"turbo_output\\players", players_folder(app)}, {"Live Editor presets", presets_folder(app)}, {"turbo_output", app.bridge.dir()}};
    for (auto& sc : browser_shortcuts())
        if (sc.first == "Desktop" || sc.first == "OneDrive Desktop" || sc.first == "Downloads") v.push_back(sc);
    return v;
}

// Folder box with a Browse... button (in-overlay folder picker)
static void folder_row(App& app, const char* label, char* buf, size_t size, FilePicker& fp, const char* popup, const fs::path& start) {
    ImGui::SetNextItemWidth(S(400.0f));
    ImGui::InputText(label, buf, size);
    ImGui::SameLine();
    std::string browse = std::string("Browse...") + popup;
    if (ImGui::Button(browse.c_str())) {
        fp.mode = PickMode::Folder;
        fp.dir = text_path(buf);
        fp.start = start;
        fp.places = preset_places(app);
        fp.key.clear();   // the export remembers its folders itself, when it runs
        ImGui::OpenPopup(popup);
    }
    fs::path chosen;
    if (file_picker_modal(popup, fp, chosen)) std::snprintf(buf, size, "%s", path_text(chosen).c_str());
}

// Name typed for the export: safe for Windows, without a .csv / .json / .dds the user may have typed
static std::string export_base(const char* typed) {
    std::string n = safe_file_name(typed);
    std::string l = lower(n);
    for (const char* e : {".csv", ".json", ".dds"}) {
        size_t k = std::strlen(e);
        if (l.size() > k && l.compare(l.size() - k, k, e) == 0) {
            n.resize(n.size() - k);
            break;
        }
    }
    return safe_file_name(n);
}

// Files an export would replace (ui_presets.h)
std::vector<fs::path> export_clashes(const fs::path& json_dir, const fs::path& csv_dir, bool want_json, bool want_csv, bool want_mini,
                                     const std::string& base, const std::vector<int64_t>& list_ids) {
    std::vector<fs::path> out;
    if (!base.empty()) {
        std::vector<fs::path> t;
        if (want_json) t.push_back(json_dir / text_path(base + ".json"));
        if (want_json && want_mini) t.push_back(json_dir / text_path(base + ".dds"));
        if (want_csv) t.push_back(csv_dir / text_path(base + ".csv"));
        return existing_files(t);
    }
    std::set<std::string> suffixes;
    for (int64_t id : list_ids) suffixes.insert("_" + std::to_string(id));
    auto scan = [&](const fs::path& dir, const std::vector<std::string>& exts) {
        std::vector<fs::path> dirs, files;
        list_folder(dir, exts, dirs, files);
        for (const auto& f : files) {
            std::string stem = path_text(f.stem());
            size_t us = stem.rfind('_');
            if (us != std::string::npos && suffixes.count(stem.substr(us))) out.push_back(f);
        }
    };
    if (want_json) scan(json_dir, want_mini ? std::vector<std::string>{".json", ".dds"} : std::vector<std::string>{".json"});
    if (want_csv) scan(csv_dir, {".csv"});
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

static void export_dialog(App& app, const PlayerRow& p) {
    static char name[128] = "";
    static char json_dir[1024] = "";
    static char csv_dir[1024] = "";
    static bool want_csv = true, want_json = true, want_mini = true;
    static int scope = 0;
    static int64_t for_pid = 0;
    static std::vector<fs::path> clash;   // files the export would replace: confirmation shown
    static FilePicker name_fp, json_fp, csv_fp;
    if (ImGui::BeginPopupModal("##pexport", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) clash.clear();
        if (for_pid != p.playerid) {
            for_pid = p.playerid;
            name[0] = 0;
            clash.clear();
        }
        if (!json_dir[0]) {
            fs::path r = remembered_folder("export.json");
            std::snprintf(json_dir, sizeof(json_dir), "%s", path_text(r.empty() ? players_folder(app) : r).c_str());
        }
        if (!csv_dir[0]) {
            fs::path r = remembered_folder("export.csv");
            std::snprintf(csv_dir, sizeof(csv_dir), "%s", path_text(r.empty() ? presets_folder(app) : r).c_str());
        }
        const std::string def_base = preset_safe_name(p.name) + "_" + std::to_string(p.playerid);
        ImGui::Text("Export %s (ID %lld)", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::RadioButton("This player", &scope, 0);
        ImGui::SameLine();
        char lbl[96];
        std::snprintf(lbl, sizeof(lbl), "Every player shown in the list (%zu)", app.list_player_ids.size());
        ImGui::RadioButton(lbl, &scope, 1);
        if (scope == 0) {
            ImGui::SetNextItemWidth(S(260.0f));
            ImGui::InputTextWithHint("File name", def_base.c_str(), name, sizeof(name));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##pexname")) {
                name_fp.mode = PickMode::Save;
                name_fp.title = "Folder and file name of the export";
                name_fp.exts = want_json ? std::vector<std::string>{".json", ".csv"} : std::vector<std::string>{".csv", ".json"};
                name_fp.dir = text_path(want_json ? json_dir : csv_dir);
                name_fp.start = want_json ? players_folder(app) : presets_folder(app);
                name_fp.places = preset_places(app);
                name_fp.ask_overwrite = false;   // the export itself lists every file it would replace
                std::snprintf(name_fp.name, sizeof(name_fp.name), "%s", name[0] ? name : def_base.c_str());
                ImGui::OpenPopup("##pexnamepick");
            }
            fs::path chosen;
            if (file_picker_modal("##pexnamepick", name_fp, chosen)) {
                std::snprintf(name, sizeof(name), "%s", export_base(path_text(chosen.filename()).c_str()).c_str());
                std::snprintf(want_json ? json_dir : csv_dir, sizeof(json_dir), "%s", path_text(chosen.parent_path()).c_str());
                clash.clear();
            }
        }
        if (checkbox_right("Turbo player JSON (every field, names, club links, loan)", &want_json)) clash.clear();
        if (want_json) {
            ImGui::Indent();
            if (checkbox_right("Copy the miniface next to the JSON", &want_mini)) clash.clear();
            folder_row(app, "JSON folder", json_dir, sizeof(json_dir), json_fp, "##pexjsonpick", players_folder(app));
            ImGui::Unindent();
        }
        if (checkbox_right("Live Editor preset CSV (its Import from preset reads it)", &want_csv)) clash.clear();
        if (want_csv) {
            ImGui::Indent();
            folder_row(app, "CSV folder", csv_dir, sizeof(csv_dir), csv_fp, "##pexcsvpick", presets_folder(app));
            ImGui::Unindent();
        }
        std::string base = scope == 0 ? (export_base(name).empty() ? def_base : export_base(name)) : std::string();
        if (scope == 0) {
            std::string files;
            if (want_json) files += base + ".json" + (want_mini ? ", " + base + ".dds" : std::string());
            if (want_csv) files += (files.empty() ? "" : ", ") + base + ".csv";
            if (!files.empty()) ImGui::TextDisabled("Writes %s", files.c_str());
        } else {
            ImGui::TextDisabled("One file set per player, named <name>_<ID>");
        }
        bool can = (want_csv || want_json) && (scope == 0 || !app.list_player_ids.empty()) && (!want_json || json_dir[0]) &&
                   (!want_csv || csv_dir[0]);
        auto run = [&]() {
            std::string err;
            if ((want_json && !ensure_folder(text_path(json_dir), &err)) || (want_csv && !ensure_folder(text_path(csv_dir), &err))) {
                app.notify("Export: " + err, true);
                return;
            }
            json o = {{"mode", "export"}, {"csv", want_csv}, {"json", want_json}, {"miniface", want_mini}, {"preset_dir", csv_dir},
                      {"json_dir", json_dir}};
            if (scope == 0) {
                o["playerid"] = p.playerid;
                o["name"] = base;
            } else {
                o["playerids"] = app.list_player_ids;
            }
            json cmd = {{"op", "run"}, {"module", "player_presets"}, {"overrides", o}};
            if (cmd.dump().size() > 3900) {
                app.notify("Too many players for one export (" + std::to_string(app.list_player_ids.size()) + "): narrow the list with the filters", true);
                return;
            }
            if (want_json) remember_folder("export.json", text_path(json_dir));
            if (want_csv) remember_folder("export.csv", text_path(csv_dir));
            app.send(cmd, scope == 0 ? "Export player" : "Export list");
        };
        if (!clash.empty()) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%zu file%s already exist%s and will be replaced:", clash.size(), clash.size() == 1 ? "" : "s",
                               clash.size() == 1 ? "s" : "");
            for (size_t i = 0; i < clash.size() && i < 6; ++i) ImGui::BulletText("%s", path_text(clash[i]).c_str());
            if (clash.size() > 6) ImGui::TextDisabled("and %zu more", clash.size() - 6);
            if (ImGui::Button("Replace and export")) {
                run();
                clash.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Back##pexport")) clash.clear();
        } else {
            if (!can) ImGui::BeginDisabled();
            if (ImGui::Button(scope == 0 ? "Export player" : "Export list")) {
                clash = export_clashes(text_path(json_dir), text_path(csv_dir), want_json, want_csv, want_mini, base,
                                       scope == 0 ? std::vector<int64_t>() : app.list_player_ids);
                if (clash.empty()) {
                    run();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!can) ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pexport")) {
            clash.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void import_dialog(App& app, const PlayerRow& p) {
    static char file[1024] = "";
    static FilePicker picker;
    static std::string previewed;
    static PresetPreview pv;
    // Names is off by default: it renames the player to the file's names (1.2.0 turned names into common names this way)
    static bool groups[8] = {true, true, true, true, true, true, false, true};
    static const char* group_names[8] = {"profile", "attributes", "positions", "playstyles", "appearance", "contract", "names", "miniface"};
    static const char* group_labels[8] = {"Profile (overall, potential, age, body, foot, skill moves...)", "Attributes", "Positions & roles",
                                          "PlayStyles", "Appearance (head, hair, skin, kit, animations...)", "Contract",
                                          "Names (renames him to the file's names)", "Miniface (Turbo JSON only)"};
    static int target = 0;
    static int teamid = 111592;
    if (ImGui::BeginPopupModal("##pimport", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Import a preset onto %s (ID %lld) or as a new player", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::SetNextItemWidth(S(480.0f));
        ImGui::InputTextWithHint("##pfile", "C:\\...\\player.csv or .json", file, sizeof(file));
        ImGui::SameLine();
        if (ImGui::Button("Browse...##preset")) {
            picker.mode = PickMode::Open;
            picker.title = "Choose a player preset (Live Editor CSV or Turbo JSON)";
            picker.exts = {".csv", ".json"};
            picker.key = "import.preset";
            picker.start = players_folder(app);
            picker.places = preset_places(app);
            if (file[0]) picker.dir = text_path(file).parent_path();
            ImGui::OpenPopup("##pfbrowser");
        }
        fs::path chosen;
        if (file_picker_modal("##pfbrowser", picker, chosen)) std::snprintf(file, sizeof(file), "%s", path_text(chosen).c_str());
        if (previewed != file) {
            previewed = file;
            pv = file[0] ? preview_preset_file(text_path(file)) : PresetPreview();
        }
        if (file[0] && !pv.ok) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", pv.error.c_str());
        if (pv.ok) {
            ImGui::TextDisabled("%s: %d row%s, %d columns; the newest row is used", pv.kind.c_str(), pv.rows, pv.rows == 1 ? "" : "s", pv.columns);
            ImGui::Text("%s  (preset player %s)  OVR %s  POT %s  position %s%s", pv.name.c_str(), pv.playerid.c_str(), pv.overall.c_str(),
                        pv.potential.c_str(), pv.position.c_str(), pv.miniface.empty() ? "" : "  + miniface");
        }
        ImGui::SeparatorText("Target");
        char lbl[128];
        std::snprintf(lbl, sizeof(lbl), "This player: %s (ID %lld)", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::RadioButton(lbl, &target, 0);
        ImGui::RadioButton("A new player in a club", &target, 1);
        if (target == 1) {
            club_picker(app, teamid);
            club_notes(app, teamid);
        } else {
            ImGui::SeparatorText("What to copy");
            for (int i = 0; i < 8; ++i) checkbox_right(group_labels[i], &groups[i]);
        }
        bool any_group = std::any_of(std::begin(groups), std::end(groups), [](bool b) { return b; });
        bool can = pv.ok && (target == 1 ? (app.model.team(teamid) != nullptr) : any_group);
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button(target == 0 ? "Import onto player" : "Import as new player")) {
            // a miniface goes to <Live Editor>\mods\legacy\data\ui\imgAssets\heads (the old one to turbo_output\miniface_backups):
            // folders made here, so Lua never needs cmd.exe for them
            if (!pv.miniface.empty() && (target == 1 || groups[7])) {
                ensure_folder(app.bridge.root() / "mods" / "legacy" / "data" / "ui" / "imgAssets" / "heads");
                ensure_folder(app.bridge.dir() / "miniface_backups");
            }
            if (target == 0) {
                json g = json::array();
                for (int i = 0; i < 8; ++i) if (groups[i]) g.push_back(group_names[i]);
                app.send({{"op", "run"}, {"module", "player_presets"},
                          {"overrides", {{"mode", "import"}, {"file", file}, {"playerid", p.playerid}, {"groups", g}}}},
                         "Import preset");
            } else {
                app.send({{"op", "run"}, {"module", "create_player"},
                          {"overrides", {{"source", {{"file", file}}}, {"teamid", teamid}}}},
                         "Import as new player");
            }
            ImGui::CloseCurrentPopup();
        }
        if (!can) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pimport")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void clone_dialog(App& app, const PlayerRow& p) {
    static int teamid = 111592;
    static int jersey = 0;
    static char first[64] = "", last[64] = "", common[64] = "", jname[64] = "";
    if (ImGui::BeginPopupModal("##pclone", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Clone %s (ID %lld): a new player with the same fields", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::TextDisabled("The copy gets the first free player ID below 460000, a free shirt number and the reserve slot.");
        club_picker(app, teamid);
        club_notes(app, teamid);
        form_label("Shirt number", "0 = the first free number at the club");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##jersey", &jersey, 0);
        jersey = std::max(0, std::min(jersey, 99));
        ImGui::SeparatorText("Names (empty = same as the original)");
        form_label("First name");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##first", first, sizeof(first));
        form_label("Surname");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##last", last, sizeof(last));
        form_label("Common name");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##common", common, sizeof(common));
        form_label("Jersey name");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##jname", jname, sizeof(jname));
        bool can = app.model.team(teamid) != nullptr;
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Clone player")) {
            json o = {{"source", {{"playerid", p.playerid}}}, {"teamid", teamid}, {"jersey", jersey}};
            json n = names_json(first, last, common, jname);
            if (!n.empty()) o["names"] = n;
            app.send({{"op", "run"}, {"module", "create_player"}, {"overrides", o}}, "Clone player");
            ImGui::CloseCurrentPopup();
        }
        if (!can) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pclone")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- Create player: the Player ID box (core/player_id.h)
// Empty = automatic (Lua's first free id, as before). A typed id is checked against every per-player table of this career
// and the asset catalog while it is typed (the verdict below the box), again from a fresh scan at the click, and once more
// by Lua's create_player and Turbo.dll's player_create before anything is written.
static CreateIdState g_create_id;
const CreateIdState& create_id_state() { return g_create_id; }

struct CreateIdBox {
    char text[16] = "";
    player_id::Use use;
    uint64_t use_version = ~0ull;
    bool typed_off = false;
    // the verdict is worked out again only when one of its inputs changes
    std::string v_text, v_lang;
    const void* v_cat = nullptr;
    uint64_t v_version = ~0ull;
    bool v_off = false, v_done = false;
    // the picker's list
    std::vector<player_id::Hidden> hidden;
    const void* h_cat = nullptr;
    uint64_t h_version = ~0ull;
    std::string h_lang;
    char search[64] = "";
    int want = 0;
};

static bool typed_ids_off(App& app) {
    std::error_code ec;
    return fs::exists(app.turbo_output() / player_id::typed_off_name(), ec);
}

static const char* const kPlayerIdTip =
    "Empty = automatic: the first free id above every existing one (ids that carry a game head are passed over).\n"
    "A typed id must be 1 to 459,999 and free in every per-player table of this career (players, names, club links, loans, "
    "transfers, career tables); Turbo checks it again right before the player is created.\n"
    "When the game files ship a 3D head or a callname for the id, the new player uses them.";

// The box, its verdict lines and the "Use a hidden player's id" picker; returns the verdict for the Create button
static const player_id::Verdict& player_id_box(App& app, CreateIdBox& b) {
    if (ImGui::IsWindowAppearing() || b.use_version != app.model.version()) {
        b.use = player_id::scan(app.db);
        b.use_version = app.model.version();
        b.typed_off = typed_ids_off(app);
    }
    const auto cat = app.asset_catalog();
    const std::string& lang = app.callnames.lang;
    if (!b.v_done || b.v_text != b.text || b.v_cat != cat.get() || b.v_version != b.use_version || b.v_lang != lang || b.v_off != b.typed_off) {
        g_create_id.verdict = player_id::check(b.text, b.use, cat.get(), lang, b.typed_off);
        b.v_text = b.text;
        b.v_cat = cat.get();
        b.v_version = b.use_version;
        b.v_lang = lang;
        b.v_off = b.typed_off;
        b.v_done = true;
    }
    g_create_id.text = b.text;
    g_create_id.typed_off = b.typed_off;
    const player_id::Verdict& v = g_create_id.verdict;

    ImGui::SeparatorText("Player ID");
    form_label("Player ID", kPlayerIdTip);
    ImGui::SetNextItemWidth(S(110.0f));
    if (b.typed_off) ImGui::BeginDisabled();
    ImGui::InputTextWithHint("##playerid", "automatic", b.text, sizeof(b.text), ImGuiInputTextFlags_CharsDecimal);
    if (b.typed_off) ImGui::EndDisabled();
    ImGui::SameLine();
    std::string pick_why;
    if (b.typed_off) pick_why = std::string("typed player ids are switched off (turbo_output\\") + player_id::typed_off_name() + ")";
    else if (!cat->loaded()) pick_why = "no asset catalog is loaded: " + cat->status_line();
    else if (cat->hidden_off()) pick_why = "hidden assets are switched off (turbo_output\\asset_hidden_off.txt)";
    if (!pick_why.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Use a hidden player's id...")) {
        b.search[0] = 0;
        ImGui::OpenPopup("##pidpick");
    }
    if (!pick_why.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", pick_why.empty() ? "Ids whose 3D head or callname ships with the game and that no player of this save has" : pick_why.c_str());

    const ImVec4 warn(1, 0.6f, 0.3f, 1), good(0.45f, 0.85f, 0.45f, 1);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(560.0f));
    if (!v.ok) {
        std::string why = v.reason;
        if (!v.owners.empty() && v.owners.front() == "players") {
            const std::string who = app.model.player_name(v.id);
            if (!who.empty()) why += " (" + who + ")";
        }
        ImGui::TextColored(warn, "Not possible: %s", why.c_str());
    } else if (v.automatic) {
        ImGui::TextDisabled("Automatic: the first free id above every existing one, below 460,000.");
    } else {
        ImGui::TextColored(good, "Player ID %lld is free in this career.", static_cast<long long>(v.id));
        if (!v.assets_line.empty()) ImGui::TextWrapped("%s", v.assets_line.c_str());
        for (const auto& n : v.notes) ImGui::TextDisabled("Note: %s", n.c_str());
    }
    ImGui::PopTextWrapPos();

    if (ImGui::BeginPopupModal("##pidpick", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (b.h_cat != cat.get() || b.h_version != b.use_version || b.h_lang != lang) {
            b.hidden = player_id::hidden_ids(cat.get(), b.use, lang);
            b.h_cat = cat.get();
            b.h_version = b.use_version;
            b.h_lang = lang;
        }
        ImGui::TextUnformatted("Use a hidden player's id");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(560.0f));
        ImGui::TextDisabled("Ids whose 3D head or callname (%s) ships with the game and that no player of this save has, usually players "
                            "who left the database. A new player with the id uses them. Names come from the asset catalog on this PC.",
                            lang.empty() ? "any language" : lang.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetNextItemWidth(S(240.0f));
        ImGui::InputTextWithHint("##pidsearch", "name or id", b.search, sizeof(b.search));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(200.0f));
        if (ImGui::BeginCombo("##pidwant", player_id::want_label(static_cast<player_id::Want>(b.want)))) {
            for (int w = 0; w < 4; ++w)
                if (ImGui::Selectable(player_id::want_label(static_cast<player_id::Want>(w)), b.want == w)) b.want = w;
            ImGui::EndCombo();
        }
        const std::vector<const player_id::Hidden*> rows = player_id::pick(b.hidden, static_cast<player_id::Want>(b.want), b.search);
        g_create_id.hidden = b.hidden.size();
        g_create_id.shown = rows.size();
        ImGui::TextDisabled("%zu of %zu ids", rows.size(), b.hidden.size());
        const ImGuiTableFlags tf = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("##pidtable", 5, tf, ImVec2(S(560.0f), S(300.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("3D head");
            ImGui::TableSetupColumn("Callname");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(rows.size()));
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                    const player_id::Hidden& h = *rows[static_cast<size_t>(i)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", static_cast<long long>(h.id));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(h.name.empty() ? "(no name in the catalog)" : h.name.c_str());
                    if (!h.source.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", assets::name_source_label(h.source).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(h.head ? (h.miniface ? "yes + miniface" : "yes") : "-");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(h.callname ? "yes" : "-");
                    ImGui::TableNextColumn();
                    char lbl[40];
                    std::snprintf(lbl, sizeof(lbl), "Use##pid%lld", static_cast<long long>(h.id));
                    if (ImGui::SmallButton(lbl)) {
                        std::snprintf(b.text, sizeof(b.text), "%lld", static_cast<long long>(h.id));
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Cancel##pidpick")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return v;
}

// The overrides' id part of a create: nothing for an automatic id; "playerid" for a typed one, and "id_assets" when a
// current catalog knows a head or a callname for it (Lua gives that head: headassetid = id, headclasscode 0)
static void put_player_id(json& o, const player_id::Verdict& v) {
    if (!v.ok || v.automatic) return;
    o["playerid"] = v.id;
    const player_id::Assets& a = v.assets;
    if (a.loaded && a.current && a.any())
        o["id_assets"] = {{"head3d", v.give_head}, {"headvariation", a.head_var}, {"callname", a.callname()}, {"miniface", a.miniface}};
}

// Create player's archetype (2.3): the template's 34 attributes reshaped so that the position's formula gives the Overall
// box (core/archetypes.h apply_target), recomputed only when an input changes
struct CreateArchetype {
    int arch = -1, position = -1, target = 0;
    int64_t template_pid = 0;
    overall::Attrs from{};
    bool have = false;  // the template has all 34 attributes
    archetypes::TargetResult r;
};
static const CreateArchetype& create_archetype(App& app, const PlayerRow& p, int arch, int position, int target) {
    static CreateArchetype c;
    overall::Attrs from{};
    bool have = false;
    if (const Table* t = app.db.table("players")) {
        have = true;
        for (int i = 0; i < overall::kAttrCount && have; ++i) {
            const Field* f = t->field(overall::kAttrNames[i]);
            have = f && f->type == FieldType::Int;
            if (have) from[static_cast<size_t>(i)] = static_cast<int>(app.db.get_int(*t, p.rec, overall::kAttrNames[i], 0));
        }
    }
    if (c.arch == arch && c.position == position && c.target == target && c.template_pid == p.playerid && c.have == have && c.from == from)
        return c;
    c.arch = arch;
    c.position = position;
    c.target = target;
    c.template_pid = p.playerid;
    c.from = from;
    c.have = have;
    c.r = archetypes::TargetResult();
    if (have && arch >= 0 && arch < static_cast<int>(archetypes::all().size()))
        archetypes::apply_target(archetypes::all()[static_cast<size_t>(arch)], position, from, target, c.r);
    return c;
}

static void create_dialog(App& app, const PlayerRow& p) {
    static CreateIdBox idbox;
    static int teamid = 111592;
    static char first[64] = "", last[64] = "", common[64] = "", jname[64] = "";
    static int overall = 65, potential = 70, position = 25, birth_year = 2004, nationality = 0, foot = 1, height = 180, weight = 75;
    static int arch = -1;
    static int64_t for_pid = 0;
    if (ImGui::BeginPopupModal("##pcreate", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (for_pid != p.playerid) {
            for_pid = p.playerid;
            overall = p.overall;
            potential = p.potential;
            position = p.position < 0 ? 25 : p.position;
            if (const Table* t = app.db.table("players")) {
                nationality = static_cast<int>(app.db.get_int(*t, p.rec, "nationality", 0));
                foot = static_cast<int>(app.db.get_int(*t, p.rec, "preferredfoot", 1));
                height = static_cast<int>(app.db.get_int(*t, p.rec, "height", 180));
                weight = static_cast<int>(app.db.get_int(*t, p.rec, "weight", 75));
            }
            birth_year = app.today().year - std::max(16, p.age < 0 ? 22 : p.age);
        }
        ImGui::Text("Create a player, with %s (ID %lld) as the template for everything not set here", p.name.c_str(),
                    static_cast<long long>(p.playerid));
        ImGui::TextDisabled("Appearance, attributes, PlayStyles and the rest are copied from the template (the attributes reshaped when an "
                            "archetype is chosen); edit them afterwards.");
        ImGui::SeparatorText("Names");
        form_label("First name");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##first", first, sizeof(first));
        form_label("Surname");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##last", last, sizeof(last));
        form_label("Common name");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##common", common, sizeof(common));
        form_label("Jersey name");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("##jname", jname, sizeof(jname));
        ImGui::SeparatorText("Club");
        club_picker(app, teamid);
        club_notes(app, teamid);
        const player_id::Verdict& idv = player_id_box(app, idbox);
        ImGui::SeparatorText("Profile");
        form_label("Position", "The game's positions; internal codes behind the toggle in the list");
        position_combo("##position", position, S(200.0f));
        form_label("Overall", "With an archetype: the target the attributes are reshaped to (the position's formula gives it)");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##overall", &overall, 0);
        form_label("Archetype", "Optional (2.3): the template's attributes reshaped to a role so that the formula for the position gives the "
                                "Overall above; None keeps the template's attributes");
        archetype_combo("##carch", arch, position, S(220.0f), "None: the template's attributes");
        form_label("Potential");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##potential", &potential, 0);
        form_label("Birth year");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##birthyear", &birth_year, 0);
        form_label("Nationality ID");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##nationality", &nationality, 0);
        form_label("Preferred foot");
        ImGui::SetNextItemWidth(S(110.0f));
        if (ImGui::BeginCombo("##foot", foot == 2 ? "Left" : "Right")) {
            if (ImGui::Selectable("Right", foot == 1)) foot = 1;
            if (ImGui::Selectable("Left", foot == 2)) foot = 2;
            ImGui::EndCombo();
        }
        form_label("Height (cm)");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##height", &height, 0);
        form_label("Weight (kg)");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##weight", &weight, 0);
        overall = std::max(1, std::min(overall, 99));
        potential = std::max(1, std::min(potential, 99));
        birth_year = std::max(1950, std::min(birth_year, 2030));
        // the archetype's attributes for that overall, shown before Create
        const CreateArchetype* ca = nullptr;
        bool arch_ok = true;
        if (arch >= 0) {
            ca = &create_archetype(app, p, arch, position, overall);
            ImGui::SeparatorText("Attributes");
            if (!ca->have) {
                ImGui::TextDisabled("The archetype needs the 34 attribute fields; this database lacks some.");
                arch_ok = false;
            } else {
                ImGui::Text("%s: the template's attributes reshaped for %s", archetypes::all()[static_cast<size_t>(arch)].name, position_name(position));
                const int closest = archetype_result_lines(ca->r, position);
                arch_ok = ca->r.ok;
                if (closest > 0) {
                    char ub[48];
                    std::snprintf(ub, sizeof(ub), "Use %d##cclosest", closest);
                    if (ImGui::Button(ub)) overall = closest;
                } else {
                    int changed = 0;
                    for (int i = 0; i < overall::kAttrCount; ++i) changed += ca->r.attrs[static_cast<size_t>(i)] != ca->from[static_cast<size_t>(i)];
                    char hdr[96];
                    std::snprintf(hdr, sizeof(hdr), "%d attributes differ from the template###carchchg", changed);
                    if (ImGui::TreeNode(hdr)) {
                        attribute_changes_table(ca->from, ca->r.attrs, &archetypes::all()[static_cast<size_t>(arch)]);
                        ImGui::TreePop();
                    }
                }
            }
        }
        bool has_name = first[0] || last[0] || common[0];
        bool can = has_name && app.model.team(teamid) != nullptr && idv.ok && arch_ok;
        if (!has_name) ImGui::TextDisabled("Give the player a name.");
        if (!can) ImGui::BeginDisabled();
        bool create = ImGui::Button("Create player");
        if (create) {
            // the id again from a fresh scan: the tables may have changed since the box was checked
            const player_id::Use fresh = player_id::scan(app.db);
            const player_id::Verdict now = player_id::check(idbox.text, fresh, app.asset_catalog().get(), app.callnames.lang, typed_ids_off(app));
            g_create_id.verdict = now;
            idbox.v_done = false;
            if (!now.ok) {
                app.notify("Create player: " + now.reason, true);
                create = false;
            }
        }
        if (create) {
            const player_id::Verdict& now = g_create_id.verdict;
            GameDate bd{birth_year, 7, 1};
            json set = json::object();
            const Table* pt = app.db.table("players");
            auto put = [&](const char* field, int64_t v) {
                if (pt && pt->field(field)) set[field] = v;   // only fields this game's players table has
            };
            put("overallrating", overall);
            put("potential", potential);
            put("preferredposition1", position);
            // the archetype's attributes: all 34 as previewed (the template's could still move before Lua copies them)
            if (ca && ca->have && ca->r.ok)
                for (int i = 0; i < overall::kAttrCount; ++i) put(overall::kAttrNames[i], ca->r.attrs[static_cast<size_t>(i)]);
            put("nationality", nationality);
            put("preferredfoot", foot);
            put("height", height);
            put("weight", weight);
            put("birthdate", gregorian_days_from_date(bd));
            json o = {{"source", {{"playerid", p.playerid}}}, {"teamid", teamid}, {"set", set},
                      {"names", names_json(first, last, common, jname)}};
            put_player_id(o, now);
            g_create_id.sent = o;
            app.log(now.automatic ? std::string("Create player: automatic player id")
                                  : "Create player: player id " + std::to_string(now.id) + " typed" +
                                        (o.contains("id_assets") ? " (game files: " + now.assets_line + ")" : std::string()));
            app.send({{"op", "run"}, {"module", "create_player"}, {"overrides", o}}, "Create player");
            ImGui::CloseCurrentPopup();
        }
        if (!can) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pcreate")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Repair names (player_presets mode repair_names): the editedplayernames rows Turbo 1.2.0's Import left (his shown name
// as a common name, first name, surname and shirt name empty) are rewritten in place with the player's own names.
// Check first; Repair writes.
static void repair_names_dialog(App& app) {
    if (ImGui::BeginPopupModal("##prepair", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(460.0f));
        ImGui::TextUnformatted("Repair player names changed by Turbo 1.2.0's Import");
        ImGui::TextDisabled("Import with the Names group could turn a player's first name and surname into one common name "
                            "and leave his shirt without a name. Repair gives every such player his own names back "
                            "(first name, surname, common name and shirt name from the game's database), in this career.");
        ImGui::TextDisabled("Only rows that match that pattern exactly are changed; any other name is left alone. "
                            "Check lists them without changing anything. Save the career afterwards to keep the repair.");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Check##prepair")) {
            app.send({{"op", "run"}, {"module", "player_presets"}, {"overrides", {{"mode", "repair_names"}, {"check", true}}}},
                     "Repair names (check)");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Repair names##prepair")) {
            app.send({{"op", "run"}, {"module", "player_presets"}, {"overrides", {{"mode", "repair_names"}}}}, "Repair names");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##prepair")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void player_preset_buttons(App& app, const PlayerRow& p) {
    bool made = app.bridge.state().is_turbo_made("create_player");
    if (ImGui::Button("Export...")) ImGui::OpenPopup("##pexport");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Live Editor preset CSV (extensions\\player_presets) and Turbo JSON with the miniface");
    ImGui::SameLine();
    if (ImGui::Button("Import...")) ImGui::OpenPopup("##pimport");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A Live Editor preset CSV (FC 27 or FC 26) or a Turbo JSON onto this player, or as a new player");
    ImGui::SameLine();
    if (ImGui::Button("Clone...")) ImGui::OpenPopup("##pclone");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new player with the same fields in a club of your choice");
    ImGui::SameLine();
    if (ImGui::Button("Create player...")) ImGui::OpenPopup("##pcreate");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new player from this one as the template: names, club, position, ratings, age");
    ImGui::SameLine();
    if (ImGui::Button("From CMTracker...")) ImGui::OpenPopup("##pcmt");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A fully populated new player picked from the CMTracker CSV library: stats, positions, PlayStyles, appearance, names, miniface");
    // on the same row when it fits (the row is long at small window widths)
    const char* repair_label = "Repair names...";
    const float repair_w = ImGui::CalcTextSize(repair_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < repair_w) ImGui::NewLine();
    if (ImGui::Button(repair_label)) ImGui::OpenPopup("##prepair");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gives back their own names to players whose names Turbo 1.2.0's Import turned into one common name");
    if (made) {
        ImGui::SameLine();
        ImGui::TextDisabled("(new players are rows Turbo adds to the database: back up your save first)");
    }
    export_dialog(app, p);
    import_dialog(app, p);
    clone_dialog(app, p);
    create_dialog(app, p);
    cmtracker_dialog(app);
    repair_names_dialog(app);
}

}  // namespace turbo
