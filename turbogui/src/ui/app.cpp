#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "core/teamnames.h"
#include "imgui.h"
#include "ui_zoom.h"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

float g_ui_scale = 1.0f;

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
    if (want == ui_scale_applied) return;
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
    if (ui_scale_applied > 0.0f) ui_scale_changed_from = ui_scale_applied;
    ui_scale_applied = want;
    g_ui_scale = want;
}

App::App(Memory& m, fs::path le_root, uint64_t mailbox_addr, std::string sess)
    : mem(m), bridge(std::move(le_root)), db(m), model(db), session(std::move(sess)), legacy(bridge.root()) {
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
    load_voice();    // voice swaps (ui_callnames.cpp): no career needed; published once the host gives the service
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
    bank_cancel_ = true;
    if (bank_thread_.joinable()) bank_thread_.join();
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

// ---- the spoken set asked from the game's audio service (core/commentary_audio.h): a build on the game thread
static bool read_text_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string App::chosen_commentary_language() const {
    if (gui_settings.is_object() && gui_settings.contains("callnames") && gui_settings["callnames"].is_object())
        return gui_settings["callnames"].value("language", std::string());
    return "";
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
    req.players = ids.players;
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
    std::snprintf(line, sizeof(line), "asking the game's audio service about %zu commentary ids and %zu players (from %s)%s...", req.names.size(),
                  req.players.size(), ids.source.c_str(), automatic ? " (automatic)" : "");
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
    if (!callnames.refreshed && !game_root.empty()) callnames.refresh(bridge.root(), game_root, chosen_commentary_language());
    caudio::SpokenWatch::Inputs in;
    const caudio::ServiceStatus st = commentary_audio->status();
    in.service = st.installed;
    in.available = st.available && !st.busy;
    in.verified = callnames.refreshed && callnames.spoken.verified;
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
    } else if (!callnames.apply_capture(c, bridge.root(), when, build, &err)) {
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
            db_error = "Turbo's Lua side cannot read the game database: " + bridge.state().meta_error;
        else
            db_error = "waiting for Turbo's Lua side (turbo_output\\bridge_meta.json)";
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
    // Voice swaps: the store goes to the service the host gave (once per service, then after every edit); the kill
    // switches are re-read every 2 s (the detours only read cached atomics)
    if (voice_service) {
        if (voice_published_to_ != voice_service) voice_publish();
        if (t >= voice_next_switches_) {
            voice_next_switches_ = t + 2.0;
            voice_service->refresh_switches();
        }
    }
    finish_bank_capture();
    finish_spoken_build();
    spoken_watch_tick();
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
            match_setup_status = (r.ok ? "" : "failed: ") + r.message;
            notify("Match setup: " + r.message, !r.ok);
            log("match setup: " + r.message);
        }
    }
    // Player names decoded by Live Editor arrived or changed: rebuild the lists (now if shown, else when shown)
    if (db.ready() && bridge.names() != seen_names_) {
        seen_names_ = bridge.names();
        model.set_extra_names(seen_names_);
        model_stale = true;
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
            notify(pending_label + ": " + (result.empty() ? (ok ? "done" : "failed") : result), !ok);
            if (pending_label.rfind("Job offer", 0) == 0)  // Managers > Job offers shows the outcome in place
                job_offer_status = (ok ? "" : "Failed: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            if (pending_label.rfind("Manager rules", 0) == 0)  // Managers > Manager rules shows the outcome in place
                manager_rules_status = (ok ? "" : "Failed: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            if (pending_label.rfind("Manager move", 0) == 0)  // Managers > Manager market
                manager_move_status = (ok ? "" : "Failed: ") + (result.empty() ? std::string(ok ? "done" : "failed") : result);
            pending_label.clear();
            flush_lua_queue();  // the mailbox is free: the next queued "keep shown name" actions
            // Lua may have changed the database (transfers, bulk edits): re-read the lists
            if (db.ready()) model_stale = true;
        }
    }
    flush_lua_queue();
    for (size_t i = 0; i < toasts.size();) {
        if (toasts[i].until < t) toasts.erase(toasts.begin() + static_cast<long>(i));
        else ++i;
    }
}

