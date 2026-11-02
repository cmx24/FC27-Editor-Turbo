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
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "core/asset_catalog.h"
#include "core/bridge.h"
#include "core/callname_voice.h"
#include "core/callnames.h"
#include "core/career_faces.h"
#include "core/club_voice.h"
#include "core/commentary_audio.h"
#include "core/edit_unlock_hook.h"
#include "core/fce_standings.h"
#include "core/kit_swap.h"
#include "core/lua_loader.h"
#include "core/layout_store.h"
#include "core/legacy.h"
#include "core/match_setup.h"
#include "core/mem.h"
#include "core/model.h"
#include "core/opp_rules.h"
#include "core/player_capture.h"
#include "core/reapply.h"
#include "core/sigscan.h"
#include "core/standings_refresh.h"
#include "core/t3db.h"
#include "core/tactic_profiles.h"
#include "core/team_sheet.h"
#include "core/tuning.h"
#include "core/teamname_override.h"
#include "nlohmann/json.hpp"
#include "gui_prefs.h"
#include "ui_role_rules.h"
#include "ui_shell.h"
#include "textures.h"

namespace turbo {

namespace voice {
class Service;  // core/callname_voice.h
}
class Preloader;  // ui/preload.h (background loading)
namespace labels {
struct CodeUsers;  // core/field_labels.h
}

constexpr const char* kGuiVersion = "X 2.0.1";

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
    // ---- undo of direct edits, per player (players table, his editedplayernames row), newest last; at most kUndoSteps each
    struct UndoStep {
        std::string table;
        uint64_t rec = 0;
        std::string field;
        Value before;
        std::vector<std::pair<std::string, Value>> also;  // other fields of the same change, same record (a typed overall's attributes)
        // Pictures the same change wrote (a head given with its miniface): undo puts each back from its backup, or removes
        // Turbo's custom file when there was none before (backup empty). Never a head undone without its miniface.
        struct Picture {
            std::string path;                 // legacy path, e.g. data/ui/imgAssets/heads/p1234.dds
            std::filesystem::path backup;     // the custom file it replaced (turbo_output\miniface_backups\...); empty = none
            // 2.19 career faces: the career whose own copy (turbo_output\careers\<career>\...) was written; backup is then the
            // career's earlier copy (careers\<career>\_undo), empty = the career had none. "" = the global mods\legacy file.
            std::string career;
        };
        std::vector<Picture> pictures;
    };
    // Fold the last `n` undo steps of this player into one step (a multi-field change such as a head given through the
    // real-face chooser) and attach the pictures it wrote. Fewer steps than `n` (the deque was capped): folds what is there.
    void group_undo(int64_t playerid, size_t n, std::vector<UndoStep::Picture> pictures);
    uint64_t undo_pushes = 0;  // every undo step pushed (any player): a caller measures how many its change pushed
    // Never a mixed result (2026-10-17): a player whose head is ANOTHER existing player's real head (class 0, headassetid =
    // that player's id) while no custom miniface of his own exists still shows his old face (the game's p<playerid>).
    // Whatever wrote the head (field editors, a preset, a Lua feature, the Database window) the player editor sees it.
    // Returns that head id, or 0 when head and miniface go together (or the user chose to keep his picture).
    int64_t head_miniface_mismatch(int64_t playerid);
    std::map<int64_t, int64_t> head_miniface_kept;  // playerid -> head id for which "Keep his old picture" was chosen
    static constexpr size_t kUndoSteps = 20;
    std::map<int64_t, std::deque<UndoStep>> undo_;
    size_t undo_count(int64_t playerid) const;
    // Undo the last edit of this player (players table or his teamplayerlinks rows). Returns false when nothing to undo
    bool undo(int64_t playerid);
    // ---- "Undo bulk" (Players > Attributes): changes of many fields at once (Overall +1/-1, an archetype, an overall
    // typed in), newest last, at most kBulkUndo; names, not Field pointers (App::refresh rebuilds the tables)
    struct BulkUndo {
        int64_t playerid = 0;
        std::string label;
        std::vector<std::pair<std::string, int64_t>> before;  // field name, value before
    };
    static constexpr size_t kBulkUndo = 30;
    std::vector<BulkUndo> bulk_undo;
    void push_bulk_undo(BulkUndo u);

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
    std::vector<std::string> preload_status() const;  // Status tab: one line per kind of picture (ui/preload.h)
    GuiPrefs prefs;                                   // gui.preload_images, gui.row_density (ui/gui_prefs.h)
    float row_height_applied = 0.0f;                  // the Row height (gui_prefs.h) the style was last built for
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
    double now = 0.0;  // seconds of the game's clock (QueryPerformanceCounter: scaled by Live Editor's speedhack, see clock_speed)
    // how fast the game's QueryPerformanceCounter runs against real time (host: Live Editor's speedhack, 3 in menus by default, 50
    // after its numpad 9 "Menu Speed +"); steady_clock durations measured here are divided by it
    double clock_speed = 1.0;
    double next_poll = 0.0;
    double next_retry = 0.0;  // next automatic connection attempt while not connected
    double next_publish = 0.0;  // next refresh of the bridge_dll.json time stamp
    std::vector<int64_t> list_player_ids;  // players currently shown in the Players list (after filters)
    uint64_t mailbox_addr_ = 0;
    int gen = 0;  // bumps on every refresh/edit so cached views reload
    std::string db_error;
    long long seen_db_gen = -1;
    bool refresh_pending = false;  // database moved while the window was hidden
    double refresh_due = 0.0;      // a full refresh at this time (after a create / clone / import result: names arrive a moment later)
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
    // 2.16 gui/perf: hook_report() asked at most once a second; the pages that show it (Settings > Health and Technical
    // details, Job offers, Manager rules) read this every frame. An empty report while hook_report is unset.
    const HookReport& hook_report_cached();
    // crest serve (win/crest_serve_win.cpp): "crest serve: on (...)" or why it is off; unset = none (shown on the crest editor)
    std::function<std::string()> crest_serve_report;
    std::function<lualoader::Faces3dStatus()> faces3d_report;  // Lua loader / 3D hair+beard hook state (Windows host; unset = none)
    std::shared_ptr<capture::CaptureService> capture;   // miniface from the game's 3D model (Windows host; tests use a fake)
    // Standings refresh after a live table edit (core/standings_refresh.h; Windows host; tests use a fake; null = none)
    std::shared_ptr<svm::RefreshService> standings_refresh;
    std::string standings_refresh_status;  // last outcome shown in the Live standings view
    // Match setup: game variables and result fixing (core/match_setup.h; Windows host; tests use a fake; null = none)
    std::shared_ptr<msetup::Service> match_setup;
    // Tactics channel E: your club's live tactic record (core/team_sheet.h; Windows host; tests use a simulated game; null = none)
    std::shared_ptr<tsheet::Service> team_sheet;
    // Gameplay tuning: the engine's tuning tables (core/tuning.h, ui_tuning.cpp; Windows host; tests use a simulated table; null = none)
    std::shared_ptr<tuning::Service> tuning;
    std::string match_setup_status;  // last variable outcome shown in the Match setup view
    int texture_test_frames = 0;
    double lua_heartbeat_seen_at = -1.0;
    uint64_t game_base = 0;  // FC27.exe image base (set by the Windows host; 0 in tests = skip vtable checks)
    // What identifies the FCE interface and DataManager on the running build (core/fce_standings.h Anchors): the Windows
    // host's resolver (signature-resolved once its scan is done), set before the first frame. Without one: no checks
    // when game_base is 0 (tests on synthetic memory), else nothing is known and every locate is refused.
    std::function<fce::Anchors()> fce_anchors_hook;
    fce::Anchors fce_anchors() const {
        if (fce_anchors_hook) return fce_anchors_hook();
        if (!game_base) return fce::Anchors::unchecked();
        fce::Anchors a;
        a.image_base = game_base;
        a.pending = "the game's signatures are not available to Turbo (no anchor resolver)";
        return a;
    }
    // ---- spoken-set capture from the loaded commentary bank (core/commentary_bank.h) on a background thread:
    // the host lists the game's readable private regions; tests give a synthetic list over the simulated memory
    std::function<std::vector<Region>()> regions_hook;
    // 4.0 sliders-rw: the host's "is [addr, addr+n) a writable page of the game" (VirtualQuery); unset in tests (simulated memory)
    std::function<bool(uint64_t, size_t)> writable_hook;
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
    std::string chosen_commentary_language() const;
    void refresh_callnames();  // Callnames::refresh with the override + the "callnames: language <lang> (<why>)" log line
    bool detect_language_then_apply(const BankCapture& c, const std::string& when, const std::string& build, std::string* err);
    bool detect_language_from_probe(const caudio::BuildResult& r);  // a clear probe sample answer (no build needed)
    bool spoken_lang_only_build_ = false;  // the running build asks for the language only (names, no players, not cached)
    double lang_off_check_ = -1.0;         // next check of callnames_lang_detect_off.txt (live kill switch)
    double settings_lang_check_ = -1.0;    // next stat of the game's settings folder (4.0 lang-settings: the Settings file's language)
    const std::string& spoken_watch_line() const { return spoken_watch.line(); }
    long long game_call_seen = -1;  // last game-call outcome shown as a toast (bridge_state.json game_call.seq; -1 = none yet)
    // Job offer tab (Teams > team edit): the club picked (the selected team unless another is chosen) and the last request label
    int64_t job_offer_team = 0;
    std::string job_offer_status;
    std::string mass_status;  // Teams > team edit > Mass actions: the last outcome
    rolerules::State role_rules;  // Teams > Mass actions > Squad roles: the rule editor (ui_role_rules.cpp)
    char job_offer_search[64] = "";
    // ---- edits FC 27 forgets at every career load (core/reapply.h, ui_reapply.cpp): the kit colours of Teams > Colours
    // and the player-specific callnames of Players > Callname are kept in turbo_output\reapply_edits.json and written
    // again the first time Turbo connects to a newly loaded career. Kill switch: turbo_output\reapply_off.txt
    ReapplyStore reapply;
    std::string reapply_status;  // summary of the last re-apply ("" = none ran this session)
    std::string reapply_error;   // the store could not be read or written ("" = fine)
    // ---- Tactics tab (ui_tactics.cpp): the preset store (core/tactic_profiles.h, turbo_output/tactic_profiles.json), loaded once in the
    // constructor; a save is a user action, never part of the frame path. A file that could not be read is set aside at the next save.
    TacticProfileStore tactic_profiles;
    std::string tactic_profiles_error;   // "" = fine
    bool tactic_profiles_unreadable = false;
    // The user's opposition rules (turbo_output/opp_rules.json, core/opp_rules.h), loaded once in the constructor like the presets: the
    // Opposition card merges them with the built-in rules. A file that could not be read is never overwritten by Turbo.
    RuleSet opp_user_rules;
    std::string opp_rules_error;   // "" = fine
    size_t opp_rules_dropped = 0;  // rules of the file that were left out as invalid
    // Keep what write_colour just wrote to a teamkits row (only the kit colour channels; other tables are kept by the
    // game's save)
    void remember_kit_colour(const Table& t, uint64_t rec, const std::string& prefix, const uint8_t rgb[3]);
    // 2.3 kit import (Teams > Kits, ui_kits.cpp): keep one teamkits field it wrote (kit_kept_field only), or put a
    // field's kept state back as it was before the import (value null = not kept). save false: the caller saves once
    // with save_kept_edits(). False when the field is not one the store keeps (or the save failed)
    bool remember_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field, int64_t value, bool save = true);
    bool restore_kept_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field, const int64_t* value,
                                bool save = true);
    // 2.15: the same in the row of `scope` (the kit import's Undo: the scope its import wrote, whatever career is loaded now)
    bool restore_kept_kit_field_in(const std::string& scope, int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field,
                                   const int64_t* value, bool save = true);
    bool save_kept_edits();
    // Keep a player-specific callname the Callname tab just wrote (or queued); `from` = whose callname it is. A player
    // with his own recording (callnames.own_recording: the master list or the game's audio service) is never kept, and
    // an older entry of his is dropped: false, with the reason in *why
    bool remember_player_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from,
                                  std::string* why = nullptr);
    // The Forget buttons; false = nothing was kept
    bool forget_kit_edit(int64_t teamtechid, int64_t kittype, int64_t teamkitid);
    bool forget_player_callname(int64_t playerid);
    // Player edits (1.2.5): a players field the window wrote in a career (App::edit_field, App::undo) is kept by
    // player_field_written when kept_player_field (attributes, overall, potential, work rates, skill moves, weak foot) and
    // the switch is on; written again at every career load while the field holds the value the edit replaced. Forget
    // one field ("" = every field of his) or turn the keeping on / off (saved in the store); false = nothing changed
    bool forget_player_edit(int64_t playerid, const std::string& field = std::string());
    bool forget_all_player_edits();
    bool set_keep_players(bool on);
    // 2.15 (Career tools > Changes & undo, kept_edit_scopes_section): move a club's kit rows of scope `from`, a callname
    // row or a player row to another scope ("global", "career:<id>"); forget one field of one exact player row. False =
    // nothing changed (or not saved)
    bool move_kept_kits(int64_t teamtechid, const std::string& from, const std::string& to);
    bool move_kept_callname(const ReapplyStore::RowKey& key, const std::string& to);
    bool move_kept_player(const ReapplyStore::RowKey& key, const std::string& to);
    bool forget_kept_player_field(const ReapplyStore::RowKey& key, const std::string& field);
    // 2.15: the loaded career's identity (core/reapply.h header; reapply.career holds the id), read once per career load
    // before the kept edits are written: its name for the UI ("Mikel Arteta, Arsenal") and the line logged as
    // "career identity: ..." in turbo_gui.log ("" before the first connect)
    std::string career_label;
    std::string career_identity_line;
    // 2.21 compdata guard (core/compdata_profile.h; ui_reapply.cpp check_compdata_profile, right after the identity): the last
    // "compdata profile: ..." line ("" outside a career) and the red "do not save" line drawn in the message area while it is set
    // ("" = no mismatch). Read-only: nothing is written.
    std::string compdata_line;
    std::string compdata_banner;
    void check_compdata_profile();
    // Write every kept entry into the connected database: kit fields in place (Database::set, range-checked, no undo
    // step), callnames through write_player_callname (never adding a row). Not written, without an error: a player
    // with his own recording, a player or kit not in this career. Returns the summary line, which also goes to the GUI
    // log, turbo_gui.log (log_hook) and reapply_status. Runs by itself from refresh() once per newly loaded career.
    std::string reapply_stored_edits();
    // A newly loaded career is reported (another Lua session or load_gen than the last re-apply) and entries are kept:
    // tick then connects at once even while the window is hidden, so the edits are back before the first match
    bool reapply_due() const;
    // ---- Turbo X Shown OVR pin (core/ovr_pin.h, ui_reapply.cpp): the players in the identified career's ovr_mask.json get their
    // stored players.overallrating written to the shown value (Database::set, the re-apply path) at the career load, after a
    // Set / Clear (force) and again every ovr_pin::kPollSeconds when the game has put its own value back; Clear (or
    // turbo_output\ovr_pin_off.txt) puts the game's value back while the field still holds the pin. Only while the database
    // Turbo reads is the one the game reports (no refresh pending). Log: "ovr mask: <pid> overallrating X -> Y (...)".
    void ovr_pin_tick(bool force = false);
    void ovr_pin_rescan();           // ovr_pin_files_ from the careers folders (start-up, after a Set / Clear)
    void ovr_pin_release_on_exit();  // ~App: pinned fields that still hold the pin get the game value back (state file kept)
    std::string ovr_pin_status;      // the last pass that wrote something or refused ("" = none yet), for the header tooltip
    bool ovr_pin_files_ = false;     // some career has an ovr_mask.json / ovr_pin_state.json: reapply_due connects at a load
    bool ovr_pin_scanned_ = false;
    double ovr_pin_next_ = 0.0;
    // ---- voice swaps (core/callname_voice.h, Players > Callname): in matches only, a player is called with another
    // player's own recording, or his own recording is turned off and a generic callname used. Nothing is written to the
    // database or the save. The store (turbo_output\callnames\voice_swaps.json, all careers) is loaded at start-up and
    // published to the host's hooks then and after every edit; the GUI tick refreshes the kill switches every 2 s.
    kitswap::Runtime* kit_swap = nullptr;     // 3D kits on the pitch (win/kit_swap_win.cpp; core/kit_swap.h); nullptr = no game hooks (tests set a fake-backed one)
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
    std::string voice_status_line() const;    // "Voice swaps: on | 3 swaps | lines changed 57 | kick-off set 2" (+ " | club lines N")
    // ---- club commentary names (core/club_voice.h, Club & crest > Identity > Commentary name, ui_club_voice.cpp): in
    // matches only, a club is spoken with another club's recorded name, or with none. The store
    // (turbo_output\callnames\club_voice.json) is loaded at start-up; the entries in effect in the loaded career go out
    // with the voice-swap table (voice_publish), again at every career load (maybe_reapply).
    clubvoice::Store club_voice;
    std::string club_voice_error;             // the store's last load / save note ("" = fine)
    // Add or replace the club's entry of e.scope (stamped now), publish, save. false (club_voice_error set) when not
    // saved; the change is published for this session anyway
    bool club_voice_keep(clubvoice::Entry e);
    bool club_voice_forget(int64_t teamid, const std::string& scope);  // false = no such entry
    // ---- live team names (core/teamname_override.h, Teams > Name): the names saved there are given to the game by the
    // host's hook at once (no restart). The store (turbo_output\team_names.json, all careers) is loaded at start-up and
    // published to the service then and after every save; the GUI tick refreshes the kill switches every 2 s.
    tnames::Service* team_names_service = nullptr;  // the host's (win/teamname_override_win.cpp); nullptr = off; tests set a fake
    tnames::Store team_names;
    std::string team_names_error;                   // the store's last load / save note ("" = fine)
    bool team_names_live() const { return team_names_service && team_names_service->available(); }
    std::string team_names_why_off() const;         // "" when live
    void team_names_publish();                      // the store to the service (nothing without one)
    // X 2.0 review fix: the edit profiles (core/edit_profiles.cpp) save through the same guarded paths as every other edit
    bool save_team_names(std::string* why);         // turbo_output\\team_names.json, never over a file unreadable at start-up
    // Keep a club's names (stamped now), publish them, save the store. false (*err) when the file was not written; the
    // names are published for this session anyway
    bool team_names_keep(tnames::Entry e, std::string* err);
    // 2.15: a club's name of scope `from` moved to `to` ("global", "career:<id>"), published and saved; a name of every
    // career also goes into Live Editor's custom_team_names.csv. False = no such name / not saved
    bool move_team_name(int64_t teamid, const std::string& from, const std::string& to);
    std::string team_names_status_line() const;     // "Live team names: on | 2 renamed clubs | names given 57"
    // Managers > Manager rules (features/manager_rules.lua) and Manager market (features/manager_move.lua)
    std::string manager_rules_status;
    int manager_rules_score = 70;     // score typed for "Set score"
    std::string manager_move_status;
    int64_t manager_move_team = 0;    // club picked for "Move to the picked club"
    char manager_move_search[64] = "";
    // Season update pack importer (ui_cmtracker.cpp season_pack_dialog): the commands of the page or undo in flight, sent one at a
    // time as Lua answers (season_pack_on_result is called from tick when an answer to a "Season pack" command arrives)
    std::deque<nlohmann::json> pack_queue;
    std::string pack_kind, pack_pack, pack_status;   // "apply" / "undo", the pack's name, what to show
    bool pack_failed = false;
    int pack_sent = 0, pack_total = 0;

    // ---- selections / UI state
    int64_t sel_player = 0;
    int64_t sel_team = 0;
    int sel_manager = -1;
    std::string db_table;
    // Legacy navigation shim (GUI revamp 2.13): request_tab = N lands on shell::area_for(N): 0 players, 1 teams, 2 managers,
    // 3 competitions, 4 database, 5 tools, 6 status, 7 game settings. New code uses go().
    int request_tab = -1;
    // ---- shell (ui_shell.h): areas, each a strip of pages
    // Open a page for the next frame (its area comes with it). Ignored when the page is invalid.
    void go(shell::Area a, shell::Page p);
    void go(shell::Page p) { go(shell::area_of(p), p); }
    // 3.1 N1: back to the place the last navigation left (shell::back_place()); false when there is none or it is hidden in this view.
    // Going back does not make the place it leaves the next "back" (no ping-pong), and clears the back place.
    bool back();
    // Where the shell is now (set while drawing; Home before the first frame)
    shell::Place place() const { return place_; }
    // The area tabs, the page strip and the page: the body of the window under the status line (called by draw())
    void draw_pages();

    // ---- helpers
    GameDate today() const;
    void notify(const std::string& text, bool error = false);
    void log(const std::string& text);
    bool refresh();  // re-walk the database and rebuild lists
    bool connected() const { return db.ready() && model.built(); }
    bool lua_alive() const;

    // Write one field through validation; updates cached lists for players/teams. players.overallrating goes through
    // set_overall (his attributes follow); every other field is edit_field
    bool edit(const Table& t, uint64_t rec, const Field& f, const Value& v);
    // A field value in words (core/field_labels.h describe(), plus what needs the database): a nation or club name, a date
    // ("2001-03-24"), Yes / No for a switch, a role / body type / colour name. Never a bare code the game gives a name.
    std::string describe_value(const Table& t, const Field& f, const Value& v);
    // "Overall changed 84 -> 87 (Player X)" for the log and toasts
    std::string describe_edit(const Table& t, uint64_t rec, const Field& f, const Value& before, const Value& after);
    std::string nation_name(int64_t nationid);  // "" when unknown
    // The write as given: range check, an undo step (undo_step false: the caller pushes one for the whole change), the
    // cached rows; for a players field also the kept edits and the development plan (player_field_written)
    bool edit_field(const Table& t, uint64_t rec, const Field& f, const Value& v, bool undo_step = true);
    // An overall typed into a field box (Profile, All fields, field search, Database tab). FC 27 shows the overall its
    // own formula gives from the attributes and works it out again on its own (a bare overallrating write does not
    // last), so the attributes that weigh most for his position move until Turbo's fitted formula
    // (core/overall.h reach, as Overall +1/-1) gives `target`, then overallrating = target. One undo step and one
    // "Undo bulk" entry. A players table without the 34 attributes: the overall alone. False (toast) when the attributes
    // cannot give it (1..99, attributes at their limits)
    bool set_overall(const Table& t, uint64_t rec, int64_t target);
    // ---- your players' development plans (features/development.lua mode plan_sync): the plan fields the window writes
    // for a player of your club (or one it lent out) are queued here and sent to Turbo's Lua side, which writes the
    // players table's values into the game's plan (the plan outranks the table at a career load: without it the edit
    // came back with its old value in every later save). One command per run of edits (kPlanSyncQuiet s without a
    // new one), several players per command; dropped when the career is left or another one loaded before it was sent.
    // The result is logged (no toast unless it failed); plan_sync_status keeps it for the Players tab. Kill switch:
    // turbo_output\plan_sync_off.txt (nothing queued while it exists).
    static constexpr double kPlanSyncQuiet = 1.0;
    void queue_plan_sync(int64_t playerid, const std::string& field);
    size_t plan_sync_waiting() const { return plan_queue_.size(); }
    std::string plan_sync_status;

    // Ask Turbo's Lua side to run something (executes on the next career-mode event)
    bool send(const nlohmann::json& cmd, const std::string& label);
    bool busy();

    void load_gui_settings();
    bool save_gui_settings();

    // ---- 5.0b-B layout (core/layout_store.h): the Dear ImGui ini text (column widths, pane sizes, window blocks) is owned here, not by ImGui.
    // The overlay gives it a file sink (turbo_output/turbo_gui_layout_v2.ini, the same file as before) and loads it once; draw() pumps it every
    // frame. Settings > Layout: reset_layout() (after its confirm; the file is parked as .bak-<stamp>), undo_reset_layout(), restore_window_default().
    LayoutStore layout;
    bool reset_layout();
    bool undo_reset_layout();
    std::map<std::string, float> layout_splits_parked_;  // the pane widths (gui.splits) set aside by Reset layout, put back by Undo reset
    void restore_window_default();  // the Restore path (gui_prefs.h window_plan, T7 D3) with no stored rectangle: the first-use size, not maximized

    // ---- game asset catalog (core/asset_catalog.h, docs/asset-catalog.md): loaded on a worker thread at the first tick
    // and again when turbo_output\asset_catalog changes (a rebuilt catalog, a switch file added or removed; polled every
    // 5 s); turbo_output\asset_catalog\lua_gate.txt is written after each load while the catalog is current, else deleted
    std::shared_ptr<const assets::Catalog> asset_catalog() const;  // never null ("not loaded" before the first load)
    const assets::DbUse& asset_use();     // what the loaded save uses (players, manager, teams, ...); rebuilt per model.version()
    bool asset_reload(const char* why);   // starts a load now; false when one is running
    bool asset_loading() const { return asset_running_.load(); }
    std::string asset_running_key() const;  // the running game's build key (host hooks), else FC27.exe's header, "" unknown
    std::string asset_load_note;            // last load: "<status line> (<ms> ms, <why>)" for Tools > Status
    // A write of a hidden id: one line in turbo_output\asset_writes.log (and the GUI log); false when it was not written
    bool log_asset_write(const std::string& target, const std::string& field, const std::string& old_value, const std::string& new_value);
    // All / In this save / Hidden filter per picker, remembered in gui_settings.json (asset_scope.<picker>)
    assets::Scope asset_scope(const std::string& picker) const;
    void set_asset_scope(const std::string& picker, assets::Scope s);
    std::filesystem::path turbo_output() const { return bridge.root() / "turbo_output"; }

    // ---- 2.19 career faces (core/career_faces.h): a face picture chosen in a career lives only in that career
    career_faces::Paths career_faces_paths() const;
    // The career whose face pictures belong in mods\legacy now: the identified career while one is loaded and its identity is
    // current for this load, else "" (main menu, Kick Off, a career not identified yet)
    std::string career_faces_wanted() const;
    // In a career (Lua reports career mode) whose identity Turbo does not know: a face picture may not be written anywhere
    // (globally it would reach every career). `why` says so for the user.
    bool career_faces_refused(std::string* why) const;
    // Writes a face picture (miniface, heads_staff) the right way: in an identified career the career's own copy (then copied
    // into mods\legacy while it is loaded), outside a career the global mods\legacy file as before. `rec` (optional) is what an
    // Undo needs. Refused (false, err) in a career Turbo has not identified. Any other legacy path: global, as before.
    bool write_face_picture(const std::string& rel, const std::vector<uint8_t>& bytes, std::string* err,
                            UndoStep::Picture* rec = nullptr);
    // Undo of one picture write_face_picture made (or a pre-2.19 global one)
    bool undo_face_picture(const UndoStep::Picture& p, std::string* err);
    // Makes mods\legacy match `career` now (career_faces::sync) and refreshes Turbo's own picture caches for every path it changed
    void career_faces_sync(const std::string& career, const char* why);
    const std::string& career_faces_active() const { return career_faces_active_; }

