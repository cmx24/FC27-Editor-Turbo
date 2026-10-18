// FC 27 LE Turbo GUI - Faces & looks > Looks studio (see ui_looks_studio.h). New file of brief 2.17-looks-studio, track U: it calls the
// existing chooser, faces card, body-type writer and field editors; it changes nothing in them.
#include "ui_looks_studio.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <system_error>
#include <utility>

#include "app.h"
#include "core/bodytype_catalog.h"
#include "core/field_labels.h"
#include "core/legacy.h"
#include "core/looks_fields_data.h"
#include "core/player_capture.h"
#include "imgui.h"
#include "ui_bodytypes.h"
#include "ui_faces.h"
#include "ui_faces3d_gate.h"
#include "ui_images.h"
#include "ui_shell.h"
#include "ui_theme.h"

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
// The faces card's pattern (ui_faces3d_gate.cpp request_capture / tick_faces3d_preview / draw_picture) with the large camera (mode 1,
// a 1024 x 1024 head-and-shoulders picture in game, players and managers alike, Tester 2026-10-17 22:09): one request at a time,
// refused while the service is busy or the faces card or the chooser's 3D heads wait for theirs, polled only while this page waits
// (the service hands each answer to whoever polls first), a timeout that frees the service. A player is asked by his player id, a
// manager by his head id (as the miniface editor does), both with their club as the second id.
class CapturePreview : public PreviewSource {
public:
    const char* title() const override { return "Big picture"; }

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
        if (!s.valid()) return false;
        const int64_t req = s.manager ? s.head : s.id;
        if (req <= 0) {
            if (why) *why = "He has no head the game can draw.";
            return false;
        }
        if (!can_request(app, why)) return false;
        capture::Request r;
        r.id = static_cast<int32_t>(req);
        r.second_id = s.club > 0 ? static_cast<int32_t>(s.club) : -1;
        r.manager = s.manager;
        r.camera = kCameraBody;
        r.use_template = false;  // the default descriptor, as the Architect's pre-registered check (FULLBODY_FAST_ROUTE.md section 5)
        r.label = "Looks studio " + s.name;
        std::string err;
        if (!app.capture->request(r, &err)) {
            if (why) *why = "The game did not take the request: " + err;
            return false;
        }
        pending_ = s;
        pending_req_ = req;
        sent_at_ = app.now;
        ++requests_;
        note_.clear();
        app.log("looks studio: picture asked for " + s.name + " (id " + std::to_string(req) + ", camera " + std::to_string(kCameraBody) +
                (s.manager ? ", manager" : "") + ")");
        return true;
    }

    void tick(App& app) override {
        if (!pending_req_ || !app.capture) return;
        capture::Result r;
        if (app.capture->poll(r)) {
            if (r.id != pending_req_) {
                app.log("looks studio: a picture for id " + std::to_string(r.id) + " arrived while waiting for " + std::to_string(pending_req_) + " (dropped)");
                return;
            }
            const int64_t req = pending_req_;
            const Subject who = pending_;
            pending_req_ = 0;
            pending_ = Subject();
            if (!r.ok) {
                note_ = "The game did not draw him: " + r.error;
                app.log("looks studio: no picture for id " + std::to_string(req) + ": " + r.error);
                return;
            }
            shown_ = who;
            image_ = fit_image(r.image, std::max(256, static_cast<int>(S(640.0f))));  // core/image.h: the pane's size, before textures.pixels
            ++version_;
            caption_ = "Big picture, drawn by the game at " + clock_text();
            app.log("looks studio: picture of id " + std::to_string(req) + " (" + r.format + ")");
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
            TextureCache::Pic pic = app.textures.pixels("looks_studio_preview", version_, image_);
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
    bool waiting_for(const Subject& s) const { return pending_req_ != 0 && same(pending_, s); }
    bool pending() const { return pending_req_ != 0; }
    bool drew() const { return drew_; }
    int requests() const { return requests_; }
    void reset() {
        pending_ = shown_ = Subject();
        pending_req_ = 0;
        image_ = Rgba();
        caption_.clear();
        note_.clear();
        requests_ = 0;
    }

private:
    Subject pending_, shown_;
    int64_t pending_req_ = 0;  // the id the request carries (player id, a manager's head id)
    double sent_at_ = 0.0;
    Rgba image_;
    uint64_t version_ = 0;
    std::string caption_, note_;
    int requests_ = 0;
    bool drew_ = false;
};

// ---------------------------------------------------------------- GameScreenHint
// No picture of our own yet: his small picture from the game files, how to get the game's picture, and where the game shows him in full
class GameScreenHint : public PreviewSource {
public:
    const char* title() const override { return "On the game's screen"; }
    bool ready(App&, const Subject&, std::string*) override { return true; }
    void draw(App& app, const Subject& s, float w, float h) override {
        const ImVec2 start = ImGui::GetCursorPos();
        const float side = std::min(w, h) * 0.4f;
        if (s.valid()) {
            std::string pic;
            if (s.manager) pic = app.legacy.first_present(legacy_path::staff_pictures(s.id, s.head));
            else pic = legacy_path::player_miniface(s.id);
            ImGui::SetCursorPos(ImVec2(start.x + (w - side) * 0.5f, start.y + h * 0.15f));
            if (!pic.empty()) draw_legacy_picture(app, pic, side, true);
            else ImGui::Dummy(ImVec2(side, side));
        }
        ImGui::SetCursorPosX(start.x + S(12.0f));
        ImGui::Spacing();
        centred_text("Draw him asks the game for his picture: head and shoulders, as the game draws him now.", w - S(24.0f));
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
    char search[64] = "";
    bool my_club = false;
    std::vector<int64_t> rows;  // players: playerid; managers: index into model.managers()
    std::string list_key;
    uint64_t list_version = ~uint64_t(0);
    // preview
    CapturePreview capture;
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
    std::string body_result;
    bool body_result_error = false;
    // the page's own Undo for managers
    std::vector<ManagerUndo> manager_undo;
    // the question before a manager's real face
    bool open_ask = false;
    bool open_chooser = false;
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

// A write of the page went through: his look is read again now, and a manager's step goes on the page's own Undo
void note_write(Studio& s, const Subject& who, uint64_t rec, const std::string& field, const Value& before, bool have_before) {
    s.signature_dirty = true;
    if (!who.manager || !have_before) return;
    s.manager_undo.push_back({who.id, rec, field, before});
    if (s.manager_undo.size() > kManagerUndoSteps) s.manager_undo.erase(s.manager_undo.begin());
}

// One field through the existing combo editor (App::edit, App's Undo for players)
bool field_combo(App& app, Studio& s, const Table& t, uint64_t rec, const Subject& who, const char* field, const char* label) {
    const Field* f = t.field(field);
    if (!f) return false;
    Value before;
    const bool have = app.db.get(t, rec, *f, before);
    ++s.st.fields;
    const bool wrote = enum_editor(app, t, rec, *f, label, S(220.0f));
    if (wrote) note_write(s, who, rec, field, before, have);
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

    // the source: the game's picture of him when there is one (or one being drawn), else the hint
    std::string why;
    PreviewSource* src = &s.hint;
    if (s.capture.ready(app, who, &why)) src = &s.capture;
    s.st.source = src->title();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float w = std::max(S(120.0f), avail.x);
    const float h = std::max(S(160.0f), std::min(avail.y - S(130.0f), w));  // the picture is square
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), S(4.0f));
    src->draw(app, who, w, h);
    s.st.picture = src == &s.capture && s.capture.drew();
    s.st.waiting = s.capture.waiting_for(who);

    s.st.preview_line = s.capture.line(who);
    if (!s.preview_why.empty()) s.st.preview_line += (s.st.preview_line.empty() ? "" : " ") + s.preview_why;
    if (!s.st.preview_line.empty()) hint_line(s.st.preview_line.c_str());
    if (s.st.risky_body) {
        ImGui::PushTextWrapPos(0.0f);
        theme::warn_text("%s", "His body was made for another player's head. A picture of it is untested and may stop the game's drawing: "
                               "the Draw button asks anyway.");
        ImGui::PopTextWrapPos();
    }
    std::string cant;
    const bool can = s.capture.can_request(app, &cant);
    if (!can) ImGui::BeginDisabled();
    const bool have_picture = s.st.picture || s.st.waiting;
    const std::string draw_label = std::string(have_picture ? "Draw again" : "Draw him") + "###lsdraw";
    if (ImGui::Button(draw_label.c_str())) {
        std::string err;
        if (s.capture.refresh(app, who, &err)) {
            s.want_picture = false;
            s.drawn_signature = s.signature;
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game draws this picture, but nobody has used this page in a real game yet.");
    // the picture is head and shoulders (Tester 2026-10-17 22:09): say what it cannot show, and where the game shows him in full
    hint_line(who.manager ? "This picture shows head and shoulders only. A manager's picture arrives; whether it changes after a change made "
                            "here is not proven yet."
                          : "This picture shows head and shoulders only. To see him in full: the game's Customise > Edit Players > Edit Real "
                            "Players (re-open him after a change: the game rebuilds him when he is selected).");
}

// His look changed while the page is shown (a card here, Undo, the face chooser, another page): ask the game for his picture once it
// can draw (players and managers; never by itself for a player's body made for another head). Picking a person asks nothing: "Draw him" does, so the
// game's one capture at a time is never taken just by browsing the list.
void auto_picture(App& app, Studio& s, const Subject& who, const Table* t, uint64_t rec) {
    const bool same = who.manager == s.last.manager && who.id == s.last.id;
    if (!same) {
        s.last = who;
        s.baseline = true;
        s.signature_dirty = true;
        s.want_picture = false;
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
    if (s.capture.refresh(app, who, &why)) {
        s.want_picture = false;
        s.drawn_signature = s.signature;
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
        if (app.undo(who.id)) s.signature_dirty = true;
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
        if (choose) s.open_ask = true;  // a manager's new face cannot be undone: ask first
        if (s.open_ask) {
            ImGui::OpenPopup("Give him a real face?###lsask");
            s.open_ask = false;
        }
        ImGui::SetNextWindowSize(ImVec2(S(420.0f), 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Give him a real face?###lsask", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            s.st.asking = true;
            ImGui::PushTextWrapPos(S(400.0f));
            ImGui::TextUnformatted("His head and his picture change as soon as you pick a face. The Undo button can't bring them back.");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Choose a face###lsaskok")) {
                s.open_chooser = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel###lsaskno")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (s.open_chooser) {
            ImGui::OpenPopup(kManagerFacePopup);
            s.open_chooser = false;
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
    if (hc != 0) {
        // a generic head takes the hair and beard of the fields
        for (const auto& f : {std::make_pair("hairtypecode", "Hair"), std::make_pair("haircolorcode", "Hair colour"),
                              std::make_pair("facialhairtypecode", "Beard"), std::make_pair("facialhaircolorcode", "Beard colour")})
            field_combo(app, s, t, rec, who, f.first, f.second);
    } else {
        hint_line("Hair & beard has its own way back: His own hair & beard.");
    }
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
    // names only (2.17.0): the tiles flow in rows of the card's width; only the rows on screen are drawn
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tw = S(190.0f);
    const float avail = ImGui::GetContentRegionAvail().x;
    const int per_row = std::max(1, static_cast<int>((avail + style.ItemSpacing.x) / (tw + style.ItemSpacing.x)));
    const int rows = (static_cast<int>(shown.size()) + per_row - 1) / per_row;
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
                const std::string label = tile.caption + "##bt" + std::to_string(tile.code);
                if (ImGui::Selectable(label.c_str(), tile.code == current, 0, ImVec2(tw, 0.0f))) picked = tile.code;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tile.tip.c_str());
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

void cards_pane(App& app, Studio& s, const Subject& who, const Table* t, uint64_t rec) {
    if (!who.valid() || !t) {
        hint_line("The cards show once you pick someone.");
        return;
    }
    undo_button(app, s, who);
    ImGui::Spacing();
    face_card(app, s, who, *t, rec);
    hair_card(app, s, who, *t, rec);
    body_card(app, s, who, *t, rec);
    fields_card(app, s, who, *t, rec, Card::Accessories);
    fields_card(app, s, who, *t, rec, Card::KitFit);
}

}  // namespace

// ---------------------------------------------------------------- public: preview sources
std::unique_ptr<PreviewSource> make_capture_preview() { return std::make_unique<CapturePreview>(); }
std::unique_ptr<PreviewSource> make_game_screen_hint() { return std::make_unique<GameScreenHint>(); }

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

void tick_looks_studio(App& app) { studio().capture.tick(app); }

void looks_studio_reset_for_tests() {
    Studio& s = studio();
    s.capture.reset();
    s.managers = false;
    s.search[0] = 0;
    s.my_club = false;
    s.rows.clear();
    s.list_key.clear();
    s.list_version = ~uint64_t(0);
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
    s.manager_undo.clear();
    s.open_ask = s.open_chooser = false;
    s.st = StudioState();
    g_timeout = 25.0;
}

void looks_studio_set_timeout_for_tests(double seconds) { g_timeout = seconds; }
void looks_studio_show_managers(bool managers) { studio().managers = managers; }

// ---------------------------------------------------------------- the page
void draw_looks_studio(App& app) {
    Studio& s = studio();
    tick_looks_studio(app);
    const int requests = s.capture.requests();
    s.st = StudioState();
    s.st.requests = requests;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted("Looks studio");
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
    ImGui::BeginChild("##lslist", ImVec2(list_w, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    person_list(app, s);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##lspreview", ImVec2(preview_w, 0), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
    preview_pane(app, s, who);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##lscards", ImVec2(0, 0), ImGuiChildFlags_Borders);
    cards_pane(app, s, who, t, rec);
    ImGui::EndChild();
    s.st.requests = s.capture.requests();
}

}  // namespace looks
}  // namespace turbo
