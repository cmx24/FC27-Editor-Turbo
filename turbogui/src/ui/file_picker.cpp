// FC 27 LE Turbo GUI - file and folder picker drawn inside the overlay (see file_picker.h for why there is no Windows
// file dialog). Open a file, save a file (name box, overwrite confirmation) or choose a folder; shortcut buttons, drive
// letters, a new-folder box, and the last folder per purpose remembered in turbo_output\gui_folders.json.
#include "file_picker.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

#include "app.h"
#include "imgui.h"
#include "nlohmann/json.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

static std::string lower_s(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string path_text(const fs::path& p) {
    try {
        auto u = p.u8string();
        return std::string(u.begin(), u.end());
    } catch (...) {
        return std::string();
    }
}

fs::path text_path(const std::string& s) {
    try {
        return fs::u8path(s);
    } catch (...) {
        return fs::path(s);
    }
}

std::string safe_file_name(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 32 || std::strchr("<>:\"/\\|?*", c)) out += '_';
        else out += static_cast<char>(c);
    }
    size_t b = out.find_first_not_of(' ');
    if (b == std::string::npos) return std::string();
    out.erase(0, b);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.size() > 120) out.resize(120);
    return out;
}

std::string preset_safe_name(const std::string& s) {
    std::string out;
    bool run = false;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            out += static_cast<char>(c);
            run = false;
        } else if (!run) {
            out += '_';
            run = true;
        }
    }
    size_t b = out.find_first_not_of('_');
    out = b == std::string::npos ? std::string() : out.substr(b);
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "player";
    if (out.size() > 60) out.resize(60);
    return out;
}

bool has_ext(const fs::path& p, const std::vector<std::string>& exts) {
    if (exts.empty()) return true;
    std::string e = lower_s(path_text(p.extension()));  // u8: string() throws on a name outside the code page
    return std::find(exts.begin(), exts.end(), e) != exts.end();
}

#ifdef _WIN32
// No "There is no disk in the drive" box (a window of its own) while a drive is listed
struct NoDriveErrors {
    DWORD old = 0;
    NoDriveErrors() { SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old); }
    ~NoDriveErrors() { SetThreadErrorMode(old, nullptr); }
};
#else
struct NoDriveErrors {};
#endif

void list_folder(const fs::path& dir, const std::vector<std::string>& exts, std::vector<fs::path>& dirs, std::vector<fs::path>& files) {
    NoDriveErrors guard;
    dirs.clear();
    files.clear();
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) dirs.push_back(it->path());
        else if (it->is_regular_file(e2) && has_ext(it->path(), exts)) files.push_back(it->path());
        if (dirs.size() + files.size() >= 5000) break;
    }
    auto by_name = [](const fs::path& a, const fs::path& b) { return lower_s(path_text(a.filename())) < lower_s(path_text(b.filename())); };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
}

std::vector<fs::path> existing_files(const std::vector<fs::path>& paths) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& p : paths)
        if (fs::exists(p, ec)) out.push_back(p);
    return out;
}

// ---------------------------------------------------------------- remembered folders
static fs::path g_store;
static bool g_store_loaded = false;
static std::map<std::string, std::string> g_folders;

void folder_store(const fs::path& file) {
    if (g_store_loaded && g_store == file) return;
    g_store = file;
    g_store_loaded = true;
    g_folders.clear();
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    json j = json::parse(in, nullptr, false);
    if (!j.is_object() || !j.contains("folders") || !j["folders"].is_object()) return;
    for (auto it = j["folders"].begin(); it != j["folders"].end(); ++it)
        if (it.value().is_string()) g_folders[it.key()] = it.value().get<std::string>();
}

fs::path remembered_folder(const std::string& key) {
    auto it = g_folders.find(key);
    if (key.empty() || it == g_folders.end()) return fs::path();
    fs::path p = text_path(it->second);
    std::error_code ec;
    return fs::is_directory(p, ec) ? p : fs::path();
}

void remember_folder(const std::string& key, const fs::path& dir) {
    if (key.empty() || dir.empty()) return;
    std::string t = path_text(dir);
    if (g_folders[key] == t) return;
    g_folders[key] = t;
    if (g_store.empty()) return;
    json j = {{"folders", g_folders}};
    fs::path tmp = g_store;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << j.dump(1);
    }
    std::error_code ec;
    fs::rename(tmp, g_store, ec);
    if (ec) {   // a C runtime whose rename does not replace an existing file
        fs::remove(g_store, ec);
        fs::rename(tmp, g_store, ec);
        if (ec) fs::remove(tmp, ec);
    }
}

