// FC 27 LE Turbo GUI - the Callname page of 2.20 (brief 2.20-S1-callname): ONE list (generic callnames and real players'
// own recordings of the loaded commentary language), a search box, one click selects, ONE Preview button, ONE Apply
// button, an Undo. Apply gives the open player that callname in matches (a voice swap, core/callname_voice.h); the
// database and the player's written names are never touched.
//
// This header is the pure part (no ImGui, no App): the list, the search, the Apply check, the Entry an Apply writes, the
// words of the status line. ui_callnames.cpp draws the page and calls App::voice_upsert / voice_forget; the native tests
// (tests/native/test_callname_pick.h) check this part directly and the page through the UI.
//
// The older pickers (By name, By player, All callnames, Players without own recording, Voice swaps) stay in
// ui_callnames.cpp as callname_editor_legacy(); the page does not draw them. Only the native tests switch them on.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/callname_voice.h"
#include "core/callnames.h"
#include "core/callnames_catalog.h"

namespace turbo {

class App;
class Table;
struct PlayerRow;

// The page used to be the tabbed editor: callname_editor_legacy() is that body, kept (not drawn). Tests only.
void callname_editor_legacy(App& app, const Table& t, const PlayerRow& p);
void callname_use_legacy_page(bool on);

// What the page drew in its last frame (plain text is not an ImGui item, so the tests read it here)
struct CallnamePageState {
    int64_t playerid = 0;
    std::string language;      // "Commentary language: ita_it" without the label
    int rows_listed = 0;       // rows of the whole list (before the search)
    int rows_shown = 0;        // rows drawn after the search
    std::string selected;      // the picked row's name ("" = none)
    std::string state_line;    // "Applied: Kane" / "Not changed: the game says ..."
    std::string why_line;      // why Apply is greyed out ("" = it can be pressed)
    std::string off_line;      // "Callnames cannot be changed right now: ..." when the service is off
    std::string missing_line;  // the search matches a name the game's files do not record in this language ("" = none)
    int rows_unsayable = 0;    // names left out of the list because the game's files hold no recording of them
    bool apply_enabled = false;
    bool preview_enabled = false;
    bool undo_shown = false;
    std::string notice;        // the last result of Apply / Undo (also a toast)
};
const CallnamePageState& callname_page_state();

namespace callpick {

// The ONE list: what the game can SAY in the loaded language and team - the generic callnames and the players' own
// recordings that the game's commentary files hold (the asset catalog's bank), plus nothing else: a name the master list
// (the user's FC 26 workbook) knows but the FC 27 files do not record would play in Preview and then be silent in a match
// (El Ghazi, Cunningham in dut_nl), so it is NOT listed. Each row once (kind + id). The rows the files lack go to
// `unsayable` (when given), so the page can tell the user why a name he searches for is missing.
// Without the game's asset catalog (no bank) the master list's rows are listed as they are: nothing can be checked.
inline std::vector<AllCallnameRow> list_rows(const CatalogBank& bank, const MasterList& masters, const CallnameIndex& index,
                                             const CallnameDbUse& use, std::vector<AllCallnameRow>* unsayable = nullptr) {
    std::vector<AllCallnameRow> in;
    if (bank.loaded()) {
        for (AllCallnameRow& r : all_callnames_bank(bank, masters, index, use))
            if (callname_row_shown(bank, assets::Scope::All, r)) in.push_back(std::move(r));
    } else {
        in = all_callnames(masters, index, use.player_name ? use.player_name : std::function<std::string(int64_t)>([](int64_t) { return std::string(); }));
    }
    std::vector<AllCallnameRow> out;
    out.reserve(in.size());
    std::set<std::pair<bool, int64_t>> seen;
    for (AllCallnameRow& r : in) {
        if (!seen.insert({r.own(), r.own() ? r.playerid : r.commentaryid}).second) continue;
        if (bank.loaded() && !voice_source_gate(bank, r).ok) {
            if (unsayable) unsayable->push_back(std::move(r));
            continue;
        }
        out.push_back(std::move(r));
    }
    return out;
}

// A row's identity: "g<commentary id>" / "p<player id>"
inline std::string row_key(const AllCallnameRow& r) { return r.own() ? "p" + std::to_string(r.playerid) : "g" + std::to_string(r.commentaryid); }
inline const char* kind_label(const AllCallnameRow& r) { return r.own() ? "Real player" : "Generic"; }
inline std::string row_name(const AllCallnameRow& r) { return r.text.empty() ? std::string("(no name)") : r.text; }

// The search box: the typed words against the callname's text, case-insensitive; never an id
inline bool matches(const std::string& search, const AllCallnameRow& r) {
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const size_t b = search.find_first_not_of(" \t");
    if (b == std::string::npos) return true;
    const size_t e = search.find_last_not_of(" \t");
    return lower(r.text).find(lower(search.substr(b, e - b + 1))) != std::string::npos;
}

// Can Apply be pressed for this row and this player? why = one plain sentence when not.
struct Verdict {
    bool ok = true;
    std::string why;
};
inline Verdict can_apply(bool service_available, const std::string& service_why, const CatalogBank& bank, const AllCallnameRow& r,
                         int64_t target_playerid) {
    Verdict v;
    if (!service_available) {
        v.ok = false;
        // the service's own reasons ("game build", "kill switch", ...) in the words of the page
        const std::string reason = service_why == "game build"    ? "this version of the game is not supported"
                                   : service_why == "kill switch" ? "it is turned off"
                                                                  : "it is not available";
        v.why = "Callnames cannot be changed right now: " + reason + ".";
        return v;
    }
    if (r.own() && r.playerid == target_playerid) {
        v.ok = false;
        v.why = "That is his own recording already.";
        return v;
    }
    const VoiceGate g = voice_source_gate(bank, r);
    if (!g.ok) {
        v.ok = false;
        v.why = "This callname cannot be used in matches: " + g.why;
        return v;
    }
    return v;
}

// What Apply writes for player `pid`: a real player's own recording (kick-off name silent, as the game does for a real
// name), or a generic callname (a player with his own recording gets it turned off in the same entry). Every line.
inline voice::Entry make_entry(int64_t pid, const std::string& player_name, const AllCallnameRow& r, bool target_has_own) {
    voice::Entry e;
    e.playerid = pid;
    e.player = player_name;
    if (r.own()) {
        e.voice_of = r.playerid;
        e.kickoff = -1;
        e.from = r.text.empty() ? "player " + std::to_string(r.playerid) : r.text;
    } else {
        if (target_has_own) e.voice_of = 0;
        e.kickoff = r.commentaryid;
        e.from = (r.text.empty() ? "callname " + std::to_string(r.commentaryid) : r.text) + " (generic)";
    }
    e.names_only = false;
    return e;
}

// The line under a search that finds names the game cannot say: "<name> has no recording in the game's dut_nl commentary,
// so the game cannot say it." Empty when the search is empty or none of the left-out names matches it.
inline std::string missing_line(const std::string& search, const std::vector<AllCallnameRow>& unsayable, const std::string& where) {
    std::vector<const AllCallnameRow*> hit;
    for (const AllCallnameRow& r : unsayable)
        if (!r.text.empty() && search.find_first_not_of(" 	") != std::string::npos && matches(search, r)) hit.push_back(&r);
    if (hit.empty()) return std::string();
    std::string s = hit[0]->text;
    if (hit.size() > 1) s += " and " + std::to_string(hit.size() - 1) + " more";
    return s + (hit.size() > 1 ? " have" : " has") + " no recording in the game's " + where + " commentary, so the game cannot say " +
           (hit.size() > 1 ? "them" : "it") + ".";
}

// The callname an entry gives, in the words of the page: "Kane", "Lobotka", "his own recording is off", "no callname"
inline std::string entry_name(const voice::Entry& e) {
    std::string from = e.from;
    static const std::string suffix = " (generic)";
    if (from.size() > suffix.size() && from.compare(from.size() - suffix.size(), suffix.size(), suffix) == 0) from.resize(from.size() - suffix.size());
    if (e.voice_of && *e.voice_of > 0) return from.empty() ? "player " + std::to_string(*e.voice_of) : from;
    if (e.kickoff && *e.kickoff > 0) return from.empty() ? "callname " + std::to_string(*e.kickoff) : from;
    if (e.voice_of) return "his own recording is off";
    return "no callname";
}

// Undo: tied to the player Apply wrote for. The entry he had before (restored), or none (his entry is removed).
struct UndoSlot {
    int64_t playerid = 0;
    std::optional<voice::Entry> before;
    bool armed = false;
};

}  // namespace callpick
}  // namespace turbo
