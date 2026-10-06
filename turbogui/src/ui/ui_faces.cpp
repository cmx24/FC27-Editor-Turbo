// FC 27 LE Turbo GUI - the real-face chooser (see ui_faces.h). One chooser body serves both targets:
//   Players > Appearance > Choose a real face...   player heads -> the player (players table)
//   Managers > Appearance > Choose a real face...  player heads or manager heads -> the manager (manager table)
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

#include "app.h"
#include "core/image.h"
#include "core/player_capture.h"
#include "imgui.h"
#include "ui_faces.h"

namespace turbo {

namespace fs = std::filesystem;
using faces::Face;
using faces::Facet;

static std::string lower_text(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------- heads
static std::vector<Face> g_pfaces, g_mfaces;
static uint64_t g_pfaces_version = ~uint64_t(0), g_mfaces_version = ~uint64_t(0);
static uint64_t g_pfaces_looks = 0;  // face3d::generation() the player heads' look pointers come from

static void facet_fields(const Table& t, const Field* out[faces::kFacetCount]) {
    for (int i = 0; i < faces::kFacetCount; ++i) out[i] = t.field(faces::facet_field(static_cast<Facet>(i)));
}

const std::vector<Face>& player_faces(App& app) {
    if (g_pfaces_version == app.model.version() && g_pfaces_looks == face3d::generation()) return g_pfaces;
    g_pfaces_version = app.model.version();
    g_pfaces_looks = face3d::generation();
    g_pfaces.clear();
    const Table* t = app.db.table("players");
    if (!t || !t->has("headclasscode") || !t->has("headassetid")) return g_pfaces;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *t)) return g_pfaces;
    const Field* hc = t->field("headclasscode");
    const Field* ha = t->field("headassetid");
    const Field* pid = t->field("playerid");
    const Field* hq = t->field("hashighqualityhead");
    if (!hc || !ha || !pid) return g_pfaces;
    const Field* ff[faces::kFacetCount];
    facet_fields(*t, ff);
    for (uint32_t idx : snap.valid) {
        Face r;
        r.id = snap.get_int(idx, *pid);
        r.headassetid = snap.get_int(idx, *ha);
        if (r.headassetid <= 0) continue;
        const int64_t cls = snap.get_int(idx, *hc);
        r.real = cls == 0 && (!hq || snap.get_int(idx, *hq) != 0);  // a real face: head class 0 and a high-quality head
        if (!r.real && cls != 0 && r.headassetid != r.id) continue;  // generic head: nothing to pick
        for (int i = 0; i < faces::kFacetCount; ++i) r.raw[i] = ff[i] ? snap.get_int(idx, *ff[i]) : faces::kNoValue;
        faces::set_look(r, face3d::lookup(r.id));  // the 3D facets: what the game's 3D render of his head shows
        if (const PlayerRow* p = app.model.player(r.id)) {
            r.name = p->name;
            r.overall = p->overall;
        } else {
            r.name = app.model.player_name(r.id);
        }
        r.lname = lower_text(r.name);
        g_pfaces.push_back(std::move(r));
    }
    std::sort(g_pfaces.begin(), g_pfaces.end(), [](const Face& a, const Face& b) { return a.name < b.name; });
    return g_pfaces;
}

const std::vector<Face>& manager_faces(App& app) {
    if (g_mfaces_version == app.model.version()) return g_mfaces;
    g_mfaces_version = app.model.version();
    g_mfaces.clear();
    const Table* t = app.db.table("manager");
    if (!t || !t->has("headassetid")) return g_mfaces;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *t)) return g_mfaces;
    std::unordered_map<int64_t, const ManagerRow*> by_id;
    for (const auto& m : app.model.managers()) by_id[m.managerid] = &m;
    const Field* mid = t->field("managerid");
    const Field* ha = t->field("headassetid");
    const Field* hc = t->field("headclasscode");
    const Field* ff[faces::kFacetCount];
    facet_fields(*t, ff);
    for (uint32_t idx : snap.valid) {
        Face r;
        r.manager = true;
        r.id = mid ? snap.get_int(idx, *mid) : 0;
        r.headassetid = snap.get_int(idx, *ha);
        if (r.headassetid <= 0) continue;
        r.real = !hc || snap.get_int(idx, *hc) == 0;
        for (int i = 0; i < faces::kFacetCount; ++i) r.raw[i] = ff[i] ? snap.get_int(idx, *ff[i]) : faces::kNoValue;
        auto it = by_id.find(r.id);
        r.name = it != by_id.end() ? it->second->name : "Manager " + std::to_string(r.id);
        r.lname = lower_text(r.name);
        g_mfaces.push_back(std::move(r));
    }
    std::sort(g_mfaces.begin(), g_mfaces.end(), [](const Face& a, const Face& b) { return a.name < b.name; });
    return g_mfaces;
}

