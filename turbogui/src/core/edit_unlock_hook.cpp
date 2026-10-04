// FC 27 LE Turbo GUI - the in-memory fallback for the game editors' unlock (see edit_unlock_hook.h)
#include "edit_unlock_hook.h"

#include <cstdio>

namespace turbo {
namespace edit_unlock {

int on_config_loaded(uint8_t* out, const char* path, size_t path_len, const Gate& gate, const KeepIds& ids,
                     ReadableFn readable, HookCounters& counters) {
    const int ci = context_index(path, path_len);
    if (ci < 0) return -1;  // not an editor Turbo unlocks: `out` is not even looked at
    ++counters.loads;
    const Settings& s = gate.settings;
    if (!gate.live || gate.kill_switch || !s.enabled || !s.hook || !s.context_on(ci)) {
        ++counters.skipped_off;
        return -1;
    }
    const WalkResult r = unlock_parsed_config(out, context_at(ci), s.experimental, ids, readable, true);
    if (!r.ok) {
        ++counters.refused;
        counters.last_error.store(r.error);
        return -1;
    }
    ++counters.configs;
    counters.fields += r.unlocked;
    counters.kept += r.kept;
    counters.last_context.store(ci);
    return ci;
}

std::vector<std::string> status_lines(const StatusInput& in, const HookCounters& c) {
    std::vector<std::string> out;
    char line[512];
    const unsigned long long configs = c.configs.load(), fields = c.fields.load();
    if (!in.off_reason.empty()) {
        std::snprintf(line, sizeof(line), "Game editors hook: off (%s)", in.off_reason.c_str());
        out.push_back(line);
        return out;
    }
    const char* state = in.kill_switch                                   ? "off (turbo_output\\edit_unlock_hook_off.txt present)"
                        : (!in.settings.enabled || !in.settings.hook)    ? "off (switched off in Tools > Game editors)"
                                                                         : "on";
    std::snprintf(line, sizeof(line), "Game editors hook: %s | configs patched %llu | fields unlocked %llu", state, configs, fields);
    out.push_back(line);
    const int last = c.last_context.load();
    const char* err = c.last_error.load();
    std::snprintf(line, sizeof(line), "  editor configs loaded %llu | last patched %s | kept %llu | left alone (off) %llu | refused %llu%s%s",
                  static_cast<unsigned long long>(c.loads.load()), last >= 0 ? context_at(last).name : "-",
                  static_cast<unsigned long long>(c.kept.load()), static_cast<unsigned long long>(c.skipped_off.load()),
                  static_cast<unsigned long long>(c.refused.load()), (err && *err) ? ": " : "", (err && *err) ? err : "");
    out.push_back(line);
    return out;
}

}  // namespace edit_unlock
}  // namespace turbo
