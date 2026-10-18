// FC 27 LE Turbo GUI - Players / Managers > Appearance > "Hair & beard (3D)" (ui_faces3d_gate.h). A look is two DB codes in the
// Turbo bands (core/faces3d.h, template v4): written live, drawn by the game at its next model build, no restart. The saved list
// of older versions (players.json) is still read at game start; the hook's state comes from App::faces3d_report.
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
#include "ui_bodytype_thumbs.h"
#include "ui_faces.h"
#include "ui_images.h"

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
    int hair_length = -1;  // the hair gallery's length filter (hair::Length, -1 = any)
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
constexpr int64_t kMgrKey = faces3d::kManagerBackupKey;
const char* const kAutoRestoreSwitch = "faces3d_auto_restore_on.txt";  // turbo_output: the marker restore writes (else LOG-ONLY)  // preview_backup.json key of a manager: kMgrKey + managerid

// A Turbo code of a look field (a band code or the "his own, live" code): never recorded as his own
bool turbo_code(const std::string& f, int64_t v) {
    if (f == "hairtypecode") return faces3d::is_turbo_hair_code(v);
    if (f == "facialhairtypecode") return faces3d::is_turbo_beard_code(v);
    return false;
}

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

// Gallery picker over a verified-id list (2.10): a thumbnail of the current id, its label, and a modal grid of the game's
// own preview pictures (the Players > Appearance gallery cell, ui_images.cpp). A cell with no preview shows the text label
// instead. 0 = none. `length` (hair only, may be null): the length filter; -1 = any.
bool gallery_picker(App& app, const char* title, const char* field, int64_t& value, const std::vector<int>& ids, char* search,
                    size_t search_size, int* length, std::string (*name)(int)) {
    bool changed = false;
    ImGui::PushID(field);
    const float thumb = S(56.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    if (value > 0) gallery_picture(app, field, value, thumb, name(static_cast<int>(value)));
    else gallery_picture(app, field, 0, thumb, "");
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(value > 0 ? name(static_cast<int>(value)).c_str() : "his own");
    if (ImGui::Button("Choose...##pick")) ImGui::OpenPopup("##f3pick");
    if (value > 0) {
        ImGui::SameLine();
        if (ImGui::Button("None##pickclear")) {
            value = 0;
            changed = true;
        }
    }
    ImGui::EndGroup();
    ImGui::SetNextWindowSize(ImVec2(S(760.0f), S(560.0f)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##f3pick", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("%s: the 3D models that fit real faces, with the game's own previews", title);
        if (length) {
            const char* cur = *length < 0 ? "Any length" : hair::length_name(*length);
            ImGui::SetNextItemWidth(S(170.0f));
            if (ImGui::BeginCombo("##f3len", cur)) {
                if (ImGui::Selectable("Any length", *length < 0)) *length = -1;
                for (int l = 0; l < hair::kLengthCount; ++l)
                    if (ImGui::Selectable(hair::length_name(l), *length == l)) *length = l;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(S(260.0f));
        ImGui::InputTextWithHint("##f3search", "id or words (short, curly, full beard...)", search, search_size);
        std::vector<GalleryPick> shown;
        for (const GalleryPick& pk : gallery_pick_list(gear_pictures(app), field, ids, length ? *length : -1))
            if (matches(name(static_cast<int>(pk.id)), search)) shown.push_back(pk);
        ImGui::TextDisabled("%zu of %zu verified ids", shown.size(), ids.size());
        const float cell = S(100.0f);
        ImGui::BeginChild("##f3grid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        const int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) /
                                         (cell + ImGui::GetStyle().ItemSpacing.x)));
        const size_t total = shown.size() + 1;  // cell 0 = none
        const int nrows = int((total + size_t(cols) - 1) / size_t(cols));
        ImGuiListClipper clip;
        clip.Begin(nrows, cell + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y);
        int64_t picked = -1;
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                for (int c = 0; c < cols; ++c) {
                    const size_t i = size_t(r) * size_t(cols) + size_t(c);
                    if (i >= total) break;
                    const int64_t id = i == 0 ? 0 : shown[i - 1].id;
                    if (c) ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::PushID(static_cast<int>(id));
                    const std::string label = id ? name(static_cast<int>(id)) : std::string();
                    std::string cap = id ? std::to_string(id) : std::string("None");
                    if (id && length) cap += std::string(" ") + hair::length_name(hair::lookup(id).length);
                    char bid[48];
                    std::snprintf(bid, sizeof(bid), "%s%lld", field, static_cast<long long>(id));
                    if (gallery_picture_cell(app, field, id, cell, cap, label, bid, value == id, id == 0)) picked = id;
                    ImGui::PopID();
                    ImGui::EndGroup();
                    if (id && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label.c_str());
                }
            }
        }
        ImGui::EndChild();
        if (picked >= 0) {
            value = picked;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Close##f3pickclose")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

int64_t headclass_of(App& app, const Table& t, int64_t pid) {
    const PlayerRow* r = app.model.player(pid);
    if (!r || !t.has("headclasscode")) return -1;
    return app.db.get_int(t, r->rec, "headclasscode", -1);
}

// ---------------------------------------------------------------- the game's picture of him
std::string clock_now() {
    const std::time_t now = std::time(nullptr);
    char b[16] = "";
    if (const std::tm* tm = std::localtime(&now)) std::strftime(b, sizeof b, "%H:%M:%S", tm);
    return b;
}

// His codes as they are now, for the given fields (only fields the table has)
std::map<std::string, int64_t> read_fields(App& app, const Table& t, uint64_t rec, const char* const* fields, size_t n) {
    std::map<std::string, int64_t> m;
    for (size_t i = 0; i < n; ++i)
        if (t.has(fields[i])) m[fields[i]] = app.db.get_int(t, rec, fields[i], 0);
    return m;
}
std::map<std::string, int64_t> read_codes(App& app, const Table& t, uint64_t rec) {
    return read_fields(app, t, rec, faces3d::kPreviewFields, 3);
}
std::map<std::string, int64_t> read_look(App& app, const Table& t, uint64_t rec) { return read_fields(app, t, rec, faces3d::kLookFields, 2); }

// A card line: the in-app Log and turbo_gui.log (App::log_hook; the Tester's parser reads the file)
void flog(App& app, const std::string& line) {
    app.log(line);
    if (app.log_hook) app.log_hook(line);
}

// Raw writes (Database::set_int, range-checked): no undo step, no kept edit (reapply_edits.json), no development-plan sync. The
// codes live in this career's database (saved with the career). true when every field was written.
bool raw_write(App& app, const Table& t, uint64_t rec, int64_t id, bool manager, const std::map<std::string, int64_t>& w, std::string& err) {
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
        if (!manager) app.model.refresh_player(id, app.today());
        ++app.gen;
        flog(app, std::string("3D hair & beard: ") + (manager ? "manager " : "player ") + std::to_string(id) + " " + line +
                " (this career only; no undo step)");
    }
    return ok;
}

void pv_say(const std::string& m, bool ok) {
    g.pv_message = m;
    g.pv_message_ok = ok;
}

// Ask the game for a 3D capture of him (the Miniface "Generate from 3D model" request: his id, his club, default camera)
bool request_capture(App& app, const PlayerRow& p, std::string& err) {
    capture::CaptureService* svc = app.capture.get();
    if (!svc) {
        err = "the game's 3D picture is not available here";
        return false;
    }
    const capture::Status st = svc->status();
    if (!st.installed || !st.available) {
        err = "the game cannot draw him now: " + (st.reason.empty() ? std::string("not available") : st.reason) + " (menus only, not in a match)";
        return false;
    }
    if (st.busy) {
        err = "another picture is being drawn (" + st.busy_label + "): try again in a moment";
        return false;
    }
    if (*bodytype_thumbs_busy_text()) return err = bodytype_thumbs_busy_text(), false;  // 2.17 looks T: the run owns the service
    capture::Request r;
    r.id = static_cast<int32_t>(p.playerid);
    r.second_id = p.club > 0 ? static_cast<int32_t>(p.club) : -1;
    r.camera = 0;
    r.use_template = true;
    r.label = "3D hair & beard " + p.name;
    if (!svc->request(r, &err)) return false;
    g.pending = p.playerid;
    g.sent_at = ImGui::GetTime();
    return true;
}

// The record of his originals: kept when made this session, or (from the file) when his codes still are the ones it wrote;
// otherwise a fresh record of what he has now
faces3d::PreviewRecord& record_for(int64_t pid, const std::string& name, const std::map<std::string, int64_t>& cur) {
    auto it = g.backup.find(pid);
    if (it != g.backup.end() && it->second.original.empty()) g.backup.erase(it), it = g.backup.end();  // no originals = no record
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
        for (const auto& [f, v] : cur)  // a Turbo code is a look, never his own: left out ("his own" then writes 0 for it)
            if (!turbo_code(f, v)) r.original[f] = v;
        it = g.backup.emplace(pid, r).first;
    }
    if (it->second.name.empty()) it->second.name = name;
    g.session_records.insert(pid);
    return it->second;
}

// Write his originals back (only fields that differ). false and err when a write failed (the record is kept then)
bool revert_codes(App& app, const Table& t, uint64_t rec, int64_t pid, std::string& err) {
    const auto it = g.backup.find(pid);
    if (it == g.backup.end()) return true;
    const std::map<std::string, int64_t> cur = read_codes(app, t, rec);
    std::map<std::string, int64_t> w;
    for (const auto& [f, v] : it->second.original)
        if (cur.count(f) && cur.at(f) != v) w[f] = v;
    if (!raw_write(app, t, rec, pid, false, w, err)) return false;
    g.backup.erase(pid);
    g.session_records.erase(pid);
    return faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
}

std::string codes_text(const std::map<std::string, int64_t>& m) {
    std::string s;
    for (const auto& [f, v] : m) s += (s.empty() ? "" : ", ") + f + " " + std::to_string(v);
    return s.empty() ? "nothing" : s;
}

// A record left by an earlier session (crash, Turbo closed mid-change), shown only when it holds his originals and his codes
// are still the ones written (otherwise it belongs to another career, or there is nothing to put back: no banner). An empty
// record (no originals) is dropped from memory and the file. Band codes are a kept look, not a leftover: no banner for them.
void leftover_banner(App& app, const Table& t, int64_t current) {
    bool dropped = false;
    for (auto it = g.backup.begin(); it != g.backup.end();)
        if (it->second.original.empty()) it = g.backup.erase(it), dropped = true;
        else ++it;
    if (dropped) {
        std::string err;
        faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
    }
    for (const auto& [pid, r] : g.backup) {
        if (pid == current) continue;  // his own card has "His own hair & beard"
        const PlayerRow* p = app.model.player(pid);
        if (!p || r.written.empty()) continue;
        const auto cur = read_codes(app, t, p->rec);
        bool still = true, band = false;
        for (const auto& [f, v] : r.written) {
            still = still && cur.count(f) && cur.at(f) == v;
            band = band || (f == "hairtypecode" && faces3d::hair_from_code(v)) || (f == "facialhairtypecode" && faces3d::beard_from_code(v));
        }
        if (!still || band) continue;
        const std::string name = r.name.empty() ? p->name : r.name;
        ImGui::PushID(static_cast<int>(pid & 0x7FFFFFFF));
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "An earlier hair preview of %s (%lld) was not put back: original %s.", name.c_str(),
                           static_cast<long long>(pid), codes_text(r.original).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Put it back")) {
            std::string err;
            if (revert_codes(app, t, p->rec, pid, err)) pv_say("Put back the earlier preview of " + name + ".", true);
            else pv_say("Putting it back failed: " + err, false);
            ImGui::PopID();
            return;  // the map changed
        }
        ImGui::PopID();
    }
}

struct Look {
    int64_t hair = 0, beard = 0;  // generic ids his codes select now (0 = his own)
};
Look look_of(const std::map<std::string, int64_t>& codes) {
    Look l;
    if (const auto it = codes.find("hairtypecode"); it != codes.end()) l.hair = faces3d::hair_from_code(it->second);
    if (const auto it = codes.find("facialhairtypecode"); it != codes.end()) l.beard = faces3d::beard_from_code(it->second);
    return l;
}

// Green when changes show at once; amber only when the hook missed the game's start (restart once); otherwise why it is off
void live_line(App& app) {
    lualoader::Faces3dStatus st;
    const lualoader::Faces3dStatus* sp = nullptr;
    if (app.faces3d_report) st = app.faces3d_report(), sp = &st;
    const faces3d::LiveLooks l = faces3d::live_looks(sp);
    if (l.active) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", l.why.c_str());
    else if (l.restart_needed) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "%s", l.why.c_str());  // the one honest restart case
    else ImGui::TextDisabled("%s", l.why.c_str());
}

