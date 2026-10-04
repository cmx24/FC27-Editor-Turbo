// FC 27 LE Turbo GUI - Players > Callname tab: the name the commentary speaks for the loaded commentary language,
// where it comes from (player-specific / common name / last name), and two type-ahead pickers to assign another one:
//   BY NAME   a playernames name whose commentary id is spoken -> written to lastnameid or commonnameid (the shown name
//             can be kept through editedplayernames)
//   BY PLAYER a player whose playernamemap callname is spoken -> written to this player's playernamemap row (added
//             through Turbo's Lua side when missing, which counts the rows again first; when the table is full, a row
//             from which no player hears a callname is taken over, and its player is named before the write); kept in
//             turbo_output\reapply_edits.json and written again at every career load, because FC 27 reloads
//             playernamemap then (ui_reapply.cpp)
// A player with his OWN recording (the game's audio service or the master list says so: an FC 27 master or the user's
// FC 26 list) is spoken from it whatever either picker writes (docs/callnames.md section 1 step 0): the tab says so in
// the "Current callname" line, warns above the assignment buttons and asks for a confirmation before anything is
// written for him; a callname written for him anyway is not kept for the next career loads.
// See core/callnames.h and docs/callnames.md.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

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
static const ImVec4 kGreen(0.4f, 0.9f, 0.4f, 1), kOrange(1, 0.6f, 0.3f, 1), kYellow(1, 0.85f, 0.4f, 1);

// An assignment to a player with his own recording waits here for the confirmation popup (##cnown)
struct PendingAssign {
    enum class Kind { None, LastName, CommonName, Player } kind = Kind::None;
    int64_t playerid = 0;
    int64_t nameid = 0;        // By name: the picked name
    int64_t commentaryid = 0;  // By player: the callname to copy
    std::string from;          // By player: whose callname it is
    bool keep_display = true;
};
static PendingAssign g_pending;
static bool g_open_own_confirm = false;
static CallnameTabState g_state;

const CallnameTabState& callname_tab_state() { return g_state; }

static std::string chosen_language(App& app) { return app.chosen_commentary_language(); }

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
    // No hand-made list and no cached set yet for this language: App::spoken_watch_tick probes the game every few seconds
    // and starts the build by itself where the game answers (the Create Player screen, a match); nothing to start here
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

// What a Lua insert carries so that Turbo's Lua side counts the rows again right before it adds one
// (features/callnames.lua room_now): the table's capacity as read now, and the career load it was read in. The command
// runs at the next career event, and FC 27 reloads playernamemap full (106 of 106 rows) at every career load.
static void add_room_check(App& app, json& a, uint32_t capacity) {
    a["room"] = true;
    a["capacity"] = capacity;
    if (app.bridge.state().load_gen >= 0) a["load_gen"] = app.bridge.state().load_gen;
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
            uint32_t used = 0, cap = 0;
            if (!app.db.rows_in_use(*e, used, cap) || used >= cap) {
                app.notify(msg + "; shown name NOT kept: the game's editedplayernames table is full (Live Editor cannot add a row)", true);
                return;
            }
            json a = {{"action", "set_display_name"}, {"playerid", p.playerid}, {"firstname", before.first},
                      {"surname", before.last}, {"commonname", before.common}};
            add_room_check(app, a, cap);
            if (send_actions(app, json::array({a}), "Keep shown name"))
                app.notify(msg + "; editedplayernames row queued for Turbo's Lua side (next career event)");
            else
                app.notify(msg + "; shown name NOT kept: Turbo's Lua side is busy, use 'Keep shown name' again", true);
        }
    } else {
        app.notify(msg);
    }
}

// How a player-specific callname is written for a player: his playernamemap row edited in place; else a row added
// through Lua when the table has room; else (FC 27's table is full: 106 of 106 rows, and Live Editor's
// InsertDBTableRow crashes the game on a full table, so a full table never reaches it) a row taken over that no player
// hears a callname from (Callnames::spare_playernamemap_row), else nothing. Computed for the line shown before the
// write and again at the write.
struct PlayerWritePlan {
    enum class Kind { NoTable, InPlace, AddRow, TakeOver, Full } kind = Kind::NoTable;
    uint64_t rec = 0;       // InPlace: his row
    bool counted = false;   // the table's row count was read (used / cap)
    uint32_t used = 0, cap = 0;
    SpareRow spare;         // TakeOver: the row taken; Full: the rows kept and why
    bool no_players = false;  // Full because the model has no players (whose row is whose cannot be told)
};

