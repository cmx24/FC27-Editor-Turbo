// FC 27 LE Turbo GUI - gameplay tuning inside FC27.exe (see tuning_win.h, core/tuning.h)
#include "tuning_win.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <thread>

#include "game_hooks.h"
#include "host.h"

namespace host {

namespace fs = std::filesystem;
using namespace turbo;

namespace {

bool g_installed = false;
ProcessMemory g_mem;
std::shared_ptr<tuning::CoreService> g_service;

double now_us() {
    return double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The native locators: code for a table a pointer chain cannot reach (e.g. a hash map of the game's tuning collections). Each one is
// read-only, fast (it runs inside the game-thread job, re-chased at every write) and returns "" + the table's base address, or why not.
// None yet: the live locator (track T1) adds them here when a knobs file names a locator of kind "native".
std::map<std::string, tuning::NativeLocator> native_locators() { return {}; }

// Reads [start, end) in chunks of kChunk (+ `overlap` bytes so a match across a chunk border is seen); a chunk that cannot be read is
// tried again in 64 KB pieces (the unreadable pieces are skipped). fn(buf, n, at) sees every readable piece; false = stop.
// The search runs inside the game process, so its own read buffer (a copy of game memory, a region of its own: 1 MB is above the heap's
// large-block size) would show copies of the objects it looks for: a piece that overlaps the buffer is never handed to fn.
constexpr size_t kChunk = 1u << 20;
bool walk(uint64_t start, uint64_t end, size_t overlap, double deadline, const std::function<void(const uint8_t*, size_t, uint64_t)>& fn) {
    std::vector<uint8_t> buf;
    buf.reserve(kChunk + overlap + 0x10000);
    const uint64_t self_lo = uint64_t(reinterpret_cast<uintptr_t>(buf.data())), self_hi = self_lo + buf.capacity();
    auto own = [&](uint64_t a, size_t n) { return a < self_hi && a + n > self_lo; };
    for (uint64_t at = start; at < end; at += kChunk) {
        if (now_us() > deadline) return false;
        const size_t n = size_t(std::min<uint64_t>(kChunk + overlap, end - at));
        if (!own(at, n) && g_mem.read_block(at, n, buf)) {
            fn(buf.data(), n, at);
            continue;
        }
        for (uint64_t p = at; p < at + n && p < end; p += 0x10000) {
            const size_t m = size_t(std::min<uint64_t>(0x10000 + overlap, end - p));
            if (!own(p, m) && g_mem.read_block(p, m, buf)) fn(buf.data(), m, p);
        }
    }
    return true;
}

// tuning::Lookup::refs: for each name, the 8-aligned qwords that point at a NUL-terminated copy of it (letter case ignored), in FC27.exe's
// image or in the game's writable private memory. Two passes (the strings, then the pointers to them), at most 64 hits a name, 120 s.
std::map<std::string, std::vector<uint64_t>> name_refs(const std::vector<std::string>& names, bool in_image) {
    std::map<std::string, std::vector<uint64_t>> out;
    if (names.empty()) return out;
    const double t0 = now_us(), deadline = t0 + 120e6;
    std::vector<Region> regs;
    if (in_image) {
        if (game_image_base() && game_image_size()) regs.push_back({game_image_base(), game_image_base() + game_image_size()});
    } else {
        regs = private_regions(0);
    }
    size_t longest = 0;
    for (const std::string& n : names) longest = std::max(longest, n.size());
    std::map<uint64_t, size_t> strings;  // address -> name index
    std::map<size_t, int> per_name;
    bool done = true;
    for (const Region& r : regs) {
        done = walk(r.start, r.end, longest + 1, deadline, [&](const uint8_t* b, size_t n, uint64_t at) {
            for (const auto& h : tuning::find_cstrings(b, n, at, names, 4096))
                if (per_name[h.first] < 64 && !strings.count(h.second)) {
                    strings[h.second] = h.first;
                    ++per_name[h.first];
                }
        });
        if (!done) break;
    }
    std::set<uint64_t> values;
    for (const auto& kv : strings) values.insert(kv.first);
    for (const Region& r : regs) {
        if (!done || values.empty()) break;
        done = walk(r.start & ~uint64_t(7), r.end, 0, deadline, [&](const uint8_t* b, size_t n, uint64_t at) {
            for (const auto& h : tuning::find_qwords(b, n, at, values, 4096)) {
                auto& v = out[names[strings[h.first]]];
                if (v.size() < 64 && std::find(v.begin(), v.end(), h.second) == v.end()) v.push_back(h.second);
            }
        });
    }
    log("gameplay tuning: searched %s for %zu name(s): %zu string(s), %zu name(s) referenced%s (%.1f s)", in_image ? "FC27.exe" : "the heap", names.size(),
        strings.size(), out.size(), done ? "" : ", STOPPED at the time limit", (now_us() - t0) / 1e6);
    return out;
}

}  // namespace

std::shared_ptr<tuning::Service> tuning_service() { return g_service; }

void install_tuning() {
    if (g_installed) return;
    g_installed = true;
    tuning::Host h;
    h.mem = &g_mem;
    h.build = game_build_key();
    h.knobs_dir = le_root() / "turbo" / tuning::knobs_dir();
    h.out_dir = le_root() / "turbo_output";
    h.lookup.signature = [](const std::string& name) { return game_signature(name.c_str()); };
    h.lookup.scan = [](const Signature& s) { return game_scan_signature(s); };
    h.lookup.natives = native_locators();
    h.lookup.refs = &name_refs;
    h.lookup.image_base = game_image_base();
    h.lookup.image_size = game_image_size();
    // the prompt dispatcher only (the game_tick hook): a job that waited for the next career event could run after the hub was left
    h.run_on_game_thread = [](std::function<void()> fn) -> std::string {
        if (!game_hooks_allowed() || !game_thread_hooked()) return "";
        const char* via = run_on_game_thread(std::move(fn));
        return via ? via : "";
    };
    h.run_background = [](std::function<void()> fn) { std::thread(std::move(fn)).detach(); };
    h.log = [](const std::string& s) { log("%s", s.c_str()); };
    h.clock_us = &now_us;
    if (!game_hooks_allowed()) h.off = "game hooks are off for this game build";
    g_service = std::make_shared<tuning::CoreService>(std::move(h));  // logs what it loaded
}

std::vector<std::string> tuning_status() {
    std::vector<std::string> out;
    if (!g_service) {
        out.push_back("gameplay tuning: not installed");
        return out;
    }
    const std::string why = g_service->registry_status();
    if (!why.empty()) {
        out.push_back("gameplay tuning: off (" + why + ")");
        return out;
    }
    const tuning::Switches sw = tuning::read_switches(le_root() / "turbo_output");
    const std::string block = sw.blocking();
    std::string state = !block.empty() ? "off (kill switch turbo_output\\" + block + ")"
                        : !sw.opt_in   ? std::string("off until turbo_output\\") + tuning::opt_in_file() + " exists"
                                       : "on";
    const std::string sess = g_service->session_off();
    if (!sess.empty()) state = "off for this session (" + sess + ")";
    const auto reg = g_service->registry();
    out.push_back("gameplay tuning: " + state + " | " + std::to_string(reg->knobs.size()) + " knobs, " + std::to_string(reg->placeholders()) +
                  " not confirmed in the running game (never written) | " + g_service->anchors_status());
    return out;
}

}  // namespace host
