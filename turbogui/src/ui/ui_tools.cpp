// FC 27 LE Turbo GUI - Turbo Tools tab (Turbo 0.1 features as buttons) and Status tab.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "app.h"
#include "core/team_preset.h"
#include "core/user_text.h"
#include "file_picker.h"
#include "hotkey_setting.h"
#include "imgui.h"
#include "ui_club_search.h"
#include "ui_edit_unlock.h"
#include "ui_faces.h"
#include "ui_club_tools.h"
#include "ui_images.h"
#include "ui_parity.h"
#include "ui_career_page.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

// need: tool key from Lua's core/caps.lua; the button is disabled (with the reason as tooltip) when this Live Editor
// build cannot run that tool
static bool run_button(App& app, const char* label, const char* module, const json& overrides, bool needs_cm,
                       const char* need = nullptr, bool primary = false) {
    const BridgeState& st = app.bridge.state();
    bool cm = st.in_cm;
    const std::string* missing = st.unavailable_reason(need);
    bool disabled = app.busy() || !app.mailbox || (needs_cm && !cm) || missing;
    if (disabled) ImGui::BeginDisabled();
    const bool clicked = primary ? theme::primary_button(label) : ImGui::Button(label);
    if (disabled) ImGui::EndDisabled();
    if (missing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", missing->c_str());
    else if (disabled && needs_cm && !cm && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Needs a loaded career save");
    if (clicked) app.send({{"op", "run"}, {"module", module}, {"overrides", overrides}}, label);
    return clicked;
}

static json& auto_cfg(App& app, const char* name) {
    json& a = app.gui_settings["auto"][name];
    if (!a.is_object()) a = json::object();
    return a;
}

static void apply_auto(App& app, const char* label) {
    if (app.save_gui_settings()) app.send({{"op", "boot"}}, label);
    else app.notify("cannot write turbo_output\\gui_settings.json", true);
}

// ---------------------------------------------------------------- Career tools pages (GUI revamp 2.13, track T5)
// The old Turbo Tools tab was one long column of headers. Each header is now a section function (the code inside is
// unchanged: the same run_button / auto_cfg calls); the pages below (registered with the shell at the end of this
// file) put them under Career tools. Chrome only: no tool, no Lua command and no setting changed.

// The legacy Tools tab (draw_tools below) draws the pages inside its own collapsing headers: no page frame, and the sections keep the
// open / closed defaults they always had. On a page the section headers start open: the page is the section.
static bool g_unframed = false;
static bool section_header(const char* label, ImGuiTreeNodeFlags flags = 0) {
    if (!g_unframed) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    return ImGui::CollapsingHeader(label, flags);
}

// The two lines every Lua-run tool page starts with. False when the command channel is missing (nothing can be sent).
static bool tools_preamble(App& app) {
    if (!app.mailbox) {
        theme::err_text("These tools cannot reach the game right now.");
        return false;
    }
    const BridgeState& st = app.bridge.state();
    if (!st.in_cm)
        theme::warn_text("Open a career first. These tools only work inside one.");
    else if (!app.lua_alive())
        theme::warn_text("Turbo is waiting for the game. Open any career menu.");
    return true;
}

// ---- Scouting page: "Your squad" (form, morale, role, contracts), scouting / development / youth, Player Career
static void section_squad(App& app) {
    const BridgeState& st = app.bridge.state();
    if (section_header("Your squad", ImGuiTreeNodeFlags_DefaultOpen)) {
        json& fm = auto_cfg(app, "form_morale");
        // Start from the GUI's saved values, else the effective config reported by Lua
        static bool init = false;
        static int form = 100, morale = 100, fitness = 0;
        if (!init && (fm.contains("form") || st.has_settings)) {
            form = fm.contains("form") ? fm.value("form", 100) : st.auto_form;
            morale = fm.contains("morale") ? fm.value("morale", 100) : st.auto_morale;
            fitness = fm.contains("fitness") ? fm.value("fitness", 0) : st.auto_fitness;
            init = true;
        }
        form_label("Form", "0 = leave the form as it is");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::SliderInt("##fmform", &form, 0, 100);
        form_label("Morale", "0 = leave the morale as it is");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::SliderInt("##fmmorale", &morale, 0, 100);
        form_label("Fitness", "0 = leave the fitness as it is; otherwise 5 to 95");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::SliderInt("##fmfitness", &fitness, 0, 95);
        if (fitness > 0 && fitness < 5) fitness = 5;
        run_button(app, "Apply now##fm", "form_morale", {{"form", form}, {"morale", morale}, {"fitness", fitness}}, true,
                   "form_morale");
        ImGui::SameLine();
        bool keep = fm.value("enabled", st.auto_form_enabled);
        if (checkbox_right("Keep every day (auto)##fm", &keep, -1.0f)) {
            fm["enabled"] = keep;
            fm["form"] = form;
            fm["morale"] = morale;
            fm["fitness"] = fitness;
            apply_auto(app, "Auto form/morale");
        }

        ImGui::Separator();
        static int role = 2;  // combo index: 2 = "3 Rotation", Turbo's default squad role
        static bool loaned = true;  // the game keeps a role for loaned-in players too (features/squad_role.lua)
        const char* roles[] = {"1 Crucial", "2 Important", "3 Rotation", "4 Sporadic", "5 Prospect"};
        form_label("Squad role");
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::Combo("##squadrole", &role, [](void* d, int i) { return static_cast<const char**>(d)[i]; }, roles, 5);
        if (role < 0) role = 2;
        ImGui::SameLine();
        checkbox_right("Include loaned-in", &loaned, -1.0f);
        ImGui::SameLine();
        run_button(app, "Set role for whole squad", "squad_role", {{"role", role + 1}, {"include_loaned_in", loaned}}, true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Every player of your club, through the game itself (Turbo.dll); a player without a squad status entry gets one.\n"
                              "The Squad screen shows the new roles when you open it again.");
        // auto.squad_role, rule "role": this role again after each season reset (the game recomputes every role then)
        json& sr = auto_cfg(app, "squad_role");
        const bool on_role = sr.contains("enabled") ? (sr.value("enabled", false) && sr.value("rule", std::string("age")) == "role")
                                                    : (st.auto_squad_role_enabled && st.auto_squad_role_rule == "role");
        bool keep_role = on_role;
        if (checkbox_right("Set it again after each season reset (auto)##srauto", &keep_role)) {
            sr["enabled"] = keep_role;
            sr["rule"] = "role";
            sr["rule_role"] = role + 1;
            sr["events"] = json::array({"SEASON_RESET"});
            apply_auto(app, "Auto squad roles");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("FC 27 recomputes every squad role at the start of a season. With this on, Turbo sets the role chosen above "
                              "again right after it (Players > My squad > Mass actions has the same switch for its by-age rule).");

        ImGui::Separator();
        static int user_years = 4, cpu_years = 5;
        form_label("Contract years", "Years added to the contracts of your squad (1 to 10)");
        ImGui::SetNextItemWidth(S(100.0f));
        ImGui::InputInt("##yearsu", &user_years);
        user_years = std::max(1, std::min(user_years, 10));
        ImGui::SameLine();
        run_button(app, "Extend my squad's contracts", "extend_user_contracts", {{"years", user_years}}, true);
        form_label("Other clubs: years", "Years added to the contracts of every other club's players (1 to 20)");
        ImGui::SetNextItemWidth(S(100.0f));
        ImGui::InputInt("##yearsc", &cpu_years);
        cpu_years = std::max(1, std::min(cpu_years, 20));
        ImGui::SameLine();
        run_button(app, "Extend every other club's contracts", "extend_cpu_contracts", {{"years", cpu_years}}, false);
    }
}

// ---------------------------------------------------------------- scouting, development, youth academy
static void section_scouting(App& app) {
    if (section_header("Scouting, development and youth academy")) {
        // reveal player data (features/reveal.lua -> Turbo.dll -> the game's PlayerDataRevealManager)
        static int reveal_team = 0, reveal_league = 0;
        static bool reveal_confirm = false;
        run_button(app, "Reveal my club##rv", "reveal", {{"scope", {{"user_team", true}}}}, true, "reveal");
        ImGui::SameLine();
        club_search_box(app, "##rvteam", reveal_team, S(180.0f));  // name or ID (ui_club_search.h)
        if (reveal_team > 0) {
            ImGui::SameLine();
            run_button(app, "Reveal club##rv", "reveal", {{"scope", {{"teamid", reveal_team}}}}, true, "reveal");
        }
        form_label("League ID", "Reveal every club of a league: its ID, then tick the box");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##rvleague", &reveal_league, 0);
        ImGui::SameLine();
        checkbox_right("every club of the league##rvc", &reveal_confirm, -1.0f);
        ImGui::SameLine();
        if (reveal_league > 0 && reveal_confirm)
            run_button(app, "Reveal league ID##rv", "reveal", {{"scope", {{"leagueid", reveal_league}}}, {"confirm", true}}, true, "reveal");
        else ImGui::TextDisabled("league ID + tick to reveal a league");
        ImGui::TextDisabled("The game keeps at most 1500 scouted players: Turbo refuses a reveal that would make it forget older reports.");

        ImGui::Separator();
        // development (features/development.lua): squad-wide forced growth
        json& dv = auto_cfg(app, "development");
        static int squad_weekly = 1;
        static bool squad_no_decline = teampreset::growth_default_no_decline(app.gui_settings);  // OFF; a saved value never turns it on
        form_label("Points per week");
        ImGui::SetNextItemWidth(S(160.0f));
        ImGui::SliderInt("##dvweekly", &squad_weekly, 1, 5);
        ImGui::SameLine();
        checkbox_right("No decline##dv", &squad_no_decline, -1.0f);
        bool squad = dv.value("user_team", false);
        if (checkbox_right("Grow my whole squad every week (auto)##dv", &squad)) {
            dv["user_team"] = squad;
            dv["weekly"] = squad_weekly;
            dv["no_decline"] = squad_no_decline;
            const bool any = dv.contains("players") && dv["players"].is_array() && !dv["players"].empty();
            dv["enabled"] = squad || any;
            apply_auto(app, "Weekly growth");
        }
        run_button(app, "Develop my squad to potential now##dv", "development",
                   {{"scope", {{"user_team", true}}}, {"mode", "to_potential"}, {"confirm", true}}, true, "development");
        ImGui::TextDisabled("One player: Players tab > Develop to potential / Growth... (weekly growth per player).");

        ImGui::Separator();
        // youth academy (features/youth.lua)
        static int yid = 0, ypot = 0, ypos = -1, ytier = -1, yvar = -1;
        run_button(app, "List my youth academy##yt", "youth", {{"mode", "list"}}, true);
        form_label("Youth player ID");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##ytid", &yid, 0);
        form_label("Potential", "0 = leave");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##ytpot", &ypot, 0);
        form_label("Position", "The position code 0 to 27; -1 = leave");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##ytpos", &ypos, 0);
        form_label("Tier", "-1 = leave");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##yttier", &ytier, 0);
        form_label("Potential range", "The width of the potential range the scouts report: 0 = exact, -1 = leave");
        ImGui::SetNextItemWidth(S(110.0f));
        ImGui::InputInt("##ytvar", &yvar, 0);
        if (yid > 0)
            run_button(app, "Set youth player##yt", "youth",
                       {{"mode", "set"}, {"playerid", yid}, {"potential", ypot}, {"position", ypos}, {"tier", ytier}, {"variance", yvar}}, true);
    }
}

// ---------------------------------------------------------------- club budget
static void section_budget(App& app) {
    const BridgeState& st = app.bridge.state();
    if (section_header("Your club: transfer budget", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string* no_budget = st.unavailable_reason("transfer_budget");
        if (no_budget)
            theme::warn_text("Not possible with this Live Editor build: %s", no_budget->c_str());
        else if (st.transfer_budget >= 0)
            ImGui::Text("Current budget: %lld", static_cast<long long>(st.transfer_budget));
        else
            ImGui::TextDisabled("Current budget: shown once a career is loaded");
        static long long amount = 50000000;
        form_label("Amount");
        ImGui::SetNextItemWidth(S(200.0f));
        ImGui::InputScalar("##tbamount", ImGuiDataType_S64, &amount);
        amount = std::max(0LL, std::min(amount, 2000000000LL));
        ImGui::SameLine();
        run_button(app, "Set budget", "transfer_budget", {{"mode", "set"}, {"amount", amount}}, true, "transfer_budget", true);
        ImGui::SameLine();
        run_button(app, "Add to budget", "transfer_budget", {{"mode", "add"}, {"amount", amount}}, true, "transfer_budget");
        ImGui::TextDisabled("FC 27 keeps the budget in the career, not in the teams table: set it here (Live Editor's "
                            "SetUserTransferBudget).");
    }
}

// The club customisation hub (draw_club_tools, ui_club_tools.cpp) is drawn by Club & Kits > Stadium & extras (track T3)

// ---------------------------------------------------------------- player career
static void section_player_career(App& app) {
    const BridgeState& st = app.bridge.state();
    if (section_header("Player Career")) {
        run_button(app, "Give my player every PlayStyle", "pap_playstyles", json::object(), true);
        ImGui::SameLine();
        json& ps = auto_cfg(app, "pap_playstyles");
        bool keep = ps.value("enabled", st.auto_playstyles_enabled);
        if (checkbox_right("Re-apply automatically##ps", &keep, -1.0f)) {
            ps["enabled"] = keep;
            apply_auto(app, "Auto PlayStyles");
        }
    }
}

// ---------------------------------------------------------------- transfer bans
static void section_bans(App& app) {
    const BridgeState& st = app.bridge.state();
    if (section_header("Transfer bans")) {
        static int until = 20990101;
        static bool exclude_mine = false;
        form_label("Ban until", "The date the bans end, as YYYYMMDD");
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("##banuntil", &until, 0);
        ImGui::SameLine();
        checkbox_right("Exclude my club", &exclude_mine, -1.0f);
        if (const std::string* r = st.unavailable_reason("transfer_bans"))
            theme::warn_text("Not possible with this Live Editor build: %s", r->c_str());
        run_button(app, "List bans", "transfer_bans", {{"mode", "list"}}, true, "transfer_bans");
        ImGui::SameLine();
        run_button(app, "Ban every team", "transfer_bans", {{"mode", "ban_all_teams"}, {"ban_until", until}, {"exclude_user_team", exclude_mine}}, true,
                   "transfer_bans");
        ImGui::SameLine();
        run_button(app, "Remove all team bans", "transfer_bans", {{"mode", "unban_all_teams"}}, true, "transfer_bans");
    }
}

// ---------------------------------------------------------------- safety (Changes & undo page)
static void section_safety(App& app) {
    const BridgeState& st = app.bridge.state();
    if (section_header("Safety")) {
        json& t = app.gui_settings["turbo"];
        if (!t.is_object()) t = json::object();
        bool dry = t.value("dry_run", st.dry_run);
        if (checkbox_right("Test only (change nothing)###dryrun", &dry)) {
            t["dry_run"] = dry;
            if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
        }
        ImGui::TextDisabled("Editors on the other pages always write immediately, after range checks.");
    }
}

// ---------------------------------------------------------------- the pages
namespace {

using shell::Page;

// One page = one editor panel (the "##editor" child of page::frame) with its sections; no select panel, no footer
// button (the pages' own primary buttons sit in the sections, drawn with the accent colour).
void editor_changes(App& app, void*) {
    form_label_width(S(170.0f));
    changes_undo_summary(app);
    section_safety(app);
    // 2.15: where the kept rows apply (global / this career / not assigned yet), open on this page
    if (!g_unframed) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    kept_edit_scopes_section(app);
    // the list of kept player edits is a section of ui_reapply.cpp: open on this page (the user may still close it)
    if (!g_unframed) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    kept_player_edits_section(app);
    form_label_width(0.0f);
}

void show(App& app, shell::page::DrawFn editor) {
    if (g_unframed) editor(app, nullptr);
    else shell::page::frame(app, nullptr, editor, nullptr, nullptr);
}
// Transfers and Job offer: the panels are the Squad track's (draw_transfers / draw_job_offer, weak references: see ui_career_page.h)
// Until it exports draw_job_offer, a club's job offer is a tab of the club panel under Squad & Players > Squad (the old Teams panel):
// the page says where and opens it. P213 triage (2026-10-17): the installed 2.13.0 build said only "not part of this build" here (and
// on Transfers), which read as broken
void squad_pointer(App& app, const char* where) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(where);
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Open Club > Squad###squadgo")) app.go(Page::TeamPlayers);  // 3.0: the squad cards live on Club > Squad
}
void editor_transfers(App& app, void*) {
    if (draw_transfers) {
        draw_transfers(app);
        return;
    }
    // Integration 2.13.0 (Squad track not merged): the legacy request_tab 5 (Turbo Tools) lands here (shell::area_for), so until
    // draw_transfers exists this page is the whole old Turbo Tools panel, as App::draw_legacy_page would draw it. Transfers
    // themselves stay under Squad & Players > Squad (the old Teams panel).
    draw_tools(app);
}
void editor_job_offer(App& app, void*) {
    if (draw_job_offer) draw_job_offer(app);
    else squad_pointer(app, "Job offers: Players > My squad, pick the club, then its Job offer tab.");
}
void page_transfers(App& app) { show(app, editor_transfers); }
void page_job_offer(App& app) { show(app, editor_job_offer); }
// Changes & undo and Career maintenance need no command channel: they read and write Turbo's own files
void page_changes(App& app) { show(app, editor_changes); }

}  // namespace

// The pages of this track that are drawn from this file, by page id (the shell registry holds the same functions; tests and
// the legacy tab call this directly). False for a page of another file.
bool draw_career_tools_page(App& app, shell::Page p) {
    switch (p) {
        case Page::Transfers: page_transfers(app); return true;
        case Page::JobOffer: page_job_offer(app); return true;
        case Page::ChangesUndo: page_changes(app); return true;
        default: return false;
    }
}

TURBO_REGISTER_PAGE(shell::Page::Transfers, page_transfers);
TURBO_REGISTER_PAGE(shell::Page::JobOffer, page_job_offer);
TURBO_REGISTER_PAGE(shell::Page::ChangesUndo, page_changes);

// Legacy entry (the old Turbo Tools tab, until the shell track removes it from app.cpp): every page of this file one after the other, so nothing is unreachable while the tab bar still exists. No section is drawn twice: the pages
// both in one frame).
void draw_tools(App& app) {
    if (!tools_preamble(app)) return;
    ImGui::BeginChild("##tools");
    form_label_width(S(170.0f));
    g_unframed = true;
    // the old order of the sections (the tests and the habits of the old tab); the pages above hold the same functions
    section_squad(app);
    kept_player_edits_section(app);
    section_scouting(app);
    section_budget(app);
    section_player_career(app);
    section_bans(app);
    section_safety(app);
    draw_game_editors(app);
    g_unframed = false;
    form_label_width(0.0f);
    ImGui::EndChild();
}

// ---------------------------------------------------------------- status
const char* key_name(int vk) {
    static thread_local std::string name;
    name = vk_name(vk);
    return name.c_str();
}

// Game-code hooks (src/win/game_hooks.cpp): build, signature table, every signature's status, hooks, dispatcher.
// 2.16 gui/perf-2: the ~230 lines (141 signatures, every hook, every game call) and the callname counts (sets of tens of
// thousands of ids) were formatted / built in every frame Technical details was open: 5.4 ms per frame in game (W1948 launch
// C). The lines are built at most once a second (with the report itself) and drawn as they are.
namespace {
struct StatusLine {
    int kind = 0;  // 0 plain, 1 warning colour, 2 wrapped
    std::string text;
};
std::vector<StatusLine> g_hook_lines;
double g_hook_lines_at = -1e9;
const void* g_hook_lines_app = nullptr;

std::string fmt_line(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

void build_hook_lines(App& app, std::vector<StatusLine>& out) {
    out.clear();
    auto add = [&out](int kind, std::string s) { out.push_back({kind, std::move(s)}); };
    const HookReport& r = app.hook_report_cached();  // 2.16: at most once a second, built on the host's report thread
    add(0, fmt_line("Game build: %s | signature table: %s | hooks: %s", r.build.empty() ? "unknown" : r.build.c_str(),
                    r.table_source.empty() ? "none" : r.table_source.c_str(), r.enabled ? "allowed" : "off"));
    if (!r.note.empty()) add(2, "  " + r.note);
    int found = 0;
    for (const auto& s : r.signatures)
        if (s.state == SigState::Found) ++found;
    add(0, fmt_line("Signatures: %d of %zu found", found, r.signatures.size()));
    for (const auto& s : r.signatures) {
        if (s.state == SigState::Found)
            add(0, fmt_line("  %s: found at 0x%llX", s.name.c_str(), static_cast<unsigned long long>(s.address)));
        else
            add(1, fmt_line("  %s: %s (%s)", s.name.c_str(), sig_state_name(s.state), s.error.c_str()));
    }
    add(0, fmt_line("Hooks active: %zu", r.hooks.size()));
    for (const auto& h : r.hooks)
        add(0, fmt_line("  %s: %s at 0x%llX | calls %lld | errors %lld%s%s", h.name.c_str(),
                        h.killed ? "KILLED (hook_<name>_off.txt)" : h.active ? "active" : "not active",
                        static_cast<unsigned long long>(h.target), h.calls, h.errors, h.note.empty() ? "" : " | ", h.note.c_str()));
    add(0, fmt_line("Game-thread dispatcher: %s | ticks %lld | Lua pumps %lld | jobs run %lld | failed %lld | dropped %lld | queued %zu",
                    r.dispatcher_hooked ? "prompt (game_tick hook)" : "on career-mode events (Lua pump)", r.dispatcher_ticks,
                    r.dispatcher_pumps, r.dispatcher_ran, r.dispatcher_failed, r.dispatcher_dropped, r.queued));
    add(0, fmt_line("  threads: tick %lu | Lua pump %lu | last drain %lu%s", static_cast<unsigned long>(r.tick_thread_id),
                    static_cast<unsigned long>(r.pump_thread_id), static_cast<unsigned long>(r.game_thread_id),
                    (r.tick_thread_id && r.pump_thread_id && r.tick_thread_id != r.pump_thread_id) ? "  (job-pool threads: both change from frame to frame)" : ""));
    add(2, fmt_line("Prompt Lua commands (synthetic career event): %s | sent %lld | Lua pumped %lld",
                    r.lua_trigger.empty() ? "not available" : r.lua_trigger.c_str(), r.lua_triggers, r.lua_trigger_pumps));
    // Miniface from the 3D model (src/win/player_capture_win.cpp)
    if (app.capture) {
        const capture::Status cs = app.capture->status();
        add(0, fmt_line("3D-model capture: %s | %s | game captures seen %d (descriptor %s) | Turbo renders %d ok, %d failed%s%s",
                        cs.installed ? "installed" : "off", cs.busy ? ("busy: " + cs.busy_label).c_str() : cs.reason.c_str(), cs.seen,
                        cs.learned ? "learned" : "not learned", cs.done, cs.failed, cs.last_format.empty() ? "" : " | last picture ",
                        cs.last_format.c_str()));
        add(0, fmt_line("  heads per request: %d%s | %s", cs.max_batch, cs.max_batch == 1 ? " (turbo_output\\player_capture_batch1.txt)" : "",
                        faces3d_status().c_str()));
    }
    if (!r.calls.empty()) {
        add(0, "Game calls (job offers, live standings, callnames):");
        for (const auto& c : r.calls) add(2, "  " + c);
    }
    // the commentary language the game loaded (core/callnames.h detect_commentary_language) and the callnames per source
    if (app.callnames.refreshed) {
        add(2, "  " + app.callnames.log_line());
        const CallnamePopulation pop = callname_population(app.callnames.bank, app.callnames.masters, app.callnames.index);
        add(2, fmt_line("  callnames %s: %s%s", app.callnames.lang.c_str(), callname_population_line(pop).c_str(),
                        app.callnames.index.built ? "" : " (open Edit a player > Callname for the database counts)"));
    }
    // the spoken-callname set (core/commentary_audio.h SpokenWatch): built where the game binds its commentary bank
    if (app.commentary_audio) {
        if (app.callnames.refreshed && app.callnames.spoken.verified)
            add(2, fmt_line("  spoken callnames (%s): %s", app.callnames.lang.c_str(), app.callnames.spoken.source.c_str()));
        else if (!app.spoken_watch_line().empty())
            add(2, fmt_line("  spoken callnames (%s): %s | probes %lld", app.callnames.lang.empty() ? "no language" : app.callnames.lang.c_str(),
                            app.spoken_watch_line().c_str(), app.spoken_watch.probes()));
    }
}
}  // namespace

void draw_hook_status(App& app) {
    ImGui::SeparatorText("Game hooks");
    if (!app.hook_report) {
        ImGui::TextDisabled("not available in this build");
        g_hook_lines_at = -1e9;
        return;
    }
    if (g_hook_lines_app != &app || app.now < g_hook_lines_at || app.now - g_hook_lines_at >= 1.0) {
        build_hook_lines(app, g_hook_lines);
        g_hook_lines_at = app.now;
        g_hook_lines_app = &app;
    }
    for (const StatusLine& l : g_hook_lines) {
        if (l.kind == 1) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", l.text.c_str());
        else if (l.kind == 2) ImGui::TextWrapped("%s", l.text.c_str());
        else ImGui::TextUnformatted(l.text.c_str());
    }
}

// Game asset catalog (core/asset_catalog.h): one status row (key, date, counts / stale / off), the switches, and the
// buttons to reload it, open its folder and copy the rebuild command
static void asset_catalog_status(App& app) {
    const auto cat = app.asset_catalog();
    const assets::State s = cat->state();
    const ImVec4 good(0.45f, 0.9f, 0.45f, 1.0f), warn(1.0f, 0.7f, 0.3f, 1.0f);
    const std::string line = "Game asset catalog: " + (app.asset_loading() && s == assets::State::NotLoaded ? std::string("loading...") : cat->status_line());
    if (s == assets::State::Current) ImGui::TextColored(good, "%s", line.c_str());
    else if (s == assets::State::Stale || s == assets::State::Error) ImGui::TextColored(warn, "%s", line.c_str());
    else ImGui::TextDisabled("%s", line.c_str());
    if (s == assets::State::Stale)
        ImGui::TextColored(warn, "  Hidden ids cannot be written until the catalog is rebuilt for this game build.");
    const fs::path out = app.turbo_output();
    // 2.16 gui/perf: the three switch files are looked at once a second, not in every frame this row is drawn
    static struct {
        assets::Switches sw;
        bool gate = false;
        double at = -1.0;
        std::string out;
    } seen;
    std::error_code ec;
    if (seen.at < 0.0 || app.now < seen.at || app.now - seen.at >= 1.0 || seen.out != out.string()) {
        seen.sw = assets::read_switches(out);
        seen.gate = fs::exists(assets::lua_gate_path(out), ec);
        seen.at = app.now;
        seen.out = out.string();
    }
    const assets::Switches sw = seen.sw;
    const bool gate = seen.gate;
    ImGui::TextDisabled("  asset_catalog_off.txt: %s | asset_hidden_off.txt: %s | lua_gate.txt: %s", sw.catalog_off ? "present (catalog off)" : "absent",
                        sw.hidden_off ? "present (hidden ids off)" : "absent", gate ? "written" : "none");
    ImGui::BeginDisabled(app.asset_loading());
    if (ImGui::SmallButton("Reload catalog##asset")) app.asset_reload("reload button");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open folder##asset")) {
        fs::create_directories(assets::catalog_dir(out), ec);
        if (!open_in_explorer(assets::catalog_dir(out))) app.notify("Explorer did not start.", true);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy rebuild command##asset")) {
        ImGui::SetClipboardText(assets::rebuild_command().c_str());
        app.notify("Copied: " + assets::rebuild_command() + " (" + assets::rebuild_where() + ")");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n(%s; read-only on the game)", assets::rebuild_command().c_str(), assets::rebuild_where().c_str());
    if (!app.asset_load_note.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Last load: %s", app.asset_load_note.c_str());
    }
}

// Status tab / Settings page, first part: pictures, health, technical details (ui_settings.cpp draws it under the preferences)
void draw_status_health(App& app) {
    const auto& st = app.bridge.state();
    images_status(app);
    // Background loading (ui/preload.h): what is ready, per kind of picture, most wanted first
    for (const std::string& line : app.preload_status()) {
        if (!line.empty() && line[0] == ' ') ImGui::TextDisabled("%s", line.c_str());
        else ImGui::Text("%s", line.c_str());
    }
    // A short health summary first: one line for the database, one for what Turbo needs from the game. The folders, hook
    // addresses, tick counters and service pointers are for bug reports: they sit in the collapsed "Technical details".
    ImGui::SeparatorText("Health");
    if (app.db.ready()) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), "Connected to the game's database (%zu tables)", app.db.table_names().size());
    else ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Not connected to the game's database: %s", app.db_error.empty() ? "enter a career, or wait a moment" : app.db_error.c_str());
    {
        std::string line;
        bool good = true;
        if (app.hook_report) {
            const HookReport& r = app.hook_report_cached();  // 2.16: not every frame (it read ~25 files and the game's locks)
            int found = 0, active = 0, killed = 0;
            for (const auto& s : r.signatures)
                if (s.state == SigState::Found) ++found;
            for (const auto& h : r.hooks) {
                if (h.active) ++active;
                if (h.killed) ++killed;
            }
            const bool dispatcher = r.dispatcher_hooked || r.dispatcher_pumps > 0 || r.dispatcher_ticks > 0;
            line = usertext::health_line(found, static_cast<int>(r.signatures.size()), active, killed, dispatcher, app.lua_alive());
            good = line.rfind("Everything", 0) == 0;
        } else {
            line = app.lua_alive() ? "Live Editor's Lua side is answering; the game hooks are not part of this build"
                                   : "Live Editor's Lua side is not answering yet (it needs a career-mode event)";
            good = app.lua_alive();
        }
        ImGui::TextColored(good ? ImVec4(0.45f, 0.9f, 0.45f, 1.0f) : ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", line.c_str());
        if (!st.unavailable.empty())
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%zu features are not possible with this Live Editor build (listed under Technical details)",
                               st.unavailable.size());
    }
    asset_catalog_status(app);
    if (ImGui::CollapsingHeader("Technical details")) {
    ImGui::SeparatorText("Connection");
    ImGui::Text("Live Editor folder: %s", app.bridge.root().string().c_str());
    ImGui::Text("Bridge files: %s", app.bridge.dir().string().c_str());
    ImGui::Text("Database meta: %s", app.bridge.meta_loaded() ? (std::to_string(app.bridge.meta().tables.size()) + " tables").c_str()
                                                              : (app.bridge.meta_error().empty() ? "not received" : app.bridge.meta_error().c_str()));
    ImGui::Text("Lua state: %s", st.loaded ? "received" : "not received");
    if (st.loaded) {
        ImGui::Text("  LE %s | session %s | update #%lld", st.le_version.c_str(), st.session.c_str(), st.seq);
        ImGui::Text("  DB service %s | career loaded: %s | your club: %s (%lld)", hex_addr(st.db_service).c_str(),
                    st.in_cm ? "yes" : "no", app.model.team_name(st.user_team).c_str(), static_cast<long long>(st.user_team));
        if (st.date.valid()) ImGui::Text("  In-game date: %04d-%02d-%02d", st.date.year, st.date.month, st.date.day);
        if (!st.unavailable.empty()) {
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "  Not possible with this Live Editor build (%zu):",
                               st.unavailable.size());
            for (const auto& u : st.unavailable) ImGui::TextWrapped("    %s: %s", u.first.c_str(), u.second.c_str());
        }
    }
    ImGui::Text("Database: %s", app.db.ready() ? (std::to_string(app.db.table_names().size()) + " tables found").c_str()
                                               : (app.db_error.empty() ? "not connected" : app.db_error.c_str()));
    if (app.model.built()) ImGui::Text("Player names from: %s", app.model.name_source().c_str());
    if (app.mailbox) {
        ImGui::Text("Command channel: %s | Lua %s", hex_addr(app.mailbox->addr()).c_str(),
                    app.lua_alive() ? "answering" : "not seen yet (needs a career-mode event)");
    } else {
        ImGui::Text("Command channel: unavailable");
    }
    draw_hook_status(app);
    // Voice swaps (Players > Callname > All callnames / Voice swaps; core/callname_voice.h)
    if (app.voice_available()) ImGui::Text("%s", app.voice_status_line().c_str());
    else ImGui::TextDisabled("%s", app.voice_status_line().c_str());
    if (!app.voice_error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "  %s", app.voice_error.c_str());
    // Live team names (Teams > Name; core/teamname_override.h)
    if (app.team_names_live()) ImGui::Text("%s", app.team_names_status_line().c_str());
    else ImGui::TextDisabled("%s", app.team_names_status_line().c_str());
    if (!app.team_names_error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "  %s", app.team_names_error.c_str());
    }  // Technical details

}

// The preferences: show/hide key, UI size, row height, background loading (Status tab > Settings; Settings page)
// 2.22 G-resize: Settings > Look and feel. UI size and Row height; the tooltip says how every other size is changed.
void draw_look_and_feel(App& app) {
    form_label("UI size", "The size of the whole Turbo window, on top of the automatic size for the window height");
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::SliderFloat("##uisize", &app.ui_scale_user, 0.6f, 2.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit() && !app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset size")) {
        app.ui_scale_user = 1.0f;
        if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("now %.2fx (window height %.0f px)", static_cast<double>(app.ui_scale_applied),
                        static_cast<double>(ImGui::GetIO().DisplaySize.y));
    // Row height (ui/gui_prefs.h): the height of every table and list row, live; the style follows on the next tick. (The old "List spacing" box.)
    form_label("Row height", "How tall the rows of tables and lists are (px at UI size 1). Drag column edges to resize columns; drag the line between panels to resize panels.");
    ImGui::SetNextItemWidth(S(140.0f));
    float rh = app.prefs.row_height;
    if (ImGui::SliderFloat("##rowheight", &rh, kRowHeightMin, kRowHeightMax, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) app.prefs.row_height = clamp_row_height(rh);
    if (ImGui::IsItemDeactivatedAfterEdit() && !app.save_gui_settings()) app.notify("cannot save the row height", true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Compact to roomy. Drag column edges to resize columns; drag the line between panels to resize panels.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset rows###rowheightreset")) {
        app.prefs.row_height = kRowHeightNormal;
        if (!app.save_gui_settings()) app.notify("cannot save the row height", true);
    }
    ImGui::TextDisabled("Drag column edges to resize columns; drag the line between panels to resize panels.");
}

void draw_preferences(App& app, bool with_look) {
    hotkey_setting(app);  // hotkey_setting.h: pick the key from the list or by pressing it
    if (with_look) draw_look_and_feel(app);
    // Background loading of pictures (ui/preload.h, gui.preload_images)
    if (ImGui::Checkbox("Preload pictures###preload", &app.prefs.preload_images)) {
        if (!app.save_gui_settings()) app.notify("cannot write gui_settings.json", true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: the galleries open with their pictures already loaded (the user's club first, then the listed players,\n"
                          "the real-face chooser, tattoos, galleries, crests, managers). Off: pictures load when a screen shows them.");

}

// The log lines (a bordered child filling the rest of the window)
void draw_status_log(App& app) {
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // 2.16 gui/perf: only the lines in view are laid out (ImGuiListClipper; at most 300 lines are kept)
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(app.log_lines.size()));
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(app.log_lines[static_cast<size_t>(i)].c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// The whole old Status tab, in its old order
void draw_status(App& app) {
    draw_status_health(app);
    ImGui::SeparatorText("Settings");
    draw_preferences(app);
    ImGui::SeparatorText("Log");
    draw_status_log(app);
}

}  // namespace turbo
