// FC 27 LE Turbo GUI - club commentary names: a club is spoken with another club's recorded name, or with none, in
// matches only (turbo_dev/PITWALL/briefs/4.0-club-callnames.md).
//
// FC 27 has no club-name bank: the commentator says a club's name from team-keyed families selected by club-id
// parameters (asset_team_ID, home_ID, away_ID, ...; the exact list is voice::is_club_param). The voice-swap Preprocess
// hook (core/callname_voice.h) rewrites those parameters from B to voice_of(B) together, so the lines recorded for club A
// play for club B. voice_of 0 = no club name: the ids are rewritten to 0, which no family holds (what the game says
// then is NOT verified in game). Nothing is written to the database or the save.
//
// Store: turbo_output\callnames\club_voice.json. Each entry has a scope like a Turbo team name (core/reapply.h):
// "career:<id>" only in that career, "global" in every career; the App publishes only the entries in effect in the
// loaded career and publishes again at every career load.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "callname_voice.h"

namespace turbo {
namespace clubvoice {

// {"turbo_club_voice": 1, "clubs": [{"teamid", "voice_of", "scope", "club", "from", "when"}]}
struct Entry {
    int64_t teamid = 0;    // B, the club whose spoken name changes
    int64_t voice_of = 0;  // A, the club whose recorded name is used; 0 = no club name
    std::string scope;     // "global" or "career:<id>" (core/reapply.h); "" = not assigned (every career)
    std::string club;      // display only
    std::string from;      // display only ("Arsenal", "No club name")
    std::string when;      // "YYYY-MM-DD HH:MM"
};

struct Store {
    std::vector<Entry> entries;  // one per club and scope, sorted by teamid, then scope
    // A missing file is an empty store (true). A bad file is renamed to club_voice.json.bad-<stamp> and gives an empty
    // store (true, *err says so). Bad entries are dropped with a note in *err. false (empty store, *err) only when the
    // file cannot be opened or a bad file cannot be set aside.
    bool load(const std::filesystem::path& file, std::string* err);
    bool save(const std::filesystem::path& file, std::string* err) const;  // atomic (tmp + rename)
    void upsert(const Entry& e);  // replaces the club's entry of the same scope
    bool forget(int64_t teamid, const std::string& scope);
    const Entry* find(int64_t teamid, const std::string& scope) const;
    // The club's entry in effect in the career identified as `career` ("" = none): this career's, else global, else not
    // assigned; nullptr = none
    const Entry* find_in_effect(int64_t teamid, const std::string& career) const;
};

std::filesystem::path store_path(const std::filesystem::path& le_root);  // <LE>\turbo_output\callnames\club_voice.json
std::string store_json(const Store& s);
bool parse_store_json(const std::string& text, Store& out, std::string* err);

// The entries in effect in `career`, as the hook reads them: sorted by teamid, one per club; a club given its own
// name is no change and is left out
std::vector<voice::Table::Team> build_teams(const Store& s, const std::string& career);

// The clubs with a recorded name in one commentary language: turbo\callnames\clubs\<lang>.json {"lang", "ids": [...]}
// (turbo/tools/fc27_commentary/club_ids.py). A missing or bad file gives an empty list (false, *err).
bool load_recorded(const std::filesystem::path& file, std::vector<int32_t>& ids, std::string* err);
std::filesystem::path recorded_path(const std::filesystem::path& le_root, const std::string& lang);

// 5.0c E15 (Cassio 10-30, "the selection seems short ... dig for hidden and custom team names"): how a recorded club is named in the picker.
// The teams-table name first (`row_name`, "" = the club has no row in this database), then the Live Editor custom_team_names.csv name
// (`fallback_name`), else "Club <id> (not in this database)" (never "team <id>"). id 0 = "No club name".
#define TURBO_E15_DONOR_LABEL 1
std::string donor_label(int64_t id, const std::string& row_name, const std::string& fallback_name);
// A club the label names (a row or a CSV name); false = "Club <id> (not in this database)"
inline bool donor_is_named(const std::string& row_name, const std::string& fallback_name) { return !row_name.empty() || !fallback_name.empty(); }
// The two ids the recordings hold that are not clubs of a database: 999997 (generic) and 9999990 (the no-club marker). They are listed only behind
// "Show special ids". The 800001-800112 block (fictional clubs) is a real choice and stays listed.
bool is_special_id(int64_t id);
// Height of the donor list: `rows` rows of `line_h` (text height + item spacing) and the child's top and bottom padding. A fixed row count, never
// GetContentRegionAvail: the card is AutoResizeY, so an available height grows with the list every frame.
float donor_list_height(float line_h, float pad_y, int rows = 14);

}  // namespace clubvoice
}  // namespace turbo
