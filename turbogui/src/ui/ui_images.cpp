// FC 27 LE Turbo GUI - pictures in the Turbo window: minifaces (players and managers), the real-face picker, the tattoo
// picker, and a file browser for pictures on the PC.
#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <system_error>
#include <thread>
#include <unordered_set>

#include "app.h"
#include "core/field_labels.h"
#include "core/gear_ids.h"
#include "core/hair_catalog.h"
#include "file_picker.h"
#include "imgui.h"
#include "ui_asset_browser.h"
#include "ui_images.h"
#include "ui_visual_picker.h"

namespace turbo {

namespace fs = std::filesystem;

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static const ImVec4 kWarn(1.0f, 0.7f, 0.3f, 1.0f);
static const ImVec4 kGood(0.55f, 0.95f, 0.55f, 1.0f);

// Miniface rendered by the game from the player's 3D model (docs/re/player_capture.md): app.capture is the service the
// Windows host installs (src/win/player_capture_win.cpp); without it the tab explains why it is off.
static const char* kPlayerCaptureNoService = "not available: Turbo's game hooks are not running in this build";

// ---------------------------------------------------------------- picture boxes
// Eight dots turning around c: the picture is on its way (decoding, or asked from the game)
static void spinner(ImDrawList* dl, ImVec2 c, float r) {
    constexpr int kDots = 8;
    const int lit = int(ImGui::GetTime() * 10.0) % kDots;
    for (int i = 0; i < kDots; ++i) {
        const float a = float(i) * 6.2831853f / float(kDots);
        const int age = (lit - i + kDots) % kDots;  // 0 = the bright one, the tail fades
        dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), std::max(1.5f, r * 0.22f),
                            ImGui::GetColorU32(ImGuiCol_Text, 1.0f - 0.11f * float(age)));
    }
}

// Empty box with a word in it (and a spinner above it when busy; a small box shows the spinner only)
static void placeholder(float side, const char* text, bool busy = false) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(p, ImVec2(p.x + side, p.y + side), ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, side - 4.0f);
    ImGui::Dummy(ImVec2(side, side));
    float ty = p.y + (side - ts.y) * 0.5f;
    if (busy) {
        const float r = std::min(side * 0.16f, ImGui::GetTextLineHeight() * 0.7f);
        const float gap = r * 0.6f;
        if (2.0f * r + gap + ts.y > side - 6.0f) {
            spinner(dl, ImVec2(p.x + side * 0.5f, p.y + side * 0.5f), r);
            return;
        }
        const float top = p.y + (side - (2.0f * r + gap + ts.y)) * 0.5f;
        spinner(dl, ImVec2(p.x + side * 0.5f, top + r), r);
        ty = top + 2.0f * r + gap;
    }
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + (side - ts.x) * 0.5f, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), text, nullptr, side - 4.0f);
}

static void picture_in_box(const TextureCache::Pic& pic, float side) {
    if (!pic.tex || pic.w <= 0 || pic.h <= 0) {
        placeholder(side, pic.failed ? "unreadable" : "decoding", !pic.failed);
        if (pic.failed && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nTurbo reads it again in a moment.", pic.error.c_str());
        return;
    }
    float s = side / float(std::max(pic.w, pic.h));
    ImVec2 sz(pic.w * s, pic.h * s);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    ImVec2 a(p.x + (side - sz.x) * 0.5f, p.y + (side - sz.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddImage(pic.tex->GetTexRef(), a, ImVec2(a.x + sz.x, a.y + sz.y));
}

static TextureCache::Pic file_picture(App& app, const fs::path& f, float side) {
    TextureCache::Pic pic = app.textures.file(f, int(std::ceil(side)));
    picture_in_box(pic, side);
    if (pic.failed) {
        // once per file and reason in turbo_gui.log: a box that shows no picture leaves a trace
        static std::unordered_set<std::string> logged;
        const std::string line = "picture unreadable: " + f.string() + ": " + pic.error;
        if (logged.size() < 500 && logged.insert(line).second) {
            app.log(line);
            if (app.log_hook) app.log_hook(line);
        }
    }
    return pic;
}

bool draw_file_picture(App& app, const fs::path& f, float side) { return file_picture(app, f, side).tex != nullptr; }

// Tooltip / status words for a game picture that is still asked for
static std::string waiting_words(App& app, const PictureInfo& pi) {
    char buf[480];
    if (pi.stalled) {
        std::string lua = app.legacy.lua_status().empty() ? std::string("no status yet") : app.legacy.lua_status();
        std::snprintf(buf, sizeof(buf),
                      "No answer from the game for %.0f s. Turbo's Lua side exports the game's pictures while a career is loaded "
                      "(Lua: %s). Reload asks again; lua\\scripts\\turbo_images.lua in Live Editor's Lua Engine exports at once.",
                      std::max(0.0, pi.waiting), lua.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "Loading from the game... %.0f s", std::max(0.0, pi.waiting));
    }
    return buf;
}

LegacyImages::State draw_legacy_picture(App& app, const std::string& path, float side, bool custom_first, PictureInfo* info) {
    fs::path f;
    PictureInfo pi;
    pi.state = app.legacy.locate(path, &f, custom_first);
    switch (pi.state) {
        case LegacyImages::State::Custom:
        case LegacyImages::State::Game: {
            pi.file = f;
            TextureCache::Pic pic = file_picture(app, f, side);
            pi.drawn = pic.tex != nullptr;
            pi.failed = pic.failed;
            pi.error = pic.error;
            break;
        }
        case LegacyImages::State::Waiting:
            pi.waiting = app.legacy.waiting_for(path);
            pi.stalled = app.legacy.stalled();
            placeholder(side, pi.stalled ? "no answer" : "loading", !pi.stalled);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", path.c_str(), waiting_words(app, pi).c_str());
            break;
        case LegacyImages::State::Missing:
            placeholder(side, "no picture");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game has no %s (Turbo's Lua side found none)", path.c_str());
            break;
        default: placeholder(side, "?"); break;
    }
    if (info) *info = pi;
    return pi.state;
}

std::string picture_status_line(App& app, const PictureInfo& pi, const char* what, bool* warn) {
    bool w = false;
    std::string s;
    switch (pi.state) {
        case LegacyImages::State::Custom:
        case LegacyImages::State::Game:
            if (pi.failed) {
                w = true;
                s = "Unreadable " + std::string(pi.state == LegacyImages::State::Custom ? "custom file " : "copy of the game's file ") +
                    pi.file.filename().string() + ": " + pi.error + ". Turbo reads it again in a moment; Reload reads it now.";
            } else if (!pi.drawn) {
                s = "Decoding...";
            } else {
                s = pi.state == LegacyImages::State::Custom ? "Custom (mods\\legacy)" : "The game's own";
            }
            break;
        case LegacyImages::State::Waiting:
            w = pi.stalled;
            s = waiting_words(app, pi);
            break;
        case LegacyImages::State::Missing: s = std::string("The game has no ") + what + " (Reload asks again)"; break;
        default:
            w = true;
            s = "Invalid picture path";
            break;
    }
    if (warn) *warn = w;
    return s;
}

void reload_picture(App& app, const std::string& path, const PictureInfo& pi) {
    const fs::path custom = app.legacy.custom_file(path);
    if (!custom.empty()) app.textures.forget(custom);
    app.textures.forget(app.legacy.cache_file(path));
    if (pi.state != LegacyImages::State::Custom) app.legacy.reload(path);
}

// "" = an item the game has no picture for (GearPictureIndex): final at once, nothing asked from the game
static LegacyImages::State draw_item_picture(App& app, const std::string& path, float side) {
    if (!path.empty()) return draw_legacy_picture(app, path, side, true);
    placeholder(side, "no picture in the game");
    return LegacyImages::State::Missing;
}

static void waiting_hint() {
    ImGui::TextDisabled("Pictures still loading arrive in the background while Turbo is open (and as you play).");
}

// ---------------------------------------------------------------- file browser (pictures on the PC)
static bool is_picture_file(const fs::path& p) {
    std::string e = lower(p.extension().string());
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".dds";
}

std::vector<fs::path> picture_files(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && is_picture_file(it->path())) out.push_back(it->path());
        if (out.size() >= 2000) break;
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) { return lower(a.filename().string()) < lower(b.filename().string()); });
    return out;
}

// Folders worth a shortcut button in the browser: Desktop and Pictures (plain and under OneDrive), the Turbo minifaces folder
static fs::path g_minifaces_folder;
std::vector<std::pair<std::string, fs::path>> browser_shortcuts() {
    std::vector<std::pair<std::string, fs::path>> out;
    std::error_code ec;
    auto add = [&](const std::string& label, const fs::path& p) {
        if (!p.empty() && fs::is_directory(p, ec)) out.emplace_back(label, p);
    };
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOME");
    if (home) {
        fs::path h(home);
        add("Desktop", h / "Desktop");
        add("Pictures", h / "Pictures");
        add("OneDrive Desktop", h / "OneDrive" / "Desktop");
        add("OneDrive Pictures", h / "OneDrive" / "Pictures");
        if (const char* od = std::getenv("OneDrive")) {
            fs::path o(od);
            if (o != h / "OneDrive") {
                add("OneDrive Desktop", o / "Desktop");
                add("OneDrive Pictures", o / "Pictures");
            }
        }
        add("Downloads", h / "Downloads");
    }
    add("turbo_minifaces", g_minifaces_folder);
    return out;
}

// Modal picture browser: Turbo's in-overlay picker (file_picker.cpp; no Windows dialog, so the game stays in full
// screen). Returns true when a picture file was chosen (out). One picker per id, its last folder remembered per id.
bool file_browser_modal(const char* id, fs::path& cur_dir, fs::path& out) {
    static std::map<std::string, FilePicker> pickers;
    FilePicker& fp = pickers[id];
    if (fp.key.empty()) {
        fp.mode = PickMode::Open;
        fp.title = "Choose a picture (PNG, JPG, BMP, TGA, DDS)";
        fp.exts = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds"};
        fp.key = std::string("pictures") + id;
    }
    // 3.0.1: the shortcut folders are probed once when the picker opens, not on every frame the picker is closed (browser_shortcuts() is
    // five to seven directory probes; Looks > Appearance Editor and the crest page drew it 120 times in 120 frames, 7 file-system calls a frame)
    static std::map<std::string, bool> places_built;
    bool& built = places_built[id];
    if (!ImGui::IsPopupOpen(id)) {
        fp.start = cur_dir;
        fp.dir = cur_dir;
        built = false;
    } else if (!built) {
        fp.places = browser_shortcuts();
        built = true;
    }
    bool chosen = file_picker_modal(id, fp, out);
    if (!fp.dir.empty()) cur_dir = fp.dir;
    return chosen;
}

// ---------------------------------------------------------------- miniface editor
struct MinifaceEditor {
    std::string target;  // legacy path being edited
    Rgba source;         // fitted to at most 1024 px
    Rgba cleaned;        // source with the background removed (when asked)
    std::string source_label;
    std::string pending_legacy;  // game picture being waited for as the source
    std::string pending_label;
    bool pending_custom_first = true;  // the source is the custom file when there is one (false: the game's own)
    Framing fr;
    bool remove_bg = false;
    int tolerance = 40;
    uint64_t version = 1;   // bumps on every change of the result
    uint64_t built = 0;
    uint64_t cleaned_for = 0;
    int cleaned_tol = -1;
    uint64_t source_version = 0;
    Rgba result;
    std::string error;
    fs::path browse_dir;
    char player_search[64] = "";
    // "The 3D model" tab (app.capture)
    int capture_id = 0;            // id the game renders (playerid / manager head id), editable
    int capture_camera = 0;        // index into capture::cameras(), or capture::kCameraLearned
    bool capture_use_template = true;
    bool capture_advanced = false;
    int capture_mode = -1, capture_extra = -1;  // advanced overrides (-1 = from the camera preset)
    bool capture_pending = false;
    int capture_pending_id = 0;
    std::string capture_note;      // last outcome, shown under the button
};

static MinifaceEditor g_player_ed, g_manager_ed;

static void set_source(MinifaceEditor& ed, Rgba img, const std::string& label) {
    ed.source = fit_image(img, 1024);
    ed.source_label = label;
    ed.fr = Framing();
    ed.pending_legacy.clear();
    ed.pending_custom_first = true;
    ed.error.clear();
    ++ed.source_version;
    ++ed.version;
}

// A finished capture the editor has not consumed yet (the service answers whichever editor polls first)
static bool g_capture_result_ready = false;
static capture::Result g_capture_result;

static void poll_capture(App& app, MinifaceEditor& ed) {
    if (!app.capture) return;
    // only while this editor waits: the face chooser's 3D heads (ui_faces.cpp) poll the same service for their own requests
    if (!g_capture_result_ready && ed.capture_pending) {
        capture::Result r;
        if (app.capture->poll(r)) {
            g_capture_result = std::move(r);
            g_capture_result_ready = true;
        }
    }
    if (!g_capture_result_ready || !ed.capture_pending || g_capture_result.id != ed.capture_pending_id) return;
    capture::Result r = std::move(g_capture_result);
    g_capture_result_ready = false;
    ed.capture_pending = false;
    if (r.ok) {
        set_source(ed, std::move(r.image), "3D model capture (" + r.format + ")");
        ed.remove_bg = true;
        ed.capture_note = "rendered: " + r.format + ", " + std::to_string(r.bytes) + " bytes";
        app.notify("3D model rendered for " + r.label + " (" + r.format + ")");
    } else {
        ed.error = "3D model: " + r.error;
        ed.capture_note = "failed: " + r.error;
        app.notify("3D model capture failed: " + r.error, true);
    }
}

// The "The 3D model" tab body
static void capture_tab(App& app, MinifaceEditor& ed, const MinifaceTarget& t) {
    ImGui::TextWrapped("Ask the game to render this %s from the 3D model (like FC 26 Live Editor's Generate Miniface) and use the "
                       "picture as the new miniface. Works in the menus (career hub, squad screens), not during a match.",
                       t.manager ? "manager's head" : "player");
    capture::CaptureService* svc = app.capture.get();
    if (!svc) {
        ImGui::BeginDisabled();
        ImGui::Button("Generate from 3D model");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kPlayerCaptureNoService);
        return;
    }
    capture::Status st = svc->status();
    if (ed.capture_id == 0) ed.capture_id = static_cast<int>(t.manager ? t.headassetid : t.id);
    ImGui::SetNextItemWidth(S(110.0f));
    ImGui::InputInt("Game id", &ed.capture_id, 0, 0);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", t.manager ? "the manager's head id (heads_staff); change it to try the managerid" : "the player id; change it to render someone else into this miniface");
    ImGui::SameLine();
    const auto& cams = capture::cameras();
    std::string cam_label = ed.capture_camera == capture::kCameraLearned ? "Learned from the game" : cams[static_cast<size_t>(std::clamp(ed.capture_camera, 0, int(cams.size()) - 1))].label;
    ImGui::SetNextItemWidth(S(330.0f));
    if (ImGui::BeginCombo("Camera", cam_label.c_str())) {
        for (size_t i = 0; i < cams.size(); ++i)
            if (ImGui::Selectable(cams[i].label, ed.capture_camera == int(i))) ed.capture_camera = int(i);
        if (st.learned && ImGui::Selectable("Learned from the game", ed.capture_camera == capture::kCameraLearned)) ed.capture_camera = capture::kCameraLearned;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game's camera / size choice is not decoded yet: these are the (mode, extra) pairs the game's own screens use. Try them.");
    if (!st.learned) ImGui::BeginDisabled();
    ImGui::Checkbox("Start from the descriptor the game used itself", &ed.capture_use_template);
    if (!st.learned) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Advanced", &ed.capture_advanced);
    if (ed.capture_advanced) {
        ImGui::SetNextItemWidth(S(80.0f));
        ImGui::InputInt("mode (-1 = preset)", &ed.capture_mode, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(80.0f));
        ImGui::InputInt("extra (-1 = preset)", &ed.capture_extra, 0, 0);
    }
    const bool can = st.installed && st.available && !st.busy && !ed.capture_pending && ed.capture_id > 0;
    if (!can) ImGui::BeginDisabled();
    if (ImGui::Button("Generate from 3D model")) {
        capture::Request r;
        r.id = ed.capture_id;
        r.second_id = t.teamid > 0 ? static_cast<int32_t>(t.teamid) : -1;  // managers too: the game's builder passes the team id
        r.manager = t.manager;
        r.camera = ed.capture_camera;
        r.use_template = ed.capture_use_template;
        r.mode_override = ed.capture_advanced ? ed.capture_mode : -1;
        r.extra_override = ed.capture_advanced ? ed.capture_extra : -1;
        r.label = (t.manager ? "manager head " : "player ") + std::to_string(ed.capture_id);
        std::string err;
        if (svc->request(r, &err)) {
            ed.capture_pending = true;
            ed.capture_pending_id = ed.capture_id;
            ed.capture_note = "asked the game to render " + r.label;
            ed.error.clear();
        } else {
            ed.error = "3D model: " + err;
        }
    }
    if (!can) ImGui::EndDisabled();
    ImGui::SameLine();
    if (st.busy || ed.capture_pending) {
        ImGui::TextColored(kWarn, "%s (%.0f s)", st.reason.empty() ? "rendering..." : st.reason.c_str(), st.busy_for);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##capture")) {
            svc->cancel();
            ed.capture_pending = false;
            ed.capture_note = "cancelled";
        }
    } else {
        ImGui::TextDisabled("%s", st.reason.c_str());
    }
    if (!ed.capture_note.empty()) ImGui::TextDisabled("%s", ed.capture_note.c_str());
    if (st.installed) {
        if (st.learned)
            ImGui::TextDisabled("Descriptor learned from the game's own captures (%d seen). Rendered by Turbo: %d ok, %d failed%s%s", st.seen, st.done,
                                st.failed, st.last_format.empty() ? "" : ", last picture ", st.last_format.c_str());
        else
            ImGui::TextWrapped("The game has not captured a portrait itself yet (Turbo learns its descriptor from that): open the squad hub or a "
                               "player's bio first if a render fails. Everything is logged to turbo_output\\player_capture.log.");
    }
}

