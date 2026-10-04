// FC 27 LE Turbo GUI - manager rules (see manager_rules.h and docs/re/manager_rules.md)
#include "manager_rules.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace turbo {

const char* ManagerRulesFns::missing() const {
    if (!com_vtable) return "com_vtable";
    if (!jsm_vtable) return "jsm_vtable";
    if (!update_score) return "com_update_job_security";
    if (!sack_manager) return "jsm_sack_manager";
    return nullptr;
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

int job_security_level(int score, const JobSecurity& js) {
    if (!js.thresholds_ok) return -1;
    // the game's level function (0x147DF9A8C): >= safe -> 3, else the first band whose threshold is above the score
    if (score >= js.safe) return 3;
    if (score < js.insecure) return 0;
    if (score < js.okay) return 1;
    return 2;
}

const char* job_security_level_name(int level) {
    switch (level) {
        case 0: return "very insecure";
        case 1: return "insecure";
        case 2: return "okay";
        case 3: return "safe";
        default: return "unknown";
    }
}

int job_security_level_from_name(const std::string& name) {
    std::string l;
    for (char c : name) l += (c == '-' || c == ' ') ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "very_insecure") return 0;
    if (l == "insecure") return 1;
    if (l == "okay" || l == "ok") return 2;
    if (l == "safe") return 3;
    return -1;
}

static bool object_ok(Memory& mem, uint64_t obj, uint64_t vtable, uint64_t size, const char* what, std::string& err) {
    if (!is_ptr(obj, 8)) {
        err = std::string(what) + " address " + hex(obj) + " is not a pointer";
        return false;
    }
    uint64_t vt = 0;
    if (!mem.rd(obj, vt)) {
        err = std::string(what) + " at " + hex(obj) + " is not readable";
        return false;
    }
    if (vtable && vt != vtable) {
        err = "the object at " + hex(obj) + " is not the " + what + " (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
        return false;
    }
    std::vector<uint8_t> all;
    if (!mem.read_block(obj, static_cast<size_t>(size), all)) {
        err = std::string(what) + " at " + hex(obj) + " is cut off (" + hex(size) + " bytes expected)";
        return false;
    }
    uint64_t hub = mem.ptr(obj + 8);
    if (!hub || !mem.ptr(hub)) {
        err = std::string(what) + " at " + hex(obj) + " has no readable career hub (+0x8): is a career loaded?";
        return false;
    }
    return true;
}

bool validate_com(Memory& mem, uint64_t com, uint64_t vtable, int expect_team, std::string& err) {
    if (!object_ok(mem, com, vtable, com::kReadSize, "ClubObjectivesManager", err)) return false;
    uint64_t owner = 0;
    uint8_t manager_mode = 0;
    int32_t team = 0, score = 0;
    if (!mem.rd(com + com::kBlockOwner, owner) || !mem.rd(com + com::kIsManagerMode, manager_mode) || !mem.rd(com + com::kUserTeam, team) ||
        !mem.rd(com + com::kScore, score)) {
        err = "ClubObjectivesManager job-security block is not readable";
        return false;
    }
    if (owner != com) {
        err = "the ClubObjectivesManager's serialised block does not point back at it (" + hex(owner) + "): layout mismatch?";
        return false;
    }
    if (manager_mode != 1) {
        err = "the ClubObjectivesManager says this is not a Manager Career (mIsManagerMode " + std::to_string(manager_mode) + ")";
        return false;
    }
    if (team <= 0) {
        err = "the ClubObjectivesManager has no user team (mUserTeamId " + std::to_string(team) + ")";
        return false;
    }
    if (expect_team > 0 && team != expect_team) {
        err = "the ClubObjectivesManager belongs to team " + std::to_string(team) + ", not your club " + std::to_string(expect_team) +
              " (another career's object?)";
        return false;
    }
    if (score < com::kScoreMin || score > com::kScoreMax) {
        err = "job security score " + std::to_string(score) + " is outside 0..100 (layout mismatch?)";
        return false;
    }
    return true;
}

bool validate_jsm(Memory& mem, uint64_t jsm, uint64_t vtable, std::string& err) {
    if (!object_ok(mem, jsm, vtable, jsm::kSize, "JobSwitchManager", err)) return false;
    uint8_t pending = 0, sacked = 0;
    if (!mem.rd(jsm + jsm::kSackPending, pending) || !mem.rd(jsm + jsm::kSacked, sacked)) {
        err = "JobSwitchManager sack flags are not readable";
        return false;
    }
    if (pending > 1 || sacked > 1) {
        err = "JobSwitchManager sack flags hold " + std::to_string(pending) + "/" + std::to_string(sacked) + " (layout mismatch?)";
        return false;
    }
    return true;
}

