// FC 27 LE Turbo GUI - the Career pages of the GUI revamp (see ui_career_pages.h): Transfers & money, Manager jobs, My squad and
// Managers & staff. Plain cards (one question per card, one main button, the rest under More), names instead of codes, an honest
// "Not tested yet" tag where the write is built but not yet seen in game (docs/fc26-parity.md is the proof table), and a question
// before every action the Undo button cannot bring back.
//
// Behaviour is the old panels' behaviour, not a copy of their layout: every button here sends the same Lua module with the same
// override keys as the old Tools / Squad / Staff panels (ui_tools.cpp section_*, ui_teams.cpp job_offer_tab, mass_actions_tab,
// manager_rules_section, manager_market_section), behind the same gates (command channel, loaded career, capability, busy), and
// reads and writes the same App fields and gui_settings keys, so an old and a new page always agree. Pieces that were already
// public (the club box, the moves table, scouts, the role rules, the manager appearance and miniface editors, the field grid) are
// called as they are.
#include "ui_career_pages.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "ui_bodytypes.h"
#include "ui_club_search.h"
#include "ui_faces.h"
#include "ui_images.h"
#include "ui_parity.h"
#include "ui_role_rules.h"
#include "ui_theme.h"

namespace turbo {

using nlohmann::json;

namespace {

CareerPageState g_state;
std::string g_status;  // the last request a Career card sent, in plain words (the one status line of a page)

// ---------------------------------------------------------------- small helpers
std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 50000000 -> "50,000,000"
std::string money(long long v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return v < 0 ? "-" + s : s;
}

struct Gate {
    bool ok = true;
    std::string why;  // shown as the hover text of the disabled button
};

// The gate every Lua-run button of the old Tools panel had (ui_tools.cpp run_button): the command channel, a loaded career when the
// tool needs one, the capability of this game build, and no other request in flight.
Gate lua_gate(App& app, bool needs_cm, const char* need) {
    const BridgeState& st = app.bridge.state();
    if (!app.mailbox) return {false, "Turbo can't reach the game right now."};
    if (needs_cm && !st.in_cm) return {false, "Open your career in the game first."};
    if (const std::string* missing = st.unavailable_reason(need)) return {false, *missing};
    if (app.busy()) return {false, "Wait for the last request to finish."};
    return {};
}

// A button that is greyed out with its reason on hover. Returns true only when it was clicked while enabled.
bool gated_button(const char* label, bool primary, const Gate& g, const char* tip) {
    if (!g.ok) ImGui::BeginDisabled();
    const bool hit = primary ? theme::primary_button(label) : ImGui::Button(label);
    if (!g.ok) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const char* t = (!g.ok && !g.why.empty()) ? g.why.c_str() : tip;
        if (t && *t) ImGui::SetTooltip("%s", t);
    }
    return hit && g.ok;
}

// A short grey hint line of a card (100 characters at most)
void hint(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("%s", text);
    ImGui::PopTextWrapPos();
}

// A bordered card with its title and, where the write is built but not yet seen in game, the small tag
bool card_begin(const char* id, const char* title, const char* tag = nullptr) {
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::SeparatorText(title);
    ++g_state.cards;
    if (tag) {
        ++g_state.tags;
        theme::warn_text("%s", tag);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Built, but nobody has seen it work in a real game yet.");
    }
    return true;
}
void card_end() {
    ImGui::EndChild();
    ImGui::Spacing();
}
constexpr const char* kNotTested = "Not tested yet";

// ---------------------------------------------------------------- ask first
// One question for every action the Undo button cannot bring back. The page asks, the popup (drawn once at the end of the page,
// where the popup id is the same every frame) sends the Lua module on "yes".
struct Pending {
    bool want_open = false, live = false;
    std::string title, body, ok;
    std::string module, label, status;
    json overrides;
    std::string* status_to = nullptr;  // an App status line set to `status` once the request is sent
};
Pending g_pend;

void ask(const std::string& title, const std::string& body, const std::string& ok, const char* module, json overrides,
         const std::string& label, const std::string& status, std::string* status_to = nullptr) {
    g_pend = Pending{};
    g_pend.want_open = g_pend.live = true;
    g_pend.title = title;
    g_pend.body = body;
    g_pend.ok = ok;
    g_pend.module = module;
    g_pend.overrides = std::move(overrides);
    g_pend.label = label;
    g_pend.status = status;
    g_pend.status_to = status_to;
}

constexpr const char* kAskId = "###careerask";  // the one literal of this id (a ### id may appear once per file)

void confirm_popup(App& app) {
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
            // false: a message says why nothing was sent, and the status line stays as it was
            if (app.send({{"op", "run"}, {"module", g_pend.module}, {"overrides", g_pend.overrides}}, g_pend.label)) {
                g_status = g_pend.status;
                if (g_pend.status_to) *g_pend.status_to = g_pend.status;
            }
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

const char* const kCantUndo = " The Undo button cannot bring this back.";

// A Turbo setting that switches a daily job on or off (gui_settings "auto"): the same keys the old panels use
json& auto_cfg(App& app, const char* name) {
    json& a = app.gui_settings["auto"][name];
    if (!a.is_object()) a = json::object();
    return a;
}
void apply_auto(App& app, const char* label) {
    if (app.save_gui_settings()) app.send({{"op", "boot"}}, label);
    else app.notify("Turbo couldn't save its settings.", true);
}

// The page frame: scrolling area, the label column of the form rows, the question line, the status line, the popup
struct Page {
    App& app;
    Page(App& a, const char* id, const char* question) : app(a) {
        ImGui::BeginChild(id, ImVec2(0, 0));
        form_label_width(S(150.0f));
        hint(question);
        ImGui::Spacing();
    }
    ~Page() {
        if (!g_status.empty()) theme::muted_text("%s", g_status.c_str());
        confirm_popup(app);
        form_label_width(0.0f);
        ImGui::EndChild();
    }
};

// The club the user manages (0 = none known)
int64_t my_club(App& app) {
    const int64_t t = app.bridge.state().user_team;
    return t > 0 && app.model.team(t) ? t : 0;
}

bool waiting_for_career(App& app) {
    g_state = CareerPageState{};
    if (!app.connected()) {
        g_state.waiting = true;
        not_connected_hint();
        return true;
    }
    g_state.club = my_club(app);
    return false;
}

// ---------------------------------------------------------------- Transfers & money
void card_budget(App& app) {
    const BridgeState& st = app.bridge.state();
    card_begin("##career_budget", "Transfer budget");
    if (const std::string* no = st.unavailable_reason("transfer_budget")) {
        theme::muted_text("This game version can't change the budget.");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", no->c_str());
    } else if (st.transfer_budget >= 0) {
        ImGui::Text("Your budget now: %s", money(st.transfer_budget).c_str());
    } else {
        theme::muted_text("Your budget shows once your career is open.");
    }
    static long long amount = 50000000;
    form_label("Amount", "The money to set, or to add to the budget.");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputScalar("##careeramount", ImGuiDataType_S64, &amount);
    amount = std::max(0LL, std::min(amount, 2000000000LL));
    const Gate g = lua_gate(app, true, "transfer_budget");
    const std::string was = st.transfer_budget >= 0 ? " Your budget is " + money(st.transfer_budget) + " now; set it back to that to undo." : "";
    if (gated_button("Set budget", true, g, "Replaces your transfer budget with the amount.")) {
        ask("Set the budget?", "Your transfer budget becomes " + money(amount) + "." + was, "Set budget", "transfer_budget",
            {{"mode", "set"}, {"amount", amount}}, "Set budget", "Budget set to " + money(amount) + ". The game shows it when you open a screen.");
    }
    ImGui::SameLine();
    if (gated_button("Add to budget", false, g, "Adds the amount on top of your transfer budget.")) {
        ask("Add to the budget?", money(amount) + " is added to your transfer budget." + was, "Add to budget", "transfer_budget",
            {{"mode", "add"}, {"amount", amount}}, "Add to budget", money(amount) + " added to the budget. The game shows it when you open a screen.");
    }
    card_end();
}

void card_other_clubs_contracts(App& app) {
    card_begin("##career_contracts_all", "Contracts of other clubs", kNotTested);
    static int years = 5;
    form_label("Years to add", "Added to every player at every other club (1 to 20).");
    ImGui::SetNextItemWidth(S(100.0f));
    ImGui::InputInt("##careeryearsall", &years);
    years = std::max(1, std::min(years, 20));
    const Gate g = lua_gate(app, false, nullptr);
    if (gated_button("Extend their contracts", true, g, "Every player of every other club gets the extra years.")) {
        ask("Extend all other clubs' contracts?",
            "Every player at every other club gets " + std::to_string(years) + " more years on his contract." + kCantUndo,
            "Extend contracts", "extend_cpu_contracts", {{"years", years}}, "Extend other clubs' contracts",
            "Asked to add " + std::to_string(years) + " years to the other clubs' contracts.");
    }
    card_end();
}

void card_moves(App& app, int64_t club) {
    card_begin("##career_moves", "Moves of my club");
    if (club <= 0) {
        theme::muted_text("Waiting for your career.");
    } else {
        ImGui::BeginChild("##career_moves_list", ImVec2(0, S(230.0f)), ImGuiChildFlags_None);
        team_transfers_tab(app, club);  // ui_parity.h: this season's moves of the club (read only)
        ImGui::EndChild();
    }
    card_end();
}

void card_bans(App& app) {
    const BridgeState& st = app.bridge.state();
    if (const std::string* no = st.unavailable_reason("transfer_bans")) {
        // The game has no ban list (docs/fc26-parity.md row 16): one honest line instead of dead buttons
        theme::muted_text("Transfer bans: not possible in this game version.");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", no->c_str());
        return;
    }
    card_begin("##career_bans", "Transfer bans", kNotTested);
    static int year = 2099, month = 1, day = 1;
    static bool keep_mine_free = true;
    form_label("Banned until", "The day the bans end.");
    ImGui::SetNextItemWidth(S(80.0f));
    ImGui::InputInt("##careerbanyear", &year, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(50.0f));
    ImGui::InputInt("##careerbanmonth", &month, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(50.0f));
    ImGui::InputInt("##careerbanday", &day, 0);
    year = std::max(2000, std::min(year, 2999));
    month = std::max(1, std::min(month, 12));
    day = std::max(1, std::min(day, 31));
    ImGui::Checkbox("My club can still buy##careerbankeep", &keep_mine_free);
    const Gate g = lua_gate(app, true, "transfer_bans");
    const int until = year * 10000 + month * 100 + day;
    if (gated_button("Ban every club", true, g, "No club can sign players until that day.")) {
        ask("Ban every club from transfers?",
            "No club can sign players until " + std::to_string(day) + "/" + std::to_string(month) + "/" + std::to_string(year) +
                (keep_mine_free ? ", except yours." : ", yours included.") + " Lift all bans takes it back.",
            "Ban every club", "transfer_bans", {{"mode", "ban_all_teams"}, {"ban_until", until}, {"exclude_user_team", keep_mine_free}},
            "Ban every club", "Asked the game to ban every club from transfers.");
    }
    ImGui::SameLine();
    if (gated_button("Lift all bans", false, g, "Takes the transfer ban off every club.")) {
        ask("Lift all bans?", "Every club can sign players again.", "Lift all bans", "transfer_bans", {{"mode", "unban_all_teams"}},
            "Lift all bans", "Asked the game to lift the bans.");
    }
    card_end();
}

// ---------------------------------------------------------------- Manager jobs
// A manager list with a search box: the manager picked is App::sel_manager (the same one the Managers & staff page uses)
void manager_picker(App& app, const char* id, char* search, size_t search_size, float height) {
    const auto& mgrs = app.model.managers();
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint((std::string("##") + id + "q").c_str(), "manager or club name", search, search_size);
    const std::string q = lower_copy(search);
    ImGui::BeginChild((std::string("##") + id + "list").c_str(), ImVec2(0, S(height)), ImGuiChildFlags_Borders);
    int shown = 0;
    for (size_t i = 0; i < mgrs.size() && shown < 60; ++i) {
        const ManagerRow& m = mgrs[i];
        const std::string team = m.teamid > 0 ? app.model.team_name(m.teamid) : std::string("free agent");
        if (!q.empty() && lower_copy(m.name).find(q) == std::string::npos && lower_copy(team).find(q) == std::string::npos) continue;
        ++shown;
        const std::string row = m.name + "  (" + team + ")##mgr" + std::to_string(i);
        if (ImGui::Selectable(row.c_str(), app.sel_manager == static_cast<int>(i))) app.sel_manager = static_cast<int>(i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Manager ID %lld", static_cast<long long>(m.managerid));
    }
    if (shown == 0) theme::muted_text("No manager matches.");
    else if (shown >= 60) theme::muted_text("Type a name to narrow the list.");
    ImGui::EndChild();
}

void card_job_offer(App& app) {
    const BridgeState& st = app.bridge.state();
    card_begin("##career_joboffer", "Job offer");
    hint("Ask a club for its manager job. You accept or decline in the game's Job Offers screen.");
    form_label("Club", "Clubs in a league can offer a job. Your own club can't.");
    club_search_box(app, "##careerjobclub", app.job_offer_team, S(280.0f), "club name");
    g_state.manager_club = app.job_offer_team;
    const TeamRow* pt = app.model.team(app.job_offer_team);
    const bool own = pt && pt->teamid == st.user_team;
    const bool can_hire = pt && pt->league >= 0 && !app.model.is_national_team(pt->teamid);
    Gate g = lua_gate(app, true, "job_offer");
    if (g.ok && !pt) g = {false, "Pick a club first."};
    else if (g.ok && own) g = {false, "That is your own club."};
    else if (g.ok && !can_hire) g = {false, "Only clubs in a league can offer a manager job, not national teams."};
    const std::string club = pt ? pt->name : std::string();
    if (gated_button("Ask for the job", true, g, "Asks the game to make this club offer you its job.")) {
        ask("Ask " + club + " for its job?",
            club + " answers at once. Accept or decline its offer in the game's Job Offers screen. This changes the running career: save first if in doubt." +
                kCantUndo,
            "Ask for the job", "job_offer", {{"enabled", true}, {"teamid", app.job_offer_team}, {"confirm", true}}, "Job offer from " + club,
            "Asked. Open a game screen or advance a day to see the club's answer.", &app.job_offer_status);
    }
    if (!app.job_offer_status.empty()) hint(app.job_offer_status.c_str());
    card_end();
}

void card_sacking(App& app) {
    const BridgeState& st = app.bridge.state();
    card_begin("##career_sacking", "Stop the club sacking me");
    if (st.job_security_score >= 0) {
        ImGui::Text("Job security: %d of 100, %s", st.job_security_score, st.job_security_level.empty() ? "not read yet" : st.job_security_level.c_str());
        if (!st.job_security_locked.empty()) {
            ImGui::SameLine();
            theme::ok_text("(kept %s by Turbo)", st.job_security_locked.c_str());
        }
    } else {
        theme::muted_text("Job security shows once your Manager Career is open.");
    }
    if (st.sacked) theme::err_text("The game has already marked you as sacked.");
    else if (st.sack_pending) theme::warn_text("A sacking is waiting for the next day. Switch this on to cancel it.");
    const Gate g = lua_gate(app, true, "manager_rules");
    static bool keep_on = true;
    auto send_rules = [&](json ov, const std::string& what) {
        ov["enabled"] = true;
        ov["confirm"] = true;
        if (app.send({{"op", "run"}, {"module", "manager_rules"}, {"overrides", ov}}, "Manager rules: " + what))
            app.manager_rules_status = g_status = "Asked: " + what + ". The game answers on its next step.";
    };
    if (gated_button("Never sack me", true, g, "The club can't sack you while this is on. Turn it off any time.")) {
        send_rules({{"unsackable", true}, {"keep", keep_on}}, "unsackable on");
    }
    ImGui::SameLine();
    if (gated_button("Allow sacking again", false, g, "Puts the game's own rule back.")) {
        send_rules({{"unsackable", false}, {"keep", false}}, "unsackable off");
    }
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::TreeNode("More##careersackmore")) {
        ImGui::Checkbox("Keep it on every time I play##careerkeepon", &keep_on);
        static int level = 0;  // 0 Safe .. 3 Very insecure, 4 the game's own
        static const char* kLevels[] = {"Safe", "Okay", "Insecure", "Very insecure", "The game's own"};
        static const char* kKeys[] = {"safe", "okay", "insecure", "very insecure", "game"};
        form_label("Job security", "Safe and Very insecure stay until you take a new job. The others are a nudge.");
        ImGui::SetNextItemWidth(S(180.0f));
        ImGui::Combo("##careerlevel", &level, kLevels, 5);
        ImGui::SameLine();
        if (gated_button("Set it", false, g, "Changes how secure the board says your job is.")) {
            const std::string what = std::string("job security ") + kKeys[level];
            ask("Change your job security?",
                level == 4 ? "The game goes back to working out your job security itself."
                           : std::string("The board treats your job as ") + lower_copy(kLevels[level]) + ". Choose \"The game's own\" to take it back.",
                "Change it", "manager_rules", {{"job_security", kKeys[level]}, {"enabled", true}, {"confirm", true}}, "Manager rules: " + what,
                "Asked: " + what + ". The game answers on its next step.", &app.manager_rules_status);
        }
        ImGui::TreePop();
    }
    if (!app.manager_rules_status.empty()) hint(app.manager_rules_status.c_str());
    card_end();
}

void card_move_manager(App& app) {
    const BridgeState& st = app.bridge.state();
    card_begin("##career_movemanager", "Move a manager");
    hint("The club's own manager takes the old job. Your club is never touched.");
    static char search[64] = "";
    manager_picker(app, "move", search, sizeof(search), 120.0f);
    const auto& mgrs = app.model.managers();
    const ManagerRow* sel = (app.sel_manager >= 0 && app.sel_manager < static_cast<int>(mgrs.size())) ? &mgrs[static_cast<size_t>(app.sel_manager)] : nullptr;
    form_label("To club", "A club in a league. Pick the manager first.");
    club_search_box(app, "##careermoveclub", app.manager_move_team, S(280.0f), "club name");
    g_state.move_club = app.manager_move_team;
    const TeamRow* dest = app.model.team(app.manager_move_team);
    const bool own_club = sel && sel->teamid > 0 && sel->teamid == st.user_team;
    Gate base = lua_gate(app, true, nullptr);
    if (base.ok && !sel) base = {false, "Pick a manager first."};
    else if (base.ok && own_club) base = {false, "That is your own manager: your job changes through job offers."};
    Gate gmove = base, gfree = base;
    if (gmove.ok && !dest) gmove = {false, "Pick a club first."};
    else if (gmove.ok && (dest->league < 0 || app.model.is_national_team(dest->teamid))) gmove = {false, "Only clubs in a league can take a manager."};
    else if (gmove.ok && (dest->teamid == st.user_team || dest->teamid == sel->teamid)) gmove = {false, "Pick a different club."};
    if (gfree.ok && sel->teamid <= 0) gfree = {false, "He is a free agent already."};
    const std::string who = sel ? sel->name : std::string();
    const std::string from = sel && sel->teamid > 0 ? app.model.team_name(sel->teamid) : std::string("the free-agent list");
    if (gated_button("Move manager", true, gmove, "Gives him that club; that club's manager takes his old job.")) {
        ask("Move " + who + "?",
            who + " goes to " + dest->name + ". Its manager takes " + from + ". Saved with your career. To undo, move him back." + kCantUndo,
            "Move manager", "manager_move", {{"enabled", true}, {"managerid", sel->managerid}, {"teamid", app.manager_move_team}, {"confirm", true}},
            "Manager move: " + who, "Asked: move " + who + " to " + dest->name + ".", &app.manager_move_status);
    }
    ImGui::SameLine();
    if (gated_button("Make available", false, gfree, "He leaves his club and becomes a free agent; a free agent takes his job.")) {
        ask("Make " + who + " a free agent?",
            "A free-agent manager takes " + from + ". Saved with your career." + kCantUndo, "Make available", "manager_move",
            {{"enabled", true}, {"managerid", sel->managerid}, {"teamid", 0}, {"confirm", true}}, "Manager move: release " + who,
            "Asked: make " + who + " a free agent.", &app.manager_move_status);
    }
    if (!app.manager_move_status.empty()) hint(app.manager_move_status.c_str());
    card_end();
}

// ---------------------------------------------------------------- My squad
void card_form_fitness(App& app) {
    const BridgeState& st = app.bridge.state();
    card_begin("##career_form", "Form and fitness", kNotTested);
    json& fm = auto_cfg(app, "form_morale");
    // start from the saved values, else the ones the game side reports (as the old panel did)
    static bool init = false;
    static int form = 100, morale = 100, fitness = 0;
    if (!init && (fm.contains("form") || st.has_settings)) {
        form = fm.contains("form") ? fm.value("form", 100) : st.auto_form;
        morale = fm.contains("morale") ? fm.value("morale", 100) : st.auto_morale;
        fitness = fm.contains("fitness") ? fm.value("fitness", 0) : st.auto_fitness;
        init = true;
    }
    form_label("Form", "0 leaves the form as it is.");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::SliderInt("##careerform", &form, 0, 100);
    form_label("Morale", "0 leaves the morale as it is.");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::SliderInt("##careermorale", &morale, 0, 100);
    form_label("Fitness", "0 leaves the fitness as it is; otherwise 5 to 95.");
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::SliderInt("##careerfitness", &fitness, 0, 95);
    if (fitness > 0 && fitness < 5) fitness = 5;
    const Gate g = lua_gate(app, true, "form_morale");
    if (gated_button("Apply to my squad", true, g, "Sets these values for every player of your club.")) {
        ask("Set form and fitness?",
            "Every player of your club gets form " + std::to_string(form) + ", morale " + std::to_string(morale) + ", fitness " +
                std::to_string(fitness) + " (0 leaves a value alone)." + kCantUndo,
            "Apply", "form_morale", {{"form", form}, {"morale", morale}, {"fitness", fitness}}, "Apply to my squad", "Asked for the new form and fitness.");
    }
    bool keep = fm.value("enabled", st.auto_form_enabled);
    if (ImGui::Checkbox("Keep this every day##careerformkeep", &keep)) {
        fm["enabled"] = keep;
        fm["form"] = form;
        fm["morale"] = morale;
        fm["fitness"] = fitness;
        apply_auto(app, "Daily form and morale");
    }
    card_end();
}

void card_squad_roles(App& app, const TeamRow* team) {
    const BridgeState& st = app.bridge.state();
    card_begin("##career_roles", "Squad roles");
    static int role = 2;  // Rotation, Turbo's default role
    static bool loaned = true;
    static const char* kRoles[] = {"Crucial", "Important", "Rotation", "Sporadic", "Prospect"};
    form_label("Role for everyone", "The game keeps a role for loaned-in players too.");
    ImGui::SetNextItemWidth(S(180.0f));
    ImGui::Combo("##careerrole", &role, kRoles, 5);
    if (role < 0) role = 2;
    ImGui::Checkbox("Include loaned-in players##careerloaned", &loaned);
    const Gate g = lua_gate(app, true, nullptr);
    if (gated_button("Set for my squad", true, g, "Gives every player of your club this role through the game itself.")) {
        ask("Set everyone's role?",
            std::string("Every player of your club gets the role \"") + kRoles[role] + "\". Open the Squad screen again to see it." + kCantUndo,
            "Set roles", "squad_role", {{"role", role + 1}, {"include_loaned_in", loaned}}, "Set role for whole squad",
            std::string("Asked: role ") + kRoles[role] + " for the whole squad.");
    }
    json& sr = auto_cfg(app, "squad_role");
    const bool on_role = sr.contains("enabled") ? (sr.value("enabled", false) && sr.value("rule", std::string("age")) == "role")
                                                : (st.auto_squad_role_enabled && st.auto_squad_role_rule == "role");
    bool keep_role = on_role;
    if (ImGui::Checkbox("Repeat after each season reset##careerrolekeep", &keep_role)) {
        sr["enabled"] = keep_role;
        sr["rule"] = "role";
        sr["rule_role"] = role + 1;
        sr["events"] = json::array({"SEASON_RESET"});
        apply_auto(app, "Squad roles after the season reset");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game works out every role again at a new season. This puts yours back.");
    if (team) {
        ImGui::SetNextItemOpen(false, ImGuiCond_Once);
        if (ImGui::TreeNode("Roles by age and position##careerrolerules")) {
            const bool disabled = app.busy() || !app.mailbox || !st.in_cm || app.model.links_of_team(team->teamid).empty();
            rolerules::draw(app, *team, true, disabled);  // ui_role_rules.h: the rule editor of the old Mass actions tab
            ImGui::TreePop();
        }
    }
    card_end();
}

void card_my_contracts(App& app) {
    card_begin("##career_contracts", "Contracts", kNotTested);
    static int years = 4;
    form_label("Years to add", "Added to the contract of every player of your club (1 to 10).");
    ImGui::SetNextItemWidth(S(100.0f));
    ImGui::InputInt("##careeryears", &years);
    years = std::max(1, std::min(years, 10));
    const Gate g = lua_gate(app, true, nullptr);
    if (gated_button("Extend my squad's contracts", true, g, "Every player of your club gets the extra years.")) {
        ask("Extend your squad's contracts?",
            "Every player of your club gets " + std::to_string(years) + " more years on his contract." + kCantUndo, "Extend contracts",
            "extend_user_contracts", {{"years", years}}, "Extend my squad's contracts",
            "Asked to add " + std::to_string(years) + " years to your squad's contracts.");
    }
    card_end();
}

void card_growth(App& app) {
    card_begin("##career_growth", "Growth");
    hint("Weekly growth is built but not tested in a match yet.");
    json& dv = auto_cfg(app, "development");
    static int weekly = 1;
    static bool no_decline = true;
    form_label("Points each week", "How much each player grows every week (1 to 5).");
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::SliderInt("##careerweekly", &weekly, 1, 5);
    ImGui::Checkbox("Nobody gets worse##careernodecline", &no_decline);
    bool squad = dv.value("user_team", false);
    if (ImGui::Checkbox("Grow my squad every week##careergrow", &squad)) {
        dv["user_team"] = squad;
        dv["weekly"] = weekly;
        dv["no_decline"] = no_decline;
        const bool any = dv.contains("players") && dv["players"].is_array() && !dv["players"].empty();
        dv["enabled"] = squad || any;
        apply_auto(app, "Weekly growth");
    }
    const Gate gd = lua_gate(app, true, "development");
    if (gated_button("Develop to potential", true, gd, "Every player of your club reaches his potential now.")) {
        ask("Develop everyone to potential?", "Every player of your club grows to his potential at once." + std::string(kCantUndo), "Develop them",
            "development", {{"scope", {{"user_team", true}}}, {"mode", "to_potential"}, {"confirm", true}}, "Develop my squad to potential",
            "Asked to develop the squad to potential.");
    }
    ImGui::SameLine();
    const Gate gr = lua_gate(app, true, "reveal");
    if (gated_button("Reveal my players", false, gr, "Shows every hidden number of the players at your club.")) {
        ask("Reveal your players?", "Every player of your club is shown as fully scouted." + std::string(kCantUndo), "Reveal them", "reveal",
            {{"scope", {{"user_team", true}}}}, "Reveal my club", "Asked the game to reveal your players.");
    }
    card_end();
}

void card_squad_tools(App& app, const TeamRow* team) {
    card_begin("##career_tools", "Squad tools");
    struct Tool {
        const char* id;
        const char* label;
        const char* detail;
        bool tested;
    };
    static const Tool kTools[] = {
        {"block_offers", "Block offers for my players", "AI clubs stop making offers for your players.", true},
        {"morale", "Make everyone very happy", "The best morale the game still calls \"very happy\" for each player.", true},
        {"long_contract", "Give everyone 60 months", "Every player gets a new 60 month contract from today.", true},
        {"form", "Best form for everyone", "Form 100 for every player of your club.", false},
        {"match_xp", "Double the XP", "Twice the experience from matches and training.", false},
        {"dev_bonus", "Bonus growth points", "Every player grows by the points below, never above his potential.", false},
    };
    static int pick = 0;
    static int bonus = 2;
    form_label("Do this for my squad", "One action at a time. You are asked before it runs.");
    ImGui::SetNextItemWidth(S(280.0f));
    if (ImGui::BeginCombo("##careertool", kTools[pick].label)) {
        for (int i = 0; i < 6; ++i)
            if (ImGui::Selectable(kTools[i].label, i == pick)) pick = i;
        ImGui::EndCombo();
    }
    hint(kTools[pick].detail);
    if (!kTools[pick].tested) theme::warn_text("%s", kNotTested);
    if (std::string(kTools[pick].id) == "dev_bonus") {
        form_label("Points", "1 to 10.");
        ImGui::SetNextItemWidth(S(100.0f));
        ImGui::InputInt("##careerbonus", &bonus, 1, 1);
        bonus = std::max(1, std::min(bonus, 10));
    }
    const size_t n = team ? app.model.links_of_team(team->teamid).size() : 0;
    Gate g = lua_gate(app, true, nullptr);
    if (g.ok && (!team || n == 0)) g = {false, "There is no squad to change yet."};
    if (gated_button("Run for my squad", true, g, "Asks first, then does it for your whole squad.")) {
        const Tool& t = kTools[pick];
        ask(std::string(t.label) + "?", std::string(t.detail) + " This changes all " + std::to_string(n) + " players of your club." + kCantUndo, "Run it",
            "team_mass", {{"teamid", team->teamid}, {"actions", json::array({t.id})}, {"bonus_xp", bonus}}, std::string("Squad tool: ") + t.label,
            "Asked. Open a game screen or advance a day to see the result.", &app.mass_status);
    }
    if (!app.mass_status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        theme::muted_text("%s", app.mass_status.c_str());
        ImGui::PopTextWrapPos();
    }
    card_end();
}

// ---------------------------------------------------------------- Managers & staff
void staff_list(App& app) {
    const auto& mgrs = app.model.managers();
    static char search[64] = "";
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##careerstaffq", "manager or club name", search, sizeof(search));
    const std::string q = lower_copy(search);
    std::vector<size_t> rows;
    rows.reserve(mgrs.size());
    for (size_t i = 0; i < mgrs.size(); ++i) {
        if (!q.empty()) {
            const std::string team = mgrs[i].teamid > 0 ? app.model.team_name(mgrs[i].teamid) : std::string();
            if (lower_copy(mgrs[i].name).find(q) == std::string::npos && lower_copy(team).find(q) == std::string::npos) continue;
        }
        rows.push_back(i);
    }
    ImGui::BeginChild("##careerstaffrows", ImVec2(0, 0));
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(rows.size()));
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const size_t i = rows[static_cast<size_t>(r)];
            const ManagerRow& m = mgrs[i];
            const std::string label = m.name + "##careerm" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), app.sel_manager == static_cast<int>(i))) app.sel_manager = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Manager ID %lld", static_cast<long long>(m.managerid));
            ImGui::SameLine(S(190.0f));
            theme::muted_text("%s", m.teamid > 0 ? app.model.team_name(m.teamid).c_str() : "free agent");
        }
    }
    if (rows.empty()) theme::muted_text("No manager matches.");
    ImGui::EndChild();
}

