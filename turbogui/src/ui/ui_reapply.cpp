// FC 27 LE Turbo GUI - edits FC 27 forgets at every career load, written again by Turbo.
//
// FC 27 reloads teamkits and playernamemap from its base data whenever a career loads (core/reapply.h has the
// measurement). Teams > Colours (kit colours) and Players > Callname (player-specific callnames) keep what they wrote in
// turbo_output\reapply_edits.json; when Turbo connects to a newly loaded career (App::refresh, once per Lua session +
// load_gen: a career loaded, entered or left; not when the manager changes club, not on a Refresh) every kept entry is
// written again:
//   kits       the kit row the colours were set on (teamtechid, teamkittypetechid, teamkitid; the single row of that type
//              when the kit id is not found and no other kit of that type is kept), each colour channel through
//              Database::set (range-checked, table and record checked alive; no undo step, like any kit edit); a value
//              already in place is not written again; only the colour channels (kit_colour_field) are ever written;
//   callnames  through write_player_callname, the code the Callname tab uses (row edited in place; else a row taken over
//              from which no player hears a callname; never an added row: the room check would be read while the game
//              reloads the table, and Live Editor's insert crashes the game on a full table). Never for a player with
//              his own recording (the master list or the game's audio service: Callnames::own_recording): the game says
//              that recording, never a player-specific callname, so the row would only be an extra write.
//   players    (1.2.5) the Players editor's edits of attributes, overall, potential, work rates, skill moves, weak foot
//              (App::player_field_written: sliders, Overall +1/-1, a typed overall, archetypes, Undo, All fields, the
//              Database tab; in a career only). Each kept field holds the value written and the value it replaced; at a
//              career load it is written again (Database::set, range-checked, no undo step) ONLY while it holds the
//              replaced value: the game put the old value back (your players' development plans outrank the players
//              table, and every save is a new CmMgrC<timestamp> file, so an edit made before Turbo synced the plan came
//              back in every later save). A field holding anything else (the game's own growth or decline since, a
//              player of another career) is left alone. Written fields of a player of your club also go into his
//              development plan (App::queue_plan_sync). On / off: "keep_players" in the store (Turbo Tools > Kept
//              player edits), which also lists every kept field with Forget.
// Not written, without an error: a player with his own recording, a player or kit not in this career. One summary line
// goes to the GUI log, turbo_gui.log and the Colours / Callname tabs; a toast only when something was written (an
// error toast when a write failed). Kill switch: turbo_output\reapply_off.txt (nothing is written; the store is kept).
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app.h"
#include "imgui.h"
#include "ui_identity.h"