void draw_picture(App& app, int64_t pid) {
    if (g.pending == pid) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "drawing... (%.0f s)", ImGui::GetTime() - g.sent_at);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##f3pv")) {
            if (app.capture) app.capture->cancel();
            g.pending = 0;
            pv_say("Cancelled (the new look stays).", false);
        }
    }
    if (g.shown == pid && !g.image.empty()) {
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
}

// The saved list of older Turbo versions (players.json, read when the game starts): shown only for a player on it
void saved_list_line(const PlayerRow& p) {
    const auto it = g.draft.find(p.playerid);
    if (it == g.draft.end()) return;
    ImGui::TextDisabled("He has a look saved by an older Turbo (hair %s, beard %s). \"His own hair & beard\" takes it off.",
                        it->second.hair > 0 ? std::to_string(it->second.hair).c_str() : "own",
                        it->second.beard > 0 ? std::to_string(it->second.beard).c_str() : "own");
}

// Pickers: the choice being made (per person shown), reset when another person is shown
struct Pick {
    bool manager = false;
    int64_t id = -1;
    int64_t hair = 0, beard = 0;
};
Pick g_pick;

void pickers(App& app, bool manager, int64_t id, const Look& now) {
    if (g_pick.manager != manager || g_pick.id != id) g_pick = Pick{manager, id, now.hair, now.beard};
    gallery_picker(app, "Hair", "hairtypecode", g_pick.hair, faces3d::band_hair_ids(), g.hair_search, sizeof(g.hair_search), &g.hair_length,
                   hair_label);
    gallery_picker(app, "Beard", "facialhairtypecode", g_pick.beard, faces3d::band_beard_ids(), g.beard_search, sizeof(g.beard_search), nullptr,
                   beard_label);
}

}  // namespace