void staff_editor(App& app, const Table& t) {
    const auto& mgrs = app.model.managers();
    if (app.sel_manager < 0 || app.sel_manager >= static_cast<int>(mgrs.size())) {
        theme::muted_text("Pick a manager on the left.");
        card_begin("##career_scouts_only", "Your scouts");
        scouts_section(app);  // ui_parity.h
        card_end();
        return;
    }
    const ManagerRow& m = mgrs[static_cast<size_t>(app.sel_manager)];
    if (!app.db.table_alive(t, m.rec)) {
        theme::err_text("The game's data changed (another save loaded?). Press Refresh at the top.");
        return;
    }
    ImGui::Text("%s", m.name.c_str());
    ImGui::SameLine();
    theme::muted_text("%s", m.teamid > 0 ? app.model.team_name(m.teamid).c_str() : "free agent");
    hint("Manager rules and moves are under Career > Manager jobs.");
    card_begin("##career_staff_details", "Details");
    field_grid(app, t, m.rec, {"firstname", "surname", "commonname", "nationality", "personalityid", "height", "weight", "skintonecode"}, "##careermgrid", 2);
    bodytype_gallery_button(app, t, m.rec, true);  // ui_bodytypes.h
    card_end();
    card_begin("##career_staff_face", "Face and picture");
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::TreeNode("Choose a real face##careerface")) {
        manager_appearance(app, t, m);  // ui_faces.h
        ImGui::TreePop();
    }
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (ImGui::TreeNode("Small picture in menus##careerminiface")) {
        MinifaceTarget mt;
        mt.manager = true;
        mt.headassetid = app.db.get_int(t, m.rec, "headassetid", 0);
        mt.path = legacy_path::staff_miniface_for(m.managerid, mt.headassetid);
        mt.shown = app.legacy.first_present(legacy_path::staff_pictures(m.managerid, mt.headassetid));
        mt.id = m.managerid;
        mt.teamid = m.teamid;
        miniface_editor(app, mt);  // ui_images.h
        ImGui::TreePop();
    }
    card_end();
    card_begin("##career_scouts", "Your scouts");
    scouts_section(app);  // ui_parity.h
    card_end();
}

}  // namespace

