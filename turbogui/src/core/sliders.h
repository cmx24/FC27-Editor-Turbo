// FC 27 LE Turbo GUI - the tactics slider registry (Turbo 2.0): every tactic / match / opposition setting as data.
//
// One SliderDef per setting (key, scope, label, description, range, default, step, binding, status) so the UI, the profile store and
// the opposition solver are all driven by the same table and no slider has code of its own. Plain data, no game memory, no ImGui.
// The catalogue is section 3 of docs/TURBO_2_0_PLAN.md. HONESTY: a slider the game is not known to read is status Preview (Turbo-only
// model) or RE (needs reverse engineering); the UI shows it, but nothing is ever written for it (writes_game() is false).
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace turbo {

enum class SliderScope { Match, Team, Position, Opposition };
// Live   = written to a proven game variable (gv::apply), takes effect in played matches
// DB     = written to a career database field that exists in the FC 27 schema (evidence Real = write proven, Plausible = field and
//          range proven by turbo/le27/fc27_db_schema.json, in-game effect not verified yet)
// Local  = a Turbo parameter (the opposition solver): applies inside Turbo, never written to the game
// Preview= plausible but unverified write path (or a formation preview): drawn, never written until promoted
// RE     = NEEDS-RE: no known write site; shown as "not applied to game", never written
enum class SliderStatus { Live, DB, Local, Preview, RE };
// How well the write path is known (the plan's REAL / PLAUSIBLE / NEEDS-RE)
enum class SliderEvidence { Real, Plausible, NeedsRe };
enum class SliderKind { Range, Enum, Toggle };
enum class BindingKind { None, GameVar, DbField, Turbo };

struct SliderBinding {
    BindingKind kind = BindingKind::None;
    std::vector<std::string> targets;  // GameVar: "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER"; DbField: "players.role1"; Turbo: solver parameter
};

struct SliderDef {
    std::string key;        // stable id, "<scope>.<name>": the key every profile file stores
    SliderScope scope = SliderScope::Match;
    std::string group;      // card the UI puts it in ("Injuries", "Pressing", ...)
    std::string label;      // plain-language name
    std::string description;  // hover text, plain language
    std::string technical;  // dim second line (game variable, FM / FC name); may be empty
    SliderKind kind = SliderKind::Range;
    int min = 0, max = 100, def = 50, step = 1;
    std::vector<int> allowed;             // Enum with gaps (time of day 0,1,3,4); empty = every value min..max
    std::vector<std::string> labels;      // Enum / Toggle names, index = value - min; empty = names not known
    bool labels_provisional = false;      // names are Turbo's, not read from the game
    bool range_from_meta = false;         // the real range comes from GetDBMeta at run time; min/max are only a safe envelope
    bool per_side = false;                // match slider that exists once for your team and once for the CPU team
    SliderBinding binding;
    SliderStatus status = SliderStatus::RE;
    SliderEvidence evidence = SliderEvidence::NeedsRe;
};

const char* scope_name(SliderScope s);     // "match" "team" "position" "opposition"
const char* status_name(SliderStatus s);   // "Live" "DB" "Local" "Preview" "RE"
const char* status_badge_help(SliderStatus s);  // one plain sentence for the badge tooltip
// true when a value of this slider is ever sent to the game (Live or DB)
inline bool writes_game(const SliderDef& d) { return d.status == SliderStatus::Live || d.status == SliderStatus::DB; }
inline bool preview_only(const SliderDef& d) { return d.status == SliderStatus::Preview || d.status == SliderStatus::RE; }

// The whole catalogue, in display order. Built once; the reference stays valid for the process.
const std::vector<SliderDef>& slider_registry();
const SliderDef* find_slider(const std::string& key);
std::vector<const SliderDef*> sliders_in_scope(SliderScope scope);
// Self-check of the table: unique non-empty keys with the "<scope>." prefix, min <= def <= max, step >= 1, allowed values inside the
// range, labels sized to the range, a binding that agrees with the status. Empty string when all is well, else one line per problem.
std::string validate_registry();

// Snap v into the slider's legal values: clamped to min..max, stepped from min, an Enum with gaps goes to the nearest allowed value
// (lower one on a tie). Total: never throws.
int clamp_slider(const SliderDef& d, int v);
bool slider_value_ok(const SliderDef& d, int v);  // v is already a legal value (clamp_slider(v) == v)

// What a profile or the UI currently holds. A key that is not in `enabled` is never written ("the game decides").
class SliderSet {
public:
    std::map<std::string, int> values;     // known keys only; always legal values
    std::set<std::string> enabled;         // keys the user switched on (a subset of `values`)
    // keys this build does not know (a profile from a newer Turbo): kept verbatim (raw JSON text) so a save does not lose them
    std::map<std::string, std::string> unknown_values;
    std::set<std::string> unknown_enabled;

    // Sets a known key (clamped); `enable` also switches it on. False for an unknown key (nothing stored).
    bool set(const std::string& key, int value, bool enable = true);
    bool has(const std::string& key) const { return values.count(key) > 0; }
    bool is_enabled(const std::string& key) const { return enabled.count(key) > 0; }
    // the stored value, else the registry default; 0 for an unknown key
    int value_or_default(const std::string& key) const;
    // Switch on / off. Switching on a key without a value gives it its default. False for an unknown key.
    bool set_enabled(const std::string& key, bool on);
    // Back to "the game decides": drops the value and the flag
    void reset(const std::string& key);
    void clear();
    size_t enabled_count() const { return enabled.size(); }
    bool empty() const { return values.empty() && unknown_values.empty(); }
    // All enabled keys of a scope (registry order)
    std::vector<std::string> enabled_keys(SliderScope scope) const;
};

// One line of a comparison between two sets: the value (and flag) on each side. "has" = the key is enabled on that side.
struct SliderChange {
    std::string key;
    bool has_a = false, has_b = false;
    int a = 0, b = 0;  // value of that side (the registry default when the side has none)
};
// Keys whose enabled state or value differs, registry order, then unknown keys. Equal sides give an empty list.
std::vector<SliderChange> diff_sliders(const SliderSet& a, const SliderSet& b);

// "What the game will receive": the enabled Live / DB sliders as (target, value) pairs. Preview / RE / Local sliders never appear.
struct SliderWrite {
    std::string key;
    std::string target;  // game variable path or "table.field"
    int value = 0;
    SliderStatus status = SliderStatus::Live;
};
std::vector<SliderWrite> game_writes(const SliderSet& s);
// How many of these sliders are preview-only (Preview or RE): for the "N of M visible settings are preview-only" strip
size_t count_preview_only(const std::vector<const SliderDef*>& visible);

}  // namespace turbo
