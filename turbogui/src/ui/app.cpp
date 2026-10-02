#include "app.h"

#include <algorithm>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "imgui.h"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

App::App(Memory& m, fs::path le_root, uint64_t mailbox_addr, std::string sess)
    : mem(m), bridge(std::move(le_root)), db(m), model(db), session(std::move(sess)) {
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

bool App::refresh() {
    next_retry = now + 5.0;
    db_error.clear();
    if (!bridge.meta_loaded()) {
        db_error = bridge.meta_error().empty() ? "waiting for Turbo's Lua side (turbo_output\\bridge_meta.json)"
                                               : bridge.meta_error();
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
    if (!db.set(t, rec, f, v, &err)) {
        notify(f.name + ": " + err, true);
        return false;
    }
    if (t.name == "players") {
        int64_t pid = db.get_int(t, rec, "playerid", 0);
        model.refresh_player(pid, today());
    } else if (t.name == "teams") {
        model.refresh_team(db.get_int(t, rec, "teamid", 0));
    } else if (t.name == "teamplayerlinks") {
        model.reload_links();
    }
    ++gen;
    log(t.name + "." + f.name + " = " + v.to_string());
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
    }
}

bool App::save_gui_settings() {
    gui_settings["gui"]["toggle_key"] = toggle_vk;
    return bridge.write_gui_settings(gui_settings.dump(2));
}

// ---------------------------------------------------------------- main window
void App::draw() {
    // Toasts (top-right)
    if (!toasts.empty()) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 20.0f, vp->WorkPos.y + 20.0f), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##turbo_toasts", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
        for (const auto& t : toasts) {
            ImGui::PushStyleColor(ImGuiCol_Text, t.error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.6f, 1.0f, 0.6f, 1.0f));
            ImGui::PushTextWrapPos(520.0f);
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
    const float max_w = ds.x > 100.0f ? ds.x : 1180.0f, max_h = ds.y > 100.0f ? ds.y : 760.0f;
    ImGui::SetNextWindowSize(ImVec2(std::min(1180.0f, max_w * 0.9f), std::min(760.0f, max_h * 0.88f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(std::min(60.0f, max_w * 0.04f), std::min(60.0f, max_h * 0.05f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(480.0f, max_w), std::min(320.0f, max_h)), ImVec2(max_w, max_h));
    if (!ImGui::Begin("FC 27 LE Turbo", &visible, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

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
    ImGui::SameLine(ImGui::GetWindowWidth() - (busy() ? 380.0f : 230.0f));
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
        const char* names[] = {"Players", "Teams", "Managers", "Database", "Turbo Tools", "Status"};
        // Taken before drawing: a panel may request another tab (e.g. Squad -> player), for the next frame
        int req = request_tab;
        request_tab = -1;
        for (int i = 0; i < 6; ++i) {
            ImGuiTabItemFlags fl = (req == i) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, fl)) {
                switch (i) {
                    case 0: draw_players(*this); break;
                    case 1: draw_teams(*this); break;
                    case 2: draw_managers(*this); break;
                    case 3: draw_database(*this); break;
                    case 4: draw_tools(*this); break;
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
