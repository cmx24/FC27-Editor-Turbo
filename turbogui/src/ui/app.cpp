#include "app.h"
#include "ui_theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "core/field_labels.h"
#include "core/hotkey.h"
#include "core/overall.h"
#include "core/teamnames.h"
#include "core/user_text.h"
#include "file_picker.h"
#include "geo.h"
#include "hotkey_setting.h"
#include "imgui.h"
#include "preload.h"
#include "ui_zoom.h"
#include "ui_asset_browser.h"
#include "ui_faces.h"
#include "ui_faces3d_gate.h"
#include "ui_images.h"

#include "ui_edit_unlock.h"
#include "ui_tactics.h"
#include "ui_live_tactic.h"
#include "ui_tactic_presets.h"
#include "ui_club_page.h"
#include "ui_tuning.h"
#include "ui_game_settings.h"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

float g_ui_scale = 1.0f;

// ---- background loading (ui/preload.h)
void App::preload_start(const char* why) {
    if (preloader) preloader->start(why);
}

std::string App::preload_line() const { return preloader ? preloader->line(*this) : std::string(); }
std::vector<std::string> App::preload_status() const { return preloader ? preloader->status(*this) : std::vector<std::string>(); }

bool App::lua_images_wanted() const {
    if (!visible) return false;
    // pictures asked on screen (a miniface opened for editing, a picker grid) wait for the game: until 1.2.5 only the
    // background loading prompted Lua, so once it was done (a second after the start with a warm cache) such a picture
    // waited for the next real career-mode event and its box stayed "loading" meanwhile
    if (legacy.wants_lua()) return true;
    return preloader && preloader->loading() && legacy.waiting_background() > 0;
}

float auto_ui_scale(float display_height) {
    float s = display_height > 0.0f ? display_height / 1080.0f : 1.0f;
    return std::max(1.0f, std::min(3.0f, s));
}

// Readable over a bright game picture: near-opaque window, light text, clearer greys (Dear ImGui's dark style is made for
// a desktop background)
static void turbo_theme(ImGuiStyle& st) {
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 6.0f;
    st.FrameRounding = 4.0f;
    st.TabRounding = 4.0f;
    st.WindowBorderSize = 1.0f;
    st.FramePadding = ImVec2(6.0f, 4.0f);
    st.ItemSpacing = ImVec2(8.0f, 5.0f);
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.32f, 0.36f, 0.44f, 0.80f);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.19f, 0.25f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.28f, 0.38f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.34f, 0.48f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.20f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.20f, 0.36f, 1.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.05f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.17f, 0.24f, 1.0f);
    // Thin bars with a small rounding (Dear ImGui's default rounding 9 turns a short grab into an oval)
    st.ScrollbarSize = 12.0f;
    st.ScrollbarRounding = 3.0f;
    st.GrabMinSize = 12.0f;
    st.GrabRounding = 3.0f;
}

void App::update_style() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    float want = auto_ui_scale(io.DisplaySize.y) * std::max(kUiScaleMin, std::min(kUiScaleMax, ui_scale_user));
    want = std::round(want * 20.0f) / 20.0f;
    if (want == ui_scale_applied && prefs.row_density == density_applied) return;
    ImGuiStyle& st = ImGui::GetStyle();
    // Start from Dear ImGui's unscaled sizes every time: ScaleAllSizes multiplies what is there, and turbo_theme only
    // resets a few sizes, so each scale change (window height, UI size, Ctrl + wheel) used to grow the scrollbars,
    // grabs and paddings again (big grey ovals after a few resizes). The font size base is kept.
    const float font_base = st.FontSizeBase, font_main = st.FontScaleMain;
    st = ImGuiStyle();
    st.FontSizeBase = font_base;
    st.FontScaleMain = font_main;
    turbo_theme(st);
    st.ScaleAllSizes(want);
    st.FontScaleDpi = want;
    // Row density (Status > Settings, gui_settings.json gui.row_density, ui/gui_prefs.h): table rows are text height +
    // 2 * CellPadding.y, list rows text + ItemSpacing.y, widget rows FrameHeight. "normal" multiplies by 1: the style is
    // then exactly the one before this setting existed (no rounding: it moved rows at fractional scales).
    st.CellPadding.y *= row_density_cell_padding(prefs.row_density);
    st.ItemSpacing.y *= row_density_item_spacing(prefs.row_density);
    st.FramePadding.y *= row_density_frame_padding(prefs.row_density);
    density_applied = prefs.row_density;
    if (ui_scale_applied > 0.0f && ui_scale_applied != want) ui_scale_changed_from = ui_scale_applied;
    ui_scale_applied = want;
    g_ui_scale = want;
}

App::App(Memory& m, fs::path le_root, uint64_t mailbox_addr, std::string sess)
    : mem(m), bridge(std::move(le_root)), db(m), model(db), session(std::move(sess)), legacy(bridge.root()) {
    preloader = std::make_unique<Preloader>();
    game_root = game_root_from_process();
    if (mailbox_addr) {
        mailbox = std::make_unique<Mailbox>(mem, mailbox_addr);
        if (mailbox->init()) {
            mailbox_addr_ = mailbox_addr;
            bridge.publish_mailbox(mailbox_addr, session, kGuiVersion);
        } else {
            mailbox.reset();
        }
    }
    load_gui_settings();
    load_reapply();  // kit colours and player-specific callnames written again at every career load (ui_reapply.cpp)
    {  // Tactics presets (turbo_output	actic_profiles.json): read once here, never in the frame path
        ProfileLoadReport rep;
        if (!load_tactic_profiles(tactic_profiles_path(bridge.root()), tactic_profiles, &tactic_profiles_error, &rep)) {
            tactic_profiles_unreadable = true;
            tactic_profiles = TacticProfileStore();
        }
    }
    {  // the user's opposition rules (turbo_output/opp_rules.json): same rule, read once here
        std::string err;
        if (!load_opp_rules(opp_rules_path(bridge.root()), opp_user_rules, &err, &opp_rules_dropped)) {
            opp_rules_error = err.empty() ? "the opposition rules file could not be read" : err;
            opp_user_rules = RuleSet();
        }
    }
    load_voice();    // voice swaps (ui_callnames.cpp): no career needed; published once the host gives the service
    folder_store(bridge.dir() / "gui_folders.json");  // last folder of each in-overlay file picker (file_picker.cpp)
    load_team_names();  // live team names (ui_identity.cpp): published once the host gives the service
    if (const char* tt = std::getenv("TURBO_GUI_TEST_TEXTURES")) texture_test = tt[0] == '1';
    log(std::string("Turbo GUI ") + kGuiVersion + " started");
}

GameDate App::today() const {
    const auto& st = bridge.state();
    if (st.date.valid()) return st.date;
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    GameDate d;
    d.year = tmv.tm_year + 1900;
    d.month = tmv.tm_mon + 1;
    d.day = tmv.tm_mday;
    return d;
}

void App::log(const std::string& text) {
    log_lines.push_back(text);
    while (log_lines.size() > 300) log_lines.pop_front();
}

void App::notify(const std::string& text, bool error) {
    log((error ? "ERROR: " : "") + text);
    toasts.push_back({text, error, now + (error ? 6.0 : 3.5)});
    if (toasts.size() > 5) toasts.erase(toasts.begin());
}

bool App::lua_alive() const { return lua_heartbeat_seen_at >= 0.0 && (now - lua_heartbeat_seen_at) < 600.0; }

std::unordered_set<int64_t> App::commentary_ids() {
    std::unordered_set<int64_t> out;
    if (const Table* t = db.table("commentarynames"); t && t->has("commentaryid")) {
        Snapshot s;
        if (s.load(db.memory(), *t)) {
            const Field& f = *t->field("commentaryid");
            for (uint32_t i : s.valid) {
                int64_t id = s.get_int(i, f);
                if (id >= kCallnameMin && id <= kCallnameMax) out.insert(id);
            }
        }
    }
    if (out.empty()) out = callnames.index.used_ids;
    return out;
}

App::~App() {
    tactics_clear_on_exit(*this);      // game variables the Tactics tab set go back to "the game decides"
    match_setup_clear_on_exit(*this);  // ... and those the Match setup panel set (ui_match.cpp)
    if (reapply_save_due_) save_reapply();  // a player edit kept in the last frame
    bank_cancel_ = true;
    if (bank_thread_.joinable()) bank_thread_.join();
    if (asset_thread_.joinable()) asset_thread_.join();  // a catalog load in flight (a few hundred ms at most)
}

// ---- spoken-set capture from the loaded commentary bank (core/commentary_bank.h)
bool App::start_bank_capture(bool automatic) {
    if (bank_running_.load()) return false;
    if (!regions_hook) {
        bank_capture_status = "not available: no memory region lister in this build";
        return false;
    }
    if (bank_thread_.joinable()) bank_thread_.join();
    bank_cancel_ = false;
    bank_running_ = true;
    {
        std::lock_guard<std::mutex> lock(bank_m_);
        bank_done_ = false;
    }
    bank_capture_status = automatic ? "capturing the loaded bank's selection tables (automatic)..." : "capturing the loaded bank's selection tables...";
    log("callnames: bank capture started" + std::string(automatic ? " (automatic)" : ""));
    Memory* m = &mem;
    std::function<std::vector<Region>()> lister = regions_hook;
    // the commentary ids the database knows (commentarynames; else the ids playernames uses): a surname table must
    // consist of them, which keeps the bank's dense sample index out of the spoken set
    auto known = std::make_shared<std::unordered_set<int64_t>>(commentary_ids());
    bank_thread_ = std::thread([this, m, lister, known]() {
        BankCapture c;
        try {
            std::vector<Region> regions = lister();
            const auto t0 = std::chrono::steady_clock::now();
            auto clock = [t0]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
            c = capture_commentary_bank(*m, regions, [this]() { return bank_cancel_.load(); }, clock, 1u << 20, known->empty() ? nullptr : known.get());
        } catch (const std::exception& e) {
            c = BankCapture{};
            c.note = std::string("capture failed: ") + e.what();
        }
        std::lock_guard<std::mutex> lock(bank_m_);
        bank_result_ = std::move(c);
        bank_done_ = true;
        bank_running_ = false;
    });
    return true;
}

