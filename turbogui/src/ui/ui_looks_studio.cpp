// FC 27 LE Turbo GUI - Faces & looks > Looks studio (see ui_looks_studio.h). New file of brief 2.17-looks-studio, track U: it calls the
// existing chooser, faces card, body-type writer and field editors; it changes nothing in them.
#include "ui_looks_studio.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>

#include "app.h"
#include "core/bodytype_catalog.h"
#include "core/bodytype_thumbs.h"
#include "core/field_labels.h"
#include "core/legacy.h"
#include "core/look_pickers.h"
#include "core/looks_fields_data.h"
#include "core/player_capture.h"
#include "imgui.h"
#include "ui_bodytypes.h"
#include "ui_faces.h"
#include "ui_faces3d_gate.h"
#include "ui_images.h"
#include "ui_shell.h"
#include "ui_split.h"
#include "ui_theme.h"
#include "ui_visual_picker.h"

namespace fs = std::filesystem;

namespace turbo {
namespace looks {

namespace {

// ---------------------------------------------------------------- the fields of Accessories and Kit fit
// Track F's core/looks_fields_data.h (generated from the FC 27 schema and the census, research_20261017\LOOKS_FIELDS.md): the title
// and the value labels go on screen; its evidence and notes are developer text and stay off it. Accessories = the accessory slots,
// boots and goalkeeper gloves; Kit fit = sleeves, shirt, shorts, socks for players and the outfit for managers.
enum class Card { Accessories, KitFit };

bool in_card(const data::LookField& f, Card c, bool manager) {
    if (f.subject != (manager ? data::LookSubject::Manager : data::LookSubject::Player)) return false;
    if (c == Card::Accessories) return f.group == data::LookGroup::Accessory || f.group == data::LookGroup::Boots || f.group == data::LookGroup::Gloves;
    return f.group == data::LookGroup::KitFit || f.group == data::LookGroup::Outfit;
}

// The words for one value of a field: track F's label (the boot colour every player has by default reads as what it most likely
// is), else the editors' own descriptor (widgets.cpp code_value_text: never a bare number)
std::string value_text(const data::LookField& f, int64_t v) {
    if (const data::LookValue* lv = data::find_look_value(f, static_cast<int32_t>(v))) {
        if (lv->alt && std::strstr(lv->alt, "no colour override")) return "No colour override";
        return lv->label;
    }
    return code_value_text(f.name, v);
}

// The fields whose change is a new look (the picture is asked again): face, hair, body and every card field
constexpr const char* kLookSignature[] = {"headassetid", "headclasscode", "hairtypecode", "facialhairtypecode", "haircolorcode",
                                       "facialhaircolorcode", "skintonecode", "bodytypecode", "height", "weight"};

constexpr const char* kNotTested = "Not tested yet";
constexpr double kSignatureEvery = 0.5;    // s between two reads of his look (changes made on other cards or pages)
constexpr double kRetryEvery = 2.0;        // s between two tries while the game cannot draw (a match, another picture)
constexpr size_t kManagerUndoSteps = 30;
constexpr int kThumbLoadsPerFrame = 3;  // new body-type pictures asked from the texture loader in one frame
constexpr double kManifestEvery = 2.0;  // s between two looks at the generator's manifest.json

double g_timeout = 25.0;

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string clock_text() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return b;
}

// Draw a picture fitted and centred into the w x h box whose top-left corner is p0
void draw_fitted(ImTextureData* tex, int pw, int ph, ImVec2 p0, float w, float h) {
    if (!tex || pw <= 0 || ph <= 0) return;
    const float s = std::min(w / float(pw), h / float(ph));
    const ImVec2 a(p0.x + (w - pw * s) * 0.5f, p0.y + (h - ph * s) * 0.5f);
    ImGui::GetWindowDrawList()->AddImage(tex->GetTexRef(), a, ImVec2(a.x + pw * s, a.y + ph * s));
}

// One wrapped line centred in a box of width w
void centred_text(const char* text, float w) {
    const float tw = ImGui::CalcTextSize(text, nullptr, false, w).x;
    const float x0 = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x0 + std::max(0.0f, (w - tw) * 0.5f));
    ImGui::PushTextWrapPos(x0 + w);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------- CapturePreview
// The faces card's pattern (ui_faces3d_gate.cpp request_capture / tick_faces3d_preview / draw_picture), one object per camera:
//   kCameraBodyFull  "Full body": track C's full-body request, the page's default. W2228 drew it at mode 0 (256 x 256, head to boots in
//                    kit); since 3.1 R1 the built-in camera is mode 1 (1024 x 1024, unproven in game): when mode 1 gives no picture the
//                    preview asks ONCE more at mode 0 (mode_override 0) and logs "full body: mode 1 gave nothing, drew at 256"
//   kCameraBody      "Head and shoulders": mode 1, a 1024 x 1024 head-and-shoulders picture (W2202)
// The request is track C's capture::body_request (default descriptor, never the learned one). One request at a time for the whole
// game: refused while the service is busy (the other camera's picture, the faces card, the chooser's 3D heads, a miniface), polled
// only while this object waits (the service hands each answer to whoever polls first), a timeout that frees the service. A player is
// asked by his player id, a manager by his head id (as the miniface editor does), both with their club as the second id.
class CapturePreview : public PreviewSource {
public:
    CapturePreview(int camera, const char* title, const char* texture_key) : camera_(camera), title_(title), key_(texture_key) {}
    const char* title() const override { return title_; }

    bool ready(App& app, const Subject& s, std::string* why) override {
        if (!app.capture) {
            if (why) *why = "The game's picture is not available here.";
            return false;
        }
        if (waiting_for(s) || (same(shown_, s) && !image_.empty())) return true;
        if (why) *why = note_.empty() ? std::string("No picture of him yet.") : note_;
        return false;
    }

    // Can a request go out now? *why in plain words when not
    bool can_request(App& app, std::string* why) const {
        capture::CaptureService* svc = app.capture.get();
        if (!svc) {
            if (why) *why = "The game's picture is not available here.";
            return false;
        }
        if (pending_req_) {
            if (why) *why = "Still drawing the last picture...";
            return false;
        }
        const capture::Status st = svc->status();
        if (!st.installed || !st.available) {
            if (why) *why = "The game draws pictures only in its menus, not in a match or while it loads.";
            return false;
        }
        if (st.busy || faces3d_preview_pending() || faces3d_running()) {
            if (why) *why = "Still drawing the last picture...";
            return false;
        }
        return true;
    }

    bool refresh(App& app, const Subject& s, std::string* why) override {
        retry_ = Subject();  // a new ask replaces a mode-0 retry still held
        return send(app, s, -1, why);
    }

    // One request: mode_override -1 = the camera's own mode, 0 = the R1 retry of the full body at W2228's mode 0
    bool send(App& app, const Subject& s, int mode_override, std::string* why) {
        if (!s.valid()) return false;
        const int64_t req = s.manager ? s.head : s.id;
        if (req <= 0) {
            if (why) *why = "He has no head the game can draw.";
            return false;
        }
        if (!can_request(app, why)) return false;
        // track C's request: the default descriptor (never the learned one), his id, his club as the second id
        capture::Request r = capture::body_request(static_cast<int32_t>(req), s.club > 0 ? static_cast<int32_t>(s.club) : -1, camera_,
                                                   "Looks studio " + s.name);
        r.manager = s.manager;  // a manager by his head id (his full body is not tested yet)
        r.mode_override = mode_override;
        std::string err;
        if (!app.capture->request(r, &err)) {
            if (why) *why = "The game did not take the request: " + err;
            return false;
        }
        pending_ = s;
        pending_req_ = req;
        pending_retry_ = mode_override == 0;
        sent_at_ = app.now;
        ++requests_;
        note_.clear();
        app.log("looks studio: picture asked for " + s.name + " (id " + std::to_string(req) + ", camera " + std::to_string(camera_) +
                (mode_override >= 0 ? ", mode " + std::to_string(mode_override) : std::string()) + (s.manager ? ", manager" : "") + ")");
        return true;
    }

