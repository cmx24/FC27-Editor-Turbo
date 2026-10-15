// FC 27 LE Turbo GUI - Players > Appearance > "3D hair and beard" (ui_faces3d_gate.h). The draft list lives here until Apply;
// the files and the generator are core/faces3d.h; the hook's state comes from App::faces3d_report (win/lua_loader_win.h).
#include "ui_faces3d_gate.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <ctime>
#include <map>
#include <set>

#include "app.h"
#include "core/face_looks.h"
#include "core/hair_catalog.h"
#include "core/lua_loader.h"
#include "core/player_capture.h"
#include "imgui.h"
#include "textures.h"
#include "ui_faces.h"

namespace turbo {

namespace fs = std::filesystem;

namespace {

constexpr double kPreviewTimeout = 25.0;  // seconds without the picture before the preview gives up (the chooser uses 20)

struct GateUi {
    bool loaded = false;
    faces3d::Table draft;    // what the pickers edit
    std::string applied;     // players_json() of what is on disk (players.json), to show "not applied"
    std::string message;     // last Apply / Remove outcome
    bool message_ok = true;
    char hair_search[48] = "", beard_search[48] = "";
    // ---- live preview
    faces3d::PreviewBackup backup;       // = preview_backup.json (originals + what the preview wrote)
    std::set<int64_t> session_records;   // backup records made in this session (the others came from the file)
    std::string backup_error;
    std::string running_key = "\x01";    // header table the running list was read for ("\x01" = never)
    bool running_ok = false;
    faces3d::Table running;              // the list this game start runs (faces3d::running_table)
    std::string running_why;
    int64_t pending = 0;                 // player whose picture is awaited (0 = none)
    double sent_at = 0.0;
    int64_t shown = 0;                   // player of the picture below
    Rgba image;
    uint64_t version = 0;
    std::string caption;
    std::string pv_message;
    bool pv_message_ok = true;
    bool keep_follow = false;
};
GateUi g;

fs::path rel(const fs::path& base, const char* r) {
    fs::path p = base;
    std::string s = r;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find('\\', a);
        p /= s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (b == std::string::npos) return p;
        a = b + 1;
    }
}

void load(App& app) {
    g.loaded = true;
    std::string err;
    faces3d::Table t;
    if (!faces3d::load_players(app.turbo_output().u8string(), t, &err)) {
        g.message = err + " (the list starts empty; Apply overwrites the file)";
        g.message_ok = false;
        t.clear();
    }
    g.draft = t;
    g.applied = faces3d::players_json(t);
    g.backup_error.clear();
    if (!faces3d::load_preview_backup(app.turbo_output().u8string(), g.backup, &g.backup_error)) g.backup.clear();
}

std::string hair_label(int id) {
    const hair::Style* s = hair::find(id);
    return std::to_string(id) + (s ? "  " + hair::describe(*s) : std::string());
}

std::string beard_label(int id) {
    static const char* const kLook[] = {"clean-shaven", "stubble", "moustache / goatee", "short beard", "full beard"};
    for (const auto& l : faces::looks::kFacialHair)
        if (l.id == id && l.look < 5) return std::to_string(id) + "  " + kLook[l.look];
    return std::to_string(id);
}

bool matches(const std::string& label, const char* search) {
    if (!search[0]) return true;
    std::string a = label, b = search;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.find(b) != std::string::npos;
}

// Searchable id picker over a verified-id list; 0 = none
bool id_picker(const char* label, int64_t& value, const std::vector<int>& ids, char* search, size_t search_size,
               std::string (*name)(int)) {
    bool changed = false;
    const std::string preview = value > 0 ? name(static_cast<int>(value)) : std::string("none (keep his own)");
    ImGui::SetNextItemWidth(S(320.0f));
    if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "id or words (short, curly, full beard...)", search, search_size);
        if (ImGui::Selectable("none (keep his own)", value == 0)) {
            value = 0;
            changed = true;
        }
        std::vector<int> shown;
        shown.reserve(ids.size());
        for (int id : ids)
            if (matches(name(id), search)) shown.push_back(id);
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(shown.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const int id = shown[i];
                ImGui::PushID(id);
                if (ImGui::Selectable(name(id).c_str(), value == id)) {
                    value = id;
                    changed = true;
                }
                ImGui::PopID();
            }
        if (shown.empty()) ImGui::TextDisabled("no verified id matches");
        ImGui::EndCombo();
    }
    return changed;
}