static bool load_source_file(MinifaceEditor& ed, const fs::path& f, const std::string& label) {
    Rgba img;
    std::string err;
    if (!load_image_file(f, img, &err)) {
        ed.error = f.filename().string() + ": " + err;
        return false;
    }
    set_source(ed, std::move(img), label);
    return true;
}

// Use a game picture (or the custom file) as the source; waits for it when it is still being exported
static void source_from_legacy(App& app, MinifaceEditor& ed, const std::string& path, const std::string& label, bool custom_first) {
    fs::path f;
    switch (app.legacy.locate(path, &f, custom_first)) {
        case LegacyImages::State::Custom:
        case LegacyImages::State::Game: load_source_file(ed, f, label); break;
        case LegacyImages::State::Waiting:
            ed.pending_legacy = path;
            ed.pending_label = label;
            ed.pending_custom_first = custom_first;
            ed.error.clear();
            break;
        case LegacyImages::State::Missing: ed.error = label + ": the game has no such picture"; break;
        default: ed.error = "invalid picture path"; break;
    }
}

static void rebuild_result(MinifaceEditor& ed, int size) {
    if (ed.built == ed.version || ed.source.empty()) return;
    const Rgba* src = &ed.source;
    if (ed.remove_bg) {
        if (ed.cleaned_for != ed.source_version || ed.cleaned_tol != ed.tolerance) {
            ed.cleaned = ed.source;
            remove_plain_background(ed.cleaned, ed.tolerance);
            ed.cleaned_for = ed.source_version;
            ed.cleaned_tol = ed.tolerance;
        }
        src = &ed.cleaned;
    }
    ed.result = frame_image(*src, size, ed.fr);
    ed.built = ed.version;
}

