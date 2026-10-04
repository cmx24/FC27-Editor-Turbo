#include "app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

#include "imgui.h"

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
}

void App::update_style() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    float want = auto_ui_scale(io.DisplaySize.y) * std::max(0.6f, std::min(2.5f, ui_scale_user));
    want = std::round(want * 20.0f) / 20.0f;
    if (want == ui_scale_applied) return;
    ImGuiStyle& st = ImGui::GetStyle();
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
    char when[32];
    std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::localtime(&t);  // render thread only
    if (!tmv || std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", tmv) == 0) std::snprintf(when, sizeof(when), "%lld", static_cast<long long>(t));
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
    model.rebuild(today());
    ++gen;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "database connected: %d tables, %zu players, %zu teams", n, model.players().size(),
                  model.teams().size());
    log(buf);
    return true;
}

void App::tick(double t) {
    now = t;
    update_style();
    legacy.tick(t);
    finish_bank_capture();
    if (t >= next_poll) {
        next_poll = t + 0.5;
        bool changed = bridge.poll_files();
        const auto& st = bridge.state();
        // Rebuild lists only when the database itself may have moved (save loaded, career entered/left),
        // not on every in-game day.
        if (changed && bridge.meta_loaded() && st.loaded &&
            (st.db_gen != seen_db_gen || st.db_service != seen_service || !db.ready())) {
            seen_db_gen = st.db_gen;
            // A full re-read takes a moment on a big database: while the window is hidden it waits
            // until the window is shown again (the first connection is made right away)
            if (visible || !db.ready()) refresh();
            else refresh_pending = true;
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
            pending_label.clear();
            // Lua may have changed the database (transfers, bulk edits): re-read the lists
            if (db.ready()) model_stale = true;
        }
    }
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
            if (us >= 0.6 && us <= 2.5) ui_scale_user = static_cast<float>(us);
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
