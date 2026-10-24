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
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "core/bodytype_thumbs.h"
#include "core/hair_catalog.h"
#include "core/rdbm_resources.h"
#include "imgui.h"
#include "ui_bodytypes.h"
#include "ui_images.h"
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

// Where a tile's picture comes from, per code (TileSource in the header; kept as bytes in the lists)
constexpr uint8_t kSrcRdbm = static_cast<uint8_t>(TileSource::Rdbm), kSrcSwatch = static_cast<uint8_t>(TileSource::Swatch),
                  kSrcGame = static_cast<uint8_t>(TileSource::Game), kSrcBlank = static_cast<uint8_t>(TileSource::Placeholder),
                  kSrcNames = static_cast<uint8_t>(TileSource::NamesOnly);

struct Source {  // what one picker shows
    std::string field, title, note;
    int kind = vis::data::kNone;
    bool body = false;
    bool gallery = false;   // the field has a game gallery (hair, beard, boots, gloves, accessories): game previews + the catalog gate
    bool catalog = false;   // ... and the game asset catalog is on for it
    bool manager = false;
    bool eyebrow = false;   // every eyebrow picture is its style's
    std::vector<int64_t> codes;
    std::vector<std::string> names, search;
    std::vector<fs::path> files;        // the RDBM26 picture (or colour file) of a code; empty = none
    std::vector<std::string> game;      // the game's preview path; "" = none
    std::vector<uint32_t> swatch;       // a colour known from a table (accessory colours); 0 = none
    std::vector<uint8_t> src;           // kSrc* per code
    std::vector<char> writable, hidden;
    std::vector<std::string> why, tag, tip;
    std::vector<int> group;             // index into groups, -1 = none
    std::vector<std::string> groups;    // eyebrow styles, generic head models
    std::unordered_map<int64_t, int> pos;
    int index_of(int64_t code) const {
        auto it = pos.find(code);
        return it == pos.end() ? -1 : it->second;
    }
};

struct HairFacets {
    int length = -1, type = -1, accessory = -1;
    bool any() const { return length >= 0 || type >= 0 || accessory >= 0; }
};

