// FC 27 LE Turbo GUI - bench probe (read-only). Logs your club's team sheet order around Team Management: the 52 player ids of your sheet
// in the game's CachedTeamSheetService (core/team_sheet.h, sheet element +0xC4: the starting XI, then the bench, then the reserves), read
// when Team Management opens, every second while it is open and for 15 s after it closed (a line only when the order changed), when it
// closes, when a save starts and once in the hub of each career load.
// Why: on 2026-10-17 Cassio's bench and reserves came back in another order right after he saved and left Team Management (the same as the
// EA forum report "FC27 career mode substitute bench order changes after exiting team management"); Turbo wrote nothing then. The probe
// answers where the order changes (the live sheet itself or only the screen), when (the Yes, the leaving, the autosave) and whether a squad
// player missing from the sheet comes with it. It never writes anything. Kill switch: turbo_output\bench_probe_off.txt.
// 2.15: the Keeper below (bench keep, opt-in) uses the same reads to put your saved order back; its write is tsheet's (apply_bench).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace turbo {
namespace bench {

constexpr int kIds = 52;  // tsheet::kSheetPidCount
using Ids = std::array<int32_t, kIds>;
using Namer = std::function<std::string(int32_t)>;

inline const char* off_file() { return "bench_probe_off.txt"; }
constexpr double kPollS = 1.0;          // a read a second while Team Management is open and after it closed
constexpr double kAfterCloseS = 15.0;   // how long after Team Management closed the probe keeps reading
constexpr double kRetryS = 5.0;         // a failed hub read of a career load is tried again this much later
constexpr int kMaxFailLogs = 3;         // read failures logged per career load
constexpr int kMaxMovesListed = 40;     // moves a diff names

// What the tick knows now (Lua's bridge state and the wall clock)
struct Input {
    bool career = false;     // a career is loaded (Lua's bridge there, in career mode, a user club)
    long long load_gen = -1;
    std::string phase;       // Lua's match phase: "hub" / "prematch" / "match" / "unknown" / ""
    bool team_mgmt = false;  // Lua saw ENTERING_TEAM_MANAGEMENT and no LEAVING_TEAM_MANAGEMENT since
    long long save_t = 0;    // os.time() of the last save start Lua saw (0 = none)
    double now = 0.0;        // wall seconds
};

enum class Kind { None, Load, Opened, Open, Closed, After, Final, Save };
// The read the tick makes now (kind None = no read)
struct Due {
    Kind kind = Kind::None;
    std::string label;  // "Team Management closed", "2 s after Team Management closed"
    bool always = false; // logged even when the order did not change
};

// 2.15: one of Team Management's working copies of your sheet as a read saw it (tsheet::TmCopy, svc+0x550 / svc+0xA60)
struct CopyView {
    bool live = false;     // Team Management's copy of your sheet is built (tsheet::TmCopy::live: your team, dirty 255, ...)
    uint64_t build = 0;    // its record vector (a new build allocates new records)
    Ids ids{};
    std::string label;     // "svc+0x550"
};

class Probe {
public:
    // 2.15: Team Management's working copies (BENCH_FINDINGS: built when it opens, in the game's own order): a line when one is built (its
    // order against your map sheet's), when it changes while built (a move on the screen) and when it goes away. Lua's team_mgmt flag is
    // blind on LE 27.1.5: the card's tick feeds Input::team_mgmt from the copy at svc+0x550 too. Also a line when your MAP sheet changes
    // (its ids or its dirty flag; map_dirty = the flag): Lua's save signal is blind too (BENCH_FINDINGS addendum), and a save clears the
    // map's dirty flag (1 -> 0 after the Yes's autosave, 2026-10-12 evidence 1014/1015), a commit changes its ids
    std::vector<std::string> on_copies(const Ids& map, int map_dirty, const std::vector<CopyView>& copies, const Namer& name);
    // Called every frame: what to read now
    Due step(const Input& in);
    // A good read: the log lines (empty = nothing to log). squad = your club's players (teamplayerlinks); meta = "sheet 3, dirty 1"
    std::vector<std::string> on_read(const Due& d, const Ids& ids, const std::vector<int32_t>& squad, const Namer& name,
                                     const std::string& meta = std::string());
    // A failed read: the log line ("" = not logged: more than kMaxFailLogs in this career load)
    std::string on_fail(const Due& d, const std::string& why);

private:
    long long gen_ = -2;
    bool tm_ = false;
    long long save_t_ = 0;
    double next_poll_ = 0.0, close_until_ = -1.0, opened_at_ = 0.0, closed_at_ = 0.0;
    bool load_done_ = false;
    double load_retry_ = 0.0, now_ = 0.0;
    int fails_ = 0;
    bool have_last_ = false, have_open_ = false, have_close_ = false;
    Ids last_{}, open_{}, close_{};
    std::vector<CopyView> copies_;  // the copies as the last read saw them (on_copies)
    bool have_map_ = false;         // on_copies: your map sheet as the last poll saw it
    Ids map_{};
    int map_dirty_ = -1;
};

// The player ids of the sheet in order (the -1 slots left out)
std::vector<int32_t> filled(const Ids& ids);
// "25 players: #0 Rico/228618, #1 ..." (#n = the index in the game's list)
std::string order_text(const Ids& ids, const Namer& name);
// "same order" / "4 of 25 places changed: Rico/228618 #12->#14, ..." (+ "joined" / "left" when the players differ)
std::string diff_text(const Ids& before, const Ids& after, const Namer& name);
// Squad players not on the sheet / sheet players not in the squad
std::vector<int32_t> missing(const std::vector<int32_t>& squad, const Ids& ids);
std::vector<int32_t> extra(const std::vector<int32_t>& squad, const Ids& ids);

// ------------------------------------------------------------------------------------------------ bench keep (2.15, opt-in)
// P214 (research_20261017\BENCH_FINDINGS.md, LEICESTER 2026-10-17 15:00-15:18): your club's MAP sheet keeps the bench / reserves order you
// saved; Team Management builds its own working copy of the sheet when it OPENS (tsheet::kTmCopyTarget, svc+0x550), in the game's own
// order, shows that and commits it with its Yes (and autosaves). So bench keep writes your saved order into that copy right after it is
// built (tsheet::apply_bench: one guarded game-thread job, read back, rolled back on a mismatch). Rules:
//  - a build: the copy is Team Management's copy of your sheet (tsheet::TmCopy::live: your team, dirty 255, ...) and was not before, or
//    its record vector changed (a new build allocates new records). The first read of a career load, or after bench keep is switched on,
//    handles a copy that is already your sheet's as a new build: the game builds both copies DURING the career load and the first Team
//    Management visit shows that copy without building it again (P_BENCHWB 16:20). Leaving Team Management tears the copy down, the next
//    visit builds a new one. Switched on while Team Management is open with your own unsaved moves: they are overwritten (said in the log);
//  - the copy is your saved players (the map's 52 values) with the same starting XI (ids 0..10) and empty places in another order: ONE
//    write for this build, queued at once while G2 for bench keep holds (the hub, no save started in the last kKeepSaveQuietS, the career
//    load: Team Management is open by design), given up kKeepDeadlineS after the build; dropped when the copy changes first (a move on the
//    screen), goes away, the career is reloaded or a switch turns bench keep off;
//  - Lua's team_mgmt flag is not used (LE 27.1.5 never sets it, BENCH_FINDINGS).
// Switches: opt-in turbo_output\bench_keep_on.txt; kill switches bench_keep_off.txt, tactics_off.txt (every tactics write) and
// call_team_sheet_off.txt (channel E's writes). Every decision is logged as "bench keep: ...".
inline const char* keep_on_file() { return "bench_keep_on.txt"; }
inline const char* keep_off_file() { return "bench_keep_off.txt"; }
constexpr int kStarters = 11;                // ids 0..10: the starting XI, never written
constexpr double kKeepDeadlineS = 20.0;      // a write not queued this long after the build is given up
constexpr long long kKeepSaveQuietS = 30;    // = tsheet::kSaveQuietSeconds (G2; a test checks they agree)
constexpr double kKeepRetryS = 1.0;          // a refused submit is tried again this much later (until the deadline)
constexpr double kKeepJobLostS = 15.0;       // a queued job not back after this long: given up (the service's own limit is 10 s)
constexpr double kCopyPollS = 0.5;           // the card's tick reads your sheet and the copies this often in the hub (probe or bench keep on)
// Lua's save signal is blind on LE 27.1.5 (BENCH_FINDINGS addendum): the write also waits until your map sheet (its 52 ids and its dirty
// flag) has not changed for this long - a commit changes its ids, a save clears its dirty flag. Counted from the first read too
constexpr double kMapQuietS = 5.0;

// "" when `now` is `saved` with the bench and reserves (ids 11..51) in another order: the same 52 values (players and empty places), the
// same ids 0..10, no player twice. Else why not: "same order", "the starting XI changed (#3 ...)", "other players on the sheet (...)", ...
std::string shuffle_of(const Ids& saved, const Ids& now);
// The places 11..51 whose id differs
int moved_places(const Ids& a, const Ids& b);

enum class KeepPhase { Off, Idle, Pending, Queued, Done };
struct KeepOut {
    std::vector<std::string> log;
    bool submit = false;  // queue the job now with these ids and this build
    Ids saved{}, before{};
    uint64_t build = 0;
};
class Keeper {
public:
    // Every read (a poll about twice a second in the hub). map = your map sheet's 52 ids (your saved order); copy = svc+0x550; on = opted in,
    // no kill switch, not off for the session; off_why = what holds it off ("" = nothing but the missing opt-in); may_write = G2 for bench
    // keep holds now (wait = why not); now = wall seconds
    KeepOut on_read(long long load_gen, const Ids& map, int map_dirty, const CopyView& copy, bool on, const std::string& off_why, bool may_write,
                    const std::string& wait, double now, const Namer& name);
    // The submit's outcome: queued, or refused (tried again every kKeepRetryS until the deadline; a new reason is logged once)
    void queued(double now);
    void not_queued(const std::string& why, std::vector<std::string>& log);
    // The job came back (its line is logged by the service)
    void on_result(bool written, const std::string& line);
    KeepPhase phase() const { return ph_; }
    // The card's line: "on: waiting for Team Management ..." / "on: put back ..." / "off ..."
    std::string state() const;

private:
    KeepPhase ph_ = KeepPhase::Off;
    long long gen_ = -2;
    bool primed_ = false, was_live_ = false;
    Ids map_seen_{};
    int map_dirty_seen_ = -2;
    double map_changed_at_ = 0.0;  // the last change of your map sheet (ids or dirty) the reads saw
    uint64_t build_ = 0;
    Ids saved_{}, expect_{};
    double built_at_ = 0.0, next_try_ = 0.0, queued_at_ = 0.0;
    std::string off_why_, last_refusal_, last_wait_, outcome_;
    int moved_ = 0;
};

}  // namespace bench
}  // namespace turbo