private:
    std::string career_faces_active_;    // the career career_faces_sync last ran for ("" = none)
    bool career_faces_ticked_ = false;   // the first tick (the start-up restore's log line goes to the host log there)
    std::string career_faces_start_line_;
    shell::Place place_{shell::Area::Home, shell::Page::Home};  // 3.0: the app opens on Home
    HookReport hook_report_cache_;      // hook_report_cached()
    double hook_report_cache_at_ = -1.0;  // App time of that report (< 0 = none)
    shell::Place go_to_;                                    // a go() waiting for the next draw_pages()
    bool back_nav_ = false;                                 // the go_to_ above is a back(): the place it leaves is not kept as the back place
    shell::Page area_page_[shell::kAreaCount] = {};         // the page last shown per area (None = the area's default)
    bool legacy_repaired_ = false;  // repair_dds_files ran (first tick)
    void load_voice();               // constructor: turbo_output\callnames\voice_swaps.json
    bool save_voice();
    const voice::Service* voice_published_to_ = nullptr;  // the service the store was last published to
    double voice_next_switches_ = 0.0;                    // next refresh_switches() (every 2 s)
    bool voice_unreadable_ = false;  // the store could not be read at start-up: never overwritten
    void load_club_voice();          // constructor: turbo_output\callnames\club_voice.json
    bool save_club_voice();
    bool club_voice_unreadable_ = false;  // the store could not be read at start-up: never overwritten
    void load_team_names();          // constructor: turbo_output\team_names.json
    const tnames::Service* team_names_published_to_ = nullptr;  // the service the store was last published to
    double team_names_next_switches_ = 0.0;                     // next refresh_switches() (every 2 s)
    bool team_names_unreadable_ = false;  // the store could not be read at start-up: never overwritten
    void load_reapply();             // constructor: turbo_output\reapply_edits.json
    bool save_reapply();             // after every change of the store; false (reapply_error set) when not written
    void maybe_reapply();            // refresh(): re-apply once per newly loaded career
    void update_career_identity();   // maybe_reapply(): once per career load (identity_key_), before any kept edit is written
    void note_career_label();        // save_reapply(): this career's name kept in the store once a row is bound to it
    std::string identity_key_;       // reapply_key() the career identity was last read for
    std::string reapply_key() const;  // "<Lua session>#<load_gen>" of the career Lua reports (db_gen from an older Lua)
    bool reapply_unreadable_ = false;  // the file on disk could not be read: set it aside at the next save
    std::string reapplied_key_;        // reapply_key() of the career the kept edits were last written to
    bool reapply_save_due_ = false;    // a player edit was kept: the store is saved at the next tick (one write per frame)
    // A players field changed through the window (edit_field, undo): keep it (ui_reapply.cpp) and queue the plan sync
    void player_field_written(int64_t playerid, const std::string& field, const Value& before, const Value& after);
    bool plan_sync_wanted(int64_t playerid);  // a player of your club or one it lent out, in a career, plan natives there
    void flush_plan_sync();                   // tick: send the queued plan fields
    std::map<int64_t, std::set<std::string>> plan_queue_;  // playerid -> plan fields written since the last command
    double plan_queue_at_ = 0.0;   // when the last field was queued
    std::string plan_queue_key_;   // reapply_key() of the career they were written in
    void finish_bank_capture();  // tick: take a finished capture, cache it, rebuild the pickers
    void finish_spoken_build();  // tick: take a finished audio-service build, cache it, rebuild the pickers
    void spoken_watch_tick();    // tick: run the watcher, start the probe / build it asks for
    void settings_lang_tick();   // tick: a new Settings file (the game saved its settings) re-detects the commentary language
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
    // asset catalog loading (asset_catalog(), asset_reload())
    void asset_tick();         // tick: take a finished load; poll the catalog folder every 5 s
    void finish_asset_load();  // tick: swap the loaded catalog in, log it, rebuild the pickers
    void publish_asset_catalog();  // the pickers' catalog (set_faces_catalog, set_gear_catalog, callnames, asset_view)
    void publish_asset_use();      // asset_view()'s DbUse, once per model.version() while a catalog is loaded
    std::mutex asset_m_;
    std::thread asset_thread_;
    std::atomic<bool> asset_running_{false};
    bool asset_done_ = false;
    std::shared_ptr<const assets::Catalog> asset_cat_;     // the catalog the pickers read (render thread only)
    std::shared_ptr<const assets::Catalog> asset_result_;  // a finished load waiting for finish_asset_load (asset_m_)
    std::string asset_result_note_;                        // its "(ms, why)" part (asset_m_)
    std::string asset_sig_;                                // assets::dir_signature at the last load start
    double asset_next_poll_ = 0.0;
    bool asset_started_ = false;
    assets::DbUse asset_use_;
    bool asset_use_built_ = false;
    uint64_t asset_view_version_ = ~0ull;  // model.version() of the DbUse published in asset_view()
};

