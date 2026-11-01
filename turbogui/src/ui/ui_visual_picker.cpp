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
#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "modal_size.h"
#include "core/body_shape.h"
#include "core/bodytype_thumbs.h"
#include "core/hairbeard_thumbs.h"
#include "core/field_labels.h"
#include "core/hair_catalog.h"
#include "core/look_pickers.h"
#include "core/rdbm_resources.h"
#include "imgui.h"
#include "ui_theme.h"
#include "ui_bodytypes.h"
#include "ui_body_figure.h"
#include "ui_faces.h"
#include "ui_fcedit_pictures.h"
#include "ui_fcedit_previews.h"
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
                  kSrcNames = static_cast<uint8_t>(TileSource::NamesOnly), kSrcFigure = static_cast<uint8_t>(TileSource::Figure),
                  kSrcFcEditor = static_cast<uint8_t>(TileSource::FcEditor);
// Turbo X: the tooltip word of a tile whose picture is FC Editor's asset picture (ui_fcedit_pictures.h)
constexpr const char* kFcEditorPictureTip = "FC Editor picture";
constexpr const char* kTurboPackPictureTip = "Turbo's in-game picture (rendered by FC 27)";  // TX-102 R2-P: fcedit::capture_picture
// TX tattoo lane: a tattoo's design picture, drawn from the game's own decal texture content/character/tattoo/tattoo_<id>_color by
// turbo/tools/tattoo/tattoo_designs.py into <turbo_output>\pictures\assets\tattoo_design\<id>.png (one folder for all seven areas)
constexpr const char* kTattooDesignField = "tattoo_design";
constexpr const char* kTattooDesignTip = "The tattoo's design (from the FC 27 game's tattoo texture)";
// Turbo X 1.0.3 RF (owner 11-01: "I just need the real 3d heads rendered - no use rendering generic heads"): the real-face chooser's
// ONLY picture is the game's render of that hair / beard on a real (scanned) head (fcedit::realface_picture: pictures/realface/<field>/
// <id>.png, then the legacy pictures/realface_test/<field>_<id>.png; the gear generator's real_face runs). A tile without one is a plain "not rendered" placeholder, still a choice: never a
// generic-head capture, a FC 27 catalogue / FC Editor / RDBM26 2D picture or a drawn class icon.
constexpr const char* kRealFacePictureTip = "Rendered by FC 27 on a real (scanned) head";
constexpr const char* kRealFaceMissingCap = "not rendered";

struct Source {  // what one picker shows
    std::string field, title, note;
    int kind = vis::data::kNone;
    bool body = false;
    bool gallery = false;   // the field has a game gallery (hair, beard, boots, gloves, accessories): game previews + the catalog gate
    bool catalog = false;   // ... and the game asset catalog is on for it
    bool manager = false;
    bool eyebrow = false;   // every eyebrow picture is its style's
    bool fcedit_any = false;  // Turbo X: at least one tile shows FC Editor's asset picture (kSrcFcEditor): never a names-only list then
    std::vector<int64_t> codes;
    std::vector<std::string> names, search;
    std::vector<fs::path> files;        // the RDBM26 picture (or colour file) of a code; empty = none
    std::vector<std::string> game;      // the game's preview path; "" = none
    std::vector<uint32_t> swatch;       // a colour known from a table (accessory colours); 0 = none
    std::vector<uint8_t> src;           // kSrc* per code
    std::vector<bodyshape::BodyShape> shape;  // a body type: the figure drawn for each code (kSrcFigure)
    std::vector<char> writable, hidden;
    std::vector<std::string> why, tip;  // step 7: the catalog's data-state tag (GalleryEntry::tag) is no longer carried: a tile is a choice
    std::vector<uint32_t> icon;            // 5.0c E10 fix round: a drawn class icon of a hair / beard tile with no game picture (0 = none; see hair_icon / beard_icon)
    std::vector<uint8_t> fcedit;           // 5.0c E10-fcedit: 1 = the tile's picture is an FC Editor preview from the local cache (still kSrcRdbm)
    std::vector<std::string> cap, reason;  // 5.0c E10-12: a grey tile's short word in its box, and why it has no picture ("" on tiles that have one)
    std::vector<int> group;             // index into groups, -1 = none
    std::vector<std::string> groups;    // eyebrow styles, generic head models
    std::string zero_game;              // Turbo X bald: the game gallery's own preview of code 0 when it lists one ("" = none): only the real-face chooser's Bald / No beard tile shows it
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
    bool show_blank = false;  // step 7: "Show N without picture" (off: tiles with no picture are left out of the grid)
    int blank_count = 0;      // N: the tiles the toggle hides (after the search and the other filters)
    // step 7: the 3D section's chooser (vp::open_choice): the band's generic ids only, Apply hands the code back instead of writing
    struct Choice {
        bool on = false;
        std::string field;
        std::vector<int64_t> ids;
        int64_t current = 0;
        bool ready = false;  // Apply was pressed: `code` waits for take_choice
        int64_t code = 0;
    } choice;
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
    std::unordered_map<std::string, bool> palette;       // 5.0c E11: RDBM26 PNG -> "is a palette (colour type 3) strand map"; probed once per file
    char folder_edit[300] = "";
    bool folder_edit_init = false;
    char fe_edit[300] = "";  // Turbo X: the FC Editor folder box (Settings > Preferences)
    bool fe_edit_init = false;
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
        s.palette.clear();
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
        {"tattooleftleg", "Tattoo: left leg"}, {"tattoorightleg", "Tattoo: right leg"}, {"bodytypecode", "Body type"},
        {"outfitid", "Outfit"}, {"seasonaloutfitid", "Seasonal outfit"}};
    auto it = m.find(f);
    if (it != m.end()) return it->second;
    if (const look::LookPicker* p = look::find(f.c_str())) return p->label;  // step 7: a registry column keeps its card label, never "Look"
    return "Look";
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


// ---- 5.0c E10 (fix round, 2026-10-31): a hair or beard tile the game ships no picture for (139 model-only hair ids, 59 model-only
// beards, hair 50, hair 0 / beard 0, a code the catalogue does not list) is DRAWN from its look class instead of a grey no-picture
// box: the class the hair catalogue gives the id (length, type, accessory; hair_catalog.h) or the nearest listed style's look (face_looks.h,
// the same table the filter and the names use), the facial hair class for a beard. It is a pictogram of the CLASS, not a render of the
// model: the tile's tooltip says so (step 7: no mark on the tile itself, and such tiles are hidden by default behind "Show N without
// picture"). A lit render of these ids needs an in-game capture lease
// (brief D3), which no offline tool replaces. The icon packs into a uint32_t: bit 31 valid, bits 28..30 kind (0 hair, 1 beard),
// bits 16..23 length (hair) or class (beard), 8..15 type, 0..7 accessory.
constexpr uint32_t kIconValid = 1u << 31;
uint32_t pack_icon(uint32_t kind, uint32_t a, uint32_t b, uint32_t c) { return kIconValid | (kind << 28) | (a << 16) | (b << 8) | c; }
uint32_t hair_icon(int64_t code) {
    uint8_t len = hair::kBald, type = hair::kOther, acc = hair::kNoAccessory;
    if (code != 0) {
        const hair::Style hs = hair::lookup(code);
        if (hs.classified) {
            len = hs.length >= hair::kUnknownLength ? static_cast<uint8_t>(hair::kShort) : hs.length;
            type = hs.type;
            acc = hs.accessory;
        } else {
            switch (labels::style_look(faces::looks::kHair, code)) {  // the nearest listed style's look: 1 bald / buzz, 2 short, 3 medium, 4 long, 5 tied
                case 1: len = hair::kBuzz; break;
                case 3: len = hair::kMedium; break;
                case 4: len = hair::kLong; break;
                case 5: len = hair::kMedium; type = hair::kTied; break;
                default: len = hair::kShort; break;
            }
        }
    }
    return pack_icon(0, len, type, acc);
}
uint32_t beard_icon(int64_t code) { return pack_icon(1, code == 0 ? 0u : labels::style_look(faces::looks::kFacialHair, code), 0, 0); }

void push_tile(Source& o, int64_t code, std::string name) {
    o.pos[code] = static_cast<int>(o.codes.size());
    o.codes.push_back(code);
    o.names.push_back(std::move(name));
    o.files.emplace_back();
    o.game.emplace_back();
    o.swatch.push_back(0);
    o.src.push_back(kSrcBlank);
    o.shape.emplace_back();
    o.writable.push_back(1);
    o.hidden.push_back(0);
    o.why.emplace_back();
    o.tip.emplace_back();
    o.cap.emplace_back();
    o.reason.emplace_back();
    o.icon.push_back(0);
    o.fcedit.push_back(0);
    o.group.push_back(-1);
}

