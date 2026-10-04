// FC 27 LE Turbo GUI - application state and panels (Dear ImGui).
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "core/bridge.h"
#include "core/callnames.h"
#include "core/commentary_audio.h"
#include "core/legacy.h"
#include "core/match_setup.h"
#include "core/mem.h"
#include "core/model.h"
#include "core/player_capture.h"
#include "core/sigscan.h"
#include "core/standings_refresh.h"
#include "core/t3db.h"
#include "nlohmann/json.hpp"
#include "textures.h"

namespace turbo {

constexpr const char* kGuiVersion = "0.4.0";

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
    ~App();

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
    Callnames callnames;     // commentary language and spoken callnames (core/callnames.h)
    std::filesystem::path game_root;  // folder of FC27.exe (language packs); tests point it at a fake game folder
    // ---- undo of direct edits, per player (players table), newest last; at most kUndoSteps each
    struct UndoStep {
        std::string table;
        uint64_t rec = 0;
        std::string field;
        Value before;
    };
    static constexpr size_t kUndoSteps = 20;
    std::map<int64_t, std::deque<UndoStep>> undo_;
    size_t undo_count(int64_t playerid) const;
    // Undo the last edit of this player (players table or his teamplayerlinks rows). Returns false when nothing to undo
    bool undo(int64_t playerid);

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
    // TURBO_GUI_TEST_TEXTURES=1 (overlay tests): a small window whose picture is replaced every few frames, so the
    // renderer creates and destroys textures the way the picture panels do
    bool texture_test = false;
    std::function<void(const std::string&)> log_hook;  // writes to turbo_gui.log (set by the Windows host)
    std::function<HookReport()> hook_report;           // game-code hook status for the Status tab (Windows host)
    std::shared_ptr<capture::CaptureService> capture;   // miniface from the game's 3D model (Windows host; tests use a fake)
    // Standings refresh after a live table edit (core/standings_refresh.h; Windows host; tests use a fake; null = none)
    std::shared_ptr<svm::RefreshService> standings_refresh;
    std::string standings_refresh_status;  // last outcome shown in the Live standings view
    // Match setup: game variables and result fixing (core/match_setup.h; Windows host; tests use a fake; null = none)
    std::shared_ptr<msetup::Service> match_setup;
    std::string match_setup_status;  // last variable outcome shown in the Match setup view
    int texture_test_frames = 0;
    double lua_heartbeat_seen_at = -1.0;
    uint64_t game_base = 0;  // FC27.exe image base (set by the Windows host; 0 in tests = skip vtable checks)
    // ---- spoken-set capture from the loaded commentary bank (core/commentary_bank.h) on a background thread:
    // the host lists the game's readable private regions; tests give a synthetic list over the simulated memory
    std::function<std::vector<Region>()> regions_hook;
    bool start_bank_capture(bool automatic = false);  // false when one is running or no region lister is set
    std::unordered_set<int64_t> commentary_ids();     // commentaryid of every commentarynames row (else playernames' ids)
    bool bank_capture_running() const { return bank_running_.load(); }
    std::string bank_capture_status;  // last capture result (one line for the Callname tab)
    // ---- the spoken set asked from the game's audio service (core/commentary_audio.h): the default source. The host
    // gives the service (a build runs on the game thread, one batch per frame); tests give a fake one
    std::shared_ptr<caudio::Service> commentary_audio;
    bool start_spoken_build(bool automatic = false);  // every commentarynames / playernames / playernamemap id + every player
    std::string spoken_build_status;  // last build result (one line for the Callname tab)
    bool spoken_auto_tried = false;   // an automatic build was started once this session (by the watcher)
    // The bank is only bound on some screens (docs/callnames.md section 5.6): while the spoken set is not verified the
    // watcher probes the game every few seconds (a sample of ids through the same call) and starts the full build by
    // itself where the game answers; the result is cached for later sessions. The id list comes from the database when
    // it is connected, else from turbo_output\callnames\ids.json (written here while connected) or from Lua's
    // bridge_commentary.txt, so the build also works from the main menu's Create Player screen.
    caudio::SpokenWatch spoken_watch;
    bool start_spoken_probe();                               // a quiet probe request (the watcher's)
    bool spoken_ids(caudio::IdCache& out, std::string* why);  // the id list at hand (database > cache > Lua list); false = none
    std::string spoken_ids_source;                           // where the last list came from (for the UI)
    std::string chosen_commentary_language() const;          // gui_settings callnames.language ("" = auto)
    const std::string& spoken_watch_line() const { return spoken_watch.line(); }
    long long game_call_seen = -1;  // last game-call outcome shown as a toast (bridge_state.json game_call.seq; -1 = none yet)
    // Job offers section (Managers tab): the club picked and the last request label
    int64_t job_offer_team = 0;
    std::string job_offer_status;
    char job_offer_search[64] = "";
    // Managers > Manager rules (features/manager_rules.lua) and Manager market (features/manager_move.lua)
    std::string manager_rules_status;
    int manager_rules_score = 70;     // score typed for "Set score"
    std::string manager_move_status;
    int64_t manager_move_team = 0;    // club picked for "Move to the picked club"
    char manager_move_search[64] = "";

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

private:
    void finish_bank_capture();  // tick: take a finished capture, cache it, rebuild the pickers
    void finish_spoken_build();  // tick: take a finished audio-service build, cache it, rebuild the pickers
    void spoken_watch_tick();    // tick: run the watcher, start the probe / build it asks for
    bool write_id_cache(const caudio::IdCache& c);  // turbo_output\callnames\ids.json (once per session and list size)
    size_t id_cache_written_ = 0;  // names.size() of the last cache written (0 = none this session)
    double spoken_ids_check_ = -1.0;  // next time the watcher re-checks that an id list is at hand
    bool spoken_have_ids_ = false;
    caudio::IdCache spoken_ids_last_;   // the list read last (a probe every few seconds must not re-read the tables)
    double spoken_ids_last_at_ = -1.0;  // when it was read (database: reused for 30 s of the same generation)
    int spoken_ids_last_gen_ = -1;
    std::string spoken_ids_last_file_;  // "" = the database; else "<file>@<write time>" (reused while unchanged)
    std::string time_stamp() const;  // "2026-10-04 00:40" (render thread)
    std::mutex bank_m_;
    std::thread bank_thread_;
    std::atomic<bool> bank_running_{false};
    std::atomic<bool> bank_cancel_{false};
    bool bank_done_ = false;
    BankCapture bank_result_;
};

