// FC 27 LE Turbo GUI - calls into the game's own career code (see game_calls.h and docs/re/job_offer.md)
#include "game_calls.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace turbo {

const char* JobMarketFns::missing() const {
    if (!vtable) return "jmm_vtable";
    if (!has_application) return "jmm_has_application";
    if (!apply_for_job) return "jmm_apply_for_job";
    if (!make_offer) return "jmm_make_offer";
    return nullptr;
}

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

// *(slot) must be a readable manager object (its first 8 bytes, the vtable pointer, readable)
static bool hub_slot_ok(Memory& mem, uint64_t hub, uint64_t slot, const char* what, std::string& err) {
    uint64_t mgr = mem.chain(hub, {slot, 0});
    if (!mgr || !mem.ptr(mgr)) {
        err = std::string("the career hub's ") + what + " (hub+" + hex(slot) + ") is not readable: is a career loaded?";
        return false;
    }
    return true;
}

bool validate_jmm(Memory& mem, uint64_t jmm, uint64_t vtable, std::string& err) {
    if (!is_ptr(jmm, 8)) {
        err = "JobMarketManager address " + hex(jmm) + " is not a pointer";
        return false;
    }
    uint64_t vt = 0;
    if (!mem.rd(jmm, vt)) {
        err = "JobMarketManager at " + hex(jmm) + " is not readable";
        return false;
    }
    if (vtable && vt != vtable) {
        err = "the object at " + hex(jmm) + " is not the JobMarketManager (vtable " + hex(vt) + ", expected " + hex(vtable) + ")";
        return false;
    }
    std::vector<uint8_t> tail;
    if (!mem.read_block(jmm + jmm::kSize - 16, 16, tail)) {
        err = "JobMarketManager at " + hex(jmm) + " is cut off (0xC90 bytes expected)";
        return false;
    }
    uint64_t hub = mem.ptr(jmm + jmm::kHub);
    if (!hub) {
        err = "JobMarketManager has no career hub (+0x8)";
        return false;
    }
    if (!hub_slot_ok(mem, hub, jmm::kHubCalendar, "calendar manager", err)) return false;
    if (!hub_slot_ok(mem, hub, jmm::kHubTeams, "teams manager", err)) return false;
    if (!hub_slot_ok(mem, hub, jmm::kHubDispatcher, "event dispatcher", err)) return false;
    if (!hub_slot_ok(mem, hub, jmm::kHubX198, "random source owner", err)) return false;
    if (!hub_slot_ok(mem, hub, jmm::kHubX6d8, "user settings object", err)) return false;
    {
        uint64_t listener = mem.chain(hub, {jmm::kHubXf38, 0});
        if (!listener || !mem.ptr(listener + 0x98)) {
            err = "the career hub's job-offer listener (hub+0xF38, +0x98) is not readable: is a career loaded?";
            return false;
        }
    }
    uint32_t count = 0;
    if (!mem.rd(jmm + jmm::kBucketCount, count) || count == 0 || count > jmm::kMaxBuckets) {
        err = "application table bucket count " + std::to_string(count) + " is out of range";
        return false;
    }
    uint64_t buckets = mem.ptr(jmm + jmm::kBuckets);
    uint64_t sentinel = 0;  // buckets[count] is EASTL's end sentinel (~0): readable, but not a pointer
    if (!buckets || !mem.rd(buckets + static_cast<uint64_t>(count) * 8, sentinel)) {
        err = "application table buckets are not readable";
        return false;
    }
    return true;
}

uint64_t find_application(Memory& mem, uint64_t jmm, int team, std::string& err) {
    err.clear();
    if (team <= 0) {
        err = "team id must be positive";
        return 0;
    }
    uint32_t count = 0;
    if (!mem.rd(jmm + jmm::kBucketCount, count) || count == 0 || count > jmm::kMaxBuckets) {
        err = "application table bucket count out of range";
        return 0;
    }
    uint64_t buckets = mem.ptr(jmm + jmm::kBuckets);
    if (!buckets) {
        err = "application table buckets are not readable";
        return 0;
    }
    // HasApplication: bucket = (int64)team % bucketCount (unsigned division), chain through +0x28 until null
    const uint64_t idx = static_cast<uint64_t>(static_cast<int64_t>(team)) % count;
    uint64_t node = 0;
    if (!mem.rd(buckets + idx * 8, node)) {
        err = "application bucket " + std::to_string(idx) + " is not readable";
        return 0;
    }
    for (int hop = 0; hop < jmm::kMaxChain; ++hop) {
        if (node == 0) return 0;
        if (!is_ptr(node, 4)) {
            err = "application chain holds a bad pointer " + hex(node);
            return 0;
        }
        int32_t key = 0;
        uint64_t next = 0;
        if (!mem.rd(node + jmm::kNodeKey, key) || !mem.rd(node + jmm::kNodeNext, next)) {
            err = "application node " + hex(node) + " is not readable";
            return 0;
        }
        if (key == team) return node;
        node = next;
    }
    err = "application chain longer than " + std::to_string(jmm::kMaxChain) + " nodes (layout mismatch?)";
    return 0;
}

static JobOfferResult fail(JobOfferResult r, const char* stage, const std::string& msg) {
    r.ok = false;
    r.stage = stage;
    r.message = msg;
    return r;
}