struct State {
    VisualPickerUi ui;
    bool want_open = false;  // the next section / row drawn for this field opens the window
    std::string want_field;
    bool popup_pending = false;  // OpenPopup on the next draw of the window's owner
    // the window is drawn by ONE owner (the first that draws it after it opened; another takes over when that one stops drawing)
    ImGuiID owner = 0;
    bool owner_set = false;
    int owner_frame = -10;
    int wrote_frame = -10;       // the frame the window last wrote a field (vp::inline_field returns it)
    std::string table;
    uint64_t rec = 0;
    bool manager = false;
    Source src;
    char search[64] = "";
    bool fav_only = false;
    int group_sel = -1;
    int scope = -1;
    HairFacets hair;
    std::string shown_key = "\x01";
    std::vector<int> shown;  // indices into src
    int64_t hover = -1;
    double hover_since = 0.0;
    // the pictures folder (looked for once; again after the setting changes)
    bool folder_checked = false;
    bool overridden = false;
    std::string override_dir;
    fs::path res;
    vis::FolderInfo info;  // 3.1 G2b: not found / found with no picture sets / found (res is the folder holding the sets)
    std::unordered_map<std::string, fs::path> set_dirs;  // set id -> its folder in res (empty: none)
    vis::FolderIndex index;                              // directory listings of the sets (per-file lookups)
    std::unordered_map<std::string, uint32_t> colours;   // colour file -> IM_COL32 (decoded once)
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
        s.info = s.overridden ? vis::folder_info(fs::u8path(s.override_dir)) : vis::find_folder_info(app.turbo_output());
        s.res = s.info.resources;
        s.set_dirs.clear();
        s.index.reset();
        s.colours.clear();
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

const char* field_title(const std::string& f) {
    static const std::unordered_map<std::string, const char*> m = {
        {"runstylecode", "Run style"}, {"animfreekickstartposcode", "Free kick run-up"}, {"animpenaltiesstartposcode", "Penalty run-up"},
        {"hairtypecode", "Hair"}, {"haircolorcode", "Hair colour"}, {"facialhairtypecode", "Beard"}, {"facialhaircolorcode", "Beard colour"},
        {"eyebrowcode", "Eyebrows"}, {"eyecolorcode", "Eye colour"}, {"skintonecode", "Skin tone"}, {"skincomplexion", "Complexion"},
        {"headtypecode", "Generic head"},
        {"jerseyfit", "Jersey fit"}, {"jerseysleevelengthcode", "Sleeves"}, {"socklengthcode", "Sock length"}, {"jerseystylecode", "Shirt"},
        {"shoetypecode", "Boots"}, {"smallsidedshoetypecode", "Small-sided boots"}, {"gkglovetypecode", "Goalkeeper gloves"},
        {"accessorycode1", "Accessory 1"}, {"accessorycode2", "Accessory 2"},
        {"accessorycode3", "Accessory 3"}, {"accessorycode4", "Accessory 4"},
        {"accessorycolourcode1", "Accessory 1 colour"}, {"accessorycolourcode2", "Accessory 2 colour"},
        {"accessorycolourcode3", "Accessory 3 colour"}, {"accessorycolourcode4", "Accessory 4 colour"},
        {"tattoohead", "Tattoo: head"}, {"tattoofront", "Tattoo: chest"},
        {"tattooback", "Tattoo: back"}, {"tattooleftarm", "Tattoo: left arm"}, {"tattoorightarm", "Tattoo: right arm"},
        {"tattooleftleg", "Tattoo: left leg"}, {"tattoorightleg", "Tattoo: right leg"}, {"bodytypecode", "Body type"}};
    auto it = m.find(f);
    return it == m.end() ? "Look" : it->second;
}

bool is_acc_field(const std::string& f) { return f.size() == 13 && f.compare(0, 12, "accessorycod") == 0 && f[12] >= '1' && f[12] <= '4'; }
bool is_acc_colour_field(const std::string& f) { return f.size() == 20 && f.compare(0, 19, "accessorycolourcode") == 0 && f[19] >= '1' && f[19] <= '4'; }
std::string colour_field_of(const std::string& acc) { return "accessorycolourcode" + acc.substr(12); }   // accessorycode3 -> accessorycolourcode3
std::string accessory_field_of(const std::string& col) { return "accessorycode" + col.substr(19); }

// The name the editors already show for a code the picture set has no name for (core/field_labels.h, gear and hair names)
std::string editor_name(App& app, const Table& t, const Field& f, int64_t code) { return app.describe_value(t, f, Value::of_int(code)); }

bool is_manager_table(const Table& t) { return !t.has("playerid"); }

// The first word of a generic head's name ("African head 123" -> "African")
std::string head_model(const std::string& name) {
    const size_t sp = name.find(' ');
    return name.empty() ? std::string("Other") : name.substr(0, sp);
}

uint32_t swatch_of(const vis::Rgb8& c) { return IM_COL32(c.r, c.g, c.b, 255); }

void push_tile(Source& o, int64_t code, std::string name) {
    o.pos[code] = static_cast<int>(o.codes.size());
    o.codes.push_back(code);
    o.names.push_back(std::move(name));
    o.files.emplace_back();
    o.game.emplace_back();
    o.swatch.push_back(0);
    o.src.push_back(kSrcBlank);
    o.writable.push_back(1);
    o.hidden.push_back(0);
    o.why.emplace_back();
    o.tag.emplace_back();
    o.tip.emplace_back();
    o.group.push_back(-1);
}

int group_index(Source& o, const std::string& g) {
    for (size_t i = 0; i < o.groups.size(); ++i)
        if (o.groups[i] == g) return static_cast<int>(i);
    o.groups.push_back(g);
    return static_cast<int>(o.groups.size()) - 1;
}

// The list of one field. `only` != INT64_MIN: a light list of that one code (the rows and inline fields show one value).
bool build_source(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager, Source& out, int64_t only = INT64_MIN) {
    out = Source();
    out.field = field;
    out.manager = manager;
    out.title = field_title(field);
    const Field* f = t.field(field);
    if (!f) return false;
    State& s = st();
    const int64_t current = app.db.get_int(t, rec, field, 0);
    const bool light = only != INT64_MIN;
    if (field == "bodytypecode") {
        out.body = true;
        out.kind = vis::data::kPng;
        bodytype::Catalog& cat = bodytype_catalog(app);
        bodytype_thumbs::configure(app.turbo_output());
        const int gender = t.has("gender") ? static_cast<int>(app.db.get_int(t, rec, "gender", -1)) : -1;
        for (int64_t c : looks::body_grid_codes(&cat, current, gender, manager)) {
            push_tile(out, c, looks::body_caption(c, &cat, manager));
            out.files.back() = bodytype_thumbs::thumb(c, manager);
            out.src.back() = out.files.back().empty() ? kSrcBlank : kSrcRdbm;
        }
    } else {
        const vis::SetDef* d = vis::set_for_field(field);
        const bool gal = gallery_has(field, manager);
        if (!d && field == "smallsidedshoetypecode") d = vis::find_set("boots");
        if (!d && !gal) return false;
        out.gallery = gal;
        out.catalog = gal && gallery_catalog_active(field, manager);
        out.kind = d ? d->kind : vis::data::kPng;
        out.eyebrow = d && std::strcmp(d->id, "eyebrow") == 0;
        const bool ghead = d && std::strcmp(d->id, "ghead") == 0;
        if (out.title == "Look" && gal) out.title = gallery_title(field, manager);
        if (d && (std::strcmp(d->id, "boots") == 0 || std::strcmp(d->id, "gloves") == 0))
            out.note = "These pictures are from FC 26 or older RDBM26 folders: a few may look different in FC 27.";
        else if (out.eyebrow)
            out.note = "One picture per eyebrow style: variants of a style share it.";
        else if (ghead)
            out.note = "Generic head (preview from RDBM26): the head model of a generic face, listed by model group.";
        const fs::path& res = resources(app);
        const bool have_res = !res.empty();
        // accessories: the colour of the slot and the accessory a colour belongs to
        int64_t colour = 0, accessory = 0;
        if (is_acc_field(field) && t.has(colour_field_of(field))) colour = app.db.get_int(t, rec, colour_field_of(field), 0);
        if (is_acc_colour_field(field) && t.has(accessory_field_of(field))) accessory = app.db.get_int(t, rec, accessory_field_of(field), 0);
        std::vector<GalleryEntry> ge;
        if (gal && !light) ge = gallery_entries(app, field, manager, colour);
        std::unordered_map<int64_t, size_t> gpos;
        for (size_t i = 0; i < ge.size(); ++i) gpos[ge[i].id] = i;

        std::vector<int64_t> all;
        if (light) {
            all.push_back(only);
        } else {
            if (d) {
                const vis::CodeDef* b = vis::codes_begin(*d);
                for (int i = 0; i < d->count; ++i) all.push_back(b[i].code);
            }
            for (const GalleryEntry& e : ge) all.push_back(e.id);
            if (gal) all.push_back(0);
            all.push_back(current);  // his value is shown even when the lists do not have it
            std::sort(all.begin(), all.end());
            all.erase(std::unique(all.begin(), all.end()), all.end());
            if (ghead) {  // by model group, then by number
                auto grp = [&](int64_t c) {
                    const vis::CodeDef* cd = vis::find_code(*d, c);
                    return head_model(cd && cd->name ? cd->name : "");
                };
                std::stable_sort(all.begin(), all.end(), [&](int64_t a, int64_t b) { return grp(a) < grp(b); });
            }
        }
        for (int64_t code : all) {
            const vis::CodeDef* own = d ? vis::find_code(*d, code) : nullptr;
            const bool named = own && own->name && own->name[0];
            std::string name;
            if (named) {
                name = own->name;
            } else {
                const auto gi = gpos.find(code);
                if (gi != gpos.end() && !ge[gi->second].name.empty()) name = ge[gi->second].name;
                else name = editor_name(app, t, *f, code);
                if (name.empty() || (code == 0 && gal && name == "0")) name = code == 0 ? "None" : out.title + " " + std::to_string(code);
            }
            push_tile(out, code, name);
            const size_t k = out.codes.size() - 1;
            // 1. RDBM26: the set's picture (per file, then the fallback folder), the accessory in the slot's colour, a colour's file
            fs::path file;
            if (have_res && d) {
                if (is_acc_colour_field(field)) {
                    if (accessory > 0) file = vis::accessory_picture(res, code, accessory, &s.index);
                } else {
                    if (is_acc_field(field) && code > 0) file = vis::accessory_picture(res, colour, code, &s.index);
                    if (file.empty()) {
                        const vis::CodeDef* c = vis::find_code_or_style(*d, code);
                        if (c) file = vis::picture_file(res, *d, *c, &s.index);
                    }
                }
            }
            vis::Rgb8 rgb;
            if (is_acc_colour_field(field) && vis::accessory_colour_swatch(code, rgb)) out.swatch[k] = swatch_of(rgb);
            // 2. the game's own preview (catalog gallery), 3. a grey tile
            std::string gp;
            if (gal && code > 0) {
                const auto gi = gpos.find(code);
                gp = gi != gpos.end() ? ge[gi->second].preview : (light ? gallery_picture_path(app, field, code) : std::string());
            }
            out.files[k] = file;
            out.game[k] = gp;
            if (!file.empty()) out.src[k] = vis::is_colour_file(file) ? kSrcSwatch : kSrcRdbm;
            else if (out.swatch[k]) out.src[k] = kSrcSwatch;
            else if (!gp.empty()) out.src[k] = kSrcGame;
            else out.src[k] = (!have_res && !gal) ? kSrcNames : kSrcBlank;
            if (out.eyebrow && !file.empty()) out.tip[k] = "style picture";
            if (!light) {
                const auto gi = gpos.find(code);
                if (gi != gpos.end()) {
                    out.tag[k] = ge[gi->second].tag;
                    out.tip[k] += ge[gi->second].tip;
                    out.hidden[k] = ge[gi->second].hidden;
                }
                if (gal && code != current) {
                    std::string why;
                    if (!gallery_write_ok(app, field, manager, code, &why)) out.writable[k] = 0, out.why[k] = why;
                }
                if (out.eyebrow) out.group[k] = group_index(out, "Style " + std::to_string(vis::eyebrow_style(code)));
                else if (ghead) out.group[k] = group_index(out, head_model(own && own->name ? own->name : ""));
            }
        }
    }
    for (size_t i = 0; i < out.codes.size(); ++i) {
        std::string q = lower(out.names[i]) + " " + std::to_string(out.codes[i]);
        if (field == "hairtypecode" && out.codes[i] > 0) q += " " + lower(hair::describe(hair::lookup(out.codes[i])));
        if (out.group[i] >= 0) q += " " + lower(out.groups[static_cast<size_t>(out.group[i])]);
        out.search.push_back(std::move(q));
    }
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

// A still picture: decoded on the store's thread like the animations (TextureCache::file() would decode up to 4 files on
// the spot in this frame: 22 ms with RDBM26's pictures, measured), given to the window at most kUploadsPerFrame per frame
constexpr size_t kStillBytes = size_t(4) << 20;
bool draw_still(App& app, const fs::path& file, int side, ImVec2 a, ImVec2 box, bool quiet = false) {
    if (file.empty()) {
        if (!quiet) box_text(ImGui::GetWindowDrawList(), a, box, "no picture");
        return false;
    }
    return draw_anim(app, file, side, kStillBytes, a, box, quiet);
}

// A colour file (RDBM26's haircolor_N.json: a 1 x 1 picture of the store) as one colour, decoded once on the store's thread
bool file_colour(const fs::path& file, uint32_t& col) {
    State& s = st();
    const std::string key = file.u8string();
    auto it = s.colours.find(key);
    if (it != s.colours.end()) {
        col = it->second;
        return true;
    }
    vis::AnimStore::Item item = store().get(file, 1, kStillBytes);
    if (!item.anim || item.anim->atlas.empty()) return false;
    const uint8_t* px = item.anim->atlas.at(0, 0);
    col = IM_COL32(px[0], px[1], px[2], 255);
    s.colours[key] = col;
    return true;
}

// A colour as a rounded swatch fitted into box
void draw_swatch_colour(uint32_t col, ImVec2 a, ImVec2 box) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float side = std::min(box.x, box.y);
    const ImVec2 p0(a.x + (box.x - side) * 0.5f, a.y + (box.y - side) * 0.5f);
    const ImVec2 p1(p0.x + side, p0.y + side);
    dl->AddRectFilled(p0, p1, col, side * 0.12f);
    dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 110), side * 0.12f, 1.5f);
}

