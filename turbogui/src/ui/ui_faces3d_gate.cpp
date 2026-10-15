// FC 27 LE Turbo GUI - Players > Appearance > "3D hair and beard" (ui_faces3d_gate.h). The draft list lives here until Apply;
// the files and the generator are core/faces3d.h; the hook's state comes from App::faces3d_report (win/lua_loader_win.h).
#include "ui_faces3d_gate.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "core/face_looks.h"
#include "core/hair_catalog.h"
#include "core/lua_loader.h"
#include "imgui.h"

namespace turbo {

namespace fs = std::filesystem;

namespace {

struct GateUi {
    bool loaded = false;
    faces3d::Table draft;    // what the pickers edit
    std::string applied;     // players_json() of what is on disk (players.json), to show "not applied"
    std::string message;     // last Apply / Remove outcome
    bool message_ok = true;
    char hair_search[48] = "", beard_search[48] = "";
};
GateUi g;

fs::path rel(const fs::path& base, const char* r) {
    fs::path p = base;
    std::string s = r;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find('\\', a);
        p /= s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (b == std::string::npos) return p;
        a = b + 1;
    }
}

void load(App& app) {
    g.loaded = true;
    std::string err;
    faces3d::Table t;
    if (!faces3d::load_players(app.turbo_output().u8string(), t, &err)) {
        g.message = err + " (the list starts empty; Apply overwrites the file)";
        g.message_ok = false;
        t.clear();
    }
    g.draft = t;
    g.applied = faces3d::players_json(t);
}

std::string hair_label(int id) {
    const hair::Style* s = hair::find(id);
    return std::to_string(id) + (s ? "  " + hair::describe(*s) : std::string());
}

std::string beard_label(int id) {
    static const char* const kLook[] = {"clean-shaven", "stubble", "moustache / goatee", "short beard", "full beard"};
    for (const auto& l : faces::looks::kFacialHair)
        if (l.id == id && l.look < 5) return std::to_string(id) + "  " + kLook[l.look];
    return std::to_string(id);
}

bool matches(const std::string& label, const char* search) {
    if (!search[0]) return true;
    std::string a = label, b = search;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.find(b) != std::string::npos;
}

// Searchable id picker over a verified-id list; 0 = none
bool id_picker(const char* label, int64_t& value, const std::vector<int>& ids, char* search, size_t search_size,
               std::string (*name)(int)) {
    bool changed = false;
    const std::string preview = value > 0 ? name(static_cast<int>(value)) : std::string("none (keep his own)");
    ImGui::SetNextItemWidth(S(320.0f));
    if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "id or words (short, curly, full beard...)", search, search_size);
        if (ImGui::Selectable("none (keep his own)", value == 0)) {
            value = 0;
            changed = true;
        }
        std::vector<int> shown;
        shown.reserve(ids.size());
        for (int id : ids)
            if (matches(name(id), search)) shown.push_back(id);
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(shown.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const int id = shown[i];
                ImGui::PushID(id);
                if (ImGui::Selectable(name(id).c_str(), value == id)) {
                    value = id;
                    changed = true;
                }
                ImGui::PopID();
            }
        if (shown.empty()) ImGui::TextDisabled("no verified id matches");
        ImGui::EndCombo();
    }
    return changed;
}

std::string status_line(App& app, bool& warn) {
    warn = false;
    if (!app.faces3d_report) return "Hook status: not available here (no game hooks).";
    const lualoader::Faces3dStatus f = app.faces3d_report();
    if (!f.opted_in) return "Hook: off at this game start (faces3d_on.txt was not there when the game started).";
    std::string s = f.installed ? "Hook: installed" : "Hook: opted in, not installed (see Status tab)";
    if (f.killed) s += " | kill switch present", warn = true;
    if (f.legacy_refused) s += " | substitution REFUSED (mods\\legacy player_kit.lua)", warn = true;
    if (f.installed) {
        s += f.captured_md5.empty() ? std::string(" | stock not captured yet") : " | stock " + f.captured_md5.substr(0, 8);
        if (f.substitute) {
            s += std::string(" | override ") +
                 (!f.header_ok ? "missing" : !f.build_key_ok ? "for another build" : (f.stock_matches_header || f.captured_md5.empty()) ? "ok" : "STALE");
            if (f.header_ok && (!f.build_key_ok || (!f.stock_matches_header && !f.captured_md5.empty()))) warn = true;
        }
        s += " | substituted " + std::to_string(f.substituted) + " | fallbacks " + std::to_string(f.fallbacks);
        if (f.fallbacks > 0) warn = true;
        if (!f.last_error.empty()) s += " | last: " + f.last_error;
    }
    return s;
}

int64_t headclass_of(App& app, const Table& t, int64_t pid) {
    const PlayerRow* r = app.model.player(pid);
    if (!r || !t.has("headclasscode")) return -1;
    return app.db.get_int(t, r->rec, "headclasscode", -1);
}

}  // namespace

const faces3d::Entry* faces3d_draft_entry(int64_t playerid) {
    const auto it = g.draft.find(playerid);
    return it == g.draft.end() ? nullptr : &it->second;
}

