// FC 27 LE Turbo GUI - Turbo X 2.0 shared UI state (themes, motion/GIF pause, navigation history, pinned favorites).
// Header-only so W-B (Shell/Hub/Themes), W-C (Pickers/AnimStore), and W-D (Players/Clubs) share one contract without build-script churn.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "ui_shell.h"

namespace turbo::x2 {

enum class ThemeMode : int {
    BroadcastDark = 0,     // >= 7:1 body text contrast, deep broadcast slate + mint-teal accent
    HighContrastAAA = 1,   // >= 10:1 AAA studio contrast, pure obsidian surfaces + crisp high-luminance borders/text
};

inline const char* theme_mode_name(ThemeMode m) {
    return m == ThemeMode::HighContrastAAA ? "high_contrast_aaa" : "broadcast_dark";
}

inline const char* theme_mode_label(ThemeMode m) {
    return m == ThemeMode::HighContrastAAA ? "High-contrast studio (AAA)" : "Broadcast dark";
}

inline ThemeMode theme_mode_from_name(const std::string& s) {
    if (s == "high_contrast_aaa" || s == "aaa" || s == "high_contrast") return ThemeMode::HighContrastAAA;
    return ThemeMode::BroadcastDark;
}

struct RuntimeState {
    ThemeMode theme = ThemeMode::BroadcastDark;
    bool reduce_motion = false;   // persisted in gui_settings.json gui.reduce_motion (0ms transitions + pauses animated GIFs)
    bool pause_gifs = false;      // persisted in gui_settings.json gui.pause_gifs (1-click Top Bar / Hub toggle)
    bool gifs_drawn_frame = false;
    bool gifs_drawn_last = false;
    bool squad_pitch_home = false;  // Home > My squad: false = Table, true = Pitch XI
    bool squad_pitch_club = false;  // Club > Squad: false = Table, true = Pitch XI
    std::vector<int64_t> fav_players;
    std::vector<int64_t> fav_clubs;