void draw_blank(const Source& src, ImVec2 a, ImVec2 box) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float side = std::min(box.x, box.y);
    const ImVec2 p0(a.x + (box.x - side) * 0.5f, a.y + (box.y - side) * 0.5f);
    dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(58, 58, 64, 255), 4.0f);
    box_text(dl, p0, ImVec2(side, side), src.kind == vis::data::kGif ? "no animation yet" : "no picture yet");
}

bool draw_picture(App& app, const Source& src, int idx, bool large, ImVec2 a, ImVec2 box) {
    if (idx < 0 || static_cast<size_t>(idx) >= src.codes.size()) {
        box_text(ImGui::GetWindowDrawList(), a, box, "no picture");
        return false;
    }
    const size_t k = static_cast<size_t>(idx);
    switch (src.src[k]) {
        case kSrcSwatch: {
            uint32_t col = src.swatch[k];
            if (!col && !(!src.files[k].empty() && file_colour(src.files[k], col))) {
                box_text(ImGui::GetWindowDrawList(), a, box, "loading");
                return false;
            }
            draw_swatch_colour(col, a, box);
            return true;
        }
        case kSrcGame: {
            const ImVec2 saved = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(a.x + (box.x - std::min(box.x, box.y)) * 0.5f, a.y));
            draw_legacy_picture(app, src.game[k], std::min(box.x, box.y), true);
            ImGui::SetCursorScreenPos(saved);
            return true;
        }
        case kSrcBlank:
            draw_blank(src, a, box);
            return false;
        case kSrcNames:
            box_text(ImGui::GetWindowDrawList(), a, box, "no picture");
            return false;
        default:
            break;
    }
    const fs::path& f = src.files[k];
    if (f.empty()) {
        draw_blank(src, a, box);
        return false;
    }
    if (src.kind == vis::data::kGif) {
        if (!large) return draw_anim(app, f, kTileSide, kTileBytes, a, box);
        if (draw_anim(app, f, kPreviewSide, kPreviewBytes, a, box, true)) return true;
        return draw_anim(app, f, kTileSide, kTileBytes, a, box);  // the tile's frames, scaled up, until the large one is ready
    }
    return draw_still(app, f, large ? 320 : 128, a, box);
}