size_t real_face_count(App& app) {
    size_t n = 0;
    for (const auto& f : player_faces(app)) n += f.real ? 1 : 0;
    return n;
}

static std::string face_picture(const Face& f) {
    return f.manager ? legacy_path::staff_miniface(f.headassetid) : legacy_path::player_miniface(f.id);
}

// ---------------------------------------------------------------- giving the head
// Fields copied from the chosen head's owner (only those both tables have)
static const char* kHeadFields[] = {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation"};
static const char* kHairFields[] = {"hairtypecode", "haircolorcode", "hairstylecode"};
static const char* kBeardFields[] = {"facialhairtypecode", "facialhaircolorcode"};
static const char* kEyeFields[] = {"eyecolorcode", "eyebrowcode", "eyedetail"};
static const char* kSkinFields[] = {"skintonecode", "skintypecode", "skincomplexion", "skinsurfacepack", "skinmakeup"};

static bool copy_head(App& app, const Table& src, uint64_t src_rec, const Table& dst, uint64_t dst_rec, const RealFaceOptions& o,
                      std::string* msg) {
    std::vector<const char*> fields(std::begin(kHeadFields), std::end(kHeadFields));
    if (o.hair) fields.insert(fields.end(), std::begin(kHairFields), std::end(kHairFields));
    if (o.beard) fields.insert(fields.end(), std::begin(kBeardFields), std::end(kBeardFields));
    if (o.eyes) fields.insert(fields.end(), std::begin(kEyeFields), std::end(kEyeFields));
    if (o.skin) fields.insert(fields.end(), std::begin(kSkinFields), std::end(kSkinFields));
    // read everything first, then write (validated per field)
    std::vector<std::pair<const Field*, Value>> writes;
    for (const char* n : fields) {
        const Field* fs_ = src.field(n);
        const Field* fd = dst.field(n);
        if (!fs_ || !fd) continue;
        Value v;
        if (!app.db.get(src, src_rec, *fs_, v)) {
            if (msg) *msg = std::string("cannot read ") + n;
            return false;
        }
        std::string verr = Database::validate(*fd, v);
        if (!verr.empty()) {
            if (msg) *msg = verr;
            return false;
        }
        writes.emplace_back(fd, v);
    }
    if (writes.empty()) {
        if (msg) *msg = "no head fields in the " + dst.name + " table";
        return false;
    }
    for (const auto& w : writes) {
        if (!app.edit(dst, dst_rec, *w.first, w.second)) {
            if (msg) *msg = "writing " + w.first->name + " failed";
            return false;
        }
    }
    return true;
}

bool apply_real_face(App& app, int64_t target_pid, int64_t owner_pid, const RealFaceOptions& o, std::string* msg) {
    const Table* t = app.db.table("players");
    const PlayerRow* target = app.model.player(target_pid);
    const PlayerRow* owner = app.model.player(owner_pid);
    if (!t || !target || !owner) {
        if (msg) *msg = "player not found";
        return false;
    }
    if (!copy_head(app, *t, owner->rec, *t, target->rec, o, msg)) return false;
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

static const ManagerRow* find_manager(App& app, int64_t managerid) {
    for (const auto& m : app.model.managers())
        if (m.managerid == managerid) return &m;
    return nullptr;
}

bool apply_real_face_to_manager(App& app, int64_t managerid, int64_t owner_id, bool owner_is_manager, const RealFaceOptions& o,
                                std::string* msg) {
    const Table* mt = app.db.table("manager");
    const Table* st = owner_is_manager ? mt : app.db.table("players");
    const ManagerRow* target = find_manager(app, managerid);
    uint64_t src_rec = 0;
    std::string owner_name;
    if (owner_is_manager) {
        if (const ManagerRow* m = find_manager(app, owner_id)) {
            src_rec = m->rec;
            owner_name = m->name;
        }
    } else if (const PlayerRow* p = app.model.player(owner_id)) {
        src_rec = p->rec;
        owner_name = p->name;
    }
    if (!mt || !st || !target || !src_rec) {
        if (msg) *msg = target ? "head owner not found" : "manager not found";
        return false;
    }
    if (!copy_head(app, *st, src_rec, *mt, target->rec, o, msg)) return false;
    const int64_t head = app.db.get_int(*mt, target->rec, "headassetid", 0);
    std::string note = "head of " + owner_name + " given to " + target->name;
    if (owner_is_manager) {
        note += "; he shows that head's miniface";  // heads_staff_<head id>: the same picture
    } else if (o.miniface && head > 0) {
        // managers show data/ui/imgAssets/heads_staff/heads_staff_<head id>.dds (512 x 512): the player's miniface, scaled up
        const std::string staff = legacy_path::staff_miniface(head);
        fs::path f;
        LegacyImages::State ls = app.legacy.locate(legacy_path::player_miniface(owner_id), &f, true);
        if (ls == LegacyImages::State::Custom || ls == LegacyImages::State::Game) {
            Rgba img;
            std::string err;
            if (load_image_file(f, img, &err) &&
                app.legacy.save_custom(staff, encode_dds_dxt5(frame_image(img, kStaffMinifaceSize, Framing())), &err)) {
                app.textures.forget(app.legacy.custom_file(staff));
                note += "; his miniface is copied to heads_staff_" + std::to_string(head);
            } else {
                note += "; miniface not copied (" + err + ")";
            }
        } else {
            note += "; miniface not copied (the game's picture is not loaded yet)";
        }
    }
    if (msg) *msg = note;
    return true;
}

// ---------------------------------------------------------------- the chooser
struct Chooser {
    char search[64] = "";
    faces::Filter filter;
    int sort = faces::kSortName;
    bool real_only = true;
    int only3d = -1;  // "Only heads with a 3D look": -1 until the user sets it (on when the list has kOnly3dDefaultMin looks)
    bool manager_heads = false;  // manager target: managers' heads instead of players'
    RealFaceOptions opts;
};
static Chooser g_player_ch, g_manager_ch;

// Next button on the same line when it fits before line_end, else on a new line
static void flow(bool& first, float w, float line_end) {
    if (first) {
        first = false;
        return;
    }
    ImGui::SameLine();
    if (ImGui::GetCursorScreenPos().x + w > line_end) ImGui::NewLine();
}

static std::string value_label(Facet fc, const faces::Count& c) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s (%d)##v%lld", faces::key_label(fc, c.key).c_str(), c.n, static_cast<long long>(c.key));
    return buf;
}

// 3D renders of the heads (defined with the render queue below)
static bool face3d_cached(App& app, bool manager, int64_t id);
static fs::path face3d_file(App& app, bool manager, int64_t id);
static int64_t head3d_id(const Face& f);

// "Hair colour: Any" button and its list of values (count of heads per value, with a picture of each). The 3D facets list
// every value of the look (those no head has under the other filters greyed out) and show a 3D render as the picture.
static void facet_button(App& app, Chooser& ch, const std::vector<Face>& list, const std::vector<size_t>& pool, Facet fc, bool& first,
                         float line_end, bool looks3d) {
    const int64_t sel = ch.filter.sel[fc];
    std::string lbl = std::string(faces::facet_title(fc, looks3d)) + ": " + faces::key_label(fc, sel) + "##facet" + std::to_string(int(fc));
    const float w = ImGui::CalcTextSize(lbl.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    flow(first, w, line_end);
    const std::string pop = "##facetpop" + std::to_string(int(fc));
    if (sel != faces::kAny) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(lbl.c_str())) ImGui::OpenPopup(pop.c_str());
    if (sel != faces::kAny) ImGui::PopStyleColor();
    if (!ImGui::BeginPopup(pop.c_str())) return;
    std::vector<faces::Count> counts = faces::facet_counts(list, pool, ch.filter, fc);
    int any = 0;
    for (size_t i : pool) any += faces::matches(list[i], ch.filter, fc) ? 1 : 0;
    char anyl[64];
    std::snprintf(anyl, sizeof(anyl), "Any (%d)##vany", any);
    if (ImGui::Selectable(anyl, sel == faces::kAny)) {
        ch.filter.sel[fc] = faces::kAny;
        ImGui::CloseCurrentPopup();
    }
    const float thumb = S(36.0f);
    const float row_h = thumb + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##fvals", ImVec2(S(330.0f), std::min(S(420.0f), row_h * float(std::max<size_t>(counts.size(), 1)) + S(8.0f))));
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(counts.size()), row_h);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const faces::Count& c = counts[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const Face* sample = c.n > 0 && c.sample < list.size() ? &list[c.sample] : nullptr;
            if (!sample) {
                ImGui::Dummy(ImVec2(thumb, thumb));  // a 3D look value no head has under the other filters
            } else if (faces::facet_3d(fc) && head3d_id(*sample) > 0 && face3d_cached(app, sample->manager, head3d_id(*sample))) {
                draw_file_picture(app, face3d_file(app, sample->manager, head3d_id(*sample)), thumb);
            } else {
                // a style group shows a listed style of its heads (one with a game preview), the other groups the first head's miniface
                std::string pic = faces::facet_has_pictures(fc) ? faces::facet_picture(fc, sample->raw[fc]) : std::string();
                if (pic.empty()) pic = face_picture(*sample);
                draw_legacy_picture(app, pic, thumb, true);
            }
            ImGui::SameLine();
            if (ImGui::Selectable(value_label(fc, c).c_str(), sel == c.key, sample ? 0 : ImGuiSelectableFlags_Disabled, ImVec2(0, thumb))) {
                ch.filter.sel[fc] = c.key;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
    }
    if (counts.empty()) ImGui::TextDisabled("No head matches the other filters.");
    ImGui::EndChild();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- 3D heads (the game renders each head from its 3D model)
// The chooser asks the capture service (app.capture, docs/re/player_capture.md) for the heads on screen, one request in
// flight, never waiting for it: each frame polls once. A render is saved as a 256 x 256 DXT5 DDS in
// turbo_output/cache/faces3d (p_<playerid>.dds, m_<headid>.dds) and drawn instead of the miniface from then on (kept
// across sessions). "Render all filtered heads" queues the whole filtered list.
namespace {
struct Head3d {
    bool manager = false;
    int32_t id = 0;  // playerid, or the head id for a manager head (what the game renders)
    int32_t teamid = -1;  // the club, passed as the capture's second id like the Miniface editor does (-1 = none)
    std::string label;
};
struct Render3d {
    std::deque<Head3d> queue;
    std::unordered_set<int64_t> queued;
    std::unordered_map<int64_t, bool> cached;  // file exists (checked once per head)
    bool pending = false;
    Head3d cur;
    double since = 0.0;
    bool batch = false;  // "Render all filtered heads" running
    size_t batch_total = 0, batch_done = 0, failed = 0;
    std::string note;
};
Render3d g_r3d;
constexpr size_t kVisibleQueueMax = 3 * capture::kMaxPlayersPerRequest;  // heads on screen waiting (not a batch)
constexpr double kRenderTimeout = 20.0;                                     // seconds before a render is given up
}  // namespace

static int64_t head3d_key(bool manager, int64_t id) { return (manager ? (int64_t(1) << 40) : 0) + id; }
static fs::path faces3d_dir(App& app) { return app.legacy.cache_dir().parent_path() / "faces3d"; }
static fs::path face3d_file(App& app, bool manager, int64_t id) {
    return faces3d_dir(app) / ((manager ? "m_" : "p_") + std::to_string(id) + ".dds");
}
// The id the game renders for a head of the chooser: players their playerid, managers their head id
static int64_t head3d_id(const Face& f) { return f.manager ? f.headassetid : f.id; }

static bool face3d_cached(App& app, bool manager, int64_t id) {
    const int64_t k = head3d_key(manager, id);
    auto it = g_r3d.cached.find(k);
    if (it != g_r3d.cached.end()) return it->second;
    std::error_code ec;
    const bool ok = fs::is_regular_file(face3d_file(app, manager, id), ec);
    g_r3d.cached[k] = ok;
    return ok;
}

static bool capture_usable(App& app) {
    if (!app.capture) return false;
    return app.capture->status().installed;
}

static void queue_head3d(App& app, const Face& f, bool front) {
    const int64_t id = head3d_id(f);
    if (id <= 0 || face3d_cached(app, f.manager, id)) return;
    const int64_t k = head3d_key(f.manager, id);
    if (g_r3d.queued.count(k)) return;
    if (g_r3d.pending && g_r3d.cur.manager == f.manager && g_r3d.cur.id == id) return;
    Head3d h;
    h.manager = f.manager;
    h.id = static_cast<int32_t>(id);
    if (f.manager) {
        if (const ManagerRow* m = find_manager(app, f.id); m && m->teamid > 0) h.teamid = static_cast<int32_t>(m->teamid);
    } else if (const PlayerRow* pr = app.model.player(f.id); pr && pr->club > 0) {
        h.teamid = static_cast<int32_t>(pr->club);
    }
    h.label = f.name;
    if (front) g_r3d.queue.push_front(h);
    else g_r3d.queue.push_back(h);
    g_r3d.queued.insert(k);
}

static bool save_head3d(App& app, const Head3d& h, const Rgba& img, std::string* err) {
    if (img.empty()) {
        if (err) *err = "empty picture";
        return false;
    }
    std::error_code ec;
    fs::create_directories(faces3d_dir(app), ec);
    const Rgba sq = frame_image(img, 256, Framing{});
    const std::vector<uint8_t> dds = encode_dds_dxt5(sq);
    const fs::path f = face3d_file(app, h.manager, h.id);
    fs::path tmp = f;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        o.write(reinterpret_cast<const char*>(dds.data()), static_cast<std::streamsize>(dds.size()));
        if (!o) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, f, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    app.textures.forget(f);
    g_r3d.cached[head3d_key(h.manager, h.id)] = true;
    return true;
}

// Once per frame of the chooser: collect a finished render, or send the next one (never waits)
static void tick_head3d(App& app) {
    capture::CaptureService* svc = app.capture.get();
    if (!svc) return;
    const double now = ImGui::GetTime();
    if (g_r3d.pending) {
        capture::Result r;
        if (svc->poll(r)) {
            g_r3d.pending = false;
            if (g_r3d.batch) ++g_r3d.batch_done;
            std::string err;
            if (r.ok && r.id == g_r3d.cur.id && save_head3d(app, g_r3d.cur, r.image, &err)) {
                g_r3d.note.clear();
            } else {
                ++g_r3d.failed;
                g_r3d.note = g_r3d.cur.label + ": " + (r.ok ? (r.id != g_r3d.cur.id ? std::string("answer for another id") : err) : r.error);
            }
        } else if (now - g_r3d.since > kRenderTimeout) {
            svc->cancel();
            g_r3d.pending = false;
            if (g_r3d.batch) ++g_r3d.batch_done;
            ++g_r3d.failed;
            g_r3d.note = g_r3d.cur.label + ": no answer from the game";
        }
        return;
    }
    if (g_r3d.queue.empty()) {
        g_r3d.batch = false;
        return;
    }
    const capture::Status st = svc->status();
    if (!st.installed || !st.available || st.busy) return;
    Head3d h = g_r3d.queue.front();
    g_r3d.queue.pop_front();
    g_r3d.queued.erase(head3d_key(h.manager, h.id));
    if (face3d_cached(app, h.manager, h.id)) {
        if (g_r3d.batch) ++g_r3d.batch_done;
        return;
    }
    capture::Request req;
    req.id = h.id;
    req.second_id = h.teamid;
    req.manager = h.manager;
    req.camera = 0;  // portrait: the game's card / manager head framing
    req.use_template = false;  // the player / staff descriptor (verified live); the learned one may be the avatar portrait
    req.label = h.label;
    std::string err;
    if (svc->request(req, &err)) {
        g_r3d.pending = true;
        g_r3d.cur = h;
        g_r3d.since = now;
    } else {
        if (g_r3d.batch) ++g_r3d.batch_done;
        ++g_r3d.failed;
        g_r3d.note = h.label + ": " + err;
    }
}

static void stop_head3d() {
    g_r3d.queue.clear();
    g_r3d.queued.clear();
    g_r3d.batch = false;
}

// A grid cell drawing the 3D render (cached file); true when clicked
static bool face3d_cell(App& app, const fs::path& file, float cell, const std::string& caption, const char* id) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(cell, cell + line));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell + line), ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    ImGui::SetCursorScreenPos(p);
    draw_file_picture(app, file, cell);
    const ImVec4 clip(p.x, p.y + cell, p.x + cell, p.y + cell + line);
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + 2.0f, p.y + cell + 1.0f), ImGui::GetColorU32(ImGuiCol_Text), caption.c_str(), nullptr, 0.0f, &clip);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + cell + line));
    ImGui::Dummy(ImVec2(cell, 0.0f));
    return clicked;
}

