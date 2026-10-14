// FC 27 LE Turbo GUI - Tactics tab honesty (Turbo 2.1 Phase 0, docs/plans/tactics-2.1/PLAN.md section 6 "Badges" / "Removed",
// turbo_output\TACTICS_REPORT.txt section E items P0-1, P0-2, P0-3, P0-5, P0-9).
//
// Plain data next to the slider registry (core/sliders.h), kept out of it so the registry stays the single list of controls:
//   1. Two badges per control, kept apart: HOW it is written (game variable / database row / preview / Turbo only) and WHAT WAS SEEN of
//      it (seen in a played match + date, no effect seen + date, untested, legacy). Evidence: turbo_output\TACTICS_REPORT.txt (2026-10-11),
//      turbo_dev\evidence\2026-10-07\RESULTS.md. A control this table does not name gets the honest default: a write path that nobody
//      saw working in a match is "untested"; no write path is "nothing written".
//   2. Which controls are legacy (EA removed them in FC 25) and stay hidden behind "show legacy fields". Since 2.4 none of them writes: the
//      old mentalities table is no write target any more; the two widths and the 7-step mentality are Turbo's preview only. (The role
//      editor's name, "Role familiarity N (Role+ / Role++)", is the registry's own label: core/sliders.cpp.)
//   3. Targets the Tactics tab never writes: your club's saved tactic row (cm_mentalities) is read once at career load and overwritten from
//      the live copy at every save (docs/re/realtime_transfers.md:170-187), so a write there alone can never reach a match. The registry
//      no longer names it; this guard refuses it anyway. 2.4: its defensivedepth / buildupplay / pos<n>role columns ARE written, by My team
//      (channel E, core/team_sheet.h) in the same job as the live record they mirror; the reason says so (still refused here).
//   4. One kill-switch file per write channel in turbo_output (tactics_off.txt stays the master switch).
//   5. Loading a preset: controls that write but were never seen working stay switched OFF (the user switches them on himself).
//   6. The snapshot store: the value of a database field before Turbo's first write, kept in turbo_output\tactics_snapshot.json per career
//      club, target and row, so "Reset to default" and the master kill switch can put it back, and only while the row still holds the
//      value Turbo wrote (game variables need no snapshot: clearing the override is the default).
// No ImGui, no game memory, no clock (callers pass timestamps). Header-only so no build list changes while other work lands.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "sliders.h"
#include "tactic_profiles.h"

