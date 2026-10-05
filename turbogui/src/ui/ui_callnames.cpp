// FC 27 LE Turbo GUI - Players > Callname tab: the name the commentary speaks for the loaded commentary language,
// where it comes from (player-specific / common name / last name), and two type-ahead pickers to assign another one:
//   BY NAME   a playernames name whose commentary id is spoken -> his player-specific callname when a playernamemap
//             row can be used (1.0.3: no name changes, plan_generic_route), else written to lastnameid or commonnameid
//             (the shown name kept through editedplayernames, written first)
//   BY PLAYER a player whose playernamemap callname is spoken -> written to this player's playernamemap row (added
//             through Turbo's Lua side when missing, which counts the rows again first; when the table is full, a row
//             from which no player hears a callname is taken over, and its player is named before the write); kept in
//             turbo_output\reapply_edits.json and written again at every career load, because FC 27 reloads
//             playernamemap then (ui_reapply.cpp)
// A player with his OWN recording (the game's audio service or the master list says so: an FC 27 master or the user's
// FC 26 list) is spoken from it whatever either picker writes (docs/callnames.md section 1 step 0): the tab says so in
// the "Current callname" line, warns above the assignment buttons and asks for a confirmation before anything is
// written for him; a callname written for him anyway is not kept for the next career loads.
// VOICE SWAPS (1.1.0, core/callname_voice.h): in matches only, nothing written to the database. All callnames gives a
// player another player's own recording ("Use his voice") or a generic callname ("Use in matches"; a player with his own
// recording unticks "Use his own recording" first); the Voice swaps tab lists them. The older database buttons follow
// under "Change the name in the database"; they are all that shows while the host's hooks are off.
// See core/callnames.h and docs/callnames.md.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "ui_callname_play.h"
#include "ui_names.h"

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
    std::string from;          // By player: whose callname it is; a generic callname: "the generic callname 'Kane'"
    bool generic = false;      // By name / All callnames: a generic callname on the player-specific route
    bool keep_display = true;
    NameChoice alt;            // All callnames: the picked name row (it may not be among the index's spoken names)
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

// ShownName / shown_name: ui_names.h (shared with the Names tab)

static bool send_actions(App& app, const json& actions, const std::string& label) {
    return app.send({{"op", "run"}, {"module", "callnames"}, {"overrides", {{"actions", actions}}}}, label);
}

// add_room_check: ui_names.h

// The game shows the new name while the career stays loaded, whatever editedplayernames says (seen in game, 1.0.2)
static const char* const kNameRouteNote = ". The game shows the new name until the career is reloaded.";

// BY NAME, the name route (1.0.3): the kept-name row first, then the name id. A player with an editedplayernames row:
// the row is edited in place (shown names and his shirt name) and a refused write stops the name id. A player without
// one: Turbo's Lua side adds the row and then writes the name id, in ONE command ([set_display_name, set_name_ids],
// queued until the mailbox is free; a refused row stops the name id). Never an insert into a full table.
static void assign_name(App& app, const Table& t, const PlayerRow& p, const NameChoice& c, bool as_common, bool keep_display) {
    const char* id_field = as_common ? "commonnameid" : "lastnameid";
    const Field* f = t.field(id_field);
    if (!f) {
        app.notify("players has no " + std::string(id_field) + " field", true);
        return;
    }
    ShownName before = shown_name(app, t, p);
    std::string msg = p.name + ": " + (as_common ? "common name" : "last name") + " -> '" + c.name + "' (name " +
                      std::to_string(c.nameid) + ", callname " + std::to_string(c.commentaryid) + ")";
    const Table* e = keep_display ? app.db.table("editedplayernames") : nullptr;
    if (!e) {
        if (!app.edit(t, p.rec, *f, Value::of_int(c.nameid))) return;
        app.notify(keep_display ? msg + "; no editedplayernames table: the shown name follows the new name id" : msg, keep_display);
        return;
    }
    if (before.edited_rec) {
        bool ok = true;
        if (const Field* ff = e->field("firstname")) ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.first));
        if (const Field* ff = e->field("surname")) ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.last));
        if (const Field* ff = e->field("commonname")) ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.common));
        if (const Field* ff = e->field("playerjerseyname"); ff && !before.jersey.empty())
            ok = ok && app.edit(*e, before.edited_rec, *ff, Value::of_str(before.jersey));
        if (!ok) {
            app.notify(p.name + ": the shown name could not be kept, so the " + id_field + " was not written", true);
            return;
        }
        if (!app.edit(t, p.rec, *f, Value::of_int(c.nameid))) return;
        app.notify(msg + "; shown name kept (editedplayernames written first)" + kNameRouteNote);
        return;
    }
    uint32_t used = 0, cap = 0;
    if (!app.db.rows_in_use(*e, used, cap) || used >= cap) {
        app.notify(p.name + ": nothing written: the editedplayernames table is full, so the shown name cannot be kept "
                            "(untick 'Keep the shown name' to write the name anyway)",
                   true);
        return;
    }
    json row = {{"action", "set_display_name"}, {"playerid", p.playerid}, {"firstname", before.first},
                {"surname", before.last}, {"commonname", before.common}, {"playerjerseyname", before.jersey}};
    add_room_check(app, row, cap);
    const json ids = {{"action", "set_name_ids"}, {"playerid", p.playerid}, {id_field, c.nameid}};
    // one command, the row first (queued in the GUI: Turbo's mailbox holds one command at a time)
    app.lua_queue.push_group({row.dump(), ids.dump()});
    const bool waiting = app.busy();
    app.flush_lua_queue();
    app.notify(msg + (waiting ? ": queued for Turbo's Lua side (kept-name row first, then the name id), sent when the running command ends"
                              : ": sent to Turbo's Lua side (kept-name row first, then the name id; next career event)") +
               kNameRouteNote);
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
        if (cn.masters.fc27())
            ImGui::TextColored(kGreen, "FC 27 master: %zu players with their own recording (decides alone); spoken surnames = its %zu generic ids"
                               " + the game's audio service set", cn.masters.real_players.size(), cn.masters.generic_ids.size());
        else
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

