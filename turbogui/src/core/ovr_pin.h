// FC 27 LE Turbo X - the "Shown OVR" pin: the player's stored players.overallrating held at the value of core/ovr_mask.h.
//
// Why a pin in the stored overall (2026-11-01, tx-ovr2): the game's screens show the STORED overallrating, it does not work it out
// from the attributes when a screen draws or a career loads:
//   - turbo-reference.md (0.2.5, 10-02): "an overallrating edit in the Turbo window shows in the game's Team Management";
//   - FIX21_OVERALL_REPORT / core/overall.h: "FC 27 shows the stored overallrating until its own growth works it out again";
//   - OVR1_BISECT (10-11): the OVR-1 victims kept overallrating 73 with every attribute at 1..3 across a dozen saves and loads;
//   - OVRMOD1 desk parse (10-25): the 10-17 Squad Hub shot lists exactly the stored overalls of LEICESTER's modifier -5 players;
//   - 2.3.0 release notes: an archetype's written overall 80 showed as the Team Management baseline (the card adds form / morale).
// The game rewrites the field itself on its growth events (docs/re/development.md: "The game recomputes the overall itself on its
// own growth events"), so the pin is checked every few seconds and written again when the game has put its own value back.
// This is NOT a display-only mask: the game's own uses of the stored overall (value, wages, AI choices, Baseline OVR) see the pin.
//
// State per career: <LE>\turbo_output\careers\<career id>\ovr_pin_state.json
//     {"version":1,"players":{"<pid>":{"pinned":85,"game":72}}}
// "pinned" = what Turbo wrote, "game" = the value the field held before (the game's own overall, the last one Turbo saw that was not
// its pin): put back when the Shown OVR is cleared, while the field still holds the pin. The file outlives a save (a career saved
// with the pin loads with the pin; the game value is still known). Platform independent and header only (no build list change).
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/ovr_mask.h"
#include "nlohmann/json.hpp"