bool names_only(App& app, const Source& src) {
    if (src.body) return false;
    if (src.gallery) return false;  // the game's previews (or grey tiles) whatever RDBM26 has
    if (src.kind == vis::data::kNone) return true;
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
    s.shown_key = "\x01";
    s.hover = -1;
    s.fav_only = false;
    s.group_sel = -1;
    s.hair = HairFacets();
    build_source(app, t, rec, field, manager, s.src);
    s.ui = VisualPickerUi();
    s.ui.open = true;
    s.ui.field = field;
    s.ui.current = app.db.get_int(t, rec, field, 0);
    s.ui.selected = s.ui.current;
    s.ui.names_only = names_only(app, s.src);
    for (uint8_t k : s.src.src) {
        if (k == kSrcRdbm) ++s.ui.source_rdbm;
        else if (k == kSrcSwatch) ++s.ui.source_swatch;
        else if (k == kSrcGame) ++s.ui.source_game;
        else ++s.ui.source_blank;
    }
    s.popup_pending = true;
    s.owner_set = false;
    s.want_open = false;
    vlog(app, "visual picker opened: " + field + " of " + t.name + " " + std::to_string(who_id(app, t, rec)) + " is " + std::to_string(s.ui.current) +
                  " (" + std::to_string(s.ui.source_rdbm) + " RDBM26 pictures, " + std::to_string(s.ui.source_swatch) + " swatches, " +
                  std::to_string(s.ui.source_game) + " game previews, " + std::to_string(s.ui.source_blank) + " without a picture)");
}

void close_picker() {
    State& s = st();
    s.ui.open = false;
    s.popup_pending = false;
    s.owner_set = false;
    if (store_ptr()) store().cancel_queued();
}

// 3.1 G2b: the one line that says where the pictures come from and WHY there are none: the folder is missing, or it is there
// and holds no picture sets (runstyle, hairstyle, fkanim). Empty when the sets are found.
std::string pictures_line(App& app, bool gallery) {
    resources(app);
    const vis::FolderInfo& fi = st().info;
    if (fi.state == vis::FolderState::Found) return "";
    if (fi.state == vis::FolderState::FoundNoSets)
        return "Found " + fi.root.u8string() + ", but no picture sets inside (" + vis::kSetFolders + "): the pickers show " +
               (gallery ? "the game's own previews and names." : "names.") +
               " Point Settings > Preferences > Pictures for the pickers at your RDBM26 install.";
    if (gallery)
        return "RDBM26 pictures come from your RDBM26 folder (Settings > Preferences > Pictures for the pickers); without it the game's own previews and names are shown.";
    return "Pictures come from your RDBM26 folder (Settings > Preferences > Pictures for the pickers). Without it you pick by name.";
}

void where_pictures_line(App& app, const Source& src) {
    const std::string line = pictures_line(app, src.gallery);
    st().ui.pictures_line = line;
    if (!line.empty()) ImGui::TextDisabled("%s", line.c_str());
}

// A small label in the top-left corner of a tile
void corner_tag(ImDrawList* dl, ImVec2 p, const char* text) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddRectFilled(ImVec2(p.x + 2.0f, p.y + 2.0f), ImVec2(p.x + ts.x + 8.0f, p.y + ts.y + 4.0f), IM_COL32(52, 78, 120, 235), 3.0f);
    dl->AddText(ImVec2(p.x + 5.0f, p.y + 3.0f), IM_COL32(225, 235, 255, 255), text);
}

// What one tile drawn reports
struct TileIo {
    bool clicked = false, hovered = false, right = false;
};
struct TileCount {
    int tiles = 0, pictures = 0;
};

// One tile: picture box, name under it, the code small and grey.
TileIo tile(App& app, const Source& src, int idx, ImVec2 box, int64_t selected, int64_t current, TileCount& count) {
    TileIo io;
    const size_t k = static_cast<size_t>(idx);
    const int64_t code = src.codes[k];
    const float line = ImGui::GetTextLineHeight();
    const ImVec2 cell(box.x, box.y + line * 2.0f + 4.0f);
    ImGui::PushID(idx);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    if (src.src[k] == kSrcGame) draw_picture(app, src, idx, false, a, box);  // draws an item of its own: before the button
    io.clicked = ImGui::InvisibleButton("##tile", cell);
    io.hovered = ImGui::IsItemHovered();
    io.right = io.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool sel = code == selected, cur = code == current;
    if (sel || io.hovered)
        dl->AddRectFilled(a, ImVec2(a.x + cell.x, a.y + cell.y), ImGui::GetColorU32(sel ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered), 4.0f);
    if (cur) dl->AddRect(a, ImVec2(a.x + cell.x, a.y + cell.y), IM_COL32(90, 200, 110, 255), 4.0f, 2.0f);
    ++count.tiles;
    if (src.src[k] == kSrcGame) ++count.pictures;
    else if (draw_picture(app, src, idx, false, a, box)) ++count.pictures;
    if (!src.writable[k]) dl->AddRectFilled(a, ImVec2(a.x + box.x, a.y + box.y), IM_COL32(20, 20, 24, 150));
    if (!src.tag[k].empty()) corner_tag(dl, a, src.tag[k].c_str());
    if (src.gallery && gallery_favourite(app, src.field, code)) dl->AddText(ImVec2(a.x + box.x - 14.0f, a.y + 1.0f), IM_COL32(255, 205, 50, 255), "*");
    const std::string& name = src.names[k];
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
    if (io.hovered && (name.size() > 18 || !src.tip[k].empty() || !src.writable[k] || !src.tag[k].empty())) {
        std::string tip = name;
        if (!src.tip[k].empty()) tip += (src.tip[k][0] == '\n' ? "" : "\n") + src.tip[k];
        if (!src.writable[k] && src.tip[k].find("Cannot be applied") == std::string::npos) tip += "\nCannot be applied: " + src.why[k];
        ImGui::SetTooltip("%s", tip.c_str());
    }
    return io;
}

bool hair_matches(const HairFacets& hf, int64_t id) {
    if (id <= 0) return true;
    const hair::Style hs = hair::lookup(id);
    if (hf.length >= 0 && hs.length != hf.length) return false;
    if (hf.type >= 0 && hs.type != hf.type) return false;
    if (hf.accessory >= 0 && hs.accessory != hf.accessory) return false;
    return true;
}