bool faces3d_preview_pending() { return g.pending != 0; }

void faces3d_set_choice(bool manager, int64_t id, int64_t hair, int64_t beard) { g_pick = Pick{manager, id, hair, beard}; }

int faces3d_restore_markers(App& app) {
    if (!g.loaded) load(app);
    const Table* t = app.db.table("players");
    if (!t || !t->has("hairtypecode") || !t->has("facialhairtypecode")) return 0;
    lualoader::Faces3dStatus st;
    const bool have = static_cast<bool>(app.faces3d_report);
    if (have) st = app.faces3d_report();
    // safe only when no baked look of this game start can come back: the hook is not running, or it serves stock, or it built
    // the saved list in memory and that list is known (research file mode: the file's own list is not known here)
    const bool hook_bakes = have && st.installed && st.substitute && !st.killed;
    if (hook_bakes && !(st.in_memory && (!st.built || st.saved_known))) return 0;
    int restored = 0;
    bool changed = false;
    // Pit wall 2.17.0: LOG-ONLY unless turbo_output\faces3d_auto_restore_on.txt exists (a career that connects could be any of
    // Cassio's careers; the manual second press of "His own hair & beard" is the default way back)
    std::error_code sec;
    const bool auto_on = fs::exists(rel(app.turbo_output(), kAutoRestoreSwitch), sec);
    std::vector<int64_t> would;
    for (auto it = g.backup.begin(); it != g.backup.end();) {
        const int64_t pid = it->first;
        const faces3d::PreviewRecord& r = it->second;
        const PlayerRow* p = pid < kMgrKey ? app.model.player(pid) : nullptr;
        const auto wh = r.written.find("hairtypecode"), wb = r.written.find("facialhairtypecode");
        const bool marker = (wh != r.written.end() && wh->second == faces3d::kHairOwnCode) || (wb != r.written.end() && wb->second == faces3d::kBeardOwnCode);
        const bool listed_now = g.draft.count(pid) != 0;
        const bool baked = hook_bakes && st.built && std::find(st.saved_ids.begin(), st.saved_ids.end(), pid) != st.saved_ids.end();
        if (!p || !marker || listed_now || baked) {
            ++it;
            continue;
        }
        const std::map<std::string, int64_t> cur = read_look(app, *t, p->rec);
        bool same = true;  // this career's record: his codes are still the ones it wrote
        for (const auto& [f, v] : r.written)
            if (cur.count(f)) same = same && cur.at(f) == v;
        std::map<std::string, int64_t> w;
        for (const char* f : faces3d::kLookFields) {
            const auto c = cur.find(f), o = r.original.find(f);
            const int64_t own = std::string(f) == "hairtypecode" ? faces3d::kHairOwnCode : faces3d::kBeardOwnCode;
            if (c != cur.end() && c->second == own && o != r.original.end() && !turbo_code(f, o->second) && o->second >= 0) w[f] = o->second;
        }
        if (!same || w.empty()) {
            ++it;
            continue;
        }
        if (!auto_on) {
            would.push_back(pid);
            ++it;
            continue;
        }
        std::string err;
        if (raw_write(app, *t, p->rec, pid, false, w, err)) {
            flog(app, "3D hair & beard: player " + std::to_string(pid) + " is off the saved list since the last game start: his own codes are back "
                      "(the his-own-live marker is not needed any more)");
            ++restored;
            changed = true;
            g.session_records.erase(pid);
            it = g.backup.erase(it);
        } else {
            ++it;
        }
    }
    if (changed) {
        std::string err;
        faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
    }
    if (!would.empty()) {
        std::string ids;
        for (int64_t id : would) ids += (ids.empty() ? "" : ", ") + std::to_string(id);
        flog(app, "3D hair & beard: would put his own look back for " + std::to_string(would.size()) + " players: " + ids +
                      " (nothing written: turbo_output\\" + kAutoRestoreSwitch + " is absent; \"His own hair & beard\" puts it back)");
    }
    return restored;
}

