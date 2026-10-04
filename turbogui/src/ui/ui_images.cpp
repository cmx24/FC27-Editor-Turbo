// FC 27 LE Turbo GUI - pictures in the Turbo window: minifaces (players and managers), the real-face picker, the tattoo
// picker, and a file browser for pictures on the PC.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <system_error>
#include <unordered_set>

#include "app.h"
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

// Track C1: miniface rendered by the game from the player's 3D model (docs/re/player_capture.md). The game-side
// hook (PlayerCaptureController request + completion listener via game_hooks.h) is not wired yet, so the button is
// shown greyed out with the reason. Flip to true once PlayerCaptureHook::available() exists.
static constexpr bool kPlayerCaptureHook = false;
static const char* kPlayerCaptureUnavailable = "not available yet: needs the game hook (track C1, docs/re/player_capture.md)";

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

static void waiting_hint() {
    ImGui::TextDisabled("Pictures still loading arrive while you play (advance the calendar, open screens).");
    ImGui::TextDisabled("All at once: hide Turbo (F8), Live Editor's Lua Engine, run lua\\scripts\\turbo_images.lua.");
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

// Modal browser; returns true when a picture file was chosen (out)
bool file_browser_modal(const char* id, fs::path& cur_dir, fs::path& out) {
    bool chosen = false;
    ImGui::SetNextWindowSize(ImVec2(S(640.0f), S(480.0f)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        static char path_buf[1024];
        static fs::path shown;
        std::error_code ec;
        if (cur_dir.empty() || !fs::is_directory(cur_dir, ec)) cur_dir = fs::current_path(ec);
        if (shown != cur_dir) {
            shown = cur_dir;
            std::snprintf(path_buf, sizeof(path_buf), "%s", cur_dir.string().c_str());
        }
        ImGui::SetNextItemWidth(-S(70.0f));
        if (ImGui::InputText("##bpath", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            fs::path typed(path_buf);
            if (fs::is_directory(typed, ec)) cur_dir = typed;
            else if (fs::is_regular_file(typed, ec) && is_picture_file(typed)) {
                out = typed;
                chosen = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Up") && cur_dir.has_parent_path() && cur_dir.parent_path() != cur_dir) cur_dir = cur_dir.parent_path();
        // shortcuts: Desktop, Pictures (also under OneDrive), the Turbo minifaces folder
        for (const auto& sc : browser_shortcuts()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(sc.first.c_str())) cur_dir = sc.second;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sc.second.string().c_str());
        }
#ifdef _WIN32
        // drive letters
        for (char d = 'C'; d <= 'Z'; ++d) {
            char root[4] = {d, ':', '\\', 0};
            if (fs::is_directory(fs::path(root), ec)) {
                ImGui::SameLine();
                char lbl[8] = {d, ':', 0};
                if (ImGui::SmallButton(lbl)) cur_dir = fs::path(root);
            }
        }
#endif
        ImGui::BeginChild("##bfiles", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
        std::vector<fs::path> dirs, files;
        for (fs::directory_iterator it(cur_dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) dirs.push_back(it->path());
            else if (it->is_regular_file(e2) && is_picture_file(it->path())) files.push_back(it->path());
            if (dirs.size() + files.size() > 5000) break;
        }
        auto by_name = [](const fs::path& a, const fs::path& b) { return lower(a.filename().string()) < lower(b.filename().string()); };
        std::sort(dirs.begin(), dirs.end(), by_name);
        std::sort(files.begin(), files.end(), by_name);
        for (const auto& d : dirs) {
            std::string lbl = "[" + d.filename().string() + "]";
            if (ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) cur_dir = d;
        }
        for (const auto& f : files) {
            if (ImGui::Selectable(f.filename().string().c_str())) {
                out = f;
                chosen = true;
            }
        }
        if (dirs.empty() && files.empty()) ImGui::TextDisabled("No folders or pictures here.");
        ImGui::EndChild();
        ImGui::TextDisabled("Double-click a folder to open it, click a picture to use it.");
        ImGui::SameLine(ImGui::GetWindowWidth() - S(90.0f));
        if (ImGui::Button("Cancel##browser")) ImGui::CloseCurrentPopup();
        if (chosen) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
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
            ImGui::TextWrapped("Ask the game to render this %s's head from the 3D model (like FC 26 Live Editor's Generate Miniface) and use "
                               "the picture as the new miniface.", t.manager ? "manager" : "player");
            if (!kPlayerCaptureHook) ImGui::BeginDisabled();
            if (ImGui::Button("Generate from 3D model")) {
                // kPlayerCaptureHook: PlayerCaptureHook::request(id, is_manager) -> picture arrives in ed.source (see docs/re/player_capture.md)
            }
            if (!kPlayerCaptureHook) {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", kPlayerCaptureUnavailable);
            }
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
static bool picture_cell(App& app, const std::string& path, bool custom_first, float cell, const std::string& caption,
                         const char* id, bool selected, bool none = false) {
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    if (none) placeholder(cell, "none");
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

// ---------------------------------------------------------------- real-face picker
struct FaceRow {
    int64_t playerid = 0;
    int64_t headassetid = 0;
    bool real = false;  // headclasscode 0 and hashighqualityhead 1 (a star head)
    std::string name;
    std::string lname;
};

static std::vector<FaceRow> g_faces;
static uint64_t g_faces_version = ~uint64_t(0);

static void build_faces(App& app) {
    if (g_faces_version == app.model.version()) return;
    g_faces_version = app.model.version();
    g_faces.clear();
    const Table* t = app.db.table("players");
    if (!t || !t->has("headclasscode") || !t->has("headassetid")) return;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *t)) return;
    const Field* hc = t->field("headclasscode");
    const Field* ha = t->field("headassetid");
    const Field* pid = t->field("playerid");
    const Field* hq = t->field("hashighqualityhead");
    if (!hc || !ha || !pid) return;
    for (uint32_t idx : snap.valid) {
        FaceRow r;
        r.playerid = snap.get_int(idx, *pid);
        r.headassetid = snap.get_int(idx, *ha);
        if (r.headassetid <= 0) continue;
        r.real = snap.get_int(idx, *hc) == 0 && (!hq || snap.get_int(idx, *hq) != 0);
        if (!r.real && snap.get_int(idx, *hc) != 0 && r.headassetid != r.playerid) continue;  // generic head: nothing to pick
        r.name = app.model.player_name(r.playerid);
        r.lname = lower(r.name);
        g_faces.push_back(std::move(r));
    }
    std::sort(g_faces.begin(), g_faces.end(), [](const FaceRow& a, const FaceRow& b) { return a.name < b.name; });
}

size_t real_face_count(App& app) {
    build_faces(app);
    size_t n = 0;
    for (const auto& f : g_faces) n += f.real ? 1 : 0;
    return n;
}

// Fields copied from the chosen head model's player (only those FC 27's players table has)
static const char* kHeadFields[] = {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation"};
static const char* kHairFields[] = {"hairtypecode", "haircolorcode", "hairstylecode"};
static const char* kBeardFields[] = {"facialhairtypecode", "facialhaircolorcode"};
static const char* kEyeFields[] = {"eyecolorcode", "eyebrowcode", "eyedetail"};
static const char* kSkinFields[] = {"skintonecode", "skintypecode", "skincomplexion", "skinsurfacepack", "skinmakeup"};

bool apply_real_face(App& app, int64_t target_pid, int64_t owner_pid, const RealFaceOptions& o, std::string* msg) {
    const Table* t = app.db.table("players");
    const PlayerRow* target = app.model.player(target_pid);
    const PlayerRow* owner = app.model.player(owner_pid);
    if (!t || !target || !owner) {
        if (msg) *msg = "player not found";
        return false;
    }
    std::vector<const char*> fields(std::begin(kHeadFields), std::end(kHeadFields));
    if (o.hair) fields.insert(fields.end(), std::begin(kHairFields), std::end(kHairFields));
    if (o.beard) fields.insert(fields.end(), std::begin(kBeardFields), std::end(kBeardFields));
    if (o.eyes) fields.insert(fields.end(), std::begin(kEyeFields), std::end(kEyeFields));
    if (o.skin) fields.insert(fields.end(), std::begin(kSkinFields), std::end(kSkinFields));
    // read everything first, then write (validated per field)
    std::vector<std::pair<const Field*, Value>> writes;
    for (const char* n : fields) {
        const Field* f = t->field(n);
        if (!f) continue;
        Value v;
        if (!app.db.get(*t, owner->rec, *f, v)) {
            if (msg) *msg = std::string("cannot read ") + n;
            return false;
        }
        std::string verr = Database::validate(*f, v);
        if (!verr.empty()) {
            if (msg) *msg = verr;
            return false;
        }
        writes.emplace_back(f, v);
    }
    for (const auto& w : writes) {
        if (!app.edit(*t, target->rec, *w.first, w.second)) {
            if (msg) *msg = "writing " + w.first->name + " failed";
            return false;
        }
    }
    std::string note = "head of " + owner->name + " given to " + target->name;
    if (o.miniface) {
        fs::path f;
        LegacyImages::State st = app.legacy.locate(legacy_path::player_miniface(owner_pid), &f, true);
        if (st == LegacyImages::State::Custom || st == LegacyImages::State::Game) {
            std::ifstream in(f, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::string err;
            if (!bytes.empty() && app.legacy.save_custom(legacy_path::player_miniface(target_pid), bytes, &err)) {
                app.textures.forget(app.legacy.custom_file(legacy_path::player_miniface(target_pid)));
                note += "; his miniface is copied too";
            } else {
                note += "; miniface not copied (" + (err.empty() ? std::string("empty file") : err) + ")";
            }
        } else {
            note += "; miniface not copied (the game's picture is not loaded yet)";
        }
    }
    if (msg) *msg = note;
    return true;
}

void real_face_picker(App& app, int64_t target_pid) {
    static char search[64] = "";
    static RealFaceOptions opts;
    ImGui::SetNextWindowSize(ImVec2(S(820.0f), S(600.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose a real face", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    build_faces(app);
    static bool real_only = true;
    ImGui::SetNextItemWidth(S(220.0f));
    ImGui::InputTextWithHint("##facesearch", "name or ID (at least 2 letters)", search, sizeof(search));
    ImGui::SameLine();
    ImGui::Checkbox("Real faces only (head class 0)", &real_only);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only players with headclasscode 0 (a real face head model) and hashighqualityhead 1");
    ImGui::SameLine();
    ImGui::Checkbox("Hair", &opts.hair);
    ImGui::SameLine();
    ImGui::Checkbox("Beard", &opts.beard);
    ImGui::SameLine();
    ImGui::Checkbox("Eyes", &opts.eyes);
    ImGui::SameLine();
    ImGui::Checkbox("Skin", &opts.skin);
    ImGui::SameLine();
    ImGui::Checkbox("His miniface too", &opts.miniface);
    std::string q = lower(search);
    bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), ::isdigit);
    std::vector<const FaceRow*> rows;
    for (const auto& f : g_faces) {
        if (real_only && !f.real) continue;
        if (!q.empty()) {
            if (numeric ? (std::to_string(f.playerid).find(q) != 0 && std::to_string(f.headassetid).find(q) != 0)
                        : f.lname.find(q) == std::string::npos)
                continue;
        }
        rows.push_back(&f);
    }
    if (!q.empty() && !numeric && q.size() < 2) ImGui::TextDisabled("Type at least 2 letters");
    ImGui::TextDisabled("%zu heads (head models of %s, with their minifaces)", rows.size(), real_only ? "real-face players" : "every player with a head model");
    const float cell = S(96.0f);
    ImGui::BeginChild("##facegrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
    int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / (cell + ImGui::GetStyle().ItemSpacing.x)));
    int nrows = int((rows.size() + size_t(cols) - 1) / size_t(cols));
    ImGuiListClipper clip;
    clip.Begin(nrows, cell + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y);
    int64_t picked = 0;
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                size_t i = size_t(r) * size_t(cols) + size_t(c);
                if (i >= rows.size()) break;
                const FaceRow* f = rows[i];
                if (c) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(static_cast<int>(f->playerid));
                char bid[48];
                std::snprintf(bid, sizeof(bid), "face%lld", static_cast<long long>(f->playerid));
                if (picture_cell(app, legacy_path::player_miniface(f->playerid), false, cell, f->name, bid, false)) picked = f->playerid;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nplayer %lld, head %lld", f->name.c_str(),
                                                              static_cast<long long>(f->playerid), static_cast<long long>(f->headassetid));
                ImGui::PopID();
                ImGui::EndGroup();
            }
        }
    }
    ImGui::EndChild();
    if (app.legacy.waiting() > 0) ImGui::TextDisabled("%zu pictures loading (lua\\scripts\\turbo_images.lua loads them at once)", app.legacy.waiting());
    if (picked) {
        std::string msg;
        bool ok = apply_real_face(app, target_pid, picked, opts, &msg);
        app.notify(msg, !ok);
        if (ok) ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Close##faces")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- tattoos
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
            if (v.i > 0) draw_legacy_picture(app, legacy_path::tattoo_preview(v.i), thumb, true);
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
                                if (picture_cell(app, id ? legacy_path::tattoo_preview(id) : std::string(), true, cell,
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

// ---------------------------------------------------------------- item galleries (hair, boots, gloves, accessories)
// The game has preview pictures for these items (legacy files listed in <Live Editor>\legacy_filename_hash_list.csv):
//   hairtypecode        data/ui/imgAssets/hairstyle/item_<id>_0.dds
//   facialhairtypecode  data/ui/imgAssets/facialhairstyle/item_<id>_0.dds
//   shoetypecode        data/ui/imgAssets/boots/item_<id>_0.dds
//   gkglovetypecode     data/ui/imgAssets/gkglove/gkglove_<id>.dds
//   accessorycode1..4   data/ui/imgAssets/accessories/item_<id>_<colour>.dds (colour = accessorycolourcodeN, else 0)
// The ids come from that list, so the gallery shows exactly what the game can draw.
struct GalleryDef {
    const char* field;
    const char* title;
    const char* folder;   // under data/ui/imgAssets/
    const char* prefix;   // file name prefix before the id
    bool variant;         // _<n> after the id
    const char* colour_field;
};
static const GalleryDef kGalleries[] = {
    {"hairtypecode", "Hair", "hairstyle", "item_", true, nullptr},
    {"facialhairtypecode", "Facial hair", "facialhairstyle", "item_", true, nullptr},
    {"shoetypecode", "Boots", "boots", "item_", true, nullptr},
    {"gkglovetypecode", "GK gloves", "gkglove", "gkglove_", false, nullptr},
    {"accessorycode1", "Accessory 1", "accessories", "item_", true, "accessorycolourcode1"},
    {"accessorycode2", "Accessory 2", "accessories", "item_", true, "accessorycolourcode2"},
    {"accessorycode3", "Accessory 3", "accessories", "item_", true, "accessorycolourcode3"},
    {"accessorycode4", "Accessory 4", "accessories", "item_", true, "accessorycolourcode4"},
};

static std::string item_path(const GalleryDef& g, int64_t id, int64_t colour) {
    char buf[200];
    if (g.variant) std::snprintf(buf, sizeof(buf), "data/ui/imgAssets/%s/%s%lld_%lld.dds", g.folder, g.prefix, static_cast<long long>(id), static_cast<long long>(colour));
    else std::snprintf(buf, sizeof(buf), "data/ui/imgAssets/%s/%s%lld.dds", g.folder, g.prefix, static_cast<long long>(id));
    return buf;
}

// ids per folder from the hash list (read once per Live Editor folder)
static std::map<std::string, std::vector<int64_t>> g_gallery_ids;
static std::map<std::string, std::unordered_set<int64_t>> g_gallery_variants;  // "<folder>/<id>_<variant>"
static fs::path g_gallery_root;
static bool g_gallery_loaded = false;

std::vector<int64_t> gallery_ids(App& app, const std::string& folder) {
    fs::path root = app.bridge.root();
    if (!g_gallery_loaded || g_gallery_root != root) {
        g_gallery_loaded = true;
        g_gallery_root = root;
        g_gallery_ids.clear();
        g_gallery_variants.clear();
        std::map<std::string, std::unordered_set<int64_t>> seen;
        for (const fs::path& csv : {root / "legacy_filename_hash_list.csv", root / "extensions" / "legacy_filename_hash_list.csv"}) {
            std::ifstream in(csv, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                size_t semi = line.find(';');
                if (semi == std::string::npos) continue;
                std::string p = line.substr(semi + 1);
                while (!p.empty() && (p.back() == '\r' || p.back() == ' ')) p.pop_back();
                const std::string pre = "data/ui/imgAssets/";
                if (p.rfind(pre, 0) != 0) continue;
                size_t slash = p.find('/', pre.size());
                if (slash == std::string::npos) continue;
                std::string fld = p.substr(pre.size(), slash - pre.size());
                for (const GalleryDef& g : kGalleries) {
                    if (fld != g.folder) continue;
                    std::string fn = p.substr(slash + 1);
                    std::string pfx = g.prefix;
                    if (fn.rfind(pfx, 0) != 0 || fn.size() < pfx.size() + 5 || fn.substr(fn.size() - 4) != ".dds") continue;
                    std::string mid = fn.substr(pfx.size(), fn.size() - pfx.size() - 4);
                    int64_t id = 0, var = 0;
                    size_t us = mid.find('_');
                    if (g.variant) {
                        if (us == std::string::npos) continue;
                        id = std::atoll(mid.substr(0, us).c_str());
                        var = std::atoll(mid.substr(us + 1).c_str());
                    } else {
                        if (us != std::string::npos) continue;
                        id = std::atoll(mid.c_str());
                    }
                    if (id < 0) continue;
                    if (seen[fld].insert(id).second) g_gallery_ids[fld].push_back(id);
                    g_gallery_variants[fld].insert(id * 1000 + var);
                    break;
                }
            }
        }
        for (auto& kv : g_gallery_ids) std::sort(kv.second.begin(), kv.second.end());
    }
    auto it = g_gallery_ids.find(folder);
    return it == g_gallery_ids.end() ? std::vector<int64_t>() : it->second;
}

static bool has_variant(const std::string& folder, int64_t id, int64_t var) {
    auto it = g_gallery_variants.find(folder);
    return it != g_gallery_variants.end() && it->second.count(id * 1000 + var) > 0;
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

void item_galleries(App& app, const Table& t, uint64_t rec) {
    static int open_gal = -1;
    static bool fav_only = false;
    const float thumb = S(56.0f);
    bool any = false;
    if (ImGui::BeginTable("##galleries", 4, ImGuiTableFlags_SizingFixedFit)) {
        for (size_t gi = 0; gi < sizeof(kGalleries) / sizeof(kGalleries[0]); ++gi) {
            const GalleryDef& g = kGalleries[gi];
            const Field* f = t.field(g.field);
            if (!f) continue;
            any = true;
            std::vector<int64_t> ids = gallery_ids(app, g.folder);
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
                int64_t var = has_variant(g.folder, v.i, colour) ? colour : 0;
                draw_legacy_picture(app, item_path(g, v.i, var), thumb, true);
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
                    std::vector<int64_t> shown;
                    for (int64_t id : ids)
                        if (!fav_only || is_favourite(app, g.field, id)) shown.push_back(id);
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
                                int64_t var = has_variant(g.folder, id, colour) ? colour : 0;
                                std::string cap = id ? (fav ? "* " : "") + std::to_string(id) : std::string("None");
                                if (picture_cell(app, id ? item_path(g, id, var) : std::string(), true, cell, cap, bid, v.i == id, id == 0))
                                    picked = id;
                                if (id && ImGui::IsItemClicked(ImGuiMouseButton_Right)) toggle_favourite(app, g.field, id);
                                ImGui::PopID();
                                ImGui::EndGroup();
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
    if (!any) ImGui::TextDisabled("None of the hair / boots / gloves / accessory fields exist in this players table.");
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