namespace turbo {

namespace fs = std::filesystem;

static const ImVec4 kWarnColour(1.0f, 0.6f, 0.3f, 1.0f);

static std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// "teamcolorprimr" -> "teamcolorprim": the colour a channel field belongs to (Teams > Colours writes r, g and b together)
static std::string colour_of(const std::string& field) {
    const char last = field.empty() ? '\0' : field.back();
    return field.size() > 1 && (last == 'r' || last == 'g' || last == 'b') ? field.substr(0, field.size() - 1) : field;
}

void App::load_reapply() {
    const fs::path p = reapply_store_path(bridge.root());
    std::string err;
    size_t dropped = 0;
    reapply_unreadable_ = false;
    reapply_error.clear();
    if (!load_reapply_store(p, reapply, &err, &dropped)) {
        reapply_unreadable_ = true;
        reapply_error = err + ": no kept edits loaded (the file is set aside as " + reapply_unreadable_path(p).filename().string() +
                        " at the next kit colour or callname edit)";
        log("re-apply: " + reapply_error);
        return;
    }
    if (dropped) log("re-apply: " + plural(dropped, "malformed entry", "malformed entries") + " in " + p.filename().string() + " ignored");
    if (!reapply.empty())
        log("re-apply: " + plural(reapply.kits.size(), "kit", "kits") + " and " + plural(reapply.callnames.size(), "player callname", "player callnames") +
            " kept in " + p.string());
}

bool App::save_reapply() {
    const fs::path p = reapply_store_path(bridge.root());
    std::string err;
    if (!save_reapply_store(p, reapply, &err, reapply_unreadable_)) {
        reapply_error = "kept edits not saved: " + err;
        notify("Re-apply: " + reapply_error, true);
        return false;
    }
    if (reapply_unreadable_) log("re-apply: the unreadable store was set aside as " + reapply_unreadable_path(p).string());
    reapply_unreadable_ = false;
    reapply_error.clear();
    return true;
}

void App::remember_kit_colour(const Table& t, uint64_t rec, const std::string& prefix, const uint8_t rgb[3]) {
    // teams.teamcolor1..3 and the stadium colours are kept by the career save; only teamkits is reloaded
    if (t.name != "teamkits" || !kit_colour_field(prefix + "r")) return;
    const int64_t tid = db.get_int(t, rec, "teamtechid", -1);
    if (tid <= 0) return;
    const int64_t type = db.get_int(t, rec, "teamkittypetechid", 0);
    const int64_t kitid = db.get_int(t, rec, "teamkitid", -1);
    const std::string when = time_stamp();
    static const char ch[3] = {'r', 'g', 'b'};
    for (int c = 0; c < 3; ++c) reapply.set_kit_field(tid, type, kitid, model.team_name(tid), when, prefix + ch[c], rgb[c]);
    save_reapply();
}

bool App::remember_player_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from, std::string* why) {
    if (why) why->clear();
    if (playerid <= 0 || commentaryid <= kNoCallname || commentaryid > kCallnameMax) return false;
    // The game says a player's own recording before any callname: such a callname is never written again at a career
    // load (and an older kept one of his goes)
    if (const int own = callnames.own_recording(playerid)) {
        const bool had = reapply.forget_callname(playerid);
        if (had) save_reapply();
        if (why)
            *why = "Not kept for the next career loads: he has his own recording in " + (callnames.lang.empty() ? std::string("this language") : callnames.lang) +
                   " (" + callnames.own_source(own) + "), which the game says instead" + (had ? " (his older kept callname is dropped too)" : "");
        log("re-apply: player-specific callname of player " + std::to_string(playerid) + " not kept: own recording (" + callnames.own_source(own) + ")");
        return false;
    }
    reapply.set_callname(playerid, commentaryid, player, from, time_stamp());
    save_reapply();
    return true;
}

bool App::forget_kit_edit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) {
    if (!reapply.forget_kit(teamtechid, kittype, teamkitid)) return false;
    log("re-apply: kit " + std::to_string(teamtechid) + " / type " + std::to_string(kittype) + " / kit id " + std::to_string(teamkitid) + " forgotten");
    return save_reapply();
}

bool App::forget_player_callname(int64_t playerid) {
    if (!reapply.forget_callname(playerid)) return false;
    log("re-apply: player-specific callname of player " + std::to_string(playerid) + " forgotten");
    return save_reapply();
}

bool App::forget_player_edit(int64_t playerid, const std::string& field) {
    if (field.empty() ? !reapply.forget_player(playerid) : !reapply.forget_player_field(playerid, field)) return false;
    log("re-apply: kept " + (field.empty() ? std::string("edits") : field) + " of player " + std::to_string(playerid) + " forgotten");
    return save_reapply();
}

bool App::forget_all_player_edits() {
    if (reapply.players.empty()) return false;
    log("re-apply: every kept player edit forgotten (" + plural(reapply.player_fields(), "field", "fields") + " of " +
        plural(reapply.players.size(), "player", "players") + ")");
    reapply.players.clear();
    return save_reapply();
}

bool App::set_keep_players(bool on) {
    if (reapply.keep_players == on) return false;
    reapply.keep_players = on;
    log(std::string("re-apply: player edits ") + (on ? "kept again" : "no longer kept (the kept ones stay in the file, not written)"));
    return save_reapply();
}

std::string App::reapply_key() const {
    // load_gen changes only when a career is loaded, entered or left (Lua bridge.lua write_state); db_gen also changes
    // when the manager changes club or the GUI asks for a refresh, which is not a reload of teamkits / playernamemap
    const auto& st = bridge.state();
    return st.session + "#" + (st.load_gen >= 0 ? "L" + std::to_string(st.load_gen) : "D" + std::to_string(st.db_gen));
}

bool App::reapply_due() const {
    const auto& st = bridge.state();
    return st.loaded && st.in_cm && !reapply.empty() && reapply_key() != reapplied_key_;
}