void faces3d_reload(App& app) {
    g.session_records.clear();
    load(app);
}

void tick_faces3d_preview(App& app) {
    if (g.pending == 0) return;
    if (!app.capture) {
        g.pending = 0;
        return;
    }
    capture::Result r;
    if (app.capture->poll(r)) {
        if (r.id != g.pending) {
            flog(app, "3D hair & beard: a picture for id " + std::to_string(r.id) + " arrived while waiting for " + std::to_string(g.pending) + " (dropped)");
            return;
        }
        const int64_t pid = g.pending;
        g.pending = 0;
        if (!r.ok) {
            pv_say("The game did not draw him: " + r.error, false);
            return;
        }
        g.shown = pid;
        g.image = std::move(r.image);
        ++g.version;
        g.caption = "Drawn by the game at " + clock_now();
        pv_say("Done. The new hair and beard show here and in the game.", true);
        return;
    }
    if (ImGui::GetTime() - g.sent_at > kPreviewTimeout) {
        app.capture->cancel();
        g.pending = 0;
        pv_say("No picture after " + std::to_string(int(kPreviewTimeout)) + " s (the game may be in a match or loading). The new look is kept.", false);
    }
}

const faces3d::Entry* faces3d_draft_entry(int64_t playerid) {
    const auto it = g.draft.find(playerid);
    return it == g.draft.end() ? nullptr : &it->second;
}

