// FC 27 LE Turbo GUI - Team > Transfers (brief 2.20-IA-pages, track "team"; see ui_team_pages.h). Cassio 2026-10-19 10:56: "team menu should have
// a transfers tab: transfers in/out, loans in/out, contract extensions, squad roles."
//
// Cards for the selected club (App::sel_team, the user's club when none is chosen):
//   Moves this season      the club's transfers and loans in and out (ui_parity.h team_transfers_tab, read only)
//   Loans in and out       players on loan at the club and players it lent out, with End loan
//   Move a player          transfer, loan or release one of the club's players; the game's own transfer and loan lists (your club)
//   Sign a player          a transfer of any player to this club
//   Contract extensions    a longer contract for one player at a time (the game stores the year a contract ends: steps of a year)
//   Squad roles            the role of each player of your club
//
// Every button sends what the old Players panel sends (module player_moves with the same actions, squad_role) or writes the field through
// App::edit_field; a move the Undo button cannot bring back asks first. The rules of the old panel (move_rules.h) are the same: a move that
// cannot run for the player is greyed out with the reason. The transfer and loan lists, the roles and the morale exist in the game for the
// user's club only; for another club those controls say so.
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "move_rules.h"
#include "table_registry.h"
#include "ui_parity.h"
#include "ui_team_common.h"
#include "ui_team_pages.h"