// 5.0c E11: RDBM26's hair PNGs of PNG colour type 3 (palette) are strand flow maps (green / blue), not pictures of a haircut: 326 of the 788
// files of its hairstyle folder. The type is byte 25 of the file (8-byte signature, then the IHDR chunk: length, "IHDR", width, height,
// bit depth, colour type). A short, unreadable or non-PNG file is not a palette PNG (it stays what it was: the picture decoder says
// "no picture" for it). Probed once per file, forgotten when the pictures folder is looked at again.
size_t g_png_probes = 0;  // PNG headers read by is_palette_png (tests: visual_picker_png_probes)

bool is_palette_png(const fs::path& file) {
    State& s = st();
    const std::string key = file.u8string();
    const auto hit = s.palette.find(key);
    if (hit != s.palette.end()) return hit->second;
    bool palette = false;
    ++g_png_probes;
    std::ifstream in(file, std::ios::binary);
    unsigned char h[26];
    if (in.read(reinterpret_cast<char*>(h), sizeof h) && in.gcount() == static_cast<std::streamsize>(sizeof h)) {
        static const unsigned char kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        palette = std::memcmp(h, kSig, sizeof kSig) == 0 && std::memcmp(h + 12, "IHDR", 4) == 0 && h[25] == 3;
    }
    s.palette[key] = palette;
    return palette;
}

// 5.0c E10-fcedit-previews: the third picture source of the hair and facial-hair tiles, after the game's preview, RDBM26 and a swatch.
// <turbo_output>/cache/previews/fcedit27/<field>/<code>.png, written offline by turbo_dev/tools/fcedit_previews.ps1 from Cassio's FC Editor
// install (lit renders; local only, never shipped). The folder is listed once per picker open (about 650 files: one directory read, no
// per-tile stat), a light one-code list reuses the last listing. A missing folder or file = nothing changes.
std::map<std::string, std::unordered_map<int64_t, fs::path>> g_fcedit;  // folder -> code -> file
size_t g_fcedit_count = 0;  // tiles of the open picker with such a picture (visual_picker_source_fcedit)