void App::finish_bank_capture() {
    BankCapture c;
    {
        std::lock_guard<std::mutex> lock(bank_m_);
        if (!bank_done_) return;
        bank_done_ = false;
        c = std::move(bank_result_);
    }
    if (bank_thread_.joinable()) bank_thread_.join();
    const std::string when = time_stamp();
    std::string build = hook_report ? hook_report().build : std::string();
    std::string err;
    char line[256];
    std::snprintf(line, sizeof(line), "%.1f s, %zu regions, %.0f MB: %s", c.seconds, c.regions, double(c.bytes) / 1e6, c.note.c_str());
    if (c.cancelled) {
        bank_capture_status = "capture cancelled";
    } else if (!c.ok) {
        bank_capture_status = line;
        notify("Callnames: " + c.note, true);
    } else if (!callnames.apply_capture(c, bridge.root(), when, build, &err)) {
        bank_capture_status = std::string(line) + " - not used: " + err;
        notify("Callnames: capture not used: " + err, true);
    } else {
        bank_capture_status = line;
        notify("Callnames: " + c.note);
        ++gen;  // the Callname tab rebuilds its pickers
    }
    log("callnames: bank capture " + bank_capture_status);
}

std::string App::time_stamp() const {
    char when[32];
    std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::localtime(&t);  // render thread only
    if (!tmv || std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", tmv) == 0) std::snprintf(when, sizeof(when), "%lld", static_cast<long long>(t));
    return when;
}

// ---- game asset catalog (core/asset_catalog.h): one load on a worker thread, swapped in on the render thread
std::shared_ptr<const assets::Catalog> App::asset_catalog() const {
    static const std::shared_ptr<const assets::Catalog> kEmpty = std::make_shared<assets::Catalog>();
    return asset_cat_ ? asset_cat_ : kEmpty;
}

std::string App::asset_running_key() const {
    if (hook_report) {
        const std::string k = hook_report().build;
        if (!k.empty()) return k;
    }
    return game_root.empty() ? std::string() : assets::exe_build_key(game_root / "FC27.exe");
}

bool App::asset_reload(const char* why) {
    if (asset_running_.load()) return false;
    if (asset_thread_.joinable()) asset_thread_.join();
    asset_started_ = true;
    const fs::path out = turbo_output();
    asset_sig_ = assets::dir_signature(out);
    const std::string key = asset_running_key();  // hook_report is the host's: asked here, not on the worker
    const std::string reason = why ? why : "";
    const std::string gate_session = session;  // lua_gate.txt carries it: Lua trusts only the gate of this GUI session
    asset_running_ = true;
    asset_thread_ = std::thread([this, out, key, reason, gate_session]() {
        auto c = std::make_shared<assets::Catalog>();
        std::string note;
        try {
            const auto t0 = std::chrono::steady_clock::now();
            c->load_dir(out, key);
            std::string gate_err;
            // lua_gate.txt follows the catalog: written while it is current, deleted when stale / off / missing
            if (!c->sync_lua_gate(out, &gate_err, gate_session)) note = "; lua_gate.txt not updated: " + gate_err;
            char buf[96];
            std::snprintf(buf, sizeof(buf), " (%.0f ms, %s)", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                          reason.c_str());
            note = buf + note;
        } catch (const std::exception& e) {
            c = std::make_shared<assets::Catalog>();
            note = std::string(" (load failed: ") + e.what() + ")";
        }
        std::lock_guard<std::mutex> lock(asset_m_);
        asset_result_ = std::move(c);
        asset_result_note_ = std::move(note);
        asset_done_ = true;
        asset_running_ = false;
    });
    return true;
}

void App::finish_asset_load() {
    std::shared_ptr<const assets::Catalog> c;
    std::string note;
    {
        std::lock_guard<std::mutex> lock(asset_m_);
        if (!asset_done_) return;
        asset_done_ = false;
        c = std::move(asset_result_);
        note = std::move(asset_result_note_);
    }
    if (asset_thread_.joinable()) asset_thread_.join();
    if (!c) return;
    // for the log only: a catalog rebuilt under the same build key keeps its state, key and file, so when it was built and
    // its counts are compared too
    const bool changed = !asset_cat_ || asset_cat_->state() != c->state() || asset_cat_->status().build_key != c->status().build_key ||
                         asset_cat_->status().file != c->status().file || asset_cat_->hidden_off() != c->hidden_off() ||
                         asset_cat_->status().built_at != c->status().built_at || asset_cat_->status().counts != c->status().counts;
    const bool was_loaded = asset_cat_ && asset_cat_->loaded();
    asset_cat_ = std::move(c);
    asset_load_note = asset_cat_->status_line() + note;
    if (!asset_cat_->status().note.empty()) asset_load_note += "; " + asset_cat_->status().note;
    // the first "no catalog" stays quiet in the log (most users have none); every real load or change is logged
    if (changed && asset_cat_->state() != assets::State::NotLoaded) {
        log("asset catalog: " + asset_load_note);
        if (log_hook) log_hook("asset catalog: " + asset_load_note);
    }
    // Every finished load reaches the pickers while there is (or was) a catalog: a catalog_<same key>.json rebuilt after a
    // content change (folder poll, Status > Reload catalog) must not leave the face / gear / callnames gates on the old
    // copy while lua_gate.txt already follows the new one. Loads are rare (start-up, a catalog file or switch changed,
    // the Reload button). No catalog = 2.0 behaviour, untouched.
    if (was_loaded || asset_cat_->loaded()) {
        publish_asset_catalog();
        ++gen;
    }
}

// Hands the catalog to every picker that reads it (real-face chooser, gear galleries and tattoos, callnames, the miniface
// and crest browsers). Not loaded (off, none, refused) = nullptr: each picker then behaves exactly as in 2.0.
void App::publish_asset_catalog() {
    const std::shared_ptr<const assets::Catalog> cat = asset_cat_ && asset_cat_->loaded() ? asset_cat_ : nullptr;
    set_faces_catalog(cat);
    set_gear_catalog(cat);
    callnames.use_catalog(cat);
    asset_view_version_ = ~0ull;  // the browsers' DbUse is published by publish_asset_use
    publish_asset_use();
}

// The miniface and crest browsers read what the save uses through asset_view(): republished once per model.version()
// while a catalog is loaded (nothing is built without one)
void App::publish_asset_use() {
    const std::shared_ptr<const assets::Catalog> cat = asset_cat_ && asset_cat_->loaded() ? asset_cat_ : nullptr;
    if (!cat) {
        if (asset_view().catalog) set_asset_view({});
        asset_view_version_ = ~0ull;
        return;
    }
    const uint64_t v = db.ready() ? model.version() : ~0ull - 1;
    if (v == asset_view_version_ && asset_view().catalog == cat) return;
    asset_view_version_ = v;
    AssetView view;
    view.catalog = cat;
    if (db.ready()) view.use = std::make_shared<const assets::DbUse>(asset_use());
    set_asset_view(std::move(view));
}

void App::asset_tick() {
    finish_asset_load();
    if (asset_cat_ && asset_cat_->loaded()) publish_asset_use();
    if (now < asset_next_poll_) return;
    asset_next_poll_ = now + 5.0;
    if (asset_running_.load()) return;
    const fs::path out = turbo_output();
    if (!asset_started_) {
        asset_reload("start-up");
    } else if (assets::dir_signature(out) != asset_sig_) {
        asset_reload("catalog folder changed");
    }
}

const assets::DbUse& App::asset_use() {
    if (!db.ready()) {
        if (asset_use_built_) {
            asset_use_ = assets::DbUse{};
            asset_use_built_ = false;
        }
        return asset_use_;
    }
    if (asset_use_built_ && asset_use_.model_version == model.version()) return asset_use_;
    assets::DbUse u;
    u.model_version = model.version();
    using F = assets::Family;
    auto set = [&u](F f) -> std::unordered_set<int64_t>& { return u.used[static_cast<size_t>(f)]; };
    struct Col {
        const char* field;
        std::vector<std::unordered_set<int64_t>*> into;
    };
    // One snapshot per table; every listed field that exists goes into its sets (section 3.6 of the design)
    auto collect = [this](const char* table, const std::vector<Col>& cols) -> bool {
        const Table* t = db.table(table);
        if (!t) return false;
        std::vector<std::pair<const Field*, const Col*>> fs_;
        for (const auto& c : cols)
            if (t->has(c.field)) fs_.push_back({t->field(c.field), &c});
        if (fs_.empty()) return true;
        Snapshot s;
        if (!s.load(db.memory(), *t)) return false;
        for (uint32_t i : s.valid)
            for (const auto& [f, c] : fs_) {
                const int64_t v = s.get_int(i, *f);
                for (auto* dst : c->into) dst->insert(v);
            }
        return true;
    };
    collect("players", {
                           {"playerid", {&set(F::Miniface), &set(F::Callnames)}},
                           {"headassetid", {&set(F::Head3d), &set(F::Miniface), &set(F::Youthface)}},
                           {"hairtypecode", {&set(F::Hair)}},
                           {"facialhairtypecode", {&set(F::Facialhair)}},
                           {"shoetypecode", {&set(F::Boots)}},
                           {"smallsidedshoetypecode", {&set(F::Boots)}},
                           {"gkglovetypecode", {&set(F::Gkglove)}},
                           {"eyebrowcode", {&set(F::Eyebrow)}},
                           {"skinsurfacepack", {&set(F::Skinsurface)}},
                           {"tattoohead", {&set(F::Tattoo)}},
                           {"tattoofront", {&set(F::Tattoo)}},
                           {"tattooback", {&set(F::Tattoo)}},
                           {"tattooleftarm", {&set(F::Tattoo)}},
                           {"tattoorightarm", {&set(F::Tattoo)}},
                           {"tattooleftleg", {&set(F::Tattoo)}},
                           {"tattoorightleg", {&set(F::Tattoo)}},
                           {"accessorycode1", {&set(F::Accessory)}},
                           {"accessorycode2", {&set(F::Accessory)}},
                           {"accessorycode3", {&set(F::Accessory)}},
                           {"accessorycode4", {&set(F::Accessory)}},
                       });
    // heads_staff pictures: the game ships them under the managerid (and Turbo's older copies under the head id)
    collect("manager", {{"headassetid", {&set(F::Head3d), &set(F::Staffface)}}, {"managerid", {&set(F::Staffface)}}});
    collect("teams", {{"teamid", {&set(F::Crest)}}});
    collect("teamkits", {{"teamtechid", {&set(F::Kit)}}});
    collect("commentarynames", {{"commentaryid", {&u.commentary_ids}}});
    collect("playernamemap", {{"commentaryid", {&u.commentary_ids}}});
    u.has_tattoo_table = collect("tattoo", {{"tattooid", {&u.tattoo_table}}});
    asset_use_ = std::move(u);
    asset_use_built_ = true;
    return asset_use_;
}

bool App::log_asset_write(const std::string& target, const std::string& field, const std::string& old_value, const std::string& new_value) {
    assets::WriteLogEntry e;
    e.target = target;
    e.field = field;
    e.old_value = old_value;
    e.new_value = new_value;
    e.catalog_key = asset_catalog()->status().build_key;
    std::string err;
    const bool ok = assets::append_write_log(turbo_output(), e, &err);
    log("asset write: " + target + " " + field + " " + old_value + " -> " + new_value + (ok ? "" : " (not logged: " + err + ")"));
    return ok;
}

assets::Scope App::asset_scope(const std::string& picker) const {
    if (gui_settings.is_object() && gui_settings.contains("asset_scope") && gui_settings["asset_scope"].is_object()) {
        const json& s = gui_settings["asset_scope"];
        if (s.contains(picker) && s[picker].is_string()) {
            const std::string v = s[picker].get<std::string>();
            if (v == "in_save") return assets::Scope::InSave;
            if (v == "hidden") return assets::Scope::Hidden;
        }
    }
    return assets::Scope::All;
}

void App::set_asset_scope(const std::string& picker, assets::Scope s) {
    if (asset_scope(picker) == s) return;
    if (!gui_settings.is_object()) gui_settings = json::object();
    gui_settings["asset_scope"][picker] = s == assets::Scope::InSave ? "in_save" : s == assets::Scope::Hidden ? "hidden" : "all";
    if (!save_gui_settings()) notify("cannot write gui_settings.json", true);
}

// ---- the spoken set asked from the game's audio service (core/commentary_audio.h): a build on the game thread
static bool read_text_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// The manual override of the detected commentary language (gui_settings "callnames": {"override_language": "<lang>"});
// off unless set. The old "language" key (forced eng_us since 2026-10-09) is ignored: detection wins (callnames.h).
std::string App::chosen_commentary_language() const {
    if (gui_settings.is_object() && gui_settings.contains("callnames") && gui_settings["callnames"].is_object()) {
        const auto& c = gui_settings["callnames"];
        if (c.contains("override_language") && c["override_language"].is_string()) return c["override_language"].get<std::string>();
    }
    return "";
}

// A full audio-service build is the runtime evidence of the loaded language (callnames.h, step a): a changed answer
// refreshes everything (spoken set, masters, bank, pickers) before the set is cached under the language it belongs to
// The cache file follows the evidence, never the guess (core/callnames.h capture_target): a known answer is cached as
// spoken_<answer>.json (also under an override of another language: the conflict is logged), an unknown one nowhere.
bool App::detect_language_then_apply(const BankCapture& c, const std::string& when, const std::string& build, std::string* err) {
    if (!c.ok) return callnames.apply_capture(c, bridge.root(), when, build, err);
    callnames.game_build = build;
    if (callnames.detect_runtime_language(c.surnames, bridge.root(), &c.asked)) refresh_callnames();
    log("callnames: runtime language check: " + (callnames.runtime_lang.empty() ? std::string("unknown") : callnames.runtime_lang) + " (" +
        callnames.runtime_why + ")");
    if (callnames.runtime_lang.empty() && !callnames.runtime_off) callnames.runtime_inconclusive = true;  // no build again this session
    std::string note;
    const bool ok = callnames.apply_runtime_capture(c, bridge.root(), when, build, err, &note);
    if (!note.empty()) log("callnames: audio-service set: " + note);
    return ok;
}

// A probe's sample answer decides the language when it is clear (no full build); `asked` = the sample
bool App::detect_language_from_probe(const caudio::BuildResult& r) {
    if (!r.ok || !callnames.lang_only()) return false;
    std::unordered_set<int64_t> asked(r.asked.begin(), r.asked.end());
    const LanguageMatch m = match_loaded_language(r.names, callnames.language_candidates(bridge.root()), &asked);
    if (m.lang.empty()) {
        log("callnames: probe sample does not decide the language (" + m.why + "): a names-only build follows");
        return false;
    }
    callnames.game_build = hook_report ? hook_report().build : std::string();
    if (callnames.accept_runtime_match(m, bridge.root())) refresh_callnames();
    log("callnames: runtime language check (probe sample): " + m.lang + " (" + m.why + ")");
    return true;
}

void App::refresh_callnames() {
    if (hook_report) {
        const std::string b = hook_report().build;
        if (!b.empty()) callnames.game_build = b;
    }
    callnames.refresh(bridge.root(), game_root, chosen_commentary_language());
    std::string line = callnames.log_line();
    if (gui_settings.is_object() && gui_settings.contains("callnames") && gui_settings["callnames"].is_object() &&
        gui_settings["callnames"].contains("language"))
        line += " [gui_settings callnames.language is ignored: use callnames.override_language for a manual override]";
    if (!callnames.prev_lang.empty() && callnames.prev_lang != callnames.lang) line += " [changed from " + callnames.prev_lang + "]";
    log(line);
    if (log_hook) log_hook(line);
}

// The id list a build asks about. With the career database connected: commentarynames (+ the preview flag), the ids
// playernames / playernamemap use, every player; the list is cached in turbo_output\callnames\ids.json. Without it
// (main menu): that cache, else the commentary ids Lua exported to bridge_commentary.txt while it was connected.
bool App::spoken_ids(caudio::IdCache& out, std::string* why) {
    out = caudio::IdCache{};
    // a probe runs every few seconds: a list read from the database in the last 30 s (same generation) is reused, a
    // list read from a file is reused while the file's time stamp is the same
    const fs::path cp = caudio::id_cache_path(bridge.root());
    const fs::path lp = bridge.locate("bridge_commentary.txt");
    auto file_stamp = [](const fs::path& f) {
        std::error_code ec;
        const auto t = fs::last_write_time(f, ec);
        return ec ? std::string() : f.string() + "@" + std::to_string(t.time_since_epoch().count());
    };
    if (!spoken_ids_last_.empty()) {
        const bool same_db = db.ready() && spoken_ids_last_file_.empty() && spoken_ids_last_at_ >= 0.0 && now - spoken_ids_last_at_ < 30.0 &&
                             spoken_ids_last_gen_ == gen;
        const bool same_file = !db.ready() && !spoken_ids_last_file_.empty() &&
                               (spoken_ids_last_file_ == file_stamp(cp) || spoken_ids_last_file_ == file_stamp(lp));
        if (same_db || same_file) {
            out = spoken_ids_last_;
            spoken_ids_source = out.source;
            return true;
        }
    }
    auto remember = [&](const caudio::IdCache& c, const std::string& file) {
        spoken_ids_last_ = c;
        spoken_ids_last_at_ = now;
        spoken_ids_last_gen_ = gen;
        spoken_ids_last_file_ = file;
    };
    if (db.ready()) {
        std::unordered_set<int64_t> ids = commentary_ids();  // commentarynames, else the ids playernames uses
        if (!callnames.index.built) callnames.build_index(db, model, model.names_by_id());
        for (int64_t id : callnames.index.used_ids) ids.insert(id);
        for (const auto& kv : callnames.index.playernamemap)
            if (kv.second > kNoCallname && kv.second <= kCallnameMax) ids.insert(kv.second);
        // plus every language bank's generic ids (core/callname_bank.h): a name only another language records is asked too
        if (callnames.catalog)
            for (int64_t id : catalog_generic_union(*callnames.catalog))
                if (id > kNoCallname) ids.insert(id);
        out.names.assign(ids.begin(), ids.end());
        std::sort(out.names.begin(), out.names.end());
        if (const Table* t = db.table("commentarynames"); t && t->has("commentaryid") && t->has("commentarypreview")) {
            Snapshot s;
            if (s.load(db.memory(), *t)) {
                const Field& fid = *t->field("commentaryid");
                const Field& fpv = *t->field("commentarypreview");
                for (uint32_t i : s.valid) {
                    const int64_t id = s.get_int(i, fid);
                    if (id >= kCallnameMin && id <= kCallnameMax && s.get_int(i, fpv) == 1) out.preview.push_back(id);
                }
                std::sort(out.preview.begin(), out.preview.end());
            }
        }
        for (const auto& p : model.players()) out.players.push_back(p.playerid);
        std::sort(out.players.begin(), out.players.end());
        if (!out.names.empty()) {
            out.when = time_stamp();
            out.session = session;
            out.source = "the career database";
            spoken_ids_source = out.source;
            write_id_cache(out);
            remember(out, "");
            return true;
        }
    }
    // not connected: the cache written while connected, else Lua's commentary list
    std::string text, err;
    if (read_text_file(cp, text) && caudio::parse_id_cache_json(text, out, &err)) {
        out.source = "the id cache " + cp.filename().string() + (out.when.empty() ? "" : " (written " + out.when + ")");
        spoken_ids_source = out.source;
        remember(out, file_stamp(cp));
        return true;
    }
    if (read_text_file(lp, text) && caudio::parse_commentary_list(text, out.names, &out.session, &err)) {
        out.source = "Lua's " + lp.filename().string() + " (" + std::to_string(out.names.size()) + " commentary ids, no players)";
        spoken_ids_source = out.source;
        remember(out, file_stamp(lp));
        return true;
    }
    spoken_ids_source.clear();
    spoken_ids_last_ = caudio::IdCache{};
    if (why) *why = "no commentary id list at hand: connect to a career once (Turbo then caches the ids in " + cp.string() + ")";
    return false;
}

bool App::write_id_cache(const caudio::IdCache& c) {
    if (c.names.empty() || id_cache_written_ == c.names.size()) return false;
    const fs::path p = caudio::id_cache_path(bridge.root());
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << caudio::id_cache_json(c);
    id_cache_written_ = c.names.size();
    log("callnames: id list cached in " + p.string() + " (" + std::to_string(c.names.size()) + " commentary ids, " + std::to_string(c.players.size()) + " players)");
    return true;
}

bool App::start_spoken_build(bool automatic) {
    if (!commentary_audio) {
        spoken_build_status = "not available: no audio-service call in this build";
        return false;
    }
    caudio::BuildRequest req;
    caudio::IdCache ids;
    std::string why;
    if (!spoken_ids(ids, &why)) {
        spoken_build_status = "not started: " + why;
        if (!automatic) notify("Callnames: " + spoken_build_status, true);
        log("callnames: audio-service build " + spoken_build_status);
        return false;
    }
    req.names = ids.names;
    // only the loaded language is missing (the spoken set is verified): the names answer it, the ~20k player queries
    // are not needed (and the result is not cached over the full set)
    spoken_lang_only_build_ = automatic && callnames.lang_only();
    if (!spoken_lang_only_build_) req.players = ids.players;
    std::string err;
    if (!commentary_audio->request(req, &err)) {
        spoken_build_status = "not started: " + err;
        if (!automatic) notify("Callnames: " + spoken_build_status, true);
        log("callnames: audio-service build " + spoken_build_status);
        if (automatic) spoken_watch.refused(now, err);
        return false;
    }
    spoken_watch.started(now, false);
    if (automatic) spoken_auto_tried = true;
    char line[300];
    std::snprintf(line, sizeof(line), "asking the game's audio service about %zu commentary ids and %zu players (from %s)%s%s...", req.names.size(),
                  req.players.size(), ids.source.c_str(), automatic ? " (automatic)" : "", spoken_lang_only_build_ ? " (language check only)" : "");
    spoken_build_status = line;
    log("callnames: " + spoken_build_status);
    return true;
}

bool App::start_spoken_probe() {
    if (!commentary_audio) return false;
    caudio::IdCache ids;
    std::string why;
    if (!spoken_ids(ids, &why)) {
        spoken_watch.refused(now, why);
        return false;
    }
    caudio::BuildRequest req;
    req.probe = true;
    req.names = caudio::probe_sample(ids.names, ids.preview);
    req.batch_start = req.batch_max = std::max<size_t>(req.names.size(), 1);  // one step
    std::string err;
    if (!commentary_audio->request(req, &err)) {
        spoken_watch.refused(now, err);
        return false;
    }
    spoken_watch.started(now, true);
    return true;
}

void App::spoken_watch_tick() {
    if (!commentary_audio) return;
    // the language and the cached set are needed before the Callname tab was ever opened (a build from the main menu)
    if (!callnames.refreshed && !game_root.empty()) refresh_callnames();
    // the language-check kill switch is live: re-checked every 2 s, not only at a refresh
    if (now >= lang_off_check_) {
        lang_off_check_ = now + 2.0;
        std::error_code ec;
        const bool off = fs::exists(lang_detect_off_path(bridge.root()), ec);
        if (off != callnames.runtime_off) {
            callnames.runtime_off = off;
            log(std::string("callnames: runtime language check ") + (off ? "switched off (" : "switched on again (") +
                lang_detect_off_path(bridge.root()).string() + ")");
            if (!off) {
                callnames.refreshed = false;  // re-detect from the saved answer / packs
                if (callnames.runtime_lang.empty()) {
                    callnames.runtime_checked = false;  // the switch answered for the game: ask it again
                    callnames.runtime_why.clear();
                }
            }
        }
    }
    caudio::SpokenWatch::Inputs in;
    const caudio::ServiceStatus st = commentary_audio->status();
    in.service = st.installed;
    in.available = st.available && !st.busy;
    in.verified = callnames.settled();  // also asks the game once per session which language it loaded
    in.lang_only = callnames.lang_only();
    if (!in.service) in.why_not = st.reason.empty() ? "the audio-service call is not available" : st.reason;
    // whether an id list is at hand is re-checked every few seconds (a file read when not connected); meanwhile the
    // last answer stands
    if (in.service && !in.verified && (spoken_ids_check_ < 0.0 || now >= spoken_ids_check_)) {
        caudio::IdCache ids;
        std::string why;
        spoken_have_ids_ = spoken_ids(ids, &why);
        if (!spoken_have_ids_) in.why_not = why;
        spoken_ids_check_ = now + (spoken_have_ids_ ? 30.0 : 5.0);
    } else if (!spoken_have_ids_ && in.service && !in.verified) {
        in.why_not = "no commentary id list at hand: connect to a career once";
    }
    in.have_ids = spoken_have_ids_;
    switch (spoken_watch.tick(now, in)) {
        case caudio::SpokenWatch::Action::Probe:
            start_spoken_probe();
            break;
        case caudio::SpokenWatch::Action::Build:
            log("callnames: the game answered the probe: the bank is bound in this screen, building the spoken set");
            start_spoken_build(true);
            break;
        default:
            break;
    }
}

void App::finish_spoken_build() {
    if (!commentary_audio) return;
    caudio::BuildResult r;
    if (!commentary_audio->poll(r)) return;
    if (r.probe) {
        // a probe: never cached, never announced; the watcher decides what comes next
        char clock[16];
        std::time_t t = std::time(nullptr);
        const std::tm* tmv = std::localtime(&t);
        if (!tmv || std::strftime(clock, sizeof(clock), "%H:%M:%S", tmv) == 0) clock[0] = 0;
        spoken_watch.last_probe_clock = clock;
        spoken_watch.on_result(now, r);
        detect_language_from_probe(r);  // a clear sample answer settles the watcher: no build follows
        return;
    }
    if (spoken_lang_only_build_) {
        // a names-only build asked for the language alone: detect, never cache (it holds no players)
        spoken_lang_only_build_ = false;
        spoken_watch.on_result(now, r);
        if (r.ok) {
            BankCapture c = caudio::to_capture(r);
            callnames.game_build = hook_report ? hook_report().build : std::string();
            if (callnames.detect_runtime_language(c.surnames, bridge.root(), &c.asked)) refresh_callnames();
            if (callnames.runtime_lang.empty() && !callnames.runtime_off) callnames.runtime_inconclusive = true;
            log("callnames: runtime language check (names-only build, not cached): " +
                (callnames.runtime_lang.empty() ? std::string("unknown") : callnames.runtime_lang) + " (" + callnames.runtime_why + ")");
        } else {
            log("callnames: language-check build: " + r.note);
        }
        return;
    }
    spoken_watch.on_result(now, r);
    const std::string when = time_stamp();
    std::string build = hook_report ? hook_report().build : std::string();
    BankCapture c = caudio::to_capture(r);
    std::string err;
    char line[400];
    std::snprintf(line, sizeof(line), "%.1f s, %zu steps: %s", c.seconds, c.steps, r.note.c_str());
    if (r.cancelled) {
        spoken_build_status = "build cancelled";
    } else if (!r.ok) {
        spoken_build_status = line;
        if (!r.unbound) notify("Callnames: " + r.note, true);  // an unbound bank is the watcher's business: no toast
    } else if (!detect_language_then_apply(c, when, build, &err)) {
        spoken_build_status = std::string(line) + " - not used: " + err;
        notify("Callnames: audio-service set not used: " + err, true);
    } else {
        spoken_build_status = line;
        std::snprintf(line, sizeof(line), "Callnames: spoken set from the game's audio service: %zu names, %zu player callnames", c.surnames.size(),
                      c.players.size());
        notify(line);
        ++gen;  // the Callname tab rebuilds its pickers
    }
    log("callnames: audio-service build " + spoken_build_status);
}

bool App::refresh() {
    next_retry = now + 5.0;
    db_error.clear();
    if (!bridge.meta_loaded()) {
        if (!bridge.meta_error().empty())
            db_error = bridge.meta_error();
        else if (!bridge.state().meta_error.empty())
            db_error = "Turbo cannot read the game database: " + bridge.state().meta_error;
        else
            db_error = "Open your career in the game; Turbo connects by itself.";
        return false;
    }
    const auto& st = bridge.state();
    std::string err;
    int n = db.refresh(st.db_service, bridge.meta(), &err);
    seen_service = st.db_service;
    if (n == 0) {
        db_error = err;
        return false;
    }
    seen_names_ = bridge.names();
    model.set_extra_names(seen_names_);
    // clubs stored as an unresolved key ("*TeamName_Abbr15_<id>") show Live Editor's custom team name, else "Team <id>"
    model.set_team_name_fallback(load_readable_team_names(bridge.root()));
    model.rebuild(today());
    ++gen;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "database connected: %d tables, %zu players, %zu teams", n, model.players().size(),
                  model.teams().size());
    log(buf);
    // FC 27 reloads teamkits and playernamemap from its base data at every career load: the kept edits go back in once
    // per newly loaded career (ui_reapply.cpp)
    maybe_reapply();
    return true;
}