// The filter key: everything that changes the list shown
std::string filter_key(const State& s) {
    char b[160];
    std::snprintf(b, sizeof(b), "|%d|%d|%d|%d|%d|%d|", s.fav_only ? 1 : 0, s.group_sel, s.scope, s.hair.length, s.hair.type, s.hair.accessory);
    return std::string(s.search) + b;
}

void filter(App& app, State& s) {
    const std::string key = filter_key(s);
    if (s.shown_key == key) return;
    s.shown_key = key;
    s.shown.clear();
    const std::string q = lower(s.search);
    const Source& src = s.src;
    const bool is_hair = src.field == "hairtypecode";
    for (size_t i = 0; i < src.codes.size(); ++i) {
        if (!q.empty() && src.search[i].find(q) == std::string::npos) continue;
        if (s.group_sel >= 0 && src.group[i] != s.group_sel) continue;
        if (s.fav_only && !(src.gallery && gallery_favourite(app, src.field, src.codes[i]))) continue;
        if (src.gallery && !gallery_scope_shows(s.scope, src.hidden[i]) && src.codes[i] != s.ui.current) continue;
        if (is_hair && !hair_matches(s.hair, src.codes[i])) continue;
        s.shown.push_back(static_cast<int>(i));
    }
}

// One hair facet combo (-1 = any), each option with its count among the list
bool facet_combo(const char* label, int& value, int n, const char* (*name)(int), const Source& src, uint8_t hair::Style::*member, const char* any) {
    std::vector<int> counts(static_cast<size_t>(n), 0);
    for (int64_t id : src.codes) {
        if (id <= 0) continue;
        const int v = hair::lookup(id).*member;
        if (v >= 0 && v < n) ++counts[static_cast<size_t>(v)];
    }
    const std::string preview = value < 0 ? std::string(any) : std::string(name(value)) + " (" + std::to_string(counts[static_cast<size_t>(value)]) + ")";
    bool changed = false;
    ImGui::SetNextItemWidth(S(170.0f));
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(any, value < 0)) value = -1, changed = true;
        for (int i = 0; i < n; ++i) {
            if (!counts[static_cast<size_t>(i)]) continue;
            const std::string item = std::string(name(i)) + " (" + std::to_string(counts[static_cast<size_t>(i)]) + ")";
            if (ImGui::Selectable(item.c_str(), value == i)) value = i, changed = true;
        }
        ImGui::EndCombo();
    }
    return changed;
}

void draw_picker_body(App& app, const Table& t, uint64_t rec) {
    State& s = st();
    s.ui.tiles_drawn = s.ui.pictures_shown = 0;
    s.ui.pictures_line.clear();  // 3.1 G2b: set again below when the grid shows the "where are the pictures" line
    s.ui.current = app.db.get_int(t, rec, s.src.field, 0);
    ImGui::SetNextItemWidth(S(260.0f));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##vpsearch", "find by name or number", s.search, sizeof(s.search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu to choose from", s.src.codes.size());
    if (!s.src.groups.empty()) {  // eyebrow styles, generic head models
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(190.0f));
        const char* prev = s.group_sel < 0 ? (s.src.eyebrow ? "All styles" : "All models") : s.src.groups[static_cast<size_t>(s.group_sel)].c_str();
        if (ImGui::BeginCombo("##vpgroup", prev)) {
            if (ImGui::Selectable(s.src.eyebrow ? "All styles" : "All models", s.group_sel < 0)) s.group_sel = -1;
            for (size_t g = 0; g < s.src.groups.size(); ++g)
                if (ImGui::Selectable(s.src.groups[g].c_str(), s.group_sel == static_cast<int>(g))) s.group_sel = static_cast<int>(g);
            ImGui::EndCombo();
        }
    }
    if (s.src.gallery) {
        ImGui::SameLine();
        ImGui::Checkbox("Favourites only##vpfav", &s.fav_only);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click a picture to star it");
        if (s.src.catalog) {
            ImGui::SameLine();
            s.scope = gallery_scope_combo(app, s.src.field, s.src.manager, S(130.0f));
        } else {
            s.scope = -1;
        }
        if (s.src.catalog) gallery_stale_line(s.src.field, s.src.manager);
    }
    if (s.src.field == "hairtypecode") {
        facet_combo("##vphairlen", s.hair.length, hair::kLengthCount, hair::length_name, s.src, &hair::Style::length, "Any length");
        ImGui::SameLine();
        facet_combo("##vphairtype", s.hair.type, hair::kTypeCount, hair::type_name, s.src, &hair::Style::type, "Any type");
        ImGui::SameLine();
        facet_combo("##vphairacc", s.hair.accessory, hair::kAccessoryCount, hair::accessory_name, s.src, &hair::Style::accessory, "Any accessories");
        if (s.hair.any()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##vphairclear")) s.hair = HairFacets();
        }
    }
    if ((s.ui.names_only || (!s.src.body && resources(app).empty())) && s.src.kind != vis::data::kNone) where_pictures_line(app, s.src);
    if (!s.src.note.empty()) ImGui::TextDisabled("%s", s.src.note.c_str());
    filter(app, s);
    if (s.src.field == "hairtypecode") ImGui::TextDisabled("%zu of %zu styles match", s.shown.size(), s.src.codes.size());

    const ImGuiStyle& style = ImGui::GetStyle();
    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.5f + style.ItemSpacing.y;
    const float preview_w = s.ui.names_only ? 0.0f : S(320.0f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    int64_t hovered_code = -1;
    // the grid's right edge can be dragged (the preview takes the rest); the size is the first one only, then the user's
    ImGui::BeginChild("##vpgrid", ImVec2(std::max(S(200.0f), avail.x - preview_w - (preview_w > 0 ? style.ItemSpacing.x : 0.0f)),
                                         std::max(S(120.0f), avail.y - footer)),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
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
                    const int idx = s.shown[static_cast<size_t>(k)];
                    const int64_t tcode = s.src.codes[static_cast<size_t>(idx)];
                    TileCount tc;
                    const TileIo io = tile(app, s.src, idx, box, s.ui.selected, s.ui.current, tc);
                    s.ui.tiles_drawn += tc.tiles;
                    s.ui.pictures_shown += tc.pictures;
                    if (io.clicked) s.ui.selected = tcode;
                    if (io.hovered) hovered_code = tcode;
                    if (io.right && s.src.gallery && tcode != 0) {  // right-click stars it (favourites, as the gallery did)
                        gallery_toggle_favourite(app, s.src.field, tcode);
                        s.shown_key = "\x01";
                    }
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
        ImGui::BeginChild("##vppreview", ImVec2(0.0f, std::max(S(120.0f), avail.y - footer)), ImGuiChildFlags_Borders);
        const int idx = s.src.index_of(pv);
        if (idx >= 0) {
            const float pw = ImGui::GetContentRegionAvail().x;
            const float ph = std::max(S(80.0f), ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 3.0f);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            draw_picture(app, s.src, idx, true, a, ImVec2(pw, ph));
            ImGui::Dummy(ImVec2(pw, ph));
            ImGui::TextWrapped("%s", s.src.names[static_cast<size_t>(idx)].c_str());
            ImGui::TextDisabled("%s %lld%s", s.src.title.c_str(), static_cast<long long>(pv), pv == s.ui.current ? ", the current one" : "");
            if (!s.src.tag[static_cast<size_t>(idx)].empty()) ImGui::TextDisabled("%s", s.src.tag[static_cast<size_t>(idx)].c_str());
        }
        ImGui::EndChild();
    }

    // Apply / Cancel
    const int pi = s.src.index_of(s.ui.selected);
    const bool can_write = pi < 0 || s.src.writable[static_cast<size_t>(pi)];
    if (!can_write) ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "Cannot be applied: %s", s.src.why[static_cast<size_t>(pi)].c_str());
    const bool changed = s.ui.selected != s.ui.current;
    ImGui::BeginDisabled(!changed || !can_write);
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
    if (pi >= 0) ImGui::TextDisabled("Picked: %s", s.src.names[static_cast<size_t>(pi)].c_str());
    if (!s.ui.last_result.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(s.ui.last_result.c_str());
    }
}