bool read_job_security(Memory& mem, uint64_t com, JobSecurity& out, std::string& err) {
    uint8_t mm = 0;
    int32_t team = 0, addon = 0, psf = 0, prev = 0, score = 0;
    if (!mem.rd(com + com::kIsManagerMode, mm) || !mem.rd(com + com::kUserTeam, team) || !mem.rd(com + com::kAddon, addon) ||
        !mem.rd(com + com::kPrevSeasonFinal, psf) || !mem.rd(com + com::kPrevScore, prev) || !mem.rd(com + com::kScore, score)) {
        err = "ClubObjectivesManager job-security block is not readable";
        return false;
    }
    out = JobSecurity();
    out.manager_mode = mm == 1;
    out.user_team = team;
    out.addon = addon;
    out.prev_season_final = psf;
    out.prev_score = prev;
    out.score = score;
    int32_t vi = 0, ins = 0, ok = 0, safe = 0, fired = 0;
    if (mem.rd(com + com::kLvlVeryInsecure, vi) && mem.rd(com + com::kLvlInsecure, ins) && mem.rd(com + com::kLvlOkay, ok) &&
        mem.rd(com + com::kLvlSafe, safe) && mem.rd(com + com::kLvlFired, fired)) {
        out.very_insecure = vi;
        out.insecure = ins;
        out.okay = ok;
        out.safe = safe;
        out.fired = fired;
        // plausible: ascending inside the score range
        out.thresholds_ok = vi >= 0 && ins >= vi && ok >= ins && safe >= ok && safe > 0 && safe <= com::kScoreMax;
    }
    out.level = job_security_level(score, out);
    return true;
}

bool read_sack_flags(Memory& mem, uint64_t jsm_addr, SackFlags& out, std::string& err) {
    uint8_t p = 0, s = 0;
    if (!mem.rd(jsm_addr + jsm::kSackPending, p) || !mem.rd(jsm_addr + jsm::kSacked, s)) {
        err = "JobSwitchManager sack flags are not readable";
        return false;
    }
    out.pending = p != 0;
    out.sacked = s != 0;
    return true;
}

bool clear_sack_pending(Memory& mem, uint64_t jsm_addr, std::string& err) {
    const uint8_t zero = 0;
    if (!mem.wr(jsm_addr + jsm::kSackPending, zero)) {
        err = "cannot clear the sack-pending flag of " + hex(jsm_addr);
        return false;
    }
    return true;
}

bool level_plan(int level, const JobSecurity& js, bool& lock, int& value, std::string& err) {
    lock = false;
    value = 0;
    if (level == 3) {
        lock = true;
        value = com::kAddonLock;
        return true;
    }
    if (level == 0) {
        lock = true;
        value = -com::kAddonLock;
        return true;
    }
    if (level != 1 && level != 2) {
        err = "job security level must be 0..3 (very insecure, insecure, okay, safe)";
        return false;
    }
    if (!js.thresholds_ok) {
        err = "the game's job security bands are not readable (insecure " + std::to_string(js.insecure) + ", okay " + std::to_string(js.okay) +
              ", safe " + std::to_string(js.safe) + "): use safe, very insecure or a score";
        return false;
    }
    const int lo = level == 2 ? js.okay : js.insecure;
    const int hi = level == 2 ? js.safe : js.okay;
    if (hi <= lo) {
        err = std::string("the game's ") + job_security_level_name(level) + " band is empty (" + std::to_string(lo) + ".." + std::to_string(hi) + ")";
        return false;
    }
    value = lo + (hi - lo) / 2;
    return true;
}

static std::string js_text(const JobSecurity& js) {
    char b[300];
    if (js.thresholds_ok)
        std::snprintf(b, sizeof(b), "job security %d/100 (%s; addon %d; bands: insecure from %d, okay from %d, safe from %d)", js.score,
                      job_security_level_name(js.level), js.addon, js.insecure, js.okay, js.safe);
    else
        std::snprintf(b, sizeof(b), "job security %d/100 (addon %d; the game's bands read %d/%d/%d and were not trusted)", js.score, js.addon,
                      js.insecure, js.okay, js.safe);
    return b;
}