void faces3d_gate_section(App& app, const Table& t, const PlayerRow& p) {
    if (!g.loaded) load(app);
    ImGui::PushID("faces3d_gate");
    const fs::path out = app.turbo_output();
    std::error_code ec;
    if (fs::exists(rel(app.bridge.root(), lualoader::kLegacyKitRel), ec))
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua exists: Live Editor serves that file and Turbo refuses "
                           "the 3D hair / beard while it is there. Remove it to use this.");

    const int64_t hc = headclass_of(app, t, p.playerid);
    auto it = g.draft.find(p.playerid);
    bool enrolled = it != g.draft.end();
    const bool can_enrol = hc == 0;
    const bool locked = !can_enrol && !enrolled;  // a non-scanned player can be un-enrolled, never enrolled
    if (locked) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Give him a generic 3D hair and/or beard", &enrolled)) {
        if (enrolled) {
            faces3d::Entry e;
            e.name = p.name;
            g.draft[p.playerid] = e;
        } else {
            g.draft.erase(p.playerid);
        }
        it = g.draft.find(p.playerid);
    }
    if (locked) ImGui::EndDisabled();
    if (!can_enrol) {
        ImGui::SameLine();
        if (hc < 0) ImGui::TextDisabled("(head class unknown)");
        else ImGui::TextDisabled("(only scanned heads, head class 0: this one is %lld)", static_cast<long long>(hc));
    }
    if (it != g.draft.end()) {
        faces3d::Entry& e = it->second;
        ImGui::Indent();
        id_picker("Hair##f3h", e.hair, faces3d::ok_hair_ids(), g.hair_search, sizeof(g.hair_search), hair_label);
        ImGui::SameLine();
        if (e.hair <= 0) ImGui::BeginDisabled();
        ImGui::Checkbox("hair colours from the database##f3hc", &e.hair_colour);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Hair, eyebrow and facial-hair colours from his DB colour fields (otherwise the scan's colour mask)");
        if (e.hair <= 0) ImGui::EndDisabled();
        id_picker("Beard##f3b", e.beard, faces3d::ok_beard_ids(), g.beard_search, sizeof(g.beard_search), beard_label);
        ImGui::SameLine();
        if (e.beard <= 0) ImGui::BeginDisabled();
        ImGui::Checkbox("beard colour from the database##f3bc", &e.beard_colour);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The generic beard takes his DB facial-hair colour (off: the scan's colour mask, which can render green)");
        if (e.beard <= 0) ImGui::EndDisabled();
        if (e.hair <= 0 && e.beard <= 0) ImGui::TextDisabled("Choose a hair or a beard (or untick to un-enrol him).");
        ImGui::Unindent();
    }

    // the whole list + Apply / Remove
    const bool pending = faces3d::players_json(g.draft) != g.applied;
    ImGui::Spacing();
    ImGui::Text("Enrolled: %zu player(s)%s", g.draft.size(), pending ? "  (changed, not applied)" : "");
    if (!g.draft.empty() && ImGui::TreeNode("##f3list", "List")) {
        int64_t drop = 0;
        for (const auto& [pid, e] : g.draft) {
            ImGui::PushID(static_cast<int>(pid & 0x7FFFFFFF));
            if (ImGui::SmallButton("x")) drop = pid;
            ImGui::SameLine();
            ImGui::Text("%lld %s: hair %s, beard %s", static_cast<long long>(pid), e.name.c_str(),
                        e.hair > 0 ? std::to_string(e.hair).c_str() : "own", e.beard > 0 ? std::to_string(e.beard).c_str() : "own");
            ImGui::PopID();
        }
        if (drop) g.draft.erase(drop);
        ImGui::TreePop();
    }
    const bool have_cache = fs::exists(rel(out, lualoader::kStockCacheRel), ec);
    if (ImGui::Button("Apply##f3")) {
        std::string build;
        if (app.faces3d_report) build = app.faces3d_report().running_build;
        if (build.empty()) build = app.asset_running_key();
        const faces3d::ApplyResult r =
            faces3d::apply(out.u8string(), g.draft, build, [&](int64_t pid) { return headclass_of(app, t, pid); });
        g.message = r.message;
        g.message_ok = r.ok;
        if (r.ok) g.applied = faces3d::players_json(g.draft);
        app.log("3D hair/beard: " + r.message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove##f3")) {
        std::string err;
        g.message_ok = faces3d::remove(out.u8string(), &err);
        g.message = g.message_ok ? "Removed lua_override\\player_kit.lua and faces3d_on.txt (your list is kept). Restart the game: every head is stock again."
                                 : "Remove failed: " + err;
        app.log("3D hair/beard: " + g.message);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deletes the generated override and faces3d_on.txt; players.json (your list) stays");
    if (!have_cache) {
        ImGui::SameLine();
        if (ImGui::Button("Enable capture##f3")) {
            std::string err;
            g.message_ok = lualoader::set_faces3d_opt_in(out.u8string(), true, &err);
            g.message = g.message_ok ? "faces3d_on.txt written: restart the game once (Turbo saves the game's own player_kit.lua at start), then Apply."
                                     : "Could not write faces3d_on.txt: " + err;
            app.log("3D hair/beard: " + g.message);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turbo needs the game's own player_kit.lua (turbo_output\\faces3d\\stock_player_kit.cache), saved by the hook at a game start");
    }
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "Restart the game to apply: player_kit.lua is read only when the game starts.");
    if (!g.message.empty())
        ImGui::TextColored(g.message_ok ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", g.message.c_str());
    bool warn = false;
    const std::string st = status_line(app, warn);
    if (warn) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", st.c_str());
    else ImGui::TextDisabled("%s", st.c_str());
    if (!have_cache) ImGui::TextDisabled("Stock player_kit.lua not captured yet: Apply needs one game start with 'Enable capture'.");
    ImGui::PopID();
}

}  // namespace turbo