// panels
void draw_teams(App& app);
// Tactics panel body (ui_tactics.cpp)
void draw_tactics(App& app);
// "Transfer bans" section for a club (what = "team") or a player ("player"): ui_players.cpp; Teams tab > Overview and
// the Transfers & money page
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
// A Live standings cell box is open: its keys go to Turbo even before Dear ImGui marks the box active (overlay_dx12.cpp)
bool live_standings_wants_keyboard();
// The gate in front of every FCE row / fixture write of Live standings and Match setup and of their Undo, looked at right
// before the write (core/fce_standings.h write_gate): "" = write now, else why not (turbo_output\live_standings_write_off.txt,
// a read-only located set, the game processing a match day). ui_standings.cpp
std::string fce_write_refusal(App& app, const fce::Located& loc);
// Why the located set cannot be edited at all for now ("" = it can): read-only, or the kill switch file (looked at once a
// second); shown above the table / fixtures. The match-day check is made only at the write (fce_write_refusal).
std::string fce_edit_off(App& app, const fce::Located& loc);
// Competitions tab, "Match setup" view: the user's next fixtures (venue, opponent, fixed result) and the gameplay switches
// (game variables) for the next matches (ui_match.cpp)
void draw_match_setup(App& app);
// Clear the game variables the Match setup panel set (its Set buttons) when the host unloads Turbo. Never throws.
void match_setup_clear_on_exit(App& app);
void draw_tools(App& app);
void draw_status(App& app);
// The Status tab in its three parts (ui_tools.cpp); the Settings page (ui_settings.cpp) draws the first two under its own title
void draw_status_health(App& app);  // pictures, health, collapsed technical details
void draw_look_and_feel(App& app);  // 2.22: UI size and Row height (Settings > Look and feel)
void draw_preferences(App& app, bool with_look = true);  // show/hide key, background loading; with_look also UI size and Row height (the old Status tab)
void draw_status_log(App& app);     // the log, filling the window
void draw_log_section(App& app, const char* key);  // ui_settings.cpp: a collapsing "Log" section (closed at first; `key` keeps its state)
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
// The player's kept edits in Players > Attributes (fields, kept / replaced / now, Forget) and the development plan line;
// the whole list with the on / off switch and Forget per field (Turbo Tools > Kept player edits)
void reapply_player_line(App& app, const PlayerRow& p);
void kept_player_edits_section(App& app);
// 2.15 (Career tools > Changes & undo): where each kept row applies, with Global / This career
void kept_edit_scopes_section(App& app);
// Slider over the field's whole range (attributes); a typed value outside it is refused by Database::set
bool slider_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);
// Same with the label column width and the number box width given (the Players > Attributes tab uses roomier values)
bool slider_editor_ex(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float label_w, float width,
                      float box_w);