// A list row's play button under the row's own ID scope: several rows can carry the same commentary id (FC 27's
// playernamemap gives 922045 to 11 players), and "##play_g<id>" alone would be one ImGui ID for all of them (the red
// "conflicting ID" tooltip in game). `row` is the row's key: the name id (By name), the player id (By player), the
// list key (All callnames).
static void row_play_button(App& app, int64_t row, CallnameAudioKind kind, int64_t id) {
    char scope[32];
    std::snprintf(scope, sizeof(scope), "##row%lld", static_cast<long long>(row));
    ImGui::PushID(scope);
    callname_play_button(app, kind, id);
    ImGui::PopID();
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
static void request_player_callname(App& app, const PlayerRow& p, const PlayerChoice& c, int own, bool generic = false) {
    if (!own) {
        assign_player_callname(app, p, c.commentaryid, c.name);
        return;
    }
    g_pending = PendingAssign{};
    g_pending.kind = PendingAssign::Kind::Player;
    g_pending.playerid = p.playerid;
    g_pending.commentaryid = c.commentaryid;
    g_pending.from = c.name;
    g_pending.generic = generic;
    g_open_own_confirm = true;
}

// 1.0.3: how a generic callname (By name, All callnames) reaches a player. The player-specific route (playernamemap:
// his row in place, a row added when the table has room, else a spare row: plan_player_write) changes no name and is
// written again at every career load (1.0.2); it is taken when the callname has a known recording, because the game
// says a playernamemap callname only then (else it falls back to his name ids): the verified spoken set or the
// master's generic ids. Else the name route (assign_name): the game shows the new name until the career is reloaded.
struct GenericRoute {
    bool player = false;  // the player-specific route
    bool audio = false;   // the callname has a known recording
    PlayerWritePlan plan;
};
static GenericRoute plan_generic_route(App& app, const PlayerRow& p, int64_t commentaryid) {
    using K = PlayerWritePlan::Kind;
    const Callnames& cn = app.callnames;
    GenericRoute r;
    r.audio = commentaryid > kNoCallname &&
              ((cn.spoken.verified && cn.spoken.spoken(commentaryid)) || cn.masters.generic_ids.count(commentaryid) > 0);
    r.plan = plan_player_write(app, p);
    r.player = r.audio && (r.plan.kind == K::InPlace || r.plan.kind == K::AddRow || r.plan.kind == K::TakeOver);
    return r;
}
// The one line shown before the click (kept in g_state.route_line); a taken-over row is named in its tooltip
static void route_line(App& app, const GenericRoute& r, bool has_name_row) {
    using K = PlayerWritePlan::Kind;
    std::string line;
    if (r.player) {
        line = std::string("Route: player-specific callname (") +
               (r.plan.kind == K::InPlace ? "his playernamemap row" : r.plan.kind == K::AddRow ? "a new playernamemap row" : "a spare playernamemap row") +
               "). No name changes.";
    } else {
        const std::string why = r.audio ? "no playernamemap row free" : "no recording known for a player-specific callname";
        line = has_name_row ? "Route: name id (" + why + "). The game shows the new name until the career is reloaded."
                            : "Nothing can be written: " + why + ", and no name row has this callname.";
    }
    g_state.route_line = line;
    ImGui::PushStyleColor(ImGuiCol_Text, r.player ? kGreen : kYellow);
    ImGui::TextWrapped("%s", line.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        if (r.player && r.plan.kind == K::TakeOver)
            ImGui::SetTooltip("Takes over the row of %s.", spare_row_text(app, r.plan.spare).c_str());
        else if (!r.player)
            ImGui::SetTooltip("%s", takeover_text(app, r.plan).empty() ? "The player-specific route needs a known recording of this callname."
                                                                       : takeover_text(app, r.plan).c_str());
    }
}
// The line next to "Assign callname": a player with his own recording is not kept for the next career loads
// (App::remember_player_callname refuses him), so his write lasts this career session only
static void assign_callname_note(int own) {
    g_state.assign_note = own ? "(his playernamemap row; this career session only: he has his own recording)"
                              : "(his playernamemap row; written again at every career load)";
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_state.assign_note.c_str());
}
// The player-specific route for a generic callname (the own-recording popup first, as By player)
static void request_generic_callname(App& app, const PlayerRow& p, const std::string& text, int64_t commentaryid, int own) {
    // "the generic callname 'Kane'": kept as the callname's origin (reapply_edits.json, toasts) and named in the popup
    PlayerChoice c;
    c.name = "the generic callname " + (text.empty() ? std::to_string(commentaryid) : "'" + text + "'");
    c.commentaryid = commentaryid;
    request_player_callname(app, p, c, own, true);
}