// mods\legacy ... player_kit.lua present: looked at once every 2 s at most (never a file-system call every frame)
static bool legacy_kit_present(App& app) {
    static double checked = -1.0e9;
    static bool present = false;
    const double now = ImGui::GetTime();
    if (now - checked >= 2.0 || now < checked) {
        checked = now;
        std::error_code ec;
        present = fs::exists(rel(app.bridge.root(), lualoader::kLegacyKitRel), ec);
    }
    return present;
}

void faces3d_gate_section(App& app, const Table& t, const PlayerRow& p) {
    if (!g.loaded) load(app);
    ImGui::PushID("faces3d_gate");
    if (legacy_kit_present(app))
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "mods\\legacy\\data\\fifarna\\lua\\assets\\player_kit.lua exists: Live Editor serves that file, so Turbo's 3D hair "
                           "and beard stay off while it is there. Remove it to use this.");
    if (!g.backup_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Hair backup unreadable: %s", g.backup_error.c_str());
    leftover_banner(app, t, p.playerid);

    const int64_t hc = headclass_of(app, t, p.playerid);
    if (hc != 0) {
        ImGui::TextDisabled("3D hair and beard are for real faces. %s", hc < 0 ? "" : "He has a generic head: use the hair and beard fields.");
        saved_list_line(p);
        ImGui::PopID();
        return;
    }
    const std::map<std::string, int64_t> cur = read_look(app, t, p.rec);
    const Look now = look_of(cur);
    pickers(app, false, p.playerid, now);
    const bool changed = g_pick.hair != now.hair || g_pick.beard != now.beard;
    if (!changed) ImGui::BeginDisabled();
    if (ImGui::Button("Apply hair & beard")) {
        faces3d::PreviewRecord& r = record_for(p.playerid, p.name, read_codes(app, t, p.rec));
        const auto w = faces3d::look_writes(g_pick.hair, g_pick.beard, cur, r.original);
        std::string err;
        if (w.empty()) {
            pv_say("Nothing to write.", true);
        } else {
            std::map<std::string, int64_t> after = read_codes(app, t, p.rec);
            for (const auto& [f, v] : w) after[f] = v;
            r.written.clear();
            for (const auto& [f, v] : after)
                if (r.original.count(f) && r.original.at(f) != v) r.written[f] = v;
            // his originals reach the disk before the database changes (a crash leaves the way back)
            if (!faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err)) {
                pv_say("Nothing written: his own codes could not be saved first (" + err + ")", false);
            } else if (!raw_write(app, t, p.rec, p.playerid, false, w, err)) {
                pv_say("Writing failed (" + err + "): press His own hair & beard", false);
            } else {
                faces3d_forget_render(app, false, p.playerid);  // the face chooser's picture of him is the old look
                if (g_pick.hair == 0 && g_pick.beard == 0) {
                    g.backup.erase(p.playerid);
                    g.session_records.erase(p.playerid);
                    faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
                }
                lualoader::Faces3dStatus st;
                const bool live = app.faces3d_report && faces3d::live_looks(&(st = app.faces3d_report())).active;
                std::string cerr;
                if (live && request_capture(app, p, cerr)) pv_say("Applied. Asking the game for his picture...", true);
                else if (live) pv_say("Applied. It shows in the game straight away (no picture here: " + cerr + ").", true);
                else pv_say("Applied and kept in this career. It shows once 3D hair and beard are on (see below).", true);
            }
        }
    }
    if (!changed) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Kept in this career's database (saved with the career). Other careers are not changed.");
    ImGui::SameLine();
    const bool on_list = g.draft.count(p.playerid) != 0;
    auto at = [&](const char* f, int64_t d) {
        const auto c = cur.find(f);
        return c == cur.end() ? d : c->second;
    };
    const bool own_h = at("hairtypecode", 0) == faces3d::kHairOwnCode, own_b = at("facialhairtypecode", 0) == faces3d::kBeardOwnCode;
    // on the saved list: until both "his own, live" codes are written; off it: an own code left there puts his originals back
    const bool has_look = now.hair > 0 || now.beard > 0 || (on_list && (!own_h || !own_b)) || (!on_list && (own_h || own_b));
    if (!has_look) ImGui::BeginDisabled();
    if (ImGui::Button("His own hair & beard")) {
        std::string err;
        bool ok = true;
        if (on_list) record_for(p.playerid, p.name, read_codes(app, t, p.rec));  // his originals are kept for the way back
        auto it = g.backup.find(p.playerid);
        const std::map<std::string, int64_t> orig = it == g.backup.end() ? std::map<std::string, int64_t>{} : it->second.original;
        std::map<std::string, int64_t> w;
        if (on_list) {
            // on the saved list: the "his own, live" codes skip it now; the list loses him for the next game starts. His record
            // stays (written = the own codes), so off the list "His own hair & beard" puts his originals back in one click
            if (cur.count("hairtypecode") && cur.at("hairtypecode") != faces3d::kHairOwnCode) w["hairtypecode"] = faces3d::kHairOwnCode;
            if (cur.count("facialhairtypecode") && cur.at("facialhairtypecode") != faces3d::kBeardOwnCode)
                w["facialhairtypecode"] = faces3d::kBeardOwnCode;
            if (it != g.backup.end()) {
                for (const auto& [f, v] : w) it->second.written[f] = v;
                if (!faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err)) w.clear(), ok = false;
            }
        } else {
            w = faces3d::look_writes(0, 0, cur, orig);
        }
        // an earlier 2.9 preview also wrote hairstylecode 0: his own style comes back when the record is from this career
        if (it != g.backup.end() && orig.count("hairstylecode") && t.has("hairstylecode")) {
            bool mine = !it->second.written.empty();
            const std::map<std::string, int64_t> all = read_codes(app, t, p.rec);
            for (const auto& [f, v] : it->second.written) mine = mine && all.count(f) && all.at(f) == v;
            if (mine && all.count("hairstylecode") && all.at("hairstylecode") != orig.at("hairstylecode")) w["hairstylecode"] = orig.at("hairstylecode");
        }
        // the record belongs to this career when his codes are the ones it wrote; another career's record keeps its way back
        bool this_career = it != g.backup.end() && !it->second.written.empty();
        if (this_career)
            for (const auto& [f, v] : it->second.written)
                if (cur.count(f)) this_career = this_career && cur.at(f) == v;
        ok = ok && raw_write(app, t, p.rec, p.playerid, false, w, err);
        if (ok && !on_list && it != g.backup.end() && this_career) {
            g.backup.erase(it);
            g.session_records.erase(p.playerid);
            ok = faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
        }
        if (ok && on_list) {
            // the list is read when the game starts: the next start no longer bakes him (no player_kit.lua file is involved)
            faces3d::Table next = g.draft;
            next.erase(p.playerid);
            const std::string js = faces3d::players_json(next);
            std::string werr;
            const bool wrote = lualoader::write_file_atomic(rel(app.turbo_output(), faces3d::kPlayersRel).u8string(), js, &werr);
            flog(app, "3D hair & beard: took " + std::to_string(p.playerid) + " off the saved list" + (wrote ? std::string() : " (players.json: " + werr + ")"));
            if (wrote) {
                g.draft = next;
                g.applied = js;
            }
        }
        if (ok) {
            g_pick = Pick{false, p.playerid, 0, 0};
            faces3d_forget_render(app, false, p.playerid);
            lualoader::Faces3dStatus st;
            const bool live = app.faces3d_report && faces3d::live_looks(&(st = app.faces3d_report())).active;
            std::string cerr;
            if (live && request_capture(app, p, cerr)) pv_say("His own hair and beard are back. Asking the game for his picture...", true);
            else pv_say("His own hair and beard are back.", true);
        } else {
            pv_say("Not changed: " + err, false);
        }
    }
    if (!has_look) ImGui::EndDisabled();
    live_line(app);
    draw_picture(app, p.playerid);
    if (!g.pv_message.empty())
        ImGui::TextColored(g.pv_message_ok ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", g.pv_message.c_str());
    saved_list_line(p);
    ImGui::PopID();
}