// This player's playernamemap row: the index's address while that row still holds him (Lua may have added or removed
// rows since the index was built, a career load replaces them all), else looked up again; 0 when he has none. So a
// write never lands in another player's row.
static uint64_t own_playernamemap_row(App& app, const Table& m, int64_t playerid) {
    const CallnameIndex& ix = app.callnames.index;
    if (auto it = ix.playernamemap_rec.find(playerid); it != ix.playernamemap_rec.end() && app.db.table_alive(m, it->second) &&
                                                       app.db.record_valid(m, it->second) && app.db.get_int(m, it->second, "playerid", -1) == playerid)
        return it->second;
    return app.db.find(m, "playerid", playerid);
}

static PlayerWritePlan plan_player_write(App& app, const PlayerRow& p) {
    PlayerWritePlan plan;
    const Table* m = app.db.table("playernamemap");
    const Field* fp = m ? m->field("playerid") : nullptr;
    const Field* fc = m ? m->field("commentaryid") : nullptr;
    if (!m || !fp || !fc) return plan;
    if (const uint64_t rec = own_playernamemap_row(app, *m, p.playerid)) {
        plan.kind = PlayerWritePlan::Kind::InPlace;
        plan.rec = rec;
        return plan;
    }
    plan.counted = app.db.rows_in_use(*m, plan.used, plan.cap);
    if (plan.counted && plan.used < plan.cap) {
        plan.kind = PlayerWritePlan::Kind::AddRow;
        return plan;
    }
    // Full (or its count unreadable: never risk Live Editor's insert): look for a row to take over
    plan.kind = PlayerWritePlan::Kind::Full;
    if (app.model.players().empty()) {
        plan.no_players = true;
        return plan;
    }
    std::vector<NameMapRow> rows;
    Snapshot snap;
    if (snap.load(app.db.memory(), *m))
        for (uint32_t i : snap.valid) rows.push_back({snap.get_int(i, *fp), snap.get_int(i, *fc), snap.addr(i)});
    plan.spare = app.callnames.spare_playernamemap_row(rows, [&](int64_t pid) { return app.model.player(pid) != nullptr; }, p.playerid);
    if (plan.spare.rec) plan.kind = PlayerWritePlan::Kind::TakeOver;
    return plan;
}

// "William Saliba (ID 1003, callname 980001: no recording in ita_it per your FC 26 list (...); he falls back to his
// name's callname)" - whose row is taken and why he hears nothing less
static std::string spare_row_text(App& app, const SpareRow& s) {
    const Callnames& cn = app.callnames;
    const std::string lang = cn.lang.empty() ? "the loaded language" : cn.lang;
    if (s.why == SpareWhy::NoPlayer)
        return "player " + std::to_string(s.playerid) + " (not in the database: no player uses the row; callname " + std::to_string(s.commentaryid) + ")";
    const PlayerRow* owner = app.model.player(s.playerid);
    std::string who = (owner ? owner->name : "player " + std::to_string(s.playerid)) + " (ID " + std::to_string(s.playerid) + ", callname " +
                      std::to_string(s.commentaryid);
    if (s.why == SpareWhy::NoCallname) return who + " = none: the row gives him nothing)";
    return who + ": no recording in " + lang + " per " + cn.silent_source(s.commentaryid) + "; he falls back to his name's callname)";
}

