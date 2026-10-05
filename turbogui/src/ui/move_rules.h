// FC 27 LE Turbo - which player moves can run for one player, and why not (Players tab, 1.1.1).
// The window mirrors the rules Lua core/moves.lua checks before it writes anything (docs/turbo-reference.md "Player
// moves"), so a button that cannot work for this player says why before the click instead of being greyed out with no
// reason. The Lua side checks again (it sees the whole database); this header only needs what the window knows.
#pragma once

#include <cstdint>
#include <string>

namespace move_rules {

constexpr int64_t kFreeAgents = 111592;
constexpr int kMaxSquad = 52;   // the team sheet's 52 slots
constexpr int kMinSquad = 18;   // 11 starters + 7 substitutes

// What the window knows about the player
struct Facts {
    int64_t user_team = 0;      // your club (0 = unknown)
    int64_t club = 0;           // his club link (0 = none)
    bool loans_known = true;    // false: no playerloans table (Lua decides)
    bool on_loan = false;       // a playerloans row
    int64_t loaned_from = 0;    // playerloans.teamidloanedfrom
    int squad = -1;             // players linked to his club (-1 = unknown)
    bool last_keeper = false;   // his club's only goalkeeper
};

inline bool free_agent(const Facts& f) { return f.club == kFreeAgents; }
inline bool at_user_club(const Facts& f) { return f.user_team > 0 && f.club == f.user_team; }

// The squad rules for a player leaving his club ("" = fine)
inline std::string leave_rule(const Facts& f) {
    if (f.club <= 0 || free_agent(f)) return "";
    if (f.last_keeper) return "He is his club's only goalkeeper: sign another one first";
    if (f.squad >= kMinSquad && f.squad - 1 < kMinSquad)
        return "His club would have " + std::to_string(f.squad - 1) + " players, fewer than the 18 a match squad needs";
    return "";
}

// Why `action` cannot run for this player ("" = it can). Actions: release, terminate_loan, loan, transfer, delete,
// transfer_list, loan_list, unlist (list_status always runs).
inline std::string why_not(const std::string& action, const Facts& f) {
    const bool lists = action == "transfer_list" || action == "loan_list" || action == "unlist";
    if (lists) {
        if (f.user_team <= 0) return "";   // the Lua side says it: your club is not known yet
        if (!at_user_club(f))
            return "The game's transfer and loan lists belong to your club: it cannot list another club's player "
                   "(FC 27 lists him on YOUR club whoever he plays for)";
        if (f.on_loan) return "He is on loan at your club: only his parent club can list him";
        return "";
    }
    if (f.club <= 0 && action != "delete") return "He has no club link (teamplayerlinks)";
    if (action == "release") {
        if (free_agent(f)) return "He is already a free agent";
        return leave_rule(f);
    }
    if (action == "terminate_loan") {
        if (f.loans_known && !f.on_loan) return "He is not on loan";
        return "";
    }
    if (action == "loan") {
        if (f.on_loan) return "He is already on loan: end that loan first (Terminate loan)";
        if (free_agent(f)) return "A free agent has no club to loan him out: transfer him instead";
        return leave_rule(f);
    }
    if (action == "transfer" || action == "delete") return leave_rule(f);
    return "";
}

// Why a transfer / loan to `to_team` cannot run ("" = it can). to_squad: players linked to that club (-1 unknown)
inline std::string why_not_to(const std::string& action, const Facts& f, int64_t to_team, bool exists, bool national,
                              int to_squad) {
    if (!exists) return "Pick a club: no team has this ID";
    if (national) return "That is a national team: pick a club";
    if (to_team == f.club) return "He already plays there";
    if (action == "loan" && to_team == kFreeAgents) return "Free Agents is not a club: pick the club he goes to";
    if (to_team != kFreeAgents && to_squad >= kMaxSquad)
        return "That club has " + std::to_string(to_squad) + " players, the most a squad holds";
    return why_not(action, f);
}

// The note shown before a move that touches your club (it runs: this is information, not a refusal). game_moves: transfers
// and releases go through the game's own move (BridgeState::game_moves)
inline const char* own_club_note(bool game_moves = false) {
    if (game_moves)
        return "Your club: the game itself makes the transfer or release (a listed player comes off your lists first; a release "
               "pays his compensation like the game's own). The squad screens, morale and form show it at once. Loans and "
               "loaned players are written into the career database and show after saving and loading. Back up your save.";
    return "Your club: Turbo writes the move into the career database (your team sheet too; a listed player comes off "
           "your lists first). Back up your save; the squad screens show it after saving and loading the career.";
}

}  // namespace move_rules