    // R1: the mode-0 retry goes out as soon as the service can take it (the game may still be settling the failed mode-1 request)
    void send_retry(App& app) {
        if (!retry_.valid()) return;
        std::string why;
        if (!can_request(app, &why)) {
            if (app.now - retry_since_ > g_timeout) {
                note_ = "The game did not draw him (no picture at mode 1, and no room to ask at 256: " + why + ")";
                retry_ = Subject();
            }
            return;
        }
        const Subject who = retry_;
        retry_ = Subject();
        if (!send(app, who, 0, &why)) note_ = "The game did not draw him (no picture at mode 1): " + why;
    }

    void tick(App& app) override {
        if (!app.capture) return;
        if (!pending_req_) {
            send_retry(app);
            return;
        }
        capture::Result r;
        if (app.capture->poll(r)) {
            if (r.id != pending_req_) {
                app.log("looks studio: a picture for id " + std::to_string(r.id) + " arrived while waiting for " + std::to_string(pending_req_) + " (dropped)");
                return;
            }
            const int64_t req = pending_req_;
            const Subject who = pending_;
            const bool was_retry = pending_retry_;
            pending_req_ = 0;
            pending_retry_ = false;
            pending_ = Subject();
            if (!r.ok) {
                app.log("looks studio: no picture for id " + std::to_string(req) + ": " + r.error);
                if (camera_ == capture::kCameraBodyFull && !was_retry) {
                    // R1: no picture at mode 1 (the 1024 stride, H1 unproven): ask ONCE more at W2228's mode 0
                    retry_ = who;
                    retry_since_ = app.now;
                    note_ = "No picture at 1024; asking again at 256...";
                    log_both(app, "looks studio: full body: mode 1 gave nothing for id " + std::to_string(req) + " (" + r.error + "); asking at mode 0");
                    send_retry(app);
                    return;
                }
                note_ = "The game did not draw him: " + r.error;
                return;
            }
            shown_ = who;
            const int side = std::max(256, static_cast<int>(S(640.0f)));  // the pane's size, before textures.pixels
            if (camera_ == capture::kCameraBodyFull) {
                // R2: the game's square picture cropped to him (his box on its plain background + 4 % per side), fitted to the pane's
                // height (upscaled too); no plain background = the whole picture, fitted to the height
                Box box;
                Rgba him;
                if (content_box(r.image, box)) him = crop_image(r.image, grow_box(box, 0.04f, r.image.w, r.image.h));
                image_ = fit_height(him.empty() ? r.image : him, side);
            } else {
                image_ = fit_image(r.image, side);  // core/image.h
            }
            ++version_;
            char size[32];
            std::snprintf(size, sizeof(size), " (%dx%d)", r.image.w, r.image.h);
            caption_ = std::string(title_) + ", drawn by the game at " + clock_text() + size;
            // turbo_gui.log too: the Tester reads the size the game drew ("DDS DXT5 1024x1024" at mode 1)
            log_both(app, "looks studio: picture of id " + std::to_string(req) + " (" + r.format + ")");
            if (was_retry) log_both(app, "looks studio: full body: mode 1 gave nothing, drew at 256 (id " + std::to_string(req) + ")");
            return;
        }
        if (app.now - sent_at_ > g_timeout) {
            app.capture->cancel();
            app.log("looks studio: no picture for id " + std::to_string(pending_req_) + " after " + std::to_string(int(g_timeout)) + " s (cancelled)");
            pending_req_ = 0;
            pending_ = Subject();
            char b[160];
            std::snprintf(b, sizeof(b), "No picture after %d s. The game draws only in its menus, not in a match.", int(g_timeout));
            note_ = b;
        }
    }

    void draw(App& app, const Subject& s, float w, float h) override {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, h));
        drew_ = false;
        if (same(shown_, s) && !image_.empty()) {
            TextureCache::Pic pic = app.textures.pixels(key_, version_, image_);
            draw_fitted(pic.tex, pic.w, pic.h, p0, w, h);
            drew_ = pic.tex != nullptr;
        }
        if (waiting_for(s)) {
            char b[64];
            std::snprintf(b, sizeof(b), "Drawing... %.0f s", std::max(0.0, app.now - sent_at_));
            ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + S(8.0f), p0.y + S(8.0f)), ImGui::GetColorU32(theme::warn()), b);
        }
    }

    // the line under the picture
    std::string line(const Subject& s) const {
        if (waiting_for(s)) return "Asking the game for his picture...";
        if (same(shown_, s) && !image_.empty()) return caption_ + "." + (note_.empty() ? std::string() : " " + note_);
        return note_;
    }
    static bool same(const Subject& a, const Subject& b) { return a.valid() && a.manager == b.manager && a.id == b.id; }
    bool waiting_for(const Subject& s) const { return (pending_req_ != 0 && same(pending_, s)) || same(retry_, s); }
    bool pending() const { return pending_req_ != 0 || retry_.valid(); }
    bool drew() const { return drew_; }
    int requests() const { return requests_; }
    void reset() {
        pending_ = shown_ = retry_ = Subject();
        pending_req_ = 0;
        pending_retry_ = false;
        image_ = Rgba();
        caption_.clear();
        note_.clear();
        requests_ = 0;
    }

    int camera() const { return camera_; }

private:
    int camera_;
    const char* title_;
    const char* key_;
    static void log_both(App& app, const std::string& line) {  // the in-app Log and turbo_gui.log (as host_log below)
        app.log(line);
        if (app.log_hook) app.log_hook(line);
    }

    Subject pending_, shown_;
    Subject retry_;               // R1: the person whose full body comes again at mode 0 once the service can take it
    double retry_since_ = 0.0;
    bool pending_retry_ = false;  // the request in flight is that mode-0 retry
    int64_t pending_req_ = 0;  // the id the request carries (player id, a manager's head id)
    double sent_at_ = 0.0;
    Rgba image_;
    uint64_t version_ = 0;
    std::string caption_, note_;
    int requests_ = 0;
    bool drew_ = false;
};

// ---------------------------------------------------------------- GameScreenHint
// No picture of our own yet: what "Draw him" does
class GameScreenHint : public PreviewSource {
public:
    const char* title() const override { return "On the game's screen"; }
    bool ready(App&, const Subject&, std::string*) override { return true; }
    void draw(App& app, const Subject& s, float w, float h) override {
        // text only: a game picture here (his miniface) looked at its files every frame (gui perf case: 4 file calls a frame)
        const ImVec2 start = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(start.x + S(12.0f), start.y + h * 0.4f));
        centred_text("Draw him asks the game for his picture in the view chosen above, as the game draws him now.", w - S(24.0f));
        ImGui::SetCursorPos(ImVec2(start.x, start.y + h));
    }
};

// ---------------------------------------------------------------- the page's state
struct ManagerUndo {
    int64_t managerid = 0;
    uint64_t rec = 0;
    std::string field;
    Value before;
};