std::string status_line(App& app, bool& warn) {
    warn = false;
    if (!app.faces3d_report) return "Hook status: not available here (no game hooks).";
    const lualoader::Faces3dStatus f = app.faces3d_report();
    if (!f.opted_in) return "Hook: off at this game start (faces3d_on.txt was not there when the game started).";
    std::string s = f.installed ? "Hook: installed" : "Hook: opted in, not installed (see Status tab)";
    if (f.killed) s += " | kill switch present", warn = true;
    if (f.legacy_refused) s += " | substitution REFUSED (mods\\legacy player_kit.lua)", warn = true;
    if (f.installed) {
        s += f.captured_md5.empty() ? std::string(" | stock not captured yet") : " | stock " + f.captured_md5.substr(0, 8);
        if (f.substitute) {
            s += std::string(" | override ") +
                 (!f.header_ok ? "missing" : !f.build_key_ok ? "for another build" : (f.stock_matches_header || f.captured_md5.empty()) ? "ok" : "STALE");
            if (f.header_ok && (!f.build_key_ok || (!f.stock_matches_header && !f.captured_md5.empty()))) warn = true;
        }
        s += " | substituted " + std::to_string(f.substituted) + " | fallbacks " + std::to_string(f.fallbacks);
        if (f.fallbacks > 0) warn = true;
        if (!f.last_error.empty()) s += " | last: " + f.last_error;
    }
    return s;
}

int64_t headclass_of(App& app, const Table& t, int64_t pid) {
    const PlayerRow* r = app.model.player(pid);
    if (!r || !t.has("headclasscode")) return -1;
    return app.db.get_int(t, r->rec, "headclasscode", -1);
}

// ---------------------------------------------------------------- live preview
std::string clock_now() {
    const std::time_t now = std::time(nullptr);
    char b[16] = "";
    if (const std::tm* tm = std::localtime(&now)) std::strftime(b, sizeof b, "%H:%M:%S", tm);
    return b;
}

// His preview fields as they are now (only fields the table has)
std::map<std::string, int64_t> read_codes(App& app, const Table& t, uint64_t rec) {
    std::map<std::string, int64_t> m;
    for (const char* f : faces3d::kPreviewFields)
        if (t.has(f)) m[f] = app.db.get_int(t, rec, f, 0);
    return m;
}

// Raw writes (Database::set_int, range-checked): no undo step, no kept edit (reapply_edits.json), no development-plan sync, so a
// preview never travels to another career. true when every field was written.
bool raw_write(App& app, const Table& t, uint64_t rec, int64_t pid, const std::map<std::string, int64_t>& w, std::string& err) {
    bool ok = true;
    std::string line;
    for (const auto& [f, v] : w) {
        std::string e;
        if (!app.db.set_int(t, rec, f, v, &e)) {
            ok = false;
            err += (err.empty() ? "" : "; ") + f + ": " + e;
            continue;
        }
        line += (line.empty() ? "" : ", ") + f + " = " + std::to_string(v);
    }
    if (!line.empty()) {
        app.model.refresh_player(pid, app.today());
        ++app.gen;
        app.log("3D hair preview: player " + std::to_string(pid) + " " + line + " (preview write: no undo step, not kept for other careers)");
    }
    return ok;
}

// The list this game start runs, re-read when the running override's table changes (or after an Apply here)
void refresh_running(App& app, const lualoader::Faces3dStatus* st) {
    const std::string key = st ? st->header_table : std::string();
    if (key == g.running_key) return;
    g.running_key = key;
    g.running.clear();
    g.running_why.clear();
    g.running_ok = !key.empty() && faces3d::running_table(app.turbo_output().u8string(), key, g.running, &g.running_why);
}