// Small searchable list of players; returns the chosen player id or 0
static int64_t player_chooser(App& app, char* search, size_t search_size, const char* id) {
    int64_t chosen = 0;
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint(id, "name or ID", search, search_size);
    std::string q = lower(search);
    bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), ::isdigit);
    ImGui::BeginChild("##chooser", ImVec2(S(420.0f), S(220.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY);
    int shown = 0;
    for (const auto& p : app.model.players()) {
        if (q.empty()) break;
        if (numeric ? std::to_string(p.playerid).find(q) != 0 : lower(p.name).find(q) == std::string::npos) continue;
        char lbl[200];
        std::snprintf(lbl, sizeof(lbl), "%s (%lld) %s##c%lld", p.name.c_str(), static_cast<long long>(p.playerid), p.club_name.c_str(),
                      static_cast<long long>(p.playerid));
        if (ImGui::Selectable(lbl)) chosen = p.playerid;
        if (++shown >= 200) {
            ImGui::TextDisabled("more... (type more of the name)");
            break;
        }
    }
    if (q.empty()) ImGui::TextDisabled("Type a name or an ID.");
    else if (shown == 0) ImGui::TextDisabled("Nobody found.");
    ImGui::EndChild();
    return chosen;
}

// ---------------------------------------------------------------- catalog picture sources (docs/asset-catalog.md)
// Game minifaces, staff faces, youth faces and legend pictures from the asset catalog, in the paged browser. One list per
// family and editor, rebuilt when the catalog, the save's DbUse or the database changes.
struct MinifaceAssetList {
    const assets::Catalog* catalog = nullptr;
    const assets::DbUse* use = nullptr;
    uint64_t model_version = ~0ull;
    std::vector<AssetBrowserItem> items;
    AssetBrowserState st;
    int64_t picked = 0;
};

static MinifaceAssetList& asset_source_list(App& app, const AssetView& view, assets::Family f, bool manager) {
    static std::map<std::pair<int, bool>, MinifaceAssetList> lists;
    MinifaceAssetList& l = lists[{static_cast<int>(f), manager}];
    if (l.catalog != view.catalog.get() || l.use != view.use.get() || l.model_version != app.model.version()) {
        l.catalog = view.catalog.get();
        l.use = view.use.get();
        l.model_version = app.model.version();
        // DB names: minifaces are keyed by playerid (or headassetid); the other families have no DB name by id
        std::function<std::string(int64_t)> db_name;
        if (f == assets::Family::Miniface)
            db_name = [&app](int64_t id) {
                const PlayerRow* p = app.model.player(id);
                return p ? p->name : std::string();
            };
        l.items = view.catalog ? asset_items_from_catalog(*view.catalog, f, view.use.get(), db_name) : std::vector<AssetBrowserItem>{};
    }
    return l;
}

// One catalog source tab: the browser; a click uses the picture as the new miniface's source
static void asset_source_tab(App& app, MinifaceEditor& ed, const AssetView& view, const AssetPictureSource& src, bool manager) {
    MinifaceAssetList& l = asset_source_list(app, view, src.family, manager);
    ImGui::TextDisabled("%s", src.note);
    if (view.catalog && view.catalog->state() == assets::State::Stale)
        ImGui::TextColored(kWarn, "Catalog built for another game build (%s): rebuild it with %s", view.catalog->status().build_key.c_str(),
                           assets::rebuild_command().c_str());
    char bid[48];
    std::snprintf(bid, sizeof(bid), "##asrc%d%d", static_cast<int>(src.family), manager ? 1 : 0);
    int64_t pick = 0;
    if (asset_browser(bid, l.st, l.items, pick, [&app](const AssetBrowserItem& it, float side) { draw_legacy_picture(app, it.path, side, false); },
                      l.picked, S(360.0f))) {
        l.picked = pick;
        auto it = std::find_if(l.items.begin(), l.items.end(), [pick](const AssetBrowserItem& x) { return x.id == pick; });
        if (it != l.items.end())
            source_from_legacy(app, ed, it->path, std::string(src.tab) + ": " + it->name + " (#" + std::to_string(it->id) + ")", false);
    }
}

void miniface_editor(App& app, const MinifaceTarget& t) {
    MinifaceEditor& ed = t.manager ? g_manager_ed : g_player_ed;
    const int size = t.manager ? kStaffMinifaceSize : kPlayerMinifaceSize;
    const char* preview_key = t.manager ? "mf_manager" : "mf_player";
    if (ed.target != t.path) {
        // a 3D render still asked for the previous player: nobody would collect it, so the service stayed busy (Generate
        // greyed out for this player, the face chooser's 3D heads stopped) until Cancel was pressed
        if (ed.capture_pending) {
            if (app.capture) app.capture->cancel();
            if (g_capture_result_ready && g_capture_result.id == ed.capture_pending_id) g_capture_result_ready = false;
        }
        std::string keep_dir = ed.browse_dir.string();
        ed = MinifaceEditor();
        ed.target = t.path;
        ed.browse_dir = keep_dir;
        // the previous player's New preview: the version numbers start again with the editor, so the same number would
        // show his picture (a 3D render or file chosen for the next player looked like it never arrived)
        app.textures.forget_pixels(preview_key);
    }
    if (ed.browse_dir.empty()) ed.browse_dir = app.bridge.root() / "turbo_minifaces";
    g_minifaces_folder = app.bridge.root() / "turbo_minifaces";

    // a 3D-model capture that finished
    poll_capture(app, ed);

    // waiting for a game picture chosen as the source
    if (!ed.pending_legacy.empty()) {
        fs::path f;
        LegacyImages::State st = app.legacy.locate(ed.pending_legacy, &f, ed.pending_custom_first);
        if (st == LegacyImages::State::Game || st == LegacyImages::State::Custom) {
            std::string lbl = ed.pending_label;
            load_source_file(ed, f, lbl);
        } else if (st == LegacyImages::State::Missing) {
            ed.error = ed.pending_label + ": the game has no such picture";
            ed.pending_legacy.clear();
        }
    }

    // ---- current miniface: the picture, what it is (or why there is none), Reload
    const float big = S(150.0f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Now");
    PictureInfo now;
    const std::string& now_path = t.shown.empty() ? t.path : t.shown;  // managers: the picture shown now
    LegacyImages::State now_state = draw_legacy_picture(app, now_path, big, true, &now);
    {
        bool warn = false;
        const std::string line = picture_status_line(app, now, "miniface", &warn);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + big);
        if (warn) ImGui::TextColored(kWarn, "%s", line.c_str());
        else if (now_state == LegacyImages::State::Custom && now.drawn) ImGui::TextColored(kGood, "%s", line.c_str());
        else ImGui::TextDisabled("%s", line.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::SmallButton("Reload##mfnow")) reload_picture(app, now_path, now);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", now_state == LegacyImages::State::Custom
                                    ? "Read the custom file again"
                                    : "Ask the game for its picture again (the cached copy is dropped and exported anew)");
    ImGui::EndGroup();
    ImGui::SameLine(0, S(24.0f));

    // ---- new miniface preview
    rebuild_result(ed, size);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("New");
    if (ed.source.empty()) {
        if (ed.pending_legacy.empty()) placeholder(big, "choose a picture");
        else placeholder(big, app.legacy.stalled() ? "no answer" : "loading", !app.legacy.stalled());
    } else {
        TextureCache::Pic pic = app.textures.pixels(preview_key, ed.built, ed.result);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        // chequerboard so transparent parts are visible
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cell = big / 8.0f;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                dl->AddRectFilled(ImVec2(p0.x + x * cell, p0.y + y * cell), ImVec2(p0.x + (x + 1) * cell, p0.y + (y + 1) * cell),
                                  ((x + y) & 1) ? IM_COL32(90, 90, 96, 255) : IM_COL32(60, 60, 66, 255));
        picture_in_box(pic, big);
        // drag to move, wheel to zoom
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("##mfdrag", ImVec2(big, big));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 0.0f)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            ed.fr.dx = std::clamp(ed.fr.dx + d.x / big, -1.5f, 1.5f);
            ed.fr.dy = std::clamp(ed.fr.dy + d.y / big, -1.5f, 1.5f);
            if (d.x != 0.0f || d.y != 0.0f) ++ed.version;
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
            ed.fr.zoom = std::clamp(ed.fr.zoom * std::pow(1.1f, ImGui::GetIO().MouseWheel), 0.2f, 6.0f);
            ++ed.version;
        }
        ImGui::TextDisabled("%s", ed.source_label.c_str());
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, S(24.0f));
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Framing");
    ImGui::SetNextItemWidth(S(180.0f));
    if (ImGui::SliderFloat("Size", &ed.fr.zoom, 0.2f, 6.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) ++ed.version;
    ImGui::SetNextItemWidth(S(180.0f));
    if (ImGui::SliderFloat("Left-right", &ed.fr.dx, -1.5f, 1.5f, "%.2f")) ++ed.version;
    ImGui::SetNextItemWidth(S(180.0f));
    if (ImGui::SliderFloat("Up-down", &ed.fr.dy, -1.5f, 1.5f, "%.2f")) ++ed.version;
    if (ImGui::Checkbox("Remove plain background", &ed.remove_bg)) ++ed.version;
    if (ed.remove_bg) {
        ImGui::SetNextItemWidth(S(180.0f));
        if (ImGui::SliderInt("Tolerance", &ed.tolerance, 5, 120)) ++ed.version;
    }
    if (ImGui::SmallButton("Reset framing")) {
        ed.fr = Framing();
        ++ed.version;
    }
    ImGui::Spacing();
    bool can_save = !ed.source.empty() && !ed.result.empty();
    if (!can_save) ImGui::BeginDisabled();
    if (ImGui::Button("Save as miniface")) {
        std::vector<uint8_t> dds = encode_dds_dxt5(ed.result);
        std::string err;
        App::UndoStep::Picture rec;
        if (dds.empty()) {
            app.notify("the new miniface could not be encoded", true);
        } else if (app.write_face_picture(t.path, dds, &err, &rec)) {  // 2.19: in a career, that career's own copy only
            fs::path written = app.legacy.custom_file(t.path);
            app.textures.forget(written);
            if (!rec.career.empty())
                app.notify("miniface written for this career only; other careers and the squad file keep theirs");
            else
                app.notify("miniface written: " + written.string() + (rec.backup.empty() ? "" : " (previous one copied to " + rec.backup.filename().string() + ")"));
        } else {
            app.notify("miniface: " + err, true);
        }
    }
    if (!can_save) ImGui::EndDisabled();
    // a custom file under the path this editor writes (a manager's "Now" can be another of his pictures)
    bool has_custom = now_state == LegacyImages::State::Custom && now_path == t.path;
    if (!has_custom) ImGui::BeginDisabled();
    if (ImGui::Button("Remove custom miniface")) ImGui::OpenPopup("##rmminiface");
    if (!has_custom) ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("##rmminiface", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Remove it (a copy goes to turbo_output\\miniface_backups)?");
        if (ImGui::Button("Remove")) {
            fs::path old = app.legacy.custom_file(t.path);
            std::string err;
            // 2.19 career faces: in a career only that career's own copy may go; a global picture stays for the other careers
            const std::string career = app.career_faces_wanted();
            const bool face = career_faces::is_face_path(t.path);
            if (face && app.career_faces_refused(&err)) {
                app.notify("miniface: " + err, true);
            } else if (face && !career.empty()) {
                fs::path undo_copy;
                if (!career_faces::has_overlay(app.career_faces_paths(), career, t.path)) {
                    app.notify("miniface: this custom picture was made outside a career and belongs to every career and the squad file; "
                               "remove it outside a career",
                               true);
                } else if (career_faces::remove_overlay(app.career_faces_paths(), career, t.path, &err, &undo_copy)) {
                    app.career_faces_sync(career, "a career's face picture removed");
                    app.textures.forget(old);
                    app.notify("This career's miniface removed; the picture the other careers see shows again");
                } else {
                    app.notify("miniface: " + err, true);
                }
            } else if (face && !career_faces::may_replace_global(app.career_faces_paths(), t.path, &err)) {
                // 2.20 faces safety (a): only Turbo's own global picture (careers\global_own.json, same hash) is ever removed
                app.log("faces safety: " + err);
                if (app.log_hook) app.log_hook("faces safety: " + err);
                app.notify("miniface: " + err, true);
            } else if (app.legacy.remove_custom(t.path, &err)) {
                if (face) career_faces::forget_global(app.career_faces_paths(), t.path, nullptr);
                app.textures.forget(old);
                // the cached "game's own" may be the custom file itself (Live Editor exports what the game loads): the
                // game's picture is exported again
                app.textures.forget(app.legacy.cache_file(t.path));
                app.legacy.reload(t.path);
                app.notify("Custom miniface removed; the game's own is used again");
            } else {
                app.notify("miniface: " + err, true);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##rmmf")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

    if (!ed.error.empty()) ImGui::TextColored(kWarn, "%s", ed.error.c_str());
    if (!ed.pending_legacy.empty()) {
        PictureInfo src;
        src.state = LegacyImages::State::Waiting;
        src.waiting = app.legacy.waiting_for(ed.pending_legacy);
        src.stalled = app.legacy.stalled();
        ImGui::TextColored(kWarn, "Waiting for the game's picture: %s", ed.pending_label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload##mfsrc")) app.legacy.reload(ed.pending_legacy);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel##mfsrc")) ed.pending_legacy.clear();
        ImGui::TextWrapped("%s", picture_status_line(app, src, "picture").c_str());
        if (!src.stalled) waiting_hint();
    }

    // ---- sources
    ImGui::SeparatorText("New miniface from");
    if (ImGui::BeginTabBar("##mfsrc")) {
        if (ImGui::BeginTabItem("An image file")) {
            fs::path folder = app.bridge.root() / "turbo_minifaces";
            ImGui::TextWrapped("Put pictures in the Turbo minifaces folder (%s) or browse to them. PNG, JPG, BMP, TGA, DDS.",
                               folder.string().c_str());
            if (ImGui::Button("Browse...")) ImGui::OpenPopup("##browser");
            fs::path chosen;
            if (file_browser_modal("##browser", ed.browse_dir, chosen)) {
                ed.browse_dir = chosen.parent_path();
                load_source_file(ed, chosen, chosen.filename().string());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Turbo minifaces folder:");
            // 2.22: the editor now sits on the Appearance Editor page too; the folder is listed once a second, not every frame
            static fs::path cached_folder;
            static std::vector<fs::path> cached_files;
            static std::chrono::steady_clock::time_point cached_at;
            const auto now_tp = std::chrono::steady_clock::now();
            if (cached_folder != folder || now_tp - cached_at > std::chrono::seconds(1)) {
                cached_files = picture_files(folder);
                cached_folder = folder;
                cached_at = now_tp;
            }
            const std::vector<fs::path>& files = cached_files;
            ImGui::BeginChild("##mffiles", ImVec2(0, S(140.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
            if (files.empty()) ImGui::TextDisabled("(empty)");
            for (const auto& f : files) {
                if (ImGui::Selectable(f.filename().string().c_str())) load_source_file(ed, f, f.filename().string());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (!t.manager && ImGui::BeginTabItem("A player's miniface")) {
            ImGui::TextDisabled("Use this player's miniface:");
            int64_t pid = player_chooser(app, ed.player_search, sizeof(ed.player_search), "##mfplayer");
            if (pid) source_from_legacy(app, ed, legacy_path::player_miniface(pid), "miniface of " + app.model.player_name(pid), true);
            ImGui::EndTabItem();
        }
        if (t.manager && ImGui::BeginTabItem("A player's miniface")) {
            ImGui::TextDisabled("Use this player's miniface (scaled up to 512 x 512):");
            int64_t pid = player_chooser(app, ed.player_search, sizeof(ed.player_search), "##mfplayer2");
            if (pid) source_from_legacy(app, ed, legacy_path::player_miniface(pid), "miniface of " + app.model.player_name(pid), true);
            ImGui::EndTabItem();
        }
        if (!t.manager && ImGui::BeginTabItem("Head model / youth face")) {
            if (t.headassetid > 0) {
                if (ImGui::Button("From his head model's miniface"))
                    source_from_legacy(app, ed, legacy_path::player_miniface(t.headassetid),
                                       "head model " + std::to_string(t.headassetid) + "'s miniface", false);
                ImGui::SameLine();
                if (ImGui::Button("From his youth face"))
                    source_from_legacy(app, ed, legacy_path::youth_face(t.headassetid), "youth face " + std::to_string(t.headassetid), true);
                ImGui::TextDisabled("Head model: the miniface of player %lld (his headassetid). Youth face:",
                                    static_cast<long long>(t.headassetid));
                ImGui::TextDisabled("data/ui/imgAssets/youthheads/p<head id>.dds (generated youth players)");
            } else {
                ImGui::TextDisabled("This player has no head model (headassetid 0).");
            }
            ImGui::EndTabItem();
        }
        if (t.manager && ImGui::BeginTabItem("The current one")) {
            if (ImGui::Button("Start from the current miniface")) source_from_legacy(app, ed, now_path, "current miniface", true);
            ImGui::EndTabItem();
        }
        // catalog sources (none when the catalog is off or not loaded: the tabs are exactly 2.0's)
        const AssetView& view = asset_view();
        for (const AssetPictureSource& src : asset_miniface_sources(t.manager, view.loaded())) {
            if (ImGui::BeginTabItem(src.tab)) {
                asset_source_tab(app, ed, view, src, t.manager);
                ImGui::EndTabItem();
            }
        }
        if (ImGui::BeginTabItem("The 3D model")) {
            capture_tab(app, ed, t);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (t.manager)
        ImGui::TextDisabled("manager miniface (512 x 512): saved as %s, his own picture (the game ships its staff pictures under "
                            "the managerid); now showing %s",
                            t.path.c_str(), now_path.c_str());
    else
        ImGui::TextDisabled("Saved to <Live Editor>\\mods\\legacy\\%s (256 x 256 DXT5). The game shows it the next time the screen is drawn.",
                            t.path.c_str());
}


// One clickable cell of a picture grid: picture, caption under it. Returns true when clicked.
bool picture_cell(App& app, const std::string& path, bool custom_first, float cell, const std::string& caption,
                  const char* id, bool selected, bool none) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    if (none) placeholder(cell, "none");
    else if (path.empty()) placeholder(cell, "no picture in the game");
    else draw_legacy_picture(app, path, cell, custom_first);
    ImGui::SetCursorScreenPos(p0);
    bool clicked = ImGui::InvisibleButton(id, ImVec2(cell, cell + line));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 clip(p0.x, p0.y, p0.x + cell, p0.y + cell + line);
    dl->AddText(nullptr, 0.0f, ImVec2(p0.x + 2.0f, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), caption.c_str(), nullptr, 0.0f, &clip);
    if (selected || ImGui::IsItemHovered())
        dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell + line), ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_ButtonHovered), 0.0f, 2.0f);
    return clicked;
}

// The real-face chooser (players and managers) is in ui_faces.cpp.

// ---------------------------------------------------------------- tattoos
static std::string tattoo_path(App& app, int64_t id);  // item galleries below
// game asset catalog helpers (item galleries below)
static const gear::FamilyView& gear_view(assets::Family f);
static const gear::IdSet& gear_used(App& app, const std::string& field);
static bool gear_cell(App& app, const gear::Item* it, const std::string& path, float cell, const std::string& caption,
                      const char* bid, bool selected, bool none, bool not_in_table = false);
static void gear_tooltip(const gear::Item* it, const std::string& head, bool not_in_table = false);
static AssetScope gallery_scope(App& app, const char* key);
static void set_gallery_scope(App& app, const char* key, AssetScope s);
static void gear_stale_banner(const gear::FamilyView& v);
static void log_hidden_write(App& app, const Table& t, uint64_t rec, const char* field, int64_t old_v, int64_t new_v,
                             const gear::FamilyView& v);
static const char* kTattooAreas[][2] = {
    {"tattoohead", "Head"},          {"tattoofront", "Front"},        {"tattooback", "Back"},
    {"tattooleftarm", "Left arm"},   {"tattoorightarm", "Right arm"}, {"tattooleftleg", "Left leg"},
    {"tattoorightleg", "Right leg"},
};

struct TattooRow {
    int64_t id = 0;
    uint32_t areas = 0;  // bit per kTattooAreas entry
};
static std::vector<TattooRow> g_tattoos;
static uint64_t g_tattoo_version = ~uint64_t(0);

static void build_tattoos(App& app) {
    if (g_tattoo_version == app.model.version()) return;
    g_tattoo_version = app.model.version();
    g_tattoos.clear();
    const Table* t = app.db.table("tattoo");
    if (!t || !t->has("tattooid")) return;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *t)) return;
    const Field* idf = t->field("tattooid");
    for (uint32_t idx : snap.valid) {
        TattooRow r;
        r.id = snap.get_int(idx, *idf);
        if (r.id <= 0) continue;
        for (size_t a = 0; a < sizeof(kTattooAreas) / sizeof(kTattooAreas[0]); ++a) {
            const Field* af = t->field(kTattooAreas[a][0]);
            if (af && snap.get_int(idx, *af) != 0) r.areas |= 1u << a;
        }
        g_tattoos.push_back(r);
    }
    std::sort(g_tattoos.begin(), g_tattoos.end(), [](const TattooRow& a, const TattooRow& b) { return a.id < b.id; });
    g_tattoos.erase(std::unique(g_tattoos.begin(), g_tattoos.end(), [](const TattooRow& a, const TattooRow& b) { return a.id == b.id; }),
                    g_tattoos.end());
}

void tattoo_editor(App& app, const Table& t, uint64_t rec) {
    static int open_area = -1;
    static bool all_areas = false;
    build_tattoos(app);
    if (!app.db.table("tattoo")) ImGui::TextDisabled("The tattoo table is empty or missing.");
    const float thumb = S(56.0f);
    if (ImGui::BeginTable("##tattoos", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
        for (size_t a = 0; a < sizeof(kTattooAreas) / sizeof(kTattooAreas[0]); ++a) {
            const Field* f = t.field(kTattooAreas[a][0]);
            if (!f) continue;
            ImGui::PushID(static_cast<int>(a));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kTattooAreas[a][1]);
            ImGui::TableNextColumn();
            Value v;
            app.db.get(t, rec, *f, v);
            if (v.i > 0) draw_item_picture(app, tattoo_path(app, v.i), thumb);
            else placeholder(thumb, "none");
            ImGui::TableNextColumn();
            field_editor(app, t, rec, *f, "##tval", S(80.0f));
            ImGui::TableNextColumn();
            if (ImGui::Button("Choose...")) {
                open_area = static_cast<int>(a);
                ImGui::OpenPopup("##tattoopick");
            }
            if (open_area == static_cast<int>(a)) {
                ImGui::SetNextWindowSize(ImVec2(S(760.0f), S(560.0f)), ImGuiCond_Appearing);
                if (ImGui::BeginPopupModal("##tattoopick", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                    ImGui::Text("%s tattoo", kTattooAreas[a][1]);
                    ImGui::SameLine();
                    ImGui::Checkbox("Show tattoos made for other areas too", &all_areas);
                    std::vector<int64_t> ids;
                    for (const auto& tr : g_tattoos)
                        if (all_areas || (tr.areas & (1u << a))) ids.push_back(tr.id);
                    // with the game asset catalog: every tattoo the game ships too (no area known: shown for every area)
                    const gear::FamilyView& tv = gear_view(assets::Family::Tattoo);
                    std::vector<gear::Item> titems;
                    std::vector<const gear::Item*> tshown;
                    if (tv.active) {
                        gear::IdSet table;
                        std::vector<int64_t> table_ids;
                        for (const auto& tr : g_tattoos) table.insert(tr.id), table_ids.push_back(tr.id);
                        titems = gear::items(tv, gear_used(app, f->name), &table, table_ids);
                        AssetScope scope = gallery_scope(app, "tattoo");
                        ImGui::SameLine();
                        if (asset_scope_combo("##tscope", scope, S(130.0f))) set_gallery_scope(app, "tattoo", scope);
                        ids.clear();
                        for (const gear::Item& it : titems) {
                            const bool area_ok = !it.known || all_areas ||
                                                 std::any_of(g_tattoos.begin(), g_tattoos.end(), [&](const TattooRow& tr) {
                                                     return tr.id == it.id && (tr.areas & (1u << a));
                                                 });
                            if (area_ok && assets::scope_shows(scope, it.hidden)) ids.push_back(it.id), tshown.push_back(&it);
                        }
                        ImGui::TextDisabled("%zu shown of %zu (the save's tattoo table + every tattoo in the game files)", ids.size(),
                                            titems.size());
                        gear_stale_banner(tv);
                    } else if (g_tattoos.empty()) {
                        ImGui::TextDisabled("The tattoo table is empty or missing.");
                    }
                    const float cell = S(100.0f);
                    ImGui::BeginChild("##tgrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
                    int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) /
                                               (cell + ImGui::GetStyle().ItemSpacing.x)));
                    size_t total = ids.size() + 1;  // first cell: none
                    int nrows = int((total + size_t(cols) - 1) / size_t(cols));
                    ImGuiListClipper clip;
                    clip.Begin(nrows, cell + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y);
                    int64_t picked = -1;
                    while (clip.Step()) {
                        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                            for (int c = 0; c < cols; ++c) {
                                size_t i = size_t(r) * size_t(cols) + size_t(c);
                                if (i >= total) break;
                                int64_t id = i == 0 ? 0 : ids[i - 1];
                                const gear::Item* gi = i == 0 || tshown.empty() ? nullptr : tshown[i - 1];
                                const bool no_row = gi && !g_tattoos.empty() && !gi->known;  // not in the save's tattoo table
                                if (c) ImGui::SameLine();
                                ImGui::BeginGroup();
                                ImGui::PushID(static_cast<int>(id));
                                char bid[48];
                                std::snprintf(bid, sizeof(bid), "tattoo%lld", static_cast<long long>(id));
                                if (gear_cell(app, gi, id ? tattoo_path(app, id) : std::string(), cell,
                                              id ? std::to_string(id) : std::string("None"), bid, v.i == id, id == 0, no_row)) {
                                    if (gi && !gi->writable) app.notify("Tattoo " + std::to_string(id) + ": " + gi->why, true);
                                    else picked = id;
                                }
                                ImGui::PopID();
                                ImGui::EndGroup();
                                if (gi && ImGui::IsItemHovered()) gear_tooltip(gi, "Tattoo " + std::to_string(id), no_row);
                            }
                        }
                    }
                    ImGui::EndChild();
                    if (picked >= 0) {
                        const int64_t before = v.i;
                        if (app.edit(t, rec, *f, Value::of_int(picked))) {
                            app.notify(std::string(kTattooAreas[a][1]) + " tattoo: " + (picked ? std::to_string(picked) : std::string("none")));
                            for (const gear::Item& it : titems)
                                if (it.id == picked && it.hidden) log_hidden_write(app, t, rec, kTattooAreas[a][0], before, picked, tv);
                        }
                        ImGui::CloseCurrentPopup();
                        open_area = -1;
                    }
                    if (ImGui::Button("Close##tattoos")) {
                        ImGui::CloseCurrentPopup();
                        open_area = -1;
                    }
                    ImGui::EndPopup();
                } else {
                    open_area = -1;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (app.legacy.waiting() > 0) waiting_hint();
}

// ---------------------------------------------------------------- item galleries (hair, boots, gloves, accessories, outfits)
// The game has preview pictures for these items (legacy files listed in <Live Editor>\legacy_filename_hash_list.csv):
//   hairtypecode        data/ui/imgAssets/craniumhair/hair_<id>_0.dds (FC 27: every id; else hairstyle/item_<id>_0.dds)
//   facialhairtypecode  data/ui/imgAssets/craniumfacialhair/Facial_hair_<id>_0.dds, else facialhairstyle/item_<id>_0.dds
//   shoetypecode        data/ui/imgAssets/shoe/shoe_<id>_0.dds (FC 27; else the older boots/item_<id>_<n...>.dds)
//   gkglovetypecode     data/ui/imgAssets/gkglove/gkglove_<id>.dds
//   accessorycode1..4   data/ui/imgAssets/accessories/item_<id>_<colour>.dds (colour = accessorycolourcodeN, else _0,
//                       else the first one listed: some accessories have no _0)
//   managers' outfitid  data/ui/imgAssets/outfit/item_<id>[_0].dds, else genericManagerOutfits/gmo_<id>_<n>.dds
// The ids and file names come from that list (GearPictureIndex), so a gallery shows exactly what the game can draw and
// an item the game has no picture for says so at once instead of waiting for the game.
// With the game asset catalog (set_gear_catalog, docs/asset-catalog.md) hair, facial hair, boots, GK gloves and tattoos
// list what the game ships instead (core/gear_ids.h): 3D models + manifest-confirmed previews with their exact paths,
// "3D only" placeholders, preview-only ids greyed and refused, content_ts-only boots and tattoos outside the save's
// tattoo table listed and refused (not checked in a match), "Hidden" tags, the All / In this save / Hidden filter, and
// hidden ids written only while the catalog is current (each write logged in turbo_output\asset_writes.log).
// Accessories 1-4: the players' codes only (referee, NPC and outfit items left out), named from the game folders, Hidden
// tags and the filter, no new write gate. Managers' outfits keep the list above.
struct GallerySource {
    const char* folder;  // under data/ui/imgAssets/ (nullptr = unused)
    const char* prefix;  // file name prefix before the id
    const char* rest;    // after the id when the list is not at hand ("_0" or "")
};
struct GalleryDef {
    const char* field;
    const char* title;
    GallerySource src[2];  // the first that lists the id wins
    const char* colour_field;
};
static const GalleryDef kGalleries[] = {
    {"hairtypecode", "Hair", {{"craniumhair", "hair_", "_0"}, {"hairstyle", "item_", "_0"}}, nullptr},
    {"facialhairtypecode", "Facial hair", {{"craniumfacialhair", "Facial_hair_", "_0"}, {"facialhairstyle", "item_", "_0"}}, nullptr},
    {"shoetypecode", "Boots", {{"shoe", "shoe_", "_0"}, {"boots", "item_", "_0"}}, nullptr},
    {"smallsidedshoetypecode", "Small-sided boots", {{"shoe", "shoe_", "_0"}, {"boots", "item_", "_0"}}, nullptr},
    {"gkglovetypecode", "GK gloves", {{"gkglove", "gkglove_", ""}, {nullptr, nullptr, nullptr}}, nullptr},
    {"accessorycode1", "Accessory 1", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode1"},
    {"accessorycode2", "Accessory 2", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode2"},
    {"accessorycode3", "Accessory 3", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode3"},
    {"accessorycode4", "Accessory 4", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode4"},
};
static const GalleryDef kManagerGalleries[] = {
    // managers carry the same hairtypecode / facialhairtypecode fields as players and the game draws them from the same hair pictures
    {"hairtypecode", "Hair", {{"craniumhair", "hair_", "_0"}, {"hairstyle", "item_", "_0"}}, nullptr},
    {"facialhairtypecode", "Facial hair", {{"craniumfacialhair", "Facial_hair_", "_0"}, {"facialhairstyle", "item_", "_0"}}, nullptr},
    {"outfitid", "Outfit", {{"outfit", "item_", "_0"}, {"genericManagerOutfits", "gmo_", "_0"}}, nullptr},
};

void GearPictureIndex::add(const std::string& path) {
    static const std::string pre = "data/ui/imgAssets/";
    if (path.size() <= pre.size() + 4 || path.compare(0, pre.size(), pre) != 0) return;
    const size_t slash = path.find('/', pre.size());
    if (slash == std::string::npos || path.find('/', slash + 1) != std::string::npos) return;
    if (lower(path.substr(path.size() - 4)) != ".dds") return;
    const std::string fn = path.substr(slash + 1, path.size() - 4 - slash - 1);
    size_t d = 0;
    while (d < fn.size() && !std::isdigit(static_cast<unsigned char>(fn[d]))) ++d;
    size_t e = d;
    while (e < fn.size() && e - d < 12 && std::isdigit(static_cast<unsigned char>(fn[e]))) ++e;
    if (d == 0 || e == d) return;  // "notfound.dds", "item__0.dds"
    // key in lower case (the list mixes "hair_" and "Hair_"), the name as listed
    const std::string name = path.substr(pre.size(), path.size() - 4 - pre.size());
    auto& names = files[lower(path.substr(pre.size(), slash - pre.size()) + "/" + fn.substr(0, d))][std::atoll(fn.substr(d, e - d).c_str())];
    if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
}

std::vector<int64_t> GearPictureIndex::ids(const std::string& folder, const std::string& prefix) const {
    std::vector<int64_t> out;
    auto it = files.find(lower(folder + "/" + prefix));
    if (it != files.end())
        for (const auto& kv : it->second) out.push_back(kv.first);
    return out;
}

std::string GearPictureIndex::find(const std::string& folder, const std::string& prefix, int64_t id, int64_t colour) const {
    auto it = files.find(lower(folder + "/" + prefix));
    if (it == files.end()) return "";
    auto jt = it->second.find(id);
    if (jt == it->second.end() || jt->second.empty()) return "";
    const size_t head = folder.size() + 1 + prefix.size() + std::to_string(id).size();  // "<folder>/<prefix><id>"
    const std::vector<std::string>& names = jt->second;
    std::string best = *std::min_element(names.begin(), names.end());
    for (const std::string& want : {"_" + std::to_string(colour), std::string("_0"), std::string()}) {
        auto nt = std::find_if(names.begin(), names.end(), [&](const std::string& n) { return n.compare(head, std::string::npos, want) == 0; });
        if (nt != names.end()) {
            best = *nt;
            break;
        }
    }
    return "data/ui/imgAssets/" + best + ".dds";
}

// read again when Live Editor's lists change: their size / time are looked at once a second at most (2.16.1: it was every frame,
// 2-3 file-system calls per frame on every page that draws a gallery, e.g. Managers > Appearance)
static GearPictureIndex g_gear;
static fs::path g_gear_root;
static std::string g_gear_stamp;
static int g_gear_frame = -1;
static double g_gear_checked = -1.0e9;  // ImGui time of the last look at the lists

const GearPictureIndex& gear_pictures(App& app) {
    const bool ctx = ImGui::GetCurrentContext() != nullptr;
    const int frame = ctx ? ImGui::GetFrameCount() : -1;
    if (frame >= 0 && frame == g_gear_frame) return g_gear;
    g_gear_frame = frame;
    fs::path root = app.bridge.root();
    const double now = ctx ? ImGui::GetTime() : -1.0;
    if (ctx && root == g_gear_root && now - g_gear_checked < 1.0 && now >= g_gear_checked) return g_gear;
    g_gear_checked = now;
    const fs::path lists[] = {root / "legacy_filename_hash_list.csv", root / "extensions" / "legacy_filename_hash_list.csv"};
    std::string stamp;
    for (const fs::path& csv : lists) {
        std::error_code ec;
        const auto sz = fs::file_size(csv, ec);
        const auto tm = ec ? fs::file_time_type() : fs::last_write_time(csv, ec);
        stamp += std::to_string(ec ? 0 : sz) + ":" + std::to_string(tm.time_since_epoch().count()) + ";";
    }
    if (g_gear_root != root || g_gear_stamp != stamp) {
        g_gear_root = root;
        g_gear_stamp = stamp;
        g_gear = GearPictureIndex();
        for (const fs::path& csv : lists) {
            std::ifstream in(csv, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                size_t semi = line.find(';');
                if (semi == std::string::npos) continue;
                std::string p = line.substr(semi + 1);
                while (!p.empty() && (p.back() == '\r' || p.back() == ' ')) p.pop_back();
                g_gear.add(p);
            }
        }
    }
    return g_gear;
}

// ---- the game asset catalog behind the galleries (nullptr: 2.0 behaviour)
static std::shared_ptr<const assets::Catalog> g_gear_cat;
static std::array<gear::FamilyView, assets::kFamilyCount> g_gear_views;
static std::array<bool, assets::kFamilyCount> g_gear_view_built{};

void set_gear_catalog(std::shared_ptr<const assets::Catalog> catalog) {
    g_gear_cat = std::move(catalog);
    g_gear_view_built.fill(false);
}

static const gear::FamilyView& gear_view(assets::Family f) {
    static const gear::FamilyView kInactive;
    const size_t i = static_cast<size_t>(f);
    if (i >= assets::kFamilyCount) return kInactive;
    if (!g_gear_view_built[i]) {
        g_gear_views[i] = gear::from_catalog(g_gear_cat.get(), f);
        g_gear_view_built[i] = true;
    }
    return g_gear_views[i];
}

// Ids rows of the save have in a players gear field (tattoos: in any of the 7 tattoo fields), per model version
static const gear::IdSet& gear_used(App& app, const std::string& field) {
    struct Entry {
        uint64_t version = ~uint64_t(0);
        int gen = -1;
        gear::IdSet ids;
    };
    static std::map<std::string, Entry> cache;
    const bool tattoo = gear::family_of_field(field) == assets::Family::Tattoo;
    Entry& e = cache[tattoo ? std::string("tattoo*") : field];
    if (e.version == app.model.version() && e.gen == app.gen) return e.ids;
    e.version = app.model.version();
    e.gen = app.gen;
    e.ids.clear();
    const Table* t = app.db.table("players");
    if (!t) return e.ids;
    auto add = [&](const char* name) {
        if (const Field* f = t->field(name))
            for (const auto& kv : values_in_use(app, *t, *f)) e.ids.insert(kv.first);
    };
    if (tattoo)
        for (const auto& area : kTattooAreas) add(area[0]);
    else
        add(field.c_str());
    return e.ids;
}

// The game's tattoo table ids (the database knows them: kept listed and writable whatever the catalog says)
static const gear::IdSet& tattoo_table_ids(App& app) {
    static gear::IdSet ids;
    static uint64_t built = ~uint64_t(0) - 1;
    build_tattoos(app);
    if (built != g_tattoo_version) {
        built = g_tattoo_version;
        ids.clear();
        for (const auto& tr : g_tattoos) ids.insert(tr.id);
    }
    return ids;
}

static AssetScope gallery_scope(App& app, const char* key) {
    const auto& all = app.gui_settings["asset_scope"];
    if (all.is_object() && all.contains(key) && all[key].is_number_integer()) {
        const int v = all[key].get<int>();
        if (v >= 0 && v <= 2) return static_cast<AssetScope>(v);
    }
    return AssetScope::All;
}
static void set_gallery_scope(App& app, const char* key, AssetScope s) {
    app.gui_settings["asset_scope"][key] = static_cast<int>(s);
    app.save_gui_settings();
}

static void gear_stale_banner(const gear::FamilyView& v) {
    if (!v.active || v.current) return;
    ImGui::TextColored(kWarn, "The game asset catalog was built for another game build (%s): items no player uses cannot be applied.",
                       v.build_key.c_str());
    ImGui::TextWrapped("Rebuild it: %s (%s)", assets::rebuild_command().c_str(), assets::rebuild_where().c_str());
}

static void log_hidden_write(App& app, const Table& t, uint64_t rec, const char* field, int64_t old_v, int64_t new_v,
                             const gear::FamilyView& v) {
    assets::WriteLogEntry e;
    e.target = "player " + std::to_string(app.db.get_int(t, rec, "playerid", 0));
    e.field = field;
    e.old_value = std::to_string(old_v);
    e.new_value = std::to_string(new_v);
    e.catalog_key = v.build_key;
    std::string err;
    if (!assets::append_write_log(app.bridge.root() / "turbo_output", e, &err) && !err.empty())
        app.notify("asset_writes.log: " + err, true);
}

// A small label in the top-left corner of the last drawn cell
static void tile_tag(const ImVec2& p, const char* text, ImU32 bg, ImU32 fg) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(p.x + 2.0f, p.y + 2.0f), ImVec2(p.x + ts.x + 8.0f, p.y + ts.y + 4.0f), bg, 3.0f);
    dl->AddText(ImVec2(p.x + 5.0f, p.y + 3.0f), fg, text);
}

// One gallery cell: picture_cell, plus the catalog's marks: a "3D only" box for a model without a preview (nothing asked
// from the game), greyed when it cannot be applied, a "Hidden" / "Preview only" / "Not in table" tag. it == nullptr:
// picture_cell as in 2.0.
static bool gear_cell(App& app, const gear::Item* it, const std::string& path, float cell, const std::string& caption,
                      const char* bid, bool selected, bool none, bool not_in_table) {
    if (!it || none) return picture_cell(app, path, true, cell, caption, bid, selected, none);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    bool clicked = false;
    if ((it->model || it->model_ts) && it->preview.empty()) {
        placeholder(cell, it->model ? "3D only" : "3D, content_ts");
        ImGui::SetCursorScreenPos(p0);
        clicked = ImGui::InvisibleButton(bid, ImVec2(cell, cell + line));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec4 clip(p0.x, p0.y, p0.x + cell, p0.y + cell + line);
        dl->AddText(nullptr, 0.0f, ImVec2(p0.x + 2.0f, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), caption.c_str(), nullptr, 0.0f, &clip);
        if (selected || ImGui::IsItemHovered())
            dl->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell + line), ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_ButtonHovered), 0.0f, 2.0f);
    } else {
        clicked = picture_cell(app, path, true, cell, caption, bid, selected, false);
    }
    if (!it->writable) ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + cell, p0.y + cell), IM_COL32(20, 20, 24, 150));
    if (it->model_ts && !it->in_save)
        tile_tag(p0, "Not checked", IM_COL32(70, 66, 50, 255), IM_COL32(200, 190, 150, 255));
    else if (!it->model && !it->preview.empty() && !it->in_save && !it->known && !it->writable)
        tile_tag(p0, "Preview only", IM_COL32(70, 66, 50, 255), IM_COL32(200, 190, 150, 255));
    else if (it->hidden)
        tile_tag(p0, not_in_table ? "Hidden, no table row" : "Hidden", IM_COL32(52, 78, 120, 255), IM_COL32(220, 232, 255, 255));
    return clicked;
}

static void gear_tooltip(const gear::Item* it, const std::string& head, bool not_in_table) {
    std::string tip = head;
    if (!it->name.empty()) tip += "\nIn the game files: " + it->name;
    if (it->model && it->preview.empty()) tip += std::string("\n") + asset_badge_tooltip(AssetBadge::ModelOnly);
    if (it->model_ts)
        tip += "\n3D model only under content_ts (the gate's boots are under content/): not checked in a match yet";
    if (it->hidden) tip += std::string("\n") + asset_badge_tooltip(AssetBadge::Hidden);
    if (not_in_table) tip += std::string("\n") + asset_badge_tooltip(AssetBadge::NotInTable);
    if (!it->writable) tip += "\nCannot be applied: " + it->why;
    ImGui::SetTooltip("%s", tip.c_str());
}

std::string gear_code_note(App& app, const Table& t, const std::string& field, int64_t id) {
    if (t.name != "players") return "";
    const gear::FamilyView& v = gear_view(gear::family_of_field(field));
    if (!v.active) return "";
    if (v.hidden_off && !gear_used(app, field).count(id)) return "";
    if (gear::family_of_field(field) == assets::Family::Tattoo && tattoo_table_ids(app).count(id)) return "";
    return gear::code_note(v, gear_used(app, field), id);
}

static std::vector<int64_t> gallery_ids(App& app, const GalleryDef& g) {
    if (const gear::FamilyView& v = gear_view(gear::family_of_field(g.field)); v.active) {
        std::vector<int64_t> ids;
        for (const gear::Item& it : gear::items(v, gear_used(app, g.field))) ids.push_back(it.id);
        return ids;
    }
    const GearPictureIndex& gx = gear_pictures(app);
    std::vector<int64_t> ids;
    for (const GallerySource& s : g.src)
        if (s.folder)
            for (int64_t id : gx.ids(s.folder, s.prefix)) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// The picture of item id from the hash list alone (no catalog); "" when the list is at hand and has none. Without the
// list: the usual file name (asked from the game).
static std::string gx_item_path(const GearPictureIndex& gx, const GalleryDef& g, int64_t id, int64_t colour) {
    if (gx.empty())
        return std::string("data/ui/imgAssets/") + g.src[0].folder + "/" + g.src[0].prefix + std::to_string(id) + g.src[0].rest + ".dds";
    for (const GallerySource& s : g.src) {
        if (!s.folder) continue;
        std::string p = gx.find(s.folder, s.prefix, id, colour);
        if (!p.empty()) return p;
    }
    return "";
}

// The picture of item id; "" when the game has none. Without the list: the usual file name (asked from the game).
static std::string item_path(App& app, const GalleryDef& g, int64_t id, int64_t colour) {
    if (const gear::FamilyView& v = gear_view(gear::family_of_field(g.field)); v.active) {
        // accessories have a picture per colour: the list's colour-aware name when it has the id, else the catalog's
        if (g.colour_field && !v.preview(id).empty()) {
            const GearPictureIndex& gx = gear_pictures(app);
            if (!gx.empty()) {
                std::string p = gx.find(g.src[0].folder, g.src[0].prefix, id, colour);
                if (!p.empty()) return p;
            }
        }
        return v.preview(id);  // "" = 3D only / not in the game
    }
    return gx_item_path(gear_pictures(app), g, id, colour);
}

// Tattoo preview of id; "" when the list is at hand and the game has none
static std::string tattoo_path(App& app, int64_t id) {
    if (const gear::FamilyView& v = gear_view(assets::Family::Tattoo); v.active) return v.preview(id);
    const GearPictureIndex& gx = gear_pictures(app);
    return gx.empty() ? legacy_path::tattoo_preview(id) : gx.find("tattoo", "item_", id);
}

std::vector<std::string> gallery_preload_paths(App& app) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    auto push = [&](const std::string& p) {
        if (!p.empty() && seen.insert(p).second) out.push_back(p);
    };
    const bool listed = !gear_pictures(app).empty();  // nothing listed: asking would keep the galleries waiting
    auto add = [&](const GalleryDef* b, const GalleryDef* e) {
        for (const GalleryDef* g = b; g != e; ++g) {
            if (const gear::FamilyView& v = gear_view(gear::family_of_field(g->field)); v.active) {
                for (const std::string& p : gear::preload_paths(v, gear_used(app, g->field))) push(p);
            } else if (listed) {
                for (int64_t id : gallery_ids(app, *g)) push(item_path(app, *g, id, 0));
            }
        }
    };
    add(std::begin(kGalleries), std::end(kGalleries));
    add(std::begin(kManagerGalleries), std::end(kManagerGalleries));
    if (const gear::FamilyView& v = gear_view(assets::Family::Tattoo); v.active) {
        for (const std::string& p : gear::preload_paths(v, gear_used(app, "tattoohead"))) push(p);
    } else if (listed) {
        for (int64_t id : gear_pictures(app).ids("tattoo", "item_")) push(tattoo_path(app, id));
    }
    return out;
}

std::vector<int64_t> gallery_codes(App& app, const std::string& field) {
    if (const gear::FamilyView& v = gear_view(gear::family_of_field(field)); v.active) {
        if (gear::family_of_field(field) != assets::Family::Tattoo) return gear::combo_codes(v, gear_used(app, field));
        const gear::IdSet& table = tattoo_table_ids(app);
        std::vector<int64_t> ids = gear::combo_codes(v, gear_used(app, field), &table);
        ids.insert(ids.end(), table.begin(), table.end());
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }
    for (const GalleryDef& g : kGalleries)
        if (field == g.field) return gallery_ids(app, g);
    for (const GalleryDef& g : kManagerGalleries)
        if (field == g.field) return gallery_ids(app, g);
    if (field.rfind("tattoo", 0) != 0) return {};
    std::vector<int64_t> ids = gear_pictures(app).ids("tattoo", "item_");
    if (!ids.empty()) ids.insert(ids.begin(), 0);  // none
    return ids;
}

static const GalleryDef* gallery_def_of(const std::string& field) {
    for (const GalleryDef& d : kGalleries)
        if (field == d.field) return &d;
    return nullptr;
}

std::string gallery_pick_path(const GearPictureIndex& gx, const std::string& field, int64_t id) {
    const GalleryDef* d = gallery_def_of(field);
    return d && id > 0 ? gx_item_path(gx, *d, id, 0) : std::string();
}

std::vector<GalleryPick> gallery_pick_list(const GearPictureIndex& gx, const std::string& field, const std::vector<int>& ids,
                                           int hair_length) {
    std::vector<GalleryPick> out;
    const bool is_hair = field == "hairtypecode";
    for (int id : ids) {
        if (id <= 0) continue;
        if (is_hair && hair_length >= 0 && hair::lookup(id).length != hair_length) continue;
        out.push_back({id, gallery_pick_path(gx, field, id)});
    }
    return out;
}

std::string gallery_picture_path(App& app, const std::string& field, int64_t id) {
    const GalleryDef* d = gallery_def_of(field);
    return d && id > 0 ? item_path(app, *d, id, 0) : std::string();
}

// A box that says the preview is missing by showing the label in it (the label is the picture)
static void text_tile(float side, const std::string& label) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(p, ImVec2(p.x + side, p.y + side), ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::Dummy(ImVec2(side, side));
    const ImVec4 clip(p.x + 2.0f, p.y + 2.0f, p.x + side - 2.0f, p.y + side - 2.0f);
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + 4.0f, p.y + 4.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str(), nullptr,
                side - 8.0f, &clip);
}

bool gallery_picture(App& app, const std::string& field, int64_t id, float side, const std::string& fallback_text) {
    if (id <= 0) {
        placeholder(side, "none");
        return false;
    }
    const std::string path = gallery_picture_path(app, field, id);
    if (path.empty()) {
        text_tile(side, fallback_text);
        return false;
    }
    fs::path f;
    const LegacyImages::State st = app.legacy.locate(path, &f, true);
    if (st == LegacyImages::State::Missing || st == LegacyImages::State::Invalid) {
        text_tile(side, fallback_text);
        return false;
    }
    draw_legacy_picture(app, path, side, true);
    return true;
}

bool gallery_picture_cell(App& app, const std::string& field, int64_t id, float side, const std::string& caption,
                          const std::string& fallback_text, const char* bid, bool selected, bool none) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    gallery_picture(app, field, none ? 0 : id, side, fallback_text);
    ImGui::SetCursorScreenPos(p0);
    const bool clicked = ImGui::InvisibleButton(bid, ImVec2(side, side + line));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 clip(p0.x, p0.y, p0.x + side, p0.y + side + line);
    dl->AddText(nullptr, 0.0f, ImVec2(p0.x + 2.0f, p0.y + side), ImGui::GetColorU32(ImGuiCol_Text), caption.c_str(), nullptr, 0.0f, &clip);
    if (selected || ImGui::IsItemHovered())
        dl->AddRect(p0, ImVec2(p0.x + side, p0.y + side + line), ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_ButtonHovered), 0.0f, 2.0f);
    return clicked;
}

// favourites per gallery: gui_settings.json favourites.<field> = [ids]
static bool is_favourite(App& app, const char* field, int64_t id) {
    const auto& fav = app.gui_settings["favourites"];
    if (!fav.is_object() || !fav.contains(field) || !fav[field].is_array()) return false;
    for (const auto& v : fav[field]) if (v.is_number_integer() && v.get<int64_t>() == id) return true;
    return false;
}
static void toggle_favourite(App& app, const char* field, int64_t id) {
    auto& arr = app.gui_settings["favourites"][field];
    if (!arr.is_array()) arr = nlohmann::json::array();
    for (auto it = arr.begin(); it != arr.end(); ++it) {
        if (it->is_number_integer() && it->get<int64_t>() == id) {
            arr.erase(it);
            app.save_gui_settings();
            return;
        }
    }
    arr.push_back(id);
    app.save_gui_settings();
}

// ---- 2.22 merged pickers: the game gallery of a field, read by ui_visual_picker.cpp (one grid per field: RDBM picture, else
// the game's preview, else a grey tile). The catalog rules (what may be written, Hidden / 3D only / Preview only tags, the
// write log) stay here, in one place.
static const GalleryDef* merged_def_of(const std::string& field, bool manager) {
    if (manager) {
        for (const GalleryDef& d : kManagerGalleries)
            if (field == d.field && field != std::string("outfitid")) return &d;
        return nullptr;
    }
    return gallery_def_of(field);
}

bool gallery_has(const std::string& field, bool manager) { return merged_def_of(field, manager) != nullptr; }

const char* gallery_title(const std::string& field, bool manager) {
    const GalleryDef* d = merged_def_of(field, manager);
    return d ? d->title : "";
}

static const gear::FamilyView& merged_view(const std::string& field, bool manager) {
    static const gear::FamilyView kInactive;
    return manager ? kInactive : gear_view(gear::family_of_field(field));
}

bool gallery_catalog_active(const std::string& field, bool manager) { return merged_view(field, manager).active; }

static GalleryEntry entry_of(App& app, const GalleryDef& g, const gear::Item& it, int64_t colour) {
    GalleryEntry e;
    e.id = it.id;
    e.preview = item_path(app, g, it.id, colour);
    e.name = it.name;
    e.model_only = (it.model || it.model_ts) && it.preview.empty();
    e.writable = it.writable;
    e.why = it.why;
    e.hidden = it.hidden;
    if (e.model_only) e.tag = it.model ? "3D only" : "3D, content_ts";
    if (it.model_ts && !it.in_save) e.tag = "Not checked";
    else if (!it.model && !it.preview.empty() && !it.in_save && !it.known && !it.writable) e.tag = "Preview only";
    else if (it.hidden) e.tag = "Hidden";
    if (it.model && it.preview.empty()) e.tip += std::string("\n") + asset_badge_tooltip(AssetBadge::ModelOnly);
    if (it.model_ts) e.tip += "\n3D model only under content_ts (the gate's boots are under content/): not checked in a match yet";
    if (it.hidden) e.tip += std::string("\n") + asset_badge_tooltip(AssetBadge::Hidden);
    if (!it.writable) e.tip += "\nCannot be applied: " + it.why;
    return e;
}

std::vector<GalleryEntry> gallery_entries(App& app, const std::string& field, bool manager, int64_t colour) {
    std::vector<GalleryEntry> out;
    const GalleryDef* g = merged_def_of(field, manager);
    if (!g) return out;
    const gear::FamilyView& v = merged_view(field, manager);
    if (v.active) {
        for (const gear::Item& it : gear::items(v, gear_used(app, field)))
            if (it.id > 0) out.push_back(entry_of(app, *g, it, colour));
        return out;
    }
    for (int64_t id : gallery_ids(app, *g)) {
        if (id <= 0) continue;
        GalleryEntry e;
        e.id = id;
        e.preview = item_path(app, *g, id, colour);
        out.push_back(std::move(e));
    }
    return out;
}

bool gallery_write_ok(App& app, const std::string& field, bool manager, int64_t id, std::string* why) {
    if (why) why->clear();
    const gear::FamilyView& v = merged_view(field, manager);
    if (!v.active) return true;
    return gear::writable(v, gear_used(app, field), id, why);
}

bool gallery_hidden(App& app, const std::string& field, bool manager, int64_t id) {
    const gear::FamilyView& v = merged_view(field, manager);
    return v.active && gear::item(v, gear_used(app, field), id).hidden;
}

void gallery_log_hidden(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t old_v, int64_t new_v) {
    log_hidden_write(app, t, rec, field.c_str(), old_v, new_v, gear_view(gear::family_of_field(field)));
}

void gallery_stale_line(const std::string& field, bool manager) { gear_stale_banner(merged_view(field, manager)); }

int gallery_scope_combo(App& app, const std::string& field, bool manager, float width) {
    if (!merged_view(field, manager).active) return -1;
    AssetScope scope = gallery_scope(app, field.c_str());
    if (asset_scope_combo("##gscope", scope, width)) set_gallery_scope(app, field.c_str(), scope);
    return static_cast<int>(scope);
}

bool gallery_scope_shows(int scope, bool hidden) {
    return scope < 0 || assets::scope_shows(static_cast<AssetScope>(scope), hidden);
}

bool gallery_favourite(App& app, const std::string& field, int64_t id) { return is_favourite(app, field.c_str(), id); }
void gallery_toggle_favourite(App& app, const std::string& field, int64_t id) { toggle_favourite(app, field.c_str(), id); }

// Hair gallery filters (hairtypecode): one combo per catalog facet (-1 = any), each option with its count among ids.
struct HairFilter {
    int length = -1, type = -1, accessory = -1;
    char search[48] = "";
};

static bool hair_combo(const char* label, int& value, int n, const char* (*name)(int), const std::vector<int64_t>& ids,
                       uint8_t hair::Style::*member, const char* any) {
    std::vector<int> counts(size_t(n), 0);
    for (int64_t id : ids) {
        int v = hair::lookup(id).*member;
        if (v >= 0 && v < n) ++counts[size_t(v)];
    }
    std::string preview = value < 0 ? std::string(any) : std::string(name(value)) + " (" + std::to_string(counts[size_t(value)]) + ")";
    bool changed = false;
    ImGui::SetNextItemWidth(S(170.0f));
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(any, value < 0)) value = -1, changed = true;
        for (int i = 0; i < n; ++i) {
            if (!counts[size_t(i)]) continue;
            std::string item = std::string(name(i)) + " (" + std::to_string(counts[size_t(i)]) + ")";
            if (ImGui::Selectable(item.c_str(), value == i)) value = i, changed = true;
        }
        ImGui::EndCombo();
    }
    return changed;
}