struct BodyTile {
    int64_t code = 0;
    std::string caption;
    std::string search;  // lower-case caption and the names behind it
    std::string tip;     // hover: who has it, and the game's number
};

struct Studio {
    bool managers = false;
    bool force_all_fields = false;  // tests: the Look card's "All fields" fold opens on its own
    bool fold_was_forced = false;   // ... and closes again on the next frame it is drawn without it (also after a reset: ImGui keeps the open state)
    char search[64] = "";
    bool my_club = false;
    std::vector<int64_t> rows;  // players: playerid; managers: index into model.managers()
    std::string list_key;
    uint64_t list_version = ~uint64_t(0);
    // preview
    CapturePreview body{capture::kCameraBodyFull, "Full body", "looks_studio_body"};
    CapturePreview big{kCameraBody, "Head and shoulders", "looks_studio_big"};
    bool view_full = true;  // the "Full body" / "Head and shoulders" switch (Cassio 22:41: full body by default)
    uint64_t sig_full = 0, sig_big = 0;  // his look when each view's picture was last asked
    uint64_t& view_sig() { return view_full ? sig_full : sig_big; }
    CapturePreview& view() { return view_full ? body : big; }
    GameScreenHint hint;
    Subject last;            // the person of the last frame
    bool baseline = true;    // the next read of his look is the one the page starts from (no picture asked for it)
    uint64_t signature = 0;  // his look as last read
    uint64_t drawn_signature = 1;
    double signature_at = -1.0;
    bool signature_dirty = true;
    bool want_picture = false;
    double next_try = 0.0;
    std::string preview_why;
    // body type
    char body_search[48] = "";
    std::vector<BodyTile> tiles;
    std::string tiles_key;
    std::set<int64_t> thumbs_loaded;   // codes whose picture reached a texture (their next draws cost nothing)
    int thumb_frame = -1, thumb_loads = 0;  // new picture loads of this frame (at most kThumbLoadsPerFrame)
    double manifest_at = -1.0;          // App::now of the last look at manifest.json (every kManifestEvery s)
    int64_t manifest_stamp = 0;         // its time stamp then (0 = none)
    std::string body_result;
    bool body_result_error = false;
    // the page's own Undo for managers
    std::vector<ManagerUndo> manager_undo;
    // the question before a manager's real face
    StudioState st;
};

Studio& studio() {
    static Studio s;
    return s;
}

// ---------------------------------------------------------------- helpers
uint64_t look_signature(App& app, const Table& t, uint64_t rec, bool manager) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](int64_t v) {
        h ^= static_cast<uint64_t>(v);
        h *= 1099511628211ull;
    };
    for (const char* f : kLookSignature)
        if (t.has(f)) mix(app.db.get_int(t, rec, f, 0));
    for (const data::LookField& f : data::kLookFields)
        if ((in_card(f, Card::Accessories, manager) || in_card(f, Card::KitFit, manager)) && t.has(f.name)) mix(app.db.get_int(t, rec, f.name, 0));
    return h;
}

Subject current_subject(App& app, const Studio& s, const Table** table, uint64_t* rec) {
    Subject out;
    *table = nullptr;
    *rec = 0;
    if (!s.managers) {
        const PlayerRow* p = app.sel_player > 0 ? app.model.player(app.sel_player) : nullptr;
        const Table* t = app.db.table("players");
        if (!p || !t) return out;
        out.id = p->playerid;
        out.club = p->club;
        out.name = p->name;
        out.gender = p->gender;
        *table = t;
        *rec = p->rec;
    } else {
        const auto& mgrs = app.model.managers();
        const Table* t = app.db.table("manager");
        if (!t || app.sel_manager < 0 || app.sel_manager >= static_cast<int>(mgrs.size())) return out;
        const ManagerRow& m = mgrs[static_cast<size_t>(app.sel_manager)];
        out.manager = true;
        out.id = m.managerid;
        out.club = m.teamid;
        out.name = m.name;
        *table = t;
        *rec = m.rec;
        out.head = app.db.get_int(*t, m.rec, "headassetid", 0);
    }
    if (*table && !app.db.table_alive(**table, *rec)) {
        *table = nullptr;
        return Subject();
    }
    return out;
}

// One line in turbo_gui.log (the host log) and the in-app Log: the page's body-type edits and its Undo (App::edit_field and App::undo
// write the in-app Log only; the Tester's live check reads turbo_gui.log)
void host_log(App& app, const std::string& line) {
    app.log(line);
    if (app.log_hook) app.log_hook(line);
}

// A write of the page went through: his look is read again now, and a manager's step goes on the page's own Undo
void note_write(Studio& s, const Subject& who, uint64_t rec, const std::string& field, const Value& before, bool have_before) {
    s.signature_dirty = true;
    if (!who.manager || !have_before) return;
    s.manager_undo.push_back({who.id, rec, field, before});
    if (s.manager_undo.size() > kManagerUndoSteps) s.manager_undo.erase(s.manager_undo.begin());
}

// 5.0b-A: one look column typed in the Look card's "All fields" fold - today's field editor (App::edit, App's Undo for players, the page's own
// Undo for managers). The registry's words where the column has a picker, else the column's own name.
bool fold_field(App& app, Studio& s, const Table& t, uint64_t rec, const Subject& who, const char* column) {
    const Field* f = t.field(column);
    if (!f) return false;
    Value before;
    const bool have = app.db.get(t, rec, *f, before);
    ++s.st.look_all;
    const look::LookPicker* lp = look::find(column);
    const bool wrote = enum_editor(app, t, rec, *f, lp ? lp->label : column, S(220.0f));
    if (wrote) note_write(s, who, rec, column, before, have);
    return wrote;
}

