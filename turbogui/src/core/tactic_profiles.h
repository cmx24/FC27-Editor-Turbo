// FC 27 LE Turbo GUI - the tactic profile store (Turbo 2.0): presets by category, kept in <Live Editor>\turbo_output\tactic_profiles.json.
//
// Modelled on reapply.* (same safety rules): atomic save (.tmp then rename), a corrupt file is set aside as
// tactic_profiles.unreadable.json, unknown keys survive a load / save round trip, a file with a NEWER version loads read-only, the
// built-in profiles are code (immutable, never saved). Every slider of a profile has an `enabled` flag: a disabled slider is never
// written, so an untouched game stays Authentic. Plain data and JSON; save and load are called on construction and on user action only,
// never from the frame path. No game memory, no ImGui, no clock: the caller passes timestamps.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "sliders.h"

namespace turbo {

constexpr int kTacticProfilesVersion = 1;

enum class ProfileCategory { Match, Team, Position, Opposition, Bundle };
const char* category_name(ProfileCategory c);                       // "match" "team" "position" "opposition" "bundle"
bool category_from_name(const std::string& s, ProfileCategory& out);

struct Profile {
    std::string id;       // stable; "builtin.<category>.<slug>" for the built-ins, "p<16 hex>" otherwise
    ProfileCategory category = ProfileCategory::Team;
    std::string name;
    std::vector<std::string> tags;
    std::string note;
    std::string created;       // text the caller supplies ("2026-10-06 14:05"); never read from the clock here
    int schema_version = kTacticProfilesVersion;
    std::string game_build;
    std::string formation;     // "4-3-3" or empty (team / bundle)
    SliderSet sliders;         // values + enabled flags (+ unknown keys kept verbatim)
    std::string rules_json;    // bundle: the opposition rules (opp_rules JSON text), else empty
    bool builtin = false;      // immutable code
    bool locked = false;       // written by a newer Turbo (schema_version > ours): shown, never modified
    std::map<std::string, std::string> extra;  // unknown fields of this profile, raw JSON text, written back as they came
};

// What a load saw (counted like parse_reapply_json counts dropped entries)
struct ProfileLoadReport {
    size_t dropped_profiles = 0;  // not an object, no id / name, unknown category, duplicate id
    size_t bad_values = 0;        // a known slider with a non-integer value
    size_t unknown_keys = 0;      // slider keys this build does not know (kept)
    size_t clamped = 0;           // known sliders whose stored value was outside the legal range
};

const std::vector<Profile>& builtin_profiles();

class TacticProfileStore {
public:
    std::vector<Profile> profiles;                 // the user's profiles (never the built-ins)
    std::map<std::string, std::string> active;     // category name -> profile id (user or built-in)
    int file_version = kTacticProfilesVersion;
    bool read_only = false;                        // the file is newer than this build: every mutator refuses
    std::string game = "fc27";
    std::string build;                             // Turbo version that last saved it
    std::map<std::string, std::string> extra;      // unknown top-level fields, raw JSON text

    bool empty() const { return profiles.empty(); }
    // user profiles first by id, then the built-ins
    const Profile* find(const std::string& id) const;
    const Profile* find_by_name(ProfileCategory c, const std::string& name) const;  // case-insensitive, user and built-in
    // built-ins first, then the user's, in file order
    std::vector<const Profile*> list(ProfileCategory c) const;
    // `name` made unique inside the category ("Gegenpress" -> "Gegenpress (2)"); `except_id` is not counted as a clash
    std::string unique_name(ProfileCategory c, const std::string& name, const std::string& except_id = std::string()) const;
    // an id nobody uses yet, derived from `seed_text` (no clock, no rand): the same text on the same store gives the same id
    std::string make_id(const std::string& seed_text) const;

    // Mutators: false / empty string when the store is read-only, the profile is built-in or locked, or the id is unknown.
    // add: an empty id gets a new one, a clashing id or name is made unique; returns the id.
    std::string add(Profile p);
    bool overwrite(const std::string& id, const SliderSet& sliders, const std::string& formation);
    std::string duplicate(const std::string& id, const std::string& new_name, const std::string& created);  // also clones built-ins
    bool remove(const std::string& id);
    bool rename(const std::string& id, const std::string& new_name);  // false when the name is taken in the category or empty
    bool set_active(ProfileCategory c, const std::string& id);        // false for an unknown id
    void clear_active(ProfileCategory c);
    const Profile* active_profile(ProfileCategory c) const;
};

// {"turbo_tactic_profiles":1,"game":"fc27","build":"...","profiles":[...],"active":{...}}; the built-ins are not written
std::string tactic_profiles_json(const TacticProfileStore& s);
// Export of chosen profiles (one, a category or a bundle): the same file shape, built-ins included, `active` left out.
std::string export_profiles_json(const std::vector<const Profile*>& which, const std::string& build);
// false (with err) when the text is not a profile file (not JSON, not an object, no "turbo_tactic_profiles" >= 1). A newer version
// loads and sets read_only. Malformed entries are dropped or kept as described in ProfileLoadReport.
bool parse_tactic_profiles_json(const std::string& text, TacticProfileStore& out, std::string* err, ProfileLoadReport* report = nullptr);

// ---- import (the data level; the UI shows the plan, the user picks an action per clash)
enum class ClashKind { None, SameId, SameName };
enum class ClashAction { Rename, Replace, Skip };
struct ImportItem {
    Profile incoming;
    ClashKind clash = ClashKind::None;
    std::string existing_id;            // the profile it clashes with
    bool existing_builtin = false;      // a built-in cannot be replaced: Replace falls back to Rename
    std::vector<SliderChange> diff;     // existing -> incoming (empty without a clash)
};
struct ImportPlan {
    std::vector<ImportItem> items;
    ProfileLoadReport report;
};
// Reads an exported / saved file and finds the clashes against `store`. False (with err) when it is not a profile file.
bool plan_import(const TacticProfileStore& store, const std::string& text, ImportPlan& plan, std::string* err);
struct ImportResult {
    size_t added = 0, replaced = 0, renamed = 0, skipped = 0;
};
// actions[i] is for plan.items[i] (missing = Rename). A read-only store imports nothing (everything counts as skipped).
ImportResult apply_import(TacticProfileStore& store, const ImportPlan& plan, const std::vector<ClashAction>& actions);

// ---- files
std::filesystem::path tactic_profiles_path(const std::filesystem::path& le_root);        // <le_root>\turbo_output\tactic_profiles.json
std::filesystem::path tactic_profiles_unreadable_path(const std::filesystem::path& p);   // next to it: tactic_profiles.unreadable.json
bool read_text_file(const std::filesystem::path& p, std::string& out, std::string* err);
// tmp file next to `p`, then rename over it (two steps when the first rename fails: Windows)
bool write_text_atomic(const std::filesystem::path& p, const std::string& text, std::string* err);
// A missing file is an empty store. False (with err) for a file that is not a profile file: set it aside on the next save.
bool load_tactic_profiles(const std::filesystem::path& p, TacticProfileStore& out, std::string* err, ProfileLoadReport* report = nullptr);
// set_aside: first move an unreadable existing file to *.unreadable.json. Refuses (false) a read-only store.
bool save_tactic_profiles(const std::filesystem::path& p, const TacticProfileStore& s, std::string* err, bool set_aside = false);

}  // namespace turbo
