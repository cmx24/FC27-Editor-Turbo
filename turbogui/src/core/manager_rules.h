// FC 27 LE Turbo GUI - manager rules: job security and unsackable for the user's manager (platform-independent part;
// the Windows host src/win/manager_rules_win.cpp resolves the game functions, owns the hooks and runs the calls on the
// game thread). docs/re/manager_rules.md has the analysis behind every offset ([H] = read from the code of
// FC27.exe 1.0.140.64835, [M] = strong inference).
//
// What the game does:
//   * job security is a score 0..100 kept by the career's ClubObjectivesManager (Live Editor manager type 133).
//     ClubObjectivesManager::UpdateJobSecurityScore(this) (0x147E07E2C) does: previous = score; score =
//     clamp(objectives part + mJobSecurityScoreAddon, 0, 100); if the level of the new score differs from the level
//     of the previous one it posts career event 0xA7 (the board / job security notification) [H]. The level is
//     the score against the thresholds the manager copied from OBJECTIVES/JOB_SECURITY_* (0 very insecure, 1
//     insecure, 2 okay, 3 safe) [H]. The addon is serialised with the career; the game only resets it (to 0, with
//     the score fields back to 80) when the user takes a new job [H].
//   * so Turbo sets the addon and asks the game to recompute: the game's own update computes the score, the level
//     and posts the event. "Safe" / "very insecure" lock the score (addon +100 / -100: any objectives part clamps to
//     100 / 0); "okay" / "insecure" aim at the middle of the band (addon = target - the objectives part).
//   * the sack is JobSwitchManager::SackManager(this) (manager type 54, 0x147DDF900): it sets mWasSacked (+0x1E1)
//     and posts career event 0xAD, which starts the game's "you have been sacked" flow [H]. Two callers:
//     HandleEvent(DAY_PASSED) while mPendingSack (+0x1E0) is set, and the CM_Manager_Contract_Ended handler
//     0x147F57D14 [H]. Unsackable = Turbo's hook does not let SackManager run (and clears mPendingSack so the next
//     DAY_PASSED does not ask again). The hook is pass-through until the user switches unsackable on.
// Every read and write goes through turbo::Memory with bounds checks; nothing is allocated by Turbo.
#pragma once
#include <cstdint>
#include <string>

#include "mem.h"

