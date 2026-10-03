// FC 27 LE Turbo GUI - application state and panels (Dear ImGui).
#pragma once
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/bridge.h"
#include "core/legacy.h"
#include "core/mem.h"
#include "core/model.h"
#include "core/t3db.h"
#include "nlohmann/json.hpp"
#include "textures.h"

namespace turbo {

constexpr const char* kGuiVersion = "0.3.0";

// UI scale (window height and the user's "UI size" setting): every fixed size in the panels goes through S()
extern float g_ui_scale;
inline float S(float px) { return px * g_ui_scale; }
// UI size = automatic scale (window height / 1080, at least 1) x the user's factor, rounded to 0.05
float auto_ui_scale(float display_height);

struct Toast {
    std::string text;
    bool error = false;
    double until = 0.0;
};

class App {
public:
    App(Memory& mem, std::filesystem::path le_root, uint64_t mailbox_addr, std::string session);

    // Call once per frame before draw() and before ImGui::NewFrame(); `now` in seconds
    void tick(double now);
    // Colours and sizes for the current UI scale (called by tick; needs an ImGui context)
    void update_style();
    // Draw the Turbo window (only when visible)
    void draw();

    // ---- services
    Memory& mem;
    Bridge bridge;
    Database db;
    Model model;
    std::unique_ptr<Mailbox> mailbox;
    std::string session;
    LegacyImages legacy;     // game pictures and custom minifaces (core/legacy.h)
    TextureCache textures;   // pictures shown in the window (textures.h)

    // ---- state
    bool visible = false;
    int toggle_vk = 0x77;  // F8
    float ui_scale_user = 1.0f;     // "UI size" setting (gui_settings.json gui.ui_scale), 0.6 .. 2.5
    float ui_scale_applied = 0.0f;  // scale the style was last built for
    float ui_scale_changed_from = 0.0f;  // previous scale when it changed this frame (main window follows)
    double now = 0.0;
    double next_poll = 0.0;
    double next_retry = 0.0;  // next automatic connection attempt while not connected
    double next_publish = 0.0;  // next refresh of the bridge_dll.json time stamp
    std::vector<int64_t> list_player_ids;  // players currently shown in the Players list (after filters)
    uint64_t mailbox_addr_ = 0;
    int gen = 0;  // bumps on every refresh/edit so cached views reload
    std::string db_error;
    long long seen_db_gen = -1;
    bool refresh_pending = false;  // database moved while the window was hidden
    bool model_stale = false;      // Lua changed the database: rebuild lists when shown
    uint64_t seen_service = 0;
    std::string pending_label;
    double pending_since = 0.0;
    std::deque<std::string> log_lines;
    std::vector<Toast> toasts;
    nlohmann::json gui_settings;
    std::shared_ptr<const NameMap> seen_names_;
    struct { float x = 0.0f, y = 0.0f; } main_window_size_;  // last size of the main window (follows UI size changes)
    int lua_heartbeat_last = 0;
    double lua_heartbeat_seen_at = -1.0;

    // ---- selections / UI state
    int64_t sel_player = 0;
    int64_t sel_team = 0;
    int sel_manager = -1;
    std::string db_table;
    int request_tab = -1;  // 0 players, 1 teams, 2 managers, 3 competitions, 4 database, 5 tools, 6 status

    // ---- helpers
    GameDate today() const;
    void notify(const std::string& text, bool error = false);
    void log(const std::string& text);
    bool refresh();  // re-walk the database and rebuild lists
    bool connected() const { return db.ready() && model.built(); }
    bool lua_alive() const;

    // Write one field through validation; updates cached lists for players/teams
    bool edit(const Table& t, uint64_t rec, const Field& f, const Value& v);

    // Ask Turbo's Lua side to run something (executes on the next career-mode event)
    bool send(const nlohmann::json& cmd, const std::string& label);
    bool busy();

    void load_gui_settings();
    bool save_gui_settings();
};

// panels
void draw_players(App& app);
void draw_teams(App& app);
void draw_managers(App& app);
void draw_competitions(App& app);
// Competitions tab: points (3 / 1 / 0) and played games from wins / draws / losses; with positions also the table order
bool recalc_league(App& app, int64_t league, bool positions, std::string* msg);
void draw_database(App& app);
void draw_tools(App& app);
void draw_status(App& app);

// Name of a show/hide key ("F8"); "?" for keys the Status tab does not offer
const char* key_name(int vk);

// widgets (widgets.cpp)
std::string field_label(const std::string& field);
// One labelled editor for a field. Returns true when a new value was written.
bool field_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label = nullptr, float width = 110.0f);
// Grid of editors for the listed fields that exist in the table
void field_grid(App& app, const Table& t, uint64_t rec, const std::vector<std::string>& names, const char* id, int columns = 3);
// What to do while the GUI is not connected to the game database (wrapped, dimmed)
void not_connected_hint();
// Every field of a record, with a filter box
void all_fields(App& app, const Table& t, uint64_t rec, const char* id);
// Date editor for gregorian-day fields (birthdate, playerjointeamdate)
bool date_field_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label);

}  // namespace turbo