faces3d::LiveGate gate_for(App& app, int64_t pid) {
    if (!app.faces3d_report) return faces3d::live_gate(nullptr, nullptr, pid);
    const lualoader::Faces3dStatus st = app.faces3d_report();
    refresh_running(app, &st);
    faces3d::LiveGate lg = faces3d::live_gate(&st, g.running_ok ? &g.running : nullptr, pid);
    if (!g.running_ok && st.substituted > 0 && !g.running_why.empty() && !lg.active) lg.why = g.running_why;
    return lg;
}

void pv_say(const std::string& m, bool ok) {
    g.pv_message = m;
    g.pv_message_ok = ok;
}

// Ask the game for a 3D capture of him (the Miniface "Generate from 3D model" request: his id, his club, default camera)
bool request_capture(App& app, const PlayerRow& p, std::string& err) {
    capture::CaptureService* svc = app.capture.get();
    if (!svc) {
        err = "the game's 3D capture is not available here";
        return false;
    }
    const capture::Status st = svc->status();
    if (!st.installed || !st.available) {
        err = "the game cannot render now: " + (st.reason.empty() ? std::string("capture not available") : st.reason) + " (menus only, not in a match)";
        return false;
    }
    if (st.busy) {
        err = "another render is running (" + st.busy_label + "): try again in a moment";
        return false;
    }
    capture::Request r;
    r.id = static_cast<int32_t>(p.playerid);
    r.second_id = p.club > 0 ? static_cast<int32_t>(p.club) : -1;
    r.camera = 0;
    r.use_template = true;
    r.label = "3D hair preview " + p.name;
    if (!svc->request(r, &err)) return false;
    g.pending = p.playerid;
    g.sent_at = ImGui::GetTime();
    return true;
}

// The record of his originals: kept when made this session, or (from the file) when his codes still are the ones it wrote;
// otherwise a fresh record of what he has now
faces3d::PreviewRecord& record_for(int64_t pid, const std::string& name, const std::map<std::string, int64_t>& cur) {
    auto it = g.backup.find(pid);
    if (it != g.backup.end() && !g.session_records.count(pid)) {
        bool same = true;
        for (const auto& [f, v] : it->second.written) {
            const auto c = cur.find(f);
            same = same && c != cur.end() && c->second == v;
        }
        if (!same) g.backup.erase(it), it = g.backup.end();
    }
    if (it == g.backup.end()) {
        faces3d::PreviewRecord r;
        r.name = name;
        r.original = cur;
        it = g.backup.emplace(pid, r).first;
    }
    g.session_records.insert(pid);
    return it->second;
}

void do_preview(App& app, const Table& t, const PlayerRow& p, const faces3d::Entry& e, const faces3d::LiveGate& lg) {
    const std::map<std::string, int64_t> cur = read_codes(app, t, p.rec);
    const auto w = faces3d::preview_writes(lg.follow_hair ? e.hair : 0, lg.follow_beard ? e.beard : 0, cur);
    std::string err;
    if (!w.empty()) {
        faces3d::PreviewRecord& r = record_for(p.playerid, p.name, cur);
        std::map<std::string, int64_t> after = cur;
        for (const auto& [f, v] : w) after[f] = v;
        r.written.clear();
        for (const auto& [f, v] : after)
            if (r.original.count(f) && r.original.at(f) != v) r.written[f] = v;
        // the backup reaches the disk before the database changes (a crash leaves the way back)
        if (!faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err)) {
            pv_say("Nothing written: the backup could not be saved (" + err + ")", false);
            return;
        }
        if (!raw_write(app, t, p.rec, p.playerid, w, err)) {
            pv_say("Writing his codes failed (" + err + "): press Revert", false);
            return;
        }
    }
    if (!request_capture(app, p, err)) {
        pv_say(std::string(w.empty() ? "" : "His codes are written, but ") + "no picture: " + err, false);
        return;
    }
    pv_say("Asked the game to render him" + std::string(w.empty() ? " (his codes already hold this choice)" : "") + "...", true);
}