JobOfferResult job_offer_create(Memory& mem, GameCaller& call, const JobMarketFns& fns, const JobOfferRequest& req) {
    JobOfferResult r;
    std::string err;
    if (const char* m = fns.missing())
        return fail(r, "validate", std::string("game function ") + m + " is not resolved on this game build");
    if (req.team <= 0) return fail(r, "validate", "team id must be a positive number");
    if (!validate_jmm(mem, req.jmm, fns.vtable, err)) return fail(r, "validate", err);

    // 1. the application (the game's own code computes the wage and creates the node)
    bool has = false;
    if (!call.has_application(req.jmm, req.team, has, err)) return fail(r, "apply", "HasApplication: " + err);
    if (!has) {
        if (!call.apply_for_job(req.jmm, req.team, err)) return fail(r, "apply", "ApplyForJob: " + err);
        r.applied = true;
        has = false;
        if (!call.has_application(req.jmm, req.team, has, err)) return fail(r, "apply", "HasApplication: " + err);
        if (!has) return fail(r, "apply", "the game did not register an application for team " + std::to_string(req.team));
    }
    // 2. the node
    uint64_t node = find_application(mem, req.jmm, req.team, err);
    if (!node) return fail(r, "find", err.empty() ? "application node for team " + std::to_string(req.team) + " not found" : err);
    r.node = node;
    int32_t key = 0, vteam = 0, sent = 0, wage = 0;
    uint8_t accepted = 0;
    if (!mem.rd(node + jmm::kNodeKey, key) || !mem.rd(node + jmm::kNodeTeam, vteam) ||
        !mem.rd(node + jmm::kNodeOffer + jmm::kOfferSentDay, sent) || !mem.rd(node + jmm::kNodeWage, wage) ||
        !mem.rd(node + jmm::kNodeOffer + jmm::kOfferAccepted, accepted))
        return fail(r, "find", "application node " + hex(node) + " is not readable");
    if (key != req.team || vteam != req.team)
        return fail(r, "find", "application node " + hex(node) + " belongs to team " + std::to_string(vteam) + " (layout mismatch?)");
    r.wage = wage;
    if (sent != -1) {
        if (accepted)
            return fail(r, "find", "you already accepted an offer from this club (sent " + std::to_string(sent) + ")");
        return fail(r, "find", "this club already made you an offer on " + std::to_string(sent) + ": check the Job Offers screen");
    }
    // 3. what OnResponseDue writes before MakeOffer, plus no second answer from DAY_PASSED
    const int32_t zero = 0;
    const int32_t team = req.team;
    if (!mem.wr(node + jmm::kNodeOffer + jmm::kOfferTeam, team) || !mem.wr(node + jmm::kNodeOffer + jmm::kOfferWage, wage) ||
        !mem.wr(node + jmm::kNodeCountdown, zero))
        return fail(r, "write", "cannot write the offer fields of node " + hex(node));
    // 4. the game makes the offer
    if (!call.make_offer(req.jmm, node + jmm::kNodeOffer, err)) return fail(r, "offer", "MakeOffer: " + err);
    // 5. check
    int32_t after = 0;
    if (!mem.rd(node + jmm::kNodeOffer + jmm::kOfferSentDay, after)) return fail(r, "check", "cannot read the offer back");
    r.sent_day = after;
    if (after == -1) return fail(r, "check", "MakeOffer ran but the offer date stayed unset");
    int today = 0;
    std::string terr;
    char buf[160];
    if (call.today(req.jmm, today, terr) && today != after)
        std::snprintf(buf, sizeof(buf), "job offer sent (dated %d, but the game's calendar says %d)", after, today);
    else
        std::snprintf(buf, sizeof(buf), "job offer sent on %d (weekly wage %d)", after, wage);
    r.ok = true;
    r.stage = "done";
    r.message = buf;
    return r;
}

// ---------------------------------------------------------------- mailbox call block
bool read_call_block(Memory& mem, uint64_t mailbox, GameCallBlock& out) {
    if (!is_ptr(mailbox)) return false;
    if (!mem.rd(mailbox + kMbCallOp, out.op) || !mem.rd(mailbox + kMbCallStatus, out.status) ||
        !mem.rd(mailbox + kMbCallSeq, out.seq) || !mem.rd(mailbox + kMbCallResultSeq, out.result_seq))
        return false;
    for (int i = 0; i < 4; ++i)
        if (!mem.rd(mailbox + kMbCallArgs + static_cast<uint64_t>(i) * 8, out.args[i])) return false;
    for (int i = 0; i < 2; ++i)
        if (!mem.rd(mailbox + kMbCallOut + static_cast<uint64_t>(i) * 8, out.out[i])) return false;
    out.text = mem.read_cstr(mailbox + kMbCallText, kMbCallTextSize);
    return true;
}

bool write_call_result(Memory& mem, uint64_t mailbox, int32_t seq, int32_t status, int64_t out0, int64_t out1,
                       const std::string& text) {
    if (!is_ptr(mailbox)) return false;
    std::vector<uint8_t> buf(kMbCallTextSize, 0);
    std::memcpy(buf.data(), text.data(), text.size() < kMbCallTextSize - 1 ? text.size() : kMbCallTextSize - 1);
    if (!mem.write(mailbox + kMbCallText, buf.data(), buf.size())) return false;
    if (!mem.wr(mailbox + kMbCallOut, out0) || !mem.wr(mailbox + kMbCallOut + 8, out1)) return false;
    if (!mem.wr(mailbox + kMbCallResultSeq, seq)) return false;
    return mem.wr(mailbox + kMbCallStatus, status);
}

bool write_call_request(Memory& mem, uint64_t mailbox, int32_t seq, int32_t op, const int64_t args[4]) {
    if (!is_ptr(mailbox)) return false;
    const int32_t idle = kCallIdle;
    if (!mem.wr(mailbox + kMbCallStatus, idle)) return false;
    for (int i = 0; i < 4; ++i)
        if (!mem.wr(mailbox + kMbCallArgs + static_cast<uint64_t>(i) * 8, args[i])) return false;
    if (!mem.wr(mailbox + kMbCallOp, op)) return false;
    return mem.wr(mailbox + kMbCallSeq, seq);
}

}  // namespace turbo