// "Render all filtered heads" / progress / Stop, or why the 3D heads are off
static void head3d_bar(App& app, const std::vector<const Face*>& rows) {
    if (!capture_usable(app)) {
        ImGui::TextDisabled("3D heads off (the game's capture hooks are not running): showing the minifaces.");
        return;
    }
    if (g_r3d.batch) {
        const size_t total = std::max<size_t>(g_r3d.batch_total, 1);
        const size_t done = std::min(g_r3d.batch_done, total);
        char ov[96];
        std::snprintf(ov, sizeof(ov), "3D renders %zu / %zu", done, total);
        ImGui::ProgressBar(float(done) / float(total), ImVec2(S(260.0f), 0.0f), ov);
        ImGui::SameLine();
        if (ImGui::Button("Stop##r3d")) stop_head3d();
    } else {
        size_t missing = 0;
        for (const Face* f : rows)
            if (head3d_id(*f) > 0 && !face3d_cached(app, f->manager, head3d_id(*f))) ++missing;
        if (missing > 0) {
            char bl[96];
            std::snprintf(bl, sizeof(bl), "Render all filtered heads (%zu)##r3dall", missing);
            if (ImGui::Button(bl)) {
                g_r3d.queue.clear();
                g_r3d.queued.clear();
                for (const Face* f : rows) queue_head3d(app, *f, false);
                g_r3d.batch = true;
                g_r3d.batch_total = g_r3d.queue.size();
                g_r3d.batch_done = 0;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Asks the game to render each filtered head from its 3D model, one at a time, in the background.\n"
                                  "Renders are kept in %s", faces3d_dir(app).string().c_str());
        } else {
            ImGui::TextDisabled("Every filtered head has its 3D render.");
        }
    }
    if (g_r3d.failed > 0 || !g_r3d.note.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu failed%s%s", g_r3d.failed, g_r3d.note.empty() ? "" : ", last: ", g_r3d.note.c_str());
    }
}