// Write his originals back (only fields that differ). false and err when a write failed (the record is kept then)
bool revert_codes(App& app, const Table& t, uint64_t rec, int64_t pid, std::string& err) {
    const auto it = g.backup.find(pid);
    if (it == g.backup.end()) return true;
    const std::map<std::string, int64_t> cur = read_codes(app, t, rec);
    std::map<std::string, int64_t> w;
    for (const auto& [f, v] : it->second.original)
        if (cur.count(f) && cur.at(f) != v) w[f] = v;
    if (!raw_write(app, t, rec, pid, w, err)) return false;
    g.backup.erase(pid);
    g.session_records.erase(pid);
    return faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
}

std::string codes_text(const std::map<std::string, int64_t>& m) {
    std::string s;
    for (const auto& [f, v] : m) s += (s.empty() ? "" : ", ") + f + " " + std::to_string(v);
    return s.empty() ? "nothing" : s;
}

// A record left by an earlier session (crash, Turbo closed mid-preview): Revert only while his codes are still the ones written
void leftover_banner(App& app, const Table& t) {
    int64_t drop = 0;
    for (const auto& [pid, r] : g.backup) {
        if (g.session_records.count(pid) && g.draft.count(pid)) continue;  // his own panel has Revert
        ImGui::PushID(static_cast<int>(pid & 0x7FFFFFFF));
        const PlayerRow* p = app.model.player(pid);
        bool still = p != nullptr;
        if (p) {
            const auto cur = read_codes(app, t, p->rec);
            for (const auto& [f, v] : r.written) still = still && cur.count(f) && cur.at(f) == v;
        }
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "A preview of %s (%lld) was not reverted: original %s.", r.name.c_str(),
                           static_cast<long long>(pid), codes_text(r.original).c_str());
        if (still) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert it")) {
                std::string err;
                if (revert_codes(app, t, p->rec, pid, err)) pv_say("Reverted the earlier preview of " + r.name + ".", true);
                else pv_say("Revert failed: " + err, false);
                ImGui::PopID();
                return;  // the map changed
            }
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("(his codes here are not the previewed ones: another career?)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Forget it")) drop = pid;
        }
        ImGui::PopID();
    }
    if (drop) {
        g.backup.erase(drop);
        std::string err;
        faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
    }
}

