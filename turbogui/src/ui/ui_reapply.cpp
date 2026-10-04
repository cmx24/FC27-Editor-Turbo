// FC 27 LE Turbo GUI - edits FC 27 forgets at every career load, written again by Turbo.
//
// FC 27 reloads teamkits and playernamemap from its base data whenever a career loads (core/reapply.h has the
// measurement). Teams > Colours (kit colours) and Players > Callname (player-specific callnames) keep what they wrote in
// turbo_output\reapply_edits.json; the first time Turbo connects to a newly loaded career (App::refresh, once per Lua
// session + db_gen) every kept entry is written again:
//   kits       the row of (teamtechid, teamkittypetechid) (the edited teamkitid when two rows share a type), each field
//              through Database::set (range-checked, table and record checked alive; no undo step, like any kit edit);
//              a value already in place is not written again;
//   callnames  through write_player_callname, the code the Callname tab uses (row edited in place; else added by Lua
//              when the table has room; else a spare row taken over; never Live Editor's insert on a full table). A
//              player the game speaks by his own recordings (the spoken set's players) is skipped: the game never says a
//              player-specific callname for him, so the row would only be an extra write into the commentary tables.
// One summary line goes to the GUI log, turbo_gui.log and the Colours / Callname tabs. Kill switch:
// turbo_output\reapply_off.txt (nothing is written; the store is kept).
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
    if (t.name != "teamkits") return;
    const int64_t tid = db.get_int(t, rec, "teamtechid", -1);
    if (tid <= 0) return;
    const int64_t type = db.get_int(t, rec, "teamkittypetechid", 0);
    const int64_t kitid = db.get_int(t, rec, "teamkitid", -1);
    const std::string when = time_stamp();
    static const char ch[3] = {'r', 'g', 'b'};
    for (int c = 0; c < 3; ++c) reapply.set_kit_field(tid, type, kitid, model.team_name(tid), when, prefix + ch[c], rgb[c]);
    save_reapply();
}

void App::remember_player_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from) {
    if (playerid <= 0 || commentaryid <= kNoCallname || commentaryid > kCallnameMax) return;
    reapply.set_callname(playerid, commentaryid, player, from, time_stamp());
    save_reapply();
}

bool App::forget_kit_edit(int64_t teamtechid, int64_t kittype) {
    if (!reapply.forget_kit(teamtechid, kittype)) return false;
    log("re-apply: kit " + std::to_string(teamtechid) + " / type " + std::to_string(kittype) + " forgotten");
    return save_reapply();
}

bool App::forget_player_callname(int64_t playerid) {
    if (!reapply.forget_callname(playerid)) return false;
    log("re-apply: player-specific callname of player " + std::to_string(playerid) + " forgotten");
    return save_reapply();
}

std::string App::reapply_key() const {
    const auto& st = bridge.state();
    return st.session + "#" + std::to_string(st.db_gen);
}

bool App::reapply_due() const {
    const auto& st = bridge.state();
    return st.loaded && st.in_cm && !reapply.empty() && reapply_key() != reapplied_key_;
}

void App::maybe_reapply() {
    // only a career database: the tables outside a career are not the ones the edits were made in
    if (!bridge.state().in_cm) return;
    const std::string key = reapply_key();
    // the same career again (the Refresh button, the window shown): what the user changed since then stays as it is
    if (key == reapplied_key_) return;
    reapplied_key_ = key;
    if (!reapply.empty()) reapply_stored_edits();
}