void App::maybe_reapply() {
    // only a career database: the tables outside a career are not the ones the edits were made in
    if (!bridge.state().in_cm) return;
    const std::string key = reapply_key();
    // the same career load again (the Refresh button, the window shown, the manager's new club): what the user changed
    // since then stays as it is
    if (key == reapplied_key_) return;
    reapplied_key_ = key;
    if (!reapply.empty()) reapply_stored_edits();
}

std::string App::reapply_stored_edits() {
    size_t colours = 0, kits = 0, players = 0, in_place = 0;
    std::vector<std::string> failed;  // a write that should have happened did not: an error toast
    std::vector<std::string> left;    // not written by design (own recording, not in this career): no toast
    auto fail = [&](const std::string& why) {
        failed.push_back(why);
        log("re-apply: not written: " + why);
    };
    auto leave = [&](const std::string& why) {
        left.push_back(why);
        log("re-apply: left alone: " + why);
    };
    std::error_code ec;
    if (fs::exists(bridge.root() / "turbo_output" / "reapply_off.txt", ec)) {
        const std::string line = "off (turbo_output\\reapply_off.txt): " + plural(reapply.size(), "kept edit", "kept edits") + " not written";
        reapply_status = time_stamp() + ": " + line;
        log("re-apply at career load: " + line);
        if (log_hook) log_hook("re-apply at career load: " + line);
        return line;
    }

    // ---- kits: the kit row the colours were set on; every kept channel written unless it is already in place
    if (!reapply.kits.empty()) {
        const Table* kt = db.table("teamkits");
        const Field* ftid = kt ? kt->field("teamtechid") : nullptr;
        const Field* ftype = kt ? kt->field("teamkittypetechid") : nullptr;
        const Field* fkid = kt ? kt->field("teamkitid") : nullptr;
        Snapshot snap;
        if (!kt || !ftid || !snap.load(db.memory(), *kt)) {
            fail(plural(reapply.kits.size(), "kit", "kits") + ": FC 27's teamkits table is not readable");
        } else {
            std::map<std::pair<int64_t, int64_t>, std::vector<uint32_t>> rows;  // (teamtechid, type) -> snapshot indexes
            for (uint32_t i : snap.valid) rows[{snap.get_int(i, *ftid), ftype ? snap.get_int(i, *ftype) : 0}].push_back(i);
            for (const auto& kv : reapply.kits) {
                const KitEdit& k = kv.second;
                const std::string label = model.team_name(k.teamtechid) + " (" + std::to_string(k.teamtechid) + ") " + kit_type_name(k.kittype) + " kit" +
                                          (k.teamkitid >= 0 ? " " + std::to_string(k.teamkitid) : std::string());
                auto it = rows.find({k.teamtechid, k.kittype});
                if (it == rows.end()) {
                    leave(label + ": not in this career's teamkits");
                    continue;
                }
                // the row with the kit id; else the single row of that type, when no other kit of that type is kept
                // (two kits of one type: never one kit's colours on the other)
                const uint32_t* pick = nullptr;
                if (fkid && k.teamkitid >= 0)
                    for (const uint32_t& i : it->second)
                        if (snap.get_int(i, *fkid) == k.teamkitid) pick = &i;
                if (!pick && it->second.size() == 1 && reapply.kits_of_type(k.teamtechid, k.kittype) == 1) pick = &it->second[0];
                if (!pick) {
                    leave(label + ": " + plural(it->second.size(), "kit", "kits") + " of this type in this career and none is kit id " + std::to_string(k.teamkitid));
                    continue;
                }
                const uint64_t rec = snap.addr(*pick);
                std::set<std::string> written;
                std::string problem;
                for (const auto& f : k.fields) {
                    const Field* fd = kt->field(f.first);
                    // the store only holds colour channels; checked again here, so no other column is ever written
                    if (!kit_colour_field(f.first) || !fd || fd->type != FieldType::Int) {
                        if (problem.empty()) problem = "no kit colour field " + f.first + " in FC 27's teamkits";
                        continue;
                    }
                    if (snap.get_int(*pick, *fd) == f.second) continue;  // already in place
                    std::string err;
                    if (!db.set(*kt, rec, *fd, Value::of_int(f.second), &err)) {
                        if (problem.empty()) problem = f.first + ": " + err;
                        continue;
                    }
                    written.insert(colour_of(f.first));
                }
                if (!written.empty()) {
                    ++kits;
                    colours += written.size();
                    log("re-apply: " + label + ": " + plural(written.size(), "colour", "colours") + " written");
                } else if (problem.empty()) {
                    ++in_place;
                }
                if (!problem.empty()) fail(label + ": " + problem);
            }
        }
    }

    // ---- player-specific callnames: the Callname tab's own write (row in place / spare row), never an added row
    if (!reapply.callnames.empty()) {
        // the language and the master list decide who has his own recording: loaded here in case the Callname tab or
        // the watcher's tick have not done it yet
        if (!callnames.refreshed && !game_root.empty()) callnames.refresh(bridge.root(), game_root, chosen_commentary_language());
        if (!db.table("playernamemap")) {
            fail(plural(reapply.callnames.size(), "player callname", "player callnames") + ": FC 27's playernamemap table is not readable");
        } else if (callnames.lang.empty()) {
            // without a language Turbo cannot tell who has his own recording: nothing written rather than a guess
            fail(plural(reapply.callnames.size(), "player callname", "player callnames") +
                 ": the commentary language is not known, so own recordings cannot be checked");
        } else {
            // the index must describe this career's rows before any row is written
            callnames.build_index(db, model, model.names_by_id());
            for (const auto& kv : reapply.callnames) {
                const CallnameEdit& c = kv.second;
                const PlayerRow* p = model.player(c.playerid);
                const std::string label = (p ? p->name : (c.player.empty() ? std::string("player") : c.player)) + " (" + std::to_string(c.playerid) + ")";
                if (!p) {
                    leave(label + ": not in this career's players");
                    continue;
                }
                if (const int own = callnames.own_recording(c.playerid)) {
                    leave(label + ": has his own recording in " + callnames.lang + " (" + callnames.own_source(own) +
                          "), which the game says instead of a player-specific callname");
                    continue;
                }
                const PlayerCallnameWrite r =
                    write_player_callname(*this, *p, c.commentaryid, c.from.empty() ? std::string("a kept edit") : c.from, false);
                switch (r.how) {
                    case PlayerCallnameWrite::How::Updated:
                    case PlayerCallnameWrite::How::TookOver:
                        ++players;
                        log("re-apply: " + r.message);
                        break;
                    case PlayerCallnameWrite::How::Unchanged:
                        ++in_place;
                        break;
                    default:  // NeedsRow, Refused (Queued never: no row is added here)
                        fail(r.message);
                        break;
                }
            }
        }
    }

    // ---- player fields (the Players editor's edits): only where the loaded value is the one the edit replaced
    size_t pfields = 0, pplayers = 0;
    if (!reapply.players.empty() && !reapply.keep_players) {
        leave(plural(reapply.players.size(), "player's kept edits", "players' kept edits") + ": keeping is off (Turbo Tools > Kept player edits)");
    } else if (!reapply.players.empty()) {
        const Table* pt = db.table("players");
        if (!pt) {
            fail(plural(reapply.players.size(), "player's kept edits", "players' kept edits") + ": FC 27's players table is not readable");
        } else {
            for (const auto& kv : reapply.players) {
                const PlayerEdit& e = kv.second;
                const PlayerRow* p = model.player(e.playerid);
                const std::string label = (p ? p->name : (e.player.empty() ? std::string("player") : e.player)) + " (" + std::to_string(e.playerid) + ")";
                if (!p || !db.table_alive(*pt, p->rec)) {
                    leave(label + ": not in this career's players");
                    continue;
                }
                std::vector<std::string> wrote, moved;
                std::string problem;
                for (const auto& f : e.fields) {
                    const Field* fd = pt->field(f.first);
                    // the store only holds kept_player_field; checked again here, so no other column is ever written
                    if (!kept_player_field(f.first) || !fd || fd->type != FieldType::Int) {
                        if (problem.empty()) problem = "no field " + f.first + " in FC 27's players table";
                        continue;
                    }
                    const int64_t loaded = db.get_int(*pt, p->rec, f.first, 0);
                    const KeptField what = kept_field_state(f.second, loaded);
                    if (what == KeptField::InPlace) continue;
                    if (what == KeptField::Moved) {
                        moved.push_back(field_label(f.first) + " " + std::to_string(loaded) + " (kept " + std::to_string(f.second.value) + ")");
                        continue;
                    }
                    std::string err;
                    if (!db.set(*pt, p->rec, *fd, Value::of_int(f.second.value), &err)) {
                        if (problem.empty()) problem = f.first + ": " + err;
                        continue;
                    }
                    wrote.push_back(field_label(f.first) + " " + std::to_string(loaded) + " -> " + std::to_string(f.second.value));
                    queue_plan_sync(e.playerid, f.first);  // his development plan holds it too (your club's players)
                }
                if (!wrote.empty()) {
                    ++pplayers;
                    pfields += wrote.size();
                    model.refresh_player(e.playerid, today());
                    std::string list;
                    for (const auto& w : wrote) list += (list.empty() ? "" : ", ") + w;
                    log("re-apply: " + label + ": " + list);
                } else if (moved.empty() && problem.empty()) {
                    ++in_place;
                }
                if (!moved.empty()) {
                    std::string list;
                    for (size_t i = 0; i < moved.size() && i < 3; ++i) list += (i ? ", " : "") + moved[i];
                    if (moved.size() > 3) list += ", +" + std::to_string(moved.size() - 3) + " more";
                    leave(label + ": " + plural(moved.size(), "field", "fields") + " moved by the game since (" + list + "): not written over");
                }
                if (!problem.empty()) fail(label + ": " + problem);
            }
        }
    }

    if (kits || players || pfields) ++gen;  // cached views (kit list, Callname index, the players list) reload
    std::string line = "re-applied " + plural(colours, "kit colour", "kit colours") + ", " + plural(players, "player callname", "player callnames");
    // the player part only when player edits are kept (the line stays as it was for everyone else)
    if (!reapply.players.empty())
        line += ", " + plural(pfields, "player field", "player fields") + (pplayers ? " (" + plural(pplayers, "player", "players") + ")" : std::string());
    if (in_place) line += " (" + std::to_string(in_place) + " already in place)";
    auto add = [&line](const char* what, const std::vector<std::string>& v) {
        if (v.empty()) return;
        line += "; " + std::to_string(v.size()) + " " + what + ": ";
        for (size_t i = 0; i < v.size() && i < 3; ++i) line += (i ? "; " : "") + v[i];
        if (v.size() > 3) line += "; +" + std::to_string(v.size() - 3) + " more (GUI log)";
    };
    add("not written", failed);
    add("left alone", left);
    reapply_status = time_stamp() + ": " + line;
    log("re-apply at career load: " + line);
    if (log_hook) log_hook("re-apply at career load: " + line);
    // A toast when something was written or a write failed; nothing for entries left alone by design (a player with his
    // own recording, a club or player of another career): those would come back at every load
    if (colours || players || pfields || !failed.empty()) notify("Kept edits: " + line, !failed.empty());
    return line;
}