// Filters, sort, what to copy, and the grid of heads. Returns the head clicked (nullptr when none).
static const Face* chooser_body(App& app, Chooser& ch, bool manager_target) {
    const bool mheads = manager_target && ch.manager_heads;
    const std::vector<Face>& list = mheads ? manager_faces(app) : player_faces(app);
    const Table* src = app.db.table(mheads ? "manager" : "players");
    const float row_end = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    // 3D looks (core/face3d_looks.h): player heads classified from the game's 3D renders; manager heads have none
    const size_t looked = mheads ? 0 : faces::count_looks(list);
    const bool looks3d = looked > 0;
    if (ch.only3d < 0 && looked >= faces::kOnly3dDefaultMin) ch.only3d = 1;
    const bool only3d = looks3d && ch.only3d > 0;
    faces::drop_hidden(ch.filter, looks3d);
    if (manager_target) {
        if (ImGui::RadioButton("Player heads", !ch.manager_heads)) {
            ch.manager_heads = false;
            ch.filter.clear();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Manager heads", ch.manager_heads)) {
            ch.manager_heads = true;
            ch.filter.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Heads of the managers in the database (heads_staff minifaces)");
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##facesearch", "name or ID", ch.search, sizeof(ch.search));
    ImGui::SameLine();
    ImGui::Checkbox("Real faces only", &ch.real_only);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", mheads ? "Only managers with head class 0 (a real face)"
                                       : "Only players with headclasscode 0 (a real face head model) and hashighqualityhead 1");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(200.0f));
    if (ImGui::BeginCombo("Sort##facesort", faces::sort_title(static_cast<faces::Sort>(ch.sort)))) {
        for (int s = 0; s < faces::kSortCount; ++s)
            if (ImGui::Selectable(faces::sort_title(static_cast<faces::Sort>(s)), ch.sort == s)) ch.sort = s;
        ImGui::EndCombo();
    }
    if (looks3d) {
        bool on = only3d;
        bool not_first = false;
        flow(not_first, ImGui::CalcTextSize("Only heads with a 3D look").x + ImGui::GetFrameHeight() + S(8.0f), row_end);
        if (ImGui::Checkbox("Only heads with a 3D look", &on)) ch.only3d = on ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%zu of %zu heads have a 3D look: skin, hair, facial hair and headwear judged from the game's 3D render.\n"
                              "In those filters the other heads are \"not classified yet\".",
                              looked, list.size());
    }

    // heads matching the search and "real faces only"; the filters below narrow them
    const std::string q = lower_text(ch.search);
    const bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), ::isdigit);
    std::vector<size_t> pool;
    for (size_t i = 0; i < list.size(); ++i) {
        const Face& f = list[i];
        if (ch.real_only && !f.real) continue;
        if (only3d && !f.look) continue;
        if (!q.empty()) {
            if (numeric ? (std::to_string(f.id).find(q) != 0 && std::to_string(f.headassetid).find(q) != 0) : f.lname.find(q) == std::string::npos)
                continue;
        }
        pool.push_back(i);
    }

    const float line_end = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool first = true;
    for (int i = 0; i < faces::facet_order_size(looks3d); ++i) {
        const Facet fc = faces::facet_order(looks3d)[i];
        if (!faces::facet_3d(fc) && (!src || !src->has(faces::facet_field(fc)))) continue;  // FC 27 has them all; older tables may not
        facet_button(app, ch, list, pool, fc, first, line_end, looks3d);
    }
    if (ch.filter.active()) {
        flow(first, ImGui::CalcTextSize("Clear filters").x + ImGui::GetStyle().FramePadding.x * 2.0f, line_end);
        if (ImGui::Button("Clear filters")) ch.filter.clear();
    }

    ImGui::TextDisabled("Also give:");
    ImGui::SameLine();
    ImGui::Checkbox("Hair", &ch.opts.hair);
    ImGui::SameLine();
    ImGui::Checkbox("Beard", &ch.opts.beard);
    ImGui::SameLine();
    ImGui::Checkbox("Eyes", &ch.opts.eyes);
    ImGui::SameLine();
    ImGui::Checkbox("Skin", &ch.opts.skin);
    if (!mheads) {
        ImGui::SameLine();
        ImGui::Checkbox("His miniface too", &ch.opts.miniface);
        if (manager_target && ImGui::IsItemHovered())
            ImGui::SetTooltip("Copies the player's miniface into the head's heads_staff picture (512 x 512), which the manager shows");
    }

    std::vector<const Face*> rows;
    for (size_t i : pool)
        if (faces::matches(list[i], ch.filter)) rows.push_back(&list[i]);
    faces::sort_faces(rows, static_cast<faces::Sort>(ch.sort));
    if (!q.empty() && !numeric && q.size() < 2) ImGui::TextDisabled("Type at least 2 letters");
    ImGui::TextDisabled("%zu of %zu heads (%s)", rows.size(), pool.size(),
                        mheads ? "managers' heads with their minifaces"
                               : (ch.real_only ? "real-face players with their minifaces" : "every player with a head model"));

    const bool use3d = capture_usable(app);
    tick_head3d(app);
    head3d_bar(app, rows);
    const float cell = S(96.0f);
    const Face* picked = nullptr;
    ImGui::BeginChild("##facegrid", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
    const int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / (cell + ImGui::GetStyle().ItemSpacing.x)));
    const int nrows = int((rows.size() + size_t(cols) - 1) / size_t(cols));
    ImGuiListClipper clip;
    clip.Begin(nrows, cell + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y);
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                const size_t i = size_t(r) * size_t(cols) + size_t(c);
                if (i >= rows.size()) break;
                const Face* f = rows[i];
                if (c) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(static_cast<int>(f->id));
                char bid[48];
                std::snprintf(bid, sizeof(bid), "%s%lld", f->manager ? "mface" : "face", static_cast<long long>(f->id));
                const int64_t rid = head3d_id(*f);
                const bool has3d = use3d && rid > 0 && face3d_cached(app, f->manager, rid);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                if (has3d) {
                    if (face3d_cell(app, face3d_file(app, f->manager, rid), cell, f->name, bid)) picked = f;
                } else {
                    if (picture_cell(app, face_picture(*f), false, cell, f->name, bid, false)) picked = f;
                    if (use3d && rid > 0) {
                        if (!g_r3d.batch && g_r3d.queue.size() < kVisibleQueueMax) queue_head3d(app, *f, true);  // on screen: first
                        ImGui::GetWindowDrawList()->AddText(ImVec2(cp.x + 3.0f, cp.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), "3D pending");
                    }
                }
                if (ImGui::IsItemHovered()) {
                    const std::string traits = faces::head_traits(*f, looks3d);  // "3D look: ..." first when the list has looks
                    ImGui::BeginTooltip();
                    if (has3d) draw_file_picture(app, face3d_file(app, f->manager, rid), S(256.0f));  // big preview of the 3D render
                    ImGui::Text("%s\n%s %lld, head %lld%s", f->name.c_str(), f->manager ? "manager" : "player",
                                static_cast<long long>(f->id), static_cast<long long>(f->headassetid), traits.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
                ImGui::EndGroup();
            }
        }
    }
    if (rows.empty()) ImGui::TextDisabled("No head matches. Clear filters or the search.");
    ImGui::EndChild();
    if (app.legacy.waiting() > 0) ImGui::TextDisabled("%zu pictures loading in the background", app.legacy.waiting());
    return picked;
}

