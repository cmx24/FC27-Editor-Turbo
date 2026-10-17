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

class Probe {
public:
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
// Puts your saved bench / reserves order back when the game reorders it right after Team Management (Cassio's test 2026-10-17 13:27: the
// Yes on leaving, then another order; EA forum report for FC 27). The Keeper decides, the tsheet service writes (core/team_sheet.h
// apply_bench: one guarded game-thread job, read back, rolled back on a mismatch). Rules:
//  - the saved order = the 52 ids read when a save starts while Team Management is open (Lua's save_t moved while team_mgmt), else the
//    ids read when it closes;
//  - after the close, a read within kAfterCloseS showing the SAME 52 values (players and empty places) with the same starting XI (ids
//    0..10) in another order arms ONE write-back for this visit: the saved order into ids 11..51 only (a permutation of what is there);
//  - the write-back is queued only once G2 holds (career hub, Team Management closed, no save started in the last kKeepSaveQuietS: the
//    Yes's own autosave counts), after a fresh read that still shows a shuffle of the saved order; given up kKeepDeadlineS after the
//    close, when Team Management opens again, the career is reloaded or left, or a switch turns it off;
//  - never written: no save seen in the visit and the order after the close is the one Team Management opened with (the game putting
//    back what a No discarded); a sheet whose players or starting XI changed.
// Switches: opt-in turbo_output\bench_keep_on.txt; kill switches bench_keep_off.txt, tactics_off.txt (every tactics write) and
// call_team_sheet_off.txt (channel E's writes). Every decision is logged as "bench keep: ...".
inline const char* keep_on_file() { return "bench_keep_on.txt"; }
inline const char* keep_off_file() { return "bench_keep_off.txt"; }
constexpr int kStarters = 11;                // ids 0..10: the starting XI, never written
constexpr double kKeepDeadlineS = 90.0;      // a write-back not queued this long after the close is given up
constexpr long long kKeepSaveQuietS = 30;    // = tsheet::kSaveQuietSeconds (G2; a test checks they agree)
constexpr double kKeepRetryS = 1.0;          // a refused submit is tried again this much later (until the deadline)
constexpr double kKeepJobLostS = 15.0;       // a queued job not back after this long: given up (the service's own limit is 10 s)
constexpr double kSaveSlackS = 2.0;          // a save Lua reports up to this long after the close still counts for the visit

// "" when `now` is `saved` with the bench and reserves (ids 11..51) in another order: the same 52 values (players and empty places), the
// same ids 0..10, no player twice. Else why not: "same order", "the starting XI changed (#3 ...)", "other players on the sheet (...)", ...
std::string shuffle_of(const Ids& saved, const Ids& now);
// The places 11..51 whose id differs
int moved_places(const Ids& a, const Ids& b);

enum class KeepPhase { Off, Idle, Open, Watch, Pending, Queued, Done };
struct KeepTick {
    bool submit = false;  // read the sheet now, then prepare() and queue the job
    std::vector<std::string> log;
};
class Keeper {
public:
    // Every tick, before the probe's read. on = opted in, no kill switch and the service's bench keep not off for the session; off_why =
    // what holds it off ("" = nothing but the missing opt-in); save_age = seconds since Lua saw a save start (-1 = none), as G2 counts it
    KeepTick step(const Input& in, bool on, const std::string& off_why, long long save_age);
    // Every read the probe scheduled (the same tick, after step): captures the saved order, spots the shuffle
    std::vector<std::string> on_read(const Due& d, const Ids& ids, const Namer& name);
    // The fresh read right before queuing: "" and the job's ids (saved = the order to write back, before = what the sheet holds now) when
    // the job may be queued; else why not (logged; the write-back is over for this visit)
    std::string prepare(const Ids& now, Ids& saved, Ids& before, std::vector<std::string>& log, const Namer& name);
    // The submit's outcome: queued, or refused (tried again every kKeepRetryS until the deadline; a new reason is logged once)
    void queued(double now);
    void not_queued(const std::string& why, std::vector<std::string>& log);
    // The job came back (its line is logged by the service)
    void on_result(bool written, const std::string& line);
    KeepPhase phase() const { return ph_; }
    // The card's line: "on: waiting for Team Management ..." / "put back ..." / "off ..."
    std::string state() const;

private:
    void reset(KeepPhase to);
    KeepPhase ph_ = KeepPhase::Off;
    long long gen_ = -2;
    bool tm_ = false;
    std::string off_why_;
    long long visit_save_t0_ = 0;       // Lua's save_t when Team Management opened
    bool saw_save_ = false;             // a save started during this visit (by the close)
    bool have_opened_ = false, have_saved_ = false, saved_at_save_ = false;
    Ids opened_{}, saved_{}, expect_{};
    double closed_at_ = 0.0, next_try_ = 0.0, queued_at_ = 0.0, now_ = 0.0;
    std::string last_refusal_, outcome_;
    int moved_ = 0;
};

}  // namespace bench
}  // namespace turbo