fs::path fcedit_picture(App& app, const std::string& field, int64_t code, bool relist) {
    if (field != "hairtypecode" && field != "facialhairtypecode") return {};
    const fs::path dir = app.turbo_output() / "cache" / "previews" / "fcedit27" / field;
    const std::string key = dir.u8string();
    auto hit = g_fcedit.find(key);
    if (hit == g_fcedit.end() || relist) {
        std::unordered_map<int64_t, fs::path> files;
        std::error_code ec;
        fs::directory_iterator it(dir, ec), end;
        while (!ec && it != end) {
            const fs::path& f = it->path();
            const std::string stem = f.stem().u8string();
            if (f.extension() == ".png" && !stem.empty() && stem.size() < 10 && std::all_of(stem.begin(), stem.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                files[std::stoll(stem)] = f;
            it.increment(ec);
        }
        hit = g_fcedit.insert_or_assign(key, std::move(files)).first;
    }
    const auto f = hit->second.find(code);
    return f == hit->second.end() ? fs::path() : f->second;
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
        // 5.0c E8 (the old modal's Generic / Specific filter, carried over): the two groups, always both so the combo never changes shape
        const int g_generic = group_index(out, "Generic only"), g_specific = group_index(out, "Player-specific only");
        const std::set<int64_t> generic_codes = looks::body_generic_codes(&cat);
        for (int64_t c : looks::body_grid_codes(&cat, current, gender, manager)) {
            push_tile(out, c, looks::body_caption(c, &cat, manager));
            out.group.back() = looks::body_is_specific(c, &cat, manager, generic_codes) ? g_specific : g_generic;
            // 5.0c E8-body: every body type is a figure drawn in code on one height scale (no picture file, players and managers alike)
            out.src.back() = kSrcFigure;
            // the generator's real in-game render (the Body-type pictures run, players only): the large preview shows it
            // when it exists; the tile and the fallback stay the figure
            out.files.back() = bodytype_thumbs::thumb(c, manager);
            out.shape.back() = bodyshape::body_shape(manager, c, &cat);
            out.tip.back() = bodyshape::body_detail(manager, c, &cat) + "\nBody type " + std::to_string(c);
            // Turbo X: FC Editor's real body render (assets\bodytypecode\"<id> <name>.png") REPLACES the drawn figure in the tile and
            // the preview; the figure stays the fallback (no picture, or while the file loads: draw_picture kSrcFcEditor)
            const std::string fe = fcedit::picture(app, field, c);
            if (!fe.empty()) {
                out.src.back() = kSrcFcEditor;
                out.files.back() = fs::u8path(fe);
                out.tip.back() += std::string("\n") + kFcEditorPictureTip;
                out.fcedit_any = true;
            }
        }
    } else {
        const vis::SetDef* d = vis::set_for_field(field);
        const bool gal = gallery_has(field, manager);
        if (!d && field == "smallsidedshoetypecode") d = vis::find_set("boots");
        // 5.0b-A: a column of the look registry (core/look_pickers.h) always opens a list. With no picture set and no gallery it is the
        // names / numbers list of its labelled codes (the codes the save uses, the range): never a dead Change button.
        const bool registry_only = !d && !gal && look::find(field.c_str()) != nullptr;
        // Turbo X: FC Editor's asset pictures (ui_fcedit_pictures.h): a picture source of every field the install has a folder for
        // (boots, gloves, outfits, heads, eyebrows, colours...). A field with no other list at all still opens on them.
        const bool fe_field = fcedit::has_field(app, field);
        if (!d && !gal && !registry_only && !fe_field) return false;
        out.gallery = gal;
        out.catalog = gal && gallery_catalog_active(field, manager);
        out.kind = d ? d->kind : vis::data::kPng;
        out.eyebrow = d && std::strcmp(d->id, "eyebrow") == 0;
        const bool ghead = d && std::strcmp(d->id, "ghead") == 0;
        if (out.title == "Look" && gal) out.title = gallery_title(field, manager);
        // step 7: a tattoo picker opens on the tattoos made for its body area (the save's tattoo table); the combo lists the others too
        const int tattoo_area = tattoo_area_index(field);
        const int g_this_area = tattoo_area >= 0 ? group_index(out, "Made for this area") : -1;
        const int g_other_area = tattoo_area >= 0 ? group_index(out, "Made for other areas") : -1;
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
        if (const auto g0 = gpos.find(0); g0 != gpos.end()) out.zero_game = ge[g0->second].preview;  // the real-face chooser's Bald tile

        std::vector<int64_t> all;
        if (light) {
            all.push_back(only);
        } else {
            // 3.2 (charter 19.17): this list is the ONE merged catalogue of the field: the RDBM26 set (named codes, the players' own
            // codes), the game's gallery ids, none, his value, and for a gallery field the codes the combo that used to sit beside it
            // offered (the codes the save uses, the named ones, the small range): every code either picker offered on 3.1.0 is here, once.
            // A code the game files do not carry stays refused by the gallery gate below (writable = 0, with the reason). Release
            // users have no asset catalogue, so the RDBM26 codes are NOT dropped when the gallery lists a few ids (catalogue-off path).
            if (d) {
                const vis::CodeDef* b = vis::codes_begin(*d);
                for (int i = 0; i < d->count; ++i) all.push_back(b[i].code);
            }
            // fix round 2 (reviewer D1): a field with labelled codes but no gallery (Sleeves 0..4, Sock length 0..3, jersey fit, hair / eye colour,
            // skin tone, accessory colours...) gets the same merge: the codes field_labels.h names (enum_defs or named codes) are offered, not only the picture
            // set's (Sleeves 4 and Sock length 3 "Low" were not offered at all). A field with no labelled code (eyebrows, tattoos) is unchanged. The offered set that changes per field is listed by the test of fix 2.
            if (gal || registry_only || fe_field || labels::enum_def(field) || labels::named_codes(field))
                for (int64_t c : code_value_list(app, t, *f, current)) all.push_back(c);
            if (fe_field && !d && !gal && !registry_only)  // Turbo X: FC Editor's pictures are the only list of this field
                for (const auto& kv : fcedit::index(app).fields.find(field)->second) all.push_back(kv.first);
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
        bool fe_listed = false;  // 5.0c E10-fcedit-previews: the cache folder is read once per full build, at the first tile that asks
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
                if (name.empty() || (code == 0 && gal && name == "0")) name = code == 0 ? (field == "hairtypecode" ? "Bald" : "None") : out.title + " " + std::to_string(code);
            }
            push_tile(out, code, name);
            const size_t k = out.codes.size() - 1;
            // TX-102 R2-P (revised): for hair and beard (code > 0) the order is (1) Turbo's in-game capture (the FC 27 game's own render,
            // fcedit::capture_picture: pictures\raw\<field> + the generator's manifest), (2) the game's catalogue preview, (3) FC Editor's FC 27
            // render (fcedit::picture: pictures\assets conversions + the FC Editor folder), (4) RDBM26 (FC 26), (5) the rest of the chain below.
            // Code 0 (Bald / No beard) keeps its old chain.
            const bool hair_or_beard = field == "hairtypecode" || field == "facialhairtypecode";
            const bool hb = hair_or_beard && code > 0;
            // TX tattoo lane: a tattoo (code > 0) has ONE picture order: (1) its design (kTattooDesignField, the game's own decal texture),
            // (2) the game's catalogue preview, (3) none. The gear generator's full-body renders (pictures\assets\tattoo<area>: the
            // tattoo barely shows under the kit), its captures, FC Editor's and RDBM26's (FC 26) pictures are never a tattoo tile.
            // Code 0 ("No tattoo") keeps its old chain.
            const bool tattoo_tile = tattoo_area >= 0 && code > 0;
            // the game's own preview (catalog gallery)
            std::string gp;
            if (gal && code > 0) {
                const auto gi = gpos.find(code);
                gp = gi != gpos.end() ? ge[gi->second].preview : (light ? gallery_picture_path(app, field, code) : std::string());
            }
            std::string hb_pic;  // (1) or (3)
            bool from_capture = false;
            if (tattoo_tile) {
                hb_pic = fcedit::picture(app, kTattooDesignField, code);
            } else if (hb) {
                hb_pic = fcedit::capture_picture(app, field, code);
                from_capture = !hb_pic.empty();
                if (hb_pic.empty() && gp.empty() && fe_field) hb_pic = fcedit::picture(app, field, code);
            }
            // RDBM26: the set's picture (per file, then the fallback folder), the accessory in the slot's colour, a colour's file.
            // Not looked at for a hair / beard that has a picture above (also saves the palette probe of review E10-12).
            fs::path file;
            if (have_res && d && !tattoo_tile && !(hb && (!hb_pic.empty() || !gp.empty()))) {
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
            // 5.0c E11 (D1): for hair and beard an RDBM26 palette PNG is a strand map, never the tile's picture: the game's lit preview
            // wins, else the grey tile (it still writes: the code is the game's). Other sets' type-3 PNGs (boots, gloves...) are real pictures.
            bool palette_only = false;
            bool from_fcedit = false, fc_picture = false;
            if (!hb_pic.empty()) {
                file = fs::u8path(hb_pic);
                if (from_capture || tattoo_tile) gp.clear();  // the capture / the design beats the preview: the tile, the large preview and the tooltip all show it
                fc_picture = true;
            } else if (hair_or_beard && !file.empty() && !gp.empty() && !vis::is_colour_file(file)) {
                file.clear();
            } else if (hair_or_beard && !file.empty() && !vis::is_colour_file(file) && is_palette_png(file)) {
                file.clear();
                palette_only = gp.empty();
            }
            // Turbo X: no game preview, no RDBM26 picture, no swatch -> FC Editor's own asset picture (read where it is, ui_fcedit_pictures.h),
            // then (5.0c E10-fcedit-previews) the lit render converted into the local cache (if any)
            if (file.empty() && gp.empty() && !out.swatch[k] && fe_field && !tattoo_tile) {
                const std::string fe = fcedit::picture(app, field, code);
                if (!fe.empty()) {
                    file = fs::u8path(fe);
                    fc_picture = true;
                    palette_only = false;
                }
            }
            if (file.empty() && gp.empty() && !out.swatch[k] && !tattoo_tile) {
                const fs::path fe = fcedit_picture(app, field, code, !light && !fe_listed);
                fe_listed = true;
                if (!fe.empty()) {
                    file = fe;
                    from_fcedit = true;
                    palette_only = false;  // a palette strand map was dropped above; the lit render replaces it
                }
            }
            out.files[k] = file;
            out.game[k] = gp;
            if (from_fcedit) {
                out.fcedit[k] = 1;
                out.tip[k] = kFcEditPreviewNote;
            }
            if (fc_picture) {
                out.src[k] = kSrcFcEditor;
                out.tip[k] = tattoo_tile ? kTattooDesignTip : from_capture ? kTurboPackPictureTip : kFcEditorPictureTip;
                out.fcedit_any = true;
            } else if (!file.empty()) out.src[k] = vis::is_colour_file(file) ? kSrcSwatch : kSrcRdbm;
            else if (out.swatch[k]) out.src[k] = kSrcSwatch;
            else if (!gp.empty()) out.src[k] = kSrcGame;
            else out.src[k] = ((!have_res || !d) && !gal && !fe_field) ? kSrcNames : kSrcBlank;
            // 5.0c E10 lit capture (brief D3): a hair / beard with no game picture takes Turbo's own lit capture
            // (<turbo_output>\cache\hairbeard_thumbs\hair_<id>.png / beard_<id>.png, core/hairbeard_thumbs.h) as its LAST source
            // before the drawn class icon below. The game's preview and a lit RDBM26 picture still win (they are set above).
            if (out.src[k] == kSrcBlank && code > 0 && (field == "hairtypecode" || field == "facialhairtypecode")) {
                hairbeard_thumbs::configure(app.turbo_output());
                const fs::path lit = hairbeard_thumbs::thumb(field == "hairtypecode" ? hairbeard_thumbs::Field::Hair : hairbeard_thumbs::Field::Beard, code);
                if (!lit.empty()) {
                    out.files[k] = lit;
                    out.src[k] = kSrcRdbm;
                    out.tip[k] = "Turbo's lit capture of the 3D model. ";
                }
            }
            if (out.src[k] == kSrcBlank) {  // 5.0c E10-12 (D4): every grey tile says why it has no picture (hover); step 7: plain words, no data-state tags
                const auto gi = gpos.find(code);
                const GalleryEntry* e = gi != gpos.end() ? &ge[gi->second] : nullptr;
                if (code == 0 && gal) {
                    out.cap[k] = "none";
                    out.reason[k] = field == "hairtypecode" ? "Hair 0 is bald." : tattoo_area >= 0 ? "No tattoo." : "No facial hair.";
                } else if (palette_only) {
                    out.reason[k] = "No picture of this one yet (RDBM26 has only a strand map for it, and the game ships no preview).";
                } else if (e && e->model_only) {
                    out.reason[k] = "The game ships a 3D model for this code but no picture of it.";
                } else if (gal && out.catalog) {
                    out.reason[k] = "No picture of this one: it is not in the game's asset catalogue.";
                } else if (gal) {
                    out.reason[k] = "No picture of this one: the game's picture list has no preview for it.";
                } else if (fe_field) {
                    out.reason[k] = have_res && d ? "No picture of this one in RDBM26 or your FC Editor folder." : "No picture of this one in your FC Editor folder.";
                } else {
                    out.reason[k] = "No picture of this one in RDBM26.";
                }
                if (gal && (field == "hairtypecode" || field == "facialhairtypecode")) {  // E10 fix round: the look class stands in for the missing picture
                    out.icon[k] = field == "hairtypecode" ? hair_icon(code) : beard_icon(code);
                    out.cap[k].clear();
                    out.reason[k] += " The tile shows its kind of look (" +
                                     (field == "hairtypecode" ? labels::hair_text(code) : labels::facial_hair_text(code)) +
                                     "), not the model itself.";
                }
            }
            if (out.eyebrow && !file.empty()) out.tip[k] = "style picture";
            if (!light) {
                const auto gi = gpos.find(code);
                if (gi != gpos.end()) out.hidden[k] = ge[gi->second].hidden;  // the scope filter (All / In this save / Hidden) still reads it
                if (gal && code != current) {
                    std::string why;
                    if (!gallery_write_ok(app, field, manager, code, &why)) out.writable[k] = 0, out.why[k] = why;
                }
                if (out.eyebrow) out.group[k] = group_index(out, "Style " + std::to_string(vis::eyebrow_style(code)));
                else if (ghead) out.group[k] = group_index(out, head_model(own && own->name ? own->name : ""));
                else if (tattoo_area >= 0) {
                    // the save's tattoo table says which areas a tattoo is made for; one the table does not know (the game files only, or
                    // none) is offered for every area, as the old picker did
                    const bool known = gi != gpos.end() && ge[gi->second].areas != 0;
                    const bool fits = !known || (ge[gi->second].areas & (1u << tattoo_area)) != 0;
                    out.group[k] = code == 0 || fits ? g_this_area : g_other_area;
                }
            }
        }
    }
    {
        size_t fe = 0;
        for (uint8_t b : out.fcedit) fe += b;
        if (!light) g_fcedit_count = fe;
        if (fe && !light && out.note.empty()) out.note = std::to_string(fe) + " tiles show a lit preview converted from your FC Editor install (local only, not shipped).";
    }
    for (size_t i = 0; i < out.codes.size(); ++i) {
        std::string q = fold_search(out.names[i]) + " " + std::to_string(out.codes[i]);
        if (field == "hairtypecode" && out.codes[i] > 0) q += " " + fold_search(hair::describe(hair::lookup(out.codes[i])));
        if (out.group[i] >= 0) q += " " + fold_search(out.groups[static_cast<size_t>(out.group[i])]);
        out.search.push_back(std::move(q));
    }
    return true;
}

// Step 7: the tiles of `in` whose code is in `keep` (in `in`'s order) and every tile writable: the 3D section's chooser lists the band's
// generic ids, and the band list itself is the gate (ui_faces3d_gate.cpp writes the band code of the pick).
// Turbo X bald: the chooser of a real face also has two tiles of its own, first: code 0 named `zero_name` ("Bald" / "No beard": the
// game's generic none part, written as faces3d::kHairBaldCode / kBeardNoneCode) with the game gallery's preview of 0 when it has one,
// else a blank box; and `own_code` named `own_name` ("His scanned hair" / "His scanned beard": no generic part), a blank "his own" box.
// own_name == nullptr: no own tile. Code 0 is offered even when `in` has no tile for it.
Source subset_source(const Source& in, const std::vector<int64_t>& keep, const char* zero_name, int64_t own_code, const char* own_name) {
    Source o;
    o.field = in.field;
    o.title = in.title;
    o.note = in.note;
    o.kind = in.kind;
    o.body = in.body;
    o.gallery = in.gallery;
    o.catalog = in.catalog;
    o.manager = in.manager;
    o.eyebrow = in.eyebrow;
    o.fcedit_any = in.fcedit_any;
    o.groups = in.groups;
    o.zero_game = in.zero_game;
    const bool hair = in.field == "hairtypecode";
    // the two tiles of the chooser itself
    push_tile(o, 0, zero_name ? zero_name : (hair ? "Bald" : "None"));
    o.search.push_back(fold_search(o.names.back()) + " 0");
    if (!in.zero_game.empty()) {
        o.game.back() = in.zero_game;
        o.src.back() = kSrcGame;
    } else {
        o.reason.back() = hair ? "Bald: the game ships no picture of hair 0." : "No beard: the game ships no picture of beard 0.";
    }
    if (own_name) {
        push_tile(o, own_code, own_name);
        o.search.push_back(fold_search(o.names.back()) + " own scan");
        o.cap.back() = "his own";
        o.reason.back() = hair ? "His scanned hair: no generic model, the hair of his scan stays." : "His scanned beard: no generic model, the beard of his scan stays.";
    }
    std::unordered_set<int64_t> want(keep.begin(), keep.end());
    for (size_t i = 0; i < in.codes.size(); ++i) {
        const int64_t code = in.codes[i];
        if (!want.count(code) || o.pos.count(code)) continue;  // 0 and his own are the chooser's tiles above
        o.pos[code] = static_cast<int>(o.codes.size());
        o.codes.push_back(code);
        o.names.push_back(in.names[i]);
        o.search.push_back(in.search[i]);
        o.files.push_back(in.files[i]);
        o.game.push_back(in.game[i]);
        o.swatch.push_back(in.swatch[i]);
        o.src.push_back(in.src[i]);
        o.shape.push_back(in.shape[i]);
        o.writable.push_back(1);
        o.hidden.push_back(0);
        o.why.emplace_back();
        o.tip.push_back(in.tip[i]);
        o.icon.push_back(in.icon[i]);
        o.fcedit.push_back(in.fcedit[i]);
        o.cap.push_back(in.cap[i]);
        o.reason.push_back(in.reason[i]);
        o.group.push_back(in.group[i]);
    }
    return o;
}

// 1.0.3 RF: every tile of the real-face chooser (but his own scan) takes the real-face render or the "not rendered" placeholder.
// Returns how many tiles have a render.
size_t realface_tiles(App& app, Source& o, int64_t own_code) {
    size_t have = 0;
    const bool hair = o.field == "hairtypecode";
    for (size_t k = 0; k < o.codes.size(); ++k) {
        const int64_t code = o.codes[k];
        if (code == own_code) continue;
        const std::string rf = fcedit::realface_picture(app, o.field, code);
        o.game[k].clear();
        o.icon[k] = 0;
        o.fcedit[k] = 0;
        o.swatch[k] = 0;
        if (!rf.empty()) {
            o.src[k] = kSrcFcEditor;
            o.files[k] = fs::u8path(rf);
            o.tip[k] = kRealFacePictureTip;
            o.cap[k].clear();
            o.reason[k].clear();
            ++have;
        } else {
            o.src[k] = kSrcBlank;
            o.files[k].clear();
            o.tip[k].clear();
            o.cap[k] = kRealFaceMissingCap;
            o.reason[k] = std::string("Not rendered on a real head yet: the game has not drawn this ") + (code == 0 ? (hair ? "bald look" : "clean shave") : hair ? "hair" : "beard") +
                          " on a scanned face (pictures\\realface). Generic-head and 2D pictures are not shown here. It can still be chosen.";
        }
    }
    o.fcedit_any = have > 0;
    return have;
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

// 5.0c E10 (fix round): the drawn class icon of a hair / beard tile (see hair_icon). A head, the hair (length, type, accessory) or the
// facial hair class drawn on it, in s x s at p0. Pictogram only: no texture, no file, the same for the same class.
void draw_look_icon(ImDrawList* dl, ImVec2 p0, float s, uint32_t icon) {
    const uint32_t kind = (icon >> 28) & 7u, a = (icon >> 16) & 255u, ty = (icon >> 8) & 255u, acc = icon & 255u;
    const ImU32 bg = IM_COL32(66, 72, 84, 255), skin = IM_COL32(226, 188, 154, 255), hairc = IM_COL32(52, 38, 32, 255),
                edge = IM_COL32(205, 205, 214, 255), band = IM_COL32(214, 92, 84, 255), eye = IM_COL32(40, 40, 46, 255);
    dl->AddRectFilled(p0, ImVec2(p0.x + s, p0.y + s), bg, 4.0f);
    const float cx = p0.x + 0.5f * s, cy = p0.y + 0.52f * s, r = 0.22f * s, kPi = 3.14159265f;
    auto cap = [&](float centre_y, float rad, bool fill, float thick) {  // the upper half disc: a filled cap or a thin arc
        dl->PathArcTo(ImVec2(cx, centre_y), rad, kPi, 2.0f * kPi, 28);
        if (fill) dl->PathFillConvex(hairc);
        else dl->PathStroke(hairc, thick);
    };
    const bool hair_kind = kind == 0;
    if (hair_kind) {  // behind the head
        if (ty == hair::kAfro) {
            dl->AddCircleFilled(ImVec2(cx, cy - 0.09f * s), 0.31f * s, hairc, 32);
        } else if (a == hair::kMedium) {
            dl->AddRectFilled(ImVec2(cx - 1.12f * r, cy - 0.9f * r), ImVec2(cx + 1.12f * r, cy + 0.55f * r), hairc, 0.5f * r);
        } else if (a == hair::kLong) {
            dl->AddRectFilled(ImVec2(cx - 1.12f * r, cy - 0.9f * r), ImVec2(cx + 1.12f * r, cy + 1.75f * r), hairc, 0.5f * r);
        }
    }
    dl->AddCircleFilled(ImVec2(cx, cy), r, skin, 32);
    dl->AddCircle(ImVec2(cx, cy), r, edge, 32, 1.0f);
    dl->AddCircleFilled(ImVec2(cx - 0.4f * r, cy - 0.12f * r), 0.075f * r, eye, 10);
    dl->AddCircleFilled(ImVec2(cx + 0.4f * r, cy - 0.12f * r), 0.075f * r, eye, 10);
    if (hair_kind) {
        const float top = cy - 0.06f * s;
        if (a == hair::kBald) {
            dl->PathArcTo(ImVec2(cx - 0.02f * s, cy - 0.02f * s), r * 0.82f, kPi * 1.15f, kPi * 1.42f, 8);  // a shine on the scalp
            dl->PathStroke(IM_COL32(255, 255, 255, 190), 1.5f);
        } else if (ty == hair::kMohawk) {
            dl->AddRectFilled(ImVec2(cx - 0.05f * s, cy - 1.5f * r), ImVec2(cx + 0.05f * s, cy - 0.55f * r), hairc, 2.0f);
        } else if (ty == hair::kAfro) {
            cap(top, r * 1.12f, true, 0);
        } else if (a == hair::kBuzz) {
            cap(top, r * 1.02f, false, 0.05f * s);
        } else {
            cap(top, r * 1.1f, true, 0);
            if (ty == hair::kCurly || ty == hair::kWavy)
                for (int i = 0; i < 6; ++i) {
                    const float ang = kPi * (1.1f + 0.16f * i);
                    dl->AddCircleFilled(ImVec2(cx + std::cos(ang) * r * 1.1f, top + std::sin(ang) * r * 1.1f), 0.05f * s, hairc, 10);
                }
            if (ty == hair::kSpiky)
                for (int i = 0; i < 5; ++i) {
                    const float x = cx + (i - 2) * 0.075f * s, y = top - r * 0.95f;
                    dl->AddTriangleFilled(ImVec2(x - 0.04f * s, y + 0.03f * s), ImVec2(x + 0.04f * s, y + 0.03f * s), ImVec2(x, y - 0.08f * s), hairc);
                }
            if (ty == hair::kBraids || ty == hair::kDreads || ty == hair::kTwists)
                for (int i = 0; i < 4; ++i) {
                    const float x = cx + (i < 2 ? -1.0f : 1.0f) * (1.0f - 0.14f * (i % 2)) * r;
                    dl->AddLine(ImVec2(x, top), ImVec2(x, cy + (a == hair::kLong ? 1.5f : 0.8f) * r), hairc, 0.045f * s);
                }
            if (ty == hair::kTied) dl->AddCircleFilled(ImVec2(cx, top - r * 1.16f), 0.075f * s, hairc, 14);
            if (ty == hair::kSlicked) dl->AddLine(ImVec2(cx - 0.9f * r, top + 0.15f * r), ImVec2(cx + 0.7f * r, top - 0.45f * r), edge, 1.0f);
        }
        if (acc == hair::kHeadband || acc == hair::kHairband)
            dl->AddLine(ImVec2(cx - 1.02f * r, cy - 0.5f * r), ImVec2(cx + 1.02f * r, cy - 0.5f * r), band, acc == hair::kHeadband ? 0.07f * s : 0.04f * s);
        if (acc == hair::kHairTie) dl->AddCircleFilled(ImVec2(cx + 0.95f * r, cy - 0.45f * r), 0.04f * s, band, 10);
        if (acc == hair::kHat) {
            dl->PathArcTo(ImVec2(cx, cy - 0.35f * r), r * 1.12f, kPi, 2.0f * kPi, 28);
            dl->PathFillConvex(band);
            dl->AddLine(ImVec2(cx - 1.5f * r, cy - 0.35f * r), ImVec2(cx + 1.5f * r, cy - 0.35f * r), band, 0.04f * s);
        }
    } else {
        cap(cy - 0.06f * s, r * 1.06f, true, 0);  // a short haircut so the head reads as a head; the beard is the subject
        const float mouth = cy + 0.5f * r;
        if (a == 1) {  // stubble: dots over the lower face
            for (int i = 0; i < 34; ++i) {
                const float u = std::fmod(i * 0.618034f, 1.0f), v = std::fmod(i * 0.381966f + 0.07f * (i % 5), 1.0f);
                const float x = cx + (u * 2.0f - 1.0f) * 0.82f * r, y = cy + 0.25f * r + v * 0.75f * r;
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r * 0.9f) dl->AddCircleFilled(ImVec2(x, y), 0.012f * s, hairc, 6);
            }
        } else if (a == 2) {  // moustache (and a goatee)
            dl->AddRectFilled(ImVec2(cx - 0.55f * r, mouth - 0.28f * r), ImVec2(cx + 0.55f * r, mouth - 0.06f * r), hairc, 0.12f * r);
            dl->AddCircleFilled(ImVec2(cx, cy + 0.88f * r), 0.14f * r, hairc, 12);
        } else if (a >= 3) {  // short / full beard: the lower face, a mouth left open
            const float rr = a == 3 ? r * 0.98f : r * 1.08f, c0 = a == 3 ? cy + 0.12f * r : cy - 0.02f * r;
            dl->PathArcTo(ImVec2(cx, c0), rr, 0.0f, kPi, 28);
            dl->PathFillConvex(hairc);
            dl->AddRectFilled(ImVec2(cx - 0.3f * r, mouth - 0.02f * r), ImVec2(cx + 0.3f * r, mouth + 0.22f * r), skin, 0.1f * r);
            dl->AddRectFilled(ImVec2(cx - 0.55f * r, mouth - 0.3f * r), ImVec2(cx + 0.55f * r, mouth - 0.06f * r), hairc, 0.1f * r);
        } else {  // clean-shaven: a mouth line
            dl->AddLine(ImVec2(cx - 0.3f * r, mouth), ImVec2(cx + 0.3f * r, mouth), eye, 1.2f);
        }
    }
}

// Step 7: a tile with no picture (hidden by default behind "Show N without picture"): the look-class icon of a hair / beard (5.0c E10
// fix round: the kind of look, not the model; its tooltip says so), else a grey box. No data-state words on the tile.
void draw_blank(const Source& src, size_t k, ImVec2 a, ImVec2 box) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float side = std::min(box.x, box.y);
    const ImVec2 p0(a.x + (box.x - side) * 0.5f, a.y + (box.y - side) * 0.5f);
    if (k < src.icon.size() && (src.icon[k] & kIconValid)) {
        draw_look_icon(dl, p0, side, src.icon[k]);
        return;
    }
    dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(58, 58, 64, 255), 4.0f);
    box_text(dl, p0, ImVec2(side, side), !src.cap[k].empty() ? src.cap[k].c_str() : src.kind == vis::data::kGif ? "no animation" : "no picture");
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
            // Turbo X: a plain cursor move back (the tile's own item follows in the caller). A zero-size item here broke the grid:
            // the next SameLine tile was placed after it, so tiles of one row stacked on top of each other (owner's screenshot 10-31)
            ImGui::SetCursorScreenPos(saved);
            return true;
        }
        case kSrcFigure: {
            const float side = std::min(box.x, box.y);
            if (large && !src.files[k].empty() && draw_still(app, src.files[k], 320, a, box, true)) return true;
            bodyfig::draw_body_figure(ImGui::GetWindowDrawList(), ImVec2(a.x + (box.x - side) * 0.5f, a.y), ImVec2(side, side), src.shape[k], false, false);
            return true;
        }
        case kSrcFcEditor: {  // Turbo X: FC Editor's asset picture; a body type keeps its figure while the file loads (or when it cannot be read)
            if (!src.body) return draw_still(app, src.files[k], large ? 320 : 128, a, box);
            const float side = std::min(box.x, box.y);
            if (draw_still(app, src.files[k], large ? 320 : 128, a, box, true)) return true;
            bodyfig::draw_body_figure(ImGui::GetWindowDrawList(), ImVec2(a.x + (box.x - side) * 0.5f, a.y), ImVec2(side, side), src.shape[k], false, false);
            return true;
        }
        case kSrcBlank:
            draw_blank(src, k, a, box);
            return (src.icon[k] & kIconValid) != 0;  // a drawn class icon is the tile's picture (5.0c E10 fix round)
        case kSrcNames:
            box_text(ImGui::GetWindowDrawList(), a, box, "no picture");
            return false;
        default:
            break;
    }
    const fs::path& f = src.files[k];
    if (f.empty()) {
        draw_blank(src, k, a, box);
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
    if (src.fcedit_any) return false;  // Turbo X: FC Editor's pictures make it a picture grid
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

// R2-F (owner: filter permanence): the grid's filters are kept PER FIELD in gui_settings.json (picker_filters.<field>, the store that
// keeps asset_scope and favourites): "Show N without picture", "Favourites only", the group combo (by its name) and the hair facets.
// The search text, the hover and the selection still start fresh at every open. Nothing is written until the person changes a filter;
// with nothing stored for the field the defaults stand (a tattoo picker on "Made for this area"). The 3D chooser keeps its own entry.
std::string g_filters_key;   // picker_filters.<key> of the open picker
std::string g_filters_seen;  // its filters as opened or last saved (json dump): filter() saves a change from it

std::string filters_store_key(const std::string& field, bool choice) { return choice ? field + "@3d" : field; }

nlohmann::json filters_json(const State& s) {
    nlohmann::json j = nlohmann::json::object();
    j["show_blank"] = s.show_blank;
    j["fav_only"] = s.fav_only;
    j["group"] = s.group_sel >= 0 && static_cast<size_t>(s.group_sel) < s.src.groups.size() ? s.src.groups[static_cast<size_t>(s.group_sel)] : std::string();
    j["hair"] = {{"length", s.hair.length}, {"type", s.hair.type}, {"accessory", s.hair.accessory}};
    return j;
}

// the stored filters of `key` over the defaults already in `s` (a missing or malformed entry keeps its default)
void restore_filters(const App& app, State& s, const std::string& key) {
    const nlohmann::json& g = app.gui_settings;
    if (!g.is_object() || !g.contains("picker_filters") || !g["picker_filters"].is_object()) return;
    const nlohmann::json& all = g["picker_filters"];
    if (!all.contains(key) || !all[key].is_object()) return;
    const nlohmann::json& j = all[key];
    if (j.contains("show_blank") && j["show_blank"].is_boolean()) s.show_blank = j["show_blank"].get<bool>();
    if (j.contains("fav_only") && j["fav_only"].is_boolean()) s.fav_only = j["fav_only"].get<bool>();
    if (j.contains("group") && j["group"].is_string()) {
        const std::string name = j["group"].get<std::string>();
        if (name.empty()) s.group_sel = -1;
        for (size_t g2 = 0; g2 < s.src.groups.size(); ++g2)
            if (!name.empty() && s.src.groups[g2] == name) s.group_sel = static_cast<int>(g2);
    }
    if (j.contains("hair") && j["hair"].is_object()) {
        const nlohmann::json& h = j["hair"];
        auto facet = [&h](const char* k, int& v, int n) {
            if (h.contains(k) && h[k].is_number_integer()) {
                const int x = h[k].get<int>();
                if (x >= -1 && x < n) v = x;
            }
        };
        facet("length", s.hair.length, hair::kLengthCount);
        facet("type", s.hair.type, hair::kTypeCount);
        facet("accessory", s.hair.accessory, hair::kAccessoryCount);
    }
}

// filter(): a filter of the open picker changed (not the search): keep it for the field
void persist_filters(App& app, const State& s) {
    if (!s.ui.open || g_filters_key.empty()) return;
    const nlohmann::json j = filters_json(s);
    const std::string d = j.dump();
    if (d == g_filters_seen) return;
    g_filters_seen = d;
    if (!app.gui_settings.is_object()) app.gui_settings = nlohmann::json::object();
    nlohmann::json& all = app.gui_settings["picker_filters"];
    if (!all.is_object()) all = nlohmann::json::object();
    all[g_filters_key] = j;
    if (!app.save_gui_settings()) vlog(app, "visual picker: the filters could not be saved");
}

// `choice` (step 7): the 3D section's chooser (vp::open_choice); nullptr = the field's own picker, Apply writes through visual_apply
void open_picker(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager, const State::Choice* choice = nullptr) {
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
    s.show_blank = false;
    s.blank_count = 0;
    s.choice = choice ? *choice : State::Choice();
    // Turbo X (2026-10-31 review): the band chooser lists a short gate of 3D models; one with no preview is still a choice, so the
    // chooser starts with them shown (the tick can hide them). The big pickers keep starting with the blank tiles hidden.
    s.show_blank = s.choice.on;
    // 5.0c RV4: the Body type window starts as the person left the "Show all body types" tick (default off = one tile per distinct figure)
    if (field == "bodytypecode") looks::body_show_all(app.prefs.body_show_all);
    build_source(app, t, rec, field, manager, s.src);
    if (s.choice.on) {
        // the band's generic ids only, plus Bald / No beard (code 0, the game's generic none part) and his scanned hair / beard (the "his
        // own" code: no generic part); the 3D section writes the band code (or the bald / no-beard code), so the list is the gate
        const bool hair = field == "hairtypecode";
        s.src = subset_source(s.src, s.choice.ids, hair ? "Bald" : "No beard", hair ? faces3d::kHairOwnCode : faces3d::kBeardOwnCode,
                              hair ? "His scanned hair" : "His scanned beard");
        s.src.title = (hair ? std::string("Hair") : std::string("Beard")) + " (3D, real face)";
        s.src.note = hair ? "Real face: a new style replaces the hair painted into his scan. 0 = Bald; \"His scanned hair\" keeps the hair of his scan."
                          : "Real face: a new beard replaces the stubble painted into his scan. 0 = No beard; \"His scanned beard\" keeps the beard of his scan.";
        // 1.0.3 RF: the pictures are the game's renders on a real head only (realface_tiles above)
        const size_t rendered = realface_tiles(app, s.src, hair ? faces3d::kHairOwnCode : faces3d::kBeardOwnCode);
        const size_t listed = s.src.codes.size() >= 1 ? s.src.codes.size() - 1 : 0;  // all but his own scan
        s.src.note += " Pictures: " + std::to_string(rendered) + " of " + std::to_string(listed) + " rendered by FC 27 on a real head; the rest say \"" +
                      kRealFaceMissingCap + "\".";
    }
    // step 7: a tattoo picker opens on the tattoos made for its body area (the combo lists the others too)
    if (tattoo_area_index(field) >= 0)
        for (size_t g = 0; g < s.src.groups.size(); ++g)
            if (s.src.groups[g] == "Made for this area") s.group_sel = static_cast<int>(g);
    // R2-F: the field's kept filters over those defaults (search, hover and selection stay fresh)
    g_filters_key = filters_store_key(field, s.choice.on);
    restore_filters(app, s, g_filters_key);
    g_filters_seen = filters_json(s).dump();
    s.ui = VisualPickerUi();
    s.ui.open = true;
    s.ui.choice = s.choice.on;
    if (field == "bodytypecode") bodytype_reset_risky();  // the "allow a player-specific body" box starts unticked for every record
    s.ui.field = field;
    s.ui.current = s.choice.on ? s.choice.current : app.db.get_int(t, rec, field, 0);
    s.ui.selected = s.ui.current;
    s.ui.names_only = names_only(app, s.src);
    for (uint8_t k : s.src.src) {
        if (k == kSrcRdbm) ++s.ui.source_rdbm;
        else if (k == kSrcSwatch) ++s.ui.source_swatch;
        else if (k == kSrcGame) ++s.ui.source_game;
        else if (k == kSrcFigure) ++s.ui.source_figure;
        else if (k == kSrcFcEditor) ++s.ui.source_fcedit;
        else ++s.ui.source_blank;
    }
    for (uint32_t ic : s.src.icon) s.ui.source_drawn += (ic & kIconValid) != 0;
    s.popup_pending = true;
    s.owner_set = false;
    s.want_open = false;
    vlog(app, "visual picker opened: " + field + " of " + t.name + " " + std::to_string(who_id(app, t, rec)) + " is " + std::to_string(s.ui.current) +
                  " (" + std::to_string(s.ui.source_rdbm) + " RDBM26 pictures, " + std::to_string(s.ui.source_swatch) + " swatches, " +
                  std::to_string(s.ui.source_game) + " game previews, " + std::to_string(s.ui.source_blank) + " without a game picture, " + std::to_string(s.ui.source_drawn) + " of those drawn from their class; " + std::to_string(s.ui.source_fcedit) + " FC Editor pictures, " + std::to_string(g_fcedit_count) + " of the pictures are FC Editor previews)");
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

// Step 7: the one-line tooltip of a greyed tile (the catalog gate refused it; the rule is unchanged, the words are plain)
const char* cannot_apply_text(const Source& src) { return src.manager ? "Cannot be applied to this manager" : "Cannot be applied to this player"; }

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
    if (io.hovered) {
        // step 7: a greyed tile says only that it cannot be applied (one line; the footer names the rule's reason once it is picked);
        // the others: the full name when the tile cuts it, why there is no picture, where the picture comes from. No data-state tags.
        std::string tip;
        if (!src.writable[k]) {
            tip = cannot_apply_text(src);
        } else {
            if (name.size() > 18) tip = name;
            if (src.src[k] == kSrcBlank && !src.reason[k].empty()) tip += (tip.empty() ? "" : "\n") + src.reason[k];
            if (!src.tip[k].empty()) tip += (tip.empty() || src.tip[k][0] == '\n' ? "" : "\n") + src.tip[k];
        }
        if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
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
    std::snprintf(b, sizeof(b), "|%d|%d|%d|%d|%d|%d|%d|", s.fav_only ? 1 : 0, s.group_sel, s.scope, s.hair.length, s.hair.type, s.hair.accessory,
                  s.show_blank ? 1 : 0);
    return std::string(s.search) + b;
}

// Step 7: a tile the grid hides unless "Show N without picture" is ticked: no picture from any source (a names-only list has no toggle:
// every tile is a name there). The current value is always shown, so the person sees what he has.
bool blank_tile(const State& s, size_t i) {
    const int64_t c = s.src.codes[i];
    // Turbo X bald: the real-face chooser's own tiles (Bald / No beard, his scanned hair / beard) are choices, never "without picture"
    if (s.choice.on && (c == 0 || c == faces3d::kHairOwnCode || c == faces3d::kBeardOwnCode)) return false;
    return !s.ui.names_only && s.src.src[i] == kSrcBlank && c != s.ui.current;
}

void filter(App& app, State& s) {
    const std::string key = filter_key(s);
    if (s.shown_key == key) return;
    s.shown_key = key;
    persist_filters(app, s);  // R2-F
    s.shown.clear();
    s.blank_count = 0;
    const std::string q = fold_search(s.search);
    const Source& src = s.src;
    const bool is_hair = src.field == "hairtypecode";
    for (size_t i = 0; i < src.codes.size(); ++i) {
        if (!q.empty() && src.search[i].find(q) == std::string::npos) continue;
        if (s.group_sel >= 0 && src.group[i] != s.group_sel) continue;
        if (s.fav_only && !(src.gallery && gallery_favourite(app, src.field, src.codes[i]))) continue;
        if (src.gallery && !gallery_scope_shows(s.scope, src.hidden[i]) && src.codes[i] != s.ui.current) continue;
        if (is_hair && !hair_matches(s.hair, src.codes[i])) continue;
        if (blank_tile(s, i)) {
            ++s.blank_count;
            if (!s.show_blank) continue;
        }
        s.shown.push_back(static_cast<int>(i));
    }
    s.ui.without_picture = s.blank_count;
    s.ui.show_without_picture = s.show_blank;
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
    if (!s.choice.on) s.ui.current = app.db.get_int(t, rec, s.src.field, 0);  // choice mode: the generic id his band code selects (open_choice)
    ImGui::SetNextItemWidth(S(260.0f));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##vpsearch", "find by name or number", s.search, sizeof(s.search));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu to choose from", s.src.codes.size());
    {  // Turbo X: where the tiles' pictures come from (counted over the whole list, whatever the filters hide)
        int game = 0, fce = 0, rdbm = 0, none = 0, figure = 0, swatch = 0;
        for (uint8_t k : s.src.src) {
            if (k == kSrcGame) ++game;
            else if (k == kSrcFcEditor) ++fce;
            else if (k == kSrcRdbm) ++rdbm;
            else if (k == kSrcFigure) ++figure;
            else if (k == kSrcSwatch) ++swatch;
            else ++none;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("pictures: game %d, FC Editor %d, RDBM26 %d, none %d", game, fce, rdbm, none);
        if (figure || swatch) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d drawn %s)", figure ? figure : swatch, figure ? "figures" : "swatches");
        }
    }
    if (!s.src.groups.empty()) {  // eyebrow styles, generic head models, body types, tattoo areas
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(190.0f));
        const bool tattoo = tattoo_area_index(s.src.field) >= 0;
        const char* all = s.src.body ? "All body types" : s.src.eyebrow ? "All styles" : tattoo ? "All tattoos" : "All models";
        const char* prev = s.group_sel < 0 ? all : s.src.groups[static_cast<size_t>(s.group_sel)].c_str();
        if (ImGui::BeginCombo("##vpgroup", prev)) {
            if (ImGui::Selectable(all, s.group_sel < 0)) s.group_sel = -1;
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
    if (s.src.body) bodytype_risky_box(app, t, rec);  // 5.0c E8-body: the old gallery's "allow a player-specific body" box lives here now
    if (s.src.body && s.src.manager) {
        // 5.0c RV4 (Cassio): look-alike manager bodies (82/83/86, 81/89) are one tile; ticked, every code is listed again (recoverable)
        bool all = looks::body_show_all();
        if (ImGui::Checkbox("Show all body types##vpallbodies", &all)) {
            looks::body_show_all(all);
            app.prefs.body_show_all = all;  // saved with the layout (gui_settings.json gui.body_show_all)
            if (!app.save_gui_settings()) vlog(app, "visual picker: Show all body types could not be saved");
            const std::string rv4_field = s.src.field;  // a copy: build_source resets s.src first
            const bool rv4_mgr = s.src.manager;
            build_source(app, t, rec, rv4_field, rv4_mgr, s.src);
            s.shown_key = "\x01";
            s.ui.source_figure = static_cast<int>(s.src.codes.size());
            vlog(app, std::string("visual picker: Show all body types ") + (all ? "on" : "off") + ", " + std::to_string(s.src.codes.size()) + " tiles");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Some manager body types draw the same figure and are shown once. Tick to list every body type. Saved with your layout.");
    }
    if ((s.ui.names_only || (!s.src.body && resources(app).empty())) && s.src.kind != vis::data::kNone) where_pictures_line(app, s.src);
    if (!s.src.note.empty()) ImGui::TextDisabled("%s", s.src.note.c_str());
    filter(app, s);
    if (s.src.field == "hairtypecode") ImGui::TextDisabled("%zu of %zu styles match", s.shown.size(), s.src.codes.size());
    // step 7: tiles with no picture are hidden by default; the count is on screen whenever any are hidden (and while they are shown).
    // Turbo X (2026-10-31): nothing at zero ("Show 0 without picture" was drawn in choice mode, where show_blank starts on); the state stays
    if (s.blank_count > 0) {
        if (s.src.field == "hairtypecode") ImGui::SameLine();
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "Show %d without picture###vpblank", s.blank_count);
        ImGui::Checkbox(lbl, &s.show_blank);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d of the choices have no picture from any source (the game, RDBM26, your FC Editor previews). Tick to list them too.", s.blank_count);
        filter(app, s);  // the grid below shows the toggle's state in this frame
    }

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
            if (!s.src.writable[static_cast<size_t>(idx)]) ImGui::TextDisabled("%s", cannot_apply_text(s.src));
        }
        ImGui::EndChild();
    }

    // Apply / Cancel
    const int pi = s.src.index_of(s.ui.selected);
    const bool can_write = pi < 0 || s.src.writable[static_cast<size_t>(pi)];
    if (!can_write) ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "%s: %s", cannot_apply_text(s.src), s.src.why[static_cast<size_t>(pi)].c_str());
    const bool changed = s.ui.selected != s.ui.current;
    ImGui::BeginDisabled(!changed || !can_write);
    if (ImGui::Button("Apply##vpapply")) {  // the one label in both modes (tests/native/gui_labels.golden.txt keys the id on it)
        if (visual_picker_apply(app)) {
            if (!s.choice.on) app.notify(s.ui.last_result);
            ImGui::CloseCurrentPopup();
            close_picker();
        }
    }
    ImGui::EndDisabled();
    if (s.choice.on && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Apply hands the pick to the Hair & beard (3D) section. Nothing is written until its \"Apply hair & beard\".");
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
    modal_size::set_next("vispicker", ImVec2(1000.0f, 680.0f));  // 5.0c E6: reopens at the size the person left it at
    bool keep = true;
    const std::string title = s.src.title + "###vispicker";
    if (ImGui::BeginPopupModal(title.c_str(), &keep, ImGuiWindowFlags_NoSavedSettings)) {
        modal_size::track("vispicker", ImVec2(1000.0f, 680.0f));
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
    fs::path fe;  // Turbo X: the FC Editor folder the light list was built with
};