std::string App::reapply_stored_edits() {
    size_t colours = 0, kits = 0, players = 0, queued = 0, in_place = 0;
    std::vector<std::string> skipped;
    auto skip = [&](const std::string& why) {
        skipped.push_back(why);
        log("re-apply: skipped " + why);
    };
    std::error_code ec;
    if (fs::exists(bridge.root() / "turbo_output" / "reapply_off.txt", ec)) {
        const std::string line = "off (turbo_output\\reapply_off.txt): " + plural(reapply.size(), "kept edit", "kept edits") + " not written";
        reapply_status = time_stamp() + ": " + line;
        log("re-apply at career load: " + line);
        if (log_hook) log_hook("re-apply at career load: " + line);
        return line;
    }

    // ---- kits: the row of (teamtechid, teamkittypetechid); every kept field written unless it is already in place
    if (!reapply.kits.empty()) {
        const Table* kt = db.table("teamkits");
        const Field* ftid = kt ? kt->field("teamtechid") : nullptr;
        const Field* ftype = kt ? kt->field("teamkittypetechid") : nullptr;
        const Field* fkid = kt ? kt->field("teamkitid") : nullptr;
        Snapshot snap;
        if (!kt || !ftid || !snap.load(db.memory(), *kt)) {
            skip(plural(reapply.kits.size(), "kit", "kits") + ": FC 27's teamkits table is not readable");
        } else {
            std::map<ReapplyStore::KitKey, std::vector<uint32_t>> rows;  // -> snapshot indexes
            for (uint32_t i : snap.valid) rows[{snap.get_int(i, *ftid), ftype ? snap.get_int(i, *ftype) : 0}].push_back(i);
            for (const auto& kv : reapply.kits) {
                const KitEdit& k = kv.second;
                const std::string label = model.team_name(k.teamtechid) + " (" + std::to_string(k.teamtechid) + ") " + kit_type_name(k.kittype) + " kit";
                auto it = rows.find(kv.first);
                if (it == rows.end()) {
                    skip(label + ": not in this career's teamkits");
                    continue;
                }
                // two rows of one type: the one that was edited (teamkitid), else none rather than a guess
                const uint32_t* pick = it->second.size() == 1 ? &it->second[0] : nullptr;
                if (fkid && k.teamkitid >= 0)
                    for (const uint32_t& i : it->second)
                        if (snap.get_int(i, *fkid) == k.teamkitid) pick = &i;
                if (!pick) {
                    skip(label + ": " + std::to_string(it->second.size()) + " kits of this type and none is kit id " + std::to_string(k.teamkitid));
                    continue;
                }
                const uint64_t rec = snap.addr(*pick);
                std::set<std::string> written;
                std::string problem;
                for (const auto& f : k.fields) {
                    const Field* fd = kt->field(f.first);
                    if (!fd || fd->type != FieldType::Int) {
                        if (problem.empty()) problem = "no integer field " + f.first + " in FC 27's teamkits";
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
                if (!problem.empty()) skip(label + ": " + problem);
            }
        }
    }

    // ---- player-specific callnames: the Callname tab's own write (row in place / Lua when there is room / spare row)
    if (!reapply.callnames.empty()) {
        if (!db.table("playernamemap")) {
            skip(plural(reapply.callnames.size(), "player callname", "player callnames") + ": FC 27's playernamemap table is not readable");
        } else {
            // the spoken set (players with their own recordings) is loaded by the watcher's tick in the game; here too in
            // case the tick has not run yet. The index must describe this career's rows before any row is written
            if (!callnames.refreshed && !game_root.empty()) callnames.refresh(bridge.root(), game_root, chosen_commentary_language());
            callnames.build_index(db, model, model.names_by_id());
            for (const auto& kv : reapply.callnames) {
                const CallnameEdit& c = kv.second;
                const PlayerRow* p = model.player(c.playerid);
                const std::string label = (p ? p->name : (c.player.empty() ? std::string("player") : c.player)) + " (" + std::to_string(c.playerid) + ")";
                if (!p) {
                    skip(label + ": not in this career's players");
                    continue;
                }
                if (callnames.spoken.real(c.playerid)) {
                    skip(label + ": has his own recordings in " + (callnames.lang.empty() ? std::string("the loaded language") : callnames.lang) +
                         " (the game says those, never a player-specific callname), not written");
                    continue;
                }
                const PlayerCallnameWrite r = write_player_callname(*this, *p, c.commentaryid, c.from.empty() ? std::string("a kept edit") : c.from);
                switch (r.how) {
                    case PlayerCallnameWrite::How::Updated:
                    case PlayerCallnameWrite::How::TookOver:
                        ++players;
                        log("re-apply: " + r.message);
                        break;
                    case PlayerCallnameWrite::How::Queued:
                        ++queued;
                        log("re-apply: " + r.message);
                        break;
                    case PlayerCallnameWrite::How::Unchanged:
                        ++in_place;
                        break;
                    default:
                        skip(label + ": " + r.message);
                        break;
                }
            }
        }
    }

    if (kits || players) ++gen;  // cached views (kit list, Callname index) reload
    std::string line = "re-applied " + plural(colours, "kit colour", "kit colours") + ", " + plural(players, "player callname", "player callnames");
    if (queued) line += ", " + plural(queued, "player callname", "player callnames") + " queued for Turbo's Lua side";
    if (in_place) line += " (" + std::to_string(in_place) + " already in place)";
    if (!skipped.empty()) {
        line += "; " + std::to_string(skipped.size()) + " skipped: ";
        for (size_t i = 0; i < skipped.size() && i < 3; ++i) line += (i ? "; " : "") + skipped[i];
        if (skipped.size() > 3) line += "; +" + std::to_string(skipped.size() - 3) + " more (GUI log)";
    }
    reapply_status = time_stamp() + ": " + line;
    log("re-apply at career load: " + line);
    if (log_hook) log_hook("re-apply at career load: " + line);
    // a toast when something changed or could not be written; nothing when everything was already in place
    if (colours || players || queued || !skipped.empty()) notify("Kept edits: " + line, !skipped.empty());
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
        ImGui::SetTooltip("FC 27 reloads kit colours and player-specific callnames from its base data whenever a career loads.\n"
                          "Turbo keeps those edits in turbo_output\\reapply_edits.json and writes them again when it connects to a\n"
                          "newly loaded career. turbo_output\\reapply_off.txt turns this off.");
}

void reapply_kit_line(App& app, const Table& kt, uint64_t rec) {
    const int64_t tid = app.db.get_int(kt, rec, "teamtechid", -1);
    const int64_t type = app.db.get_int(kt, rec, "teamkittypetechid", 0);
    const KitEdit* k = app.reapply.kit(tid, type);
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
        if (app.forget_kit_edit(tid, type)) app.notify(app.model.team_name(tid) + " " + kit_type_name(type) + " kit: kept colours forgotten (they last until the career is loaded again)");
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
    if (app.callnames.spoken.real(p.playerid))
        ImGui::TextColored(kWarnColour, "Not written at career load: the game speaks this player by his own recordings.");
}

}  // namespace turbo