// ---------------------------------------------------------------- panels
void reapply_status_line(App& app) {
    if (!app.reapply_error.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarnColour, "Kept edits: %s", app.reapply_error.c_str());
        ImGui::PopTextWrapPos();
    }
    if (app.reapply_status.empty()) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Kept edits at the last career load: %s", app.reapply_status.c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 reloads kit colours and player-specific callnames from its base data whenever a career loads, and your\n"
                          "players' development plans put their old attributes back. Turbo keeps those edits in\n"
                          "turbo_output\\reapply_edits.json and writes them again when it connects to a newly loaded career (a player field\n"
                          "only while it holds the value the edit replaced). turbo_output\\reapply_off.txt turns this off.");
}

void reapply_kit_line(App& app, const Table& kt, uint64_t rec) {
    const int64_t tid = app.db.get_int(kt, rec, "teamtechid", -1);
    const int64_t type = app.db.get_int(kt, rec, "teamkittypetechid", 0);
    const int64_t kitid = app.db.get_int(kt, rec, "teamkitid", -1);
    const KitEdit* k = app.reapply.kit(tid, type, kitid);
    if (!k) return;
    std::set<std::string> colours;
    for (const auto& f : k->fields) colours.insert(colour_of(f.first));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Kept for every career load: %s%s%s", plural(colours.size(), "colour", "colours").c_str(), k->when.empty() ? "" : ", last edit ",
                        k->when.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 reloads kit colours from its base data whenever a career loads: Turbo writes these again then.\n"
                          "Forget stops that; the colours shown now stay until the career is loaded again.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget##kitkeep")) {
        if (app.forget_kit_edit(tid, type, k->teamkitid))
            app.notify(app.model.team_name(tid) + " " + kit_type_name(type) + " kit: kept colours forgotten (they last until the career is loaded again)");
    }
}