void preview_panel(App& app, const Table& t, const PlayerRow& p, faces3d::Entry& e) {
    ImGui::Spacing();
    ImGui::SeparatorText("Live preview (rendered by the game)");
    ImGui::TextWrapped("Preview works only for players enrolled with 'Follow his database codes' when the game started. Enrolling a "
                       "player the first time (or switching him to follow) needs Apply and a game restart. It renders in the menus "
                       "(career hub, squad screens), not during a match. Colour switches take effect at the next game start.");
    const faces3d::LiveGate lg = gate_for(app, p.playerid);
    ImGui::TextColored(lg.active ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s%s", lg.active ? "" : "Gate not active for him this session: ",
                       lg.why.c_str());
    const bool previewed = g.backup.count(p.playerid) != 0;
    const capture::Status cst = app.capture ? app.capture->status() : capture::Status();
    const bool can = lg.active && app.capture && cst.installed && cst.available && !cst.busy && g.pending == 0;
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button("Preview##f3pv")) do_preview(app, t, p, e, lg);
    if (!can) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Writes his hair / beard codes (and hair style 0) in this career, the originals kept in "
                          "turbo_output\\faces3d\\preview_backup.json first, then asks the game for a picture. 'none' = his current code.");
    ImGui::SameLine();
    if (!previewed) ImGui::BeginDisabled();
    if (ImGui::Button("Revert##f3pv")) {
        std::string err;
        if (revert_codes(app, t, p.rec, p.playerid, err)) {
            std::string cerr;
            const bool again = lg.active && request_capture(app, p, cerr);
            pv_say(std::string("His original codes are back") + (again ? "; rendering him again..." : "."), true);
        } else {
            pv_say("Revert failed: " + err, false);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep##f3pv")) {
        faces3d::Table next = g.draft;
        faces3d::Entry& k = next[p.playerid];
        k = e;
        k.follow = g.keep_follow;
        std::string build;
        if (app.faces3d_report) build = app.faces3d_report().running_build;
        if (build.empty()) build = app.asset_running_key();
        if (!k.follow && k.hair <= 0 && k.beard <= 0) {
            pv_say("Keep: choose a hair or a beard first (a fixed look needs one).", false);
        } else {
            const faces3d::ApplyResult r =
                faces3d::apply(app.turbo_output().u8string(), next, build, [&](int64_t pid) { return headclass_of(app, t, pid); });
            if (!r.ok) {
                pv_say("Keep: nothing changed (" + r.message + ")", false);
            } else {
                g.draft = next;
                g.applied = faces3d::players_json(next);
                faces3d_forget_render(app, false, p.playerid);  // the chooser's cached head is the old look
                std::string err;
                if (k.follow) {
                    g.backup.erase(p.playerid);
                    g.session_records.erase(p.playerid);
                    faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
                    pv_say("Kept: he keeps following his codes; this career keeps the previewed codes, other careers show their own. " + r.message,
                           true);
                } else if (revert_codes(app, t, p.rec, p.playerid, err)) {
                    pv_say("Kept as a fixed look (hair " + std::to_string(k.hair) + ", beard " + std::to_string(k.beard) +
                               ") for the next game start, in every career. His original codes are back in this career, so until the restart "
                               "the game shows his own look. " + r.message,
                           true);
                } else {
                    pv_say("Kept as a fixed look, but putting his original codes back failed (" + err + "): press Revert. " + r.message, false);
                }
                app.log("3D hair preview: Keep " + std::to_string(p.playerid) + ": " + r.message);
            }
        }
    }
    if (!previewed) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Bakes this hair / beard into players.json as a fixed entry (the override is regenerated; restart the game): other "
                          "careers are unaffected, and this career's codes are put back. Tick 'keep following' to keep the codes instead.");
    ImGui::SameLine();
    ImGui::Checkbox("keep following his codes##f3pv", &g.keep_follow);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keep leaves him following the database: this career keeps the previewed codes; in other careers (TORINO, "
                          "EVERTON...) he shows whatever hair / beard codes that career holds.");
    if (previewed) ImGui::TextDisabled("Original codes: %s", codes_text(g.backup[p.playerid].original).c_str());
    if (g.pending == p.playerid) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "rendering... (%.0f s)", ImGui::GetTime() - g.sent_at);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##f3pv")) {
            if (app.capture) app.capture->cancel();
            g.pending = 0;
            pv_say("Cancelled (his codes stay as previewed until Revert).", false);
        }
    } else if (!can && lg.active) {
        ImGui::TextDisabled("%s", !app.capture ? "No 3D capture here." : cst.busy ? "Another render is running." : cst.reason.c_str());
    }
    if (g.shown == p.playerid && !g.image.empty()) {
        const float side = S(256.0f);
        TextureCache::Pic pic = app.textures.pixels("faces3d_preview", g.version, g.image);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(side, side));
        if (pic.tex && pic.w > 0 && pic.h > 0) {
            const float s = side / float(std::max(pic.w, pic.h));
            const ImVec2 a(p0.x + (side - pic.w * s) * 0.5f, p0.y + (side - pic.h * s) * 0.5f);
            ImGui::GetWindowDrawList()->AddImage(pic.tex->GetTexRef(), a, ImVec2(a.x + pic.w * s, a.y + pic.h * s));
        }
        ImGui::TextDisabled("%s", g.caption.c_str());
    }
    if (!g.pv_message.empty())
        ImGui::TextColored(g.pv_message_ok ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", g.pv_message.c_str());
}

}  // namespace

bool faces3d_preview_pending() { return g.pending != 0; }

