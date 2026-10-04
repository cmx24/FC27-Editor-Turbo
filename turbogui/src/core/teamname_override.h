// FC 27 LE Turbo GUI - live team names: what the game shows for a club's name, changed at once (no restart).
//
// The game never reads teams.teamname for a club's shown name: every screen asks its localization service for the
// strings "TeamName_<id>", "TeamName_Abbr15_<id>", "TeamName_Abbr10_<id>", "TeamName_Abbr3_<id>" (GetTeamName and
// ~20 direct users, the scoreboard included). They all end in LocImpl::Lookup(this, eastl::string* out, const char*
// key, int mode) (0x1421E2120 on build 6AB9813C-211EF000). Live Editor answers its custom_team_names.csv one level
// below (its hook on StrTab::GetString 0x140B1C034), from a copy it reads only when it starts. Turbo hooks Lookup ABOVE
// Live Editor (win/teamname_override_win.cpp): the original runs first (the game and Live Editor untouched), then the
// detour replaces `out` for the keys of the clubs renamed here. Lookup caches nothing, so the next lookup on any
// thread shows the new name; a screen that is already open shows it when it is built again. docs/turbo-reference.md.
//
// This header is the testable part (no Windows or game includes):
//   * Store: turbo_output\team_names.json, every club renamed in Turbo (all careers, like Live Editor's CSV);
//   * match_key: "TeamName[_AbbrN]_<id>" (and "IWL_" + that) -> team id and kind, allocation-free (the detour);
//   * Snapshot / SnapshotSlot: the immutable table the detour reads, swapped through an atomic pointer; old tables
//     are kept until the slot is destroyed (the host never destroys its slot);
//   * Service: what the App talks to (the host's; tests use a fake).
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace turbo {
namespace tnames {

// The four strings of a club, in the order of the Kind values
enum Kind : int { Full = 0, Abbr15 = 1, Abbr10 = 2, Abbr3 = 3 };
constexpr int kKinds = 4;
// Longest value per kind: the name in bytes (teams.teamname holds 59 + NUL), the short forms in characters
constexpr size_t kMaxLen[kKinds] = {59, 15, 10, 3};
// A value as Turbo keeps it: no ';', no line breaks, trimmed, cut to kMaxLen without halving a UTF-8 sequence
// (clean_team_name / clean_team_abbr, the same rules as Live Editor's CSV)
std::string clean_value(const std::string& s, Kind k);
const char* kind_key_part(Kind k);  // "", "Abbr15_", "Abbr10_", "Abbr3_"
// "TeamName_7", "TeamName_Abbr15_7", ... (Live Editor's CSV keys)
std::string key_of(int64_t teamid, Kind k);
// The club's long (official) name, "Associazione Calcio Milan": FC 27 has no string for it (only the four keys above,
// docs/re/team_names.md), so Turbo keeps it in its store and never gives it to the game. Cleaned like the name.
constexpr size_t kMaxLongLen = 100;
std::string clean_long_name(const std::string& s);

// ---------------------------------------------------------------- store (turbo_output\team_names.json)
// {"turbo_team_names": 1, "teams": [{"teamid": 7, "name": "...", "long": "...", "abbr15": "...", "abbr10": "...",
//   "abbr3": "...", "when": "YYYY-MM-DD HH:MM"}]}   ("long" since 1.1.4: files without it load, older Turbos ignore it)
struct Entry {
    int64_t teamid = 0;
    std::string text[kKinds];  // empty = not changed by Turbo (the game / Live Editor answer)
    std::string long_name;     // Turbo's only (FC 27 has no string for it); empty = the same as the name
    std::string when;
    const std::string& name() const { return text[Full]; }
    bool empty() const;
};

struct Store {
    std::vector<Entry> entries;  // one per club, sorted by teamid
    // A missing file is an empty store (true). A bad file is renamed to team_names.json.bad-<stamp> and gives an
    // empty store (true, *err says so). Bad entries are dropped with a note in *err. false (empty store, *err) only
    // when the file cannot be opened or a bad file cannot be set aside.
    bool load(const std::filesystem::path& file, std::string* err);
    bool save(const std::filesystem::path& file, std::string* err) const;  // atomic (tmp + rename)
    void upsert(const Entry& e);  // replaces the club's entry; an entry with no text removes it
    bool forget(int64_t teamid);
    const Entry* find(int64_t teamid) const;
};

std::filesystem::path store_path(const std::filesystem::path& le_root);  // <LE>\turbo_output\team_names.json
std::string store_json(const Store& s);
bool parse_store_json(const std::string& text, Store& out, std::string* err);

// ---------------------------------------------------------------- the key the game asks for (detour; no allocation)
struct KeyMatch {
    int64_t teamid = 0;
    Kind kind = Full;
};
// "TeamName_<id>", "TeamName_Abbr15_<id>", "TeamName_Abbr10_<id>", "TeamName_Abbr3_<id>", optionally after "IWL_"
// (what LocalizeString tries first when the service's IWL flag is set). Case-insensitive like the game's key hash; the
// id is 1..10 decimal digits (no sign) and ends the key. Reads at most 40 bytes of the key.
bool match_key(const char* key, KeyMatch& out) noexcept;

// Upper case for the game's "_upper" requests (Lookup mode 0). The portable one covers ASCII, Latin-1, Latin
// Extended-A, Greek and Cyrillic; the Windows host passes LCMapStringEx instead. Never called by the detour.
using UpperFn = std::string (*)(const std::string&);
std::string utf8_upper_basic(const std::string& s);

// ---------------------------------------------------------------- the table the detour reads (immutable once built)
struct Snapshot {
    struct Item {
        int64_t teamid = 0;
        std::string text[kKinds];
        std::string upper[kKinds];
    };
    std::vector<Item> items;  // sorted by teamid
    // The text for a match, upper-cased for mode 0; nullptr = not changed by Turbo. No allocation.
    const char* lookup(const KeyMatch& m, bool upper) const noexcept;
    const char* lookup(const char* key, int mode) const noexcept;  // match_key + lookup (mode 0 = upper)
    bool empty() const { return items.empty(); }
};
Snapshot build_snapshot(const Store& s, UpperFn upper = &utf8_upper_basic);

// The atomic pointer the detour reads. publish() keeps every table it ever published until the slot is destroyed, so a
// detour still reading an older one never reads freed memory. Readers never lock.
class SnapshotSlot {
public:
    const Snapshot* current() const noexcept { return cur_.load(std::memory_order_acquire); }
    const Snapshot* publish(Snapshot s);  // returns the new table
    size_t kept() const;
private:
    std::atomic<const Snapshot*> cur_{nullptr};
    mutable std::mutex m_;
    std::vector<std::unique_ptr<const Snapshot>> all_;
};

// ---------------------------------------------------------------- counters (atomics; the Status tab)
struct Stats {
    std::atomic<uint64_t> calls{0};      // Lookup calls seen while live
    std::atomic<uint64_t> team_keys{0};  // of them, TeamName keys
    std::atomic<uint64_t> given{0};      // names Turbo gave the game
};
struct StatsSnapshot {
    uint64_t calls = 0, team_keys = 0, given = 0;
};
StatsSnapshot snapshot(const Stats& s);

// ---------------------------------------------------------------- cached switch (turbo_output\team_names_hook_off.txt)
std::filesystem::path hook_off_path(const std::filesystem::path& out_dir);

// ---------------------------------------------------------------- the host service the App talks to
// win/teamname_override_win.cpp implements it; tests use a fake. publish() builds the table (with the host's upper
// case) and swaps it in: the next lookup shows it.
class Service {
public:
    virtual ~Service() = default;
    virtual bool available() const = 0;         // the hook is installed and live (no kill switch)
    virtual std::string why_off() const = 0;    // "" when available, else the reason in words
    virtual void publish(const Store& s) = 0;
    virtual StatsSnapshot stats() const = 0;
    virtual void refresh_switches() = 0;        // GUI tick, every 2 s: kill-switch files -> cached atomics
};

}  // namespace tnames
}  // namespace turbo