// The window, drawn by ONE owner of the buttons (the id scope of the OpenPopup is the one of the BeginPopupModal)
void draw_picker_window(App& app, const Table& t, uint64_t rec) {
    State& s = st();
    if (!s.ui.open || s.table != t.name || s.rec != rec) return;
    const ImGuiID me = ImGui::GetID("##vpowner");
    const int fc = ImGui::GetFrameCount();
    if (!s.owner_set) {
        s.owner = me;
        s.owner_set = true;
    } else if (s.owner != me) {
        if (fc - s.owner_frame <= 1) return;  // another owner draws it
        s.owner = me;                          // the owner left (another page): this one takes the window over
        s.popup_pending = true;
    }
    s.owner_frame = fc;
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
    const int idx = src.index_of(code);
    if (idx >= 0 && !src.src.empty() && src.src[static_cast<size_t>(idx)] == kSrcGame) {
        // the game's picture is an item of its own: drawn first, at the cursor (moving the cursor back after a Dummy trips ImGui's SetCursorPos check)
        draw_picture(app, src, idx, false, a, box);
        ImGui::Dummy(box);
        return;
    }
    ImGui::Dummy(box);
    if (idx >= 0 && src.src[static_cast<size_t>(idx)] == kSrcNames) return;
    if (src.src.empty() || idx < 0) {
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + box.x, a.y + box.y), IM_COL32(58, 58, 64, 255), 4.0f);
        return;
    }
    draw_picture(app, src, idx, false, a, box);
}

// Rows keep a light source (the one value: its name and picture) per field and record, rebuilt when the value or the folder changes
struct RowCache {
    Source src;
    std::string table;
    uint64_t rec = 0;
    int64_t value = INT64_MIN;
    bool manager = false;
    fs::path res;
};

RowCache& row_cache(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager) {
    static std::unordered_map<std::string, RowCache> cache;
    const int64_t v = app.db.get_int(t, rec, field, 0);
    RowCache& rc = cache[field];
    const fs::path& res = resources(app);
    if (rc.table != t.name || rc.rec != rec || rc.value != v || rc.res != res || rc.manager != manager) {
        build_source(app, t, rec, field, manager, rc.src, v);
        rc.table = t.name;
        rc.rec = rec;
        rc.value = v;
        rc.res = res;
        rc.manager = manager;
    }
    return rc;
}

void draw_row(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager) {
    if (!t.has(field)) return;
    State& s = st();
    const int64_t v = app.db.get_int(t, rec, field, 0);
    RowCache& rc = row_cache(app, t, rec, field, manager);
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
    ImGui::TableSetupColumn("Look###vtitle", ImGuiTableColumnFlags_WidthFixed, S(150.0f));
    ImGui::TableSetupColumn("Picture###vpic", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
    ImGui::TableSetupColumn("Chosen###vname", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Change###vbtn", ImGuiTableColumnFlags_WidthFixed, S(100.0f));
    ImGui::TableHeadersRow();
}

}  // namespace

const VisualPickerUi& visual_picker_ui() { return st().ui; }

void visual_picker_request(const std::string& field) {
    st().want_open = true;
    st().want_field = field;
}

void visual_picker_select(int64_t code) { st().ui.selected = code; }

TileSource visual_picker_tile_source(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i < 0 ? TileSource::NamesOnly : static_cast<TileSource>(src.src[static_cast<size_t>(i)]);
}

bool visual_picker_tile_writable(int64_t code, std::string* why) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    if (why) *why = i < 0 ? std::string() : src.why[static_cast<size_t>(i)];
    return i < 0 || src.writable[static_cast<size_t>(i)];
}

size_t visual_picker_list_size() { return st().src.codes.size(); }

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
    st().colours.clear();
}

