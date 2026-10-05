// FC 27 LE Turbo GUI - "transfer_list" game call inside FC27.exe (see transfer_list_win.h, core/transfer_list.h,
// docs/re/transfer_lists.md)
#include "transfer_list_win.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "game_calls_win.h"
#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

tl::Fns g_fns;
bool g_installed = false;
std::string g_off;  // why the call is off at install time ("" = resolved)
std::mutex g_mutex;
std::string g_last;  // last outcome (Status tab)
std::atomic<long long> g_runs{0}, g_queued{0}, g_ok{0};

std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

fs::path call_off_path() { return le_root() / "turbo_output" / "call_transfer_list_off.txt"; }

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// ---------------------------------------------------------------- the real calls (game thread only)
struct RealCaller : tl::Caller {
    using AddFn = void (*)(void*, int);
    using RemoveFn = uint8_t (*)(void*, int, uint8_t);

    bool add_transfer(uint64_t helper, int player, std::string& err) override {
        if (!g_fns.add_transfer) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<AddFn>(static_cast<uintptr_t>(g_fns.add_transfer))(reinterpret_cast<void*>(helper), player);
        return true;
    }
    bool add_loan(uint64_t helper, int player, std::string& err) override {
        if (!g_fns.add_loan) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<AddFn>(static_cast<uintptr_t>(g_fns.add_loan))(reinterpret_cast<void*>(helper), player);
        return true;
    }
    bool try_remove(uint64_t helper, int player, bool loan_list, bool& removed, std::string& err) override {
        if (!g_fns.try_remove) {
            err = "function not resolved";
            return false;
        }
        removed = (reinterpret_cast<RemoveFn>(static_cast<uintptr_t>(g_fns.try_remove))(reinterpret_cast<void*>(helper), player,
                                                                                         loan_list ? 1 : 0) & 1) != 0;
        return true;
    }
    // UserActionsHandlingHelperImpl::ToggleTransferBlock(helper, pid): void (Helper*, int), vtable slot 30
    bool toggle_block(uint64_t helper, int player, std::string& err) override {
        if (!g_fns.toggle_block) {
            err = "function not resolved";
            return false;
        }
        reinterpret_cast<AddFn>(static_cast<uintptr_t>(g_fns.toggle_block))(reinterpret_cast<void*>(helper), player);
        return true;
    }
};

tl::Result run_now(tl::Request req) {
    tl::Result r;
    std::string why;
    if (!transfer_list_ready(&why)) {
        r.stage = "off";
        r.message = why;
    } else {
        if (!req.image_base) req.image_base = game_image_base();
        if (!req.image_size) req.image_size = game_image_size();
        ProcessMemory mem;
        RealCaller caller;
        r = tl::run(mem, caller, g_fns, req);
    }
    ++g_runs;
    if (r.ok) ++g_ok;
    // list actions: before / after = the contract status; block actions: the block state 0 / 1 (the contract status is in the message)
    log("game call transfer_list(%s, player %d, club %d, comm %s): %s [%s] %s (%s %d -> %d, helper %s, user team %d)", tl::action_name(req.action),
        req.player, req.club, hex(req.comm).c_str(), r.ok ? "ok" : "failed", r.stage.c_str(), r.message.c_str(),
        tl::is_block_action(req.action) ? "block state" : "status", r.before, r.after, hex(r.at.helper).c_str(), r.at.user_team);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = std::string(r.ok ? "ok: " : "failed: ") + r.message;
    }
    return r;
}

void publish(int32_t seq, const tl::Result& r, int32_t status) {
    if (seq == 0) return;
    publish_call_result(seq, status, r.before, r.after, r.message);
}

}  // namespace

// ---------------------------------------------------------------- public API
tl::Fns transfer_list_fns() { return g_fns; }

bool transfer_list_ready(std::string* why) {
    if (!g_installed) {
        if (why) *why = "game calls are not installed";
        return false;
    }
    if (!game_hooks_allowed()) {
        if (why) *why = "game hooks are off for this game build (Status tab > Game hooks)";
        return false;
    }
    if (!g_off.empty()) {
        if (why) *why = g_off;
        return false;
    }
    if (file_exists(call_off_path())) {
        if (why) *why = "kill switch turbo_output\\call_transfer_list_off.txt is present";
        return false;
    }
    return true;
}

