// FC 27 LE Turbo GUI - Players > Callname tab: the name the commentary speaks for the loaded commentary language,
// where it comes from (player-specific / common name / last name), and two type-ahead pickers to assign another one:
//   BY NAME   a playernames name whose commentary id is spoken -> written to lastnameid or commonnameid (the shown name
//             can be kept through editedplayernames)
//   BY PLAYER a player whose playernamemap callname is spoken -> written to this player's playernamemap row (added
//             through Turbo's Lua side when missing)
// See core/callnames.h and docs/callnames.md.
#include <cstdio>
#include <cstring>
#include <string>

#include "app.h"
#include "imgui.h"

namespace turbo {

using json = nlohmann::json;

static int g_index_gen = -1;
static char g_name_search[64] = "";
static char g_player_search[64] = "";
static int64_t g_sel_name = 0;
static int64_t g_sel_player = 0;
static bool g_keep_display = true;
static const int kMaxRows = 300;

static std::string chosen_language(App& app) {
    if (app.gui_settings.is_object() && app.gui_settings.contains("callnames") && app.gui_settings["callnames"].is_object())
        return app.gui_settings["callnames"].value("language", std::string());
    return "";
}

static void refresh_all(App& app) {
    app.callnames.refresh(app.bridge.root(), app.game_root, chosen_language(app));
    app.callnames.build_index(app.db, app.model, app.model.names_by_id());
    g_index_gen = app.gen;
}

static void ensure_ready(App& app) {
    if (!app.callnames.refreshed) {
        refresh_all(app);
    } else if (!app.callnames.index.built || app.callnames.index.model_version != app.model.version() || g_index_gen != app.gen) {
        app.callnames.build_index(app.db, app.model, app.model.names_by_id());
        g_index_gen = app.gen;
    }
    // No hand-made list and no cached set yet for this language: ask the game's audio service once by itself
    if (!app.spoken_auto_tried && !app.callnames.lang.empty() && !app.callnames.spoken.verified && app.commentary_audio) {
        app.spoken_auto_tried = true;
        app.start_spoken_build(true);
    }
}

// The three name parts the game shows for the player: editedplayernames when it has a row, else the name ids
struct ShownName {
    std::string first, last, common;
    uint64_t edited_rec = 0;
};
static ShownName shown_name(App& app, const Table& t, const PlayerRow& p) {
    ShownName n;
    const auto& names = app.model.names_by_id();
    auto name_of = [&](const char* field) {
        int64_t id = app.db.get_int(t, p.rec, field, 0);
        auto it = names.find(id);
        return id > 0 && it != names.end() ? it->second : std::string();
    };
    n.first = name_of("firstnameid");
    n.last = name_of("lastnameid");
    n.common = name_of("commonnameid");
    if (const Table* e = app.db.table("editedplayernames"); e && e->has("playerid")) {
        uint64_t rec = app.db.find(*e, "playerid", p.playerid);
        if (rec) {
            n.edited_rec = rec;
            Value v;
            if (const Field* f = e->field("firstname"); f && app.db.get(*e, rec, *f, v) && !v.to_string().empty()) n.first = v.to_string();
            if (const Field* f = e->field("surname"); f && app.db.get(*e, rec, *f, v) && !v.to_string().empty()) n.last = v.to_string();
            if (const Field* f = e->field("commonname"); f && app.db.get(*e, rec, *f, v) && !v.to_string().empty()) n.common = v.to_string();
        }
    }
    return n;
}

static bool send_actions(App& app, const json& actions, const std::string& label) {
    return app.send({{"op", "run"}, {"module", "callnames"}, {"overrides", {{"actions", actions}}}}, label);
}

// BY NAME: write the name id, then keep the shown name (editedplayernames row edited in place, or added through Lua)
static void assign_name(App& app, const Table& t, const PlayerRow& p, const NameChoice& c, bool as_common, bool keep_display) {
    const Field* f = t.field(as_common ? "commonnameid" : "lastnameid");
    if (!f) {
        app.notify("players has no " + std::string(as_common ? "commonnameid" : "lastnameid") + " field", true);
        return;
    }
    ShownName before = shown_name(app, t, p);
    if (!app.edit(t, p.rec, *f, Value::of_int(c.nameid))) return;
    std::string msg = p.name + ": " + (as_common ? "common name" : "last name") + " -> '" + c.name + "' (name " +
                      std::to_string(c.nameid) + ", callname " + std::to_string(c.commentaryid) + ")";
    if (keep_display) {
        const Table* e = app.db.table("editedplayernames");
        if (!e) {
            app.notify(msg + "; no editedplayernames table: the shown name follows the new name id", true);
            return;
        }
        if (before.edited_rec) {
            bool ok = true;
            if (const Field* ff = e->field("firstname")) ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.first));
            if (const Field* ff = e->field("surname")) ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.last));
            if (const Field* ff = e->field("commonname")) ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.common));
            if (ok) app.notify(msg + "; shown name kept (editedplayernames updated)");
        } else {
            json a = {{"action", "set_display_name"}, {"playerid", p.playerid}, {"firstname", before.first},
                      {"surname", before.last}, {"commonname", before.common}};
            if (send_actions(app, json::array({a}), "Keep shown name"))
                app.notify(msg + "; editedplayernames row queued for Turbo's Lua side (next career event)");
            else
                app.notify(msg + "; shown name NOT kept: Turbo's Lua side is busy, use 'Keep shown name' again", true);
        }
    } else {
        app.notify(msg);
    }
}