bool visual_apply(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t code, std::string* msg) {
    const Field* f = t.field(field);
    if (!f) {
        if (msg) *msg = std::string(field_title(field)) + " is not in this database";
        return false;
    }
    if (field == "bodytypecode") return apply_bodytype(app, t, rec, code, false, msg);
    // the game's catalog gate (hair, beard, boots, gloves of players): only what FC 27 ships may be written
    const bool manager = is_manager_table(t);
    bool hidden_before = false;
    if (gallery_has(field, manager)) {
        std::string why;
        if (!gallery_write_ok(app, field, manager, code, &why)) {
            if (msg) *msg = std::string(field_title(field)) + " " + std::to_string(code) + ": " + why;
            return false;
        }
        hidden_before = !manager && gallery_hidden(app, field, manager, code);
    }
    const int64_t before = app.db.get_int(t, rec, field, 0);
    if (!app.edit(t, rec, *f, Value::of_int(code))) {
        if (msg) *msg = std::string(field_title(field)) + " was not changed";
        return false;
    }
    if (hidden_before) gallery_log_hidden(app, t, rec, field, before, code);  // turbo_output\asset_writes.log, as the gallery did
    st().wrote_frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -10;
    if (msg) {
        std::string name;
        const vis::SetDef* d = vis::set_for_field(field);
        if (d) name = vis::code_name(*d, code, editor_name(app, t, *f, code));
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
                           "skintonecode", "skincomplexion", "headtypecode", "bodytypecode"}},
        {"Kit and boots", {"jerseyfit", "jerseysleevelengthcode", "jerseystylecode", "socklengthcode", "shoetypecode", "smallsidedshoetypecode",
                           "gkglovetypecode", "accessorycode1", "accessorycolourcode1", "accessorycode2", "accessorycolourcode2", "accessorycode3",
                           "accessorycolourcode3", "accessorycode4", "accessorycolourcode4"}},
        {"Tattoos", {"tattoohead", "tattoofront", "tattooback", "tattooleftarm", "tattoorightarm", "tattooleftleg", "tattoorightleg"}},
    };
    ImGui::PushID("vispickers");
    if (resources(app).empty()) ImGui::TextDisabled("%s", pictures_line(app, false).c_str());
    for (const auto& g : groups) {
        bool any = false;
        for (const std::string& f : g.second) any = any || t.has(f);
        if (!any) continue;
        ImGui::SeparatorText(g.first);
        ImGui::PushID(g.first);
        if (ImGui::BeginTable("##vrows", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
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
    if (ImGui::BeginTable("##vrow", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit)) {
        begin_rows();
        draw_row(app, t, rec, field, manager);
        ImGui::EndTable();
    }
    ImGui::PopID();
    if (st().want_field == field) st().want_open = false;
    if (st().ui.field == field) draw_picker_window(app, t, rec);
    ImGui::PopID();
}

// ---- the inline field (2.22)
namespace vp {

bool has_picture(App& app, const Table& t, const char* field, bool manager) {
    if (!t.has(field)) return false;
    const std::string f = field;
    if (f == "bodytypecode") return false;
    if (gallery_has(f, manager)) return true;
    return vis::set_for_field(f) != nullptr && !resources(app).empty();
}

void open(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager) {
    new_frame(app);
    open_picker(app, t, rec, field, manager);
}

void window(App& app, const Table& t, uint64_t rec) {
    new_frame(app);
    ImGui::PushID("vpwin");
    draw_picker_window(app, t, rec);
    ImGui::PopID();
}

bool draw_field_picture(App& app, const Table& t, uint64_t rec, const char* field, bool manager, float side) {
    new_frame(app);
    if (!t.has(field)) return false;
    RowCache& rc = row_cache(app, t, rec, field, manager);
    const int idx = rc.src.index_of(app.db.get_int(t, rec, field, 0));
    if (idx < 0) return false;
    const uint8_t k = rc.src.src[static_cast<size_t>(idx)];
    if (k != kSrcRdbm && k != kSrcSwatch) return false;
    row_picture(app, rc.src, app.db.get_int(t, rec, field, 0), side);
    return true;
}

bool inline_field(App& app, const Table& t, uint64_t rec, const char* field, const char* label, bool manager) {
    new_frame(app);
    if (!t.has(field)) return false;
    const int64_t v = app.db.get_int(t, rec, field, 0);
    RowCache& rc = row_cache(app, t, rec, field, manager);
    const float side = S(56.0f);
    ImGui::PushID("vpinline");
    ImGui::PushID(field);
    if (label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(S(170.0f));
    }
    row_picture(app, rc.src, v, side);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1.0f, std::max(0.0f, (side - ImGui::GetFrameHeight()) * 0.5f - ImGui::GetStyle().ItemSpacing.y)));  // centre under the picture (no SetCursorPos: ImGui asserts)
    const int idx = rc.src.index_of(v);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(idx >= 0 ? rc.src.names[static_cast<size_t>(idx)].c_str() : "-");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s = %lld", field, static_cast<long long>(v));
    ImGui::SameLine();
    if (ImGui::Button("Change##vpchange")) open_picker(app, t, rec, field, manager);
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::PopID();
    if (st().ui.open && st().ui.field == field) window(app, t, rec);
    return st().wrote_frame == ImGui::GetFrameCount();
}

void inline_appearance(App& app, const Table& t, uint64_t rec, bool manager) {
    static const struct {
        const char* field;
        const char* title;
    } kRows[] = {{"eyebrowcode", "Eyebrows"},
                 {"haircolorcode", "Hair colour"},
                 {"facialhaircolorcode", "Beard colour"},
                 {"eyecolorcode", "Eye colour"},
                 {"skintonecode", "Skin tone"},
                 {"accessorycolourcode1", "Accessory 1 colour"},
                 {"accessorycolourcode2", "Accessory 2 colour"},
                 {"accessorycolourcode3", "Accessory 3 colour"},
                 {"accessorycolourcode4", "Accessory 4 colour"},
                 {"headtypecode", "Generic head"}};
    bool any = false;
    for (const auto& r : kRows) any = any || has_picture(app, t, r.field, manager);
    if (!any) return;
    ImGui::SeparatorText("Colours and details");
    for (const auto& r : kRows)
        if (has_picture(app, t, r.field, manager)) inline_field(app, t, rec, r.field, r.title, manager);
}


// ---- the inline grid (2.22): the Set pieces card shows run-ups and run styles as moving tiles in the page itself, not in a window
namespace {
struct GridState {
    Source src;
    std::string table;
    uint64_t rec = 0;
    int64_t value = INT64_MIN;
    fs::path res;
    int64_t selected = -1;
    int tiles = 0, pictures = 0;
    bool built = false;
    std::string result;
};
std::map<std::string, GridState>& grids() {
    static std::map<std::string, GridState> g;
    return g;
}
}  // namespace

bool inline_grid(App& app, const Table& t, uint64_t rec, const char* field, const char* title, float height, bool manager) {
    new_frame(app);
    if (!t.has(field)) return false;
    GridState& g = grids()[field];
    const int64_t v = app.db.get_int(t, rec, field, 0);
    const fs::path& res = resources(app);
    if (!g.built || g.table != t.name || g.rec != rec || g.value != v || g.res != res) {
        const bool same = g.built && g.table == t.name && g.rec == rec;
        if (!build_source(app, t, rec, field, manager, g.src)) {
            g.built = false;
            return false;
        }
        for (uint8_t& k : g.src.src)  // an inline grid is a picture grid: a tile with no animation says so (never the bare names list)
            if (k == kSrcNames) k = kSrcBlank;
        g.table = t.name;
        g.rec = rec;
        g.value = v;
        g.res = res;
        g.built = true;
        if (!same || g.selected < 0 || g.src.index_of(g.selected) < 0) g.selected = v;
        if (!same) g.result.clear();
    }
    ImGui::PushID("vpgrid");
    ImGui::PushID(field);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    const int ci = g.src.index_of(v);
    ImGui::SameLine();
    ImGui::TextDisabled("now: %s", ci >= 0 ? g.src.names[static_cast<size_t>(ci)].c_str() : "-");
    ImGui::SameLine();
    ImGui::BeginDisabled(g.selected == v);
    bool wrote = false;
    if (ImGui::Button("Apply##vpgridapply")) {
        std::string msg;
        wrote = visual_apply(app, t, rec, field, g.selected, &msg);
        g.result = msg;
        app.notify(msg, !wrote);
        vlog(app, "visual grid: " + std::string(field) + " of " + t.name + " " + std::to_string(who_id(app, t, rec)) + " picked " +
                      std::to_string(g.selected) + ", read back " + std::to_string(app.db.get_int(t, rec, field, INT64_MIN)) +
                      (wrote ? "" : " (refused: " + msg + ")"));
    }
    ImGui::EndDisabled();
    if (!g.result.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g.result.c_str());
    }
    const bool tall = g.src.kind == vis::data::kGif;
    const ImVec2 box = tall ? ImVec2(S(84.0f), S(136.0f)) : ImVec2(S(84.0f), S(84.0f));
    const float cell_h = box.y + ImGui::GetTextLineHeight() * 2.0f + 4.0f;
    ImGui::BeginChild("##grid", ImVec2(0.0f, std::max(height, cell_h + S(14.0f))), ImGuiChildFlags_Borders);
    const ImGuiStyle& style = ImGui::GetStyle();
    const int n = static_cast<int>(g.src.codes.size());
    const int per_row = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (box.x + style.ItemSpacing.x)));
    const int rows = (n + per_row - 1) / per_row;
    TileCount tc;
    ImGuiListClipper clip;
    clip.Begin(rows, cell_h + style.ItemSpacing.y);
    while (clip.Step())
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
            for (int c = 0; c < per_row; ++c) {
                const int k = r * per_row + c;
                if (k >= n) break;
                if (c > 0) ImGui::SameLine();
                const TileIo io = tile(app, g.src, k, box, g.selected, v, tc);
                if (io.clicked) g.selected = g.src.codes[static_cast<size_t>(k)];
            }
    ImGui::EndChild();
    g.tiles = tc.tiles;
    g.pictures = tc.pictures;
    ImGui::PopID();
    ImGui::PopID();
    return wrote;
}