std::vector<std::string> transfer_list_status() {
    std::vector<std::string> out;
    std::string why;
    char line[640];
    if (transfer_list_ready(&why))
        std::snprintf(line, sizeof(line),
                      "transfer_list: ready | add_transfer %s, add_loan %s, try_remove %s | vtables helper %s, dao %s, tm %s, pcm %s, um %s | "
                      "runs %lld (ok %lld, queued %lld)",
                      hex(g_fns.add_transfer).c_str(), hex(g_fns.add_loan).c_str(), hex(g_fns.try_remove).c_str(), hex(g_fns.helper_vtable).c_str(),
                      hex(g_fns.dao_vtable).c_str(), hex(g_fns.tm_vtable).c_str(), hex(g_fns.pcm_vtable).c_str(), hex(g_fns.um_vtable).c_str(),
                      g_runs.load(), g_ok.load(), g_queued.load());
    else
        std::snprintf(line, sizeof(line), "transfer_list: off (%s)", why.c_str());
    out.push_back(line);
    if (why.empty()) {
        // Block Offers has its own signatures: the list actions run without them
        if (const char* m = g_fns.missing_block(true))
            out.push_back(std::string("  block offers: off (signature ") + m + " was not found on this game build)");
        else {
            std::snprintf(line, sizeof(line), "  block offers: ready | toggle %s | vtables block cache %s, block dao %s", hex(g_fns.toggle_block).c_str(),
                          hex(g_fns.cachedblock_vtable).c_str(), hex(g_fns.blockdao_vtable).c_str());
            out.push_back(line);
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_last.empty()) out.push_back("  last: " + g_last);
    return out;
}

tl::Result transfer_list_request(tl::Request req, int32_t seq) {
    tl::Result r;
    std::string why;
    if (!transfer_list_ready(&why)) {
        r.stage = "off";
        r.message = why;
        publish(seq, r, kCallFailed);
        return r;
    }
    if (req.player <= 0 || !tl::valid_action(req.action)) {
        r.stage = "validate";
        r.message = req.player <= 0 ? "player id must be a positive number" : "unknown transfer-list action " + std::to_string(req.action);
        publish(seq, r, kCallFailed);
        return r;
    }
    const uint32_t tid = GetCurrentThreadId();
    const uint32_t game = game_thread_id();
    if (game != 0 && tid == game) {
        r = run_now(req);
        publish(seq, r, r.ok ? kCallOk : kCallFailed);
        return r;
    }
    ++g_queued;
    const char* via = run_on_game_thread([req, seq]() {
        tl::Result rr = run_now(req);
        publish(seq, rr, rr.ok ? kCallOk : kCallFailed);
    });
    r.stage = "queued";
    r.message = std::string("queued for the game thread: it runs ") +
                (std::string(via) == "hook" ? "at the next game tick"
                 : std::string(via) == "lua" ? "on the next career-mode event (advance a day)"
                                            : "when the game-thread dispatcher is available");
    publish(seq, r, kCallQueued);
    log("game call transfer_list(%s, player %d) queued from thread %lu (game thread %lu, via %s)", tl::action_name(req.action), req.player,
        static_cast<unsigned long>(tid), static_cast<unsigned long>(game), via && *via ? via : "none yet");
    return r;
}

void install_transfer_list() {
    if (g_installed) return;
    g_installed = true;
    if (!game_hooks_allowed()) {
        g_off = "game hooks are off for this game build";
        log("game calls: transfer_list off (%s)", g_off.c_str());
        return;
    }
    g_fns.add_transfer = game_signature("uah_add_transfer_list");
    g_fns.add_loan = game_signature("uah_add_loan_list");
    g_fns.try_remove = game_signature("uah_try_remove_from_list");
    g_fns.helper_vtable = game_signature("uah_vtable");
    g_fns.dao_vtable = game_signature("dao_vtable");
    g_fns.tm_vtable = game_signature("tm_vtable");
    g_fns.pcm_vtable = game_signature("pcm_vtable");
    g_fns.um_vtable = game_signature("um_vtable");
    if (const char* m = g_fns.missing()) {
        g_off = std::string("signature ") + m + " was not found on this game build";
        log("game calls: transfer_list off (%s)", g_off.c_str());
        return;
    }
    // Block Offers: its own signatures; when one is missing only the block actions are refused (tl::locate says which), the list
    // actions keep working
    g_fns.toggle_block = game_signature("uah_toggle_transfer_block");
    g_fns.cachedblock_vtable = game_signature("cachedblock_vtable");
    g_fns.blockdao_vtable = game_signature("blockdao_vtable");
    if (const char* m = g_fns.missing_block(true))
        log("game calls: transfer_list block offers off (signature %s was not found on this game build; the list actions are unaffected)", m);
    else
        log("game calls: transfer_list block offers resolved (toggle %s, block cache vtable %s, block dao vtable %s)", hex(g_fns.toggle_block).c_str(),
            hex(g_fns.cachedblock_vtable).c_str(), hex(g_fns.blockdao_vtable).c_str());
    log("game calls: transfer_list resolved (add_transfer %s, add_loan %s, try_remove %s, helper vtable %s, dao vtable %s, tm vtable %s, "
        "pcm vtable %s, um vtable %s; kill switch turbo_output\\call_transfer_list_off.txt)",
        hex(g_fns.add_transfer).c_str(), hex(g_fns.add_loan).c_str(), hex(g_fns.try_remove).c_str(), hex(g_fns.helper_vtable).c_str(),
        hex(g_fns.dao_vtable).c_str(), hex(g_fns.tm_vtable).c_str(), hex(g_fns.pcm_vtable).c_str(), hex(g_fns.um_vtable).c_str());
}

}  // namespace host