    // Browser-style Back / Forward stack (Place + entity context)
    struct NavEntry {
        shell::Place place;
        int64_t player = 0;
        int64_t team = 0;
        bool operator==(const NavEntry& o) const {
            return place == o.place && player == o.player && team == o.team;
        }
    };
    std::vector<NavEntry> history;
    int history_idx = -1;
    bool history_Applying = false;
};

inline RuntimeState& state() {
    static RuntimeState s;
    return s;
}

inline void reset_for_new_app() {
    RuntimeState& s = state();
    s.theme = ThemeMode::BroadcastDark;
    s.reduce_motion = false;
    s.pause_gifs = false;
    s.gifs_drawn_frame = false;
    s.gifs_drawn_last = false;
    s.squad_pitch_home = false;
    s.squad_pitch_club = false;
    s.fav_players.clear();
    s.fav_clubs.clear();
    s.history.clear();
    s.history_idx = -1;
    s.history_Applying = false;
}

inline ThemeMode theme_mode() { return state().theme; }
inline void set_theme_mode(ThemeMode m) { state().theme = m; }

inline bool reduce_motion() { return state().reduce_motion; }
inline void set_reduce_motion(bool on) { state().reduce_motion = on; }

inline bool pause_gifs() { return state().pause_gifs; }
inline void set_pause_gifs(bool on) { state().pause_gifs = on; }

inline bool gifs_paused() { return state().reduce_motion || state().pause_gifs; }

inline void begin_frame() {
    RuntimeState& s = state();
    s.gifs_drawn_last = s.gifs_drawn_frame;
    s.gifs_drawn_frame = false;
}
inline void note_gif_drawn() { state().gifs_drawn_frame = true; }
inline bool gifs_on_screen() { return state().gifs_drawn_last || state().gifs_drawn_frame; }

// ---- Favorites (players & clubs, capped at 24 each, preserved in gui_settings.json gui.favorites)
inline bool is_fav_player(int64_t pid) {
    const auto& v = state().fav_players;
    return pid > 0 && std::find(v.begin(), v.end(), pid) != v.end();
}
inline bool toggle_fav_player(int64_t pid) {
    if (pid <= 0) return false;
    auto& v = state().fav_players;
    auto it = std::find(v.begin(), v.end(), pid);
    if (it != v.end()) {
        v.erase(it);
        return false;
    }
    if (v.size() >= 24) v.erase(v.begin());
    v.push_back(pid);
    return true;
}

inline bool is_fav_club(int64_t tid) {
    const auto& v = state().fav_clubs;
    return tid > 0 && std::find(v.begin(), v.end(), tid) != v.end();
}
inline bool toggle_fav_club(int64_t tid) {
    if (tid <= 0) return false;
    auto& v = state().fav_clubs;
    auto it = std::find(v.begin(), v.end(), tid);
    if (it != v.end()) {
        v.erase(it);
        return false;
    }
    if (v.size() >= 24) v.erase(v.begin());
    v.push_back(tid);
    return true;
}

// ---- Navigation history
inline void history_record(const shell::Place& pl, int64_t player, int64_t team) {
    if (!pl.valid()) return;
    RuntimeState& s = state();
    if (s.history_Applying) return;
    const RuntimeState::NavEntry cur{pl, player, team};
    if (s.history_idx >= 0 && s.history_idx < static_cast<int>(s.history.size()) &&
        s.history[static_cast<size_t>(s.history_idx)].place == pl) {
        s.history[static_cast<size_t>(s.history_idx)].player = player;
        s.history[static_cast<size_t>(s.history_idx)].team = team;
        return;
    }
    if (s.history_idx + 1 < static_cast<int>(s.history.size()))
        s.history.erase(s.history.begin() + s.history_idx + 1, s.history.end());
    if (s.history.size() >= 32) s.history.erase(s.history.begin());
    s.history.push_back(cur);
    s.history_idx = static_cast<int>(s.history.size()) - 1;
}

inline bool can_history_back() { return state().history_idx > 0; }
inline bool can_history_forward() {
    const RuntimeState& s = state();
    return s.history_idx >= 0 && s.history_idx + 1 < static_cast<int>(s.history.size());
}

// ---- JSON persistence into gui_settings.json["gui"]
inline void load_from_json(const nlohmann::json& root) {
    RuntimeState& s = state();
    if (!root.is_object()) return;
    auto g = root.find("gui");
    if (g == root.end() || !g->is_object()) return;
    if (auto it = g->find("theme"); it != g->end() && it->is_string()) s.theme = theme_mode_from_name(it->get<std::string>());
    if (auto it = g->find("reduce_motion"); it != g->end() && it->is_boolean()) s.reduce_motion = it->get<bool>();
    if (auto it = g->find("pause_gifs"); it != g->end() && it->is_boolean()) s.pause_gifs = it->get<bool>();
    if (auto it = g->find("squad_pitch_home"); it != g->end() && it->is_boolean()) s.squad_pitch_home = it->get<bool>();
    if (auto it = g->find("squad_pitch_club"); it != g->end() && it->is_boolean()) s.squad_pitch_club = it->get<bool>();
    if (auto fav = g->find("favorites"); fav != g->end() && fav->is_object()) {
        s.fav_players.clear();
        s.fav_clubs.clear();
        if (auto fp = fav->find("players"); fp != fav->end() && fp->is_array()) {
            for (const auto& x : *fp)
                if (x.is_number_integer() && x.get<int64_t>() > 0 && s.fav_players.size() < 24) s.fav_players.push_back(x.get<int64_t>());
        }
        if (auto fc = fav->find("clubs"); fc != fav->end() && fc->is_array()) {
            for (const auto& x : *fc)
                if (x.is_number_integer() && x.get<int64_t>() > 0 && s.fav_clubs.size() < 24) s.fav_clubs.push_back(x.get<int64_t>());
        }
    }
}

inline void save_to_json(nlohmann::json& root) {
    const RuntimeState& s = state();
    if (!root.is_object()) root = nlohmann::json::object();
    nlohmann::json& g = root["gui"];
    if (!g.is_object()) g = nlohmann::json::object();
    g["theme"] = theme_mode_name(s.theme);
    g["reduce_motion"] = s.reduce_motion;
    g["pause_gifs"] = s.pause_gifs;
    g["squad_pitch_home"] = s.squad_pitch_home;
    g["squad_pitch_club"] = s.squad_pitch_club;
    g["favorites"] = {{"players", s.fav_players}, {"clubs", s.fav_clubs}};
}

}  // namespace turbo::x2