bool App::edit(const Table& t, uint64_t rec, const Field& f, const Value& v) {
    std::string err;
    Value before;
    bool have_before = db.get(t, rec, f, before);
    if (!db.set(t, rec, f, v, &err)) {
        notify(f.name + ": " + err, true);
        return false;
    }
    if (t.name == "players") {
        int64_t pid = db.get_int(t, rec, "playerid", 0);
        model.refresh_player(pid, today());
        if (have_before && !(before == v)) {
            auto& steps = undo_[pid];
            steps.push_back({t.name, rec, f.name, before});
            while (steps.size() > kUndoSteps) steps.pop_front();
        }
    } else if (t.name == "teams") {
        model.refresh_team(db.get_int(t, rec, "teamid", 0));
    } else if (t.name == "teamplayerlinks") {
        model.reload_links();
    }
    ++gen;
    log(t.name + "." + f.name + " = " + v.to_string());
    return true;
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
    const Field* f = t ? t->field(step.field) : nullptr;
    if (!t || !f || !db.table_alive(*t, step.rec)) {
        notify("undo: the database changed (save loaded?) - press Refresh", true);
        return false;
    }
    std::string err;
    if (!db.set(*t, step.rec, *f, step.before, &err)) {
        notify("undo " + step.field + ": " + err, true);
        return false;
    }
    model.refresh_player(playerid, today());
    ++gen;
    log("undo " + step.table + "." + step.field + " = " + step.before.to_string());
    notify("undone: " + field_label(step.field) + " back to " + step.before.to_string());
    return true;
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
        int vk = gui_settings["gui"].value("toggle_key", 0x77);
        if (vk > 0 && vk < 256) toggle_vk = vk;
        const json& g = gui_settings["gui"];
        if (g.contains("ui_scale") && g["ui_scale"].is_number()) {
            double us = g["ui_scale"].get<double>();
            if (std::isfinite(us) && us > 0.0) ui_scale_user = std::max(kUiScaleMin, std::min(kUiScaleMax, static_cast<float>(us)));
        }
    }
}

bool App::save_gui_settings() {
    gui_settings["gui"]["toggle_key"] = toggle_vk;
    gui_settings["gui"]["ui_scale"] = std::round(ui_scale_user * 100.0f) / 100.0f;
    return bridge.write_gui_settings(gui_settings.dump(2));
}

// ---------------------------------------------------------------- main window
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
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Not connected");
    }
    ImGui::SameLine();
    if (connected()) {
        ImGui::TextDisabled("| %zu players | %zu teams | %s | %s", model.players().size(), model.teams().size(),
                            st.in_cm ? "career loaded" : "no career loaded", st.le_version.c_str());
    } else {
        ImGui::TextDisabled("| %s", db_error.empty() ? "waiting for Turbo's Lua side (enter a career)" : db_error.c_str());
    }
    if (!lua_queue.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "| %zu 'keep shown name' waiting", lua_queue.size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turbo's Lua side runs one command at a time: these are sent, batched, as soon as it is free");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - (busy() ? S(380.0f) : S(230.0f)));
    if (busy()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Queued: %s", pending_label.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(st.in_cm ? "Runs on the next career-mode event"
                                       : "No career loaded: run lua\\scripts\\turbo_exec.lua in Live Editor's Lua Engine");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) {
            mailbox->cancel();
            notify(pending_label + ": cancelled");
            pending_label.clear();
        }
    } else if (ImGui::Button("Refresh")) {
        bridge.poll_files();
        if (refresh()) notify("Lists refreshed from the game");
        else notify(db_error, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("v%s", kGuiVersion);
    ImGui::Separator();

    if (ImGui::BeginTabBar("##turbo_tabs")) {
        const char* names[] = {"Players", "Teams", "Managers", "Competitions", "Database", "Turbo Tools", "Status"};
        // Taken before drawing: a panel may request another tab (e.g. Squad -> player), for the next frame
        int req = request_tab;
        request_tab = -1;
        for (int i = 0; i < 7; ++i) {
            ImGuiTabItemFlags fl = (req == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, fl)) {
                switch (i) {
                    case 0: draw_players(*this); break;
                    case 1: draw_teams(*this); break;
                    case 2: draw_managers(*this); break;
                    case 3: draw_competitions(*this); break;
                    case 4: draw_database(*this); break;
                    case 5: draw_tools(*this); break;
                    default: draw_status(*this); break;
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace turbo