// One field of Accessories / Kit fit: a combo with track F's words, written through App::edit (App's Undo for players, the page's own
// for managers). Choices and colours offer F's values; catalogues (boots, gloves, accessories, outfits) the codes the existing editors
// offer (widgets.cpp code_value_list: the game's pictures and gear write gate, core/gear_ids.h, and the codes the save uses).
bool look_combo(App& app, Studio& s, const Table& t, uint64_t rec, const Subject& who, const data::LookField& lf) {
    const Field* f = t.field(lf.name);
    if (!f || f->type != FieldType::Int) return false;
    Value before;
    if (!app.db.get(t, rec, *f, before)) return false;
    ++s.st.fields;
    const int64_t cur = before.i;
    bool wrote = false;
    if (vp::has_picture(app, t, lf.name, who.manager)) {  // 2.22 G-pickers: the picture or swatch inline, the grid behind Change
        wrote = vp::inline_field(app, t, rec, lf.name, lf.title, who.manager);
        if (wrote) note_write(s, who, rec, lf.name, before, true);
        return wrote;
    }
    ImGui::PushID(lf.name);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(lf.title);
    ImGui::SameLine(S(170.0f));
    ImGui::SetNextItemWidth(S(240.0f));
    const std::string shown = value_text(lf, cur);
    if (ImGui::BeginCombo("##lv", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        std::vector<int64_t> codes;
        if (lf.kind == data::LookKind::Catalog) {
            codes = code_value_list(app, t, *f, cur);
        } else {
            for (int32_t i = 0; i < lf.n_values; ++i) codes.push_back(lf.values[i].code);
            if (std::find(codes.begin(), codes.end(), cur) == codes.end()) codes.insert(codes.begin(), cur);
        }
        static char filt[48] = "";
        if (ImGui::IsWindowAppearing()) filt[0] = 0;
        if (codes.size() > 12) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##lvq", "search", filt, sizeof(filt));
        }
        const std::string q = lower_copy(filt);
        for (int64_t v : codes) {
            if (v != cur && (v < f->min || v > f->max())) continue;
            const std::string txt = value_text(lf, v);
            if (!q.empty() && lower_copy(txt).find(q) == std::string::npos) continue;
            const std::string item = txt + "##" + std::to_string(v);
            if (ImGui::Selectable(item.c_str(), v == cur) && v != cur) wrote = app.edit(t, rec, *f, Value::of_int(v));
            if (v == cur && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s = %lld", lf.name, static_cast<long long>(v));  // the game's number: hover only
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s\n%s = %lld", lf.title, shown.c_str(), lf.name, static_cast<long long>(cur));
    ImGui::PopID();
    if (wrote) note_write(s, who, rec, lf.name, before, true);
    return wrote;
}

bool card_begin(Studio& s, const char* id, const char* title, const char* tag = nullptr) {
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::SeparatorText(title);
    ++s.st.cards;
    if (tag) {
        theme::warn_text("%s", tag);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Built, but nobody has seen it work in a real game yet.");
    }
    return true;
}
void card_end() {
    ImGui::EndChild();
    ImGui::Spacing();
}

void hint_line(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    theme::muted_text("%s", text);
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------- the person list
void rebuild_rows(App& app, Studio& s) {
    s.rows.clear();
    const std::string q = lower_copy(s.search);
    if (!s.managers) {
        const int64_t mine = app.bridge.state().user_team;
        std::vector<const PlayerRow*> v;
        for (const PlayerRow& p : app.model.players()) {
            if (s.my_club && (mine <= 0 || p.club != mine)) continue;
            if (!q.empty() && lower_copy(p.name).find(q) == std::string::npos && lower_copy(p.club_name).find(q) == std::string::npos) continue;
            v.push_back(&p);
        }
        std::sort(v.begin(), v.end(), [](const PlayerRow* a, const PlayerRow* b) {
            const std::string la = lower_copy(a->name), lb = lower_copy(b->name);
            return la != lb ? la < lb : a->playerid < b->playerid;
        });
        s.rows.reserve(v.size());
        for (const PlayerRow* p : v) s.rows.push_back(p->playerid);
    } else {
        const auto& mgrs = app.model.managers();
        for (size_t i = 0; i < mgrs.size(); ++i) {
            if (!q.empty()) {
                const std::string team = mgrs[i].teamid > 0 ? app.model.team_name(mgrs[i].teamid) : std::string();
                if (lower_copy(mgrs[i].name).find(q) == std::string::npos && lower_copy(team).find(q) == std::string::npos) continue;
            }
            s.rows.push_back(static_cast<int64_t>(i));
        }
    }
}

void person_list(App& app, Studio& s) {
    if (ImGui::RadioButton("Players###lsplayers", !s.managers)) s.managers = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Managers###lsmanagers", s.managers)) s.managers = true;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##lsq", "name or club", s.search, sizeof(s.search));
    if (!s.managers) {
        const bool known = app.bridge.state().user_team > 0;
        if (!known) {
            ImGui::BeginDisabled();
            s.my_club = false;
        }
        ImGui::Checkbox("My club###lsmine", &s.my_club);
        if (!known) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", known ? "Only the players of your club" : "Open your career in the game first");
    }
    const std::string key = std::string(s.managers ? "m|" : "p|") + (s.my_club ? "1|" : "0|") + s.search;
    if (key != s.list_key || app.model.version() != s.list_version) {
        rebuild_rows(app, s);
        s.list_key = key;
        s.list_version = app.model.version();
    }
    s.st.listed = static_cast<int>(s.rows.size());
    ImGui::BeginChild("##lsrows", ImVec2(0, 0));
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(s.rows.size()));
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const int64_t key_id = s.rows[static_cast<size_t>(r)];
            if (!s.managers) {
                const PlayerRow* p = app.model.player(key_id);
                if (!p) continue;
                const std::string label = p->name + "##lsp" + std::to_string(p->playerid);
                if (ImGui::Selectable(label.c_str(), app.sel_player == p->playerid)) app.sel_player = p->playerid;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s, player ID %lld", p->club_name.empty() ? "no club" : p->club_name.c_str(),
                                                              static_cast<long long>(p->playerid));
            } else {
                const auto& mgrs = app.model.managers();
                if (key_id < 0 || key_id >= static_cast<int64_t>(mgrs.size())) continue;
                const ManagerRow& m = mgrs[static_cast<size_t>(key_id)];
                const std::string label = m.name + "##lsm" + std::to_string(key_id);
                if (ImGui::Selectable(label.c_str(), app.sel_manager == static_cast<int>(key_id))) app.sel_manager = static_cast<int>(key_id);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s, manager ID %lld", m.teamid > 0 ? app.model.team_name(m.teamid).c_str() : "free agent",
                                      static_cast<long long>(m.managerid));
            }
        }
    }
    if (s.rows.empty()) theme::muted_text("%s", s.managers ? "No manager matches." : "No player matches.");
    ImGui::EndChild();
}

// ---------------------------------------------------------------- the preview pane
void preview_pane(App& app, Studio& s, const Subject& who) {
    if (!who.valid()) {
        hint_line(s.managers ? "Pick a manager on the left." : "Pick a player on the left.");
        return;
    }
    ImGui::TextUnformatted(who.name.c_str());
    ImGui::SameLine();
    theme::muted_text("%s", who.club > 0 ? app.model.team_name(who.club).c_str() : (who.manager ? "free agent" : "no club"));

    // the switch, then the source: the game's picture of him in that view when there is one (or one being drawn), else the hint
    if (ImGui::RadioButton("Full body###lsviewbody", s.view_full) && !s.view_full) {
        s.view_full = true;
        s.want_picture = !s.body.ready(app, who, nullptr) || s.sig_full != s.signature;  // none of him yet, or one older than his look
        s.next_try = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Head and shoulders###lsviewhead", !s.view_full) && s.view_full) {
        s.view_full = false;
        s.want_picture = !s.big.ready(app, who, nullptr) || s.sig_big != s.signature;
        s.next_try = 0.0;
    }
    s.st.full_view = s.view_full;
    CapturePreview& cur = s.view();
    std::string why;
    PreviewSource* src = &s.hint;
    if (cur.ready(app, who, &why)) src = &cur;
    s.st.source = src->title();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float full_w = std::max(S(120.0f), avail.x);
    const float h = std::max(S(160.0f), std::min(avail.y - S(130.0f), full_w));
    // R2: the full body is a standing man (his picture is cropped to him): a narrow pane, centred; head and shoulders stay square
    const float w = s.view_full ? std::min(full_w, h * 0.6f) : full_w;
    if (w < full_w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (full_w - w) * 0.5f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), S(4.0f));
    src->draw(app, who, w, h);
    s.st.picture = src == &cur && cur.drew();
    s.st.waiting = cur.waiting_for(who);

    s.st.preview_line = cur.line(who);
    if (!s.preview_why.empty()) s.st.preview_line += (s.st.preview_line.empty() ? "" : " ") + s.preview_why;
    if (!s.st.preview_line.empty()) hint_line(s.st.preview_line.c_str());
    if (s.st.risky_body) {
        ImGui::PushTextWrapPos(0.0f);
        theme::warn_text("%s", "His body was made for another player's head. A picture of it is untested and may stop the game's drawing: "
                               "the Draw button asks anyway.");
        ImGui::PopTextWrapPos();
    }
    std::string cant;
    const bool can = cur.can_request(app, &cant);
    if (!can) ImGui::BeginDisabled();
    const bool have_picture = s.st.picture || s.st.waiting;
    const std::string draw_label = std::string(have_picture ? "Draw again" : "Draw him") + "###lsdraw";
    if (ImGui::Button(draw_label.c_str())) {
        std::string err;
        if (cur.refresh(app, who, &err)) {
            s.want_picture = false;
            s.drawn_signature = s.view_sig() = s.signature;
            s.preview_why.clear();
        } else {
            s.preview_why = err;
        }
    }
    if (!can) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", can ? "Asks the game for a new picture of him (about a second, in the game's menus only)" : cant.c_str());
    ImGui::SameLine();
    theme::warn_text("%s", kNotTested);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", who.manager ? "A manager's picture, full body above all, has not been seen through this page in a real game yet."
                                            : "The game draws this picture, but nobody has used this page in a real game yet.");
    hint_line("Drawn by the game with his current look.");
    if (who.manager && s.view_full) hint_line("A manager's full body is not tested yet: the game may answer with no picture.");
}