GridInfo grid_info(const char* field) {
    GridInfo gi;
    auto it = grids().find(field);
    if (it == grids().end()) return gi;
    const GridState& g = it->second;
    gi.built = g.built;
    gi.codes = static_cast<int>(g.src.codes.size());
    gi.tiles = g.tiles;
    gi.pictures = g.pictures;
    gi.selected = g.selected;
    gi.current = g.value;
    int blank = 0;
    for (uint8_t k : g.src.src) blank += k == kSrcBlank;
    gi.without_picture = blank;
    return gi;
}

void grid_select(const char* field, int64_t code) { grids()[field].selected = code; }

bool grid_apply(App& app, const Table& t, uint64_t rec, const char* field, std::string* msg) {
    GridState& g = grids()[field];
    const bool ok = visual_apply(app, t, rec, field, g.selected, msg);
    if (ok) g.value = INT64_MIN;  // rebuilt on the next draw
    return ok;
}

}  // namespace vp

void visual_pictures_setting(App& app) {
    State& s = st();
    if (!s.folder_edit_init) {
        s.folder_edit_init = true;
        const std::string cur = vis::load_folder_setting(app.turbo_output());
        std::snprintf(s.folder_edit, sizeof(s.folder_edit), "%s", cur.empty() ? vis::kDefaultRoot : cur.c_str());
    }
    ImGui::SetNextItemWidth(S(360.0f));
    ImGui::InputText("RDBM26 folder###vpfolder", s.folder_edit, sizeof(s.folder_edit));
    ImGui::SameLine();
    if (ImGui::Button("Use this folder###vpfolderset")) {
        std::string err;
        if (vis::save_folder_setting(app.turbo_output(), s.folder_edit, &err)) {
            visual_pictures_refresh(app);
            const vis::FolderState fs_now = visual_pictures_state(app);
            if (fs_now == vis::FolderState::Found) app.notify("Pictures found");
            else if (fs_now == vis::FolderState::FoundNoSets) app.notify(std::string("Folder found, but no picture sets inside (") + vis::kSetFolders + "): the pickers show names");
            else app.notify("No pictures in that folder: the pickers show names");
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
    const vis::FolderState fstate = visual_pictures_state(app);
    if (fstate == vis::FolderState::Found)
        ImGui::TextDisabled("Found: run styles, free kicks and appearance pictures are read from your RDBM26 install (never copied).");
    else if (fstate == vis::FolderState::FoundNoSets)
        ImGui::TextDisabled("%s", visual_pictures_note(app).c_str());  // "Found <path>, but no picture sets inside (runstyle, hairstyle, fkanim)..."
    else
        ImGui::TextDisabled("Not found. Turbo shows the animations and pictures of your own RDBM26 install; without it the pickers list names.");
}

// 3.1 G2b: the state of the pictures folder, and the line that says why there are no pictures ("" when the sets are found)
vis::FolderState visual_pictures_state(App& app) {
    resources(app);
    return st().info.state;
}

std::string visual_pictures_note(App& app) { return pictures_line(app, false); }

}  // namespace turbo