static void by_name_picker(App& app, const Table& t, const PlayerRow& p) {
    const CallnameIndex& ix = app.callnames.index;
    ImGui::SetNextItemWidth(S(260.0f));
    ImGui::InputTextWithHint("##cnsearch", "type a name or a name id", g_name_search, sizeof(g_name_search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu spoken names", ix.names.size());
    const int own = app.callnames.own_recording(p.playerid);
    const ImGuiStyle& style = ImGui::GetStyle();
    // under the list: the "Selected" line, the route line (up to two lines), the keep-name checkbox, the warning, the buttons
    const float below = 3.0f * ImGui::GetTextLineHeightWithSpacing() + 2.0f * ImGui::GetFrameHeightWithSpacing() + own_warning_height(app, own) +
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
            ImGui::SetNextItemAllowOverlap();  // the play button below takes its own clicks
            if (ImGui::Selectable(idbuf, g_sel_name == c.nameid, ImGuiSelectableFlags_SpanAllColumns)) g_sel_name = c.nameid;
            ImGui::TableNextColumn();
            row_play_button(app, c.nameid, CallnameAudioKind::Generic, c.commentaryid);
            ImGui::SameLine();
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
    // 1.0.3: the player-specific route when it can be used (no name changes), else the name ids below
    const GenericRoute route = plan_generic_route(app, p, sel->commentaryid);
    route_line(app, route, true);
    if (route.player) {
        if (own) own_recording_warning(app, p, own);
        if (ImGui::Button("Assign callname")) request_generic_callname(app, p, sel->name, sel->commentaryid, own);
        assign_callname_note(own);
        return;
    }
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

// ---------------------------------------------------------------- voice swaps (1.1.0, core/callname_voice.h)
// In matches only: B is called with A's own recording ("Use his voice"), or a generic callname is used for him ("Use in
// matches"; for a player with his own recording only once it is turned off). Kept in voice_swaps.json under
// turbo_output\callnames for every career and published to the host's hooks (App::voice_upsert); nothing is written to
// the database, so the name on screen stays his.

// The name the screen shows for a player (common name, else last name), else the last word of `fallback`
static std::string screen_surname(App& app, const Table& t, int64_t playerid, const std::string& fallback) {
    std::string full = fallback;
    if (const PlayerRow* r = app.model.player(playerid)) {
        const ShownName n = shown_name(app, t, *r);
        if (!n.common.empty()) return n.common;
        if (!n.last.empty()) return n.last;
        if (full.empty()) full = r->name;
    }
    const size_t sp = full.find_last_of(' ');
    if (!full.empty() && sp != std::string::npos && sp + 1 < full.size()) return full.substr(sp + 1);
    return full.empty() ? "player " + std::to_string(playerid) : full;
}

// Who he is called after: A's surname for a voice swap, the generic callname's text, else its id
static std::string voice_source(App& app, const Table& t, const voice::Entry& e) {
    if (e.voice_of && *e.voice_of > 0) return screen_surname(app, t, *e.voice_of, e.from);
    if (e.kickoff && *e.kickoff > 0) return e.from.empty() ? "callname " + std::to_string(*e.kickoff) : e.from;
    return "";
}

// "Lobotka's own recording (voice swap)", "Del Piero (generic), own recording off", "own recording off"
static std::string voice_called(App& app, const Table& t, const voice::Entry& e) {
    const std::string src = voice_source(app, t, e);
    if (e.voice_of && *e.voice_of > 0) return src + "'s own recording (voice swap)";
    if (e.voice_of) return src.empty() ? "own recording off" : src + ", own recording off";
    return src.empty() ? "surname lines silent" : src;
}

// The surname lines (the callname set at kick-off): "silent", the generic callname, or the game's rule
static std::string voice_other_lines(const voice::Entry& e) {
    if (e.kickoff && *e.kickoff == -1) return "silent";
    if (e.kickoff) return e.from.empty() ? "callname " + std::to_string(*e.kickoff) : e.from;
    if (e.voice_of && *e.voice_of > 0) return "silent";  // build_table's default for a swap
    return "the callname rule";
}

// "<A> has no recording in <lang>: silent" when the FC 27 master (or the verified spoken set, for a generic id) says
// the source has none; "" when it has one or nobody can tell
static std::string voice_silent_text(App& app, const Table& t, const voice::Entry& e) {
    const Callnames& cn = app.callnames;
    const std::string lang = cn.lang.empty() ? "the loaded language" : cn.lang;
    const std::string who = voice_source(app, t, e);
    if (e.voice_of && *e.voice_of > 0) {
        if (!cn.masters.fc27() || cn.own_recording(*e.voice_of)) return "";
        return who + " has no recording in " + lang + ": silent";
    }
    if (e.kickoff && *e.kickoff > 0) {
        const bool known = cn.masters.fc27() || cn.spoken.verified;
        const bool has = cn.masters.generic_ids.count(*e.kickoff) > 0 || (cn.spoken.verified && cn.spoken.spoken(*e.kickoff));
        if (!known || has) return "";
        return who + " has no recording in " + lang + ": silent";
    }
    return "";
}

static void voice_off_line(App& app) {
    g_state.voice_off_line = "Voice swaps are off: " + app.voice_why_off();
    ImGui::TextDisabled("%s", g_state.voice_off_line.c_str());
}

// The play button for what he is called in matches: A's own recording, else the generic callname
static void voice_play_button(App& app, const voice::Entry& e) {
    if (e.voice_of && *e.voice_of > 0) callname_play_button(app, CallnameAudioKind::Own, *e.voice_of);
    else if (e.kickoff && *e.kickoff > 0) callname_play_button(app, CallnameAudioKind::Generic, *e.kickoff);
    else return;
    ImGui::SameLine();
}

// A swap waits here for its confirmation (##vsconfirm)
struct PendingVoice {
    int64_t playerid = 0;
    voice::Entry entry;
    std::string line;    // "Marianucci will be called Lobotka in matches. His name on screen stays Marianucci."
    std::string button;  // "Use his voice" / "Use in matches"
};
static PendingVoice g_pvoice;
static bool g_open_voice_confirm = false;

// All callnames: "Use his voice" (an own-recording row) or "Use in matches" (a generic row), parked for the popup
static void request_voice(App& app, const Table& t, const PlayerRow& p, const AllCallnameRow& r, int own) {
    voice::Entry e;
    e.playerid = p.playerid;
    e.player = p.name;
    // Name lines only belongs to a voice swap (the list shows it only there): never carried into own recording off,
    // where it would turn his own recording off in the name lines only
    if (const voice::Entry* old = app.voice_store.find(p.playerid); old && r.own()) e.names_only = old->names_only;
    std::string called;
    if (r.own()) {
        const PlayerRow* a = app.model.player(r.playerid);
        e.voice_of = r.playerid;
        e.kickoff = -1;  // his surname lines silent: the game's own pattern for a player with his own recording
        e.from = a ? a->name : (r.text.empty() ? "player " + std::to_string(r.playerid) : r.text);
        called = screen_surname(app, t, r.playerid, e.from);
    } else {
        if (own) e.voice_of = 0;  // his own recording off (the checkbox), the generic callname instead
        e.kickoff = r.commentaryid;
        called = r.text.empty() ? "callname " + std::to_string(r.commentaryid) : r.text;
        e.from = called + " (generic)";
    }
    const std::string b = screen_surname(app, t, p.playerid, p.name);
    g_pvoice = PendingVoice{};
    g_pvoice.playerid = p.playerid;
    g_pvoice.entry = e;
    g_pvoice.line = b + " will be called " + called + " in matches. His name on screen stays " + b + ".";
    g_pvoice.button = r.own() ? "Use his voice" : "Use in matches";
    g_open_voice_confirm = true;
}

static void voice_confirm_popup(App& app, const Table& t, const PlayerRow& p) {
    if (g_open_voice_confirm) {
        ImGui::OpenPopup("##vsconfirm");
        g_open_voice_confirm = false;
    }
    if (!ImGui::BeginPopupModal("##vsconfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    g_state.voice_confirm_open = true;
    if (g_pvoice.playerid != p.playerid) {
        // another player was opened meanwhile: nothing to confirm for this one
        g_pvoice = PendingVoice{};
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    g_state.voice_confirm_line = g_pvoice.line;
    ImGui::TextUnformatted(g_pvoice.line.c_str());
    ImGui::TextDisabled("In matches only. Nothing is written to the database.");
    if (ImGui::Button((g_pvoice.button + "##vsconfirm").c_str())) {
        const voice::Entry e = g_pvoice.entry;
        g_pvoice = PendingVoice{};
        if (app.voice_upsert(e)) app.notify(p.name + ": " + voice_called(app, t, e) + " in matches");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##vsconfirm")) {
        g_pvoice = PendingVoice{};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The voice-swap button under the All callnames pick (the database buttons follow under "Change the name in the
// database"); false when the service is off (the off line is drawn, only the database buttons follow)
static bool voice_pick_buttons(App& app, const Table& t, const PlayerRow& p, const AllCallnameRow& r, int own) {
    if (!app.voice_available()) {
        voice_off_line(app);
        return false;
    }
    const voice::Entry* ve = app.voice_store.find(p.playerid);
    if (r.own()) {
        const bool self = r.playerid == p.playerid;
        if (self) ImGui::BeginDisabled();
        if (ImGui::Button("Use his voice##all")) request_voice(app, t, p, r, own);
        if (self) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", self ? "(his own recording)" : "(in matches only; no database write)");
        return true;
    }
    // a player with his own recording on: the game says it first, so a generic callname needs it off
    const bool own_on = own && !(ve && ve->voice_of);
    if (own_on) {
        g_state.voice_turn_off_shown = true;
        ImGui::TextColored(kOrange, "Turn off his own recording first");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Untick 'Use his own recording' above.");
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Use in matches##all")) request_voice(app, t, p, r, own);
    if (own_on) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(in matches only; no database write)");
    return true;
}

// Fifth picker tab: every voice swap kept (all careers), Name lines only, Remove, Forget all
static void voice_swaps_tab(App& app, const Table& t) {
    if (!app.voice_error.empty()) ImGui::TextColored(kOrange, "%s", app.voice_error.c_str());
    if (!app.voice_available()) voice_off_line(app);
    auto& entries = app.voice_store.entries;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%zu voice swap%s (every career)", entries.size(), entries.size() == 1 ? "" : "s");
    if (!entries.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Forget all##vs")) ImGui::OpenPopup("##vsforgetall");
    }
    if (ImGui::BeginPopupModal("##vsforgetall", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Forget all %zu voice swaps?", entries.size());
        ImGui::TextDisabled("Every player is called as the game says again.");
        if (ImGui::Button("Forget all##vsforgetall")) {
            app.voice_forget_all();
            app.notify("Voice swaps: all forgotten");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##vsforgetall")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (entries.empty()) {
        ImGui::TextDisabled("None yet: All callnames > 'Use his voice' or 'Use in matches'.");
        return;
    }
    int64_t remove = 0;
    voice::Entry toggled;
    bool toggle = false;
    ImGui::BeginChild("##vslist", ImVec2(0, list_height(ImGui::GetStyle().WindowPadding.y)), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##vstable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Called");
        ImGui::TableSetupColumn("Other lines", ImGuiTableColumnFlags_WidthFixed, S(130.0f));
        ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, S(70.0f));
        ImGui::TableHeadersRow();
        for (const voice::Entry& e : entries) {
            ++g_state.voice_rows;
            const std::string id = std::to_string(e.playerid);
            // one ID scope per row: two swaps from one source draw the same play button (##play_o<A>)
            ImGui::PushID(id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const PlayerRow* pr = app.model.player(e.playerid);
            const std::string who = (pr ? pr->name : (e.player.empty() ? std::string("player") : e.player)) + " (" + id + ")";
            if (pr) {
                if (ImGui::Selectable((who + "##vsp" + id).c_str(), app.sel_player == e.playerid)) app.sel_player = e.playerid;
            } else {
                ImGui::TextUnformatted(who.c_str());
                if (app.model.built()) {
                    ++g_state.voice_absent;
                    ImGui::TextDisabled("not in this career");
                }
            }
            ImGui::TableNextColumn();
            voice_play_button(app, e);
            ImGui::TextUnformatted(voice_called(app, t, e).c_str());
            if (e.voice_of && *e.voice_of > 0) {
                bool names = e.names_only;
                if (ImGui::Checkbox(("Name lines only##vs" + id).c_str(), &names)) {
                    toggled = e;
                    toggled.names_only = names;
                    toggle = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only the lines that say his name use the other voice; his other lines stay his.");
            }
            const std::string silent = voice_silent_text(app, t, e);
            if (!silent.empty()) ImGui::TextColored(kOrange, "%s", silent.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(voice_other_lines(e).c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(("Remove##vs" + id).c_str())) remove = e.playerid;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    if (toggle) app.voice_upsert(toggled);
    if (remove && app.voice_forget(remove)) app.notify("Voice swap removed (player " + std::to_string(remove) + ")");
}

// ALL CALLNAMES: every callname of the language the master lists (generic surnames and players' own recordings)
static char g_all_search[64] = "";
static int64_t g_sel_all = 0;  // > 0: a generic commentary id; < 0: -playerid of an own recording
static void all_callnames_picker(App& app, const Table& t, const PlayerRow& p) {
    Callnames& cn = app.callnames;
    if (!cn.masters.loaded()) {
        ImGui::TextDisabled("No master list for %s: this list needs one (turbo\\callnames\\masters\\%s.json).", cn.lang.c_str(), cn.lang.c_str());
        return;
    }
    // rebuilt when the master list or the database changes (Refresh, a new model)
    static std::vector<AllCallnameRow> rows;
    static std::string rows_key;
    const std::string key = cn.masters.file + "|" + cn.masters.built + "|" + std::to_string(cn.masters.generic_ids.size()) + "|" +
                            std::to_string(cn.masters.real_players.size()) + "|" + std::to_string(cn.index.model_version);
    if (key != rows_key) {
        rows = all_callnames(cn.masters, cn.index, [&](int64_t pid) {
            const PlayerRow* r = app.model.player(pid);
            return r ? r->name : std::string();
        });
        rows_key = key;
    }
    ImGui::SetNextItemWidth(S(260.0f));
    ImGui::InputTextWithHint("##cnallsearch", "type a callname, player or id", g_all_search, sizeof(g_all_search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu generic callnames + %zu own recordings (%s)", cn.masters.generic_ids.size(), cn.masters.real_players.size(),
                        cn.masters.label().c_str());
    const int own = cn.own_recording(p.playerid);
    // + the voice-swap button and the "Change the name in the database" title (1.1.0)
    const float below = ImGui::GetTextLineHeightWithSpacing() * 6.0f + 3.0f * ImGui::GetFrameHeightWithSpacing() + own_warning_height(app, own) +
                        ImGui::GetStyle().WindowPadding.y;
    ImGui::BeginChild("##cnall", ImVec2(0, list_height(below)), ImGuiChildFlags_Borders);
    int shown = 0, total = 0;
    const AllCallnameRow* sel = nullptr;
    if (ImGui::BeginTable("##cnalltable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableSetupColumn("Callname");
        ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthFixed, S(200.0f));
        ImGui::TableHeadersRow();
        for (const auto& r : rows) {
            const int64_t key = r.own() ? -r.playerid : r.commentaryid;
            if (key == g_sel_all) sel = &r;
            if (!callname_filter_match(g_all_search, r.text, r.own() ? r.playerid : r.commentaryid)) continue;
            ++total;
            if (shown >= kMaxRows) continue;
            ++shown;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idbuf[48];
            std::snprintf(idbuf, sizeof(idbuf), "%lld##%s", static_cast<long long>(r.own() ? r.playerid : r.commentaryid), r.own() ? "own" : "gen");
            ImGui::SetNextItemAllowOverlap();  // the play button below takes its own clicks
            if (ImGui::Selectable(idbuf, g_sel_all == key, ImGuiSelectableFlags_SpanAllColumns)) {
                g_sel_all = key;
                sel = &r;
            }
            ImGui::TableNextColumn();
            row_play_button(app, key, r.own() ? CallnameAudioKind::Own : CallnameAudioKind::Generic, r.own() ? r.playerid : r.commentaryid);
            ImGui::SameLine();
            ImGui::TextUnformatted(r.text.empty() ? "(no text)" : r.text.c_str());
            ImGui::TableNextColumn();
            if (r.own())
                ImGui::TextDisabled("own recording");
            else
                ImGui::Text("%d name row%s, %d player%s", r.name_rows, r.name_rows == 1 ? "" : "s", r.users, r.users == 1 ? "" : "s");
        }
        ImGui::EndTable();
    }
    if (total > shown) ImGui::TextDisabled("%d more: type more of the callname", total - shown);
    ImGui::EndChild();
    if (!sel) {
        ImGui::TextDisabled("Pick a callname above.");
        return;
    }
    if (sel->own()) {
        ImGui::Text("Selected: %s (ID %lld), own recording", sel->text.c_str(), static_cast<long long>(sel->playerid));
        // 1.1.0: a voice swap gives it to this player in matches; the database cannot
        if (!voice_pick_buttons(app, t, p, *sel, own))
            ImGui::TextColored(kYellow, "The game always uses a player's own recording; the database cannot give it to another player.");
        return;
    }
    ImGui::Text("Selected: '%s' (callname %lld, %d name row%s, %d player%s)", sel->text.c_str(), static_cast<long long>(sel->commentaryid),
                sel->name_rows, sel->name_rows == 1 ? "" : "s", sel->users, sel->users == 1 ? "" : "s");
    // 1.1.0: in matches only (a voice swap), then the older database buttons
    if (voice_pick_buttons(app, t, p, *sel, own)) ImGui::SeparatorText("Change the name in the database");
    // 1.0.3: the player-specific route when it can be used (no name changes), else the name row, else nothing
    const GenericRoute route = plan_generic_route(app, p, sel->commentaryid);
    route_line(app, route, sel->nameid != 0);
    if (own) own_recording_warning(app, p, own);
    if (route.player) {
        if (ImGui::Button("Assign callname##all")) request_generic_callname(app, p, sel->text, sel->commentaryid, own);
        assign_callname_note(own);
    } else if (sel->nameid) {
        // through the name row (as By name, the shown name kept): a player with a common name (commonnameid > 0) is
        // called by it and never by his last name (resolve_callname), so his common name gets it; else his last name
        const bool as_common = t.has("commonnameid") && app.db.get_int(t, p.rec, "commonnameid", 0) > 0;
        const char* field = as_common ? "commonnameid" : "lastnameid";
        ImGui::Checkbox("Keep the shown name (editedplayernames)##all", &g_keep_display);
        if (!t.has(field)) ImGui::BeginDisabled();
        if (ImGui::Button(as_common ? "Assign as common name##all" : "Assign as last name##all")) {
            NameChoice c;
            c.nameid = sel->nameid;
            c.name = sel->text;
            c.commentaryid = sel->commentaryid;
            c.users = sel->users;
            request_name(app, t, p, c, as_common, own);
            if (own) g_pending.alt = c;
        }
        if (!t.has(field)) ImGui::EndDisabled();
        ImGui::SameLine();
        if (as_common)
            ImGui::TextDisabled("(name %lld; he has a common name: the game says it, never his last name)", static_cast<long long>(sel->nameid));
        else
            ImGui::TextDisabled("(name %lld)", static_cast<long long>(sel->nameid));
    }
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
            ImGui::SetNextItemAllowOverlap();  // the play button below takes its own clicks
            if (ImGui::Selectable(idbuf, g_sel_player == c.playerid, ImGuiSelectableFlags_SpanAllColumns)) g_sel_player = c.playerid;
            ImGui::TableNextColumn();
            row_play_button(app, c.playerid, CallnameAudioKind::Generic, c.commentaryid);  // the callname that would be copied
            ImGui::SameLine();
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
    const std::string sources = cn.masters.loaded() && cn.masters.fc27() ? std::string("(FC 27 master)")
                                : cn.masters.loaded() ? "(game's audio service + " + cn.masters.label() + ")"
                                                      : "(game's audio service only: no master list)";
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
    if (!name && g_pending.alt.nameid && g_pending.alt.nameid == g_pending.nameid) name = &g_pending.alt;
    std::string what;
    if (g_pending.kind == PendingAssign::Kind::Player && g_pending.generic)  // "write the generic callname 'Kane' (900017) to ..."
        what = "write " + g_pending.from +
               (g_pending.from.find('\'') != std::string::npos ? " (" + std::to_string(g_pending.commentaryid) + ")" : std::string()) +
               " to his playernamemap row";
    else if (g_pending.kind == PendingAssign::Kind::Player)
        what = "write callname " + std::to_string(g_pending.commentaryid) + " (" + g_pending.from + "'s) to his playernamemap row";
    else
        what = std::string("assign '") + (name ? name->name : "?") + "' (callname " + (name ? std::to_string(name->commentaryid) : std::string("?")) +
               ") as his " + (g_pending.kind == PendingAssign::Kind::CommonName ? "common" : "last") + " name";
    g_state.confirm_what = what;
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
    const voice::Entry* ve = app.voice_store.find(p.playerid);
    const bool voice_on = app.voice_available();
    const bool swapped = ve && voice_on;
    ImGui::AlignTextToFramePadding();
    // hear what the game says: his own recording, else the rule's callname
    if (!swapped && (info.own || info.source != CallnameSource::None)) {
        callname_play_button(app, info.own ? CallnameAudioKind::Own : CallnameAudioKind::Generic, info.own ? p.playerid : info.commentaryid);
        ImGui::SameLine();
    }
    if (swapped && ve->voice_of && *ve->voice_of == 0 && !ve->kickoff) {
        // 1.1.0: his own recording off, no generic callname: the callname rule speaks his surname lines
        g_state.current_line = "In matches: own recording off; the callname rule gives " + (rule.empty() ? std::string("none") : rule);
        ImGui::TextColored(kGreen, "%s", g_state.current_line.c_str());
    } else if (swapped) {
        // 1.1.0: a voice swap decides what the commentary says in matches; the game's rule is shown greyed below it
        voice_play_button(app, *ve);
        g_state.current_line = "In matches: " + voice_called(app, t, *ve);
        ImGui::TextColored(kGreen, "%s", g_state.current_line.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A voice swap: in matches only, nothing written to the database. His name on screen stays his.");
        const std::string game = info.own ? "his own recording in " + cn.lang : rule.empty() ? std::string("no callname") : rule;
        g_state.rule_line = "Not used while the swap is on: " + game;
        ImGui::TextDisabled("%s", g_state.rule_line.c_str());
    } else if (info.own) {
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
    if (ve) {
        g_state.voice_warning = voice_silent_text(app, t, *ve);
        if (!g_state.voice_warning.empty()) ImGui::TextColored(kOrange, "%s", g_state.voice_warning.c_str());
        if (!voice_on) voice_off_line(app);
    }
    // 1.1.0: a player with his own recording can have it off in matches (then All callnames > Use in matches)
    if (info.own && voice_on) {
        bool use_own = !(ve && ve->voice_of);
        if (ImGui::Checkbox("Use his own recording##vs", &use_own)) {
            if (use_own) {
                if (app.voice_forget(p.playerid)) app.notify(p.name + ": his own recording is used again in matches");
            } else {
                voice::Entry e;
                e.playerid = p.playerid;
                e.player = p.name;
                e.voice_of = 0;
                if (ve && ve->kickoff) {
                    e.kickoff = ve->kickoff;
                    e.from = ve->from;
                }
                if (app.voice_upsert(e)) app.notify(p.name + ": own recording off in matches");
            }
            ve = app.voice_store.find(p.playerid);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off: in matches the commentary does not use his own recording. Then pick a generic callname\n"
                              "(All callnames > Use in matches) or another player's voice (Use his voice).");
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
        if (ImGui::BeginTabItem("All callnames")) {
            all_callnames_picker(app, t, p);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Players without own recording")) {
            club_players_tab(app, p);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Voice swaps")) {
            voice_swaps_tab(app, t);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // outside the tab items: both pickers park their request here (an ID pushed by a tab item would hide the popup)
    own_confirm_popup(app, t, p);
    voice_confirm_popup(app, t, p);
}

// ---------------------------------------------------------------- App: the voice-swap store (core/callname_voice.h)

void App::load_voice() {
    const std::filesystem::path p = voice::store_path(bridge.root());
    std::string err;
    voice_store = voice::VoiceStore{};
    voice_error.clear();
    voice_unreadable_ = !voice_store.load(p, &err);
    // a note even when it loaded: a bad file set aside, bad entries dropped
    if (voice_unreadable_) voice_error = (err.empty() ? "cannot read " + p.string() : err) + ": no voice swaps loaded";
    else voice_error = err;
    if (!voice_error.empty()) log("voice swaps: " + voice_error);
    if (!voice_store.entries.empty()) log("voice swaps: " + std::to_string(voice_store.entries.size()) + " kept in " + p.string());
}

bool App::save_voice() {
    const std::filesystem::path p = voice::store_path(bridge.root());
    std::string err;
    if (voice_unreadable_) {
        // never overwrite a file that could not be read: this session's swaps stay until Turbo closes
        voice_error = "voice swaps not saved: " + p.filename().string() + " could not be read at start-up (left as it is)";
        notify(voice_error, true);
        return false;
    }
    if (!voice_store.save(p, &err)) {
        voice_error = "voice swaps not saved: " + err;
        notify(voice_error, true);
        return false;
    }
    voice_error.clear();
    return true;
}

void App::voice_publish() {
    if (!voice_service) return;
    voice_service->publish(voice::build_table(voice_store));
    voice_published_to_ = voice_service;
}

bool App::voice_upsert(voice::Entry e) {
    e.when = time_stamp();
    voice_store.upsert(e);
    const bool saved = save_voice();
    voice_publish();
    log("voice swaps: player " + std::to_string(e.playerid) + ": voice_of " + (e.voice_of ? std::to_string(*e.voice_of) : std::string("-")) +
        ", kick-off " + (e.kickoff ? std::to_string(*e.kickoff) : std::string("-")) + (e.names_only ? ", name lines only" : ""));
    return saved;
}

bool App::voice_forget(int64_t playerid) {
    if (!voice_store.forget(playerid)) return false;
    save_voice();
    voice_publish();
    log("voice swaps: player " + std::to_string(playerid) + " forgotten");
    return true;
}

bool App::voice_forget_all() {
    if (voice_store.entries.empty()) return false;
    voice_store.entries.clear();
    save_voice();
    voice_publish();
    log("voice swaps: all forgotten");
    return true;
}

std::string App::voice_why_off() const {
    if (!voice_service) return "not in this build of Turbo";
    if (voice_service->available()) return "";
    const std::string why = voice_service->why_off();
    return why.empty() ? "hooks not installed" : why;
}

std::string App::voice_status_line() const {
    if (!voice_available()) return "Voice swaps: off (" + voice_why_off() + ")";
    const voice::StatsSnapshot st = voice_service->stats();
    const size_t n = voice_store.entries.size();
    return "Voice swaps: on | " + std::to_string(n) + (n == 1 ? " swap" : " swaps") + " | lines changed " + std::to_string(st.rewrites) +
           " | kick-off set " + std::to_string(st.kickoffs);
}

}  // namespace turbo