void reapply_callname_line(App& app, const PlayerRow& p) {
    const CallnameEdit* c = app.reapply.callname(p.playerid);
    if (!c) return;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Kept for every career load: player-specific callname %lld%s%s%s", static_cast<long long>(c->commentaryid),
                        c->from.empty() ? "" : " (from ", c->from.c_str(), c->from.empty() ? "" : ")");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("FC 27 reloads playernamemap from its base data whenever a career loads: Turbo writes this callname again\n"
                          "then. Forget stops that; the callname stays until the career is loaded again.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget##cnkeep")) {
        if (app.forget_player_callname(p.playerid)) app.notify(p.name + ": kept player-specific callname forgotten (it lasts until the career is loaded again)");
    }
    if (const int own = app.callnames.own_recording(p.playerid))
        ImGui::TextColored(kWarnColour, "Not written at career load: he has his own recording (%s), which the game says instead.",
                           app.callnames.own_source(own).c_str());
}

// "Overall 85 (was 80)": a kept field for the Players tab and the list
static std::string kept_text(const std::string& field, const PlayerFieldEdit& e) {
    return field_label(field) + " " + std::to_string(e.value) + " (was " + std::to_string(e.was) + ")";
}

// The field's value in the loaded career ("-" when the player is not in it)
static std::string now_text(App& app, int64_t playerid, const std::string& field) {
    const Table* t = app.db.ready() ? app.db.table("players") : nullptr;
    const PlayerRow* p = t ? app.model.player(playerid) : nullptr;
    if (!p || !t->field(field) || !app.db.table_alive(*t, p->rec)) return "-";
    return std::to_string(app.db.get_int(*t, p->rec, field, 0));
}