void real_face_picker(App& app, int64_t target_pid) {
    ImGui::SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose a real face", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (const Face* f = chooser_body(app, g_player_ch, false)) {
        const int64_t owner = f->id;
        std::string msg;
        const bool ok = apply_real_face(app, target_pid, owner, g_player_ch.opts, &msg);
        app.notify(msg, !ok);
        if (ok) ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Close##faces")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void manager_real_face_picker(App& app, int64_t managerid) {
    ImGui::SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kManagerFacePopup, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (const Face* f = chooser_body(app, g_manager_ch, true)) {
        const int64_t owner = f->id;
        const bool owner_mgr = f->manager;
        std::string msg;
        const bool ok = apply_real_face_to_manager(app, managerid, owner, owner_mgr, g_manager_ch.opts, &msg);
        app.notify(msg, !ok);
        if (ok) ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Close##mfaces")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Managers > Appearance
void manager_appearance(App& app, const Table& t, const ManagerRow& m) {
    ImGui::BeginChild("##mapp");
    const int64_t head = app.db.get_int(t, m.rec, "headassetid", 0);
    if (head > 0) draw_legacy_picture(app, legacy_path::staff_miniface(head), S(96.0f), true);
    else ImGui::Dummy(ImVec2(S(96.0f), S(96.0f)));
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("Choose a real face...")) ImGui::OpenPopup(kManagerFacePopup);
    ImGui::TextDisabled("head asset %lld, head class %lld", static_cast<long long>(head),
                        static_cast<long long>(app.db.get_int(t, m.rec, "headclasscode", 0)));
    ImGui::TextDisabled("Miniface: heads_staff_%lld (edit or render it in the Miniface tab)", static_cast<long long>(head));
    ImGui::EndGroup();
    manager_real_face_picker(app, m.managerid);
    ImGui::SeparatorText("Outfit");
    item_galleries(app, t, m.rec, true);  // the game's outfit pictures (ui_images.cpp)
    ImGui::SeparatorText("Appearance fields");
    field_grid(app, t, m.rec,
               {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation", "skintonecode", "skintypecode",
                "skincomplexion", "haircolorcode", "hairtypecode", "hairstylecode", "facialhairtypecode", "facialhaircolorcode",
                "eyecolorcode", "eyebrowcode", "bodytypecode", "height", "weight", "outfitid"},
               "##mappgrid", 2);
    ImGui::EndChild();
}

}  // namespace turbo