namespace turbo {

// ClubObjectivesManager layout (docs/re/manager_rules.md section 2)
namespace com {
constexpr uint64_t kHub = 0x08;                 // CareerHub* (ctor: [rcx+8] = hub)                       [H]
constexpr uint64_t kSettings = 0x10;            // OBJECTIVES/* settings block (ctor passes this+0x10)     [H]
constexpr uint64_t kSetVeryInsecure = 0x38;     // OBJECTIVES/JOB_SECURITY_VERY_INSECURE (loader +0x28)    [H]
constexpr uint64_t kSetInsecure = 0x3C;         // OBJECTIVES/JOB_SECURITY_INSECURE                        [H]
constexpr uint64_t kSetOkay = 0x40;             // OBJECTIVES/JOB_SECURITY_OKAY                            [H]
constexpr uint64_t kSetSafe = 0x44;             // OBJECTIVES/JOB_SECURITY_SAFE                            [H]
constexpr uint64_t kSetFired = 0x4C;            // OBJECTIVES/JOB_SECURITY_FIRED_POINTS                    [H]
constexpr uint64_t kBlockOwner = 0x108;         // serialised block: +0 = the manager itself (ctor)        [H]
constexpr uint64_t kIsManagerMode = 0x110;      // mIsManagerMode (u8)                                     [H]
constexpr uint64_t kUserTeam = 0x114;           // mUserTeamId (i32)                                       [H]
constexpr uint64_t kAddon = 0x118;              // mJobSecurityScoreAddon (i32)                            [H]
constexpr uint64_t kPrevSeasonFinal = 0x11C;    // mJobSecurityScore.mPreviousSeasonFinalScore             [H]
constexpr uint64_t kPrevScore = 0x120;          // mJobSecurityScore.mPreviousScore                        [H]
constexpr uint64_t kScore = 0x124;              // mJobSecurityScore.mScore (0..100)                       [H]
// the thresholds the level function (vtable 0x14B019638 slot 6, this = +0x268) compares the score with; copied from
// the settings above by 0x147E06AAC [H]
constexpr uint64_t kLvlVeryInsecure = 0x278;
constexpr uint64_t kLvlInsecure = 0x27C;        // score <  this: very insecure
constexpr uint64_t kLvlOkay = 0x280;            // score <  this: insecure
constexpr uint64_t kLvlSafe = 0x284;            // score <  this: okay, else safe
constexpr uint64_t kLvlFired = 0x288;
constexpr uint64_t kReadSize = 0x2A0;           // bytes that must be readable
constexpr int kScoreMin = 0, kScoreMax = 100;   // the clamp in the score computation 0x147E5F664          [H]
constexpr int kAddonLock = 100;                 // |addon| that pins the score to 100 / 0 whatever the objectives say
}  // namespace com

// JobSwitchManager layout (docs/re/manager_rules.md section 3)
namespace jsm {
constexpr uint64_t kSize = 0x1E8;               // allocation size at the hub builder 0x147F18F41          [H]
constexpr uint64_t kHub = 0x08;                 // CareerHub*                                              [H]
constexpr uint64_t kLastSwitchDate = 0x1B8;     // mLastJobSwitchDate (serialised block starts here)       [H]
constexpr uint64_t kPreviousTeam = 0x1BC;       // mPreviousTeamId                                         [H]
constexpr uint64_t kSackPending = 0x1E0;        // mPendingSack (u8): DAY_PASSED calls SackManager when set [H]
constexpr uint64_t kSacked = 0x1E1;             // mWasSacked (u8): set by SackManager                     [H]
}  // namespace jsm

// Game objects / functions the rules need (resolved by signature on the running build; 0 = unknown)
struct ManagerRulesFns {
    uint64_t com_vtable = 0;       // ClubObjectivesManager vtable (the ctor's lea; ctor proven by the hub builder)
    uint64_t jsm_vtable = 0;       // JobSwitchManager vtable (idem)
    uint64_t update_score = 0;     // ClubObjectivesManager::UpdateJobSecurityScore(this) - called by Turbo
    uint64_t sack_manager = 0;     // JobSwitchManager::SackManager(this) - the guarded hook target
    // Name of the first required entry that is missing, nullptr when all are there
    const char* missing() const;
};

struct JobSecurity {
    bool manager_mode = false;
    int user_team = 0;
    int addon = 0, prev_season_final = 0, prev_score = 0, score = 0;
    // the thresholds the game's level function uses (-1 when not plausible)
    int very_insecure = -1, insecure = -1, okay = -1, safe = -1, fired = -1;
    bool thresholds_ok = false;
    int level = -1;                 // 0 very insecure .. 3 safe (the game's own rule), -1 unknown
};

// Level index of a score with the game's rule (score < insecure: 0, < okay: 1, < safe: 2, else 3); -1 when the
// thresholds are unknown
int job_security_level(int score, const JobSecurity& js);
const char* job_security_level_name(int level);   // "very insecure" / "insecure" / "okay" / "safe" / "unknown"
// Level index for a name ("safe", "okay", "insecure", "very insecure" / "very_insecure"), -1 = not a level
int job_security_level_from_name(const std::string& name);

// The pointer is a readable ClubObjectivesManager: vtable (when known), its serialised block points back at it,
// manager mode, a user team (expect_team > 0 must match), a score in 0..100, a readable hub. Nothing is written.
bool validate_com(Memory& mem, uint64_t com, uint64_t vtable, int expect_team, std::string& err);
// The pointer is a readable JobSwitchManager: vtable, size, both sack flags 0/1, a readable hub
bool validate_jsm(Memory& mem, uint64_t jsm, uint64_t vtable, std::string& err);

bool read_job_security(Memory& mem, uint64_t com, JobSecurity& out, std::string& err);

struct SackFlags {
    bool pending = false, sacked = false;
};
bool read_sack_flags(Memory& mem, uint64_t jsm, SackFlags& out, std::string& err);
// Clears mPendingSack (the DAY_PASSED path then does not call SackManager). Never touches mWasSacked.
bool clear_sack_pending(Memory& mem, uint64_t jsm, std::string& err);

// The game function Turbo calls (on the game thread, by the Windows host; a fake in the native tests)
class JobSecurityCaller {
public:
    virtual ~JobSecurityCaller() = default;
    virtual bool update_job_security(uint64_t com, std::string& err) = 0;
};

// ---------------------------------------------------------------- mailbox game call (core/game_calls.h op 4)
// args[0] = sub-op, args[1] = object address (ClubObjectivesManager / JobSwitchManager from Lua's checked walk;
// 0 = the pointer the capture hooks saw), args[2] = value, args[3] = expected user team (0 = skip the check).
constexpr int32_t kCallOpManagerRules = 4;
constexpr int64_t kMrGet = 1;           // out0 = score, out1 = addon, text = level + bands
constexpr int64_t kMrSetLevel = 2;      // value = level 0..3: safe / very insecure lock, okay / insecure aim at the band
constexpr int64_t kMrSetScore = 3;      // value = target score 0..100 (addon = target - objectives part)
constexpr int64_t kMrRestore = 4;       // addon 0: the game's own score again
constexpr int64_t kMrUnsackable = 5;    // value 1 on / 0 off; on clears a pending sack (address = JobSwitchManager)
constexpr int64_t kMrFlags = 6;         // out0 = unsackable, out1 = pending | sacked << 1 (address = JobSwitchManager)

struct ManagerRulesRequest {
    int64_t sub = 0;
    uint64_t addr = 0;
    int64_t value = 0;
    int expect_team = 0;
};

struct ManagerRulesResult {
    bool ok = false;
    std::string stage;    // "validate", "write", "call", "check", "done" (the host adds "off" / "queued")
    std::string message;  // for the user (toast / log / Lua)
    int64_t out0 = 0, out1 = 0;
};

// Unsackable switch state, shared by the call and the SackManager hook (the host keeps one instance)
struct UnsackableState {
    bool on = false;
    long long refused = 0;       // SackManager calls the hook did not let through
    long long passed = 0;        // SackManager calls that ran (switch off)
};

// The target score / addon a level asks for, given the game's thresholds: level 3 -> addon +100 (locked safe),
// 0 -> addon -100 (locked very insecure), 2 / 1 -> the middle of the okay / insecure band. Returns false (err) when
// the level is not 0..3 or the band is unknown.
bool level_plan(int level, const JobSecurity& js, bool& lock, int& addon_or_target, std::string& err);

// Job security sub-ops (kMrGet / kMrSetLevel / kMrSetScore / kMrRestore) on the ClubObjectivesManager `com`:
// validate, write the addon, call the game's UpdateJobSecurityScore, read back and report. The caller runs it on the
// game thread.
ManagerRulesResult job_security_call(Memory& mem, JobSecurityCaller& game, const ManagerRulesFns& fns, const ManagerRulesRequest& req);
// Unsackable / flags sub-ops on the JobSwitchManager `jsm` (0 allowed for kMrUnsackable: the switch is armed anyway)
ManagerRulesResult unsackable_call(Memory& mem, const ManagerRulesFns& fns, const ManagerRulesRequest& req, UnsackableState& st);
// The hook's decision for one SackManager(this) call: true = refuse (switch on; mPendingSack is cleared on a checked
// JobSwitchManager), false = let the game sack. `why` says what happened (for the log).
bool sack_should_refuse(Memory& mem, const ManagerRulesFns& fns, uint64_t jsm, UnsackableState& st, std::string& why);

}  // namespace turbo