static bool hair_matches(const HairFilter& hf, int64_t id) {
    if (id <= 0) return true;
    hair::Style s = hair::lookup(id);
    if (hf.length >= 0 && s.length != hf.length) return false;
    if (hf.type >= 0 && s.type != hf.type) return false;
    if (hf.accessory >= 0 && s.accessory != hf.accessory) return false;
    if (hf.search[0]) {
        std::string q = lower(hf.search);
        if (std::to_string(id).find(q) == std::string::npos && lower(hair::describe(s)).find(q) == std::string::npos) return false;
    }
    return true;
}

void item_galleries(App& app, const Table& t, uint64_t rec, bool manager) {
    static HairFilter hf;
    static int open_gal = -1;
    static bool fav_only = false;
    const float thumb = S(56.0f);
    bool any = false;
    const GalleryDef* defs = manager ? kManagerGalleries : kGalleries;
    const size_t ndefs = manager ? std::size(kManagerGalleries) : std::size(kGalleries);
    if (ImGui::BeginTable("##galleries", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
        for (size_t gi = 0; gi < ndefs; ++gi) {
            const GalleryDef& g = defs[gi];
            const Field* f = t.field(g.field);
            if (!f) continue;
            any = true;
            // with the game asset catalog (players' hair, facial hair, boots, GK gloves): what the game ships, core/gear_ids.h
            const gear::FamilyView& gv = gear_view(manager ? assets::Family::Count : gear::family_of_field(g.field));
            std::vector<gear::Item> gitems;
            std::vector<int64_t> ids;
            size_t nhidden = 0;
            if (gv.active) {
                gitems = gear::items(gv, gear_used(app, g.field));
                for (const gear::Item& it : gitems) ids.push_back(it.id), nhidden += it.hidden;
            } else {
                ids = gallery_ids(app, g);
            }
            int64_t colour = g.colour_field ? app.db.get_int(t, rec, g.colour_field, 0) : 0;
            ImGui::PushID(static_cast<int>(gi));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(g.title);
            ImGui::TableNextColumn();
            Value v;
            app.db.get(t, rec, *f, v);
            if (v.i > 0) {
                // 2.22: the RDBM26 picture of the value when there is one, else the game's own preview
                if (!vp::draw_field_picture(app, t, rec, g.field, manager, thumb))
                    draw_item_picture(app, item_path(app, g, v.i, colour), thumb);
            } else {
                placeholder(thumb, "none");
            }
            ImGui::TableNextColumn();
            enum_editor(app, t, rec, *f, nullptr, S(240.0f));  // its name ("Adidas boots #16"), or the id where there is none
            ImGui::TableNextColumn();
            // 2.22: hair, beard, boots, gloves and accessories open the ONE merged grid of ui_visual_picker.cpp (RDBM26 picture, else
            // this gallery's preview, else a grey tile); the rest (the manager outfit) keeps the gallery window below
            const bool merged = gallery_has(g.field, manager);
            if (ids.empty() && !merged) ImGui::BeginDisabled();
            if (ImGui::Button("Choose...##gal")) {
                if (merged) {
                    vp::open(app, t, rec, g.field, manager);
                } else {
                    open_gal = static_cast<int>(gi);
                    ImGui::OpenPopup("##gallerypick");
                }
            }
            if (ids.empty() && !merged) {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("(no previews listed in legacy_filename_hash_list.csv)");
            } else if (ids.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("no game previews listed");
            } else if (gv.active) {
                ImGui::SameLine();
                ImGui::TextDisabled("%zu in the game, %zu hidden", ids.size(), nhidden);
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("%zu in the game", ids.size());
            }
            if (open_gal == static_cast<int>(gi)) {
                ImGui::SetNextWindowSize(ImVec2(S(760.0f), S(560.0f)), ImGuiCond_Appearing);
                if (ImGui::BeginPopupModal("##gallerypick", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                    ImGui::Text("%s (%s)", g.title, g.field);
                    ImGui::SameLine();
                    ImGui::Checkbox("Favourites only", &fav_only);
                    ImGui::SameLine();
                    ImGui::TextDisabled("right-click a picture to star it");
                    AssetScope scope = AssetScope::All;
                    if (gv.active) {
                        scope = gallery_scope(app, g.field);
                        ImGui::SameLine();
                        if (asset_scope_combo("##gscope", scope, S(130.0f))) set_gallery_scope(app, g.field, scope);
                        gear_stale_banner(gv);
                    }
                    const bool is_hair = std::strcmp(g.field, "hairtypecode") == 0;
                    if (is_hair) {
                        hair_combo("##hairlen", hf.length, hair::kLengthCount, hair::length_name, ids, &hair::Style::length, "Any length");
                        ImGui::SameLine();
                        hair_combo("##hairtype", hf.type, hair::kTypeCount, hair::type_name, ids, &hair::Style::type, "Any type");
                        ImGui::SameLine();
                        hair_combo("##hairacc", hf.accessory, hair::kAccessoryCount, hair::accessory_name, ids, &hair::Style::accessory,
                                   "Any accessories");
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(S(150.0f));
                        ImGui::InputTextWithHint("##hairsearch", "id or words", hf.search, sizeof(hf.search));
                        if (hf.length >= 0 || hf.type >= 0 || hf.accessory >= 0 || hf.search[0]) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear##hairfilters")) hf = HairFilter{};
                        }
                    }
                    std::vector<int64_t> shown;
                    std::vector<const gear::Item*> shown_items;  // parallel to shown (catalog only)
                    for (size_t k = 0; k < ids.size(); ++k) {
                        const int64_t id = ids[k];
                        if (id == 0 || (fav_only && !is_favourite(app, g.field, id)) || (is_hair && !hair_matches(hf, id))) continue;
                        if (gv.active && !assets::scope_shows(scope, gitems[k].hidden)) continue;
                        shown.push_back(id);
                        if (gv.active) shown_items.push_back(&gitems[k]);
                    }
                    if (is_hair) ImGui::TextDisabled("%zu of %zu styles match", shown.size(), ids.size());
                    const float cell = S(100.0f);
                    ImGui::BeginChild("##ggrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
                    int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) /
                                               (cell + ImGui::GetStyle().ItemSpacing.x)));
                    size_t total = shown.size() + 1;
                    int nrows = int((total + size_t(cols) - 1) / size_t(cols));
                    ImGuiListClipper clip;
                    clip.Begin(nrows, cell + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y);
                    int64_t picked = -1;
                    while (clip.Step()) {
                        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                            for (int c = 0; c < cols; ++c) {
                                size_t i = size_t(r) * size_t(cols) + size_t(c);
                                if (i >= total) break;
                                int64_t id = i == 0 ? 0 : shown[i - 1];
                                const gear::Item* gi = i == 0 || shown_items.empty() ? nullptr : shown_items[i - 1];
                                if (c) ImGui::SameLine();
                                ImGui::BeginGroup();
                                ImGui::PushID(static_cast<int>(id));
                                char bid[64];
                                std::snprintf(bid, sizeof(bid), "%s%lld", g.field, static_cast<long long>(id));
                                bool fav = id && is_favourite(app, g.field, id);
                                std::string cap = id ? (fav ? "* " : "") + std::to_string(id) : std::string("None");
                                if (is_hair && id) cap += std::string(" ") + hair::length_name(hair::lookup(id).length);
                                if (gear_cell(app, gi, id ? item_path(app, g, id, colour) : std::string(), cell, cap, bid, v.i == id, id == 0)) {
                                    if (gi && !gi->writable) app.notify(std::string(g.title) + " " + std::to_string(id) + ": " + gi->why, true);
                                    else picked = id;
                                }
                                if (id && ImGui::IsItemClicked(ImGuiMouseButton_Right)) toggle_favourite(app, g.field, id);
                                ImGui::PopID();
                                ImGui::EndGroup();
                                if (id && ImGui::IsItemHovered()) {
                                    char head[160];
                                    std::snprintf(head, sizeof(head), "%s\n%s = %lld", code_value_text(g.field, id).c_str(), g.field,
                                                  static_cast<long long>(id));
                                    if (gi) gear_tooltip(gi, head);
                                    else ImGui::SetTooltip("%s", head);
                                }
                            }
                        }
                    }
                    ImGui::EndChild();
                    if (picked >= 0) {
                        const int64_t before = v.i;
                        if (app.edit(t, rec, *f, Value::of_int(picked))) {
                            app.notify(std::string(g.title) + ": " + (picked ? std::to_string(picked) : std::string("none")));
                            for (const gear::Item& it : gitems)
                                if (it.id == picked && it.hidden) log_hidden_write(app, t, rec, g.field, before, picked, gv);
                        }
                        ImGui::CloseCurrentPopup();
                        open_gal = -1;
                    }
                    if (ImGui::Button("Close##gallery")) {
                        ImGui::CloseCurrentPopup();
                        open_gal = -1;
                    }
                    ImGui::EndPopup();
                } else {
                    open_gal = -1;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    vp::window(app, t, rec);  // 2.22: the merged grid when one of the Choose... buttons opened it (same id scope as the buttons)
    if (!any) ImGui::TextDisabled(manager ? "The outfitid field does not exist in this manager table."
                                          : "None of the hair / boots / gloves / accessory fields exist in this players table.");
    if (!manager) {
        // socks and kit sleeves: no preview pictures in the game, so named lists (Live Editor's names, field_labels.h).
        // Written by Turbo into the players table; the game's own career editor pickers for these are never used here.
        bool header = false;
        static const char* const kKit[][2] = {{"sockstylecode", "Sock style"}, {"socklengthcode", "Sock length"},
                                              {"jerseysleevelengthcode", "Sleeves"}};
        for (const auto& k : kKit) {
            const Field* f = t.field(k[0]);
            if (!f) continue;
            if (!header) ImGui::SeparatorText("Socks and sleeves"), header = true;
            enum_editor(app, t, rec, *f, k[1], S(240.0f));
        }
    }
    if (app.legacy.waiting() > 0) waiting_hint();
}

// ---------------------------------------------------------------- Status tab section
// 2.16 gui/perf: how many pictures the cache holds. Counting walks every file under it (24,675 in Cassio's cache, 2026-10-17):
// done in the frame, every frame, it froze the Settings page ("the status screen lags so bad it seems to freeze"). Counted on a
// worker thread now, at most every 10 s while the line is drawn (at once after "Empty the cache"); the line shows the last count.
namespace {
struct CacheCount {
    std::atomic<long long> n{-1};  // -1 = not counted yet
    std::atomic<bool> running{false};
};
std::shared_ptr<CacheCount> g_cache_count = std::make_shared<CacheCount>();
double g_cache_count_at = -1e9;
std::string g_cache_count_dir;
}  // namespace

static std::string cache_count_text(App& app) {
    const fs::path dir = app.legacy.cache_dir();
    const std::string key = dir.string();
    if (!g_cache_count->running.load() && (app.now - g_cache_count_at >= 10.0 || app.now < g_cache_count_at || key != g_cache_count_dir)) {
        if (key != g_cache_count_dir) g_cache_count->n = -1;
        g_cache_count_at = app.now;
        g_cache_count_dir = key;
        g_cache_count->running = true;
        std::shared_ptr<CacheCount> c = g_cache_count;  // the worker keeps its own reference (detached: never joined in DllMain)
        std::thread([c, dir]() {
            long long n = 0;
            try {
                n = static_cast<long long>(LegacyImages::count_cached_files(dir));
            } catch (...) {
                n = 0;
            }
            c->n = n;
            c->running = false;
        }).detach();
    }
    const long long n = g_cache_count->n.load();
    return n < 0 ? std::string("(counting)") : std::to_string(n);
}

void images_status(App& app) {
    ImGui::SeparatorText("Game images (minifaces, tattoo previews)");
    ImGui::Text("Cached: %s pictures in %s", cache_count_text(app).c_str(), app.legacy.cache_dir().string().c_str());
    ImGui::Text("Waiting: %zu   Not in the game: %zu   Shown now: %zu", app.legacy.waiting(), app.legacy.missing_count(), app.textures.size());
    if (!app.legacy.lua_status().empty()) ImGui::TextDisabled("Turbo's Lua side: %s", app.legacy.lua_status().c_str());
    if (app.legacy.stalled())
        ImGui::TextColored(kWarn, "%zu pictures asked on screen got no answer from Turbo's Lua side for %.0f s (is a career loaded?)",
                           app.legacy.waiting_on_screen(), LegacyImages::kStallSeconds);
    if (ImGui::Button("Empty the cache")) {
        std::string err;
        app.textures.clear();
        g_cache_count_at = -1e9;  // counted again at once
        if (app.legacy.clear_cache(&err)) app.notify("Picture cache emptied; pictures load again from the game");
        else app.notify("cache: " + err, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(after a game update or a new mods\\legacy file)");
    waiting_hint();
}

}  // namespace turbo