// BY PLAYER: this player's playernamemap row (edited in place when it exists, else added through Lua)
static void assign_player_callname(App& app, const PlayerRow& p, int64_t commentaryid, const std::string& from) {
    const Table* m = app.db.table("playernamemap");
    auto rec = app.callnames.index.playernamemap_rec.find(p.playerid);
    if (m && rec != app.callnames.index.playernamemap_rec.end()) {
        const Field* f = m->field("commentaryid");
        if (f && app.edit(*m, rec->second, *f, Value::of_int(commentaryid)))
            app.notify(p.name + ": player-specific callname " + std::to_string(commentaryid) + " (from " + from + ")");
        return;
    }
    json a = {{"action", "set_playernamemap"}, {"playerid", p.playerid}, {"commentaryid", commentaryid}};
    if (send_actions(app, json::array({a}), "Player callname"))
        app.notify(p.name + ": playernamemap row (" + std::to_string(commentaryid) + ", from " + from + ") queued for Turbo's Lua side");
}

static void language_line(App& app) {
    Callnames& cn = app.callnames;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Commentary language:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(190.0f));
    std::string current = cn.lang.empty() ? "(none found)" : cn.lang;
    if (ImGui::BeginCombo("##cnlang", current.c_str())) {
        bool auto_sel = chosen_language(app).empty();
        if (ImGui::Selectable("detect automatically", auto_sel)) {
            if (app.gui_settings.is_object() && app.gui_settings.contains("callnames")) app.gui_settings["callnames"].erase("language");
            app.save_gui_settings();
            refresh_all(app);
        }
        for (const auto& pk : cn.packs) {
            std::string label = pk.code + (pk.downloaded ? " (downloaded)" : " (base game)");
            if (ImGui::Selectable(label.c_str(), pk.code == cn.lang)) {
                app.gui_settings["callnames"]["language"] = pk.code;
                app.save_gui_settings();
                refresh_all(app);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh##cn")) refresh_all(app);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Look for language packs and the spoken-id list again, rebuild the pickers");
    ImGui::TextDisabled("%s", cn.lang_why.c_str());
    if (cn.no_game_root) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Game folder unknown: the tests or the host must set it.");
    if (cn.spoken.verified) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "Spoken set from %s: %zu names, %zu player callnames", cn.spoken.source.c_str(),
                           cn.spoken.ids.size(), cn.spoken.players.size());
    } else {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Unverified: %s", cn.spoken.source.c_str());
    }
    if (!cn.list_error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", cn.list_error.c_str());
    if (!cn.cache_error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", cn.cache_error.c_str());
    // One row: the spoken set from the game's audio service (core/commentary_audio.h; the default, built on the game
    // thread, one batch per frame) and, as a diagnostic, the memory scan of the loaded bank (core/commentary_bank.h,
    // background thread); one status line for whichever ran last
    caudio::ServiceStatus st;
    if (app.commentary_audio) st = app.commentary_audio->status();
    const bool can_build = app.commentary_audio && st.available;
    if (!can_build) ImGui::BeginDisabled();
    if (ImGui::Button("Rebuild from the game's audio service##cn")) app.start_spoken_build(false);
    if (!can_build) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Asks the game's own audio service, id by id, whether the loaded commentary bank has a recording: the same\n"
                          "check the Create Player screen runs on its name list (PLAYER_NAME_FE / surname_ID), plus the in-match check\n"
                          "for players with their own recordings (PLAYER_LOW_SIMPLE / PLAYER_LOW_LINK with player_db_pID). Runs on the\n"
                          "game thread in small batches, one per frame; cached as turbo_output\\callnames\\spoken_<lang>.json.");
    ImGui::SameLine();
    const bool running = app.bank_capture_running();
    if (running) ImGui::BeginDisabled();
    if (ImGui::Button("Capture from the loaded bank##cn")) app.start_bank_capture(false);
    if (running) ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Diagnostic: scans the game's memory for the loaded bank's selection tables and caches what it finds as\n"
                          "turbo_output\\callnames\\spoken_<lang>.json (the audio-service build is the normal way). A hand-made\n"
                          "spoken_<lang>.txt under turbo\\callnames overrides either.");
    ImGui::SameLine();
    if (st.busy) ImGui::TextDisabled("building: %s", st.progress.c_str());
    else if (running) ImGui::TextDisabled("%s", app.bank_capture_status.c_str());
    else if (!app.spoken_build_status.empty()) ImGui::TextDisabled("%s", app.spoken_build_status.c_str());
    else if (!app.bank_capture_status.empty()) ImGui::TextDisabled("last capture: %s", app.bank_capture_status.c_str());
    else if (!app.commentary_audio) ImGui::TextDisabled("audio-service build not available in this build of Turbo");
    else if (!st.installed) ImGui::TextDisabled("not available: %s", st.reason.c_str());
    else ImGui::TextDisabled("%s", st.reason.empty() ? "ready" : st.reason.c_str());
    if (!cn.spoken.verified && !cn.list_path.empty())
        ImGui::TextDisabled("Override list looked for: %s; capture cache: %s", cn.list_path.c_str(), cn.cache_path.c_str());
}