void App::tick(double t) {
    now = t;
    update_style();
    if (!legacy_repaired_) {
        // once, before the game shows a crest: custom files with a wrong mip count crash it (legacy.h)
        legacy_repaired_ = true;
        for (const auto& line : legacy.repair_dds_files()) {
            log(line);
            if (log_hook) log_hook(line);
        }
    }
    legacy.tick(t);
    if (!visible) legacy.not_prompting();
    {
        // Lua went quiet while pictures on screen wait: said once in the log (the boxes say it on screen)
        static bool stalled_logged = false;
        const bool stalled = legacy.stalled();
        if (stalled && !stalled_logged) {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "pictures: %zu asked from the game, no answer from Turbo's Lua side for %.0f s (Lua: %s)",
                          legacy.waiting_on_screen(), LegacyImages::kStallSeconds,
                          legacy.lua_status().empty() ? "no status yet" : legacy.lua_status().c_str());
            log(buf);
            if (log_hook) log_hook(buf);
        }
        stalled_logged = stalled;
    }
    // Background loading: from the first show (F8) or the first career connection, whichever comes first
    if (preloader) {
        if (!preloader->started() && (visible || connected())) preload_start(visible ? "first show" : "career connected");
        preloader->tick(*this);
    }

    edit_unlock_tick(*this);  // Game editors: originals in, unlocked files out (ui_edit_unlock.cpp)
    live_tactic_tick(*this);  // Tactics channel E: job results, the read-back 5 s after a write, the kick-off check (ui_live_tactic.cpp)
    tactic_presets_tick(*this);  // Tactic presets: a load / undo finishes when its channel E job came back (ui_tactic_presets.cpp)
    tuning_tick(*this);  // Gameplay tuning: job results, the undo file, the re-apply of kept values at a career load (ui_tuning.cpp)
    // Voice swaps: the store goes to the service the host gave (once per service, then after every edit); the kill
    // switches are re-read every 2 s (the detours only read cached atomics)
    if (voice_service) {
        if (voice_published_to_ != voice_service) voice_publish();
        if (t >= voice_next_switches_) {
            voice_next_switches_ = t + 2.0;
            voice_service->refresh_switches();
        }
    }
    // Live team names: the same pattern (published once per service, then after every save; switches every 2 s)
    if (team_names_service) {
        if (team_names_published_to_ != team_names_service) team_names_publish();
        if (t >= team_names_next_switches_) {
            team_names_next_switches_ = t + 2.0;
            team_names_service->refresh_switches();
        }
    }
    finish_bank_capture();
    finish_spoken_build();
    spoken_watch_tick();
    asset_tick();  // game asset catalog: first load, reload when its folder changes (worker thread)
    if (t >= next_poll) {
        next_poll = t + 0.5;
        bool changed = bridge.poll_files();
        const auto& st = bridge.state();
        // A game call Lua queued on the game thread finished (core/game_calls.h): Lua reports it in bridge_state.json
        if (changed && st.loaded) {
            if (game_call_seen < 0) game_call_seen = st.game_call_seq;  // outcomes from before this window started
            else if (st.game_call_seq != game_call_seen) {
                game_call_seen = st.game_call_seq;
                if (!st.game_call_text.empty()) {
                    notify("Game call: " + st.game_call_text, !st.game_call_ok);
                    job_offer_status = st.game_call_text;
                }
            }
        }
        // Rebuild lists only when the database itself may have moved (save loaded, career entered/left),
        // not on every in-game day.
        if (changed && bridge.meta_loaded() && st.loaded &&
            (st.db_gen != seen_db_gen || st.db_service != seen_service || !db.ready())) {
            seen_db_gen = st.db_gen;
            // A full re-read takes a moment on a big database: while the window is hidden it waits
            // until the window is shown again (the first connection is made right away), unless kept edits wait
            // for the newly loaded career (kit colours, callnames: they must be back before the first match)
            if (visible || !db.ready() || reapply_due()) refresh();
            else refresh_pending = true;
        }
    }
    // A standings refresh queued after a live table edit finished on the game thread (core/standings_refresh.h)
    if (standings_refresh) {
        svm::Result r;
        while (standings_refresh->poll(r)) {
            standings_refresh_status = (r.ok ? (r.warning ? "warning: " : "") : "failed: ") + r.message;
            notify("Standings refresh: " + r.message, !r.ok || r.warning);
            log("standings refresh [" + r.stage + (r.warning ? ", warning" : "") + "]: " + r.message);
        }
    }
    // A game-variable set / clear ran on the game thread (core/match_setup.h)
    if (match_setup) {
        msetup::VarResult r;
        while (match_setup->poll(r)) {
            const std::string text = usertext::match_var_outcome(r.ok, r.cleared, r.name, r.value, r.message);
            match_setup_status = (r.ok ? "" : "failed: ") + text;
            notify("Match setup: " + text, !r.ok);
            log("match setup: " + r.message);
        }
    }
    // Player names decoded by Live Editor arrived or changed: rebuild the lists (now if shown, else when shown)
    if (db.ready() && bridge.names() != seen_names_) {
        seen_names_ = bridge.names();
        model.set_extra_names(seen_names_);
        model_stale = true;
    }
    if (refresh_due > 0.0 && t >= refresh_due) {
        refresh_due = 0.0;
        refresh_pending = true;
    }
    if (visible && refresh_pending) {
        refresh_pending = false;
        model_stale = false;
        refresh();
    }
    // Not connected although Lua reported the database (e.g. it was still loading): try again every 5 s
    if (!db.ready() && bridge.meta_loaded() && bridge.state().loaded && t >= next_retry) refresh();
    if (visible && model_stale && db.ready()) {
        model_stale = false;
        model.rebuild(today());
        ++gen;
    }
    if (mailbox && t >= next_publish) {
        next_publish = t + 2.0;
        bridge.publish_mailbox(mailbox_addr_, session, kGuiVersion);
    }
    if (mailbox) {
        int hb = mailbox->heartbeat();
        if (hb != lua_heartbeat_last) {
            lua_heartbeat_last = hb;
            lua_heartbeat_seen_at = t;
        }
        bool ok = false;
        std::string result;
        if (mailbox->take_result(ok, result)) {
            const std::string pending_before = pending_label;
            const bool was_pack = pending_label.rfind("Season pack", 0) == 0;
            // the development plan after the window's own edits (flush_plan_sync): logged; a toast only when it failed
            const bool plan_sync = pending_label.rfind("Development plan (", 0) == 0;
            const std::string text = pending_label + ": " + (result.empty() ? (ok ? "done" : "failed") : result);
            // a result with per-player lines (squad roles) toasts its first line; the log keeps all of it and Teams > Mass
            // actions shows all of it in place
            const size_t nl = result.find('\n');
            if (plan_sync) {
                plan_sync_status = time_stamp() + ": " + (ok ? "" : "FAILED: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
                if (ok) log(text);
                else notify(text, true);
                if (log_hook) log_hook("development plan: " + plan_sync_status);
            } else {
                if (nl != std::string::npos) log(text);
                notify(pending_label + ": " + (result.empty() ? (ok ? "done" : "failed")
                                               : nl == std::string::npos ? result
                                               : result.substr(0, nl) + (pending_label.rfind("Mass actions", 0) == 0 ? " (each player: under the Mass actions buttons)"
                                                                                                                       : " (each player: turbo_output\\turbo_gui.log)")),
                       !ok);
            }
            if (pending_label.rfind("Job offer", 0) == 0)  // Managers > Job offers shows the outcome in place
                job_offer_status = (ok ? "" : "Failed: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            if (pending_label.rfind("Mass actions", 0) == 0)  // Teams > team edit > Mass actions
                mass_status = (ok ? "" : "Not everything was done: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            if (pending_label.rfind("Squad roles ", 0) == 0)  // the role rule editor (the preview file is read here, once)
                rolerules::on_result(*this, pending_label, ok, result);
            if (pending_label.rfind("Manager rules", 0) == 0)  // Managers > Manager rules shows the outcome in place
                manager_rules_status = (ok ? "" : "Failed: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            if (pending_label.rfind("Manager move", 0) == 0)  // Managers > Manager market
                manager_move_status = (ok ? "" : "Failed: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            pending_label.clear();
            if (was_pack) season_pack_on_result(*this, ok, result);   // the next queued command, or the end of the run
            flush_lua_queue();  // the mailbox is free: the next queued "keep shown name" actions
            // Lua may have changed the database (transfers, bulk edits): re-read the lists (a plan sync writes no table)
            if (db.ready() && !plan_sync) model_stale = true;
            // a new player's name row is read a moment after the result: one more full refresh
            if (ok && db.ready() && (pending_before.rfind("Create player", 0) == 0 || pending_before.rfind("Clone player", 0) == 0 ||
                                     pending_before.rfind("Import", 0) == 0 || pending_before.rfind("Repair names", 0) == 0))
                refresh_due = t + 1.5;
        }
    }
    flush_lua_queue();
    flush_plan_sync();  // the window's edits of your players into their development plans (after a quiet second)
    // player edits kept for every career load: one save per frame however many fields an action wrote
    if (reapply_save_due_) {
        reapply_save_due_ = false;
        save_reapply();
    }
    for (size_t i = 0; i < toasts.size();) {
        if (toasts[i].until < t) toasts.erase(toasts.begin() + static_cast<long>(i));
        else ++i;
    }
}

bool overall_follow_edit(App& app, const Table& t, uint64_t rec, const Field& f, const Value& v);  // ui_players.cpp (2.1)

bool App::edit(const Table& t, uint64_t rec, const Field& f, const Value& v) {
    if (t.name == "players" && f.name == "overallrating" && f.type == FieldType::Int && v.type == FieldType::Int) return set_overall(t, rec, v.i);
    if (t.name == "players") return overall_follow_edit(*this, t, rec, f, v);  // an attribute: the overall follows it
    return edit_field(t, rec, f, v);
}

bool App::edit_field(const Table& t, uint64_t rec, const Field& f, const Value& v, bool undo_step) {
    std::string err;
    Value before;
    bool have_before = db.get(t, rec, f, before);
    if (!db.set(t, rec, f, v, &err)) {
        notify(labels::field_title(f.name) + ": " + err, true);
        return false;
    }
    if (t.name == "players" || t.name == "editedplayernames") {
        // editedplayernames (Players > Names, Callname): the player's shown name; its edits get undo steps too
        int64_t pid = db.get_int(t, rec, "playerid", 0);
        if (t.name == "players") model.refresh_player(pid, today());
        if (t.name == "editedplayernames" || f.name == "firstnameid" || f.name == "lastnameid" || f.name == "commonnameid")
            model.refresh_player_name(pid);
        if (have_before && !(before == v) && undo_step) {
            auto& steps = undo_[pid];
            steps.push_back({t.name, rec, f.name, before, {}, {}});
            ++undo_pushes;
            while (steps.size() > kUndoSteps) steps.pop_front();
        }
        // kept for every career load, and into his development plan (ui_reapply.cpp, flush_plan_sync)
        if (t.name == "players" && have_before) player_field_written(pid, f.name, before, v);
    } else if (t.name == "teams") {
        model.refresh_team(db.get_int(t, rec, "teamid", 0));
    } else if (t.name == "teamplayerlinks") {
        model.reload_links();
    }
    ++gen;
    // the log says what changed in words; the game's own name and value follow in brackets for bug reports
    log((have_before ? describe_edit(t, rec, f, before, v) : labels::field_title(f.name) + " set to " + describe_value(t, f, v)) + "  [" +
        t.name + "." + f.name + " = " + v.to_string() + "]");
    return true;
}

std::string App::nation_name(int64_t nationid) { return geo(*this).nation_name(nationid); }

std::string App::describe_value(const Table& t, const Field& f, const Value& v) {
    if (v.type == FieldType::String) return v.s.empty() ? "(empty)" : v.s;
    if (v.type != FieldType::Int) return v.to_string();
    if (f.name == "nationality" && (t.name == "players" || t.name == "manager")) {
        const std::string n = nation_name(v.i);
        return n.empty() ? "Nation " + std::to_string(v.i) : n;
    }
    if ((f.name == "rivalteam" && t.name == "teams") || (f.name == "teamid" && t.name == "manager")) return model.team_name(v.i);
    if (f.name == "birthdate" || f.name == "playerjointeamdate") {
        const GameDate d = date_from_gregorian_days(v.i);
        if (!d.valid()) return std::to_string(v.i);
        char b[24];
        std::snprintf(b, sizeof(b), "%04d-%02d-%02d", d.year, d.month, d.day);
        return b;
    }
    if (labels::is_position_field(t.name, f.name)) return labels::describe(t.name, f.name, v.i);  // formations.position0 is the GK slot, not a switch
    if (f.depth == 1 && f.min == 0) return v.i ? "Yes" : "No";  // a switch
    return labels::describe(t.name, f.name, v.i);
}

std::string App::describe_edit(const Table& t, uint64_t rec, const Field& f, const Value& before, const Value& after) {
    std::string subject;
    if (t.name == "players" || t.name == "editedplayernames") subject = model.player_name(db.get_int(t, rec, "playerid", 0));
    else if (t.name == "teams") subject = model.team_name(db.get_int(t, rec, "teamid", 0));
    else if (t.name == "teamplayerlinks") subject = model.player_name(db.get_int(t, rec, "playerid", 0));
    else if (t.name == "manager") {
        const int64_t mid = db.get_int(t, rec, "managerid", -1);
        for (const auto& m : model.managers())
            if (m.managerid == mid) subject = m.name;
    }
    return labels::describe_change(f.name, describe_value(t, f, before), describe_value(t, f, after), subject);
}

size_t App::undo_count(int64_t playerid) const {
    auto it = undo_.find(playerid);
    return it == undo_.end() ? 0 : it->second.size();
}

bool App::undo(int64_t playerid) {
    auto it = undo_.find(playerid);
    if (it == undo_.end() || it->second.empty()) return false;
    UndoStep step = it->second.back();
    it->second.pop_back();
    const Table* t = db.table(step.table);
    if (!t || !t->field(step.field) || !db.table_alive(*t, step.rec)) {
        notify("undo: the database changed (save loaded?) - press Refresh", true);
        return false;
    }
    // the fields of the same change first (a typed overall's attributes), the step's own field last
    std::vector<std::pair<std::string, Value>> writes = step.also;
    writes.push_back({step.field, step.before});
    size_t done = 0;
    for (const auto& w : writes) {
        const Field* f = t->field(w.first);
        std::string err;
        Value now;
        const bool have_now = f && db.get(*t, step.rec, *f, now);
        if (!f || !db.set(*t, step.rec, *f, w.second, &err)) {
            notify("undo " + w.first + ": " + (f ? err : std::string("no such field")), true);
            continue;
        }
        ++done;
        if (t->name == "players" && have_now) player_field_written(playerid, w.first, now, w.second);
    }
    model.refresh_player(playerid, today());
    model.refresh_player_name(playerid);
    ++gen;
    log("undo " + step.table + "." + step.field + " = " + step.before.to_string() +
        (step.also.empty() ? std::string() : " (and " + std::to_string(step.also.size()) + " more fields)"));
    if (!done) return false;
    // the pictures of the same change (a head given with its miniface): back from the backup, or Turbo's file removed
    size_t pics = 0;
    for (const UndoStep::Picture& p : step.pictures) {
        std::string err;
        bool ok = false;
        if (p.backup.empty()) {
            ok = legacy.custom_file(p.path).empty() || legacy.remove_custom(p.path, &err);
        } else {
            std::ifstream in(p.backup, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.empty()) err = "the backup " + p.backup.string() + " is missing or empty";
            else ok = legacy.save_custom(p.path, bytes, &err);
        }
        textures.forget(legacy.custom_file(p.path));
        if (ok) {
            ++pics;
            log("undo picture " + p.path + (p.backup.empty() ? std::string(": Turbo's file removed (the game's own shows)")
                                                              : ": back from " + p.backup.string()));
        } else {
            notify("undo of the picture " + p.path + " failed: " + err + " (the head fields are back)", true);
        }
    }
    if (!step.pictures.empty()) {
        notify("undone: the head (" + std::to_string(step.also.size() + 1) + " fields)" + (pics ? std::string(" and the miniface") : std::string()) +
               " back as before");
        return true;
    }
    notify("undone: " + field_label(step.field) + " back to " + describe_value(*t, *t->field(step.field), step.before) +
           (step.also.empty() ? std::string()
            : step.also.size() == 1 && step.also[0].first == "overallrating" ? std::string(" (and the overall it moved)")
                                                                             : " (and " + std::to_string(step.also.size()) + " attributes)"));
    return true;
}

int64_t App::head_miniface_mismatch(int64_t playerid) {
    const Table* t = db.table("players");
    const PlayerRow* p = model.player(playerid);
    if (!t || !p) return 0;
    const int64_t head = db.get_int(*t, p->rec, "headassetid", 0);
    if (db.get_int(*t, p->rec, "headclasscode", 1) != 0 || head <= 0 || head == playerid) return 0;
    if (!model.player(head)) return 0;  // not another player's own head (a legend, an old id): nothing to compare
    if (!legacy.custom_file(legacy_path::player_miniface(playerid)).empty()) return 0;  // his own picture was written
    auto k = head_miniface_kept.find(playerid);
    if (k != head_miniface_kept.end() && k->second == head) return 0;
    return head;
}

bool App::give_matching_miniface(int64_t playerid, std::string* msg) {
    auto say = [&](const std::string& m) {
        if (msg) *msg = m;
    };
    const int64_t head = head_miniface_mismatch(playerid);
    if (!head) {
        say("nothing to do: his head and miniface already go together");
        return false;
    }
    const std::string src = legacy_path::player_miniface(head), dst = legacy_path::player_miniface(playerid);
    fs::path file;
    const LegacyImages::State st = legacy.locate(src, &file);
    if (st == LegacyImages::State::Waiting) {
        say("the picture of head " + std::to_string(head) + " is still loading: click again in a moment (nothing was written)");
        return false;
    }
    if (st != LegacyImages::State::Custom && st != LegacyImages::State::Game) {
        head_miniface_kept[playerid] = head;
        say("the game has no picture for head " + std::to_string(head) + ": his old miniface stays");
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err;
    fs::path backup;
    if (bytes.empty() || !legacy.save_custom(dst, bytes, &err, &backup)) {
        say("his miniface could not be written: " + (bytes.empty() ? std::string("empty picture") : err));
        return false;
    }
    textures.forget(legacy.custom_file(dst));
    // the picture joins the head change's undo step: one Undo puts both back
    auto u = undo_.find(playerid);
    if (u != undo_.end() && !u->second.empty()) u->second.back().pictures.push_back({dst, backup});
    log("miniface " + dst + " now shows " + src + " (matches his head " + std::to_string(head) + ")");
    say("his miniface now shows " + fs::path(src).filename().string() + ", the picture of his new head (Undo puts both back)");
    return true;
}

void App::group_undo(int64_t playerid, size_t n, std::vector<UndoStep::Picture> pictures) {
    auto it = undo_.find(playerid);
    if (it == undo_.end() || it->second.empty()) {
        if (pictures.empty()) return;
        // no field step to hang them on (nothing changed in the table): the pictures alone would be an odd undo; keep none
        return;
    }
    auto& steps = it->second;
    n = std::min(n, steps.size());
    if (n == 0) return;
    // the oldest of the n steps becomes the step; the others go into `also` (all on the same record: one head, one player)
    UndoStep merged = steps[steps.size() - n];
    for (size_t k = steps.size() - n + 1; k < steps.size(); ++k) {
        const UndoStep& s = steps[k];
        if (s.table != merged.table || s.rec != merged.rec) continue;  // a different record never joins (defensive)
        merged.also.push_back({s.field, s.before});
        merged.also.insert(merged.also.end(), s.also.begin(), s.also.end());
    }
    for (auto& p : pictures) merged.pictures.push_back(std::move(p));
    steps.erase(steps.end() - static_cast<std::ptrdiff_t>(n), steps.end());
    steps.push_back(std::move(merged));
}

void App::push_bulk_undo(BulkUndo u) {
    if (u.before.empty()) return;
    bulk_undo.push_back(std::move(u));
    while (bulk_undo.size() > kBulkUndo) bulk_undo.erase(bulk_undo.begin());
}

bool App::set_overall(const Table& t, uint64_t rec, int64_t target) {
    const Field* of = t.field("overallrating");
    if (!of) return false;
    overall::Attrs cur{};
    for (int i = 0; i < overall::kAttrCount; ++i) {
        const Field* af = t.field(overall::kAttrNames[i]);
        // not FC 27's players table (no 34 attributes): nothing to move, the overall alone
        if (!af || af->type != FieldType::Int) return edit_field(t, rec, *of, Value::of_int(target));
        cur[static_cast<size_t>(i)] = static_cast<int>(db.get_int(t, rec, overall::kAttrNames[i], 0));
    }
    const int64_t pid = db.get_int(t, rec, "playerid", 0);
    const int64_t stored = db.get_int(t, rec, "overallrating", 0);
    const int pos = static_cast<int>(db.get_int(t, rec, "preferredposition1", 14));
    char msg[256];
    overall::Attrs want = cur;
    if (target < 1 || target > 99 || !overall::reach(pos, want, static_cast<int>(target))) {
        std::snprintf(msg, sizeof(msg),
                      "Overall %lld not written: %s (FC 27 works the overall out from the attributes, so Turbo moves them to give it)",
                      static_cast<long long>(target),
                      target < 1 || target > 99 ? "the game's overall is 1..99" : "his attributes for his position are at their limits");
        notify(msg, true);
        return false;
    }
    UndoStep step{t.name, rec, "overallrating", Value::of_int(stored), {}, {}};
    BulkUndo bulk;
    bulk.playerid = pid;
    bulk.label = "overall " + std::to_string(stored) + " -> " + std::to_string(target);
    int moved = 0;
    bool failed = false;
    for (int i = 0; i < overall::kAttrCount && !failed; ++i) {
        const size_t k = static_cast<size_t>(i);
        if (want[k] == cur[k]) continue;
        const Field* af = t.field(overall::kAttrNames[i]);
        if (!edit_field(t, rec, *af, Value::of_int(want[k]), false)) {
            failed = true;
            break;
        }
        step.also.push_back({af->name, Value::of_int(cur[k])});
        bulk.before.push_back({af->name, cur[k]});
        ++moved;
    }
    if (!failed && stored != target && !edit_field(t, rec, *of, Value::of_int(target), false)) failed = true;
    if (failed) {
        // a write was refused (toast by edit_field): what this change wrote goes back
        for (const auto& b : step.also)
            if (const Field* af = t.field(b.first)) edit_field(t, rec, *af, b.second, false);
        return false;
    }
    if (stored != target) bulk.before.push_back({"overallrating", stored});
    if (!bulk.before.empty()) {
        auto& steps = undo_[pid];
        steps.push_back(std::move(step));
        ++undo_pushes;
        while (steps.size() > kUndoSteps) steps.pop_front();
        push_bulk_undo(std::move(bulk));
    }
    if (moved)
        std::snprintf(msg, sizeof(msg), "Overall %lld -> %lld: %d attributes moved so that the game's own overall gives it (Undo puts them back)",
                      static_cast<long long>(stored), static_cast<long long>(target), moved);
    else
        std::snprintf(msg, sizeof(msg), "Overall %lld -> %lld (his attributes already give it)", static_cast<long long>(stored),
                      static_cast<long long>(target));
    notify(msg);
    return true;
}

// ---- development plans (features/development.lua mode plan_sync)
bool App::plan_sync_wanted(int64_t playerid) {
    const BridgeState& st = bridge.state();
    if (!st.in_cm || !mailbox || playerid <= 0) return false;
    if (st.unavailable_reason("development")) return false;  // no plan natives in this Live Editor build
    std::error_code ec;
    if (fs::exists(bridge.root() / "turbo_output" / "plan_sync_off.txt", ec)) return false;  // kill switch
    if (st.user_team <= 0) return true;                       // the club is not known: Lua asks the game per player
    for (const LinkRow& l : model.links_of_player(playerid))
        if (l.teamid == st.user_team) return true;
    // lent out by your club: the plan may stay with him
    if (const Table* lt = db.table("playerloans"))
        if (uint64_t r = db.find(*lt, "playerid", playerid)) return db.get_int(*lt, r, "teamidloanedfrom", 0) == st.user_team;
    return false;
}

void App::queue_plan_sync(int64_t playerid, const std::string& field) {
    if (!plan_field(field) || !plan_sync_wanted(playerid)) return;
    const std::string key = reapply_key();
    if (!plan_queue_.empty() && plan_queue_key_ != key) {
        // written before this career load and not sent yet: that load's save has what it has (the re-apply that
        // runs at the load queues the kept fields again)
        log("development plan: " + std::to_string(plan_queue_.size()) + " player(s)' edits not sent: another career load came first");
        plan_queue_.clear();
    }
    if (plan_queue_.empty()) plan_queue_key_ = key;
    plan_queue_[playerid].insert(field);
    plan_queue_at_ = now;
}

void App::flush_plan_sync() {
    if (plan_queue_.empty()) return;
    if (!bridge.state().in_cm || reapply_key() != plan_queue_key_) {
        // the fields were written into a career that is no longer loaded: its save has what it has
        log("development plan: " + std::to_string(plan_queue_.size()) + " player(s)' edits not sent: the career was left or another one loaded");
        plan_queue_.clear();
        return;
    }
    if (now - plan_queue_at_ < kPlanSyncQuiet || !mailbox || mailbox->pending()) return;
    json sync = json::array();
    size_t size = 0, fields = 0;
    std::vector<int64_t> sent;
    for (const auto& kv : plan_queue_) {
        json e = {{"playerid", kv.first}, {"fields", json(kv.second)}};
        const size_t len = e.dump().size() + 1;
        if (!sync.empty() && size + len > kMbTextSize - 256) break;  // the rest goes with the next command
        size += len;
        fields += kv.second.size();
        sync.push_back(std::move(e));
        sent.push_back(kv.first);
    }
    const std::string label = "Development plan (" + std::to_string(sent.size()) + (sent.size() == 1 ? " player, " : " players, ") +
                              std::to_string(fields) + (fields == 1 ? " field)" : " fields)");
    if (!send({{"op", "run"}, {"module", "development"}, {"overrides", {{"mode", "plan_sync"}, {"sync", sync}}}}, label)) {
        // the command channel refused it (toast by send): not tried again every frame
        plan_sync_status = "not sent: Turbo's command channel refused the command";
        plan_queue_.clear();
        return;
    }
    for (int64_t pid : sent) plan_queue_.erase(pid);
}

void App::player_field_written(int64_t playerid, const std::string& field, const Value& before, const Value& after) {
    if (playerid <= 0 || before.type != FieldType::Int || after.type != FieldType::Int || before.i == after.i) return;
    // outside a career (a squads file): no development plan, and nothing a career load would put back
    if (!bridge.state().in_cm) return;
    if (reapply.keep_players && kept_player_field(field)) {
        const PlayerRow* p = model.player(playerid);
        if (reapply.note_player_edit(playerid, p ? p->name : std::string(), time_stamp(), field, before.i, after.i)) reapply_save_due_ = true;
    }
    queue_plan_sync(playerid, field);
}

bool App::busy() { return mailbox && mailbox->pending(); }

void App::flush_lua_queue() {
    if (lua_queue.empty() || !mailbox || mailbox->pending()) return;
    // the command wraps the actions: {"op":"run","module":"callnames","overrides":{"actions":[...]}} (kMbTextSize max)
    size_t n = 0;
    const std::string arr = lua_queue.batch(kMbTextSize - 128, n);
    json actions = json::parse(arr, nullptr, false);
    if (!n || actions.is_discarded()) {
        lua_queue.pop(n ? n : 1);
        return;
    }
    const std::string label = "Keep shown name" + (n > 1 ? " (" + std::to_string(n) + " players)" : std::string());
    if (send({{"op", "run"}, {"module", "callnames"}, {"overrides", {{"actions", actions}}}}, label)) lua_queue.pop(n);
}

bool App::send(const json& cmd, const std::string& label) {
    if (!mailbox) {
        notify("Turbo's command channel is not available", true);
        return false;
    }
    std::string err;
    if (!mailbox->submit(cmd.dump(), &err)) {
        notify(label + ": " + err, true);
        return false;
    }
    pending_label = label;
    pending_since = now;
    log("sent: " + cmd.dump());
    return true;
}

void App::load_gui_settings() {
    gui_settings = json::object();
    std::ifstream f(bridge.root() / "turbo_output" / "gui_settings.json", std::ios::binary);
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        json j = json::parse(ss.str(), nullptr, false);
        if (!j.is_discarded() && j.is_object()) gui_settings = j;
    }
    if (gui_settings.contains("gui") && gui_settings["gui"].is_object()) {
        const json& g = gui_settings["gui"];
        int vk = g.contains("toggle_key") && g["toggle_key"].is_number_integer() ? g["toggle_key"].get<int>() : 0x77;
        int mods = g.contains("toggle_mods") && g["toggle_mods"].is_number_integer() ? g["toggle_mods"].get<int>() : 0;
        if (hotkey_valid_vk(vk)) {
            toggle_vk = vk;
            toggle_mods = mods & 7;
        }
        if (g.contains("ui_scale") && g["ui_scale"].is_number()) {
            double us = g["ui_scale"].get<double>();
            if (std::isfinite(us) && us > 0.0) ui_scale_user = std::max(kUiScaleMin, std::min(kUiScaleMax, static_cast<float>(us)));
        }
    }
    prefs = GuiPrefs::load(gui_settings);  // gui.preload_images, gui.row_density (ui/gui_prefs.h)
    // the asset catalog's commentary team ("" automatic, "*" every team, "dr" ...): Callnames, docs/asset-catalog.md
    if (gui_settings.contains("callnames") && gui_settings["callnames"].is_object()) {
        const json& c = gui_settings["callnames"];
        if (c.contains("bank_team") && c["bank_team"].is_string()) callnames.bank_team_setting = c["bank_team"].get<std::string>();
    }
}

bool App::save_gui_settings() {
    gui_settings["gui"]["toggle_key"] = toggle_vk;
    gui_settings["gui"]["toggle_mods"] = toggle_mods;
    gui_settings["gui"]["ui_scale"] = std::round(ui_scale_user * 100.0f) / 100.0f;
    prefs.save(gui_settings);
    return bridge.write_gui_settings(gui_settings.dump(2));
}

// The Advanced view's note on a tab or panel with no proven in-game effect (gui_prefs.h unproven_banner_text)
void draw_unproven_banner() {
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", unproven_banner_text());
}

// ---------------------------------------------------------------- main window
// ---- GUI revamp shell (ui_shell.h): five areas + Advanced, each a strip of pages --------------------------------------------

void App::go(shell::Area a, shell::Page p) {
    using namespace shell;
    if (p == Page::None || p == Page::Count || area_of(p) != a) return;
    if (!page_visible(p, prefs.advanced_mode)) return;
    go_to_ = Place{a, p};
}

// A page nobody registered yet (its track has not landed): the panel it lived in before the revamp, drawn as it was, so that
// nothing disappears while the pages move (ui_shell.h legacy_tab_of)
static void draw_legacy_page(App& app, shell::Page p) {
    // Transfers lived in Teams (legacy_tab_of) but the legacy request_tab 5 (Turbo Tools) lands on it (area_for): until T5's
    // page is registered it shows the Turbo Tools panel; the Teams panel, transfers included, stays under Squad
    const int lt = p == shell::Page::Transfers ? 5 : shell::legacy_tab_of(p);
    if (p == shell::Page::GameplayPresets) {
        if (app.prefs.advanced_mode) draw_unproven_banner();
        else theme::muted_text("Presets write the EA Custom slider block; the effect on play is not yet measured.");
    }
    switch (lt) {
        case 0: draw_players(app); break;
        case 1: draw_teams(app); break;
        case 2: draw_managers(app); break;
        case 3: draw_competitions(app); break;
        case 4: draw_database(app); break;
        case 5: draw_tools(app); break;
        case 6: draw_status(app); break;
        case 7: draw_game_settings(app); break;  // 2.8: ui_game_settings.cpp (presets applied with the game closed)
        default: ImGui::TextDisabled("Nothing to show here."); break;
    }
}

// One button of a strip (area or page): the selected one is filled, the others plain. Wraps to a new line when the window is
// too narrow for the next one. True when clicked.
static bool strip_button(const char* label, bool selected, bool first) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(label).x + st.FramePadding.x * 2.0f;
    if (!first) {
        // P213 triage: measured on the previous button's row. Measured before SameLine (as it was), the room is always the whole
        // width (the cursor already sits at the next line's start), so the strip never wrapped: in a window narrower than the strip
        // (Career tools needs about 1450 px at UI size 1; the default window is 1180 x 760) its last pages ran off the right edge
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();  // the next line
    }
    const ImVec4 fill = selected ? theme::accent() : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, fill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? theme::accent_hover() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, selected ? theme::accent_active() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    const bool hit = ImGui::Button(label);
    ImGui::PopStyleColor(3);
    return hit;
}

// Navigation happens in the frame it is asked for (a click, go(), request_tab): the page of the new place is drawn at once.
void App::draw_pages() {
    using namespace shell;
    const bool adv = prefs.advanced_mode;
    // Legacy request_tab = N (cross-links, tests) -> its new home. Taken before drawing: a panel may request another place
    // for the next frame. A page the view hides (Simple: the Advanced area) is not opened, as the old hidden tabs were not.
    if (request_tab >= 0) {
        const Place pl = area_for(request_tab);
        request_tab = -1;
        if (pl.valid() && page_visible(pl.page, adv)) go_to_ = pl;
    }
    // A link inside the Club & Kits area (club-tools "Kits" link, ui_club_page.h): taken here, same frame
    if (const Page cp = club_take_page_request(); cp != Page::None) go(area_of(cp), cp);
    if (go_to_.valid() && page_visible(go_to_.page, adv)) place_ = go_to_;
    go_to_ = Place{};
    // Where we are: the area must be visible in this view (Advanced turned off: back to Squad & Players), the page one of its own
    if (!area_visible(place_.area, adv) || area_of(place_.page) != place_.area) {
        const Area a = area_visible(place_.area, adv) ? place_.area : Area::Squad;
        place_ = Place{a, default_page(a)};
    }
    area_page_[static_cast<int>(place_.area)] = place_.page;

    // Simple / Advanced view (gui_prefs.h): Simple hides the Advanced area (raw rows, bulk edit, not-proven panels)
    if (!adv) ImGui::TextDisabled("%s", simple_banner_text());

    ImGui::PushID("##shell");
    // the area strip
    bool first = true;
    for (int ai = 0; ai < kAreaCount; ++ai) {
        const Area a = static_cast<Area>(ai);
        if (!area_visible(a, adv)) continue;
        if (strip_button(area_label(a), place_.area == a, first) && place_.area != a) {
            const Page remembered = area_page_[ai];
            place_ = Place{a, (remembered != Page::None && page_visible(remembered, adv)) ? remembered : default_page(a)};
            area_page_[ai] = place_.page;
        }
        first = false;
    }
    // the page strip of the area (an area with one page, Settings, has none)
    const auto& strip = pages_of(place_.area);
    if (strip.size() > 1) {
        // the page strip continues on the same row when it fits (one row of chrome, not two: the panels keep their height)
        ImGui::SameLine(0.0f, S(14.0f));
        ImGui::TextDisabled("|");
        ImGui::PushID("##pages");
        bool pfirst = false;
        for (Page p : strip) {
            if (strip_button(page_label(p), place_.page == p, pfirst)) {
                place_.page = p;
                area_page_[static_cast<int>(place_.area)] = p;
            }
            pfirst = false;
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    const Page page = place_.page;
    if (!draw_page(*this, page, adv)) draw_legacy_page(*this, page);
}

void App::draw() {
    textures.new_frame(now);
    zoom_input(*this);  // Ctrl + mouse wheel / Ctrl + 0 (ui_zoom.h); the style follows on the next tick
    if (texture_test && visible) {
        ++texture_test_frames;
        uint64_t version = static_cast<uint64_t>(texture_test_frames / 5);
        Rgba img;
        img.w = img.h = 64;
        img.px.assign(64 * 64 * 4, 255);
        for (size_t i = 0; i < img.px.size(); i += 4) img.px[i] = static_cast<uint8_t>(version * 37);
        TextureCache::Pic pic = textures.pixels("texture_test", version, img);
        ImGui::SetNextWindowPos(ImVec2(S(20.0f), S(20.0f)), ImGuiCond_Always);
        ImGui::Begin("##texture_test", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
        if (pic.tex) ImGui::Image(pic.tex->GetTexRef(), ImVec2(64, 64));
        ImGui::End();
        if (texture_test_frames % 120 == 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "texture test: %zu created, %zu destroyed and freed", textures.created(), textures.freed());
            log(buf);
            if (log_hook) log_hook(buf);
        }
    }
    // Toasts (top-right)
    if (!toasts.empty()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - S(20.0f), vp->WorkPos.y + S(20.0f)), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##turbo_toasts", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
        for (const auto& t : toasts) {
            ImGui::PushStyleColor(ImGuiCol_Text, t.error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
            ImGui::PushTextWrapPos(S(520.0f));
            ImGui::TextUnformatted(t.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    // the face chooser's 3D heads: renders go on with the chooser (or Turbo) closed; never waits
    tick_faces3d(*this);
    tick_faces3d_preview(*this);  // Players > Appearance > 3D hair: the live preview's own picture (ui_faces3d_gate.cpp)

    if (!visible) return;

    // First use: 1180x760 or 90% of the screen, whichever is smaller; never larger than the screen (smaller windowed
    // resolutions, or a saved layout from a bigger screen)
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float max_w = ds.x > 100.0f ? ds.x : S(1180.0f), max_h = ds.y > 100.0f ? ds.y : S(760.0f);
    ImGui::SetNextWindowSize(ImVec2(std::min(S(1180.0f), max_w * 0.9f), std::min(S(760.0f), max_h * 0.88f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(std::min(S(60.0f), max_w * 0.04f), std::min(S(60.0f), max_h * 0.05f)), ImGuiCond_FirstUseEver);
    if (ui_scale_changed_from > 0.0f && main_window_size_.x > 0.0f) {
        // UI size changed: the window grows or shrinks with its contents (clamped below)
        const float r = ui_scale_applied / ui_scale_changed_from;
        ImGui::SetNextWindowSize(ImVec2(main_window_size_.x * r, main_window_size_.y * r), ImGuiCond_Always);
    }
    ui_scale_changed_from = 0.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(S(480.0f), max_w), std::min(S(320.0f), max_h)), ImVec2(max_w, max_h));
    if (!ImGui::Begin("FC 27 LE Turbo", &visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    main_window_size_.x = ImGui::GetWindowSize().x;
    main_window_size_.y = ImGui::GetWindowSize().y;

    // Status line
    const auto& st = bridge.state();
    if (connected()) {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.45f, 1.0f), "Connected");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Waiting for your career");
    }
    ImGui::SameLine();
    if (connected()) {
        ImGui::TextDisabled("| %zu players | %zu teams | %s | %s", model.players().size(), model.teams().size(),
                            st.in_cm ? "career loaded" : "no career loaded", st.le_version.c_str());
    } else {
        ImGui::TextDisabled("| %s", db_error.empty() ? "Open your career in the game; Turbo connects by itself." : db_error.c_str());
    }
    if (!lua_queue.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "| %zu 'keep shown name' waiting", lua_queue.size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turbo's Lua side runs one command at a time: these are sent, batched, as soon as it is free");
    }
    {
        const std::string pl = preload_line();  // ui/preload.h
        if (!pl.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "| %s", pl.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Faces, tattoos, item previews and crests load in the background while Turbo is open");
        }
    }
    if (faces3d_running()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "| %s", faces3d_line().c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game renders the face chooser's heads from their 3D models (Render all filtered heads)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop##faces3d")) faces3d_stop();
    }
    const float right_x = ImGui::GetWindowWidth() - (busy() ? S(380.0f) : S(230.0f));
    const bool hotkey_inline = hotkey_bar(*this, right_x);  // hotkey_setting.h: the show/hide key, when it fits here
    ImGui::SameLine(right_x);
    if (busy()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Waiting: %s", usertext::pending_text(pending_label).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(st.in_cm ? "It runs as soon as the game is ready (the next career-mode event)"
                                       : "No career loaded: run lua\\scripts\\turbo_exec.lua in Live Editor's Lua Engine");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) {
            mailbox->cancel();
            notify(usertext::pending_text(pending_label) + ": cancelled");
            pending_label.clear();
        }
    } else if (ImGui::Button("Refresh")) {
        bridge.poll_files();
        if (refresh()) notify("Lists refreshed from the game");
        else notify(db_error, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", kGuiVersion);
    ImGui::SameLine();
    if (ImGui::Checkbox("Advanced view", &prefs.advanced_mode) && !save_gui_settings()) notify("cannot write gui_settings.json", true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off (Simple): only features proven to work in the game. On: everything, including tactics, tuning and raw tables");
    if (!hotkey_inline) hotkey_control(*this, "bar");  // no room on the status line: a line of its own
    ImGui::Separator();

    draw_pages();  // the area tabs, the page strip and the page (App::draw_pages below)
    hotkey_popup(*this);  // hotkey_setting.h: waits for the new show/hide key after Change...
    // the capture window was not drawn this frame: stop waiting for a key (hotkey_setting.h)
    if (hotkey_capture && hotkey_capture_frame != ImGui::GetFrameCount()) hotkey_capture = false;
    ImGui::End();
}

}  // namespace turbo