// His look changed while the page is shown (a card here, Undo, the face chooser, another page): ask the game for his picture once it
// can draw (players and managers; never by itself for a player's body made for another head). 3.1 R3: picking a PLAYER with no
// picture of him in the current view asks for one too (no "Draw him" click; the retry loop waits while the game cannot draw). Picking
// a manager still asks nothing (his full body is not tested): "Draw him" does.
void auto_picture(App& app, Studio& s, const Subject& who, const Table* t, uint64_t rec) {
    const bool same = who.manager == s.last.manager && who.id == s.last.id;
    if (!same) {
        s.last = who;
        s.baseline = true;
        s.signature_dirty = true;
        s.want_picture = who.valid() && !who.manager && app.capture && !s.view().ready(app, who, nullptr);  // R3
        s.next_try = 0.0;
        s.preview_why.clear();
        s.body_result.clear();
    }
    if (!who.valid() || !t) return;
    if (s.signature_dirty || s.signature_at < 0.0 || app.now >= s.signature_at + kSignatureEvery) {
        s.signature = look_signature(app, *t, rec, who.manager);
        s.signature_at = app.now;
        s.signature_dirty = false;
        if (s.baseline) {
            s.drawn_signature = s.signature;
            s.baseline = false;
        } else if (s.signature != s.drawn_signature) {
            s.want_picture = true;
            s.next_try = 0.0;
        }
    }
    // a player's own body on a head it was not made for: the capture renderer may not cope (FULLBODY_FAST_ROUTE.md 2 d)
    s.st.risky_body = false;
    if (!who.manager && t->has("bodytypecode")) {
        const int hc = t->has("headclasscode") ? static_cast<int>(app.db.get_int(*t, rec, "headclasscode", -1)) : -1;
        s.st.risky_body = bodytype::risky_pairing(bodytype_catalog(app), app.db.get_int(*t, rec, "bodytypecode", 0), hc);
    }
    if (!s.want_picture || s.st.risky_body) return;
    if (app.now < s.next_try) return;
    std::string why;
    if (s.view().refresh(app, who, &why)) {
        s.want_picture = false;
        s.drawn_signature = s.view_sig() = s.signature;
        s.preview_why.clear();
    } else {
        s.preview_why = why;
        s.next_try = app.now + kRetryEvery;
    }
}

// ---------------------------------------------------------------- cards
std::string undo_text(App& app, const Studio& s, const Subject& who) {
    if (!who.valid()) return std::string();
    if (!who.manager) {
        const auto it = app.undo_.find(who.id);
        if (it == app.undo_.end() || it->second.empty()) return std::string();
        const App::UndoStep& step = it->second.back();
        if (!step.pictures.empty()) return "Face";
        return labels::field_title(step.field);
    }
    for (auto it = s.manager_undo.rbegin(); it != s.manager_undo.rend(); ++it)
        if (it->managerid == who.id) return labels::field_title(it->field);
    return std::string();
}

void undo_button(App& app, Studio& s, const Subject& who) {
    const std::string what = undo_text(app, s, who);
    s.st.undo_label = what.empty() ? std::string() : "Undo: " + what;
    if (what.empty()) ImGui::BeginDisabled();
    const std::string label = (what.empty() ? std::string("Undo") : "Undo: " + what) + "###lsundo";
    const bool hit = ImGui::Button(label.c_str());
    if (what.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", what.empty() ? "Nothing to undo for him yet."
                                             : who.manager ? "Puts back the last change made on this page."
                                                           : "Puts back his last change, made on this page or another one.");
    if (!hit) return;
    if (!who.manager) {
        std::string step_line;
        if (const auto it = app.undo_.find(who.id); it != app.undo_.end() && !it->second.empty()) {
            const App::UndoStep& step = it->second.back();
            step_line = "looks studio: undo " + step.table + "." + step.field + " = " + step.before.to_string() + " (" + who.name + ")";
        }
        if (app.undo(who.id)) {
            s.signature_dirty = true;
            if (!step_line.empty()) host_log(app, step_line);
        }
        return;
    }
    for (auto it = s.manager_undo.end(); it != s.manager_undo.begin();) {
        --it;
        if (it->managerid != who.id) continue;
        const ManagerUndo step = *it;
        s.manager_undo.erase(it);
        const Table* t = app.db.table("manager");
        const Field* f = t ? t->field(step.field) : nullptr;
        if (!t || !f || !app.db.table_alive(*t, step.rec)) {
            app.notify("Undo: the game's data changed (another save loaded?). Press Refresh at the top.", true);
            return;
        }
        if (app.edit_field(*t, step.rec, *f, step.before)) {
            app.notify("Undone: " + labels::field_title(step.field) + " back to " + app.describe_value(*t, *f, step.before));
            host_log(app, "looks studio: undo manager." + step.field + " = " + step.before.to_string() + " (" + who.name + ")");
            s.signature_dirty = true;
        }
        return;
    }
}