static void by_name_picker(App& app, const Table& t, const PlayerRow& p) {
    const CallnameIndex& ix = app.callnames.index;
    ImGui::SetNextItemWidth(S(260.0f));
    ImGui::InputTextWithHint("##cnsearch", "type a name or a name id", g_name_search, sizeof(g_name_search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu spoken names", ix.names.size());
    ImGui::BeginChild("##cnames", ImVec2(0, S(200.0f)), ImGuiChildFlags_Borders);
    int shown = 0, total = 0;
    if (ImGui::BeginTable("##cntable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name ID", ImGuiTableColumnFlags_WidthFixed, S(70.0f));
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Commentary ID", ImGuiTableColumnFlags_WidthFixed, S(110.0f));
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
        ImGui::TableHeadersRow();
        for (const auto& c : ix.names) {
            if (!callname_filter_match(g_name_search, c.name, c.nameid)) continue;
            ++total;
            if (shown >= kMaxRows) continue;
            ++shown;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idbuf[32];
            std::snprintf(idbuf, sizeof(idbuf), "%lld", static_cast<long long>(c.nameid));
            if (ImGui::Selectable(idbuf, g_sel_name == c.nameid, ImGuiSelectableFlags_SpanAllColumns)) g_sel_name = c.nameid;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(c.commentaryid));
            ImGui::TableNextColumn();
            ImGui::Text("%d", c.users);
        }
        ImGui::EndTable();
    }
    if (total > shown) ImGui::TextDisabled("%d more: type more of the name", total - shown);
    ImGui::EndChild();
    const NameChoice* sel = nullptr;
    for (const auto& c : ix.names)
        if (c.nameid == g_sel_name) sel = &c;
    if (!sel) {
        ImGui::TextDisabled("Pick a name above.");
        return;
    }
    ImGui::Text("Selected: %s (name %lld, callname %lld, %d player%s)", sel->name.c_str(), static_cast<long long>(sel->nameid),
                static_cast<long long>(sel->commentaryid), sel->users, sel->users == 1 ? "" : "s");
    ImGui::Checkbox("Keep the shown name (editedplayernames)", &g_keep_display);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The game shows editedplayernames first, so the player keeps his name on screen while the commentary speaks the chosen one");
    bool has_common = t.has("commonnameid"), has_last = t.has("lastnameid");
    if (!has_last) ImGui::BeginDisabled();
    if (ImGui::Button("Assign as last name")) assign_name(app, t, p, *sel, false, g_keep_display);
    if (!has_last) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!has_common) ImGui::BeginDisabled();
    if (ImGui::Button("Assign as common name")) assign_name(app, t, p, *sel, true, g_keep_display);
    if (!has_common) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(a common name wins over the last name; a player-specific callname wins over both)");
}

