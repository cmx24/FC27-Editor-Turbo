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
#include "core/callname_voice.h"
#include "core/callnames.h"
#include "core/commentary_audio.h"
#include "core/edit_unlock_hook.h"
#include "core/legacy.h"
#include "core/match_setup.h"
#include "core/mem.h"
#include "core/model.h"
#include "core/player_capture.h"
#include "core/reapply.h"
#include "core/sigscan.h"
#include "core/standings_refresh.h"
#include "core/t3db.h"
#include "core/teamname_override.h"
#include "nlohmann/json.hpp"
#include "textures.h"

namespace turbo {

namespace voice {
class Service;  // core/callname_voice.h
}
class Preloader;  // ui/preload.h (background loading)

constexpr const char* kGuiVersion = "1.1.3";

// UI scale (window height and the user's "UI size" setting): every fixed size in the panels goes through S()
extern float g_ui_scale;
inline float S(float px) { return px * g_ui_scale; }
// Range of the user's UI size factor (Turbo Tools slider, Ctrl + mouse wheel: ui_zoom.h)
constexpr float kUiScaleMin = 0.6f, kUiScaleMax = 2.5f;
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
    CallnamePlayer callname_player;  // the Callname tab's play buttons (core/callname_audio.h); the host gives the WavPlayer
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
    int toggle_mods = 0;   // with Ctrl / Alt / Shift (core/hotkey.h kHotkey*; gui_settings.json gui.toggle_mods)
    bool hotkey_capture = false;  // Status tab waits for the new show/hide key: the host does not toggle meanwhile
    int hotkey_capture_frame = -1;  // ImGui frame the setting was last drawn in (another tab shown: the wait ends)
    // ---- background loading of what the screens show (ui/preload.h): starts at the first show (F8) and when a career
    // connects; pictures are asked from Turbo's Lua side and decoded on a worker thread, callnames are read
    std::unique_ptr<Preloader> preloader;
    void preload_start(const char* why);
    std::string preload_line() const;     // "Loading pictures: 812 of 4120" ("" when nothing is loading)
    bool lua_images_wanted() const;       // pictures are waiting and Turbo is shown: the host nudges Lua (synthetic event)
    float ui_scale_user = 1.0f;     // "UI size" setting (gui_settings.json gui.ui_scale), 0.6 .. 2.5
    float ui_scale_applied = 0.0f;  // scale the style was last built for
    float ui_scale_changed_from = 0.0f;  // previous scale when it changed this frame (main window follows)
    // Ctrl + mouse wheel zoom (ui_zoom.cpp). The Windows host polls Ctrl itself (the game may send no key messages) and
    // hands a Ctrl + wheel over here instead of to Dear ImGui (no scrolling); tests use io.KeyCtrl + io.MouseWheel
    struct {
        float wheel = 0.0f;   // notches from the host since the last frame (positive = bigger)
        bool reset = false;   // the host saw Ctrl + 0
        double save_at = 0.0; // gui_settings.json is written then (once the wheel rests); 0 = nothing to save
        bool by_host = false; // the host feeds wheel / reset: Dear ImGui's own Ctrl state is not used (can be stale in game)
    } zoom;
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
    LuaActionQueue lua_queue;  // "keep shown name" actions waiting for the mailbox (flush_lua_queue, every tick)
    void flush_lua_queue();
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
    // ---- edits FC 27 forgets at every career load (core/reapply.h, ui_reapply.cpp): the kit colours of Teams > Colours
    // and the player-specific callnames of Players > Callname are kept in turbo_output\reapply_edits.json and written
    // again the first time Turbo connects to a newly loaded career. Kill switch: turbo_output\reapply_off.txt
    ReapplyStore reapply;
    std::string reapply_status;  // summary of the last re-apply ("" = none ran this session)
    std::string reapply_error;   // the store could not be read or written ("" = fine)
    // Keep what write_colour just wrote to a teamkits row (only the kit colour channels; other tables are kept by the
    // game's save)
    void remember_kit_colour(const Table& t, uint64_t rec, const std::string& prefix, const uint8_t rgb[3]);
    // Keep a player-specific callname the Callname tab just wrote (or queued); `from` = whose callname it is. A player
    // with his own recording (callnames.own_recording: the master list or the game's audio service) is never kept, and
    // an older entry of his is dropped: false, with the reason in *why
    bool remember_player_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from,
                                  std::string* why = nullptr);
    // The Forget buttons; false = nothing was kept
    bool forget_kit_edit(int64_t teamtechid, int64_t kittype, int64_t teamkitid);
    bool forget_player_callname(int64_t playerid);
    // Write every kept entry into the connected database: kit fields in place (Database::set, range-checked, no undo
    // step), callnames through write_player_callname (never adding a row). Not written, without an error: a player
    // with his own recording, a player or kit not in this career. Returns the summary line, which also goes to the GUI
    // log, turbo_gui.log (log_hook) and reapply_status. Runs by itself from refresh() once per newly loaded career.
    std::string reapply_stored_edits();
    // A newly loaded career is reported (another Lua session or load_gen than the last re-apply) and entries are kept:
    // tick then connects at once even while the window is hidden, so the edits are back before the first match
    bool reapply_due() const;
    // ---- voice swaps (core/callname_voice.h, Players > Callname): in matches only, a player is called with another
    // player's own recording, or his own recording is turned off and a generic callname used. Nothing is written to the
    // database or the save. The store (turbo_output\callnames\voice_swaps.json, all careers) is loaded at start-up and
    // published to the host's hooks then and after every edit; the GUI tick refreshes the kill switches every 2 s.
    voice::Service* voice_service = nullptr;  // the host's (win/callname_voice_win.cpp); nullptr = off; tests set a fake
    voice::VoiceStore voice_store;
    std::string voice_error;                  // the store's last load / save note ("" = fine)
    bool voice_available() const { return voice_service && voice_service->available(); }
    std::string voice_why_off() const;        // "" when available
    // ---- game editors' in-memory fallback (core/edit_unlock_hook.h, Tools > Game editors): the host's post-hook on the
    // editor config loader; nullptr = no game hooks (tests). The switches live in gui_settings "edit_unlock" (eu::Options).
    turbo::edit_unlock::HookService* edit_unlock_hook = nullptr;
    // Add or replace the player's entry (stamped now), save, publish. false (voice_error set) when not saved; the swap
    // is published for this session anyway
    bool voice_upsert(voice::Entry e);
    bool voice_forget(int64_t playerid);      // false = no entry
    bool voice_forget_all();
    void voice_publish();                     // build_table(voice_store) to the service (nothing without one)
    std::string voice_status_line() const;    // "Voice swaps: on | 3 swaps | lines changed 57 | kick-off set 2"
    // ---- live team names (core/teamname_override.h, Teams > Name): the names saved there are given to the game by the
    // host's hook at once (no restart). The store (turbo_output\team_names.json, all careers) is loaded at start-up and
    // published to the service then and after every save; the GUI tick refreshes the kill switches every 2 s.
    tnames::Service* team_names_service = nullptr;  // the host's (win/teamname_override_win.cpp); nullptr = off; tests set a fake
    tnames::Store team_names;
    std::string team_names_error;                   // the store's last load / save note ("" = fine)
    bool team_names_live() const { return team_names_service && team_names_service->available(); }
    std::string team_names_why_off() const;         // "" when live
    void team_names_publish();                      // the store to the service (nothing without one)
    // Keep a club's names (stamped now), publish them, save the store. false (*err) when the file was not written; the
    // names are published for this session anyway
    bool team_names_keep(tnames::Entry e, std::string* err);
    std::string team_names_status_line() const;     // "Live team names: on | 2 renamed clubs | names given 57"
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
    bool legacy_repaired_ = false;  // repair_dds_files ran (first tick)
    void load_voice();               // constructor: turbo_output\callnames\voice_swaps.json
    bool save_voice();
    const voice::Service* voice_published_to_ = nullptr;  // the service the store was last published to
    double voice_next_switches_ = 0.0;                    // next refresh_switches() (every 2 s)
    bool voice_unreadable_ = false;  // the store could not be read at start-up: never overwritten
    void load_team_names();          // constructor: turbo_output\team_names.json
    const tnames::Service* team_names_published_to_ = nullptr;  // the service the store was last published to
    double team_names_next_switches_ = 0.0;                     // next refresh_switches() (every 2 s)
    bool team_names_unreadable_ = false;  // the store could not be read at start-up: never overwritten
    void load_reapply();             // constructor: turbo_output\reapply_edits.json
    bool save_reapply();             // after every change of the store; false (reapply_error set) when not written
    void maybe_reapply();            // refresh(): re-apply once per newly loaded career
    std::string reapply_key() const;  // "<Lua session>#<load_gen>" of the career Lua reports (db_gen from an older Lua)
    bool reapply_unreadable_ = false;  // the file on disk could not be read: set it aside at the next save
    std::string reapplied_key_;        // reapply_key() of the career the kept edits were last written to
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
// The live view's one-line status of the last table edit ("Torino FC: W 2 -> 3, Pts 0 -> 3 (applied)", a refusal, ...)
std::string live_standings_status();
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
// What the Callname tab drew in its last frame (plain text is not an ImGui item, so the tests read it here)
struct CallnameTabState {
    int64_t playerid = 0;       // the player shown
    int own = 0;                // kOwnFromGame | kOwnFromMasters: he has his own recording (core/callnames.h)
    std::string current_line;   // "Current callname: ..."
    std::string rule_line;      // with an own recording: what the callname rule would give instead
    bool warning_shown = false; // the own-recording warning above the assignment buttons was drawn
    bool confirm_open = false;  // the "has his own recording: assign anyway?" popup is open
    std::string takeover_line;  // By player: whose playernamemap row a write takes over (the table is full), or why none can be
    std::string confirm_takeover;  // the same, in the confirmation popup
    std::string route_line;     // By name / All callnames: the route a generic callname takes (1.0.3), shown before the click
    std::string assign_note;    // the line next to "Assign callname": kept for every career load, or this session only
    std::string confirm_what;   // the confirmation popup's "Write it anyway: <what>?"
    // voice swaps (1.1.0)
    std::string voice_warning;        // "<A> has no recording in <lang>: silent" ("" = none)
    std::string voice_off_line;       // "Voice swaps are off: <why>" when drawn
    bool voice_turn_off_shown = false;  // All callnames: "Turn off his own recording first" (Use in matches greyed)
    bool voice_confirm_open = false;  // the "will be called ... in matches" popup is open
    std::string voice_confirm_line;   // its text
    int voice_rows = 0;               // Voice swaps list: rows drawn
    int voice_absent = 0;             // of them "not in this career"
};
const CallnameTabState& callname_tab_state();
// What giving a player a player-specific callname did (write_player_callname)
struct PlayerCallnameWrite {
    // NeedsRow: no row of his, the table has room, but the write was asked not to add one (the re-apply at career load)
    enum class How { Updated, Unchanged, Queued, TookOver, NeedsRow, Refused };
    How how = How::Refused;
    std::string message;    // for a toast (Callname tab) or the re-apply log
    bool notified = false;  // the reason was already shown as a toast (the command channel refused the Lua command)
    bool ok() const { return how != How::Refused && how != How::NeedsRow; }
};
// The player's playernamemap row gets `commentaryid`: edited in place when the row exists; else added by Turbo's Lua
// side when the table has room and `allow_insert` (queued on the mailbox; Lua counts the rows again before the insert);
// else a row from which no player hears a callname is taken over (Callnames::spare_playernamemap_row); else refused.
// Live Editor's InsertDBTableRow crashes the game on a full table (FC 27's playernamemap is full: 106 of 106 rows), so
// a full table never reaches it. Shared by the Callname tab (allow_insert) and the re-apply at career load, which
// never adds a row (its room check would be read while the game reloads the table).
PlayerCallnameWrite write_player_callname(App& app, const PlayerRow& p, int64_t commentaryid, const std::string& from, bool allow_insert);
// Kept edits in the panels (ui_reapply.cpp): the last re-apply summary and a store problem (Teams > Colours, Players >
// Callname); a kit row's kept colours with Forget (inside the kit's header); the player's kept callname with Forget
void reapply_status_line(App& app);
void reapply_kit_line(App& app, const Table& kt, uint64_t rec);
void reapply_callname_line(App& app, const PlayerRow& p);
// Slider over the field's whole range (attributes); a typed value outside it is refused by Database::set
bool slider_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);
// Combo with readable labels for an enumerated field (preferred foot, work rates, stars ...); false = no labels known
bool enum_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);
// Labels for enumerated fields: nullptr when the field is not enumerated; label for value v, or nullptr
const char* enum_label(const std::string& field, int64_t v);
bool is_enum_field(const std::string& field);

}  // namespace turbo
