// FC 27 LE Turbo GUI - pictures in the Turbo window: minifaces (players and managers), the real-face picker, the tattoo
// picker, and a file browser for pictures on the PC.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <unordered_set>

#include "app.h"
#include "core/hair_catalog.h"
#include "file_picker.h"
#include "imgui.h"
#include "ui_images.h"

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
static void placeholder(float side, const char* text) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(p, ImVec2(p.x + side, p.y + side), ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, side - 4.0f);
    ImGui::Dummy(ImVec2(side, side));
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + (side - ts.x) * 0.5f, p.y + (side - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                text, nullptr, side - 4.0f);
}

static void picture_in_box(const TextureCache::Pic& pic, float side) {
    if (!pic.tex || pic.w <= 0 || pic.h <= 0) {
        placeholder(side, pic.failed ? "unreadable" : "...");
        if (pic.failed && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pic.error.c_str());
        return;
    }
    float s = side / float(std::max(pic.w, pic.h));
    ImVec2 sz(pic.w * s, pic.h * s);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));
    ImVec2 a(p.x + (side - sz.x) * 0.5f, p.y + (side - sz.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddImage(pic.tex->GetTexRef(), a, ImVec2(a.x + sz.x, a.y + sz.y));
}

bool draw_file_picture(App& app, const fs::path& f, float side) {
    TextureCache::Pic pic = app.textures.file(f, int(std::ceil(side)));
    picture_in_box(pic, side);
    return pic.tex != nullptr;
}

LegacyImages::State draw_legacy_picture(App& app, const std::string& path, float side, bool custom_first) {
    fs::path f;
    LegacyImages::State st = app.legacy.locate(path, &f, custom_first);
    switch (st) {
        case LegacyImages::State::Custom:
        case LegacyImages::State::Game: draw_file_picture(app, f, side); break;
        case LegacyImages::State::Waiting: placeholder(side, "..."); break;
        case LegacyImages::State::Missing: placeholder(side, "no picture"); break;
        default: placeholder(side, "?"); break;
    }
    return st;
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
    if (!ImGui::IsPopupOpen(id)) {
        fp.start = cur_dir;
        fp.dir = cur_dir;
        fp.places = browser_shortcuts();
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
    ed.error.clear();
    ++ed.source_version;
    ++ed.version;
}

// A finished capture the editor has not consumed yet (the service answers whichever editor polls first)
static bool g_capture_result_ready = false;
static capture::Result g_capture_result;

static void poll_capture(App& app, MinifaceEditor& ed) {
    if (!app.capture) return;
    if (!g_capture_result_ready) {
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
    ImGui::BeginChild("##chooser", ImVec2(S(420.0f), S(220.0f)), ImGuiChildFlags_Borders);
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

void miniface_editor(App& app, const MinifaceTarget& t) {
    MinifaceEditor& ed = t.manager ? g_manager_ed : g_player_ed;
    const int size = t.manager ? kStaffMinifaceSize : kPlayerMinifaceSize;
    if (ed.target != t.path) {
        std::string keep_dir = ed.browse_dir.string();
        ed = MinifaceEditor();
        ed.target = t.path;
        ed.browse_dir = keep_dir;
    }
    if (ed.browse_dir.empty()) ed.browse_dir = app.bridge.root() / "turbo_minifaces";
    g_minifaces_folder = app.bridge.root() / "turbo_minifaces";

    // a 3D-model capture that finished
    poll_capture(app, ed);

    // waiting for a game picture chosen as the source
    if (!ed.pending_legacy.empty()) {
        fs::path f;
        LegacyImages::State st = app.legacy.locate(ed.pending_legacy, &f, true);
        if (st == LegacyImages::State::Game || st == LegacyImages::State::Custom) {
            std::string lbl = ed.pending_label;
            load_source_file(ed, f, lbl);
        } else if (st == LegacyImages::State::Missing) {
            ed.error = ed.pending_label + ": the game has no such picture";
            ed.pending_legacy.clear();
        }
    }

    // ---- current miniface
    const float big = S(150.0f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Now");
    LegacyImages::State now_state = draw_legacy_picture(app, t.path, big, true);
    switch (now_state) {
        case LegacyImages::State::Custom: ImGui::TextColored(kGood, "Custom (mods\\legacy)"); break;
        case LegacyImages::State::Game: ImGui::TextDisabled("The game's own"); break;
        case LegacyImages::State::Waiting: ImGui::TextDisabled("Loading from the game..."); break;
        case LegacyImages::State::Missing: ImGui::TextDisabled("the game has no miniface"); break;
        default: break;
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, S(24.0f));

    // ---- new miniface preview
    rebuild_result(ed, size);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("New");
    if (ed.source.empty()) {
        placeholder(big, ed.pending_legacy.empty() ? "choose a picture" : "loading...");
    } else {
        TextureCache::Pic pic = app.textures.pixels(t.manager ? "mf_manager" : "mf_player", ed.built, ed.result);
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
        fs::path backup;
        if (dds.empty()) {
            app.notify("the new miniface could not be encoded", true);
        } else if (app.legacy.save_custom(t.path, dds, &err, &backup)) {
            fs::path written = app.legacy.custom_file(t.path);
            app.textures.forget(written);
            app.notify("miniface written: " + written.string() + (backup.empty() ? "" : " (previous one copied to " + backup.filename().string() + ")"));
        } else {
            app.notify("miniface: " + err, true);
        }
    }
    if (!can_save) ImGui::EndDisabled();
    bool has_custom = now_state == LegacyImages::State::Custom;
    if (!has_custom) ImGui::BeginDisabled();
    if (ImGui::Button("Remove custom miniface")) ImGui::OpenPopup("##rmminiface");
    if (!has_custom) ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("##rmminiface", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Remove it (a copy goes to turbo_output\\miniface_backups)?");
        if (ImGui::Button("Remove")) {
            fs::path old = app.legacy.custom_file(t.path);
            std::string err;
            if (app.legacy.remove_custom(t.path, &err)) {
                app.textures.forget(old);
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
        ImGui::TextColored(kWarn, "Waiting for the game's picture: %s", ed.pending_label.c_str());
        waiting_hint();
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
            std::vector<fs::path> files = picture_files(folder);
            ImGui::BeginChild("##mffiles", ImVec2(0, S(140.0f)), ImGuiChildFlags_Borders);
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
            if (ImGui::Button("Start from the current miniface")) source_from_legacy(app, ed, t.path, "current miniface", true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("The 3D model")) {
            capture_tab(app, ed, t);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (t.manager)
        ImGui::TextDisabled("manager miniface (heads_staff, 512x512): every manager with head asset %lld shows it",
                            static_cast<long long>(t.headassetid));
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
    if (ImGui::BeginTable("##tattoos", 4, ImGuiTableFlags_SizingFixedFit)) {
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
                    if (g_tattoos.empty()) ImGui::TextDisabled("The tattoo table is empty or missing.");
                    const float cell = S(100.0f);
                    ImGui::BeginChild("##tgrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders);
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
                                if (c) ImGui::SameLine();
                                ImGui::BeginGroup();
                                ImGui::PushID(static_cast<int>(id));
                                char bid[48];
                                std::snprintf(bid, sizeof(bid), "tattoo%lld", static_cast<long long>(id));
                                if (picture_cell(app, id ? tattoo_path(app, id) : std::string(), true, cell,
                                                 id ? std::to_string(id) : std::string("None"), bid, v.i == id, id == 0))
                                    picked = id;
                                ImGui::PopID();
                                ImGui::EndGroup();
                            }
                        }
                    }
                    ImGui::EndChild();
                    if (picked >= 0) {
                        if (app.edit(t, rec, *f, Value::of_int(picked)))
                            app.notify(std::string(kTattooAreas[a][1]) + " tattoo: " + (picked ? std::to_string(picked) : std::string("none")));
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
    {"gkglovetypecode", "GK gloves", {{"gkglove", "gkglove_", ""}, {nullptr, nullptr, nullptr}}, nullptr},
    {"accessorycode1", "Accessory 1", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode1"},
    {"accessorycode2", "Accessory 2", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode2"},
    {"accessorycode3", "Accessory 3", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode3"},
    {"accessorycode4", "Accessory 4", {{"accessories", "item_", "_0"}, {nullptr, nullptr, nullptr}}, "accessorycolourcode4"},
};
static const GalleryDef kManagerGalleries[] = {
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

// read again when Live Editor's lists change (size / time looked at once per frame)
static GearPictureIndex g_gear;
static fs::path g_gear_root;
static std::string g_gear_stamp;
static int g_gear_frame = -1;

const GearPictureIndex& gear_pictures(App& app) {
    const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    if (frame >= 0 && frame == g_gear_frame) return g_gear;
    g_gear_frame = frame;
    fs::path root = app.bridge.root();
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

static std::vector<int64_t> gallery_ids(App& app, const GalleryDef& g) {
    const GearPictureIndex& gx = gear_pictures(app);
    std::vector<int64_t> ids;
    for (const GallerySource& s : g.src)
        if (s.folder)
            for (int64_t id : gx.ids(s.folder, s.prefix)) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// The picture of item id; "" when the game has none. Without the list: the usual file name (asked from the game).
static std::string item_path(App& app, const GalleryDef& g, int64_t id, int64_t colour) {
    const GearPictureIndex& gx = gear_pictures(app);
    if (gx.empty())
        return std::string("data/ui/imgAssets/") + g.src[0].folder + "/" + g.src[0].prefix + std::to_string(id) + g.src[0].rest + ".dds";
    for (const GallerySource& s : g.src) {
        if (!s.folder) continue;
        std::string p = gx.find(s.folder, s.prefix, id, colour);
        if (!p.empty()) return p;
    }
    return "";
}

// Tattoo preview of id; "" when the list is at hand and the game has none
static std::string tattoo_path(App& app, int64_t id) {
    const GearPictureIndex& gx = gear_pictures(app);
    return gx.empty() ? legacy_path::tattoo_preview(id) : gx.find("tattoo", "item_", id);
}

std::vector<std::string> gallery_preload_paths(App& app) {
    std::vector<std::string> out;
    if (gear_pictures(app).empty()) return out;  // nothing listed: asking would keep the galleries waiting
    auto add = [&](const GalleryDef* b, const GalleryDef* e) {
        for (const GalleryDef* g = b; g != e; ++g)
            for (int64_t id : gallery_ids(app, *g)) out.push_back(item_path(app, *g, id, 0));
    };
    add(std::begin(kGalleries), std::end(kGalleries));
    add(std::begin(kManagerGalleries), std::end(kManagerGalleries));
    for (int64_t id : gear_pictures(app).ids("tattoo", "item_")) out.push_back(tattoo_path(app, id));
    return out;
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
    if (ImGui::BeginTable("##galleries", 4, ImGuiTableFlags_SizingFixedFit)) {
        for (size_t gi = 0; gi < ndefs; ++gi) {
            const GalleryDef& g = defs[gi];
            const Field* f = t.field(g.field);
            if (!f) continue;
            any = true;
            std::vector<int64_t> ids = gallery_ids(app, g);
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
                draw_item_picture(app, item_path(app, g, v.i, colour), thumb);
            } else {
                placeholder(thumb, "none");
            }
            ImGui::TableNextColumn();
            field_editor(app, t, rec, *f, "##gval", S(80.0f));
            ImGui::TableNextColumn();
            if (ids.empty()) ImGui::BeginDisabled();
            if (ImGui::Button("Choose...##gal")) {
                open_gal = static_cast<int>(gi);
                ImGui::OpenPopup("##gallerypick");
            }
            if (ids.empty()) {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("(no previews listed in legacy_filename_hash_list.csv)");
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
                    for (int64_t id : ids)
                        if ((!fav_only || is_favourite(app, g.field, id)) && (!is_hair || hair_matches(hf, id))) shown.push_back(id);
                    if (is_hair) ImGui::TextDisabled("%zu of %zu styles match", shown.size(), ids.size());
                    const float cell = S(100.0f);
                    ImGui::BeginChild("##ggrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders);
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
                                if (c) ImGui::SameLine();
                                ImGui::BeginGroup();
                                ImGui::PushID(static_cast<int>(id));
                                char bid[64];
                                std::snprintf(bid, sizeof(bid), "%s%lld", g.field, static_cast<long long>(id));
                                bool fav = id && is_favourite(app, g.field, id);
                                std::string cap = id ? (fav ? "* " : "") + std::to_string(id) : std::string("None");
                                if (picture_cell(app, id ? item_path(app, g, id, colour) : std::string(), true, cell, cap, bid, v.i == id, id == 0))
                                    picked = id;
                                if (id && ImGui::IsItemClicked(ImGuiMouseButton_Right)) toggle_favourite(app, g.field, id);
                                ImGui::PopID();
                                ImGui::EndGroup();
                                if (is_hair && id && ImGui::IsItemHovered())
                                    ImGui::SetTooltip("Hair %lld: %s", static_cast<long long>(id), hair::describe(hair::lookup(id)).c_str());
                            }
                        }
                    }
                    ImGui::EndChild();
                    if (picked >= 0) {
                        if (app.edit(t, rec, *f, Value::of_int(picked)))
                            app.notify(std::string(g.title) + ": " + (picked ? std::to_string(picked) : std::string("none")));
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
    if (!any) ImGui::TextDisabled(manager ? "The outfitid field does not exist in this manager table."
                                          : "None of the hair / boots / gloves / accessory fields exist in this players table.");
    if (app.legacy.waiting() > 0) waiting_hint();
}

// ---------------------------------------------------------------- Status tab section
void images_status(App& app) {
    ImGui::SeparatorText("Game images (minifaces, tattoo previews)");
    ImGui::Text("Cached: %zu pictures in %s", app.legacy.cached_files(), app.legacy.cache_dir().string().c_str());
    ImGui::Text("Waiting: %zu   Not in the game: %zu   Shown now: %zu", app.legacy.waiting(), app.legacy.missing_count(), app.textures.size());
    if (!app.legacy.lua_status().empty()) ImGui::TextDisabled("Turbo's Lua side: %s", app.legacy.lua_status().c_str());
    if (ImGui::Button("Empty the cache")) {
        std::string err;
        app.textures.clear();
        if (app.legacy.clear_cache(&err)) app.notify("Picture cache emptied; pictures load again from the game");
        else app.notify("cache: " + err, true);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(after a game update or a new mods\\legacy file)");
    waiting_hint();
}

}  // namespace turbo