static void by_player_picker(App& app, const PlayerRow& p) {
    const CallnameIndex& ix = app.callnames.index;
    ImGui::SetNextItemWidth(S(260.0f));
    ImGui::InputTextWithHint("##cpsearch", "type a player name, club or id", g_player_search, sizeof(g_player_search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu players with a spoken player-specific callname", ix.players.size());
    ImGui::BeginChild("##cplayers", ImVec2(0, S(200.0f)), ImGuiChildFlags_Borders);
    int shown = 0, total = 0;
    if (ImGui::BeginTable("##cptable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Player ID", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Club");
        ImGui::TableSetupColumn("Commentary ID", ImGuiTableColumnFlags_WidthFixed, S(110.0f));
        ImGui::TableHeadersRow();
        for (const auto& c : ix.players) {
            if (c.playerid == p.playerid) continue;
            if (!callname_filter_match(g_player_search, c.name + " " + c.club, c.playerid)) continue;
            ++total;
            if (shown >= kMaxRows) continue;
            ++shown;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idbuf[32];
            std::snprintf(idbuf, sizeof(idbuf), "%lld", static_cast<long long>(c.playerid));
            if (ImGui::Selectable(idbuf, g_sel_player == c.playerid, ImGuiSelectableFlags_SpanAllColumns)) g_sel_player = c.playerid;
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.club.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(c.commentaryid));
        }
        ImGui::EndTable();
    }
    if (total > shown) ImGui::TextDisabled("%d more: type more of the name", total - shown);
    ImGui::EndChild();
    const PlayerChoice* sel = nullptr;
    for (const auto& c : ix.players)
        if (c.playerid == g_sel_player) sel = &c;
    if (!sel) {
        ImGui::TextDisabled("Pick a player above: his player-specific callname is copied to this player.");
        return;
    }
    ImGui::Text("Selected: %s (%s, player %lld) speaks callname %lld", sel->name.c_str(), sel->club.empty() ? "no club" : sel->club.c_str(),
                static_cast<long long>(sel->playerid), static_cast<long long>(sel->commentaryid));
    if (ImGui::Button("Use this player's callname")) assign_player_callname(app, p, sel->commentaryid, sel->name);
    ImGui::SameLine();
    ImGui::TextDisabled("(writes this player's playernamemap row; the shown name does not change)");
}

void callname_editor(App& app, const Table& t, const PlayerRow& p) {
    ensure_ready(app);
    Callnames& cn = app.callnames;
    language_line(app);
    ImGui::Separator();

    CallnameInfo info = cn.resolve(p, app.db);
    ImGui::AlignTextToFramePadding();
    if (info.real) {
        auto it = cn.spoken.players.find(p.playerid);
        int v = it != cn.spoken.players.end() ? it->second : 0;
        std::string how;
        if (cn.spoken.from == SpokenSet::From::GameAudio) {
            if (v & caudio::kPlayerLowSimple) how += "PLAYER_LOW_SIMPLE";
            if (v & caudio::kPlayerLowLink) how += std::string(how.empty() ? "" : " + ") + "PLAYER_LOW_LINK";
            if (how.empty()) how = "the game's audio service";
        } else {
            how = std::to_string(v) + (v == 1 ? " player-keyed table" : " player-keyed tables");
        }
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "Recorded by name in %s: the bank has this player's own recordings (%s)", cn.lang.c_str(),
                           how.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Player-specific (\"Real\") recordings are bound to the player id inside the bank, not to a commentary id:\n"
                              "the commentary says this name whatever the callname rule below gives.");
    }
    if (info.source == CallnameSource::None) {
        ImGui::Text("Current callname: none (%s)", info.real ? "the recordings above are used" : "the commentary does not say this player's name");
    } else {
        std::string via;
        if (info.nameid > 0) {
            auto it = app.model.names_by_id().find(info.nameid);
            via = " '" + (it != app.model.names_by_id().end() ? it->second : std::string("?")) + "' (name " + std::to_string(info.nameid) + ")";
        }
        bool spoken = cn.spoken.spoken(info.commentaryid);
        ImGui::Text("Current callname: %lld from %s%s - ", static_cast<long long>(info.commentaryid), callname_source_name(info.source), via.c_str());
        ImGui::SameLine();
        if (spoken) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), cn.spoken.verified ? "spoken in %s" : "used by playernames (unverified) in %s", cn.lang.c_str());
        else ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "NOT spoken in %s", cn.lang.c_str());
    }
    if (cn.index.playernamemap.count(p.playerid)) {
        if (ImGui::Button("Remove player-specific callname...")) ImGui::OpenPopup("##rmcallname");
        ImGui::SameLine();
        ImGui::TextDisabled("(back to the common / last name rule)");
    }
    if (ImGui::BeginPopupModal("##rmcallname", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove the playernamemap row of %s (ID %lld)?", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::TextDisabled("The commentary falls back to his common or last name. Done by Turbo's Lua side on the next career event.");
        if (ImGui::Button("Remove")) {
            json a = {{"action", "remove_playernamemap"}, {"playerid", p.playerid}};
            send_actions(app, json::array({a}), "Remove player callname");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##rmcallname")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SeparatorText("Assign a spoken callname");
    if (ImGui::BeginTabBar("##cnpick")) {
        if (ImGui::BeginTabItem("By name")) {
            by_name_picker(app, t, p);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("By player")) {
            by_player_picker(app, p);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}  // namespace turbo