// panels
void draw_players(App& app);
void draw_teams(App& app);
// "Transfer bans" section for a club (what = "team") or a player ("player"): ui_players.cpp; Teams tab > Overview and
// the player editor's Contract & Clubs tab
void transfer_ban_section(App& app, const char* what, int64_t id);
void draw_managers(App& app);
void draw_competitions(App& app);
// Competitions tab: points (3 / 1 / 0) and played games from wins / draws / losses; with positions also the table order
bool recalc_league(App& app, int64_t league, bool positions, std::string* msg);
// Competitions tab, "Live standings" view: the game engine's own table rows (ui_standings.cpp)
void draw_live_standings(App& app);
// The live view's lines about the game's standings view (what the tests read back): what the game's Standings screen
// reads (or why it could not be read), and the warning when the selected group is not one it shows ("" = none)
std::string live_standings_view_line();
std::string live_standings_view_warning();
// Competitions tab, "Match setup" view: the user's next fixtures (venue, opponent, fixed result) and the gameplay switches
// (game variables) for the next matches (ui_match.cpp)
void draw_match_setup(App& app);
void draw_database(App& app);
void draw_tools(App& app);
void draw_status(App& app);
// Game-code hook status (part of the Status tab; App::hook_report supplies the data)
void draw_hook_status(App& app);

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
// Players > Callname tab: the spoken name for the loaded commentary language, pickers and assignment (ui_callnames.cpp)
void callname_editor(App& app, const Table& t, const PlayerRow& p);
// Slider over the field's whole range (attributes); a typed value outside it is refused by Database::set
bool slider_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);
// Combo with readable labels for an enumerated field (preferred foot, work rates, stars ...); false = no labels known
bool enum_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);
// Labels for enumerated fields: nullptr when the field is not enumerated; label for value v, or nullptr
const char* enum_label(const std::string& field, int64_t v);
bool is_enum_field(const std::string& field);

}  // namespace turbo
