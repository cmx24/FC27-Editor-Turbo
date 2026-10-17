// FC 27 LE Turbo GUI - bench probe (read-only). Logs your club's team sheet order around Team Management: the 52 player ids of your sheet
// in the game's CachedTeamSheetService (core/team_sheet.h, sheet element +0xC4: the starting XI, then the bench, then the reserves), read
// when Team Management opens, every second while it is open and for 15 s after it closed (a line only when the order changed), when it
// closes, when a save starts and once in the hub of each career load.
// Why: on 2026-10-17 Cassio's bench and reserves came back in another order right after he saved and left Team Management (the same as the
// EA forum report "FC27 career mode substitute bench order changes after exiting team management"); Turbo wrote nothing then. The probe
// answers where the order changes (the live sheet itself or only the screen), when (the Yes, the leaving, the autosave) and whether a squad
// player missing from the sheet comes with it. It never writes anything. Kill switch: turbo_output\bench_probe_off.txt.
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

}  // namespace bench
}  // namespace turbo
