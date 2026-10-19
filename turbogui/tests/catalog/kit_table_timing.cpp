// 2.19 kit-table-cache: offline timing of the game-start path of Turbo's kit swap (core/kit_swap.cpp Runtime::start) on a SCRATCH LE root
// (a copy of turbo_output\kits: swap_state.json, the staged kits, kit_index.cache), with the game's real Oodle and tocs (read only).
//   run 1 (cold): no kit_table.cache -> the full build, which saves the cache
//   run 2 (warm): a new Runtime -> the table from the cache
// Prints both wall times, the table sizes and whether items / verdicts / blocks are the same. Nothing outside the scratch root is written.
// Usage: kit_table_timing <scratch LE root> <FC 27 folder> [turbo version] [game build]
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
#include "kit_swap.h"

using namespace turbo::kitswap;
namespace fs = std::filesystem;

// Runtime::wait_idle gives up after 30 s; a cold build takes minutes: poll until the worker published a table and finished (state not building)
static Status wait_ready(Runtime& rt, double limit_s) {
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        Status s = rt.status();
        if (s.state != "building" && (s.ready_at_ms || s.state != "ready")) {
            rt.wait_idle();  // the cache save after the publication
            return rt.status();
        }
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > limit_s) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

static int run(const std::string& le, const std::string& game, const OodleFn& oodle, const std::string& ver, const std::string& build, const char* name, Status* out) {
    Runtime rt(le, game, oodle);
    rt.require_opt_in = false;
    rt.turbo_version = ver;
    rt.game_build = build;
    rt.log_line = [](const std::string& l) { std::printf("  log: %s\n", l.c_str()); };
    const auto t0 = std::chrono::steady_clock::now();
    rt.start();
    // the table goes out before the kit index is read: time to publication = ready_after_ms
    const Status s = wait_ready(rt, 1200);
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int swapped = 0, left = 0;
    for (const auto& f : s.folders) (f.swapped ? swapped : left)++;
    std::printf("%s: state %s, table %s, published %.0f ms after start (table step %.0f ms), worker done after %.1f s\n", name, s.state.c_str(), s.table_cached ? "FROM CACHE" : "BUILT",
                s.ready_after_ms, s.table_ms, total);
    std::printf("  %s\n  slots %zu, kit folders swapped %d, left %d, table %zu blocks, %.1f MB, items %zu\n  note: %s\n", s.message.c_str(), s.slots.size(), swapped, left, s.blocks,
                double(s.bytes) / 1048576.0, s.items.size(), s.cache_note.c_str());
    *out = s;
    return s.state == "ready" ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: kit_table_timing <scratch LE root> <FC 27 folder> [turbo version] [game build]\n");
        return 2;
    }
    const std::string le = argv[1], game = argv[2];
    const std::string ver = argc > 3 ? argv[3] : "2.18.0", build = argc > 4 ? argv[4] : "6AC07E31-2145C000";
    OodleFn oodle;
#ifdef _WIN32
    HMODULE m = LoadLibraryW((fs::path(game) / "oo2core_9_win64.dll").wstring().c_str());
    using Fn = int64_t (*)(const void*, int64_t, void*, int64_t, int, int, int, void*, int64_t, void*, void*, void*, int64_t, int);
    Fn fn = m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "OodleLZ_Decompress"))) : nullptr;
    if (fn)
        oodle = [fn](const uint8_t* s, size_t n, uint8_t* d, size_t dn) { return fn(s, int64_t(n), d, int64_t(dn), 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3) == int64_t(dn); };
#endif
    if (!oodle) {
        std::printf("no Oodle\n");
        return 2;
    }
    const fs::path cache = fs::path(le) / "turbo_output" / "kits" / "kit_table.cache";
    std::error_code ec;
    fs::remove(cache, ec);
    Status cold, warm;
    int rc = run(le, game, oodle, ver, build, "COLD (no cache)", &cold);
    std::printf("cache file: %s, %lld bytes\n", fs::exists(cache) ? "written" : "MISSING", fs::exists(cache) ? static_cast<long long>(fs::file_size(cache, ec)) : 0LL);
    rc |= run(le, game, oodle, ver, build, "WARM (cache)", &warm);
    bool same = cold.message == warm.message && cold.blocks == warm.blocks && cold.bytes == warm.bytes && cold.items.size() == warm.items.size() &&
                cold.folders.size() == warm.folders.size();
    for (size_t i = 0; same && i < cold.items.size(); ++i)
        same = cold.items[i].res == warm.items[i].res && cold.items[i].swap == warm.items[i].swap && cold.items[i].reason == warm.items[i].reason &&
               cold.items[i].table_item == warm.items[i].table_item;
    for (size_t i = 0; same && i < cold.folders.size(); ++i) same = cold.folders[i].kind == warm.folders[i].kind && cold.folders[i].swapped == warm.folders[i].swapped;
    std::printf("SAME RESULT (message, blocks, bytes, items, verdicts): %s\n", same ? "yes" : "NO");
    std::printf("TIMING cold %.1f s -> warm %.2f s (to the table's publication)\n", cold.ready_after_ms / 1000.0, warm.ready_after_ms / 1000.0);
    for (const auto& f : warm.folders)
        if (f.team == 100646 || f.team == 10)
            std::printf("  verdict team %d slot %d %s: %s%s\n", f.team, f.slot, f.kind.c_str(), f.swapped ? "swapped" : "LEFT ", f.swapped ? "" : (f.role + ": " + f.reason).c_str());
    return rc || !same || !warm.table_cached ? 1 : 0;
}