namespace turbo {
namespace ovr_pin {

constexpr double kPollSeconds = 5.0;  // how often the masked players' stored overall is read back (only theirs)

struct Entry {
    int pinned = 0;  // the value Turbo wrote
    int game = 0;    // the value the field held before Turbo's pin (put back on Clear)
    bool operator==(const Entry& o) const { return pinned == o.pinned && game == o.game; }
};
using State = std::map<int64_t, Entry>;

inline std::filesystem::path state_file(const std::filesystem::path& turbo_output, const std::string& career) {
    return turbo_output / "careers" / career / "ovr_pin_state.json";
}
// <LE>\turbo_output\ovr_pin_off.txt: no pin is written, the pinned players get their game value back (ovr_mask.json is kept)
inline std::filesystem::path kill_switch(const std::filesystem::path& turbo_output) { return turbo_output / "ovr_pin_off.txt"; }

inline bool load_state(const std::filesystem::path& f, State& out, std::string* err = nullptr) {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::exists(f, ec)) return true;
    std::ifstream in(f, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot read " + f.string();
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        const nlohmann::json j = nlohmann::json::parse(ss.str());
        if (!j.is_object() || j.value("version", 0) != 1 || !j.contains("players") || !j["players"].is_object())
            throw std::runtime_error("not the version 1 schema");
        for (auto it = j["players"].begin(); it != j["players"].end(); ++it) {
            const std::string& k = it.key();
            const auto& v = it.value();
            if (k.empty() || k.find_first_not_of("0123456789") != std::string::npos || !v.is_object() || !v.contains("pinned") ||
                !v.contains("game") || !v["pinned"].is_number_integer() || !v["game"].is_number_integer())
                throw std::runtime_error("bad entry '" + k + "'");
            out[std::stoll(k)] = Entry{v["pinned"].get<int>(), v["game"].get<int>()};
        }
    } catch (const std::exception& e) {
        out.clear();
        if (err) *err = std::string("ovr_pin_state.json unreadable: ") + e.what();
        return false;
    }
    return true;
}

// Writes the state (atomic, ovr_mask::write_atomic); an empty state removes the file
inline bool save_state(const std::filesystem::path& f, const State& s, std::string* err = nullptr) {
    std::error_code ec;
    if (s.empty()) {
        if (std::filesystem::exists(f, ec)) std::filesystem::remove(f, ec);
        if (ec && err) *err = "cannot remove " + f.string();
        return !ec;
    }
    nlohmann::json j;
    j["version"] = 1;
    j["players"] = nlohmann::json::object();
    for (const auto& kv : s) j["players"][std::to_string(kv.first)] = {{"pinned", kv.second.pinned}, {"game", kv.second.game}};
    return ovr_mask::write_atomic(f, j.dump() + "\n", err);
}

enum class Act {
    Pin,      // a new Shown OVR (or a changed one): game value -> shown value
    Repin,    // the field moved away from the pin (the game's growth, or another write): that value -> shown value again
    Restore,  // the Shown OVR was cleared (or the kill switch is on): pin -> the game value
};
inline const char* act_name(Act a) {
    switch (a) {
        case Act::Pin: return "pin";
        case Act::Repin: return "re-pin";
        default: return "put back";
    }
}
struct Step {
    int64_t pid = 0;
    Act act = Act::Pin;
    int from = 0, to = 0;
};

// One pass. `mask` = the career's ovr_mask.json (empty when the kill switch is on), `state` = what Turbo pinned (updated in place),
// `read(pid)` = the field's current value, < 0 when the player is not readable in this career. Returns the writes to make; the
// caller writes them and calls `failed()` for a write that did not happen so the next pass tries again.
//   masked, not pinned yet:       game = current; Pin when current != shown
//   masked, pinned:               current == shown -> nothing; current == the old pin (the shown value changed) -> Pin;
//                                 anything else (the game wrote its own value) -> game = current, Repin
//   pinned, no longer masked:     current == the pin -> Restore to game; the entry goes (also when the game moved it since: never
//                                 written over), and when the player is not readable
inline std::vector<Step> plan(const ovr_mask::Map& mask, State& state, const std::function<int(int64_t)>& read) {
    std::vector<Step> out;
    for (const auto& kv : mask) {
        const int64_t pid = kv.first;
        const int shown = kv.second;
        const int cur = read(pid);
        if (cur < 0) continue;  // not in this career's players (yet): nothing written, nothing recorded
        auto it = state.find(pid);
        if (it == state.end()) {
            state[pid] = Entry{shown, cur};
            if (cur != shown) out.push_back({pid, Act::Pin, cur, shown});
            continue;
        }
        Entry& e = it->second;
        if (cur == shown) {
            e.pinned = shown;
        } else if (cur == e.pinned) {
            e.pinned = shown;
            out.push_back({pid, Act::Pin, cur, shown});
        } else {
            e.game = cur;
            e.pinned = shown;
            out.push_back({pid, Act::Repin, cur, shown});
        }
    }
    for (auto it = state.begin(); it != state.end();) {
        if (mask.count(it->first)) {
            ++it;
            continue;
        }
        const int cur = read(it->first);
        if (cur >= 0 && cur == it->second.pinned && it->second.game != cur) out.push_back({it->first, Act::Restore, cur, it->second.game});
        it = state.erase(it);
    }
    return out;
}

// A write of plan() that did not happen: Restore keeps its entry (tried again next pass); Pin / Repin need nothing (the next pass
// sees the field still off the pin and writes again)
inline void failed(State& state, const Step& s) {
    if (s.act == Act::Restore) state[s.pid] = Entry{s.from, s.to};
}

// Turbo unloads: every pinned field that still holds the pin gets the game value back (the state file is left as it is, so a career
// saved with the pin keeps its game value for the next load)
inline std::vector<Step> release_all(const State& state, const std::function<int(int64_t)>& read) {
    std::vector<Step> out;
    for (const auto& kv : state) {
        const int cur = read(kv.first);
        if (cur >= 0 && cur == kv.second.pinned && kv.second.game != cur) out.push_back({kv.first, Act::Restore, cur, kv.second.game});
    }
    return out;
}

}  // namespace ovr_pin
}  // namespace turbo