void tick_faces3d_preview(App& app) {
    if (g.pending == 0) return;
    if (!app.capture) {
        g.pending = 0;
        return;
    }
    capture::Result r;
    if (app.capture->poll(r)) {
        if (r.id != g.pending) {
            app.log("3D hair preview: an answer for id " + std::to_string(r.id) + " arrived while waiting for " + std::to_string(g.pending) + " (dropped)");
            return;
        }
        const int64_t pid = g.pending;
        g.pending = 0;
        if (!r.ok) {
            pv_say("The game did not render him: " + r.error, false);
            return;
        }
        g.shown = pid;
        g.image = std::move(r.image);
        ++g.version;
        std::string codes;
        if (const Table* t = app.db.table("players"))
            if (const PlayerRow* p = app.model.player(pid)) codes = codes_text(read_codes(app, *t, p->rec));
        g.caption = "Rendered by the game at " + clock_now() + " (" + r.format + "); his codes: " + codes;
        pv_say("Rendered.", true);
        return;
    }
    if (ImGui::GetTime() - g.sent_at > kPreviewTimeout) {
        app.capture->cancel();
        g.pending = 0;
        pv_say("No picture after " + std::to_string(int(kPreviewTimeout)) + " s: the game may be in a match or loading. His codes stay as "
               "previewed until Revert.",
               false);
    }
}

const faces3d::Entry* faces3d_draft_entry(int64_t playerid) {
    const auto it = g.draft.find(playerid);
    return it == g.draft.end() ? nullptr : &it->second;
}

