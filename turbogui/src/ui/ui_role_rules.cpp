// Turbo 2.0: the squad role rule editor. See ui_role_rules.h.
#include "ui_role_rules.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app.h"
#include "imgui.h"

namespace turbo {
namespace rolerules {

using nlohmann::json;
namespace fs = std::filesystem;

static const char* kRoleNames[kRoleCount] = {"Crucial", "Important", "Rotation", "Sporadic", "Prospect"};
static const char* kGroups[4] = {"GK", "DEF", "MID", "ATT"};

const char* role_name(int role) { return (role >= 1 && role <= kRoleCount) ? kRoleNames[role - 1] : "-"; }
const char* pos_group_name(int bit) { return (bit >= 0 && bit < 4) ? kGroups[bit] : "?"; }

std::vector<Rule> default_rules() {
    Rule adult;
    adult.name = "age 19 and older";
    adult.age_min = 19;
    adult.role = 3;
    Rule young;
    young.name = "younger";
    young.role = 5;
    return {adult, young};
}

State::State() : rules(default_rules()) {}

bool add_rule(State& s) {
    if (static_cast<int>(s.rules.size()) >= kMaxRules) return false;
    Rule r;
    r.name = "rule " + std::to_string(s.rules.size() + 1);
    s.rules.push_back(r);
    return true;
}

bool remove_rule(State& s, size_t i) {
    if (i >= s.rules.size() || s.rules.size() <= 1) return false;
    s.rules.erase(s.rules.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
}

bool move_rule(State& s, size_t i, int dir) {
    if (i >= s.rules.size()) return false;
    const long j = static_cast<long>(i) + dir;
    if (j < 0 || j >= static_cast<long>(s.rules.size())) return false;
    std::swap(s.rules[i], s.rules[static_cast<size_t>(j)]);
    return true;
}

void set_pin(State& s, int64_t pid, const std::string& name, int role) {
    role = std::clamp(role, 0, kRoleCount);
    for (Pin& p : s.pins)
        if (p.pid == pid) {
            p.role = role;
            return;
        }
    s.pins.push_back({pid, name, role});
}

bool remove_pin(State& s, int64_t pid) {
    auto it = std::find_if(s.pins.begin(), s.pins.end(), [&](const Pin& p) { return p.pid == pid; });
    if (it == s.pins.end()) return false;
    s.pins.erase(it);
    return true;
}

std::string validate(const State& s) {
    if (s.rules.empty()) return "Give at least one rule.";
    if (static_cast<int>(s.rules.size()) > kMaxRules) return "At most 32 rules.";
    for (size_t i = 0; i < s.rules.size(); ++i) {
        const Rule& r = s.rules[i];
        const std::string n = "Rule " + std::to_string(i + 1) + ": ";
        if (r.role < 1 || r.role > kRoleCount) return n + "pick a squad role.";
        if (r.age_min < 0 || r.age_min > 99 || r.age_max < 0 || r.age_max > 99) return n + "ages go from 1 to 99 (0 = no limit).";
        if (r.age_min > 0 && r.age_max > 0 && r.age_min > r.age_max) return n + "the youngest age is above the oldest.";
        if (r.rank_min < 0 || r.rank_min > 999 || r.rank_max < 0 || r.rank_max > 999) return n + "OVR ranks go from 1 to 999 (0 = no limit).";
        if (r.rank_min > 0 && r.rank_max > 0 && r.rank_min > r.rank_max) return n + "the first OVR rank is below the last.";
    }
    return "";
}

static json rules_json(const State& s) {
    json arr = json::array();
    for (const Rule& r : s.rules) {
        json o = {{"role", r.role}};
        if (!r.name.empty()) o["name"] = r.name;
        if (r.age_min > 0) o["age_min"] = r.age_min;
        if (r.age_max > 0) o["age_max"] = r.age_max;
        if (r.rank_min > 0) o["ovr_rank_min"] = r.rank_min;
        if (r.rank_max > 0) o["ovr_rank_max"] = r.rank_max;
        if (r.pos != 0 && r.pos != 15u) {  // none or all four ticked = every position
            json p = json::array();
            for (int b = 0; b < 4; ++b)
                if (r.pos & (1u << b)) p.push_back(kGroups[b]);
            o["pos"] = p;
        }
        arr.push_back(o);
    }
    return arr;
}

json overrides(const State& s, int64_t teamid, bool preview) {
    json pins = json::object();
    for (const Pin& p : s.pins) pins[std::to_string(p.pid)] = p.role;
    json o = {{"teamid", teamid}, {"actions", json::array({"squad_roles"})}, {"role_rules", rules_json(s)}, {"role_pins", pins}};
    if (preview) o["role_preview"] = true;
    else if (!s.save_rule) o["role_save"] = false;
    return o;
}

json clear_overrides(int64_t teamid) {
    return {{"teamid", teamid}, {"actions", json::array({"squad_roles"})}, {"role_rule_clear", true}};
}

static int int_or(const json& j, const char* k, int dflt) {
    auto it = j.find(k);
    return (it != j.end() && it->is_number()) ? it->get<int>() : dflt;
}

bool parse_preview(const std::string& text, Preview& out, std::string (*name_of)(void*, int64_t), void* ctx) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    Preview p;
    p.team = j.value("team", 0LL);
    p.loaned = int_or(j, "loaned", 0);
    if (j.contains("no_entry") && j["no_entry"].is_array()) p.no_entry = j["no_entry"].size();
    if (j.contains("outside") && j["outside"].is_array()) p.outside = j["outside"].size();
    if (j.contains("counts") && j["counts"].is_object())
        for (auto it = j["counts"].begin(); it != j["counts"].end(); ++it)
            if (it.value().is_number()) p.counts.emplace_back(it.key(), it.value().get<int>());
    std::sort(p.counts.begin(), p.counts.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    if (j.contains("rows") && j["rows"].is_array())
        for (const json& r : j["rows"]) {
            if (!r.is_object()) continue;
            PreviewRow w;
            w.pid = r.value("pid", 0LL);
            w.age = int_or(r, "age", -1);
            w.ovr = int_or(r, "ovr", -1);
            w.rank = int_or(r, "rank", -1);
            w.old_role = int_or(r, "old_role", -1);
            w.new_role = int_or(r, "new_role", -1);
            if (r.contains("group") && r["group"].is_string()) w.group = r["group"].get<std::string>();
            if (r.contains("rule")) {
                if (r["rule"].is_number()) w.rule = "rule " + std::to_string(r["rule"].get<int>());
                else if (r["rule"].is_string()) w.rule = r["rule"].get<std::string>();
            }
            if (r.contains("skip") && r["skip"].is_string()) w.skip = r["skip"].get<std::string>();
            w.name = name_of ? name_of(ctx, w.pid) : ("#" + std::to_string(w.pid));
            p.rows.push_back(std::move(w));
        }
    p.loaded = true;
    out = std::move(p);
    return true;
}

// ---------------------------------------------------------------- files, answers
static fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }

void refresh_files(App& app, bool force) {
    State& s = app.role_rules;
    if (!force && app.now - s.files_checked < 2.0) return;
    s.files_checked = app.now;
    std::error_code ec;
    s.killswitch = fs::exists(out_dir(app) / "role_reapply_off.txt", ec);
    s.rule_saved = fs::exists(out_dir(app) / "role_rule.json", ec);
}

static std::string name_cb(void* ctx, int64_t pid) {
    const std::string n = static_cast<App*>(ctx)->model.player_name(pid);
    return n.empty() ? "#" + std::to_string(pid) : n;
}

void on_result(App& app, const std::string& label, bool ok, const std::string& result) {
    State& s = app.role_rules;
    const std::string text = result.empty() ? std::string(ok ? "done" : "failed") : result;
    s.status = (ok ? "" : "Refused: ") + text;
    if (label.rfind("Squad roles preview", 0) == 0 && ok) {
        std::ifstream f(out_dir(app) / "role_preview.json", std::ios::binary);  // once per answer, not per frame
        std::stringstream ss;
        ss << f.rdbuf();
        if (!parse_preview(ss.str(), s.preview, name_cb, &app)) s.status = "The preview was answered but turbo_output\\role_preview.json could not be read.";
    }
    refresh_files(app, true);
}

// ---------------------------------------------------------------- drawing
static bool role_combo(const char* id, int* role, bool allow_zero) {
    bool changed = false;
    const char* cur = (*role == 0 && allow_zero) ? "Leave alone" : role_name(*role);
    ImGui::SetNextItemWidth(S(120.0f));
    if (ImGui::BeginCombo(id, cur)) {
        if (allow_zero && ImGui::Selectable("Leave alone", *role == 0)) {
            *role = 0;
            changed = true;
        }
        for (int r = 1; r <= kRoleCount; ++r)
            if (ImGui::Selectable(role_name(r), *role == r)) {
                *role = r;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

static void send_overrides(App& app, const json& ov, const char* label, const std::string& wait) {
    if (app.send({{"op", "run"}, {"module", "team_mass"}, {"overrides", ov}}, label)) app.role_rules.status = wait;
}

static const char* kWait = "Requested: Live Editor runs it on the next career-mode event (open a screen or advance a day).";

void draw(App& app, const TeamRow& tr, bool own, bool disabled) {
    State& s = app.role_rules;
    const BridgeState& st = app.bridge.state();
    refresh_files(app, false);
    if (!ImGui::CollapsingHeader("Squad roles")) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Rules that give each player of your club his squad role by age, OVR rank and position");
        return;
    }
    ImGui::PushID("rolerules");
    ImGui::TextWrapped("Each player gets the role of the FIRST rule he matches; a pin beats the rules. Age 0 and OVR rank 0 mean no limit; "
                       "the OVR rank is the player's place in the squad (1 = best). No position ticked means every position. "
                       "Your own club only: the game keeps no roles for other clubs.");
    ImGui::TextWrapped("Apply saves the rule (turbo_output\\role_rule.json) and Turbo applies it again after the game's season reset "
                       "rewrites the roles, until you press Clear saved rule.");
    const bool blocked = disabled || !own || app.busy();
    if (!own) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s is not your club: the editor works but Preview and Apply need your own club.", tr.name.c_str());
    ImGui::BeginDisabled(disabled || !own);

    // ---- rules
    int del = -1, up = -1, down = -1;
    for (size_t i = 0; i < s.rules.size(); ++i) {
        Rule& r = s.rules[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%zu.", i + 1);
        ImGui::SameLine();
        char nm[64];
        std::snprintf(nm, sizeof nm, "%s", r.name.c_str());
        ImGui::SetNextItemWidth(S(150.0f));
        if (ImGui::InputText("##name", nm, sizeof nm)) r.name = nm;
        ImGui::SameLine();
        ImGui::TextUnformatted("gets");
        ImGui::SameLine();
        role_combo("##role", &r.role, false);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("age from##a1", &r.age_min, 1, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("to##a2", &r.age_max, 1, 5);
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("OVR rank from##r1", &r.rank_min, 1, 5);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90.0f));
        ImGui::InputInt("to##r2", &r.rank_max, 1, 5);
        r.age_min = std::clamp(r.age_min, 0, 99);
        r.age_max = std::clamp(r.age_max, 0, 99);
        r.rank_min = std::clamp(r.rank_min, 0, 999);
        r.rank_max = std::clamp(r.rank_max, 0, 999);
        for (int b = 0; b < 4; ++b) {
            ImGui::SameLine();
            bool on = (r.pos >> b) & 1u;
            if (ImGui::Checkbox(kGroups[b], &on)) r.pos = on ? (r.pos | (1u << b)) : (r.pos & ~(1u << b));
        }
        ImGui::SameLine();
        if (ImGui::Button("Up")) up = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::Button("Down")) down = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::Button("Remove")) del = static_cast<int>(i);
        ImGui::PopID();
    }
    if (up >= 0) move_rule(s, static_cast<size_t>(up), -1);
    if (down >= 0) move_rule(s, static_cast<size_t>(down), +1);
    if (del >= 0) remove_rule(s, static_cast<size_t>(del));
    if (ImGui::Button("Add rule")) add_rule(s);
    ImGui::SameLine();
    if (ImGui::Button("Reset to default rules")) {
        s.rules = default_rules();
        s.pins.clear();
    }

    // ---- pins
    ImGui::SeparatorText("Pins (a pin beats the rules)");
    int pin_del = -1;
    for (size_t i = 0; i < s.pins.size(); ++i) {
        Pin& p = s.pins[i];
        ImGui::PushID(static_cast<int>(i) + 1000);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", p.name.empty() ? ("#" + std::to_string(p.pid)).c_str() : p.name.c_str());
        ImGui::SameLine(S(260.0f));
        role_combo("##pinrole", &p.role, true);
        ImGui::SameLine();
        if (ImGui::Button("Remove pin")) pin_del = static_cast<int>(i);
        ImGui::PopID();
    }
    if (pin_del >= 0) s.pins.erase(s.pins.begin() + pin_del);
    {
        const std::vector<LinkRow> links = app.model.links_of_team(tr.teamid);  // the squad (a handful of rows, built once per frame while open)
        std::string cur = s.pick_pid ? app.model.player_name(s.pick_pid) : std::string("Pick a player");
        ImGui::SetNextItemWidth(S(240.0f));
        if (ImGui::BeginCombo("##pinplayer", cur.c_str())) {
            for (const LinkRow& l : links) {
                std::string n = app.model.player_name(l.playerid);
                if (n.empty()) n = "#" + std::to_string(l.playerid);
                if (ImGui::Selectable((n + "##pp" + std::to_string(l.playerid)).c_str(), s.pick_pid == l.playerid)) s.pick_pid = l.playerid;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        role_combo("##pickrole", &s.pick_role, true);
        ImGui::SameLine();
        ImGui::BeginDisabled(s.pick_pid == 0);
        if (ImGui::Button("Add pin")) {
            set_pin(s, s.pick_pid, app.model.player_name(s.pick_pid), s.pick_role);
            s.pick_pid = 0;
        }
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();  // editing ends here; the buttons below carry their own state

    // ---- actions
    ImGui::Separator();
    const std::string bad = validate(s);
    ImGui::BeginDisabled(blocked || !bad.empty());
    if (ImGui::Button("Preview", ImVec2(S(120.0f), 0))) send_overrides(app, overrides(s, tr.teamid, true), "Squad roles preview", kWait);
    ImGui::SameLine();
    if (ImGui::Button("Apply...", ImVec2(S(120.0f), 0))) {
        s.confirm = "apply";
        ImGui::OpenPopup("Apply squad roles?");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(disabled || app.busy() || !st.in_cm || !own);
    if (ImGui::Button("Clear saved rule", ImVec2(S(160.0f), 0))) {
        s.confirm = "clear";
        ImGui::OpenPopup("Apply squad roles?");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Re-apply after season reset", &s.save_rule);
    if (!bad.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", bad.c_str());

    if (ImGui::BeginPopupModal("Apply squad roles?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(440.0f));
        if (s.confirm == "clear")
            ImGui::TextWrapped("Remove the saved squad role rule? The roles stay as they are but Turbo no longer re-applies them after a season reset.");
        else
            ImGui::TextWrapped("Apply %zu rule(s) and %zu pin(s) to the squad of %s? This changes the running career (save first if in doubt)%s.",
                               s.rules.size(), s.pins.size(), tr.name.c_str(),
                               s.save_rule ? "; the rule is saved and re-applied after the game's season reset" : "");
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Run")) {
            if (s.confirm == "clear") send_overrides(app, clear_overrides(tr.teamid), "Squad roles clear", kWait);
            else send_overrides(app, overrides(s, tr.teamid, false), "Squad roles apply", kWait);
            s.confirm.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            s.confirm.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- status lines
    ImGui::TextDisabled("Saved rule: %s. Re-apply after the season reset: %s.", s.rule_saved ? "yes (turbo_output\\role_rule.json)" : "none",
                        s.killswitch ? "OFF (turbo_output\\role_reapply_off.txt exists: delete it to turn it back on)" : "on (create turbo_output\\role_reapply_off.txt to turn it off)");
    if (!s.status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(s.status.c_str());
        ImGui::PopTextWrapPos();
    }

    // ---- preview table
    const Preview& pv = s.preview;
    if (pv.loaded) {
        ImGui::SeparatorText("Preview (nothing was written)");
        std::string sum;
        for (const auto& c : pv.counts) sum += (sum.empty() ? "" : "; ") + c.first + ": " + std::to_string(c.second);
        if (pv.loaned) sum += (sum.empty() ? "" : "; ") + std::string("loaned in: ") + std::to_string(pv.loaned);
        if (pv.no_entry) sum += (sum.empty() ? "" : "; ") + std::string("no role entry: ") + std::to_string(pv.no_entry);
        if (pv.outside) sum += (sum.empty() ? "" : "; ") + std::string("outside the squad: ") + std::to_string(pv.outside);
        ImGui::TextWrapped("%zu players. %s", pv.rows.size(), sum.empty() ? "No skips." : sum.c_str());
        if (ImGui::BeginTable("##rolepreview", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, S(260.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            for (const char* h : {"Player", "Age", "OVR", "Old role", "New role", "Rule", "Skip reason"}) ImGui::TableSetupColumn(h);
            ImGui::TableHeadersRow();
            for (const PreviewRow& r : pv.rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.name.c_str());
                ImGui::TableNextColumn();
                r.age >= 0 ? ImGui::Text("%d", r.age) : ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                r.ovr >= 0 ? ImGui::Text("%d", r.ovr) : ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(role_name(r.old_role));
                ImGui::TableNextColumn();
                if (r.new_role >= 1) ImGui::TextUnformatted(role_name(r.new_role));
                else ImGui::TextDisabled("-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.rule.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.skip.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopID();
}

}  // namespace rolerules
}  // namespace turbo