void reapply_player_line(App& app, const PlayerRow& p) {
    const PlayerEdit* e = app.reapply.player(p.playerid);
    if (e) {
        std::string list;
        size_t shown = 0;
        for (const auto& f : e->fields) {
            if (shown++ == 4) {
                list += ", +" + std::to_string(e->fields.size() - 4) + " more";
                break;
            }
            list += (list.empty() ? "" : ", ") + kept_text(f.first, f.second);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - S(90.0f));
        if (app.reapply.keep_players) ImGui::TextDisabled("Kept for every career load: %s", list.c_str());
        else ImGui::TextColored(kWarnColour, "Kept, but keeping is off (Turbo Tools > Kept player edits): %s", list.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
            ImGui::TextUnformatted("Written again whenever a career loads (whatever the save is called), but only while the field holds\n"
                                   "the value the edit replaced: the game put the old value back. A value the game moved since (its own\n"
                                   "growth or decline) is left alone.");
            if (ImGui::BeginTable("##keptplayer", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Field");
                ImGui::TableSetupColumn("Kept");
                ImGui::TableSetupColumn("Was");
                ImGui::TableSetupColumn("Now");
                ImGui::TableHeadersRow();
                for (const auto& f : e->fields) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(field_label(f.first).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", static_cast<long long>(f.second.value));
                    ImGui::TableNextColumn();
                    ImGui::Text("%lld", static_cast<long long>(f.second.was));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(now_text(app, p.playerid, f.first).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTooltip();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Forget##playerkeep")) {
            if (app.forget_player_edit(p.playerid))
                app.notify(p.name + ": kept edits forgotten (the values stay until the career is loaded again)");
        }
    } else if (!app.reapply.keep_players) {
        ImGui::TextDisabled("Player edits are not kept for career loads (Turbo Tools > Kept player edits).");
    }
    // his edits on their way into his development plan (your club's players), and the last answer
    if (app.plan_sync_waiting() || !app.plan_sync_status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (app.plan_sync_waiting())
            ImGui::TextDisabled("Development plan: %zu player(s)' edits waiting to be sent", app.plan_sync_waiting());
        else
            ImGui::TextDisabled("Development plan, last update: %s", app.plan_sync_status.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Your club's players have a development plan that outranks the players table when a career loads.\n"
                              "Attributes, work rates, skill moves and weak foot edited here go into his plan too, so the save keeps them.");
    }
}

void kept_player_edits_section(App& app) {
    if (!ImGui::CollapsingHeader("Kept player edits (every career load)")) return;
    ImGui::PushID("keptplayers");
    bool on = app.reapply.keep_players;
    if (ImGui::Checkbox("Keep my player edits for every career load", &on)) app.set_keep_players(on);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Attributes, overall, potential, work rates, skill moves and weak foot you edit in the Players tab (in a career) are "
                        "kept in turbo_output\\reapply_edits.json. Whenever a career loads, whatever the save file is called, a kept "
                        "field is written again if it holds the value your edit replaced (the game put the old value back); a value "
                        "the game moved since (its own growth or decline) is left alone. Your club's players also get the value in "
                        "their development plan.");
    ImGui::PopTextWrapPos();
    reapply_status_line(app);
    if (app.reapply.players.empty()) {
        ImGui::TextDisabled("No player edits kept.");
        ImGui::PopID();
        return;
    }
    ImGui::Text("%s of %s kept", plural(app.reapply.player_fields(), "field", "fields").c_str(),
                plural(app.reapply.players.size(), "player", "players").c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget all...##keptall")) ImGui::OpenPopup("##keptforgetall");
    if (ImGui::BeginPopup("##keptforgetall")) {
        ImGui::Text("Forget every kept player edit (%s)?", plural(app.reapply.player_fields(), "field", "fields").c_str());
        ImGui::TextDisabled("The values stay as they are now; they are just no longer written again at a career load.");
        if (ImGui::Button("Forget all##keptforgetall")) {
            if (app.forget_all_player_edits()) app.notify("Every kept player edit forgotten");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##keptforgetall")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    int64_t forget_pid = 0;
    std::string forget_field;
    const float h = std::min(S(320.0f), ImGui::GetTextLineHeightWithSpacing() * (2.5f + static_cast<float>(app.reapply.player_fields())));
    if (ImGui::BeginTable("##keptlist", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0, h))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, S(130.0f));
        ImGui::TableSetupColumn("Kept", ImGuiTableColumnFlags_WidthFixed, S(44.0f));
        ImGui::TableSetupColumn("Was", ImGuiTableColumnFlags_WidthFixed, S(44.0f));
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, S(44.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(64.0f));
        ImGui::TableHeadersRow();
        for (const auto& kv : app.reapply.players) {
            const PlayerEdit& e = kv.second;
            const PlayerRow* p = app.db.ready() ? app.model.player(e.playerid) : nullptr;
            const std::string who = (p ? p->name : (e.player.empty() ? std::string("player") : e.player)) + " (" + std::to_string(e.playerid) + ")";
            for (const auto& f : e.fields) {
                ImGui::PushID(static_cast<int>(e.playerid & 0x7FFFFFFF));
                ImGui::PushID(f.first.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(who.c_str());
                if (ImGui::IsItemHovered() && !e.when.empty()) ImGui::SetTooltip("last edit %s", e.when.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(field_label(f.first).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(f.second.value));
                ImGui::TableNextColumn();
                ImGui::Text("%lld", static_cast<long long>(f.second.was));
                ImGui::TableNextColumn();
                const std::string now = now_text(app, e.playerid, f.first);
                ImGui::TextUnformatted(now.c_str());
                if (ImGui::IsItemHovered()) {
                    if (now == "-") ImGui::SetTooltip("Not in the loaded career (or not connected)");
                    else if (now == std::to_string(f.second.value)) ImGui::SetTooltip("In place");
                    else if (now == std::to_string(f.second.was)) ImGui::SetTooltip("The old value: written again at the next career load");
                    else ImGui::SetTooltip("Moved by the game since (growth or decline): left alone");
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Forget")) {
                    forget_pid = e.playerid;
                    forget_field = f.first;
                }
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    // after the loop: the map is not changed while it is drawn
    if (forget_pid && app.forget_player_edit(forget_pid, forget_field))
        app.notify(field_label(forget_field) + " of player " + std::to_string(forget_pid) + ": no longer kept");
    ImGui::PopID();
}

}  // namespace turbo
