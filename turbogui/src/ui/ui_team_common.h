// FC 27 LE Turbo GUI - small helpers shared by the two Team pages (ui_team_players.cpp, ui_team_transfers.cpp): the gate every
// Lua-run button has, a button greyed out with its reason, a bordered card, the club chooser header and the one question that is
// asked before an action the Undo button cannot bring back. Header-only (inline); nothing here is registered with the shell.
#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "ui_club_page.h"
#include "ui_club_search.h"
#include "ui_team_pages.h"
#include "ui_theme.h"

namespace turbo::teampage {

using nlohmann::json;

inline std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 50000000 -> "50,000,000"
inline std::string money(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return v < 0 ? "-" + s : s;
}

struct Gate {
    bool ok = true;
    std::string why;  // shown as the hover text of the disabled button
};

// The gate of every Lua-run button of the old panels: the command channel, a loaded career when the tool needs one, the capability of
// this game build, and no other request in flight.
inline Gate lua_gate(App& app, bool needs_cm, const char* need) {
    const BridgeState& st = app.bridge.state();
    if (!app.mailbox) return {false, "Turbo can't reach the game right now."};
    if (needs_cm && !st.in_cm) return {false, "Open your career in the game first."};
    if (need && *need)
        if (const std::string* missing = st.unavailable_reason(need)) return {false, *missing};
    if (app.busy()) return {false, "Wait for the last request to finish."};
    return {};
}

// A button that is greyed out with its reason on hover. True only when it was clicked while enabled.
inline bool gated_button(const char* label, bool primary, const Gate& g, const char* tip) {
    if (!g.ok) ImGui::BeginDisabled();
    const bool hit = primary ? theme::primary_button(label) : ImGui::Button(label);
    if (!g.ok) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const char* t = (!g.ok && !g.why.empty()) ? g.why.c_str() : tip;
        if (t && *t) ImGui::SetTooltip("%s", t);
    }
    return hit && g.ok;
}

// A short grey hint line
inline void hint(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("%s", text);
    ImGui::PopTextWrapPos();
}

// A bordered card with its title; the caller counts it in its page state
inline void card_begin(const char* id, const char* title) {
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::SeparatorText(title);
}
inline void card_end() {
    ImGui::EndChild();
    ImGui::Spacing();
}

inline const char* const kCantUndo = " The Undo button cannot bring this back.";

// ---------------------------------------------------------------- ask first
// One question for every action the Undo button cannot bring back. The page asks; the popup (drawn once at the end of the page, where
// the popup id is the same every frame) sends the request on "yes" and nothing on Cancel.
struct Pending {
    bool want_open = false, live = false;
    std::string title, body, ok;
    json cmd;  // the whole request: {"op":"run","module":...,"overrides":...}
    std::function<bool(App&)> action;  // instead of `cmd`: a page action that sends its own request (true = sent)
    std::string label, status;
};
inline Pending g_pend;
inline std::string g_status;  // the last request a Team card sent, in plain words (the one status line of a page)

inline void ask(const std::string& title, const std::string& body, const std::string& ok, const json& cmd, const std::string& label,
                const std::string& status) {
    g_pend = Pending{};
    g_pend.want_open = g_pend.live = true;
    g_pend.title = title;
    g_pend.body = body;
    g_pend.ok = ok;
    g_pend.cmd = cmd;
    g_pend.label = label;
    g_pend.status = status;
}
inline void ask_do(const std::string& title, const std::string& body, const std::string& ok, std::function<bool(App&)> action,
                   const std::string& status) {
    g_pend = Pending{};
    g_pend.want_open = g_pend.live = true;
    g_pend.title = title;
    g_pend.body = body;
    g_pend.ok = ok;
    g_pend.action = std::move(action);
    g_pend.status = status;
}
inline json run_cmd(const char* module, const json& overrides) { return {{"op", "run"}, {"module", module}, {"overrides", overrides}}; }

inline void confirm_popup(App& app) {
    constexpr const char* kAskId = "###teamask";
    if (g_pend.want_open) {
        ImGui::OpenPopup(kAskId);
        g_pend.want_open = false;
    }
    if (!g_pend.live) return;
    const std::string name = g_pend.title + kAskId;
    if (ImGui::BeginPopupModal(name.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(420.0f));
        ImGui::TextUnformatted(g_pend.body.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (theme::primary_button(g_pend.ok.c_str())) {
            // false: a message says why nothing was sent and the status line stays as it was
            const bool sent = g_pend.action ? g_pend.action(app) : app.send(g_pend.cmd, g_pend.label);
            if (sent) g_status = g_pend.status;
            g_pend.live = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g_pend.live = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else if (!g_pend.want_open) {
        g_pend.live = false;  // closed some other way
    }
}

// ---------------------------------------------------------------- the club
// "Club [chooser] [My club]": the header of both pages. Returns the club (0 = none to show). `id` makes the chooser's id.
inline int64_t club_header(App& app, const char* id) {
    const int64_t team = club_page_team(app);
    ImGui::AlignTextToFramePadding();
    theme::muted_text("Club");
    ImGui::SameLine();
    club_search_box(app, id, app.sel_team, S(260.0f));
    const int64_t mine = app.bridge.state().user_team;
    if (mine > 0 && app.model.team(mine)) {
        ImGui::SameLine();
        const std::string b = std::string("My club###") + id + "mine";
        if (ImGui::Button(b.c_str())) app.sel_team = mine;
    }
    ImGui::Separator();
    return app.model.team(app.sel_team) ? app.sel_team : team;
}

// One line in the log (turbo_gui.log) when what a page shows changes, so a live check can read the count back: "Team page Players: club 14 (Leicester
// City), 26 players". Written once per change, not per frame.
inline void log_page_once(App& app, const char* key, const std::string& text) {
    static std::map<std::string, std::string> last;
    std::string& l = last[key];
    if (l == text) return;
    l = text;
    app.log(text);
}

// The roles the game knows, in its own order (squad_role module: role 1..5)
inline const char* const* role_names() {
    static const char* const r[] = {"Crucial", "Important", "Rotation", "Sporadic", "Prospect"};
    return r;
}

// The players of a club, best first (the order of the squad screens): PlayerRow pointers, rebuilt by the caller every frame it needs them
inline std::vector<const PlayerRow*> squad_of(App& app, int64_t club) {
    std::vector<const PlayerRow*> out;
    for (const LinkRow& l : app.model.links_of_team(club))
        if (const PlayerRow* p = app.model.player(l.playerid)) out.push_back(p);
    std::stable_sort(out.begin(), out.end(), [](const PlayerRow* a, const PlayerRow* b) { return a->overall > b->overall; });
    return out;
}

}  // namespace turbo::teampage