void face_card(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec) {
    card_begin(s, "##lsface", "Face");
    const int64_t hc = t.has("headclasscode") ? app.db.get_int(t, rec, "headclasscode", -1) : -1;
    hint_line(hc == 0 ? "He has a real face." : hc == 1 ? "He has a generic face." : "His face is one of the game's own heads.");
    const bool choose = ImGui::Button("Choose a real face...###lsface");
    if (!who.manager) {
        if (choose) ImGui::OpenPopup("Choose a real face");
        real_face_picker(app, who.id);  // ui_faces.cpp: the head and his picture together, one step of his Undo
    } else {
        // 3.1 R4: one click opens the chooser; the "can't be undone" warning is its first line (no question before it). The page
        // begins the chooser's window first in the frame (same name, same size as ui_faces.cpp sets) for that line, then
        // manager_real_face_picker appends the chooser itself to the same window (ImGui's Begin appends on a second call).
        if (choose) ImGui::OpenPopup(kManagerFacePopup);
        ImGui::SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(kManagerFacePopup, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            s.st.asking = true;  // the warning is shown
            ImGui::PushTextWrapPos(0.0f);
            theme::warn_text("%s", "His head and his picture change as soon as you pick a face. The Undo button can't bring them back.");
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        manager_real_face_picker(app, who.id);  // ui_faces.cpp
    }
    card_end();
}

void hair_card(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec) {
    card_begin(s, "##lshair", "Hair & beard");
    // closed until opened: the faces card looks at a file every frame it is drawn (ui_faces3d_gate.cpp), as on the Appearance tab
    ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    if (!ImGui::TreeNode("Change his hair and beard###lshairnode")) {
        card_end();
        return;
    }
    if (!who.manager) {
        if (const PlayerRow* p = app.model.player(who.id)) faces3d_gate_section(app, t, *p);  // ui_faces3d_gate.cpp
    } else {
        const auto& mgrs = app.model.managers();
        if (app.sel_manager >= 0 && app.sel_manager < static_cast<int>(mgrs.size()))
            faces3d_manager_section(app, t, mgrs[static_cast<size_t>(app.sel_manager)]);  // ui_faces3d_gate.cpp
    }
    const int64_t hc = t.has("headclasscode") ? app.db.get_int(t, rec, "headclasscode", -1) : -1;
    // a generic head takes the game's hairstyles: they are rows of the Look card below (ONE picker for each, 5.0b-A)
    if (hc != 0) hint_line("This is a generic head: pick his hair and beard in the Look card below.");
    else hint_line("Hair & beard has its own way back: His own hair & beard.");
    ImGui::TreePop();
    card_end();
}

void build_tiles(App& app, Studio& s, int64_t current, int gender, bool manager) {
    bodytype::Catalog& cat = bodytype_catalog(app);
    const std::string key = std::string(manager ? "m" : "p") + std::to_string(current) + "|" + std::to_string(gender) + "|" + std::to_string(cat.probed_count()) + "|" +
                            std::to_string(cat.localized_count());
    if (key == s.tiles_key) return;
    s.tiles_key = key;
    s.tiles.clear();
    for (int64_t code : body_grid_codes(&cat, current, gender, manager)) {
        BodyTile tile;
        tile.code = code;
        const std::string cap = body_caption(code, &cat, manager);
        tile.caption = code == current && cap == "Unique body" ? std::string("His own body") : cap;
        std::string who;
        if (const labels::data::Users* u = labels::find_users("bodytypecode", code)) who = u->names;
        else who = cat.examples_text(code);
        tile.search = lower_copy(tile.caption + " " + who);
        tile.tip = who.empty() ? tile.caption : "Players with this body: " + who;
        const data::LookField* bf = data::find_look_field(manager ? data::LookSubject::Manager : data::LookSubject::Player, "bodytypecode");
        const data::LookValue* bv = bf ? data::find_look_value(*bf, static_cast<int32_t>(code)) : nullptr;
        char b[160] = "";
        if (bv && bv->used > 0 && bv->h_max > 0)
            std::snprintf(b, sizeof(b), "\nHeight %d-%d cm, weight %d-%d kg, used by %d %s", bv->h_min, bv->h_max, bv->w_min, bv->w_max, bv->used,
                          manager ? "managers" : "players");
        else if (const bodytype::Entry* e = cat.find(code); e && e->has_height())
            std::snprintf(b, sizeof(b), "\nHeight %d-%d cm, weight %d-%d kg, used by %lld players", e->height_min, e->height_max, e->weight_min,
                          e->weight_max, static_cast<long long>(e->players));
        tile.tip += std::string(b) + "\nBody type " + std::to_string(code);
        s.tiles.push_back(std::move(tile));
    }
}

// A new manifest.json (a generator run went on or started again): list the pictures again now and let every tile load its picture
// anew (one time-stamp look every kManifestEvery s, never per tile)
void watch_manifest(App& app, Studio& s) {
    if (s.manifest_at >= 0.0 && app.now >= s.manifest_at && app.now < s.manifest_at + kManifestEvery) return;
    s.manifest_at = app.now;
    std::error_code ec;
    const fs::path mf = bodytype_thumbs::manifest_file(app.turbo_output());
    const auto t = fs::last_write_time(mf, ec);
    const int64_t stamp = ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
    if (stamp == s.manifest_stamp) return;
    s.manifest_stamp = stamp;
    bodytype_thumbs::invalidate();
    for (int64_t code : s.thumbs_loaded) app.textures.forget(bodytype_thumbs::grid_file(app.turbo_output(), code));
    s.thumbs_loaded.clear();
}

void body_card(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec) {
    card_begin(s, "##lsbody", "Body type");
    if (!t.has("bodytypecode")) {
        hint_line("This database has no body types.");
        card_end();
        return;
    }
    const int64_t current = app.db.get_int(t, rec, "bodytypecode", 0);
    build_tiles(app, s, current, who.gender, who.manager);
    ImGui::SetNextItemWidth(S(240.0f));
    ImGui::InputTextWithHint("##lsbodyq", "find a body or a player", s.body_search, sizeof(s.body_search));
    const std::string q = lower_copy(s.body_search);
    std::vector<const BodyTile*> shown;
    for (const BodyTile& tile : s.tiles)
        if (q.empty() || tile.search.find(q) != std::string::npos) shown.push_back(&tile);
    // the pictures of track T2's generator (core/bodytype_thumbs.h): thumb() is a cached listing (rescanned every few seconds), a
    // new manifest.json (another run) empties it at once; a tile with no picture shows its name only
    bodytype_thumbs::configure(app.turbo_output());
    watch_manifest(app, s);
    std::vector<fs::path> pics(shown.size());
    bool any_pic = false;
    for (size_t i = 0; i < shown.size(); ++i) {
        pics[i] = bodytype_thumbs::thumb(shown[i]->code, who.manager);
        any_pic = any_pic || !pics[i].empty();
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tw = any_pic ? S(120.0f) : S(190.0f);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int per_row = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (tw + style.ItemSpacing.x)));
    const int rows = (static_cast<int>(shown.size()) + per_row - 1) / per_row;
    if (ImGui::GetFrameCount() != s.thumb_frame) {
        s.thumb_frame = ImGui::GetFrameCount();
        s.thumb_loads = 0;
    }
    int64_t picked = -1;
    ImGuiListClipper clip;
    clip.Begin(rows);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            for (int c = 0; c < per_row; ++c) {
                const size_t i = static_cast<size_t>(r * per_row + c);
                if (i >= shown.size()) break;
                const BodyTile& tile = *shown[i];
                if (c > 0) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(static_cast<int>(tile.code));
                const bool selected = tile.code == current;
                if (any_pic) {
                    // the picture box (empty where the generator made none), then the name under it
                    const ImVec2 p0 = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("##btpic", ImVec2(tw, tw))) picked = tile.code;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.tip.c_str());
                    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + tw, p0.y + tw),
                                                              ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_FrameBg), S(4.0f));
                    if (!pics[i].empty()) {
                        const bool known = s.thumbs_loaded.count(tile.code) != 0;
                        if (known || s.thumb_loads < kThumbLoadsPerFrame) {
                            if (!known) ++s.thumb_loads;
                            TextureCache::Pic pic = app.textures.file(pics[i], bodytype_thumbs::kGridWidth);
                            if (pic.tex) {
                                s.thumbs_loaded.insert(tile.code);
                                draw_fitted(pic.tex, pic.w, pic.h, p0, tw, tw);
                                ++s.st.thumbs;
                            }
                        }
                    }
                }
                const std::string label = tile.caption + "##bt" + std::to_string(tile.code);
                if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(tw, 0.0f))) picked = tile.code;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.tip.c_str());
                ImGui::PopID();
                ImGui::EndGroup();
                ++s.st.body_tiles;
            }
        }
    }
    if (shown.empty()) hint_line("No body matches.");
    if (picked >= 0 && picked != current) {
        Value before;
        const Field* f = t.field("bodytypecode");
        const bool have = f && app.db.get(t, rec, *f, before);
        std::string msg;
        const bool ok = apply_bodytype(app, t, rec, picked, false, &msg);  // ui_bodytypes.cpp: validated, App's Undo for players
        s.body_result = msg;
        s.body_result_error = !ok;
        if (ok) note_write(s, who, rec, "bodytypecode", before, have);
        if (ok && f && have && before.i != picked)
            host_log(app, "looks studio: " + app.describe_edit(t, rec, *f, before, Value::of_int(picked)) + "  [" + t.name + ".bodytypecode = " +
                              std::to_string(picked) + "] (" + who.name + ")");
    }
    if (!s.body_result.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (s.body_result_error) theme::err_text("%s", s.body_result.c_str());
        else theme::muted_text("%s", s.body_result.c_str());
        ImGui::PopTextWrapPos();
    }
    s.st.body_result = s.body_result;
    card_end();
}