void faces3d_manager_section(App& app, const Table& t, const ManagerRow& m) {
    if (!g.loaded) load(app);
    ImGui::PushID("faces3d_mgr");
    const int64_t hc = t.has("headclasscode") ? app.db.get_int(t, m.rec, "headclasscode", -1) : -1;
    if (hc != 0) {
        ImGui::TextDisabled("3D hair and beard are for real faces. %s", hc < 0 ? "" : "He has a generic head: use the hair and beard fields.");
        ImGui::PopID();
        return;
    }
    const std::map<std::string, int64_t> cur = read_look(app, t, m.rec);
    const Look now = look_of(cur);
    pickers(app, true, m.managerid, now);
    const bool changed = g_pick.hair != now.hair || g_pick.beard != now.beard;
    if (!changed) ImGui::BeginDisabled();
    if (ImGui::Button("Apply hair & beard")) {
        // his own codes for "His own hair & beard", in preview_backup.json (kMgrKey + managerid) before the database changes
        faces3d::PreviewRecord& r = g.backup[kMgrKey + m.managerid];
        r.name = m.name;
        for (const auto& [f, v] : cur)
            if (!r.original.count(f) && !turbo_code(f, v)) r.original[f] = v;
        std::string err;
        const auto w = faces3d::look_writes(g_pick.hair, g_pick.beard, cur, r.original);
        r.written = w;
        if (r.original.empty()) g.backup.erase(kMgrKey + m.managerid);
        if (!faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err)) {
            pv_say("Nothing written: his own codes could not be saved first (" + err + ")", false);
        } else if (raw_write(app, t, m.rec, m.managerid, true, w, err)) {
            faces3d_forget_render(app, true, m.managerid);
            pv_say("Applied for the manager. It shows the next time the game draws him (Turbo has no picture of managers).", true);
        } else {
            pv_say("Writing failed: " + err, false);
        }
    }
    if (!changed) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Kept in this career's database (saved with the career). Other careers are not changed.");
    ImGui::SameLine();
    const bool has_look = now.hair > 0 || now.beard > 0;
    if (!has_look) ImGui::BeginDisabled();
    if (ImGui::Button("His own hair & beard")) {
        std::string err;
        const auto it = g.backup.find(kMgrKey + m.managerid);
        if (raw_write(app, t, m.rec, m.managerid, true,
                      faces3d::look_writes(0, 0, cur, it == g.backup.end() ? std::map<std::string, int64_t>{} : it->second.original), err)) {
            if (it != g.backup.end()) {
                g.backup.erase(it);
                faces3d::save_preview_backup(app.turbo_output().u8string(), g.backup, &err);
            }
            g_pick = Pick{true, m.managerid, 0, 0};
            faces3d_forget_render(app, true, m.managerid);
            pv_say("His own hair and beard are back.", true);
        } else {
            pv_say("Not changed: " + err, false);
        }
    }
    if (!has_look) ImGui::EndDisabled();
    live_line(app);
    if (!g.pv_message.empty())
        ImGui::TextColored(g.pv_message_ok ? ImVec4(0.5f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", g.pv_message.c_str());
    ImGui::PopID();
}

}  // namespace turbo