RowCache& row_cache(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager) {
    static std::unordered_map<std::string, RowCache> cache;
    const int64_t v = app.db.get_int(t, rec, field, 0);
    RowCache& rc = cache[field];
    const fs::path& res = resources(app);
    const fs::path fe = fcedit::folder(app);
    if (rc.table != t.name || rc.rec != rec || rc.value != v || rc.res != res || rc.fe != fe || rc.manager != manager) {
        build_source(app, t, rec, field, manager, rc.src, v);
        rc.table = t.name;
        rc.rec = rec;
        rc.value = v;
        rc.res = res;
        rc.fe = fe;
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

std::string visual_picker_tile_reason(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i < 0 || src.src[static_cast<size_t>(i)] != kSrcBlank ? std::string() : src.reason[static_cast<size_t>(i)];
}

// TX-102 R2-P: the picture the tile AND the large preview draw (draw_picture: the game's preview for a Game tile, else the file), and the tooltip
std::string visual_picker_tile_picture(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    if (i < 0) return {};
    const size_t k = static_cast<size_t>(i);
    return src.src[k] == kSrcGame ? src.game[k] : src.files[k].u8string();
}
std::string visual_picker_tile_tip(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i < 0 ? std::string() : src.tip[static_cast<size_t>(i)];
}

bool visual_picker_tile_drawn(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i >= 0 && (src.icon[static_cast<size_t>(i)] & kIconValid) != 0;
}

uint32_t visual_picker_tile_icon(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i < 0 ? 0u : src.icon[static_cast<size_t>(i)];
}

std::string visual_picker_tile_name(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i < 0 ? std::string() : src.names[static_cast<size_t>(i)];
}

size_t visual_picker_png_probes() { return g_png_probes; }
size_t visual_picker_source_fcedit() { return g_fcedit_count; }
std::string visual_picker_tile_note(int64_t code) {
    const Source& src = st().src;
    const int i = src.index_of(code);
    return i < 0 || !src.fcedit[static_cast<size_t>(i)] ? std::string() : std::string(kFcEditPreviewNote);
}
size_t visual_picker_list_size() { return st().src.codes.size(); }
std::vector<int64_t> visual_picker_codes() { return st().src.codes; }
std::vector<int64_t> visual_picker_shown_codes() {
    const State& s = st();
    std::vector<int64_t> out;
    for (int i : s.shown) out.push_back(s.src.codes[static_cast<size_t>(i)]);
    return out;
}

VisualPickerFilters visual_picker_filters() {
    const State& s = st();
    VisualPickerFilters f;
    f.show_blank = s.show_blank;
    f.fav_only = s.fav_only;
    if (s.group_sel >= 0 && static_cast<size_t>(s.group_sel) < s.src.groups.size()) f.group = s.src.groups[static_cast<size_t>(s.group_sel)];
    f.hair_length = s.hair.length;
    f.hair_type = s.hair.type;
    f.hair_accessory = s.hair.accessory;
    f.search = s.search;
    f.groups = s.src.groups;
    return f;
}
void visual_picker_set_filters(const VisualPickerFilters& f) {
    State& s = st();
    s.show_blank = f.show_blank;
    s.fav_only = f.fav_only;
    s.group_sel = -1;
    for (size_t g = 0; g < s.src.groups.size(); ++g)
        if (!f.group.empty() && s.src.groups[g] == f.group) s.group_sel = static_cast<int>(g);
    s.hair.length = f.hair_length;
    s.hair.type = f.hair_type;
    s.hair.accessory = f.hair_accessory;
    std::snprintf(s.search, sizeof(s.search), "%s", f.search.c_str());
}

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
    st().palette.clear();
}

// 5.0b-A, the single write path: a pick that changes the 3D head (look::render_changes: head, hair, beard, eyes, skin, body) drops the cached
// render of him, so the face chooser's picture is not the old look. The faces gate already did this for its own writes; the grids did not.
static void forget_head_render(App& app, const Table& t, uint64_t rec, const std::string& field) {
    if (!look::render_changes(field.c_str())) return;
    const int64_t id = who_id(app, t, rec);
    if (id >= 0) faces3d_forget_render(app, is_manager_table(t), id);
}

bool visual_apply(App& app, const Table& t, uint64_t rec, const std::string& field, int64_t code, std::string* msg) {
    const Field* f = t.field(field);
    if (!f) {
        if (msg) *msg = std::string(field_title(field)) + " is not in this database";
        return false;
    }
    if (field == "bodytypecode") {
        const bool ok = apply_bodytype(app, t, rec, code, bodytype_allow_risky(), msg);
        if (ok) forget_head_render(app, t, rec, field);
        return ok;
    }
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
    forget_head_render(app, t, rec, field);
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
    if (s.choice.on) {  // step 7: the 3D section's chooser hands the generic id back; its own "Apply hair & beard" writes the band code
        s.choice.code = s.ui.selected;
        s.choice.ready = true;
        s.ui.current = s.ui.selected;
        const int i = s.src.index_of(s.ui.selected);
        s.ui.last_result = "Picked " + (i >= 0 ? s.src.names[static_cast<size_t>(i)] : std::to_string(s.ui.selected));
        vlog(app, "visual picker (3D chooser): " + s.src.field + " of " + t->name + " " + std::to_string(who_id(app, *t, s.rec)) + " picked " +
                      std::to_string(s.ui.selected) + " (generic id; 0 = bald / no beard, " + std::to_string(faces3d::kHairOwnCode) + " / " +
                      std::to_string(faces3d::kBeardOwnCode) + " = his scan; nothing written: the 3D section's Apply writes the code)");
        return true;
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
    if (fcedit::has_field(app, f)) return true;  // Turbo X: FC Editor's asset pictures (boots, gloves, accessories, outfits... of anyone)
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

int rows(App& app, const Table& t, uint64_t rec, const std::vector<std::string>& fields, bool manager) {
    new_frame(app);
    int drawn = 0;
    ImGui::PushID("vprows");
    bool any = false;
    for (const std::string& f : fields) any = any || t.has(f);
    if (any && ImGui::BeginTable("##vrows", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        begin_rows();
        for (const std::string& f : fields) {
            if (!t.has(f)) continue;
            draw_row(app, t, rec, f, manager);
            ++drawn;
        }
        ImGui::EndTable();
    }
    if (!any) ImGui::TextDisabled("This database has none of these fields.");
    st().want_open = false;
    draw_picker_window(app, t, rec);
    ImGui::PopID();
    return drawn;
}

void open_choice(App& app, const Table& t, uint64_t rec, const std::string& field, bool manager, const std::vector<int>& ids, int64_t current) {
    new_frame(app);
    State::Choice c;
    c.on = true;
    c.field = field;
    for (int id : ids)
        if (id > 0) c.ids.push_back(id);
    c.current = current;
    open_picker(app, t, rec, field, manager, &c);
}

bool take_choice(const std::string& field, int64_t* code) {
    State& s = st();
    if (!s.choice.ready || s.choice.field != field) return false;
    s.choice.ready = false;
    if (code) *code = s.choice.code;
    return true;
}

void draw_code_picture(App& app, const Table& t, uint64_t rec, const char* field, bool manager, int64_t code, float side) {
    new_frame(app);
    // a light source of the one code, kept per field (the 3D section shows one hair and one beard thumbnail)
    static std::unordered_map<std::string, RowCache> cache;
    RowCache& rc = cache[field];
    const fs::path& res = resources(app);
    const fs::path fe = fcedit::folder(app);
    if (rc.table != t.name || rc.rec != rec || rc.value != code || rc.res != res || rc.fe != fe || rc.manager != manager) {
        build_source(app, t, rec, field, manager, rc.src, code);
        rc.table = t.name;
        rc.rec = rec;
        rc.value = code;
        rc.res = res;
        rc.fe = fe;
        rc.manager = manager;
    }
    row_picture(app, rc.src, code, side);
}

void draw_realface_picture(App& app, const char* field, int64_t code, float side) {
    new_frame(app);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 box(side, side);
    ImGui::Dummy(box);
    const std::string rf = fcedit::realface_picture(app, field, code);
    if (!rf.empty() && draw_still(app, fs::u8path(rf), 128, a, box, true)) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kRealFacePictureTip);
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), IM_COL32(58, 58, 64, 255), 4.0f);
    box_text(dl, a, box, rf.empty() ? kRealFaceMissingCap : "loading");
    if (rf.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("Not rendered on a real head yet (pictures\\realface).");
}

int look_rows(App& app, const Table& t, uint64_t rec, bool manager, bool skip_band_rows) {
    new_frame(app);
    int drawn = 0;
    ImGui::PushID("lookrows");
    if (resources(app).empty()) ImGui::TextDisabled("%s", pictures_line(app, false).c_str());
    const std::vector<const look::LookPicker*> all = look::pickers_for(manager ? look::kManager : look::kPlayer);
    const int64_t hc = skip_band_rows && t.has("headclasscode") ? app.db.get_int(t, rec, "headclasscode", -1) : -1;
    for (const char* g : look::kGroups) {
        std::vector<const look::LookPicker*> rows;
        for (const look::LookPicker* p : all)
            if (std::strcmp(p->group, g) == 0 && t.has(p->column) && !look::dedicated_card(p->column) &&
                !(skip_band_rows && look::band_owned(manager ? look::kManager : look::kPlayer, p->column, hc)))  // 5.0c E3: the 3D section owns them
                rows.push_back(p);
        if (rows.empty()) continue;
        ImGui::SeparatorText(g);
        ImGui::PushID(g);
        drawn += static_cast<int>(rows.size());  // the rows the card lists; ImGui draws them while the card is on screen (BeginTable is false when it is scrolled out)
        if (ImGui::BeginTable("##vrows", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            begin_rows();
            for (const look::LookPicker* p : rows) draw_row(app, t, rec, p->column, manager);
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    st().want_open = false;
    draw_picker_window(app, t, rec);
    ImGui::PopID();
    return drawn;
}

bool draw_field_picture(App& app, const Table& t, uint64_t rec, const char* field, bool manager, float side) {
    new_frame(app);
    if (!t.has(field)) return false;
    RowCache& rc = row_cache(app, t, rec, field, manager);
    const int idx = rc.src.index_of(app.db.get_int(t, rec, field, 0));
    if (idx < 0) return false;
    const uint8_t k = rc.src.src[static_cast<size_t>(idx)];
    if (k != kSrcRdbm && k != kSrcSwatch && k != kSrcFcEditor) return false;
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

    // Turbo X: the FC Editor install (ui_fcedit_pictures.h): its assets\ pictures are a picture source of every picker (read in place, never copied)
    if (!s.fe_edit_init) {
        s.fe_edit_init = true;
        std::snprintf(s.fe_edit, sizeof(s.fe_edit), "%s", app.prefs.fc_editor_dir.empty() ? fcedit::kDefaultDir : app.prefs.fc_editor_dir.c_str());
    }
    ImGui::SetNextItemWidth(S(360.0f));
    ImGui::InputText("FC Editor folder###fefolder", s.fe_edit, sizeof(s.fe_edit));
    ImGui::SameLine();
    if (ImGui::Button("Use this folder###fefolderset")) {
        app.prefs.fc_editor_dir = std::strcmp(s.fe_edit, fcedit::kDefaultDir) == 0 ? std::string() : std::string(s.fe_edit);
        fcedit::refresh();
        if (!app.save_gui_settings()) app.notify("The setting could not be saved", true);
        const fcedit::Index& ix = fcedit::index(app);
        if (ix.pictures) app.notify("FC Editor pictures found: " + std::to_string(ix.pictures));
        else app.notify(ix.found ? "No pictures in that folder's assets" : "No assets folder there: the pickers go without FC Editor pictures");
    }
    ImGui::SameLine();
    if (ImGui::Button("Default###fefolderdef")) {
        std::snprintf(s.fe_edit, sizeof(s.fe_edit), "%s", fcedit::kDefaultDir);
        app.prefs.fc_editor_dir.clear();
        fcedit::refresh();
        if (!app.save_gui_settings()) app.notify("The setting could not be saved", true);
    }
    const fcedit::Index& ix = fcedit::index(app);
    if (ix.pictures)
        ImGui::TextDisabled("Found: %zu pictures for %zu fields (hair, beards, boots, gloves, outfits, heads, body types...), read from your FC Editor install (never copied).",
                            ix.pictures, ix.fields.size());
    else if (ix.found)
        ImGui::TextDisabled("Found the folder, but no pictures in its assets sub-folders.");
    else
        ImGui::TextDisabled("Not found. With an FC Editor install here, its asset pictures fill the pickers' tiles that have no game or RDBM26 picture.");
}

// 3.1 G2b: the state of the pictures folder, and the line that says why there are no pictures ("" when the sets are found)
vis::FolderState visual_pictures_state(App& app) {
    resources(app);
    return st().info.state;
}

std::string visual_pictures_note(App& app) { return pictures_line(app, false); }

}  // namespace turbo