namespace turbo {

// ------------------------------------------------------------------------------------------------ badges
// How a value reaches the game (LiveSheet: tactics channel E, your club's live tactic record written in place, core/team_sheet.h)
enum class WriteKind { GameVar, DbRow, Preview, TurboOnly, LiveSheet };
// What was seen of its effect
enum class SeenKind { SeenInMatch, NoEffectSeen, Untested, Legacy, NothingWritten };

struct TacticStatus {
    WriteKind written = WriteKind::Preview;
    SeenKind seen = SeenKind::NothingWritten;
    std::string date;          // "2026-10-07" for SeenInMatch / NoEffectSeen, else empty
    std::string note;          // one line: the evidence or the caveat
    bool legacy_hidden = false;  // only drawn behind "show legacy fields"
    std::string label;         // display label (the registry's)
};

inline const char* written_badge(WriteKind k) {
    switch (k) {
        case WriteKind::GameVar: return "game variable";
        case WriteKind::DbRow: return "database row";
        case WriteKind::Preview: return "preview";
        case WriteKind::TurboOnly: return "Turbo only";
        case WriteKind::LiveSheet: return "live team sheet";
    }
    return "preview";
}

inline std::string seen_badge(const TacticStatus& s) {
    switch (s.seen) {
        case SeenKind::SeenInMatch: return "seen in a played match " + s.date;
        case SeenKind::NoEffectSeen: return "no effect seen " + s.date;
        case SeenKind::Untested: return "untested";
        case SeenKind::Legacy: return "legacy, likely ignored";
        case SeenKind::NothingWritten: return "nothing written";
    }
    return "";
}

// The hover text of the "how written" badge
inline const char* written_help(WriteKind k) {
    switch (k) {
        case WriteKind::GameVar: return "Set as a game variable through the game's own call; lasts until the game closes; played matches only.";
        case WriteKind::DbRow: return "Written into the career database row; saved with the career.";
        case WriteKind::Preview: return "Shown in the preview only: no proven way to write it to the game. Nothing is written.";
        case WriteKind::TurboOnly: return "A Turbo setting: it shapes Turbo's own calculation and is never sent to the game.";
        case WriteKind::LiveSheet:
            return "Written in place into your club's live tactic record in the game's memory (channel E), on the game thread, and read back. It should "
                   "reach the next match set up after it and your next in-game save (not verified yet).";
    }
    return "";
}

// The hover text of the "what was seen" badge
inline const char* seen_help(SeenKind k) {
    switch (k) {
        case SeenKind::SeenInMatch: return "Its effect was seen in a played match on the date shown (turbo_dev\\evidence).";
        case SeenKind::NoEffectSeen: return "The game accepted the write, but no effect was seen in a played match on the date shown.";
        case SeenKind::Untested: return "The write path exists, but nobody has seen it work in a played match yet. Off by default.";
        case SeenKind::Legacy: return "EA removed this control in FC 25; the old table is written, likely ignored. Off by default.";
        case SeenKind::NothingWritten: return "Nothing is written for this control.";
    }
    return "";
}

// The one-line tag under a row (the old "Live" / "saved data, in-game effect unverified" wording is gone)
inline std::string status_tag(const TacticStatus& s) {
    switch (s.written) {
        case WriteKind::Preview: return "not applied to game";
        case WriteKind::TurboOnly: return "Turbo only, never written to the game";
        case WriteKind::GameVar:
        case WriteKind::DbRow:
        case WriteKind::LiveSheet: break;
    }
    std::string t = std::string(written_badge(s.written)) + ", " + seen_badge(s);
    if (!s.note.empty()) t += ": " + s.note;
    return t;
}

namespace tactics_status_detail {

struct Entry {
    SeenKind seen;
    const char* date;
    const char* note;
    bool legacy_hidden;
};

inline const std::map<std::string, Entry>& table() {
    static const std::map<std::string, Entry> t = {
        // ---- game variables: turbo_dev\evidence\2026-10-07\RESULTS.md (Napoli v Nott'm Forest, turbo04, 12-minute matches)
        {"match.injury_frequency_user", {SeenKind::SeenInMatch, "2026-10-07", "injuries at 1', 3', 5' with frequency and severity 100 on both sides (not isolated from severity)", false}},
        {"match.injury_frequency_cpu", {SeenKind::SeenInMatch, "2026-10-07", "Forest injuries at 1' and 3' with the value at 100", false}},
        {"match.injury_severity_user", {SeenKind::SeenInMatch, "2026-10-07", "forced substitutions with frequency and severity both 100 (not isolated from frequency)", false}},
        {"match.injury_severity_cpu", {SeenKind::SeenInMatch, "2026-10-07", "Forest forced to carry two injured players (not isolated from frequency)", false}},
        {"match.never_injure", {SeenKind::Untested, "", "accepted by the game 2026-10-04 (read back); no played match with it set is on record", false}},
        {"match.weather", {SeenKind::SeenInMatch, "2026-10-07", "3 gave snow on the pitch, 1 'Partly Clear'; the other numbers were never shown", false}},
        {"match.time_of_day", {SeenKind::NoEffectSeen, "2026-10-07", "0 and 4 both gave a 5:00 PM daylight match", false}},
        {"match.difficulty", {SeenKind::NoEffectSeen, "2026-10-07", "set to 5, the pre-match line still said Amateur; no in-match place shows the level", false}},
        {"match.cpu_subs_off", {SeenKind::SeenInMatch, "2026-10-07", "Forest made no substitution for two injured players (watched to 9:36)", false}},
        // ---- database rows
        {"match.foul_strictness", {SeenKind::Untested, "", "written to every referee row; never tested in a match; which end is the strict one is unknown", false}},
        {"match.card_strictness", {SeenKind::Untested, "", "written to every referee row; never tested in a match; which end is the strict one is unknown", false}},
        {"team.line_depth", {SeenKind::Untested, "", "teams.defensivedepth is written (match effect unknown); your saved tactic row is NOT written: the game overwrites it at every save", false}},
        {"team.buildup_play", {SeenKind::Untested, "", "teams.buildupplay is written (match effect unknown); your saved tactic row is NOT written: the game overwrites it at every save", false}},
        // ---- legacy: EA removed these controls in FC 25. Since 2.4 nothing is written for them (the old mentalities table is no write
        // target); the widths shape Turbo's pitch preview only
        {"team.width_def", {SeenKind::NothingWritten, "", "removed in FC 25; Turbo's preview only, never written", true}},
        {"team.width_att", {SeenKind::NothingWritten, "", "removed in FC 25; Turbo's preview only, never written", true}},
        {"team.mentality", {SeenKind::NothingWritten, "", "FC 27 has no 7-step mentality; preview only", true}},
        // ---- the role editor: players.role1..9 are the player's own Role+ / Role++ familiarity (field_labels.h), not a slot's role
        {"pos.role1", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role2", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role3", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role4", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role5", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role6", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role7", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role8", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        {"pos.role9", {SeenKind::Untested, "", "the player's own familiarity list, not the role of a tactic slot", false}},
        // ---- opposition offsets: the same proven CPU injury variables; difficulty had no effect seen
        {"opp.injury_frequency_offset", {SeenKind::SeenInMatch, "2026-10-07", "the CPU injury frequency variable, seen 2026-10-07; the offset is added to the Match value", false}},
        {"opp.injury_severity_offset", {SeenKind::SeenInMatch, "2026-10-07", "the CPU injury severity variable, seen 2026-10-07; the offset is added to the Match value", false}},
        {"opp.difficulty_offset", {SeenKind::NoEffectSeen, "2026-10-07", "the match difficulty override showed no effect on 2026-10-07", false}},
    };
    return t;
}

}  // namespace tactics_status_detail

// The status of a slider: the table above, else the honest default from its binding
inline TacticStatus tactic_status(const SliderDef& d) {
    TacticStatus s;
    s.label = d.label;
    switch (d.status) {
        case SliderStatus::Live: s.written = WriteKind::GameVar; s.seen = SeenKind::Untested; break;
        case SliderStatus::DB: s.written = WriteKind::DbRow; s.seen = SeenKind::Untested; break;
        case SliderStatus::Local: s.written = WriteKind::TurboOnly; s.seen = SeenKind::NothingWritten; break;
        case SliderStatus::Preview:
        case SliderStatus::RE: s.written = WriteKind::Preview; s.seen = SeenKind::NothingWritten; break;
    }
    if (s.seen == SeenKind::Untested) s.note = "never seen working in a played match";
    const auto& t = tactics_status_detail::table();
    auto it = t.find(d.key);
    if (it == t.end()) return s;
    const tactics_status_detail::Entry& e = it->second;
    // a write path that does not exist cannot have been seen: the binding wins over the table
    if (s.written == WriteKind::Preview || s.written == WriteKind::TurboOnly) s.seen = SeenKind::NothingWritten;
    else s.seen = e.seen;
    s.date = e.date;
    s.note = e.note;
    s.legacy_hidden = e.legacy_hidden;
    return s;
}

inline bool legacy_hidden(const SliderDef& d) { return tactic_status(d).legacy_hidden; }
// writes something and was never seen working: stays off when a preset loads, asks before Apply
inline bool untested_write(const TacticStatus& s) {
    return (s.written == WriteKind::GameVar || s.written == WriteKind::DbRow) && s.seen != SeenKind::SeenInMatch;
}

// ------------------------------------------------------------------------------------------------ dead write targets
// The columns of the saved tactic row that channel E (My team) writes with the live record (2.4 WP-B1): defensivedepth, buildupplay,
// pos0role..pos10role
inline bool channel_e_row_column(const std::string& field) {
    if (field == "defensivedepth" || field == "buildupplay") return true;
    if (field.size() < 8 || field.size() > 9 || field.compare(0, 3, "pos") != 0 || field.compare(field.size() - 4, 4, "role") != 0) return false;
    const std::string n = field.substr(3, field.size() - 7);
    if (n.empty() || n.find_first_not_of("0123456789") != std::string::npos) return false;
    return std::atoi(n.c_str()) <= 10;
}

// Targets the Tactics tab never writes: nullptr = fine, else the one-line reason (both kinds of cm_mentalities column are refused here)
inline const char* dead_target_reason(const std::string& target) {
    if (target.rfind("cm_mentalities.", 0) != 0) return nullptr;
    if (channel_e_row_column(target.substr(15)))
        return "not written here: My team (channel E) writes this column of your saved tactic row together with your club's live tactic (the row "
               "alone is read once at career load and overwritten from the live copy at every save)";
    return "dead: your saved tactic row is read once at career load and overwritten from the live copy at every save (not written)";
}

// ------------------------------------------------------------------------------------------------ channel E (your club's live tactic)
// The badges of the live tactic card (ui_live_tactic.cpp): written "live team sheet", seen "untested" until the in-game acceptance test of
// turbo_output\TACTICS_CHANNEL_E_IMPL.txt is run (then the entry below gets SeenInMatch and its date). Not registry sliders: keyed live.*
namespace tactics_status_detail {
inline const std::map<std::string, Entry>& live_sheet_table() {
    static const std::map<std::string, Entry> t = {
        {"live.build_up", {SeenKind::Untested, "", "Build-Up Style: written into your club's live tactic record (and in the same job its cm_mentalities row, when that row matches the live tactic: the Apply line says which), read back; the match's own copy is read at kick-off (log line); whether Edit Tactic shows it and a played match uses it: not checked yet", false}},
        {"live.line_height", {SeenKind::Untested, "", "Line Height: written into your club's live tactic record (and in the same job its cm_mentalities row, when that row matches the live tactic: the Apply line says which), read back; the match's own copy is read at kick-off (log line); whether Edit Tactic shows it and a played match uses it: not checked yet", false}},
        {"live.approach", {SeenKind::Untested, "", "Defensive Approach: written into your club's live tactic record, band-consistent with the Line Height, read back; the match's own copy is read at kick-off (log line); whether Edit Tactic shows it and a played match uses it: not checked yet", false}},
        {"live.role", {SeenKind::Untested, "", "the slot's three role values (word, role id, focus) written together and read back; Edit Tactic and a played match not checked yet", false}},
    };
    return t;
}
}  // namespace tactics_status_detail

inline TacticStatus live_sheet_status(const std::string& key, const std::string& label) {
    TacticStatus s;
    s.label = label;
    s.written = WriteKind::LiveSheet;
    s.seen = SeenKind::Untested;
    const auto& t = tactics_status_detail::live_sheet_table();
    auto it = t.find(key);
    if (it != t.end()) {
        s.seen = it->second.seen;
        s.date = it->second.date;
        s.note = it->second.note;
    } else {
        s.note = "never seen working in a played match";
    }
    return s;
}

// ------------------------------------------------------------------------------------------------ the club tactic's teams.* fields (gate X2)
// teams.defensivedepth / teams.buildupplay are database sliders of the Tactics tab (team.line_depth / team.buildup_play, "accepted, untested").
// Gate X2 (docs/TACTICS_OVERHAUL_DECISION.md 4.2) decides where they go, and THIS function is the one place that says it:
//   TacticsTab - until X2: the Tactics tab writes them as today;
//   Removed    - X2 shows the match copy takes the map record's value: the tab no longer writes them (the club tactic is My team's job);
//   ChannelE   - X2 shows the match copy takes teams.*: the tab no longer writes them; channel E's job writes the club's teams row with the
//                live record (a second core/team_sheet.h DbRowWrite: the same fields, defensivedepth / buildupplay, checks and rollback).
enum class TeamsTacticRoute { TacticsTab, Removed, ChannelE };
inline TeamsTacticRoute teams_tactic_route() { return TeamsTacticRoute::TacticsTab; }
inline bool teams_tactic_target(const std::string& target) { return target == "teams.defensivedepth" || target == "teams.buildupplay"; }
// nullptr = the Tactics tab writes this target; else why it does not (the row says it)
inline const char* teams_tactic_refusal(const std::string& target) {
    if (!teams_tactic_target(target)) return nullptr;
    switch (teams_tactic_route()) {
        case TeamsTacticRoute::TacticsTab: return nullptr;
        case TeamsTacticRoute::Removed:
            return "not written: the match takes your club's tactic from its live record (gate X2): set it in My team (channel E)";
        case TeamsTacticRoute::ChannelE:
            return "not written here: My team (channel E) writes it together with your club's live tactic (gate X2)";
    }
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ kill switches
enum class WriteChannel { GameVar, DbRow, LiveSheet, Tuning };

struct KillSwitch {
    const char* file;   // in turbo_output
    const char* what;   // what it turns off
    bool all = false;   // the master switch
    WriteChannel channel = WriteChannel::GameVar;
};

inline const std::vector<KillSwitch>& kill_switches() {
    static const std::vector<KillSwitch> k = {
        {"tactics_off.txt", "every tactics write (and the effect layers of the pitch)", true, WriteChannel::GameVar},
        {"tactics_gamevar_off.txt", "the Tactics tab's game-variable rows (injuries, weather, time of day, difficulty, CPU substitutions)", false, WriteChannel::GameVar},
        {"tactics_db_off.txt", "career database rows (teams, referee, players)", false, WriteChannel::DbRow},
        {"call_team_sheet_off.txt", "your club's live tactic (channel E)", false, WriteChannel::LiveSheet},
        // after the channel's own file (channel_switch_file names the first one): only the cm_mentalities row channel E writes with the record
        {"channel_e_db_off.txt", "the cm_mentalities row of your club's live tactic (channel E writes the live record alone)", false, WriteChannel::LiveSheet},
        {"tuning_profile_off.txt", "the gameplay tuning profile's automatic writes (career load, hub)", false, WriteChannel::Tuning},
    };
    return k;
}

inline const char* channel_name(WriteChannel c) {
    switch (c) {
        case WriteChannel::GameVar: return "game variables";
        case WriteChannel::DbRow: return "database rows";
        case WriteChannel::LiveSheet: return "live team sheet";
        case WriteChannel::Tuning: return "gameplay tuning profile";
    }
    return "";
}

// The channel a write target goes through: a game variable has no "table." part (2.4: the old mentalities table has no channel of its
// own any more: no control writes it; a row kept from 2.3 in the snapshot store still goes back when tactics_off.txt appears)
inline WriteChannel channel_of_target(const std::string& target) {
    const size_t dot = target.find('.');
    if (dot == std::string::npos) return WriteChannel::GameVar;
    return WriteChannel::DbRow;
}

inline const char* channel_switch_file(WriteChannel c) {
    for (const KillSwitch& k : kill_switches())
        if (!k.all && k.channel == c) return k.file;
    return "";
}

// Which switch files exist right now (the caller reads the directory; this is plain data)
struct SwitchState {
    std::set<std::string> present;  // file names
    bool all() const { return present.count(kill_switches().front().file) > 0; }
    bool blocks(WriteChannel c) const { return all() || present.count(channel_switch_file(c)) > 0; }
    // the file that blocks this channel ("" = none): the master first
    std::string blocking_file(WriteChannel c) const {
        if (all()) return kill_switches().front().file;
        const char* f = channel_switch_file(c);
        return present.count(f) ? f : "";
    }
    std::vector<std::string> files() const { return std::vector<std::string>(present.begin(), present.end()); }
};

inline SwitchState read_switches(const std::filesystem::path& turbo_output) {
    SwitchState s;
    std::error_code ec;
    for (const KillSwitch& k : kill_switches())
        if (std::filesystem::exists(turbo_output / k.file, ec)) s.present.insert(k.file);
    return s;
}

// ------------------------------------------------------------------------------------------------ presets: untested stays off
struct ProfileLoadResult {
    std::vector<std::string> held_off;  // keys loaded with their value but left switched off (untested / legacy write)
    std::vector<std::string> enabled;   // keys switched on
};

// Copies a preset into `working`: a control that writes to the game but was never seen working in a played match (or a legacy field) gets
// its value but stays OFF; everything the preset leaves to the game is reset. Preview-only and Turbo-only controls follow the preset.
inline ProfileLoadResult load_profile_into(const Profile& p, SliderSet& working) {
    ProfileLoadResult r;
    for (const auto& kv : p.sliders.values) {
        const SliderDef* d = find_slider(kv.first);
        if (!d) continue;
        if (!p.sliders.is_enabled(kv.first)) {
            working.reset(kv.first);
            continue;
        }
        const TacticStatus s = tactic_status(*d);
        if (untested_write(s) || s.legacy_hidden) {
            working.set(kv.first, kv.second, false);
            working.set_enabled(kv.first, false);
            r.held_off.push_back(kv.first);
        } else {
            working.set(kv.first, kv.second, true);
            r.enabled.push_back(kv.first);
        }
    }
    return r;
}

// ------------------------------------------------------------------------------------------------ snapshot rows
// A snapshot row names the career it was kept in, so a value kept in one career is never put into another: "club:95|team:1". The user's
// club stands for the career until the bridge publishes the save's own id (TACTICS_REPORT.txt P0-8); two careers with the same club share
// it (the "the row still holds Turbo's value" check below is the second guard). Kinds: team (teams.teamid), player (players.playerid),
// referee (referee.refereeid), mentalities (mentalities.mentalityid: written by 2.1-2.3 only; kept so their rows can still be put back).
inline std::string snapshot_row(int64_t user_club, const std::string& row) { return "club:" + std::to_string(user_club) + "|" + row; }

struct SnapshotRow {
    int64_t club = 0;
    std::string kind;  // "team" "player" "referee" "mentalities"
    int64_t id = 0;
};

inline bool snapshot_int(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (!end || *end != 0) return false;
    out = int64_t(v);
    return true;
}

// false when the text is not "club:<n>|<kind>:<n>"
inline bool parse_snapshot_row(const std::string& s, SnapshotRow& out) {
    const size_t bar = s.find('|');
    if (bar == std::string::npos || s.rfind("club:", 0) != 0) return false;
    if (!snapshot_int(s.substr(5, bar - 5), out.club)) return false;
    const std::string rest = s.substr(bar + 1);
    const size_t colon = rest.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    out.kind = rest.substr(0, colon);
    return snapshot_int(rest.substr(colon + 1), out.id);
}

// The field that finds a row of this kind in its table ("" = not a kind Turbo restores)
inline const char* snapshot_key_field(const std::string& kind) {
    if (kind == "team") return "teamid";
    if (kind == "player") return "playerid";
    if (kind == "referee") return "refereeid";
    // the 2.3 upgrade path: since 2.4 nothing writes the old mentalities table, but a row a 2.1-2.3 Turbo wrote there is still put back by
    // tactics_off.txt through this key (native case "the 2.3 upgrade path"): keep this line
    if (kind == "mentalities") return "mentalityid";
    return "";
}

// ------------------------------------------------------------------------------------------------ snapshots (database fields)
struct DbSnapshot {
    std::string target;  // "teams.defensivedepth"
    std::string row;     // "club:1|team:1", "club:1|player:2001", "club:1|referee:155" (snapshot_row)
    int64_t value = 0;   // the value before Turbo's first write
    std::string when;    // "2026-10-11 15:02" (caller's clock)
    std::string label;   // "Arsenal" (for the user only)
    int64_t written = 0;        // the value Turbo wrote last: a restore only undoes Turbo's own write (the row must still hold it)
    bool has_written = false;
};

constexpr int kTacticSnapshotVersion = 1;

class TacticSnapshots {
public:
    std::map<std::string, DbSnapshot> items;  // "target@row" -> snapshot

    static std::string key(const std::string& target, const std::string& row) { return target + "@" + row; }
    // Keeps the FIRST value seen for this target and row; false when one is already kept (the original stays)
    bool remember(const std::string& target, const std::string& row, int64_t value, const std::string& when, const std::string& label) {
        const std::string k = key(target, row);
        if (items.count(k)) return false;
        items[k] = DbSnapshot{target, row, value, when, label};
        return true;
    }
    const DbSnapshot* find(const std::string& target, const std::string& row) const {
        auto it = items.find(key(target, row));
        return it == items.end() ? nullptr : &it->second;
    }
    // What Turbo wrote (after every write): false when nothing is kept for this row
    bool note_written(const std::string& target, const std::string& row, int64_t value) {
        auto it = items.find(key(target, row));
        if (it == items.end()) return false;
        it->second.written = value;
        it->second.has_written = true;
        return true;
    }
    // Reset may restore only when the row still holds what Turbo wrote (else someone else changed it since: leave it alone)
    static bool may_restore(const DbSnapshot& s, int64_t current) { return !s.has_written || current == s.written; }
    bool forget(const std::string& target, const std::string& row) { return items.erase(key(target, row)) > 0; }
    bool empty() const { return items.empty(); }

    std::string to_json() const {
        nlohmann::ordered_json j = nlohmann::ordered_json::object();
        j["turbo_tactics_snapshot"] = kTacticSnapshotVersion;
        nlohmann::ordered_json rows = nlohmann::ordered_json::array();
        for (const auto& kv : items) {
            nlohmann::ordered_json r = nlohmann::ordered_json::object();
            r["target"] = kv.second.target;
            r["row"] = kv.second.row;
            r["value"] = kv.second.value;
            r["when"] = kv.second.when;
            r["label"] = kv.second.label;
            if (kv.second.has_written) r["written"] = kv.second.written;
            rows.push_back(r);
        }
        j["rows"] = rows;
        return j.dump(1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    }
    // false (with err) when the text is not a snapshot file; malformed rows are dropped and counted
    static bool parse(const std::string& text, TacticSnapshots& out, std::string* err, size_t* dropped = nullptr) {
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("turbo_tactics_snapshot") || !j["turbo_tactics_snapshot"].is_number_integer() ||
            j["turbo_tactics_snapshot"].get<int>() < 1) {
            if (err) *err = "not a Turbo tactics snapshot file";
            return false;
        }
        out.items.clear();
        size_t bad = 0;
        if (j.contains("rows") && j["rows"].is_array()) {
            for (const nlohmann::json& r : j["rows"]) {
                if (!r.is_object() || !r.contains("target") || !r["target"].is_string() || !r.contains("row") || !r["row"].is_string() ||
                    !r.contains("value") || !r["value"].is_number_integer()) {
                    ++bad;
                    continue;
                }
                DbSnapshot s;
                s.target = r["target"].get<std::string>();
                s.row = r["row"].get<std::string>();
                s.value = r["value"].get<int64_t>();
                s.when = r.value("when", std::string());
                s.label = r.value("label", std::string());
                if (r.contains("written") && r["written"].is_number_integer()) {
                    s.written = r["written"].get<int64_t>();
                    s.has_written = true;
                }
                out.items.emplace(key(s.target, s.row), s);
            }
        }
        if (dropped) *dropped = bad;
        return true;
    }
};

// What the master kill switch does with one kept row (restore_all in ui_tactics.cpp): put it back only when it belongs to the loaded
// career's club, Turbo's write is on record and the row still holds that value
enum class RestoreVerdict { Restore, OtherCareer, NeverWritten, ChangedSince, NotARow };

inline RestoreVerdict restore_verdict(const DbSnapshot& s, int64_t user_club, int64_t current) {
    SnapshotRow r;
    if (!parse_snapshot_row(s.row, r) || snapshot_key_field(r.kind)[0] == 0) return RestoreVerdict::NotARow;
    if (r.club != user_club) return RestoreVerdict::OtherCareer;
    if (!s.has_written) return RestoreVerdict::NeverWritten;
    if (current != s.written) return RestoreVerdict::ChangedSince;
    return RestoreVerdict::Restore;
}

inline std::filesystem::path tactics_snapshot_path(const std::filesystem::path& le_root) { return le_root / "turbo_output" / "tactics_snapshot.json"; }

// A missing file is an empty store (true). False (with err) when the file exists and is not a snapshot file.
inline bool load_tactic_snapshots(const std::filesystem::path& p, TacticSnapshots& out, std::string* err) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        out.items.clear();
        return true;
    }
    std::string text;
    if (!read_text_file(p, text, err)) return false;
    return TacticSnapshots::parse(text, out, err);
}

inline bool save_tactic_snapshots(const std::filesystem::path& p, const TacticSnapshots& s, std::string* err) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    return write_text_atomic(p, s.to_json(), err);
}

}  // namespace turbo