void faces3d_gate_section(App& app, const Table& t, const PlayerRow& p) {
    if (!g.loaded) load(app);
    ImGui::PushID("faces3d_gate");
    const fs::path out = app.turbo_output();
    std::error_code ec;
    if (fs::exists(rel(app.bridge.root(), lualoader::kLegacyKitRel), ec))
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua exists: Live Editor serves that file and Turbo refuses "
                           "the 3D hair / beard while it is there. Remove it to use this.");
    if (!g.backup_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Preview backup unreadable: %s", g.backup_error.c_str());
    leftover_banner(app, t);

    const int64_t hc = headclass_of(app, t, p.playerid);
    auto it = g.draft.find(p.playerid);
    bool enrolled = it != g.draft.end();
    const bool can_enrol = hc == 0;
    const bool locked = !can_enrol && !enrolled;  // a non-scanned player can be un-enrolled, never enrolled
    if (locked) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Give him a generic 3D hair and/or beard", &enrolled)) {
        if (enrolled) {
            faces3d::Entry e;
            e.name = p.name;
            g.draft[p.playerid] = e;
        } else {
            g.draft.erase(p.playerid);
        }
        it = g.draft.find(p.playerid);
    }
    if (locked) ImGui::EndDisabled();
    if (!can_enrol) {
        ImGui::SameLine();
        if (hc < 0) ImGui::TextDisabled("(head class unknown)");
        else ImGui::TextDisabled("(only scanned heads, head class 0: this one is %lld)", static_cast<long long>(hc));
    }
    if (it != g.draft.end()) {
        faces3d::Entry& e = it->second;
        ImGui::Indent();
        id_picker("Hair##f3h", e.hair, faces3d::ok_hair_ids(), g.hair_search, sizeof(g.hair_search), hair_label);
        ImGui::SameLine();
        if (e.hair <= 0) ImGui::BeginDisabled();
        ImGui::Checkbox("hair colours from the database##f3hc", &e.hair_colour);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Hair, eyebrow and facial-hair colours from his DB colour fields (otherwise the scan's colour mask)");
        if (e.hair <= 0) ImGui::EndDisabled();
        id_picker("Beard##f3b", e.beard, faces3d::ok_beard_ids(), g.beard_search, sizeof(g.beard_search), beard_label);
        ImGui::SameLine();
        if (e.beard <= 0) ImGui::BeginDisabled();
        ImGui::Checkbox("beard colour from the database##f3bc", &e.beard_colour);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The generic beard takes his DB facial-hair colour (off: the scan's colour mask, which can render green)");
        if (e.beard <= 0) ImGui::EndDisabled();
        if (!e.follow && e.hair <= 0 && e.beard <= 0) ImGui::TextDisabled("Choose a hair or a beard (or untick to un-enrol him).");
        ImGui::Checkbox("Follow his database codes (live preview)##f3follow", &e.follow);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gate v3 'follow': the game draws his generic hair / beard from his hairtypecode / facialhairtypecode (verified "
                              "ids, hair style 0), so Preview can show a choice without a restart. Needs Apply and one game restart. In other "
                              "careers he shows whatever codes that career holds: Keep bakes a fixed look instead.");
        if (e.follow) ImGui::TextDisabled("Following: the pickers above are what Preview writes and Keep bakes; players.json stores no ids for him.");
        preview_panel(app, t, p, e);
        ImGui::Unindent();
    }

    // the whole list + Apply / Remove
    const bool pending = faces3d::players_json(g.draft) != g.applied;
    ImGui::Spacing();
    ImGui::Text("Enrolled: %zu player(s)%s", g.draft.size(), pending ? "  (changed, not applied)" : "");
    if (!g.draft.empty() && ImGui::TreeNode("##f3list", "List")) {
        int64_t drop = 0;
        for (const auto& [pid, e] : g.draft) {
            ImGui::PushID(static_cast<int>(pid & 0x7FFFFFFF));
            if (ImGui::SmallButton("x")) drop = pid;
            ImGui::SameLine();
            if (e.follow)
                ImGui::Text("%lld %s: follows his database codes (%s)", static_cast<long long>(pid), e.name.c_str(),
                            e.follow_hair && e.follow_beard ? "hair and beard" : e.follow_hair ? "hair" : "beard");
            else
                ImGui::Text("%lld %s: hair %s, beard %s", static_cast<long long>(pid), e.name.c_str(),
                            e.hair > 0 ? std::to_string(e.hair).c_str() : "own", e.beard > 0 ? std::to_string(e.beard).c_str() : "own");
            ImGui::PopID();
        }
        if (drop) g.draft.erase(drop);
        ImGui::TreePop();
    }
    const bool have_cache = fs::exists(rel(out, lualoader::kStockCacheRel), ec);
    if (ImGui::Button("Apply##f3")) {
        std::string build;
        if (app.faces3d_report) build = app.faces3d_report().running_build;
        if (build.empty()) build = app.asset_running_key();
        const faces3d::ApplyResult r =
            faces3d::apply(out.u8string(), g.draft, build, [&](int64_t pid) { return headclass_of(app, t, pid); });
        g.message = r.message;
        g.message_ok = r.ok;
        if (r.ok) g.applied = faces3d::players_json(g.draft);
        app.log("3D hair/beard: " + r.message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove##f3")) {
        std::string err;
        g.message_ok = faces3d::remove(out.u8string(), &err);
        g.message = g.message_ok ? "Removed lua_override\\player_kit.lua and faces3d_on.txt (your list is kept). Restart the game: every head is stock again."
                                 : "Remove failed: " + err;
        app.log("3D hair/beard: " + g.message);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deletes the generated override and faces3d_on.txt; players.json (your list) stays");
    if (!have_cache) {
        ImGui::SameLine();
        if (ImGui::Button("Enable capture##f3")) {
            std::string err;
            g.message_ok = lualoader::set_faces3d_opt_in(out.u8string(), true, &err);
            g.message = g.message_ok ? "faces3d_on.txt written: restart the game once (Turbo saves the game's own player_kit.lua at start), then Apply."
                                     : "Could not write faces3d_on.txt: " + err;
            app.log("3D hair/beard: " + g.message);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turbo needs the game's own player_kit.lua (turbo_output\\faces3d\\stock_player_kit.cache), saved by the hook at a game start");
    }
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "Restart the game to apply: player_kit.lua is read only when the game starts.");
    if (!g.message.empty())
        ImGui::TextColored(g.message_ok ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", g.message.c_str());
    bool warn = false;
    const std::string st = status_line(app, warn);
    if (warn) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", st.c_str());
    else ImGui::TextDisabled("%s", st.c_str());
    if (!have_cache) ImGui::TextDisabled("Stock player_kit.lua not captured yet: Apply needs one game start with 'Enable capture'.");
    ImGui::PopID();
}

}  // namespace turbo