// ---------------------------------------------------------------- the pages
void draw_career_transfers_money(App& app) {
    if (waiting_for_career(app)) return;
    Page page(app, "##career_money", "Your budget, other clubs' contracts and this season's moves.");
    card_budget(app);
    card_other_clubs_contracts(app);
    card_moves(app, g_state.club);
    card_bans(app);
    hint("To put a player on the transfer or loan list, open him and use More.");
}

void draw_career_manager_jobs(App& app) {
    if (waiting_for_career(app)) return;
    Page page(app, "##career_jobs", "Get a job offer, keep your job, or move another manager.");
    card_job_offer(app);
    card_sacking(app);
    card_move_manager(app);
}

void draw_career_my_squad(App& app) {
    if (waiting_for_career(app)) return;
    Page page(app, "##career_squad", "How is my squad? Form, roles, contracts and growth for the whole club.");
    const TeamRow* team = g_state.club > 0 ? app.model.team(g_state.club) : nullptr;
    if (team) {
        ImGui::Text("%s", team->name.c_str());
    } else {
        theme::muted_text("Waiting for your career.");
    }
    card_form_fitness(app);
    card_squad_roles(app, team);
    card_my_contracts(app);
    card_growth(app);
    card_squad_tools(app, team);
}

void draw_career_staff(App& app) {
    if (waiting_for_career(app)) return;
    const Table* t = app.db.table("manager");
    if (!t) {
        theme::muted_text("The game's data has no manager table under that name.");
        return;
    }
    ImGui::BeginChild("##career_stafflist", ImVec2(S(320.0f), 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    staff_list(app);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##career_staffedit", ImVec2(0, 0));
    staff_editor(app, *t);
    ImGui::EndChild();
}

const CareerPageState& career_page_state() { return g_state; }

}  // namespace turbo