void fields_card(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec, Card which) {
    const bool kit = which == Card::KitFit;
    card_begin(s, kit ? "##lskit" : "##lsgear", kit ? (who.manager ? "Outfit" : "Kit fit") : "Accessories", kNotTested);
    int drawn = 0;
    for (const data::LookField& f : data::kLookFields) {
        if (!in_card(f, which, who.manager) || !t.has(f.name)) continue;
        look_combo(app, s, t, rec, who, f);
        ++drawn;
    }
    if (!drawn) hint_line("This database has none of these fields.");
    card_end();
}

// 2.22 (G-ia): Tattoos (all seven body areas, preview and picker: ui_images.cpp tattoo_editor). Players only: managers have none.
void tattoo_card(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec) {
    if (who.manager) return;
    card_begin(s, "##lstattoo", "Tattoos");
    tattoo_editor(app, t, rec);
    s.st.tattoo_card = true;
    card_end();
}

// 2.22 (G-ia; Cassio: "miniface editor disappeared too"): the whole miniface editor (ui_images.cpp miniface_editor: its capture, a player's
// miniface, asset-source and result tabs), open, for the person picked. Same target as the Card view's (ui_player_page.cpp) and, for a
// manager, as Career > Managers & staff. No head/miniface mismatch prompt (replaced by the 2.20 faces-safety rules).
void miniface_card(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec) {
    card_begin(s, "##lsminiface", who.manager ? "Manager miniface" : "Player miniface");
    MinifaceTarget mt;
    mt.manager = who.manager;
    mt.headassetid = app.db.get_int(t, rec, "headassetid", 0);
    mt.id = who.id;
    mt.teamid = who.club;
    mt.table = &t;  // 5.0b-C: the editor draws the Body type row
    mt.rec = rec;
    if (who.manager) {
        mt.path = legacy_path::staff_miniface_for(who.id, mt.headassetid);
        mt.shown = app.legacy.first_present(legacy_path::staff_pictures(who.id, mt.headassetid));
    } else {
        mt.path = legacy_path::player_miniface(who.id);
    }
    miniface_editor(app, mt);
    s.st.miniface_card = true;
    card_end();
}

// 5.0b-A: the Look card - ONE card for a player and for a manager, drawn FROM the registry (core/look_pickers.h): a row for every look attribute
// the person's table holds (picture, in-game name, Change; hair and beard are one row each), except those a card of their own draws (Face,
// Body type, Tattoos). The fold "All fields" is today's field grid: every look column of the registry and the typed-only ones, typed.
// 5.0c E3: fold3d = the Teams > Managers > Appearance tab, where this card is the ONE hair editor of the manager (the 3D section and the raw
// grid under it are gone, ui_teams.cpp): a real-face manager gets the 3D hair and beard fold on top of the rows (closed until opened: the faces
// card reads a file every frame it is drawn). The Looks page passes false: its own hair_card above draws the same section.
void look_card_body(App& app, Studio& s, const Subject& who, const Table& t, uint64_t rec, bool fold3d = false) {
    card_begin(s, "##lslook", "Look");
    ++s.st.look_cards;
    if (fold3d && who.manager && t.has("headclasscode") && app.db.get_int(t, rec, "headclasscode", -1) == 0) {
        for (const ManagerRow& m : app.model.managers()) {
            if (m.managerid != who.id) continue;
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (ImGui::TreeNode("3D hair and beard###look3dnode")) {
                faces3d_manager_section(app, t, m);  // ui_faces3d_gate.cpp: the faces3d band writer, Apply, His own hair & beard
                ImGui::TreePop();
            }
            break;
        }
    }
    s.st.look_rows += vp::look_rows(app, t, rec, who.manager);
    if (s.force_all_fields) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    else if (s.fold_was_forced) ImGui::SetNextItemOpen(false, ImGuiCond_Always);
    else ImGui::SetNextItemOpen(false, ImGuiCond_Once);
    s.fold_was_forced = s.force_all_fields;
    if (ImGui::TreeNode("All fields###lslookall")) {
        const look::Subject sj = who.manager ? look::kManager : look::kPlayer;
        for (const look::LookPicker* p : look::pickers_for(sj))
            if (t.has(p->column)) fold_field(app, s, t, rec, who, p->column);
        for (const char* c : look::typed_only(sj))
            if (t.has(c)) fold_field(app, s, t, rec, who, c);
        ImGui::TreePop();
    }
    card_end();
}

void cards_pane(App& app, Studio& s, const Subject& who, const Table* t, uint64_t rec) {
    s.st.tattoo_card = false;
    s.st.miniface_card = false;
    s.st.look_cards = s.st.look_rows = s.st.look_all = 0;
    if (!who.valid() || !t) {
        hint_line("The cards show once you pick someone.");
        return;
    }
    undo_button(app, s, who);
    ImGui::Spacing();
    face_card(app, s, who, *t, rec);
    hair_card(app, s, who, *t, rec);
    body_card(app, s, who, *t, rec);
    tattoo_card(app, s, who, *t, rec);
    miniface_card(app, s, who, *t, rec);
    fields_card(app, s, who, *t, rec, Card::Accessories);
    fields_card(app, s, who, *t, rec, Card::KitFit);
    look_card_body(app, s, who, *t, rec);  // 5.0b-A: ONE Look card, its rows from the registry (core/look_pickers.h)
    // tests (looks_studio_open_all_fields_for_tests): the card is the last one and a test cannot scroll; ImGui skips the items of a card that is
    // out of the pane's view, so the pane goes to its end while the test holds the fold open
    if (s.force_all_fields) ImGui::SetScrollY(ImGui::GetScrollMaxY());
}

}  // namespace

// ---------------------------------------------------------------- public: preview sources
std::unique_ptr<PreviewSource> make_capture_preview(int camera) {
    return std::make_unique<CapturePreview>(camera, camera == kCameraBody ? "Head and shoulders" : "Full body", "looks_studio_made");
}
std::unique_ptr<PreviewSource> make_game_screen_hint() { return std::make_unique<GameScreenHint>(); }

// ---------------------------------------------------------------- public: the Look card
void look_card(App& app, const Subject& who, uint64_t rec) {
    const Table* t = app.db.table(who.manager ? "manager" : "players");
    if (!t || !who.valid()) return;
    look_card_body(app, studio(), who, *t, rec, true);  // 5.0c E3: the Teams > Managers tab: with the 3D hair and beard fold
}