// Combo with readable labels for an enumerated field (preferred foot, work rates, stars ...); false = no labels known
bool enum_editor(App& app, const Table& t, uint64_t rec, const Field& f, const char* label, float width);
// Labels for enumerated fields: nullptr when the field is not enumerated; label for value v, or nullptr
const char* enum_label(const std::string& field, int64_t v);
bool is_enum_field(const std::string& field);
// Text of a code field's value as its combo shows it, never empty and never a bare number (the enumerated fields above,
// else core/field_labels.h); `live`: who uses a player-specific code in the loaded table
std::string code_value_text(const std::string& field, int64_t v, const labels::CodeUsers* live = nullptr);
// The values a field holds in the loaded table: how many records hold each and, in the players table, the three
// best-rated players holding it (one snapshot per table and model version)
const std::map<int64_t, labels::CodeUsers>& values_in_use(App& app, const Table& t, const Field& f);
// The codes the field's combo offers: the named and described ones, the ones the loaded table uses, the game's
// pictures, a small unnamed field's whole range, and always the current value
std::vector<int64_t> code_value_list(App& app, const Table& t, const Field& f, int64_t current);

// ---- form rows (widgets.cpp): the label on the LEFT in a fixed column, the control on the RIGHT, aligned.
// The label column width of the panel being drawn (S() applied by the caller); call it at the top of a panel or block.
// 0 restores the default (S(150)).
void form_label_width(float w);
float form_label_width();
// Label on the left; the cursor stays on the same line at the end of the label column for the control that follows (give
// that control a hidden "##id" label and its own width). `tip`: tooltip for the label.
void form_label(const char* label, const char* tip = nullptr);
// Checkbox with the label on the left and the box on the right. Same ImGui ID as ImGui::Checkbox(label), so saved
// settings and the tests find it under the same name; the whole row (label and box) is the click target. True when toggled.
// label_w: the label column the box aligns to (0 = form_label_width(); negative = tight, the box right after the text).
bool checkbox_right(const char* label, bool* v, float label_w = 0.0f);
// A two-sided range filter row: label, then "from" and "to" number boxes (core/range_filter.h: 0 = no bound, from <= to is
// kept). `id` names the boxes "##<id>_from" / "##<id>_to". True when a value changed.
bool range_filter_row(const char* label, const char* id, int& from, int& to, int hi, float box_w, const char* tip = nullptr);
// Combo over the game's visible positions (core/model.h visible_positions()); the full internal table behind a "Show all
// internal positions" toggle in the popup (position_show_all()). `none_label` (optional) adds a -1 entry ("None",
// "Any position"); lo/hi restrict the codes offered (hi < 0 = the whole table). A current value outside the visible list is
// shown with its descriptor and stays selectable. Sets `pos` and returns true on a pick.
bool position_combo(const char* id, int& pos, float width, const char* none_label = nullptr, int lo = 0, int hi = -1);
bool& position_show_all();

// ui_cmtracker.cpp: the answer of a "Season pack" command (sends the next queued one, or ends the run)
void season_pack_on_result(App& app, bool ok, const std::string& result);

}  // namespace turbo