// The line shown before a By player write that does not edit the player's own row ("" when it edits it or adds a row)
static std::string takeover_text(App& app, const PlayerWritePlan& plan) {
    using K = PlayerWritePlan::Kind;
    if (plan.kind != K::TakeOver && plan.kind != K::Full) return "";
    const std::string count = plan.counted ? " (" + std::to_string(plan.used) + " of " + std::to_string(plan.cap) + " rows)" : " (its row count could not be read)";
    if (plan.kind == K::TakeOver)
        return "The playernamemap table is full" + count + ": this takes over the row of " + spare_row_text(app, plan.spare) + ".";
    if (plan.no_players) return "The playernamemap table is full" + count + " and the players are not loaded: no row can be taken over.";
    std::string why = "The playernamemap table is full" + count + " and no row can be taken over: " + std::to_string(plan.spare.spoken) +
                      (plan.spare.spoken == 1 ? " row holds a callname" : " rows hold a callname") + " spoken in " +
                      (app.callnames.lang.empty() ? std::string("the loaded language") : app.callnames.lang);
    if (plan.spare.unknown > 0) {
        // what is missing for an answer (Callnames::spoken_answer): the spoken set for 900001..965000, the list above
        const Callnames& cn = app.callnames;
        const char* missing = cn.masters.loaded()  ? "the spoken set is not built yet"
                              : cn.spoken.verified ? "ids above 965000 need a master list (an FC 27 master or your FC 26 list)"
                                                   : "the spoken set is not built yet and there is no master list";
        why += ", " + std::to_string(plan.spare.unknown) + (plan.spare.unknown == 1 ? " row a callname" : " rows a callname") +
               " Turbo cannot check (" + missing + ")";
    }
    return why + ". Pick the callname By name instead (Assign as last name / common name).";
}

// One playernamemap field through Database::set (range-checked; table and record checked alive). No undo step:
// playernamemap edits never had one. The Callname tab and the re-apply at career load write this way.
static bool put_int(App& app, const Table& t, uint64_t rec, const Field& f, int64_t v, std::string& err) {
    if (!app.db.set(t, rec, f, Value::of_int(v), &err)) return false;
    ++app.gen;
    app.log(t.name + "." + f.name + " = " + std::to_string(v));
    return true;
}

// BY PLAYER (and the re-apply at career load, ui_reapply.cpp, with allow_insert false): plan_player_write decides how
PlayerCallnameWrite write_player_callname(App& app, const PlayerRow& p, int64_t commentaryid, const std::string& from, bool allow_insert) {
    using K = PlayerWritePlan::Kind;
    using How = PlayerCallnameWrite::How;
    PlayerCallnameWrite r;
    const PlayerWritePlan plan = plan_player_write(app, p);
    const Table* m = app.db.table("playernamemap");
    if (plan.kind == K::NoTable || !m) {
        r.message = "FC 27's database has no playernamemap table (or it lacks playerid / commentaryid)";
        return r;
    }
    const Field& fp = *m->field("playerid");
    const Field& fc = *m->field("commentaryid");
    CallnameIndex& ix = app.callnames.index;
    const std::string what = p.name + ": player-specific callname " + std::to_string(commentaryid) + " (from " + from + ")";
    std::string err;
    if (plan.kind == K::InPlace) {
        ix.playernamemap_rec[p.playerid] = plan.rec;
        r.message = what;
        if (app.db.get_int(*m, plan.rec, "commentaryid", -1) == commentaryid) {
            r.how = How::Unchanged;
            return r;
        }
        if (!put_int(app, *m, plan.rec, fc, commentaryid, err)) {
            r.message = p.name + ": playernamemap.commentaryid not written: " + err;
            return r;
        }
        ix.playernamemap[p.playerid] = commentaryid;
        r.how = How::Updated;
        return r;
    }
    if (plan.kind == K::AddRow) {
        if (!allow_insert) {
            r.how = How::NeedsRow;
            r.message = p.name + ": he has no playernamemap row in this career; Turbo adds one only when you assign the callname again in "
                                 "Players > Callname";
            return r;
        }
        json a = {{"action", "set_playernamemap"}, {"playerid", p.playerid}, {"commentaryid", commentaryid}};
        add_room_check(app, a, plan.cap);
        if (!send_actions(app, json::array({a}), "Player callname")) {
            r.message = p.name + ": playernamemap row not queued: Turbo's command channel is busy or not available";
            r.notified = true;  // send() showed why
            return r;
        }
        r.how = How::Queued;
        r.message = p.name + ": playernamemap row (" + std::to_string(commentaryid) + ", from " + from + ") queued for Turbo's Lua side";
        return r;
    }
    if (plan.kind == K::TakeOver) {
        const std::string taken = spare_row_text(app, plan.spare);
        // Both values are checked before the first write. The callname first, then the player id; when the second write
        // fails the row gets its callname back (half a takeover would hand this callname to the row's old player)
        std::string verr = Database::validate(fc, Value::of_int(commentaryid));
        if (verr.empty()) verr = Database::validate(fp, Value::of_int(p.playerid));
        if (!verr.empty()) {
            r.message = p.name + ": no player-specific callname written: " + verr;
            return r;
        }
        if (!put_int(app, *m, plan.spare.rec, fc, commentaryid, err)) {
            r.message = p.name + ": the row of " + taken + " was not written: " + err;
            return r;
        }
        if (!put_int(app, *m, plan.spare.rec, fp, p.playerid, err)) {
            std::string ignored;
            app.db.set(*m, plan.spare.rec, fc, Value::of_int(plan.spare.commentaryid), &ignored);
            r.message = p.name + ": the row of " + taken + " was not written (put back as it was): " + err;
            return r;
        }
        ix.playernamemap_rec.erase(plan.spare.playerid);
        ix.playernamemap.erase(plan.spare.playerid);
        ix.playernamemap_rec[p.playerid] = plan.spare.rec;
        ix.playernamemap[p.playerid] = commentaryid;
        r.how = How::TookOver;
        r.message = what + "; the table is full, so the row of " + taken + " was taken over";
        return r;
    }
    r.message = p.name + ": no player-specific callname written. " + takeover_text(app, plan);
    return r;
}

