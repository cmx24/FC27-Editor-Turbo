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
#include <sstream>

#include "app.h"
#include "imgui.h"
#include "move_rules.h"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool is_preset_file(const fs::path& p) {
    std::string e = lower(p.extension().string());
    return e == ".csv" || e == ".json";
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

// ---------------------------------------------------------------- shared widgets
// Modal file browser for preset files (CSV / JSON); returns true when a file was chosen
static bool preset_file_browser(const char* id, fs::path& cur_dir, fs::path& out) {
    bool chosen = false;
    ImGui::SetNextWindowSize(ImVec2(S(640.0f), S(480.0f)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        static char path_buf[1024];
        static fs::path shown;
        std::error_code ec;
        if (cur_dir.empty() || !fs::is_directory(cur_dir, ec)) cur_dir = fs::current_path(ec);
        if (shown != cur_dir) {
            shown = cur_dir;
            std::snprintf(path_buf, sizeof(path_buf), "%s", cur_dir.string().c_str());
        }
        ImGui::SetNextItemWidth(-S(70.0f));
        if (ImGui::InputText("##ppath", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            fs::path typed(path_buf);
            if (fs::is_directory(typed, ec)) cur_dir = typed;
            else if (fs::is_regular_file(typed, ec) && is_preset_file(typed)) {
                out = typed;
                chosen = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Up") && cur_dir.has_parent_path() && cur_dir.parent_path() != cur_dir) cur_dir = cur_dir.parent_path();
#ifdef _WIN32
        for (char d = 'C'; d <= 'Z'; ++d) {
            char root[4] = {d, ':', '\\', 0};
            if (fs::is_directory(fs::path(root), ec)) {
                ImGui::SameLine();
                char lbl[8] = {d, ':', 0};
                if (ImGui::SmallButton(lbl)) cur_dir = fs::path(root);
            }
        }
#endif
        ImGui::BeginChild("##pfiles", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
        std::vector<fs::path> dirs, files;
        for (fs::directory_iterator it(cur_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) dirs.push_back(it->path());
            else if (it->is_regular_file(e2) && is_preset_file(it->path())) files.push_back(it->path());
            if (dirs.size() + files.size() > 5000) break;
        }
        auto by_name = [](const fs::path& a, const fs::path& b) { return lower(a.filename().string()) < lower(b.filename().string()); };
        std::sort(dirs.begin(), dirs.end(), by_name);
        std::sort(files.begin(), files.end(), by_name);
        for (const auto& d : dirs) {
            std::string lbl = "[" + d.filename().string() + "]";
            if (ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) cur_dir = d;
        }
        for (const auto& f : files) {
            if (ImGui::Selectable(f.filename().string().c_str())) {
                out = f;
                chosen = true;
            }
        }
        if (dirs.empty() && files.empty()) ImGui::TextDisabled("No folders or preset files (.csv, .json) here.");
        ImGui::EndChild();
        ImGui::TextDisabled("Double-click a folder to open it, click a file to use it.");
        ImGui::SameLine(ImGui::GetWindowWidth() - S(90.0f));
        if (ImGui::Button("Cancel##pbrowser")) ImGui::CloseCurrentPopup();
        if (chosen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return chosen;
}

// Club picker: an ID box plus a searchable list of clubs (national teams are shown but refused by Lua)
static void club_picker(App& app, int& teamid, char* search, size_t search_size) {
    ImGui::SetNextItemWidth(S(110.0f));
    ImGui::InputInt("Club ID", &teamid, 0);
    ImGui::SameLine();
    const TeamRow* t = app.model.team(teamid);
    if (t) ImGui::TextDisabled("%s%s", t->name.c_str(), app.model.is_national_team(teamid) ? " [national team]" : "");
    else ImGui::TextDisabled("(no such team)");
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##clubsearch", "search a club", search, static_cast<int>(search_size));
    std::string q = lower(search);
    if (!q.empty()) {
        ImGui::BeginChild("##clublist", ImVec2(S(320.0f), S(110.0f)), ImGuiChildFlags_Borders);
        int shown = 0;
        for (const auto& tr : app.model.teams()) {
            if (lower(tr.name).find(q) == std::string::npos) continue;
            char lbl[160];
            std::snprintf(lbl, sizeof(lbl), "%s (%lld)%s", tr.name.c_str(), static_cast<long long>(tr.teamid),
                          app.model.is_national_team(tr.teamid) ? " [national]" : "");
            if (ImGui::Selectable(lbl, teamid == tr.teamid)) teamid = static_cast<int>(tr.teamid);
            if (++shown >= 60) break;
        }
        ImGui::EndChild();
    }
}

static const char* kOwnClubCreate =
    "Your club: he joins your squad as a reserve and your team sheet (back up your save; the squad screens show him "
    "after saving and loading the career)";

static bool own_club(App& app, int teamid) {
    int64_t user = app.bridge.state().user_team;
    return user > 0 && teamid == user;
}

// The club notes shown before the click (1.1.1: any club, yours included; Lua create_player checks the same squad
// rule and refuses with the same reason)
static void club_notes(App& app, int teamid) {
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
static void export_dialog(App& app, const PlayerRow& p) {
    static char name[64] = "";
    static char preset_dir[512] = "";
    static bool want_csv = true, want_json = true, want_mini = true;
    static int scope = 0;
    static int64_t for_pid = 0;
    if (ImGui::BeginPopupModal("##pexport", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (for_pid != p.playerid) {
            for_pid = p.playerid;
            name[0] = 0;
        }
        if (!preset_dir[0]) std::snprintf(preset_dir, sizeof(preset_dir), "%s", (app.bridge.root() / "extensions" / "player_presets").string().c_str());
        ImGui::Text("Export %s (ID %lld)", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::RadioButton("This player", &scope, 0);
        ImGui::SameLine();
        char lbl[96];
        std::snprintf(lbl, sizeof(lbl), "Every player shown in the list (%zu)", app.list_player_ids.size());
        ImGui::RadioButton(lbl, &scope, 1);
        ImGui::Checkbox("Live Editor preset CSV (its Import from preset reads it)", &want_csv);
        ImGui::SetNextItemWidth(S(420.0f));
        ImGui::InputText("Preset folder", preset_dir, sizeof(preset_dir));
        ImGui::Checkbox("Turbo player JSON in turbo_output\\players (every field, names, club links, loan)", &want_json);
        ImGui::Checkbox("Copy the miniface next to the JSON", &want_mini);
        if (scope == 0) {
            ImGui::SetNextItemWidth(S(260.0f));
            ImGui::InputTextWithHint("File name", "empty = player name + ID", name, sizeof(name));
        }
        bool can = (want_csv || want_json) && (scope == 0 || !app.list_player_ids.empty());
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button(scope == 0 ? "Export player" : "Export list")) {
            json o = {{"mode", "export"}, {"csv", want_csv}, {"json", want_json}, {"miniface", want_mini}, {"preset_dir", preset_dir}};
            if (scope == 0) {
                o["playerid"] = p.playerid;
                if (name[0]) o["name"] = name;
            } else {
                o["playerids"] = app.list_player_ids;
            }
            json cmd = {{"op", "run"}, {"module", "player_presets"}, {"overrides", o}};
            if (cmd.dump().size() > 3900)
                app.notify("Too many players for one export (" + std::to_string(app.list_player_ids.size()) + "): narrow the list with the filters", true);
            else
                app.send(cmd, scope == 0 ? "Export player" : "Export list");
            ImGui::CloseCurrentPopup();
        }
        if (!can) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pexport")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void import_dialog(App& app, const PlayerRow& p) {
    static char file[1024] = "";
    static fs::path browse_dir;
    static std::string previewed;
    static PresetPreview pv;
    static bool groups[8] = {true, true, true, true, true, true, true, true};
    static const char* group_names[8] = {"profile", "attributes", "positions", "playstyles", "appearance", "contract", "names", "miniface"};
    static const char* group_labels[8] = {"Profile (overall, potential, age, body, foot, skill moves...)", "Attributes", "Positions & roles",
                                          "PlayStyles", "Appearance (head, hair, skin, kit, animations...)", "Contract", "Names", "Miniface (Turbo JSON only)"};
    static int target = 0;
    static int teamid = 111592;
    static char club_search[64] = "";
    if (ImGui::BeginPopupModal("##pimport", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (browse_dir.empty()) browse_dir = app.bridge.root() / "extensions" / "player_presets";
        ImGui::Text("Import a preset onto %s (ID %lld) or as a new player", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::SetNextItemWidth(S(480.0f));
        ImGui::InputTextWithHint("##pfile", "C:\\...\\player.csv or .json", file, sizeof(file));
        ImGui::SameLine();
        if (ImGui::Button("Browse...##preset")) ImGui::OpenPopup("##pfbrowser");
        fs::path chosen;
        if (preset_file_browser("##pfbrowser", browse_dir, chosen)) std::snprintf(file, sizeof(file), "%s", chosen.string().c_str());
        if (previewed != file) {
            previewed = file;
            pv = file[0] ? preview_preset_file(fs::path(file)) : PresetPreview();
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
            club_picker(app, teamid, club_search, sizeof(club_search));
            club_notes(app, teamid);
        } else {
            ImGui::SeparatorText("What to copy");
            for (int i = 0; i < 8; ++i) ImGui::Checkbox(group_labels[i], &groups[i]);
        }
        bool any_group = std::any_of(std::begin(groups), std::end(groups), [](bool b) { return b; });
        bool can = pv.ok && (target == 1 ? (app.model.team(teamid) != nullptr) : any_group);
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button(target == 0 ? "Import onto player" : "Import as new player")) {
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
    static char club_search[64] = "";
    static char first[64] = "", last[64] = "", common[64] = "", jname[64] = "";
    if (ImGui::BeginPopupModal("##pclone", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Clone %s (ID %lld): a new player with the same fields", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::TextDisabled("The copy gets the first free player ID below 460000, a free shirt number and the reserve slot.");
        club_picker(app, teamid, club_search, sizeof(club_search));
        club_notes(app, teamid);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Shirt number (0 = first free)", &jersey, 0);
        jersey = std::max(0, std::min(jersey, 99));
        ImGui::SeparatorText("Names (empty = same as the original)");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("First name", first, sizeof(first));
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("Surname", last, sizeof(last));
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("Common name", common, sizeof(common));
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("Jersey name", jname, sizeof(jname));
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

static void create_dialog(App& app, const PlayerRow& p) {
    static int teamid = 111592;
    static char club_search[64] = "";
    static char first[64] = "", last[64] = "", common[64] = "", jname[64] = "";
    static int overall = 65, potential = 70, position = 25, birth_year = 2004, nationality = 0, foot = 1, height = 180, weight = 75;
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
        ImGui::TextDisabled("Appearance, attributes, PlayStyles and the rest are copied from the template; edit them afterwards.");
        ImGui::SeparatorText("Names");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("First name", first, sizeof(first));
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("Surname", last, sizeof(last));
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("Common name", common, sizeof(common));
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputText("Jersey name", jname, sizeof(jname));
        ImGui::SeparatorText("Club");
        club_picker(app, teamid, club_search, sizeof(club_search));
        club_notes(app, teamid);
        ImGui::SeparatorText("Profile");
        ImGui::SetNextItemWidth(S(110.0f));
        if (ImGui::BeginCombo("Position", position_name(position))) {
            for (int k = 0; k < position_count(); ++k)
                if (ImGui::Selectable(position_name(k), position == k)) position = k;
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Overall", &overall, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Potential", &potential, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Birth year", &birth_year, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Nationality ID", &nationality, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        if (ImGui::BeginCombo("Preferred foot", foot == 2 ? "Left" : "Right")) {
            if (ImGui::Selectable("Right", foot == 1)) foot = 1;
            if (ImGui::Selectable("Left", foot == 2)) foot = 2;
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Height (cm)", &height, 0);
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("Weight (kg)", &weight, 0);
        overall = std::max(1, std::min(overall, 99));
        potential = std::max(1, std::min(potential, 99));
        birth_year = std::max(1950, std::min(birth_year, 2030));
        bool has_name = first[0] || last[0] || common[0];
        bool can = has_name && app.model.team(teamid) != nullptr;
        if (!has_name) ImGui::TextDisabled("Give the player a name.");
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Create player")) {
            GameDate bd{birth_year, 7, 1};
            json set = json::object();
            const Table* pt = app.db.table("players");
            auto put = [&](const char* field, int64_t v) {
                if (pt && pt->field(field)) set[field] = v;   // only fields this game's players table has
            };
            put("overallrating", overall);
            put("potential", potential);
            put("preferredposition1", position);
            put("nationality", nationality);
            put("preferredfoot", foot);
            put("height", height);
            put("weight", weight);
            put("birthdate", gregorian_days_from_date(bd));
            json o = {{"source", {{"playerid", p.playerid}}}, {"teamid", teamid}, {"set", set},
                      {"names", names_json(first, last, common, jname)}};
            app.send({{"op", "run"}, {"module", "create_player"}, {"overrides", o}}, "Create player");
            ImGui::CloseCurrentPopup();
        }
        if (!can) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##pcreate")) ImGui::CloseCurrentPopup();
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
    if (made) {
        ImGui::SameLine();
        ImGui::TextDisabled("(new players are rows Turbo adds to the database: back up your save first)");
    }
    export_dialog(app, p);
    import_dialog(app, p);
    clone_dialog(app, p);
    create_dialog(app, p);
}

}  // namespace turbo