// ---------------------------------------------------------------- public: body-type grid
std::string body_caption(int64_t code, const bodytype::Catalog* cat, bool manager) {
    // track F's words where they name the body ("Tall, lean", "Manager body 82"); its "Style N" (nothing names it) is not shown
    if (const data::LookField* f = data::find_look_field(manager ? data::LookSubject::Manager : data::LookSubject::Player, "bodytypecode"))
        if (const data::LookValue* v = data::find_look_value(*f, static_cast<int32_t>(code)); v && v->src != data::LookSrc::StyleN) return v->label;
    if (cat && cat->named(code)) return cat->name(code);
    if (const labels::NamedCodes* n = labels::named_codes("bodytypecode"))
        if (const char* name = labels::find_name(*n, code)) return name;
    std::string first;
    if (const labels::data::Users* u = labels::find_users("bodytypecode", code)) first = u->names;
    else if (cat) {
        if (const bodytype::Entry* e = cat->find(code); e && !e->examples.empty()) first = e->examples.front();
    }
    const size_t comma = first.find(',');
    if (comma != std::string::npos) first.resize(comma);
    return first.empty() ? std::string("Unique body") : "Like " + first;
}

std::vector<int64_t> body_grid_codes(const bodytype::Catalog* cat, int64_t current, int gender, bool manager) {
    std::vector<int64_t> out;
    if (manager) {
        // managers have their own set (track F: the generic bodies and 81-89, "Manager body N")
        if (const data::LookField* f = data::find_look_field(data::LookSubject::Manager, "bodytypecode"))
            for (int32_t i = 0; i < f->n_values; ++i) out.push_back(f->values[i].code);
        if (current > 0 && std::find(out.begin(), out.end(), current) == out.end()) out.push_back(current);
        return out;
    }
    const data::LookField* pf = data::find_look_field(data::LookSubject::Player, "bodytypecode");
    std::set<int64_t> generic, own;
    if (const labels::NamedCodes* n = labels::named_codes("bodytypecode"))
        for (const auto& e : *n) generic.insert(e.first);
    if (pf)
        for (int32_t i = 0; i < pf->n_values; ++i) (pf->values[i].src == data::LookSrc::StyleN ? own : generic).insert(pf->values[i].code);
    for (const labels::data::Users& u : labels::data::kUsers)
        if (std::string(u.field) == "bodytypecode") own.insert(u.code);
    if (cat) {
        for (int64_t c : cat->codes()) {
            if (cat->named(c)) generic.insert(c);
            else if (const bodytype::Entry* e = cat->find(c); e && !e->examples.empty()) own.insert(c);
        }
    }
    // a body only the other gender has (the probe's gender per code, else track F's census counts) is left out, unless it is his
    auto wrong_gender = [&](int64_t c) {
        if (gender < 0 || c == current) return false;
        if (cat)
            if (const bodytype::Entry* e = cat->find(c); e && e->gender >= 0) return e->gender != gender;
        if (pf)
            if (const data::LookValue* v = data::find_look_value(*pf, static_cast<int32_t>(c)); v && v->used > 0) return (gender == 0 ? v->male : v->female) == 0;
        return false;
    };
    for (int64_t c : generic)
        if (!wrong_gender(c)) out.push_back(c);
    std::vector<std::pair<std::string, int64_t>> named;
    for (int64_t c : own) {
        if (generic.count(c) || wrong_gender(c)) continue;
        const std::string cap = body_caption(c, cat);
        if (cap != "Unique body") named.push_back({lower_copy(cap), c});  // a body nothing names gets no tile (but his own one)
    }
    std::sort(named.begin(), named.end());
    for (const auto& n : named) out.push_back(n.second);
    if (current > 0 && std::find(out.begin(), out.end(), current) == out.end()) out.push_back(current);
    return out;
}

std::vector<std::string> card_fields(bool manager, bool kit_fit) {
    std::vector<std::string> out;
    for (const data::LookField& f : data::kLookFields)
        if (in_card(f, kit_fit ? Card::KitFit : Card::Accessories, manager)) out.push_back(f.name);
    return out;
}

const StudioState& studio_state() { return studio().st; }

void tick_looks_studio(App& app) {
    studio().body.tick(app);
    studio().big.tick(app);
}

void looks_studio_reset_for_tests() {
    Studio& s = studio();
    s.body.reset();
    s.big.reset();
    s.view_full = true;
    s.sig_full = s.sig_big = 0;
    s.managers = false;
    s.search[0] = 0;
    s.my_club = false;
    s.rows.clear();
    s.list_key.clear();
    s.list_version = ~uint64_t(0);
    s.force_all_fields = false;
    s.fold_was_forced = true;  // ImGui keeps the fold's open state: the next draw closes it once
    s.last = Subject();
    s.signature_dirty = true;
    s.baseline = true;
    s.drawn_signature = 1;
    s.want_picture = false;
    s.next_try = 0.0;
    s.preview_why.clear();
    s.body_search[0] = 0;
    s.tiles.clear();
    s.tiles_key.clear();
    s.body_result.clear();
    s.thumbs_loaded.clear();
    s.manifest_at = -1.0;
    s.manifest_stamp = 0;
    s.manager_undo.clear();
    s.st = StudioState();
    g_timeout = 25.0;
}

void looks_studio_set_timeout_for_tests(double seconds) { g_timeout = seconds; }
void looks_studio_show_managers(bool managers) { studio().managers = managers; }
void looks_studio_open_all_fields_for_tests(bool open) { studio().force_all_fields = open; }

// ---------------------------------------------------------------- the page
void draw_looks_studio(App& app) {
    Studio& s = studio();
    tick_looks_studio(app);
    const int requests = s.body.requests() + s.big.requests();
    s.st = StudioState();
    s.st.requests = requests;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted("Appearance Editor");
    ImGui::PopFont();
    ImGui::SameLine();
    theme::muted_text("%s", "Face, hair, body, gear and kit fit for players and managers.");
    if (!app.connected()) {
        not_connected_hint();
        return;
    }
    const Table* t = nullptr;
    uint64_t rec = 0;
    const Subject who = current_subject(app, s, &t, &rec);
    s.st.subject = who;
    auto_picture(app, s, who, t, rec);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float list_w = std::min(S(260.0f), avail.x * 0.25f);
    const float preview_w = std::max(S(220.0f), (avail.x - list_w) * 0.45f);
    // 5.0b-B: the three panes keep their width in gui_settings.json gui.splits (split::pane_x, key "<page>/<id>"), so a restart brings them back.
    // The child IDs are the same literals, so the ini blocks of earlier versions still seed them. Defaults (unscaled px) = the old first-use sizes.
    const float un = g_ui_scale > 0.0f ? 1.0f / g_ui_scale : 1.0f;
    split::pane_x("##lslist", list_w * un, 160.0f);
    person_list(app, s);
    ImGui::EndChild();
    ImGui::SameLine();
    split::pane_x("##lspreview", preview_w * un, 220.0f);
    preview_pane(app, s, who);
    ImGui::EndChild();
    ImGui::SameLine();
    // 2.22: the cards' pane is as tall as the page; its right edge is the page's, so the divider to the preview is the one that drags (the preview's
    // ResizeX); this pane is resizable too (ResizeX of a child on the right edge widens it into the window's scroll)
    split::pane_x("##lscards", std::max(220.0f, ImGui::GetContentRegionAvail().x * un), 220.0f, 2.0f);  // max 2x the rest: it may widen into the page's scroll
    cards_pane(app, s, who, t, rec);
    ImGui::EndChild();
    s.st.requests = s.body.requests() + s.big.requests();
}

}  // namespace looks
}  // namespace turbo
