#include "ui_visual_picker.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "core/bodytype_thumbs.h"
#include "core/rdbm_resources.h"
#include "imgui.h"
#include "ui_bodytypes.h"
#include "ui_looks_studio.h"

namespace fs = std::filesystem;

namespace turbo {

namespace {

namespace vis = visual;

constexpr int kTileSide = 180;                       // a tile's animation frames are decoded at this size (longer side)
constexpr size_t kTileBytes = size_t(16) << 20;      // one tile's atlas at most (a 134-frame free kick gets smaller frames)
constexpr int kPreviewSide = 420;                    // the large preview
constexpr size_t kPreviewBytes = size_t(32) << 20;
constexpr int kUploadsPerFrame = 2;                  // new animations given to the window's textures per frame
constexpr double kHoverDelay = 0.15;                 // the preview follows the mouse after this long on a tile

struct Source {  // what one picker shows
    std::string field, title, note;
    int kind = vis::data::kNone;
    bool body = false;
    std::vector<int64_t> codes;
    std::vector<std::string> names, search;
    std::vector<fs::path> files;
    int index_of(int64_t code) const {
        for (size_t i = 0; i < codes.size(); ++i)
            if (codes[i] == code) return static_cast<int>(i);
        return -1;
    }
};

struct State {
    VisualPickerUi ui;
    bool want_open = false;  // the next section / row drawn for this field opens the window
    std::string want_field;
    bool popup_pending = false;  // OpenPopup on the next draw of the window's owner
    std::string table;
    uint64_t rec = 0;
    bool manager = false;
    Source src;
    char search[64] = "";
    std::string shown_query = "\x01";
    std::vector<int> shown;  // indices into src
    int64_t hover = -1;
    double hover_since = 0.0;
    // the pictures folder (looked for once; again after the setting changes)
    bool folder_checked = false;
    bool overridden = false;
    std::string override_dir;
    fs::path res;
    std::unordered_map<std::string, fs::path> set_dirs;  // set id -> its folder in res (empty: none)
    char folder_edit[300] = "";
    bool folder_edit_init = false;
    // textures
    std::unordered_set<std::string> uploaded;
    int frame = -1, uploads_left = 0;
};

State& st() {
    static State s;
    return s;
}

// The decoding thread lives as long as the window (the game never unloads Turbo: no join at process exit, where joining
// a thread under the loader lock could hang). The tests stop it with visual_pickers_shutdown().
vis::AnimStore*& store_ptr() {
    static vis::AnimStore* p = nullptr;
    return p;
}
vis::AnimStore& store() {
    vis::AnimStore*& p = store_ptr();
    if (!p) p = new vis::AnimStore();
    return *p;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

const fs::path& resources(App& app) {
    State& s = st();
    if (!s.folder_checked) {
        s.folder_checked = true;
        s.res = s.overridden ? vis::resources_dir(fs::u8path(s.override_dir)) : vis::find_resources(app.turbo_output());
        s.set_dirs.clear();
        for (size_t i = 0; i < vis::set_count(); ++i) {
            const vis::SetDef& d = vis::set_at(i);
            fs::path dir;
            if (!s.res.empty() && d.kind != vis::data::kNone && d.dir && d.dir[0]) {
                if (is_dir(s.res / d.dir)) dir = s.res / d.dir;
                else if (d.fallback_dir && is_dir(s.res / d.fallback_dir)) dir = s.res / d.fallback_dir;
            }
            s.set_dirs[d.id] = dir;
        }
    }
    return s.res;
}

const fs::path& set_dir(App& app, const vis::SetDef& d) {
    resources(app);
    static const fs::path none;
    auto it = st().set_dirs.find(d.id);
    return it == st().set_dirs.end() ? none : it->second;
}

const char* field_title(const std::string& f) {
    static const std::unordered_map<std::string, const char*> m = {
        {"runstylecode", "Run style"}, {"animfreekickstartposcode", "Free kick run-up"}, {"animpenaltiesstartposcode", "Penalty run-up"},
        {"hairtypecode", "Hair"}, {"haircolorcode", "Hair colour"}, {"facialhairtypecode", "Beard"}, {"facialhaircolorcode", "Beard colour"},
        {"eyebrowcode", "Eyebrows"}, {"eyecolorcode", "Eye colour"}, {"skintonecode", "Skin tone"}, {"skincomplexion", "Complexion"},
        {"jerseyfit", "Jersey fit"}, {"jerseysleevelengthcode", "Sleeves"}, {"socklengthcode", "Sock length"}, {"jerseystylecode", "Shirt"},
        {"shoetypecode", "Boots"}, {"gkglovetypecode", "Goalkeeper gloves"}, {"accessorycode1", "Accessory 1"}, {"accessorycode2", "Accessory 2"},
        {"accessorycode3", "Accessory 3"}, {"accessorycode4", "Accessory 4"}, {"tattoohead", "Tattoo: head"}, {"tattoofront", "Tattoo: chest"},
        {"tattooback", "Tattoo: back"}, {"tattooleftarm", "Tattoo: left arm"}, {"tattoorightarm", "Tattoo: right arm"},
        {"tattooleftleg", "Tattoo: left leg"}, {"tattoorightleg", "Tattoo: right leg"}, {"bodytypecode", "Body type"}};
    auto it = m.find(f);
    return it == m.end() ? "Look" : it->second;
}

// The name the editors already show for a code the picture set has no name for (core/field_labels.h, gear and hair names)
std::string editor_name(App& app, const Table& t, const Field& f, int64_t code) { return app.describe_value(t, f, Value::of_int(code)); }

bool build_source(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager, Source& out) {
    out = Source();
    out.field = field;
    out.title = field_title(field);
    const Field* f = t.field(field);
    if (!f) return false;
    const int64_t current = app.db.get_int(t, rec, field, 0);
    if (field == "bodytypecode") {
        out.body = true;
        out.kind = vis::data::kPng;
        bodytype::Catalog& cat = bodytype_catalog(app);
        bodytype_thumbs::configure(app.turbo_output());
        const int gender = t.has("gender") ? static_cast<int>(app.db.get_int(t, rec, "gender", -1)) : -1;
        for (int64_t c : looks::body_grid_codes(&cat, current, gender, manager)) {
            out.codes.push_back(c);
            out.names.push_back(looks::body_caption(c, &cat, manager));
            out.files.push_back(bodytype_thumbs::thumb(c, manager));
        }
    } else {
        const vis::SetDef* d = vis::set_for_field(field);
        if (!d) return false;
        out.kind = d->kind;
        if (std::strcmp(d->id, "boots") == 0 || std::strcmp(d->id, "gloves") == 0)
            out.note = "These pictures are from FC 26: a few may look different in FC 27.";
        const fs::path& dir = set_dir(app, *d);
        const vis::CodeDef* b = vis::codes_begin(*d);
        for (int i = 0; i < d->count; ++i) {
            const vis::CodeDef& c = b[i];
            out.codes.push_back(c.code);
            out.names.push_back(vis::code_name(*d, c.code, c.name && c.name[0] ? std::string() : editor_name(app, t, *f, c.code)));
            out.files.push_back(dir.empty() || !c.file ? fs::path() : dir / c.file);
        }
        if (out.index_of(current) < 0) {  // his value is not in the set: still shown (first), with the editors' name
            out.codes.insert(out.codes.begin(), current);
            out.names.insert(out.names.begin(), vis::code_name(*d, current, editor_name(app, t, *f, current)));
            out.files.insert(out.files.begin(), fs::path());
        }
    }
    for (size_t i = 0; i < out.codes.size(); ++i) out.search.push_back(lower(out.names[i]) + " " + std::to_string(out.codes[i]));
    return true;
}

void new_frame(App& app) {
    State& s = st();
    const int fc = ImGui::GetFrameCount();
    if (s.frame == fc) return;
    s.frame = fc;
    s.uploads_left = kUploadsPerFrame;
    s.ui.uploads_last_frame = 0;
    if (store_ptr())
        for (const std::string& k : store().take_evicted()) {
            app.textures.forget_pixels(k);
            s.uploaded.erase(k);
        }
}

void fit_rect(float w, float h, ImVec2 a, ImVec2 box, ImVec2& p0, ImVec2& p1) {
    const float sc = std::min(box.x / std::max(1.0f, w), box.y / std::max(1.0f, h));
    const float dw = w * sc, dh = h * sc;
    p0 = ImVec2(a.x + (box.x - dw) * 0.5f, a.y + (box.y - dh) * 0.5f);
    p1 = ImVec2(p0.x + dw, p0.y + dh);
}

void box_text(ImDrawList* dl, ImVec2 a, ImVec2 box, const char* text) {
    const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, box.x - 4.0f);
    dl->AddText(nullptr, 0.0f, ImVec2(a.x + (box.x - std::min(ts.x, box.x)) * 0.5f, a.y + (box.y - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), text, nullptr, box.x - 4.0f);
}

// An animation drawn into box at a; false (and a word in the box) while it is not ready
bool draw_anim(App& app, const fs::path& file, int side, size_t max_bytes, ImVec2 a, ImVec2 box, bool quiet = false) {
    State& s = st();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    vis::AnimStore::Item it = store().get(file, side, max_bytes);
    if (!it.anim) {
        if (!quiet) box_text(dl, a, box, it.failed ? "no picture" : "loading");
        return false;
    }
    if (!s.uploaded.count(it.key)) {
        if (s.uploads_left <= 0) {
            if (!quiet) box_text(dl, a, box, "loading");
            return false;
        }
        --s.uploads_left;
        ++s.ui.uploads_last_frame;
        s.uploaded.insert(it.key);
    }
    const vis::Anim& an = *it.anim;
    TextureCache::Pic pic = app.textures.pixels(it.key, 1, an.atlas);
    if (!pic.tex) {
        if (pic.failed) s.uploaded.erase(it.key);
        if (!quiet) box_text(dl, a, box, "loading");
        return false;
    }
    float u0, v0, u1, v1;
    an.uv(an.frame_at(ImGui::GetTime() * 1000.0), u0, v0, u1, v1);
    ImVec2 p0, p1;
    fit_rect(static_cast<float>(an.frame_w), static_cast<float>(an.frame_h), a, box, p0, p1);
    dl->AddImage(pic.tex->GetTexRef(), p0, p1, ImVec2(u0, v0), ImVec2(u1, v1));
    return true;
}

bool draw_still(App& app, const fs::path& file, int side, ImVec2 a, ImVec2 box, bool quiet = false) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (file.empty()) {
        if (!quiet) box_text(dl, a, box, "no picture");
        return false;
    }
    app.textures.preload(file, side);  // decoded on the background thread; file() then only uploads it
    TextureCache::Pic pic = app.textures.file(file, side);
    if (!pic.tex || pic.w <= 0 || pic.h <= 0) {
        if (!quiet) box_text(dl, a, box, pic.failed ? "no picture" : "loading");
        return false;
    }
    ImVec2 p0, p1;
    fit_rect(static_cast<float>(pic.w), static_cast<float>(pic.h), a, box, p0, p1);
    dl->AddImage(pic.tex->GetTexRef(), p0, p1);
    return true;
}

bool draw_picture(App& app, const Source& src, int idx, bool large, ImVec2 a, ImVec2 box) {
    if (idx < 0 || static_cast<size_t>(idx) >= src.files.size() || src.files[static_cast<size_t>(idx)].empty()) {
        box_text(ImGui::GetWindowDrawList(), a, box, "no picture");
        return false;
    }
    const fs::path& f = src.files[static_cast<size_t>(idx)];
    if (src.kind == vis::data::kGif) {
        if (!large) return draw_anim(app, f, kTileSide, kTileBytes, a, box);
        if (draw_anim(app, f, kPreviewSide, kPreviewBytes, a, box, true)) return true;
        return draw_anim(app, f, kTileSide, kTileBytes, a, box);  // the tile's frames, scaled up, until the large one is ready
    }
    return draw_still(app, f, large ? 320 : 128, a, box);
}

bool names_only(App& app, const Source& src) {
    if (src.kind == vis::data::kNone) return true;
    if (src.body) return false;
    return resources(app).empty();
}

// One line in the window's log and in turbo_gui.log (the Tester's live check reads these)
void vlog(App& app, const std::string& line) {
    app.log(line);
    if (app.log_hook) app.log_hook(line);
}

int64_t who_id(App& app, const Table& t, uint64_t rec) {
    return t.has("playerid") ? app.db.get_int(t, rec, "playerid", -1) : app.db.get_int(t, rec, "managerid", -1);
}

void open_picker(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager) {
    State& s = st();
    store().cancel_queued();
    s.table = t.name;
    s.rec = rec;
    s.manager = manager;
    s.search[0] = 0;
    s.shown_query = "\x01";
    s.hover = -1;
    build_source(app, t, rec, field, manager, s.src);
    s.ui = VisualPickerUi();
    s.ui.open = true;
    s.ui.field = field;
    s.ui.current = app.db.get_int(t, rec, field, 0);
    s.ui.selected = s.ui.current;
    s.ui.names_only = names_only(app, s.src);
    s.popup_pending = true;
    s.want_open = false;
    vlog(app, "visual picker opened: " + field + " of " + t.name + " " + std::to_string(who_id(app, t, rec)) + " is " + std::to_string(s.ui.current));
}

void close_picker() {
    State& s = st();
    s.ui.open = false;
    s.popup_pending = false;
    if (store_ptr()) store().cancel_queued();
}

void where_pictures_line() {
    ImGui::TextDisabled("Pictures come from your RDBM26 folder (Settings > Preferences > Pictures for the pickers). Without it you pick by name.");
}

// One tile: picture box, name under it, the code small and grey. Returns true when clicked.
bool tile(App& app, State& s, int idx, ImVec2 box, bool& hovered) {
    const Source& src = s.src;
    const int64_t code = src.codes[static_cast<size_t>(idx)];
    const float line = ImGui::GetTextLineHeight();
    const ImVec2 cell(box.x, box.y + line * 2.0f + 4.0f);
    ImGui::PushID(idx);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tile", cell);
    hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool sel = code == s.ui.selected, cur = code == s.ui.current;
    if (sel || hovered)
        dl->AddRectFilled(a, ImVec2(a.x + cell.x, a.y + cell.y), ImGui::GetColorU32(sel ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered), 4.0f);
    if (cur) dl->AddRect(a, ImVec2(a.x + cell.x, a.y + cell.y), IM_COL32(90, 200, 110, 255), 4.0f, 0, 2.0f);
    ++s.ui.tiles_drawn;
    if (draw_picture(app, src, idx, false, a, box)) ++s.ui.pictures_shown;
    const std::string& name = src.names[static_cast<size_t>(idx)];
    const ImVec2 np(a.x + 2.0f, a.y + box.y + 2.0f);
    dl->PushClipRect(np, ImVec2(a.x + cell.x - 2.0f, np.y + line), true);
    const float nw = ImGui::CalcTextSize(name.c_str()).x;
    dl->AddText(ImVec2(np.x + std::max(0.0f, (cell.x - 4.0f - nw) * 0.5f), np.y), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    dl->PopClipRect();
    char cbuf[48];
    std::snprintf(cbuf, sizeof(cbuf), cur ? "%lld  (now)" : "%lld", static_cast<long long>(code));
    ImFont* font = ImGui::GetFont();
    const float fs_small = ImGui::GetFontSize() * 0.85f;
    const float cw = font->CalcTextSizeA(fs_small, FLT_MAX, 0.0f, cbuf).x;
    dl->AddText(font, fs_small, ImVec2(a.x + std::max(0.0f, (cell.x - cw) * 0.5f), np.y + line), ImGui::GetColorU32(ImGuiCol_TextDisabled), cbuf);
    if (hovered && name.size() > 18) ImGui::SetTooltip("%s", name.c_str());
    return clicked;
}

void filter(State& s) {
    if (s.shown_query == s.search) return;
    s.shown_query = s.search;
    s.shown.clear();
    const std::string q = lower(s.search);
    for (size_t i = 0; i < s.src.codes.size(); ++i)
        if (q.empty() || s.src.search[i].find(q) != std::string::npos) s.shown.push_back(static_cast<int>(i));
}

void draw_picker_body(App& app, const Table& t, uint64_t rec) {
    State& s = st();
    s.ui.tiles_drawn = s.ui.pictures_shown = 0;
    s.ui.current = app.db.get_int(t, rec, s.src.field, 0);
    ImGui::SetNextItemWidth(S(260.0f));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##vpsearch", "find by name or number", s.search, sizeof(s.search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu to choose from", s.src.codes.size());
    if (s.ui.names_only && s.src.kind != vis::data::kNone) where_pictures_line();
    if (!s.src.note.empty()) ImGui::TextDisabled("%s", s.src.note.c_str());
    filter(s);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footer = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    const float preview_w = s.ui.names_only ? 0.0f : S(320.0f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    int64_t hovered_code = -1;
    ImGui::BeginChild("##vpgrid", ImVec2(std::max(S(200.0f), avail.x - preview_w - (preview_w > 0 ? style.ItemSpacing.x : 0.0f)),
                                         std::max(S(120.0f), avail.y - footer)),
                      ImGuiChildFlags_Borders);
    const int n = static_cast<int>(s.shown.size());
    if (s.ui.names_only) {
        ImGuiListClipper clip;
        clip.Begin(n);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const int idx = s.shown[static_cast<size_t>(r)];
                const int64_t code = s.src.codes[static_cast<size_t>(idx)];
                ImGui::PushID(idx);
                if (ImGui::Selectable(s.src.names[static_cast<size_t>(idx)].c_str(), code == s.ui.selected)) s.ui.selected = code;
                ImGui::SameLine();
                ImGui::TextDisabled(code == s.ui.current ? "%lld  (now)" : "%lld", static_cast<long long>(code));
                ImGui::PopID();
                ++s.ui.tiles_drawn;
            }
    } else {
        const bool tall = s.src.kind == vis::data::kGif;
        const ImVec2 box = tall ? ImVec2(S(104.0f), S(170.0f)) : ImVec2(S(104.0f), S(104.0f));
        const float cell_h = box.y + ImGui::GetTextLineHeight() * 2.0f + 4.0f;
        const float gw = ImGui::GetContentRegionAvail().x;
        const int per_row = std::max(1, static_cast<int>((gw + style.ItemSpacing.x) / (box.x + style.ItemSpacing.x)));
        const int rows = (n + per_row - 1) / per_row;
        ImGuiListClipper clip;
        clip.Begin(rows, cell_h + style.ItemSpacing.y);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
                for (int c = 0; c < per_row; ++c) {
                    const int k = r * per_row + c;
                    if (k >= n) break;
                    if (c > 0) ImGui::SameLine();
                    bool hov = false;
                    const int idx = s.shown[static_cast<size_t>(k)];
                    if (tile(app, s, idx, box, hov)) s.ui.selected = s.src.codes[static_cast<size_t>(idx)];
                    if (hov) hovered_code = s.src.codes[static_cast<size_t>(idx)];
                }
    }
    ImGui::EndChild();

    // the preview: the tile under the mouse once it has been there a moment, else the one picked
    if (hovered_code != s.hover) {
        s.hover = hovered_code;
        s.hover_since = ImGui::GetTime();
    }
    const int64_t pv = s.hover >= 0 && ImGui::GetTime() - s.hover_since >= kHoverDelay ? s.hover : s.ui.selected;
    s.ui.previewed = pv;
    if (preview_w > 0) {
        ImGui::SameLine();
        ImGui::BeginChild("##vppreview", ImVec2(preview_w, std::max(S(120.0f), avail.y - footer)), ImGuiChildFlags_Borders);
        const int idx = s.src.index_of(pv);
        if (idx >= 0) {
            const float pw = ImGui::GetContentRegionAvail().x;
            const float ph = std::max(S(80.0f), ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 3.0f);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            draw_picture(app, s.src, idx, true, a, ImVec2(pw, ph));
            ImGui::Dummy(ImVec2(pw, ph));
            ImGui::TextWrapped("%s", s.src.names[static_cast<size_t>(idx)].c_str());
            ImGui::TextDisabled("%s %lld%s", s.src.title.c_str(), static_cast<long long>(pv), pv == s.ui.current ? ", his now" : "");
        }
        ImGui::EndChild();
    }

    // Apply / Cancel
    const bool changed = s.ui.selected != s.ui.current;
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Apply##vpapply")) {
        if (visual_picker_apply(app)) {
            app.notify(s.ui.last_result);
            ImGui::CloseCurrentPopup();
            close_picker();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel##vpcancel")) {
        ImGui::CloseCurrentPopup();
        close_picker();
    }
    ImGui::SameLine();
    const int si = s.src.index_of(s.ui.selected);
    if (si >= 0) ImGui::TextDisabled("Picked: %s", s.src.names[static_cast<size_t>(si)].c_str());
    if (!s.ui.last_result.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(s.ui.last_result.c_str());
    }
}

// The window, drawn by the owner of the "Choose..." buttons (same id scope as the OpenPopup)
void draw_picker_window(App& app, const Table& t, uint64_t rec) {
    State& s = st();
    if (!s.ui.open || s.table != t.name || s.rec != rec) return;
    if (s.popup_pending) {
        ImGui::OpenPopup("###vispicker");
        s.popup_pending = false;
    }
    ImGui::SetNextWindowSize(ImVec2(S(1000.0f), S(680.0f)), ImGuiCond_Appearing);
    bool keep = true;
    const std::string title = s.src.title + "###vispicker";
    if (ImGui::BeginPopupModal(title.c_str(), &keep, ImGuiWindowFlags_NoSavedSettings)) {
        draw_picker_body(app, t, rec);
        ImGui::EndPopup();
    } else {
        close_picker();
        return;
    }
    if (!keep) close_picker();
}

// The value's small picture and name in a row
void row_picture(App& app, const Source& src, int64_t code, float side) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 box = src.kind == vis::data::kGif ? ImVec2(side * 0.6f, side) : ImVec2(side, side);
    ImGui::Dummy(box);
    if (names_only(app, src)) return;
    draw_picture(app, src, src.index_of(code), false, a, box);
}

// Rows keep a light source (names + files, no search) per field and record, rebuilt when the value or the folder changes
struct RowCache {
    Source src;
    std::string table;
    uint64_t rec = 0;
    int64_t value = INT64_MIN;
    fs::path res;
};

void draw_row(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager) {
    static std::unordered_map<std::string, RowCache> cache;
    if (!t.has(field)) return;
    State& s = st();
    const int64_t v = app.db.get_int(t, rec, field, 0);
    RowCache& rc = cache[field];
    const fs::path& res = resources(app);
    if (rc.table != t.name || rc.rec != rec || rc.value != v || rc.res != res) {
        build_source(app, t, rec, field, manager, rc.src);
        rc.table = t.name;
        rc.rec = rec;
        rc.value = v;
        rc.res = res;
    }
    ImGui::PushID(field.c_str());
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rc.src.title.c_str());
    ImGui::TableNextColumn();
    row_picture(app, rc.src, v, S(56.0f));
    ImGui::TableNextColumn();
    const int idx = rc.src.index_of(v);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(idx >= 0 ? rc.src.names[static_cast<size_t>(idx)].c_str() : "-");
    ImGui::SameLine();
    ImGui::TextDisabled("%lld", static_cast<long long>(v));
    ImGui::TableNextColumn();
    if (ImGui::Button("Choose...") || (s.want_open && s.want_field == field)) open_picker(app, t, rec, field, manager);
    ImGui::PopID();
}

void begin_rows() {
    ImGui::TableSetupColumn("##vtitle", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
    ImGui::TableSetupColumn("##vpic", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
    ImGui::TableSetupColumn("##vname", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##vbtn", ImGuiTableColumnFlags_WidthFixed, S(100.0f));
}

}  // namespace

const VisualPickerUi& visual_picker_ui() { return st().ui; }

void visual_picker_request(const std::string& field) {
    st().want_open = true;
    st().want_field = field;
}

void visual_picker_select(int64_t code) { st().ui.selected = code; }

void visual_picker_close() { close_picker(); }

void visual_pictures_refresh(App& app) {
    st().folder_checked = false;
    resources(app);
}

void visual_pictures_override(const std::string& resources_dir) {
    State& s = st();
    s.overridden = !resources_dir.empty();
    s.override_dir = resources_dir;
    s.folder_checked = false;
}

void visual_pickers_shutdown() {
    vis::AnimStore*& p = store_ptr();
    delete p;
    p = nullptr;
    st().uploaded.clear();
}

bool visual_apply(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t code, std::string* msg) {
    const Field* f = t.field(field);
    if (!f) {
        if (msg) *msg = std::string(field_title(field)) + " is not in this database";
        return false;
    }
    if (field == "bodytypecode") return apply_bodytype(app, t, rec, code, false, msg);
    if (!app.edit(t, rec, *f, Value::of_int(code))) {
        if (msg) *msg = std::string(field_title(field)) + " was not changed";
        return false;
    }
    if (msg) {
        std::string name;
        if (const vis::SetDef* d = vis::set_for_field(field)) name = vis::code_name(*d, code, editor_name(app, t, *f, code));
        *msg = std::string(field_title(field)) + " set to " + (name.empty() ? std::to_string(code) : name);
    }
    return true;
}

bool visual_picker_apply(App& app) {
    State& s = st();
    const Table* t = app.db.table(s.table);
    if (!s.ui.open || !t) {
        s.ui.last_result = "Nothing to apply";
        return false;
    }
    std::string msg;
    const bool ok = visual_apply(app, *t, s.rec, s.src.field, s.ui.selected, &msg);
    s.ui.last_result = msg;
    // read back from the game's table (the pass/fail point of the Tester's live check)
    const int64_t back = app.db.get_int(*t, s.rec, s.src.field, INT64_MIN);
    vlog(app, "visual picker: " + s.src.field + " of " + t->name + " " + std::to_string(who_id(app, *t, s.rec)) + " picked " +
                 std::to_string(s.ui.selected) + ", read back " + std::to_string(back) + (ok ? "" : " (refused: " + msg + ")"));
    if (ok) s.ui.current = s.ui.selected;
    return ok;
}

void visual_pickers_section(App& app, const Table& t, uint64_t rec, bool manager) {
    new_frame(app);
    static const std::vector<std::pair<const char*, std::vector<std::string>>> groups = {
        {"Movement", {"runstylecode", "animfreekickstartposcode", "animpenaltiesstartposcode"}},
        {"Hair and face", {"hairtypecode", "haircolorcode", "facialhairtypecode", "facialhaircolorcode", "eyebrowcode", "eyecolorcode",
                           "skintonecode", "skincomplexion", "bodytypecode"}},
        {"Kit and boots", {"jerseyfit", "jerseysleevelengthcode", "jerseystylecode", "socklengthcode", "shoetypecode", "gkglovetypecode",
                           "accessorycode1", "accessorycode2", "accessorycode3", "accessorycode4"}},
        {"Tattoos", {"tattoohead", "tattoofront", "tattooback", "tattooleftarm", "tattoorightarm", "tattooleftleg", "tattoorightleg"}},
    };
    ImGui::PushID("vispickers");
    if (resources(app).empty()) where_pictures_line();
    for (const auto& g : groups) {
        bool any = false;
        for (const std::string& f : g.second) any = any || t.has(f);
        if (!any) continue;
        ImGui::SeparatorText(g.first);
        ImGui::PushID(g.first);
        if (ImGui::BeginTable("##vrows", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            begin_rows();
            for (const std::string& f : g.second) draw_row(app, t, rec, f, manager);
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    st().want_open = false;
    draw_picker_window(app, t, rec);
    ImGui::PopID();
}

void visual_field_row(App& app, const Table& t, uint64_t rec, const char* field, bool manager) {
    new_frame(app);
    if (!t.has(field)) return;
    ImGui::PushID("vispickers");
    ImGui::PushID(field);
    if (ImGui::BeginTable("##vrow", 4, ImGuiTableFlags_SizingFixedFit)) {
        begin_rows();
        draw_row(app, t, rec, field, manager);
        ImGui::EndTable();
    }
    ImGui::PopID();
    if (st().want_field == field) st().want_open = false;
    if (st().ui.field == field) draw_picker_window(app, t, rec);
    ImGui::PopID();
}

void visual_pictures_setting(App& app) {
    State& s = st();
    if (!s.folder_edit_init) {
        s.folder_edit_init = true;
        const std::string cur = vis::load_folder_setting(app.turbo_output());
        std::snprintf(s.folder_edit, sizeof(s.folder_edit), "%s", cur.empty() ? vis::kDefaultRoot : cur.c_str());
    }
    ImGui::SeparatorText("Pictures for the pickers");
    ImGui::SetNextItemWidth(S(360.0f));
    ImGui::InputText("RDBM26 folder###vpfolder", s.folder_edit, sizeof(s.folder_edit));
    ImGui::SameLine();
    if (ImGui::Button("Use this folder###vpfolderset")) {
        std::string err;
        if (vis::save_folder_setting(app.turbo_output(), s.folder_edit, &err)) {
            visual_pictures_refresh(app);
            app.notify(resources(app).empty() ? "No pictures in that folder: the pickers show names" : "Pictures found");
        } else {
            app.notify("The setting could not be saved: " + err, true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Default###vpfolderdef")) {
        std::snprintf(s.folder_edit, sizeof(s.folder_edit), "%s", vis::kDefaultRoot);
        vis::save_folder_setting(app.turbo_output(), "");
        visual_pictures_refresh(app);
    }
    const fs::path& res = resources(app);
    if (res.empty())
        ImGui::TextDisabled("Not found. Turbo shows the animations and pictures of your own RDBM26 install; without it the pickers list names.");
    else
        ImGui::TextDisabled("Found: run styles, free kicks and appearance pictures are read from your RDBM26 install (never copied).");
}

}  // namespace turbo