static void assign_player_callname(App& app, const PlayerRow& p, int64_t commentaryid, const std::string& from) {
    const PlayerCallnameWrite r = write_player_callname(app, p, commentaryid, from, true);
    std::string msg = r.message;
    if (r.ok()) {
        // FC 27 reloads playernamemap at every career load: Turbo keeps the callname and writes it again then
        // (ui_reapply.cpp) - not for a player with his own recording, whom the game never speaks by it
        std::string why;
        if (!app.remember_player_callname(p.playerid, commentaryid, p.name, from, &why) && !why.empty()) msg += ". " + why;
    }
    if (!r.notified) app.notify(msg, !r.ok());
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Look for language packs, the spoken-id list and the master list again, rebuild the pickers");
    // Why this language: on its own line when it needs attention, else behind "(?)" (the tab is short of height: the
    // assignment buttons must stay visible in the default window)
    const bool attention = cn.lang.empty() || cn.lang_why.find("not installed") != std::string::npos ||
                           cn.lang_why.find("several") != std::string::npos;
    if (attention) {
        ImGui::TextDisabled("%s", cn.lang_why.c_str());
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cn.lang_why.c_str());
    }
    if (cn.no_game_root) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Game folder unknown: the tests or the host must set it.");
    if (cn.spoken.verified) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "Spoken set from %s: %zu names, %zu player callnames", cn.spoken.source.c_str(),
                           cn.spoken.ids.size(), cn.spoken.players.size());
    } else {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Unverified: %s", cn.spoken.source.c_str());
        if (!app.spoken_watch_line().empty()) {
            ImGui::TextWrapped("%s", app.spoken_watch_line().c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("In the career hub the game's commentary bank is not bound: every name answers 'no audio'. Turbo asks the\n"
                                  "game about a few ids every few seconds and builds the whole set as soon as the game answers 'yes' (on the\n"
                                  "Create Player screen, main menu or career, or in a match); the result is cached for the next sessions.");
        }
    }
    if (!cn.list_error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", cn.list_error.c_str());
    if (!cn.cache_error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", cn.cache_error.c_str());
    // The master list (an FC 27 master, else the user's FC 26 list): the second (and usually the bigger) source of "has
    // his own recording"
    if (cn.masters.loaded()) {
        ImGui::TextColored(kGreen, "%s: %zu players with their own recording, %zu generic names", cn.masters.label(true).c_str(),
                           cn.masters.real_players.size(), cn.masters.generic_ids.size());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nmade from %s (%s) by turbo\\tools\\import_callname_masters.py.\n%s", cn.masters.file.c_str(),
                              cn.masters.source.empty() ? "?" : cn.masters.source.c_str(), cn.masters.built.empty() ? "date unknown" : cn.masters.built.c_str(),
                              cn.masters.fc27() ? "FC 27 data, built from the game: a player listed 'real' there has his own recording."
                                                : "FC 26 data: FC 27 mostly reuses these recordings, so a player listed 'real' there is treated as\n"
                                                  "having his own recording, like the players the game's audio service names.");
    } else if (!cn.masters_error.empty()) {
        ImGui::TextColored(kOrange, "Master list not used: %s", cn.masters_error.c_str());
    } else if (!cn.lang.empty()) {
        ImGui::TextDisabled("No master list for %s: players with their own recording are known only from the game's audio service", cn.lang.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Looked for %s.\nMake it with: python turbo\\tools\\import_callname_masters.py (reads an FC 27 master\n"
                              "<name>_master_fc27.xlsm when there is one, else your FC 26 <name>_master.xlsm workbooks)",
                              cn.masters_path.c_str());
    }
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

// Above the assignment buttons of a player with his own recording: what will (not) happen. Kept short (the popup and
// the tooltip explain); its height is reserved below the picker list (list_height).
static std::string own_warning_text(const App& app, int own) {
    return "He has his own recording (" + app.callnames.own_source(own) + "): a callname set here will not be heard. Writing asks to confirm.";
}
static float own_warning_height(const App& app, int own) {
    if (!own) return 0.0f;
    return ImGui::CalcTextSize(own_warning_text(app, own).c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y +
           ImGui::GetStyle().ItemSpacing.y;
}
static void own_recording_warning(App& app, const PlayerRow& p, int own) {
    g_state.warning_shown = true;
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::TextWrapped("%s", own_warning_text(app, own).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s has his own recording in %s: the game says it before playernamemap and his name ids, so a name\n"
                          "picked here is written but not heard. To hear a callname, test it on a player without one\n"
                          "(tab 'Players without own recording').",
                          p.name.c_str(), app.callnames.lang.c_str());
}

// Height of a picker list: what the tab has left once `below` (the lines under the list) is kept, 80..200 px. The
// default window is short: with a fixed 200 px the assignment buttons ended below the visible part of the tab.
static float list_height(float below) {
    const float avail = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y - below;
    return std::max(S(80.0f), std::min(S(200.0f), avail));
}

// Straight to the write for a player without his own recording; else parked until the popup confirms it
static void request_name(App& app, const Table& t, const PlayerRow& p, const NameChoice& c, bool as_common, int own) {
    if (!own) {
        assign_name(app, t, p, c, as_common, g_keep_display);
        return;
    }
    g_pending = PendingAssign{};
    g_pending.kind = as_common ? PendingAssign::Kind::CommonName : PendingAssign::Kind::LastName;
    g_pending.playerid = p.playerid;
    g_pending.nameid = c.nameid;
    g_pending.keep_display = g_keep_display;
    g_open_own_confirm = true;
}
static void request_player_callname(App& app, const PlayerRow& p, const PlayerChoice& c, int own) {
    if (!own) {
        assign_player_callname(app, p, c.commentaryid, c.name);
        return;
    }
    g_pending = PendingAssign{};
    g_pending.kind = PendingAssign::Kind::Player;
    g_pending.playerid = p.playerid;
    g_pending.commentaryid = c.commentaryid;
    g_pending.from = c.name;
    g_open_own_confirm = true;
}

static void by_name_picker(App& app, const Table& t, const PlayerRow& p) {
    const CallnameIndex& ix = app.callnames.index;
    ImGui::SetNextItemWidth(S(260.0f));
    ImGui::InputTextWithHint("##cnsearch", "type a name or a name id", g_name_search, sizeof(g_name_search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu spoken names", ix.names.size());
    const int own = app.callnames.own_recording(p.playerid);
    const ImGuiStyle& style = ImGui::GetStyle();
    // under the list: the "Selected" line, the keep-name checkbox, the warning, the buttons
    const float below = ImGui::GetTextLineHeightWithSpacing() + 2.0f * ImGui::GetFrameHeightWithSpacing() + own_warning_height(app, own) +
                        style.WindowPadding.y;
    ImGui::BeginChild("##cnames", ImVec2(0, list_height(below)), ImGuiChildFlags_Borders);
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
    if (own) own_recording_warning(app, p, own);
    bool has_common = t.has("commonnameid"), has_last = t.has("lastnameid");
    if (!has_last) ImGui::BeginDisabled();
    if (ImGui::Button("Assign as last name")) request_name(app, t, p, *sel, false, own);
    if (!has_last) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!has_common) ImGui::BeginDisabled();
    if (ImGui::Button("Assign as common name")) request_name(app, t, p, *sel, true, own);
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
    const int own = app.callnames.own_recording(p.playerid);
    // A full table: whose row the write takes over, named before anything is written (or why none can be)
    const PlayerWritePlan plan = plan_player_write(app, p);
    const std::string takeover = takeover_text(app, plan);
    g_state.takeover_line = takeover;
    const float takeover_h =
        takeover.empty() ? 0.0f
                         : ImGui::CalcTextSize(takeover.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y + ImGui::GetStyle().ItemSpacing.y;
    // under the list: the "Selected" line, the takeover line, the warning, the button
    const float below = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() + takeover_h + own_warning_height(app, own) +
                        ImGui::GetStyle().WindowPadding.y;
    ImGui::BeginChild("##cplayers", ImVec2(0, list_height(below)), ImGuiChildFlags_Borders);
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
    auto takeover_line = [&]() {
        if (takeover.empty()) return;
        ImGui::PushStyleColor(ImGuiCol_Text, plan.kind == PlayerWritePlan::Kind::Full ? kOrange : kYellow);
        ImGui::TextWrapped("%s", takeover.c_str());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("FC 27 reloads playernamemap from its base data at every career load, so a taken-over row is back with\n"
                              "its player after the next load. Turbo takes only a row whose player is not in the database, whose\n"
                              "callname is none, or whose callname has no recording in %s (the game's audio service for\n"
                              "900001..965000, the master list above): no player loses a callname he hears.",
                              app.callnames.lang.c_str());
    };
    if (!sel) {
        ImGui::TextDisabled("Pick a player above: his player-specific callname is copied to this player.");
        takeover_line();
        return;
    }
    ImGui::Text("Selected: %s (%s, player %lld) speaks callname %lld", sel->name.c_str(), sel->club.empty() ? "no club" : sel->club.c_str(),
                static_cast<long long>(sel->playerid), static_cast<long long>(sel->commentaryid));
    takeover_line();
    if (own) own_recording_warning(app, p, own);
    if (ImGui::Button("Use this player's callname")) request_player_callname(app, p, *sel, own);
    ImGui::SameLine();
    ImGui::TextDisabled("(writes this player's playernamemap row; the shown name does not change)");
}

// The database rule's result ("900017 from last name 'Kane' (name 17)"), "" when it gives none
static std::string rule_result(App& app, const CallnameInfo& info) {
    if (info.source == CallnameSource::None) return "";
    std::string via;
    if (info.nameid > 0) {
        auto it = app.model.names_by_id().find(info.nameid);
        via = " '" + (it != app.model.names_by_id().end() ? it->second : std::string("?")) + "' (name " + std::to_string(info.nameid) + ")";
    }
    return std::to_string(info.commentaryid) + " from " + callname_source_name(info.source) + via;
}

// "PLAYER_LOW_SIMPLE + PLAYER_LOW_LINK" / "2 player-keyed tables" for the game's side, the list's name for the master list
static std::string own_details(const Callnames& cn, int64_t playerid, int own) {
    std::string out;
    if (own & kOwnFromGame) {
        auto it = cn.spoken.players.find(playerid);
        int v = it != cn.spoken.players.end() ? it->second : 0;
        std::string how;
        if (cn.spoken.players_from == SpokenSet::From::BankCapture) {
            how = std::to_string(v) + (v == 1 ? " player-keyed table" : " player-keyed tables");
        } else {
            if (v & caudio::kPlayerLowSimple) how += "PLAYER_LOW_SIMPLE";
            if (v & caudio::kPlayerLowLink) how += std::string(how.empty() ? "" : " + ") + "PLAYER_LOW_LINK";
        }
        if (!how.empty()) out = "the game: " + how;
    }
    if (own & kOwnFromMasters) {
        auto it = cn.masters.names.find(playerid);
        out += std::string(out.empty() ? "" : "; ") + cn.masters.label() + ": " +
               (it != cn.masters.names.end() ? "'" + it->second + "'" : std::string("listed as 'real'"));
    }
    return out;
}

// Third picker tab: the players of this player's club without their own recording, i.e. the ones on whom an assigned
// callname can be heard (a test player). A click opens the player. A tab, not a button line: the tab has no height to spare.
static void club_players_tab(App& app, const PlayerRow& p) {
    const Callnames& cn = app.callnames;
    if (p.club <= 0) {
        ImGui::TextDisabled("%s has no club: open a club player to see his team-mates without their own recording.", p.name.c_str());
        return;
    }
    std::vector<const PlayerRow*> without, with;
    for (const auto& pr : app.model.players())
        if (pr.club == p.club) (cn.own_recording(pr.playerid) ? with : without).push_back(&pr);
    ImGui::Text("%s: %zu of %zu players have no own recording in %s", p.club_name.c_str(), without.size(), without.size() + with.size(),
                cn.lang.empty() ? "the loaded language" : cn.lang.c_str());
    ImGui::SameLine();
    const std::string sources = cn.masters.loaded() ? "(game's audio service + " + cn.masters.label() + ")" : "(game's audio service only: no master list)";
    ImGui::TextDisabled("%s", sources.c_str());
    // under the list: one line naming the players with their own recording
    const float below = ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().WindowPadding.y;
    ImGui::BeginChild("##cnclublist", ImVec2(0, list_height(below)), ImGuiChildFlags_Borders);
    for (const PlayerRow* w : without) {
        std::string label = w->name + " (" + std::to_string(w->playerid) + ")##cnclub";
        if (ImGui::Selectable(label.c_str(), w->playerid == p.playerid)) app.sel_player = w->playerid;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open %s: a callname assigned to him is the one the game says", w->name.c_str());
    }
    if (without.empty()) ImGui::TextDisabled("Every player of %s has his own recording.", p.club_name.c_str());
    ImGui::EndChild();
    if (!with.empty()) {
        std::string names;
        for (const PlayerRow* w : with) names += (names.empty() ? "" : ", ") + w->name;
        ImGui::TextDisabled("With their own recording (a callname is not heard): %s", names.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", names.c_str());
    }
}

// The confirmation of an assignment to a player with his own recording (parked by request_name / request_player_callname)
static void own_confirm_popup(App& app, const Table& t, const PlayerRow& p) {
    if (g_open_own_confirm) {
        ImGui::OpenPopup("##cnown");
        g_open_own_confirm = false;
    }
    if (!ImGui::BeginPopupModal("##cnown", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    g_state.confirm_open = true;
    if (g_pending.kind == PendingAssign::Kind::None || g_pending.playerid != p.playerid) {
        // another player was opened meanwhile: nothing to confirm for this one
        g_pending = PendingAssign{};
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    Callnames& cn = app.callnames;
    const int own = cn.own_recording(p.playerid);
    const NameChoice* name = nullptr;
    for (const auto& c : cn.index.names)
        if (c.nameid == g_pending.nameid) name = &c;
    std::string what;
    if (g_pending.kind == PendingAssign::Kind::Player)
        what = "write callname " + std::to_string(g_pending.commentaryid) + " (" + g_pending.from + "'s) to his playernamemap row";
    else
        what = std::string("assign '") + (name ? name->name : "?") + "' (callname " + (name ? std::to_string(name->commentaryid) : std::string("?")) +
               ") as his " + (g_pending.kind == PendingAssign::Kind::CommonName ? "common" : "last") + " name";
    ImGui::TextColored(kOrange, "%s (ID %lld) has his own recording in %s (%s).", p.name.c_str(), static_cast<long long>(p.playerid),
                       cn.lang.c_str(), cn.own_source(own).c_str());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(560.0f));
    ImGui::TextWrapped("The game uses a player's own recording before playernamemap and his name ids, so the commentator keeps "
                       "saying his own name: the callname written now will not be heard while that recording exists.");
    if (cn.own_unconfirmed(own))
        ImGui::TextWrapped("Only your FC 26 list (FC 26 data) says so: the game's audio service did not list him, but its list is known "
                           "to miss players.");
    if (g_pending.kind == PendingAssign::Kind::Player) {
        ImGui::TextWrapped("For this career session only: Turbo does not write it again at the next career load.");
        // a full table: the other player whose row the write takes over is named here too, before anything is written
        const PlayerWritePlan plan = plan_player_write(app, p);
        g_state.confirm_takeover = takeover_text(app, plan);
        if (!g_state.confirm_takeover.empty())
            ImGui::TextColored(plan.kind == PlayerWritePlan::Kind::Full ? kOrange : kYellow, "%s", g_state.confirm_takeover.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Text("Write it anyway: %s?", what.c_str());
    if (ImGui::Button("Assign anyway##cnown")) {
        const PendingAssign a = g_pending;
        g_pending = PendingAssign{};
        if (a.kind == PendingAssign::Kind::Player) {
            assign_player_callname(app, p, a.commentaryid, a.from);
        } else if (name) {
            assign_name(app, t, p, *name, a.kind == PendingAssign::Kind::CommonName, a.keep_display);
        } else {
            app.notify(p.name + ": the picked name is no longer in the list; nothing written", true);
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##cnown")) {
        g_pending = PendingAssign{};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void callname_editor(App& app, const Table& t, const PlayerRow& p) {
    ensure_ready(app);
    Callnames& cn = app.callnames;
    g_state = CallnameTabState{};
    g_state.playerid = p.playerid;
    language_line(app);
    ImGui::Separator();

    CallnameInfo info = cn.resolve(p, app.db);
    g_state.own = info.own;
    const std::string rule = rule_result(app, info);
    ImGui::AlignTextToFramePadding();
    if (info.own) {
        // Step 0 of the game's rule: his own recording wins, so the line names it - never "none"
        g_state.current_line = "Current callname: his own recording in " + cn.lang + " (" + cn.own_source(info.own) +
                               (cn.own_unconfirmed(info.own) ? "; not confirmed by the game's audio service, whose list is incomplete" : "") + ")";
        ImGui::TextColored(kGreen, "%s", g_state.current_line.c_str());
        if (ImGui::IsItemHovered()) {
            const std::string details = own_details(cn, p.playerid, info.own);
            ImGui::SetTooltip("Own (\"Real\") recordings are bound to the player id inside the bank, not to a commentary id: the\n"
                              "commentator says this name whatever playernamemap or the name ids give.%s%s",
                              details.empty() ? "" : "\n", details.c_str());
        }
        g_state.rule_line = "Not used while he has it: the callname rule gives " + (rule.empty() ? std::string("none") : rule);
        ImGui::TextDisabled("%s", g_state.rule_line.c_str());
    } else if (info.source == CallnameSource::None) {
        // Without a master list the game's audio service is the only source, and it misses most own recordings: say so
        // rather than a flat "the commentary does not say his name"
        g_state.current_line = cn.masters.loaded()
                                   ? std::string("Current callname: none (the commentary does not say this player's name)")
                                   : "Current callname: none from the callname rule (no master list for " + cn.lang +
                                         ": an own recording of his would not be known here)";
        ImGui::Text("%s", g_state.current_line.c_str());
    } else {
        bool spoken = cn.spoken.spoken(info.commentaryid);
        g_state.current_line = "Current callname: " + rule + " - ";
        ImGui::Text("%s", g_state.current_line.c_str());
        ImGui::SameLine();
        const std::string tail = std::string(!spoken ? "NOT spoken in " : cn.spoken.verified ? "spoken in " : "used by playernames (unverified) in ") + cn.lang;
        g_state.current_line += tail;
        ImGui::TextColored(spoken ? kGreen : kOrange, "%s", tail.c_str());
        if (info.source == CallnameSource::CommonName && !spoken)
            ImGui::TextDisabled("He has a common name, so the game uses it and never his last name: use 'Assign as common name'.");
    }
    if (cn.index.playernamemap.count(p.playerid)) {
        if (ImGui::Button("Remove player-specific callname...")) ImGui::OpenPopup("##rmcallname");
        ImGui::SameLine();
        ImGui::TextDisabled("(back to the common / last name rule)");
    }
    // the callname Turbo keeps for this player and writes again at every career load (ui_reapply.cpp), with Forget
    reapply_callname_line(app, p);
    reapply_status_line(app);
    if (ImGui::BeginPopupModal("##rmcallname", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Remove the playernamemap row of %s (ID %lld)?", p.name.c_str(), static_cast<long long>(p.playerid));
        ImGui::TextDisabled("The commentary falls back to his common or last name. Done by Turbo's Lua side on the next career event.");
        if (ImGui::Button("Remove")) {
            json a = {{"action", "remove_playernamemap"}, {"playerid", p.playerid}};
            send_actions(app, json::array({a}), "Remove player callname");
            // meant for good: a kept callname is not written again at the next career load
            app.forget_player_callname(p.playerid);
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
        if (ImGui::BeginTabItem("Players without own recording")) {
            club_players_tab(app, p);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // outside the tab items: both pickers park their request here (an ID pushed by a tab item would hide the popup)
    own_confirm_popup(app, t, p);
}

}  // namespace turbo