namespace turbo {

using nlohmann::json;
using namespace teampage;

namespace {

TeamPageState g_state;
TeamMoveInputs g_in;

bool is_own_club(App& app, int64_t club) {
    const BridgeState& st = app.bridge.state();
    return st.in_cm && st.user_team > 0 && club == st.user_team;
}

// ---------------------------------------------------------------- the move rules (the old panel's move_facts)
move_rules::Facts facts_of(App& app, const PlayerRow& p) {
    move_rules::Facts f;
    f.user_team = app.bridge.state().user_team;
    f.club = p.club;
    f.loans_known = app.model.loans_known();
    f.on_loan = p.on_loan;
    f.loaned_from = p.loaned_from;
    if (p.club > 0) {
        const auto squad = app.model.links_of_team(p.club);
        f.squad = static_cast<int>(squad.size());
        if (p.positions[0] == 0) {  // a goalkeeper: is there another one?
            f.last_keeper = true;
            for (const auto& l : squad) {
                const PlayerRow* m = l.playerid != p.playerid ? app.model.player(l.playerid) : nullptr;
                if (m && m->positions[0] == 0) {
                    f.last_keeper = false;
                    break;
                }
            }
        }
    }
    return f;
}

// The gate of a move button: this build can run the tool (key: caps "move_transfer" ...), a career is open, nothing is in flight, and the
// move can run for THIS player (`why`, move_rules.h; "" = it can)
Gate move_gate(App& app, const char* key, const std::string& why) {
    const BridgeState& st = app.bridge.state();
    if (const std::string* missing = st.unavailable_reason(key)) return {false, *missing};
    if (!app.mailbox) return {false, "Turbo can't reach the game right now."};
    if (!st.in_cm) return {false, "Open your career in the game first."};
    if (app.busy()) return {false, "Wait for the last request to finish."};
    if (!why.empty()) return {false, "Not possible for him: " + why};
    return {};
}

json move_cmd(const json& action) { return run_cmd("player_moves", {{"actions", json::array({action})}}); }

// The sentence appended to a question that touches the user's club
std::string own_note(App& app, const move_rules::Facts& f, int64_t other_club) {
    if (f.user_team > 0 && (f.club == f.user_team || other_club == f.user_team)) return std::string(" ") + move_rules::own_club_note(app.bridge.state().game_moves);
    return std::string();
}

// A combo over the players of the club; the pick is kept in `sel` (and in App::sel_player, shared with Team > Players)
const PlayerRow* squad_combo(App& app, const std::vector<const PlayerRow*>& squad, int64_t& sel, const char* id) {
    // the player of Team > Players follows into this combo (both pages share App::sel_player); a pick here is shared back
    const auto in = [&](int64_t pid) { return std::any_of(squad.begin(), squad.end(), [&](const PlayerRow* p) { return p->playerid == pid; }); };
    if (in(app.sel_player)) sel = app.sel_player;
    else if (!in(sel)) sel = squad.empty() ? 0 : squad.front()->playerid;
    const PlayerRow* cur = sel > 0 ? app.model.player(sel) : nullptr;
    ImGui::SetNextItemWidth(S(280.0f));
    if (ImGui::BeginCombo(id, cur ? cur->name.c_str() : "no players")) {
        for (const PlayerRow* p : squad) {
            const std::string row = p->name + "  " + position_name(p->position) + " " + std::to_string(p->overall) + "##tpmv" + std::to_string(p->playerid);
            if (ImGui::Selectable(row.c_str(), p->playerid == sel)) {
                sel = p->playerid;
                app.sel_player = sel;
            }
        }
        ImGui::EndCombo();
    }
    return cur;
}

// ---------------------------------------------------------------- Moves this season
void card_moves(App& app, int64_t club) {
    ++g_state.cards;
    card_begin("##tt_moves", "Moves this season");
    ImGui::BeginChild("##tt_moves_list", ImVec2(0, S(230.0f)), ImGuiChildFlags_None);
    team_transfers_tab(app, club);  // ui_parity.h: transfers and loans in and out of the club (read only)
    ImGui::EndChild();
    card_end();
}

// ---------------------------------------------------------------- Loans in and out
struct LoanCache {
    uint64_t version = ~0ull;
    int64_t club = -1;
    std::vector<int64_t> in, out;
};
LoanCache g_loans;

void refresh_loans(App& app, int64_t club) {
    if (g_loans.version == app.model.version() && g_loans.club == club) return;
    g_loans = LoanCache{};
    g_loans.version = app.model.version();
    g_loans.club = club;
    for (const PlayerRow& p : app.model.players()) {
        if (!p.on_loan) continue;
        if (p.club == club) g_loans.in.push_back(p.playerid);
        else if (p.loaned_from == club) g_loans.out.push_back(p.playerid);
    }
}

void loan_rows(App& app, const std::vector<int64_t>& ids, bool incoming, const char* none_text) {
    if (ids.empty()) {
        theme::muted_text("%s", none_text);
        return;
    }
    for (size_t i = 0; i < ids.size() && i < 40; ++i) {
        const PlayerRow* p = app.model.player(ids[i]);
        if (!p) continue;
        ImGui::PushID(static_cast<int>(p->playerid & 0x7FFFFFFF));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", p->name.c_str());
        ImGui::SameLine(S(190.0f));
        const int64_t other = incoming ? p->loaned_from : p->club;
        theme::muted_text("%s %s", incoming ? "from" : "now at", other > 0 ? app.model.team_name(other).c_str() : "no club");
        ImGui::SameLine(S(420.0f));
        const move_rules::Facts f = facts_of(app, *p);
        const Gate g = move_gate(app, "move_terminate_loan", move_rules::why_not("terminate_loan", f));
        if (gated_button(("End loan###tpend" + std::to_string(p->playerid)).c_str(), false, g, "He goes back to his parent club.")) {
            ask("End the loan of " + p->name + "?", p->name + " goes back to his parent club." + own_note(app, f, 0) + kCantUndo, "End the loan",
                move_cmd({{"action", "terminate_loan"}, {"playerid", p->playerid}}), "Terminate loan", "Asked: end the loan of " + p->name + ".");
        }
        ImGui::PopID();
    }
    if (ids.size() > 40) theme::muted_text("%zu more not shown.", ids.size() - 40);
}

void card_loans(App& app, int64_t club) {
    refresh_loans(app, club);
    g_state.loans_in = g_loans.in.size();
    g_state.loans_out = g_loans.out.size();
    ++g_state.cards;
    card_begin("##tt_loans", "Loans in and out");
    if (!app.model.loans_known()) {
        theme::muted_text("This game's data has no loan records.");
    } else {
        ImGui::SeparatorText("On loan at this club");
        loan_rows(app, g_loans.in, true, "Nobody is on loan here.");
        ImGui::SeparatorText("Lent out by this club");
        loan_rows(app, g_loans.out, false, "Nobody is lent out.");
    }
    card_end();
}

// ---------------------------------------------------------------- Move a player
void list_fallback(App& app, const PlayerRow& p) {
    const move_rules::Facts f = facts_of(app, p);
    struct B {
        const char* label;
        const char* action;
        const char* key;
        const char* tip;
    };
    static const B buttons[] = {
        {"Transfer list###tplst1", "transfer_list", "move_transfer_list", "Puts him on your club's transfer list; AI clubs then make offers."},
        {"Loan list###tplst2", "loan_list", "move_loan_list", "Puts him on your club's loan list."},
        {"Remove from lists###tplst3", "unlist", "move_unlist", "Takes him off the transfer list and the loan list."},
        {"List status###tplst4", "list_status", "move_list_status", "Asks the game whether he is on the transfer or loan list."},
    };
    bool first = true;
    for (const B& b : buttons) {
        if (!first) ImGui::SameLine();
        first = false;
        const std::string act = b.action;
        const Gate g = move_gate(app, b.key, act == "list_status" ? std::string() : move_rules::why_not(act, f));
        if (gated_button(b.label, false, g, b.tip)) app.send(move_cmd({{"action", act}, {"playerid", p.playerid}}), std::string(b.label).substr(0, std::string(b.label).find('#')));
    }
}

void card_move(App& app, int64_t club, const std::vector<const PlayerRow*>& squad) {
    ++g_state.cards;
    card_begin("##tt_move", "Move a player");
    int64_t& to_team = g_in.to_team;
    int& fee = g_in.fee;
    int& wage = g_in.wage;
    int& months = g_in.months;
    int& loan_months = g_in.loan_months;
    form_label("Player", "One of this club's players.");
    const PlayerRow* p = squad_combo(app, squad, g_in.player, "##tpmvplayer");
    if (!p) {
        theme::muted_text("This club has no players.");
        card_end();
        return;
    }
    g_state.player = p->playerid;
    const move_rules::Facts f = facts_of(app, *p);
    form_label("To club", "The club he goes to. A loan goes there too.");
    club_search_box(app, "##tpmvclub", to_team, S(280.0f));
    form_label("Fee", "What the club pays.");
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::InputInt("##tpmvfee", &fee, 0);
    fee = std::max(0, fee);
    form_label("Wage", "0 keeps his current wage.");
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::InputInt("##tpmvwage", &wage, 0);
    wage = std::max(0, wage);
    form_label("Contract months", "The length of his new contract.");
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::InputInt("##tpmvmonths", &months, 0);
    months = std::max(1, std::min(months, 120));
    form_label("Loan months", "How long a loan lasts.");
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::InputInt("##tpmvloanm", &loan_months, 0);
    loan_months = std::max(1, std::min(loan_months, 60));

    const TeamRow* dest = app.model.team(to_team);
    const bool exists = dest != nullptr;
    const bool national = exists && app.model.is_national_team(to_team);
    const int to_squad = exists ? static_cast<int>(app.model.links_of_team(to_team).size()) : -1;
    const std::string why_t = move_rules::why_not_to("transfer", f, to_team, exists, national, to_squad);
    const std::string why_l = move_rules::why_not_to("loan", f, to_team, exists, national, to_squad);
    const std::string dest_name = dest ? dest->name : std::string();
    if (f.on_loan && why_t.empty()) hint("He is on loan: a transfer ends the loan (his parent club sells him).");

    if (gated_button("Transfer###tpmvtransfer", true, move_gate(app, "move_transfer", why_t), "He moves to that club for good.")) {
        ask("Transfer " + p->name + "?", p->name + " moves to " + dest_name + " for " + money(fee) + "." + own_note(app, f, to_team) + kCantUndo, "Transfer",
            move_cmd({{"action", "transfer"}, {"playerid", p->playerid}, {"to_teamid", to_team}, {"fee", fee}, {"wage", wage}, {"months", months}}), "Transfer",
            "Asked: transfer " + p->name + " to " + dest_name + ".");
    }
    ImGui::SameLine();
    if (gated_button("Loan###tpmvloan", false, move_gate(app, "move_loan", why_l), "He plays for that club for the loan months, then comes back.")) {
        ask("Loan " + p->name + "?", p->name + " goes to " + dest_name + " for " + std::to_string(loan_months) + " months." + own_note(app, f, to_team) + kCantUndo,
            "Loan him", move_cmd({{"action", "loan"}, {"playerid", p->playerid}, {"to_teamid", to_team}, {"months", loan_months}}), "Loan",
            "Asked: loan " + p->name + " to " + dest_name + ".");
    }
    ImGui::SameLine();
    if (gated_button("Release###tpmvrelease", false, move_gate(app, "move_release", move_rules::why_not("release", f)), "He leaves the club and becomes a free agent.")) {
        ask("Release " + p->name + "?", p->name + " leaves " + (p->club > 0 ? app.model.team_name(p->club) : std::string("his club")) + " and becomes a free agent." +
                                             own_note(app, f, 0) + kCantUndo,
            "Release him", move_cmd({{"action", "release"}, {"playerid", p->playerid}}), "Release", "Asked: release " + p->name + ".");
    }
    ImGui::Spacing();
    ImGui::SeparatorText("Transfer and loan lists");
    if (!is_own_club(app, club)) {
        theme::muted_text("The game's transfer and loan lists belong to your own club.");
    } else if (list_buttons) {
        list_buttons(app, *p, true);  // the old Players panel's list buttons (needs exposing, see ui_team_pages.h)
    } else {
        list_fallback(app, *p);
    }
    card_end();
}

// ---------------------------------------------------------------- Sign a player
void card_sign(App& app, int64_t club) {
    ++g_state.cards;
    card_begin("##tt_sign", "Sign a player");
    static char search[64] = "";
    int64_t& pick = g_in.sign_pick;
    int& fee = g_in.sign_fee;
    int& wage = g_in.sign_wage;
    int& months = g_in.sign_months;
    form_label("Find a player", "Part of his name.");
    ImGui::SetNextItemWidth(S(280.0f));
    ImGui::InputTextWithHint("##tpsignq", "player name", search, sizeof(search));
    const std::string q = lower_copy(search);
    if (q.size() >= 3) {
        // the search walks every player of the database: it runs when the text, the club or the lists change, not every frame
        static std::string last_q;
        static uint64_t last_version = ~0ull;
        static int64_t last_club = -1;
        static std::vector<int64_t> hits;
        if (q != last_q || last_version != app.model.version() || last_club != club) {
            last_q = q;
            last_version = app.model.version();
            last_club = club;
            hits.clear();
            for (const PlayerRow& p : app.model.players()) {
                if (p.club == club || lower_copy(p.name).find(q) == std::string::npos) continue;
                hits.push_back(p.playerid);
                if (hits.size() >= 8) break;
            }
        }
        for (int64_t id : hits) {
            const PlayerRow* p = app.model.player(id);
            if (!p) continue;
            const std::string row = p->name + "  (" + (p->club > 0 ? app.model.team_name(p->club) : std::string("no club")) + ")  " + position_name(p->position) +
                                    " " + std::to_string(p->overall) + "##tpsign" + std::to_string(p->playerid);
            if (ImGui::Selectable(row.c_str(), pick == p->playerid)) pick = p->playerid;
        }
        if (hits.empty()) theme::muted_text("No player matches.");
    } else {
        hint("Type at least three letters.");
    }
    const PlayerRow* p = pick > 0 ? app.model.player(pick) : nullptr;
    const TeamRow* dest = app.model.team(club);
    if (p && dest) {
        ImGui::Text("Sign %s for %s", p->name.c_str(), dest->name.c_str());
        form_label("Fee", "What the club pays.");
        ImGui::SetNextItemWidth(S(140.0f));
        ImGui::InputInt("##tpsignfee", &fee, 0);
        fee = std::max(0, fee);
        form_label("Wage", "0 keeps his current wage.");
        ImGui::SetNextItemWidth(S(140.0f));
        ImGui::InputInt("##tpsignwage", &wage, 0);
        wage = std::max(0, wage);
        form_label("Contract months", "The length of his new contract.");
        ImGui::SetNextItemWidth(S(140.0f));
        ImGui::InputInt("##tpsignmonths", &months, 0);
        months = std::max(1, std::min(months, 120));
        const move_rules::Facts f = facts_of(app, *p);
        const std::string why = move_rules::why_not_to("transfer", f, club, true, app.model.is_national_team(club), static_cast<int>(app.model.links_of_team(club).size()));
        if (gated_button("Sign him###tpsignbtn", true, move_gate(app, "move_transfer", why), "He moves to this club for good.")) {
            ask("Sign " + p->name + "?", p->name + " moves to " + dest->name + " for " + money(fee) + "." + own_note(app, f, club) + kCantUndo, "Sign him",
                move_cmd({{"action", "transfer"}, {"playerid", p->playerid}, {"to_teamid", club}, {"fee", fee}, {"wage", wage}, {"months", months}}), "Transfer",
                "Asked: sign " + p->name + " for " + dest->name + ".");
        }
    }
    card_end();
}

// ---------------------------------------------------------------- Contract extensions
constexpr int kExtendMonths[] = {12, 24, 36, 48, 60};
const char* const kExtendLabels[] = {"1 year", "2 years", "3 years", "4 years", "5 years"};

void card_extensions(App& app, int64_t club, const std::vector<const PlayerRow*>& squad) {
    ++g_state.cards;
    card_begin("##tt_extend", "Contract extensions");
    const Table* pt = app.db.table("players");
    const Field* cf = pt ? pt->field("contractvaliduntil") : nullptr;
    if (!pt || !cf) {
        theme::muted_text("This game's data has no contract end year.");
        card_end();
        return;
    }
    int& pick = g_in.extend_pick;
    form_label("Extend by", "The game stores the year a contract ends, so a contract grows in whole years.");
    ImGui::SetNextItemWidth(S(160.0f));
    ImGui::Combo("##tpextby", &pick, kExtendLabels, 5);
    pick = std::max(0, std::min(pick, 4));
    const int add_years = kExtendMonths[pick] / 12;
    g_state.listed = squad.size();
    g_state.extend_line.clear();
    ImGui::BeginChild("##tt_extend_rows", ImVec2(0, S(260.0f)), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##tpexttbl", 4, tbl::kData | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Contract ends", ImGuiTableColumnFlags_WidthFixed, S(110.0f));
        ImGui::TableSetupColumn("After", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(90.0f));
        ImGui::TableHeadersRow();
        for (const PlayerRow* p : squad) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(p->playerid & 0x7FFFFFFF));
            const int64_t until = app.db.table_alive(*pt, p->rec) ? app.db.get_int(*pt, p->rec, "contractvaliduntil", 0) : 0;
            const int64_t after = until + add_years;
            const bool loaned_in = p->on_loan && p->loaned_from != club;
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(p->name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(until));
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(after));
            if (g_state.extend_line.empty()) g_state.extend_line = p->name + ": contract until " + std::to_string(after);
            ImGui::TableNextColumn();
            Gate g;
            if (loaned_in) g = {false, "A loaned player keeps the contract of his parent club."};
            else if (until <= 0) g = {false, "He has no contract end year yet: set it in the Contract card of Team > Players."};
            else if (after > cf->max()) g = {false, "That is longer than the game allows."};
            else if (!app.db.table_alive(*pt, p->rec)) g = {false, "The game's data changed: press Refresh at the top."};
            if (gated_button(("Extend###tpext" + std::to_string(p->playerid)).c_str(), false, g, "Writes the new end year into his contract (the Undo button takes it back).")) {
                if (app.edit_field(*pt, p->rec, *cf, Value::of_int(after))) g_status = "Extended " + p->name + "'s contract to " + std::to_string(after) + ".";
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (is_own_club(app, club)) {
        const std::string all = "Whole squad to " + std::to_string(add_years) + (add_years == 1 ? " year" : " years") + "###tpextall";
        const Gate g = lua_gate(app, true, "extend_user_contracts");
        if (gated_button(all.c_str(), false, g, "Every player of your club gets a contract that ends this many years from today's date in the game.")) {
            ask("Extend your squad's contracts?",
                "Every player of your club gets a contract that ends " + std::to_string(add_years) + (add_years == 1 ? " year" : " years") +
                    " from today's date in the game." + kCantUndo,
                "Extend contracts", run_cmd("extend_user_contracts", {{"years", add_years}}), "Extend my squad's contracts",
                "Asked: every contract of your squad ends " + std::to_string(add_years) + " years from today.");
        }
    }
    card_end();
}

// ---------------------------------------------------------------- Squad roles
void card_roles(App& app, int64_t club, const std::vector<const PlayerRow*>& squad) {
    ++g_state.cards;
    card_begin("##tt_roles", "Squad roles");
    if (!is_own_club(app, club)) {
        theme::muted_text("The game keeps squad roles for your own club only.");
        card_end();
        return;
    }
    const Gate g = lua_gate(app, true, "squad_role");
    ImGui::BeginChild("##tt_roles_rows", ImVec2(0, S(260.0f)), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##tproletbl", 4, tbl::kData | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, S(70.0f));
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(130.0f));
        ImGui::TableHeadersRow();
        for (const PlayerRow* p : squad) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(p->playerid & 0x7FFFFFFF));
            int& role = g_in.role_pick.try_emplace(p->playerid, 2).first->second;
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(p->name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(position_name(p->position));
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo("##tprolepick", &role, role_names(), 5);
            role = std::max(0, std::min(role, 4));
            ImGui::TableNextColumn();
            if (gated_button(("Set###tprole" + std::to_string(p->playerid)).c_str(), false, g, "Gives him this role through the game itself. Open the Squad screen again to see it."))
                app.send(run_cmd("squad_role", {{"playerid", p->playerid}, {"role", role + 1}}), "Squad role " + p->name);
            ImGui::SameLine();
            if (gated_button(("Read###tproleread" + std::to_string(p->playerid)).c_str(), false, g, "Asks the game which role he has now."))
                app.send(run_cmd("squad_role", {{"playerid", p->playerid}, {"mode", "check"}}), "Squad role " + p->name);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    hint("The game works every role out again at the start of a season. Whole-squad roles are in Team > Players, tab Whole squad.");
    card_end();
}

}  // namespace

// ---------------------------------------------------------------- the page
void draw_team_transfers(App& app) {
    g_state = TeamPageState{};
    if (!app.connected()) {
        g_state.waiting = true;
        not_connected_hint();
        return;
    }
    PageFrame page(app, "##team_transfers", "Moves in and out, loans, contract extensions and squad roles of one club.");
    const int64_t club = club_header(app, "##tttclub");
    g_state.club = club;
    if (club <= 0) {
        g_state.waiting = true;
        theme::muted_text("Waiting for your career.");
        return;
    }
    g_state.own_club = is_own_club(app, club);
    const std::vector<const PlayerRow*> squad = squad_of(app, club);
    log_page_once(app, "transfers", "Team page Transfers: club " + std::to_string(club) + " (" + app.model.team_name(club) + "), " +
                                        std::to_string(squad.size()) + " players");
    card_moves(app, club);
    card_loans(app, club);
    card_move(app, club, squad);
    card_sign(app, club);
    card_extensions(app, club, squad);
    card_roles(app, club, squad);
}

const TeamPageState& team_transfers_state() { return g_state; }
TeamMoveInputs& team_move_inputs() { return g_in; }

}  // namespace turbo