bool ensure_folder(const fs::path& dir, std::string* err) {
    if (dir.empty()) {
        if (err) *err = "no folder";
        return false;
    }
    NoDriveErrors guard;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) return true;
    fs::create_directories(dir, ec);
    std::error_code e2;
    if (fs::is_directory(dir, e2)) return true;
    if (err) *err = "cannot create " + path_text(dir) + (ec ? ": " + ec.message() : std::string());
    return false;
}

bool open_in_explorer(const fs::path& dir) {
#ifdef _WIN32
    // explorer.exe through CreateProcessW (kernel32): Turbo.dll does not import the shell (turbo_imconfig.h)
    std::wstring cmd = L"explorer.exe \"" + dir.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    (void)dir;
    return false;
#endif
}

// ---------------------------------------------------------------- the picker
static void go(FilePicker& fp, const fs::path& d) {
    fp.dir = d;
    fp.error.clear();
    fp.confirm = false;
}

static fs::path save_target(const FilePicker& fp) {
    std::string n = safe_file_name(fp.name);
    if (n.empty()) return fs::path();
    fs::path p = fp.dir / text_path(n);
    if (!fp.exts.empty() && !has_ext(p, fp.exts)) p += fp.exts.front();
    return p;
}

bool file_picker_modal(const char* popup_id, FilePicker& fp, fs::path& out) {
    bool done = false;
    ImGui::SetNextWindowSize(ImVec2(S(680.0f), S(500.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(popup_id, nullptr, ImGuiWindowFlags_NoSavedSettings)) return false;
    std::error_code ec;
    if (ImGui::IsWindowAppearing()) {
        fs::path r = remembered_folder(fp.key);
        if (!r.empty()) fp.dir = r;
        else if (fp.dir.empty() || !fs::is_directory(fp.dir, ec)) fp.dir = fp.start;
        fp.error.clear();
        fp.confirm = false;
        fp.new_folder_open = false;
        fp.listed.clear();
    }
    if (fp.dir.empty() || !fs::is_directory(fp.dir, ec)) {
        if (!fp.start.empty()) ensure_folder(fp.start);   // Turbo's own default folders (turbo_output\players, ...)
        fp.dir = fs::is_directory(fp.start, ec) ? fp.start : fs::current_path(ec);
    }
    if (fp.shown != fp.dir) {
        fp.shown = fp.dir;
        std::snprintf(fp.path_buf, sizeof(fp.path_buf), "%s", path_text(fp.dir).c_str());
    }
    if (!fp.title.empty()) ImGui::TextUnformatted(fp.title.c_str());

    // folder path (type a folder or a file and press Enter), Up, shortcuts, drives
    ImGui::SetNextItemWidth(-S(60.0f));
    if (ImGui::InputText("##fppath", fp.path_buf, sizeof(fp.path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        fs::path typed = text_path(fp.path_buf);
        NoDriveErrors guard;
        if (fs::is_directory(typed, ec)) go(fp, typed);
        else if (fp.mode != PickMode::Folder && fs::is_regular_file(typed, ec) && has_ext(typed, fp.exts)) {
            if (fp.mode == PickMode::Open) {
                out = typed;
                done = true;
            } else {
                go(fp, typed.parent_path());
                std::snprintf(fp.name, sizeof(fp.name), "%s", path_text(typed.filename()).c_str());
            }
        } else {
            fp.error = "No such folder: " + std::string(fp.path_buf);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Up##fp") && fp.dir.has_parent_path() && fp.dir.parent_path() != fp.dir) go(fp, fp.dir.parent_path());
    bool first = true;
    for (const auto& pl : fp.places) {
        if (pl.second.empty() || !fs::is_directory(pl.second, ec)) continue;
        if (!first) ImGui::SameLine();
        first = false;
        std::string lbl = pl.first + "##fpplace";
        if (ImGui::SmallButton(lbl.c_str())) go(fp, pl.second);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path_text(pl.second).c_str());
    }
#ifdef _WIN32
    DWORD drives = GetLogicalDrives();   // no disk access: an empty card reader never shows a system box
    for (int d = 2; d < 26; ++d) {
        if (!(drives & (1u << d))) continue;
        if (!first) ImGui::SameLine();
        first = false;
        char lbl[16] = {static_cast<char>('A' + d), ':', '#', '#', 'f', 'p', 'd', 0};
        char root[4] = {static_cast<char>('A' + d), ':', '\\', 0};
        if (ImGui::SmallButton(lbl)) go(fp, fs::path(root));
    }
#endif

    // folder content (listed again when the folder changes, and every 2 s)
    double now = ImGui::GetTime();
    if (fp.listed != fp.dir || now - fp.listed_at > 2.0 || now < fp.listed_at) {
        list_folder(fp.dir, fp.exts, fp.dirs, fp.files);
        fp.listed = fp.dir;
        fp.listed_at = now;
    }
    const float fh = ImGui::GetFrameHeightWithSpacing(), th = ImGui::GetTextLineHeightWithSpacing();
    float footer = fh * (fp.mode == PickMode::Save ? 2.0f : 1.0f) + th + (fp.new_folder_open ? fh : 0.0f) +
                   (!fp.error.empty() ? th : 0.0f) + (fp.confirm ? th : 0.0f) + S(4.0f);
    ImGui::BeginChild("##fpfiles", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    fs::path open_dir;
    for (const auto& d : fp.dirs) {
        std::string lbl = "[" + path_text(d.filename()) + "]";
        if (ImGui::Selectable(lbl.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) open_dir = d;
    }
    fs::path sel = fp.mode == PickMode::Save ? fp.dir / text_path(fp.name) : fs::path();
    for (const auto& f : fp.files) {
        std::string lbl = path_text(f.filename());
        if (fp.mode == PickMode::Folder) {
            ImGui::TextDisabled("%s", lbl.c_str());
            continue;
        }
        if (ImGui::Selectable(lbl.c_str(), fp.mode == PickMode::Save && f == sel)) {
            if (fp.mode == PickMode::Open) {
                out = f;
                done = true;
            } else {
                std::snprintf(fp.name, sizeof(fp.name), "%s", lbl.c_str());
                fp.confirm = false;
            }
        }
    }
    if (fp.dirs.empty() && fp.files.empty()) ImGui::TextDisabled("Empty folder.");
    ImGui::EndChild();
    if (!open_dir.empty()) go(fp, open_dir);

    if (!fp.error.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", fp.error.c_str());
    if (fp.new_folder_open) {
        ImGui::SetNextItemWidth(S(260.0f));
        bool enter = ImGui::InputTextWithHint("##fpnewdir", "new folder name", fp.new_folder, sizeof(fp.new_folder),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Create##fpnewdir") || enter) {
            std::string n = safe_file_name(fp.new_folder);
            std::string err;
            if (n.empty()) fp.error = "Type a folder name.";
            else if (ensure_folder(fp.dir / text_path(n), &err)) {
                go(fp, fp.dir / text_path(n));
                fp.new_folder[0] = 0;
                fp.new_folder_open = false;
            } else {
                fp.error = err;
            }
        }
    }

    // file name (Save) and the buttons
    if (fp.mode == PickMode::Save) {
        ImGui::SetNextItemWidth(S(360.0f));
        if (ImGui::InputText("File name##fpname", fp.name, sizeof(fp.name))) fp.confirm = false;
        if (!fp.exts.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", fp.exts.front().c_str());
        }
    }
    if (fp.confirm) {
        fs::path target = save_target(fp);
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s already exists. Replace it?", path_text(target.filename()).c_str());
        if (ImGui::Button("Replace##fp")) {
            out = target;
            done = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Back##fp")) fp.confirm = false;
    } else if (fp.mode == PickMode::Save) {
        fs::path target = save_target(fp);
        if (target.empty()) ImGui::BeginDisabled();
        if (ImGui::Button("Save##fp")) {
            if (fp.ask_overwrite && fs::exists(target, ec)) fp.confirm = true;
            else {
                out = target;
                done = true;
            }
        }
        if (target.empty()) ImGui::EndDisabled();
    } else if (fp.mode == PickMode::Folder) {
        if (ImGui::Button("Use this folder##fp")) {
            out = fp.dir;
            done = true;
        }
    } else {
        ImGui::TextDisabled("Click a file to use it.");
    }
    ImGui::SameLine();
    if (ImGui::Button("New folder##fp")) fp.new_folder_open = !fp.new_folder_open;
    ImGui::SameLine();
    if (ImGui::Button("Cancel##fp")) ImGui::CloseCurrentPopup();
    // secondary: Explorer is a Windows window of its own, so the game leaves full screen (said next to the button)
    ImGui::SameLine();
    if (ImGui::SmallButton("Open in Explorer##fp") && !open_in_explorer(fp.dir)) fp.error = "Explorer did not start.";
    ImGui::SameLine();
    ImGui::TextDisabled("(leaves full screen)");
    ImGui::TextDisabled("Double-click a folder to open it.");

    if (done) {
        remember_folder(fp.key, fp.mode == PickMode::Folder ? out : out.parent_path());
        fp.dir = fp.mode == PickMode::Folder ? out : out.parent_path();
        fp.confirm = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return done;
}

}  // namespace turbo