static ManagerRulesResult fail(const char* stage, const std::string& msg) {
    ManagerRulesResult r;
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

ManagerRulesResult job_security_call(Memory& mem, JobSecurityCaller& game, const ManagerRulesFns& fns, const ManagerRulesRequest& req) {
    if (const char* m = fns.missing()) return fail("validate", std::string("signature ") + m + " was not found on this game build");
    if (req.sub != kMrGet && req.sub != kMrSetLevel && req.sub != kMrSetScore && req.sub != kMrRestore)
        return fail("validate", "not a job security sub-op: " + std::to_string(req.sub));
    std::string err;
    if (!validate_com(mem, req.addr, fns.com_vtable, req.expect_team, err)) return fail("validate", err);
    JobSecurity before;
    if (!read_job_security(mem, req.addr, before, err)) return fail("validate", err);
    if (req.sub == kMrGet) {
        ManagerRulesResult r;
        r.ok = true;
        r.stage = "done";
        r.out0 = before.score;
        r.out1 = before.addon;
        r.message = js_text(before);
        return r;
    }
    // the addon the request asks for
    int32_t addon = 0;
    int target = -1;      // the score the request aims at (-1: none, restore)
    bool lock = false;
    if (req.sub == kMrSetLevel) {
        int v = 0;
        if (!level_plan(static_cast<int>(req.value), before, lock, v, err)) return fail("validate", err);
        if (lock) {
            addon = v;
            target = v > 0 ? com::kScoreMax : com::kScoreMin;
        } else {
            target = v;
        }
    } else if (req.sub == kMrSetScore) {
        if (req.value < com::kScoreMin || req.value > com::kScoreMax) return fail("validate", "job security score must be 0..100");
        target = static_cast<int>(req.value);
    }
    if (target >= 0 && !lock) {
        // the objectives part of the last update (score = clamp(part + addon)); exact unless that update was clamped
        const int part = before.score - before.addon;
        addon = static_cast<int32_t>(std::max(-com::kAddonLock, std::min(com::kAddonLock, target - part)));
    }
    if (!mem.wr(req.addr + com::kAddon, addon)) return fail("write", "cannot write the job security addon of " + hex(req.addr));
    int32_t check = 0;
    if (!mem.rd(req.addr + com::kAddon, check) || check != addon) return fail("write", "the job security addon did not take the value");
    if (!game.update_job_security(req.addr, err)) {
        mem.wr(req.addr + com::kAddon, static_cast<int32_t>(before.addon));  // back to what the game had
        return fail("call", "ClubObjectivesManager::UpdateJobSecurityScore failed: " + err + " (the addon was put back)");
    }
    JobSecurity after;
    if (!read_job_security(mem, req.addr, after, err)) return fail("check", err);
    ManagerRulesResult r;
    r.ok = true;
    r.stage = "done";
    r.out0 = after.score;
    r.out1 = after.addon;
    std::string what = req.sub == kMrRestore ? "the game's own score again"
                       : lock                ? std::string("locked ") + (addon > 0 ? "safe" : "very insecure")
                                             : "aimed at " + std::to_string(target);
    r.message = "job security " + std::to_string(before.score) + " -> " + std::to_string(after.score) + " (" + what + "): " + js_text(after);
    if (target >= 0 && after.score != target)
        r.message += "; the game's objectives part moved, so the score is " + std::to_string(after.score) + " instead of " + std::to_string(target) +
                     " (apply again to correct it)";
    return r;
}

ManagerRulesResult unsackable_call(Memory& mem, const ManagerRulesFns& fns, const ManagerRulesRequest& req, UnsackableState& st) {
    if (const char* m = fns.missing()) return fail("validate", std::string("signature ") + m + " was not found on this game build");
    if (req.sub != kMrUnsackable && req.sub != kMrFlags) return fail("validate", "not an unsackable sub-op: " + std::to_string(req.sub));
    std::string err;
    SackFlags f;
    bool have_flags = false;
    if (req.addr) {
        if (!validate_jsm(mem, req.addr, fns.jsm_vtable, err)) return fail("validate", err);
        if (!read_sack_flags(mem, req.addr, f, err)) return fail("validate", err);
        have_flags = true;
    }
    ManagerRulesResult r;
    r.ok = true;
    r.stage = "done";
    std::string note;
    if (req.sub == kMrUnsackable) {
        st.on = req.value != 0;
        if (have_flags && st.on && f.pending) {
            if (!clear_sack_pending(mem, req.addr, err)) return fail("write", err);
            f.pending = false;
            note = " (a pending sack was cancelled)";
        } else if (have_flags && f.sacked) {
            note = " (the game already marked you as sacked: that cannot be undone here)";
        } else if (!have_flags) {
            note = " (the JobSwitchManager is not known yet: the switch is armed anyway)";
        }
        r.message = std::string("unsackable ") + (st.on ? "on: the game's SackManager is refused" : "off: the game may sack you again") + note;
    } else {
        char b[200];
        std::snprintf(b, sizeof(b), "unsackable %s, sacks refused %lld, sack pending %s, sacked %s", st.on ? "on" : "off", st.refused,
                      have_flags ? (f.pending ? "yes" : "no") : "?", have_flags ? (f.sacked ? "yes" : "no") : "?");
        r.message = b;
    }
    r.out0 = st.on ? 1 : 0;
    r.out1 = (f.pending ? 1 : 0) | (f.sacked ? 2 : 0);
    return r;
}

bool sack_should_refuse(Memory& mem, const ManagerRulesFns& fns, uint64_t jsm_addr, UnsackableState& st, std::string& why) {
    if (!st.on) {
        ++st.passed;
        why = "unsackable is off: the game sacks you";
        return false;
    }
    ++st.refused;
    std::string err;
    if (validate_jsm(mem, jsm_addr, fns.jsm_vtable, err)) {
        if (clear_sack_pending(mem, jsm_addr, err))
            why = "refused (unsackable on); the pending sack was cleared";
        else
            why = "refused (unsackable on); " + err;
    } else {
        why = "refused (unsackable on); nothing written: " + err;
    }
    return true;
}

}  // namespace turbo
