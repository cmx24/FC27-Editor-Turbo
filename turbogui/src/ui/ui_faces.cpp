// FC 27 LE Turbo GUI - the real-face chooser (see ui_faces.h). One chooser body serves both targets:
//   Players > Appearance > Choose a real face...   player heads -> the player (players table)
//   Managers > Appearance > Choose a real face...  player heads or manager heads -> the manager (manager table)
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "app.h"
#include "core/field_labels.h"
#include "core/game_heads.h"
#include "core/image.h"
#include "core/player_capture.h"
#include "imgui.h"
#include "ui_asset_browser.h"
#include "ui_faces.h"
#include "ui_bodytype_thumbs.h"
#include "ui_faces3d_gate.h"

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
static std::vector<game_heads::ManagerName> g_mnames;  // the save's managers, for the name match (rebuilt with g_mfaces)
static uint64_t g_pfaces_version = ~uint64_t(0), g_mfaces_version = ~uint64_t(0);
// head ids some row uses (design 3.6: players.headassetid of any class, manager.headassetid): "Hidden" is the rest
static game_heads::UsedHeads g_used_by_players, g_used_by_managers;
static uint64_t g_pfaces_looks = 0;  // face3d::generation() the player heads' look pointers come from

static void facet_fields(const Table& t, const Field* out[faces::kFacetCount]) {
    for (int i = 0; i < faces::kFacetCount; ++i) out[i] = t.field(faces::facet_field(static_cast<Facet>(i)));
}

// 2.17 gui/perf-4: the player heads (every player of the save: ~21,900 rows in a played career) are built from one
// snapshot, kFaceRowsPerFrame rows per frame, while the chooser (or anything else) keeps the last complete list; built in one
// go when the chooser opened, they made its first frame take 695 ms in game (W1948). A small save is built in one call.
constexpr size_t kFaceRowsPerFrame = 3000;
struct PlayerFacesBuild {
    bool active = false;
    uint64_t version = 0, looks = 0;
    std::unique_ptr<Snapshot> snap;
    size_t pos = 0;
    double frame_now = -1e300;  // app.now of the frame that last went on (one step per frame)
    std::vector<Face> out;
    game_heads::UsedHeads used;
};
static PlayerFacesBuild g_pbuild;
static uint64_t g_pfaces_stamp = 0;  // +1 at each complete player-head list (the lists built from it key on this)

static bool player_faces_loading(size_t* done, size_t* total) {
    if (!g_pbuild.active || !g_pbuild.snap) return false;
    if (done) *done = g_pbuild.pos;
    if (total) *total = g_pbuild.snap->valid.size();
    return true;
}

const std::vector<Face>& player_faces(App& app) {
    const uint64_t version = app.model.version(), looks = face3d::generation();
    if (g_pfaces_version == version && g_pfaces_looks == looks) return g_pfaces;
    PlayerFacesBuild& b = g_pbuild;
    auto finish = [&](bool built) {
        if (built) {
            std::sort(b.out.begin(), b.out.end(), [](const Face& x, const Face& y) { return x.name < y.name; });
            g_pfaces.swap(b.out);
            g_used_by_players.swap(b.used);
        } else {
            g_pfaces.clear();
            g_used_by_players.clear();
        }
        g_pfaces_version = version;
        g_pfaces_looks = looks;
        ++g_pfaces_stamp;
        b = PlayerFacesBuild{};
    };
    if (b.active && (b.version != version || b.looks != looks)) b = PlayerFacesBuild{};  // the save moved on: start again
    const Table* t = app.db.table("players");
    if (!t || !t->has("headclasscode") || !t->has("headassetid")) {
        finish(false);
        return g_pfaces;
    }
    const Field* hc = t->field("headclasscode");
    const Field* ha = t->field("headassetid");
    const Field* pid = t->field("playerid");
    const Field* hq = t->field("hashighqualityhead");
    if (!hc || !ha || !pid) {
        finish(false);
        return g_pfaces;
    }
    if (!b.active) {
        b.active = true;
        b.version = version;
        b.looks = looks;
        b.snap = std::make_unique<Snapshot>();
        if (!b.snap->load(app.db.memory(), *t)) {
            finish(false);
            return g_pfaces;
        }
    } else if (b.frame_now == app.now) {
        return g_pfaces;  // this frame's rows are done: the next frame goes on
    }
    b.frame_now = app.now;
    const Field* ff[faces::kFacetCount];
    facet_fields(*t, ff);
    const size_t end = std::min(b.snap->valid.size(), b.pos + kFaceRowsPerFrame);
    for (; b.pos < end; ++b.pos) {
        const uint32_t idx = b.snap->valid[b.pos];
        const Snapshot& snap = *b.snap;
        Face r;
        r.id = snap.get_int(idx, *pid);
        r.headassetid = snap.get_int(idx, *ha);
        if (r.headassetid <= 0) continue;
        b.used.insert(r.headassetid);
        const int64_t cls = snap.get_int(idx, *hc);
        r.hq = hq ? static_cast<int>(snap.get_int(idx, *hq)) : -1;
        // a real face: head class 0, of any quality (hashighqualityhead is a badge and an optional filter: G1 2026-10-17;
        // 1,176 of a career's 4,023 real faces have 0)
        r.real = cls == 0;
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
        b.out.push_back(std::move(r));
    }
    if (b.pos < b.snap->valid.size()) return g_pfaces;
    finish(true);
    return g_pfaces;
}

const std::vector<Face>& manager_faces(App& app) {
    if (g_mfaces_version == app.model.version()) return g_mfaces;
    g_mfaces_version = app.model.version();
    g_mfaces.clear();
    g_mnames.clear();
    g_used_by_managers.clear();
    const Table* t = app.db.table("manager");
    if (!t || !t->has("headassetid")) return g_mfaces;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *t)) return g_mfaces;
    std::unordered_map<int64_t, const ManagerRow*> by_id;
    for (const auto& m : app.model.managers()) by_id[m.managerid] = &m;
    const Field* mid = t->field("managerid");
    const Field* ha = t->field("headassetid");
    const Field* hc = t->field("headclasscode");
    const Field* hq = t->field("hashighqualityhead");
    const Field* fn = t->field("firstname");
    const Field* sn = t->field("surname");
    const Field* cn = t->field("commonname");
    const Field* ff[faces::kFacetCount];
    facet_fields(*t, ff);
    for (uint32_t idx : snap.valid) {
        Face r;
        r.manager = true;
        r.id = mid ? snap.get_int(idx, *mid) : 0;
        r.headassetid = snap.get_int(idx, *ha);
        if (r.headassetid <= 0) continue;
        g_used_by_managers.insert(r.headassetid);
        r.real = !hc || snap.get_int(idx, *hc) == 0;
        r.hq = hq ? static_cast<int>(snap.get_int(idx, *hq)) : -1;
        for (int i = 0; i < faces::kFacetCount; ++i) r.raw[i] = ff[i] ? snap.get_int(idx, *ff[i]) : faces::kNoValue;
        auto it = by_id.find(r.id);
        r.name = it != by_id.end() ? it->second->name : "Manager " + std::to_string(r.id);
        r.lname = lower_text(r.name);
        game_heads::ManagerName mn;
        mn.managerid = r.id;
        mn.headassetid = r.headassetid;
        if (fn) mn.first = snap.get_str(idx, *fn);
        if (sn) mn.last = snap.get_str(idx, *sn);
        if (cn) mn.common = snap.get_str(idx, *cn);
        mn.label = r.name;
        g_mnames.push_back(std::move(mn));
        g_mfaces.push_back(std::move(r));
    }
    std::sort(g_mfaces.begin(), g_mfaces.end(), [](const Face& a, const Face& b) { return a.name < b.name; });
    return g_mfaces;
}

// ---------------------------------------------------------------- game heads (asset catalog)
static game_heads::HeadCatalog g_heads_cat;  // copy of the head3d family of the app's catalog (set_faces_catalog)
static uint64_t g_heads_cat_gen = 1;         // bumped by set_faces_catalog
static std::vector<Face> g_gfaces, g_pall;    // hidden game heads; player heads + game heads (the chooser's list)
static uint64_t g_gfaces_version = ~uint64_t(0), g_gfaces_gen = 0, g_gfaces_looks = 0, g_pall_version = ~uint64_t(0), g_pall_gen = 0,
                g_pall_looks = 0;
static uint64_t g_gfaces_pstamp = ~uint64_t(0), g_pall_pstamp = ~uint64_t(0);  // 2.17: the player list they were built from

void set_faces_catalog(std::shared_ptr<const assets::Catalog> catalog) {
    g_heads_cat = catalog ? game_heads::from_catalog(*catalog) : game_heads::HeadCatalog();
    ++g_heads_cat_gen;
}

const std::vector<Face>& game_faces(App& app) {
    player_faces(app);
    manager_faces(app);  // both tables fill the used-head sets
    if (g_gfaces_version == app.model.version() && g_gfaces_gen == g_heads_cat_gen && g_gfaces_looks == face3d::generation() &&
        g_gfaces_pstamp == g_pfaces_stamp)
        return g_gfaces;
    g_gfaces_version = app.model.version();
    g_gfaces_pstamp = g_pfaces_stamp;
    g_gfaces_gen = g_heads_cat_gen;
    g_gfaces_looks = face3d::generation();
    g_gfaces.clear();
    game_heads::UsedHeads used = g_used_by_players;
    used.insert(g_used_by_managers.begin(), g_used_by_managers.end());
    for (const game_heads::HeadInfo* h : game_heads::hidden_heads(g_heads_cat, used)) {
        Face r;
        r.id = h->id;
        r.headassetid = h->id;
        r.real = true;
        r.game_head = true;
        r.name = game_heads::display_name(*h);
        r.name_source = h->source;
        r.lname = lower_text(r.name);
        // no database fields: a database facet filter never matches it. A real face's head id is its playerid, so a head
        // whose player was rendered before he left the save has that render's 3D look (the 3D facets filter it).
        if (const face3d::Look* look = face3d::lookup(h->id)) faces::set_look(r, look);
        g_gfaces.push_back(std::move(r));
    }
    std::sort(g_gfaces.begin(), g_gfaces.end(), [](const Face& a, const Face& b) { return a.name < b.name; });
    return g_gfaces;
}

// Managers' own heads shipped under another id (a former player's or a legend's head), found by name
// (game_heads::manager_name_matches): listed under "Manager heads" with a "name match" badge, given only through the
// usual confirmation (G1 2026-10-17)
static std::vector<Face> g_matchfaces, g_mall;
static uint64_t g_match_version = ~uint64_t(0), g_match_gen = 0, g_mall_version = ~uint64_t(0), g_mall_gen = 0;
static uint64_t g_match_pstamp = ~uint64_t(0), g_mall_mstamp = ~uint64_t(0);  // 2.17: the player list / match list they came from
static uint64_t g_match_stamp = 0;  // +1 at each rebuilt match list

const std::vector<Face>& manager_match_faces(App& app) {
    player_faces(app);
    manager_faces(app);  // both tables fill the used-head sets and the managers' names
    if (g_match_version == app.model.version() && g_match_gen == g_heads_cat_gen && g_match_pstamp == g_pfaces_stamp) return g_matchfaces;
    g_match_version = app.model.version();
    g_match_pstamp = g_pfaces_stamp;
    ++g_match_stamp;
    g_match_gen = g_heads_cat_gen;
    g_matchfaces.clear();
    game_heads::UsedHeads used = g_used_by_players;
    used.insert(g_used_by_managers.begin(), g_used_by_managers.end());
    for (const game_heads::NameMatch& m : game_heads::manager_name_matches(g_heads_cat, g_mnames, used)) {
        Face r;
        r.id = m.head->id;
        r.headassetid = m.head->id;
        r.real = true;
        r.game_head = true;
        r.match_manager = m.manager->managerid;
        r.match_label = m.manager->label;
        r.name = game_heads::display_name(*m.head);
        r.name_source = m.head->source;
        r.lname = lower_text(r.name + " " + r.match_label);  // found by the manager's name too
        g_matchfaces.push_back(std::move(r));
    }
    return g_matchfaces;
}

// The chooser's manager-head list: the save's managers' heads, then the name-matched game heads
static const std::vector<Face>& manager_and_match_faces(App& app) {
    const std::vector<Face>& m = manager_faces(app);
    const std::vector<Face>& g = manager_match_faces(app);
    if (g.empty()) return m;
    if (g_mall_version == app.model.version() && g_mall_gen == g_heads_cat_gen && g_mall_mstamp == g_match_stamp) return g_mall;
    g_mall_version = app.model.version();
    g_mall_mstamp = g_match_stamp;
    g_mall_gen = g_heads_cat_gen;
    g_mall = m;
    g_mall.insert(g_mall.end(), g.begin(), g.end());
    return g_mall;
}

// The chooser's player-head list: the save's heads, then the hidden game heads
static const std::vector<Face>& player_and_game_faces(App& app) {
    const std::vector<Face>& p = player_faces(app);
    const std::vector<Face>& g = game_faces(app);
    if (g.empty()) return p;
    if (g_pall_version == app.model.version() && g_pall_gen == g_heads_cat_gen && g_pall_looks == face3d::generation() &&
        g_pall_pstamp == g_pfaces_stamp)
        return g_pall;
    g_pall_version = app.model.version();
    g_pall_pstamp = g_pfaces_stamp;
    g_pall_gen = g_heads_cat_gen;
    g_pall_looks = face3d::generation();
    g_pall = p;
    g_pall.insert(g_pall.end(), g.begin(), g.end());
    return g_pall;
}

// ---------------------------------------------------------------- the miniface that comes with the head
// Never a mixed result (G1 2026-10-17): every head the chooser gives brings the matching picture into the target's own
// miniface, or nothing is written while that picture is still loading, or the message says plainly that the game has
// none and his old picture stays (game_heads::miniface_plan).
namespace {
struct MinifaceMove {
    std::vector<std::string> sources;  // legacy paths in order; the first one present is the picture
    std::string target;                // his miniface: p<playerid>, or heads_staff_<managerid> (legacy_path::staff_miniface_for)
    int size = kPlayerMinifaceSize;    // 256 for players, 512 for managers
    std::string label;                 // "his miniface p123.dds", for the messages
};
}  // namespace

static std::string file_part(const std::string& path) {
    const size_t s = path.rfind('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

static MinifaceMove to_player(int64_t pid, std::vector<std::string> sources) {
    MinifaceMove m;
    m.sources = std::move(sources);
    m.target = legacy_path::player_miniface(pid);
    m.size = kPlayerMinifaceSize;
    m.label = "his miniface " + file_part(m.target);
    return m;
}

static MinifaceMove to_manager(int64_t managerid, std::vector<std::string> sources) {
    MinifaceMove m;
    m.sources = std::move(sources);
    m.target = legacy_path::staff_miniface_for(managerid, 0);
    m.size = kStaffMinifaceSize;
    m.label = "the manager's picture " + file_part(m.target);
    return m;
}

// The game heads' picture sources: the game's miniface p<head>, else its legend picture (as the catalog lists them)
static std::vector<std::string> game_head_sources(const game_heads::HeadInfo& h) {
    std::vector<std::string> s;
    if (h.miniface) s.push_back(legacy_path::player_miniface(h.id));
    if (h.legend) s.push_back(legacy_path::legend_picture(h.id));
    return s;
}

// Where the picture is now (asks the game when needed): Ready with its path and file, Loading, Stalled (asked and
// nothing came for LegacyImages::kStallSeconds: never a dead end), or None
static game_heads::PictureState find_source(App& app, const MinifaceMove& mv, std::string* path, fs::path* file) {
    if (mv.sources.empty()) return game_heads::PictureState::None;
    LegacyImages::State st = LegacyImages::State::Missing;
    const std::string p = app.legacy.first_present(mv.sources, file, &st);
    if (path) *path = p;
    const bool waiting = st == LegacyImages::State::Waiting;
    return game_heads::settle_picture(st == LegacyImages::State::Custom || st == LegacyImages::State::Game, waiting,
                                      waiting ? app.legacy.waiting_for(p) : 0.0, LegacyImages::kStallSeconds);
}

// The picture written as his miniface: a player's miniface onto a player byte for byte, anything else framed to the
// target's size and encoded as DXT5 (save_custom keeps a backup of what it replaces)
static bool write_miniface(App& app, const MinifaceMove& mv, const std::string& src, const fs::path& file, std::string* err,
                           fs::path* backup = nullptr) {
    std::vector<uint8_t> bytes;
    if (mv.size == kPlayerMinifaceSize && src.find("/heads/p") != std::string::npos) {
        std::ifstream in(file, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else {
        Rgba img;
        if (!load_image_file(file, img, err)) return false;
        bytes = encode_dds_dxt5(frame_image(img, mv.size, Framing()));
    }
    if (bytes.empty()) {
        if (err) *err = "empty picture";
        return false;
    }
    if (!app.legacy.save_custom(mv.target, bytes, err, backup)) return false;
    app.textures.forget(app.legacy.custom_file(mv.target));
    return true;
}

// After the head fields are written: the miniface part of the plan, said in `note`
static void finish_miniface(App& app, const MinifaceMove& mv, const game_heads::MinifacePlan& plan, const std::string& src,
                            const fs::path& file, std::string& note, bool* in_step,
                            std::vector<App::UndoStep::Picture>* written = nullptr) {
    if (in_step) *in_step = false;
    if (!plan.copy) {
        note += "; " + plan.note;
        return;
    }
    if (src == mv.target) {  // his own picture already
        if (in_step) *in_step = true;
        note += "; " + mv.label + " already shows it";
        return;
    }
    std::string err;
    fs::path backup;
    if (write_miniface(app, mv, src, file, &err, &backup)) {
        if (written) written->push_back({mv.target, backup});
        if (in_step) *in_step = true;
        note += "; " + mv.label + " now shows " + file_part(src);
    } else {
        note += "; " + mv.label + " could not be written (" + err + "), so it still shows the old face";
    }
}

bool apply_game_head(App& app, int64_t head_id, int64_t target_id, bool target_is_manager, const RealFaceOptions& o, std::string* msg,
                     bool* in_step) {
    if (in_step) *in_step = false;
    auto fail = [&](const std::string& w) {
        if (msg) *msg = w;
        return false;
    };
    std::string why;
    if (!game_heads::may_give(g_heads_cat, head_id, &why)) return fail("head " + std::to_string(head_id) + " not given: " + why);
    const game_heads::HeadInfo* h = g_heads_cat.find(head_id);
    const Table* t = app.db.table(target_is_manager ? "manager" : "players");
    uint64_t rec = 0;
    std::string target_name;
    if (target_is_manager) {
        for (const auto& m : app.model.managers())
            if (m.managerid == target_id) {
                rec = m.rec;
                target_name = m.name;
            }
    } else if (const PlayerRow* p = app.model.player(target_id)) {
        rec = p->rec;
        target_name = p->name;
    }
    if (!t || !rec || !h) return fail(target_is_manager ? "manager not found" : "player not found");
    // the miniface first: while it is loading nothing is written (never a mixed result)
    const MinifaceMove mv = target_is_manager ? to_manager(target_id, game_head_sources(*h)) : to_player(target_id, game_head_sources(*h));
    std::string src;
    fs::path src_file;
    const game_heads::MinifacePlan plan = game_heads::miniface_plan(find_source(app, mv, &src, &src_file), mv.label);
    if (!plan.write_head) return fail(plan.note);
    // validate everything first, then write
    std::vector<std::pair<const Field*, Value>> writes;
    for (const game_heads::FieldWrite& w : game_heads::head_writes(*h)) {
        const Field* f = t->field(w.field);
        if (!f) continue;
        const Value v = Value::of_int(w.value);
        const std::string verr = Database::validate(*f, v);
        if (!verr.empty()) return fail(std::string(w.field) + ": " + verr);
        writes.emplace_back(f, v);
    }
    // 2.16: a Turbo 3D hair / beard (band codes, core/faces3d.h) belongs to the old head: the new head leaves the band (a player's
    // own codes recorded in turbo_output\faces3d\preview_backup.json, else 0; a real face ignores these codes)
    {
        std::map<std::string, int64_t> cur, own;
        for (const char* f : faces3d::kLookFields)
            if (t->has(f)) cur[f] = app.db.get_int(*t, rec, f, 0);
        {
            faces3d::PreviewBackup backup;  // a manager's own codes sit under faces3d::kManagerBackupKey + managerid
            if (faces3d::load_preview_backup(app.turbo_output().u8string(), backup, nullptr)) {
                const auto r = backup.find(target_is_manager ? faces3d::kManagerBackupKey + target_id : target_id);
                if (r != backup.end()) own = r->second.original;
            }
        }
        for (const auto& [name, value] : faces3d::look_writes(0, 0, cur, own)) {
            const Field* f = t->field(name);
            if (!f) continue;
            const Value v = Value::of_int(value);
            const std::string verr = Database::validate(*f, v);
            if (!verr.empty()) return fail(name + ": " + verr);
            writes.emplace_back(f, v);
        }
    }
    if (writes.empty()) return fail("no head fields in the " + t->name + " table");
    const std::string target = (target_is_manager ? "manager " : "player ") + std::to_string(target_id);
    const uint64_t pushes = app.undo_pushes;
    for (const auto& w : writes) {
        const int64_t old = app.db.get_int(*t, rec, w.first->name, 0);
        if (!app.edit(*t, rec, *w.first, w.second)) return fail("writing " + w.first->name + " failed");
        assets::WriteLogEntry e;
        e.target = target;
        e.field = w.first->name;
        e.old_value = std::to_string(old);
        e.new_value = std::to_string(w.second.i);
        e.catalog_key = g_heads_cat.build_key;
        assets::append_write_log(app.bridge.dir(), e);  // best effort: the edit itself is done
    }
    std::string note = "game head " + game_heads::display_name(*h) + " given to " + target_name + " (logged in asset_writes.log)";
    if (!target_is_manager) faces3d_forget_render(app, false, target_id);  // the chooser's render of him shows the old head
    std::vector<App::UndoStep::Picture> written;
    finish_miniface(app, mv, plan, src, src_file, note, in_step, &written);
    // a player: one Undo puts the whole give back (head fields and miniface); managers have no per-row undo
    if (!target_is_manager) app.group_undo(target_id, static_cast<size_t>(app.undo_pushes - pushes), std::move(written));
    (void)o;
    if (msg) *msg = note;
    return true;
}

size_t real_face_count(App& app) {
    size_t n = 0;
    for (const auto& f : player_faces(app)) n += f.real ? 1 : 0;
    return n;
}

// The picture of a head of the chooser: a player's miniface p<playerid>; a manager's first picture of
// legacy_path::staff_pictures (heads_staff_<managerid>, heads_staff_<headassetid>, the generic manager head's gmh)
static std::string face_picture(App& app, const Face& f) {
    if (!f.manager) return legacy_path::player_miniface(f.id);
    const std::string p = app.legacy.first_present(legacy_path::staff_pictures(f.id, f.headassetid));
    return p.empty() ? legacy_path::staff_miniface(f.id) : p;
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
    // a Turbo 3D hair / beard (band codes, core/faces3d.h) belongs to the old head: the new head takes the owner's own codes
    if (!o.hair && dst.has("hairtypecode") && faces3d::is_turbo_hair_code(app.db.get_int(dst, dst_rec, "hairtypecode", 0)))
        fields.push_back("hairtypecode");
    if (!o.beard && dst.has("facialhairtypecode") && faces3d::is_turbo_beard_code(app.db.get_int(dst, dst_rec, "facialhairtypecode", 0)))
        fields.push_back("facialhairtypecode");
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

bool apply_real_face(App& app, int64_t target_pid, int64_t owner_pid, const RealFaceOptions& o, std::string* msg, bool* in_step) {
    if (in_step) *in_step = false;
    const Table* t = app.db.table("players");
    const PlayerRow* target = app.model.player(target_pid);
    const PlayerRow* owner = app.model.player(owner_pid);
    if (!t || !target || !owner) {
        if (msg) *msg = "player not found";
        return false;
    }
    // his owner's miniface comes with the head; while it is loading nothing is written (never a mixed result)
    const MinifaceMove mv = to_player(target_pid, {legacy_path::player_miniface(owner_pid)});
    std::string src;
    fs::path src_file;
    const game_heads::MinifacePlan plan = game_heads::miniface_plan(find_source(app, mv, &src, &src_file), mv.label);
    if (!plan.write_head) {
        if (msg) *msg = plan.note;
        return false;
    }
    const uint64_t pushes = app.undo_pushes;
    if (!copy_head(app, *t, owner->rec, *t, target->rec, o, msg)) return false;
    faces3d_forget_render(app, false, target_pid);  // the chooser's render of him shows the old head
    std::string note = "head of " + owner->name + " given to " + target->name;
    std::vector<App::UndoStep::Picture> written;
    finish_miniface(app, mv, plan, src, src_file, note, in_step, &written);
    // one Undo puts the whole give back: every head field and the miniface (never a head undone without its picture)
    app.group_undo(target_pid, static_cast<size_t>(app.undo_pushes - pushes), std::move(written));
    if (msg) *msg = note;
    return true;
}

static const ManagerRow* find_manager(App& app, int64_t managerid) {
    for (const auto& m : app.model.managers())
        if (m.managerid == managerid) return &m;
    return nullptr;
}

bool apply_real_face_to_manager(App& app, int64_t managerid, int64_t owner_id, bool owner_is_manager, const RealFaceOptions& o,
                                std::string* msg, bool* in_step) {
    if (in_step) *in_step = false;
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
    // the owner's picture comes with the head, written as the target's own heads_staff_<managerid> (512 x 512): a player's
    // miniface, or the owner manager's picture in the order Turbo shows it; while it is loading nothing is written
    const int64_t owner_head = owner_is_manager ? app.db.get_int(*st, src_rec, "headassetid", 0) : 0;
    const MinifaceMove mv = to_manager(managerid, owner_is_manager ? legacy_path::staff_pictures(owner_id, owner_head)
                                                                    : std::vector<std::string>{legacy_path::player_miniface(owner_id)});
    std::string src;
    fs::path src_file;
    const game_heads::MinifacePlan plan = game_heads::miniface_plan(find_source(app, mv, &src, &src_file), mv.label);
    if (!plan.write_head) {
        if (msg) *msg = plan.note;
        return false;
    }
    if (!copy_head(app, *st, src_rec, *mt, target->rec, o, msg)) return false;
    std::string note = "head of " + owner_name + " given to " + target->name;
    finish_miniface(app, mv, plan, src, src_file, note, in_step);
    if (msg) *msg = note;
    return true;
}

// ---------------------------------------------------------------- the chooser
struct Chooser {
    char search[64] = "";
    faces::Filter filter;
    int sort = faces::kSortName;
    bool real_only = true;
    bool only3d = faces::kOnly3dDefaultOn;  // "Only heads with a 3D look" (off by default: G1 2026-10-17)
    bool hq_only = faces::kHqOnlyDefaultOn;  // "High-quality heads only" (hashighqualityhead 1; off by default)
    bool manager_heads = false;  // manager target: managers' heads instead of players'
    RealFaceOptions opts;
    AssetScope scope = AssetScope::All;  // All / In this save / Hidden (game heads); this session only
    int64_t pending_game_head = 0;       // a game head clicked: the confirmation row asks before writing
    float confirm_h = 0.0f;              // height of that confirmation block last frame (the grid leaves room for it)
    // 2.17 gui/perf-4: what the grid shows, kept until the list or a search / option / filter / sort changes. Rebuilt every
    // frame before, it cost the chooser 20-27 ms per frame in game (W1948: ~28,000 heads walked, filtered and sorted by name)
    struct View {
        std::string list_key, key;  // the list (identity, size, versions) / the list + every choice that narrows or orders it
        size_t looked = 0;          // heads with a 3D look in the list
        std::vector<size_t> pool;   // search, real / HQ / 3D only, scope
        std::vector<const Face*> rows;  // the pool through the facet filters, sorted
    } view;
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
                if (pic.empty()) pic = face_picture(app, *sample);
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
    if (faces::facet_3d(fc)) {
        // unsure values and heads without a 3D look are no choice (at most 5 per menu): say how many there are, and how
        // far the classification can be trusted (the hand check compiled into Turbo)
        const faces::Unknowns u = faces::facet_unknowns(list, pool, ch.filter, fc);
        if (u.unsure > 0) ImGui::TextDisabled("%d heads unsure (no value matches them)", u.unsure);
        if (u.unclassified > 0) ImGui::TextDisabled("%d heads not classified yet (no 3D look)", u.unclassified);
        const std::string note = faces::check_note(fc);
        if (!note.empty()) {
            ImGui::TextDisabled("%s", note.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The 3D looks are judged by eye from the game's renders. Heads checked one by one against\n"
                                  "their renders tell how often the table puts a head in the right group of this menu.\n"
                                  "A value marked \"?\" in a head's tooltip was a guess: no filter value matches it.");
        }
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- 3D heads (the game renders each head from its 3D model)
// The chooser asks the capture service (app.capture, docs/re/player_capture.md) for heads, up to Status::max_batch
// (the controller's 6) per request, one request in flight, never waiting for it: tick_faces3d runs every frame from
// App::draw (so "Render all filtered heads" goes on with the chooser closed) and hands out a couple of results per frame.
// A render is saved as a 256 x 256 DXT5 DDS in turbo_output/cache/faces3d (p_<playerid>.dds, m_<headid>.dds) and drawn
// instead of the miniface from then on (kept across sessions). Heads on screen go first; "Render all filtered heads"
// queues the whole filtered list behind them.
namespace {
struct Head3d {
    bool manager = false;
    int32_t id = 0;  // playerid, or the head id for a manager head (what the game renders)
    int32_t teamid = -1;  // the club, passed as the capture's second id like the Miniface editor does (-1 = none)
    std::string label;
    bool in_batch = false;  // counted by "Render all filtered heads" when it finishes
};
struct Render3d {
    std::deque<Head3d> queue;  // "Render all filtered heads", in order
    std::unordered_set<int64_t> queued;
    std::deque<Head3d> front;  // heads on screen: sent before the queue (newest first, oldest dropped)
    std::unordered_set<int64_t> fronted;
    std::unordered_map<int64_t, bool> cached;  // file exists (checked once per head)
    std::unordered_map<int64_t, int> fails;    // failed renders per head
    std::vector<Head3d> inflight;              // the heads of the request in flight (empty = none)
    double since = 0.0;                        // request sent / last answer (the timeout counts from it)
    bool batch = false;                        // "Render all filtered heads" running
    size_t batch_total = 0, batch_done = 0, failed = 0, rendered = 0;
    std::string note;
};
Render3d g_r3d;
constexpr size_t kVisibleQueueMax = 3 * capture::kMaxPlayersPerRequest;  // heads on screen waiting
constexpr double kRenderTimeout = 20.0;                                     // seconds without an answer before a request is given up
constexpr int kMaxHeadFails = 2;                                            // a head failing this often is not asked again (this session)
constexpr int kResultsPerFrame = 2;                                         // decoded + saved per frame (never a long frame)
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

void faces3d_forget_render(App& app, bool manager, int64_t id) {
    const fs::path f = face3d_file(app, manager, id);
    std::error_code ec;
    fs::remove(f, ec);
    g_r3d.cached.erase(head3d_key(manager, id));
    g_r3d.fails.erase(head3d_key(manager, id));
    app.textures.forget(f);
}

static bool head3d_given_up(int64_t k) {
    auto it = g_r3d.fails.find(k);
    return it != g_r3d.fails.end() && it->second >= kMaxHeadFails;
}

static bool head3d_inflight(int64_t k) {
    for (const Head3d& h : g_r3d.inflight)
        if (head3d_key(h.manager, h.id) == k) return true;
    return false;
}

static bool capture_usable(App& app) {
    if (!app.capture) return false;
    return app.capture->status().installed;
}

static void queue_head3d(App& app, const Face& f, bool front) {
    if (f.game_head) return;  // the capture renders a player by playerid: a game head has no player to render
    const int64_t id = head3d_id(f);
    if (id <= 0 || face3d_cached(app, f.manager, id)) return;
    const int64_t k = head3d_key(f.manager, id);
    if (head3d_given_up(k) || head3d_inflight(k)) return;
    if (front ? g_r3d.fronted.count(k) != 0 : g_r3d.queued.count(k) != 0) return;
    Head3d h;
    h.manager = f.manager;
    h.id = static_cast<int32_t>(id);
    if (f.manager) {
        if (const ManagerRow* m = find_manager(app, f.id); m && m->teamid > 0) h.teamid = static_cast<int32_t>(m->teamid);
    } else if (const PlayerRow* pr = app.model.player(f.id); pr && pr->club > 0) {
        h.teamid = static_cast<int32_t>(pr->club);
    }
    h.label = f.name;
    if (front) {
        // on screen: first; the oldest (scrolled away) make room
        while (g_r3d.front.size() >= kVisibleQueueMax) {
            g_r3d.fronted.erase(head3d_key(g_r3d.front.back().manager, g_r3d.front.back().id));
            g_r3d.front.pop_back();
        }
        g_r3d.front.push_front(h);
        g_r3d.fronted.insert(k);
    } else {
        g_r3d.queue.push_back(h);
        g_r3d.queued.insert(k);
    }
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

// A head's render failed: one more try at the end of "Render all filtered heads", else counted as failed
static void head3d_failed(const Head3d& h, const std::string& why) {
    const int64_t k = head3d_key(h.manager, h.id);
    const int n = ++g_r3d.fails[k];
    g_r3d.note = h.label + ": " + why;
    if (h.in_batch && g_r3d.batch && n < kMaxHeadFails && !g_r3d.queued.count(k)) {
        g_r3d.queue.push_back(h);
        g_r3d.queued.insert(k);
        return;
    }
    ++g_r3d.failed;
    if (h.in_batch) ++g_r3d.batch_done;
}

// The heads of the next request: on-screen heads first, then the "Render all" queue; distinct ids (the game hands each
// picture back with its id only), skipping heads already rendered or given up
static std::vector<Head3d> next_heads3d(App& app, size_t max) {
    std::vector<Head3d> pick;
    auto picked = [&](int64_t k) -> Head3d* {
        for (Head3d& h : pick)
            if (head3d_key(h.manager, h.id) == k) return &h;
        return nullptr;
    };
    auto id_taken = [&](int32_t id) {
        for (const Head3d& h : pick)
            if (h.id == id) return true;
        return false;
    };
    while (!g_r3d.front.empty() && pick.size() < max) {
        Head3d h = g_r3d.front.front();
        const int64_t k = head3d_key(h.manager, h.id);
        if (!picked(k) && id_taken(h.id)) break;  // a player and a manager head with the same id: next request
        g_r3d.front.pop_front();
        g_r3d.fronted.erase(k);
        if (picked(k) || face3d_cached(app, h.manager, h.id) || head3d_given_up(k)) continue;
        h.in_batch = false;
        pick.push_back(h);
    }
    while (!g_r3d.queue.empty() && pick.size() < max) {
        Head3d h = g_r3d.queue.front();
        const int64_t k = head3d_key(h.manager, h.id);
        if (!picked(k) && id_taken(h.id)) break;
        g_r3d.queue.pop_front();
        g_r3d.queued.erase(k);
        if (Head3d* p = picked(k)) {  // on screen too: counted by the batch when it finishes
            p->in_batch = g_r3d.batch;
            continue;
        }
        if (face3d_cached(app, h.manager, h.id) || head3d_given_up(k)) {
            if (g_r3d.batch) ++g_r3d.batch_done;
            continue;
        }
        h.in_batch = g_r3d.batch;
        pick.push_back(h);
    }
    return pick;
}

// Every frame (App::draw): collect finished renders (a couple per frame), or send the next request (never waits)
void tick_faces3d(App& app) {
    capture::CaptureService* svc = app.capture.get();
    if (!svc) return;
    // the 3D hair preview (ui_faces3d_gate.cpp) waits for its own picture: send nothing meanwhile, or its answer could be taken here
    if (g_r3d.inflight.empty() && faces3d_preview_pending()) return;
    if (g_r3d.inflight.empty() && *bodytype_thumbs_busy_text()) return;  // 2.17 looks T: the body-type picture maker owns the service
    const double now = ImGui::GetTime();
    if (!g_r3d.inflight.empty()) {
        for (int n = 0; n < kResultsPerFrame && !g_r3d.inflight.empty(); ++n) {
            capture::Result r;
            if (!svc->poll(r)) break;
            g_r3d.since = now;
            auto it = std::find_if(g_r3d.inflight.begin(), g_r3d.inflight.end(), [&](const Head3d& h) { return h.id == r.id; });
            if (it == g_r3d.inflight.end()) {
                g_r3d.note = "answer for id " + std::to_string(r.id) + ", which was not asked";
                continue;
            }
            const Head3d h = *it;
            g_r3d.inflight.erase(it);
            std::string err;
            if (r.ok && save_head3d(app, h, r.image, &err)) {
                ++g_r3d.rendered;
                g_r3d.fails.erase(head3d_key(h.manager, h.id));
                if (h.in_batch) ++g_r3d.batch_done;
            } else {
                head3d_failed(h, r.ok ? err : r.error);
            }
        }
        if (!g_r3d.inflight.empty() && now - g_r3d.since > kRenderTimeout) {
            svc->cancel();
            const std::vector<Head3d> lost = std::move(g_r3d.inflight);
            g_r3d.inflight.clear();
            for (const Head3d& h : lost) head3d_failed(h, "no answer from the game");
        }
        return;
    }
    if (g_r3d.queue.empty() && g_r3d.front.empty()) {
        g_r3d.batch = false;
        return;
    }
    const capture::Status st = svc->status();
    if (!st.installed || !st.available || st.busy) return;
    const size_t max = static_cast<size_t>(std::clamp(st.max_batch, 1, static_cast<int>(capture::kMaxPlayersPerRequest)));
    std::vector<Head3d> pick = next_heads3d(app, max);
    if (pick.empty()) {
        if (g_r3d.queue.empty() && g_r3d.front.empty()) g_r3d.batch = false;
        return;
    }
    capture::Request req;
    req.camera = 0;            // portrait: the game's card / manager head framing
    req.use_template = false;  // the player / staff descriptor (verified live); the learned one may be the avatar portrait
    if (pick.size() == 1) {
        req.id = pick[0].id;
        req.second_id = pick[0].teamid;
        req.manager = pick[0].manager;
        req.label = pick[0].label;
    } else {
        for (const Head3d& h : pick) {
            capture::BatchEntry e;
            e.id = h.id;
            e.second_id = h.teamid;
            e.manager = h.manager;
            e.label = h.label;
            req.batch.push_back(e);
        }
        req.label = capture::request_label(req);
    }
    std::string err;
    if (svc->request(req, &err)) {
        g_r3d.inflight = std::move(pick);
        g_r3d.since = now;
    } else {
        for (const Head3d& h : pick) head3d_failed(h, err);
    }
}

void faces3d_stop() {
    g_r3d.queue.clear();
    g_r3d.queued.clear();
    g_r3d.front.clear();
    g_r3d.fronted.clear();
    g_r3d.batch = false;
}

bool faces3d_running() { return g_r3d.batch; }

std::string faces3d_line() {
    if (!g_r3d.batch) return std::string();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "3D heads: %zu / %zu", std::min(g_r3d.batch_done, g_r3d.batch_total), g_r3d.batch_total);
    std::string s = buf;
    if (g_r3d.failed > 0) s += ", " + std::to_string(g_r3d.failed) + " failed";
    return s;
}

std::string faces3d_status() {
    char buf[200];
    std::snprintf(buf, sizeof(buf), "3D heads (face chooser): %zu rendered, %zu failed this session | %zu in flight, %zu waiting%s", g_r3d.rendered,
                  g_r3d.failed, g_r3d.inflight.size(), g_r3d.queue.size() + g_r3d.front.size(), g_r3d.batch ? " | Render all running" : "");
    return buf;
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

// A grid cell for a head with no picture (a game head the game ships no miniface for); true when clicked
static bool no_picture_cell(float cell, const std::string& caption, const char* id) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(cell, cell + line));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg));
    const char* txt = "no picture";
    const ImVec2 ts = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(p.x + (cell - ts.x) * 0.5f, p.y + (cell - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), txt);
    const ImVec4 clip(p.x, p.y + cell, p.x + cell, p.y + cell + line);
    dl->AddText(nullptr, 0.0f, ImVec2(p.x + 2.0f, p.y + cell + 1.0f), ImGui::GetColorU32(ImGuiCol_Text), caption.c_str(), nullptr, 0.0f, &clip);
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
        if (ImGui::Button("Stop##r3d")) faces3d_stop();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stops sending heads (the request in flight still finishes)");
    } else {
        size_t missing = 0;
        for (const Face* f : rows)
            if (!f->game_head && head3d_id(*f) > 0 && !face3d_cached(app, f->manager, head3d_id(*f))) ++missing;
        if (missing > 0) {
            char bl[96];
            std::snprintf(bl, sizeof(bl), "Render all filtered heads (%zu)##r3dall", missing);
            if (ImGui::Button(bl)) {
                g_r3d.queue.clear();
                g_r3d.queued.clear();
                g_r3d.fails.clear();  // asked again: heads given up earlier get their tries back
                g_r3d.failed = 0;
                g_r3d.note.clear();
                for (const Face* f : rows) queue_head3d(app, *f, false);
                g_r3d.batch = true;
                g_r3d.batch_total = g_r3d.queue.size();
                g_r3d.batch_done = 0;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Asks the game to render each filtered head from its 3D model, up to %d per request, in the background\n"
                                  "(it goes on with this window closed; progress and Stop on Turbo's status line).\n"
                                  "Renders are kept in %s",
                                  int(capture::kMaxPlayersPerRequest), faces3d_dir(app).string().c_str());
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
    const std::vector<Face>& list = mheads ? manager_and_match_faces(app) : player_and_game_faces(app);
    const size_t game_count = mheads ? manager_match_faces(app).size() : game_faces(app).size();
    const bool game_heads_on = game_count > 0;
    const Table* src = app.db.table(mheads ? "manager" : "players");
    const float row_end = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    // 3D looks (core/face3d_looks.h): player heads classified from the game's 3D renders; manager heads have none
    // 2.17 gui/perf-4: the list is the same object until it is rebuilt (model, catalog or 3D looks changed): keyed on those
    char list_key[160];
    std::snprintf(list_key, sizeof(list_key), "%p|%zu|%llu|%llu|%llu|%llu|%llu|%d", static_cast<const void*>(&list), list.size(),
                  static_cast<unsigned long long>(app.model.version()), static_cast<unsigned long long>(g_heads_cat_gen),
                  static_cast<unsigned long long>(face3d::generation()), static_cast<unsigned long long>(g_pfaces_stamp),
                  static_cast<unsigned long long>(g_match_stamp), mheads ? 1 : 0);
    if (ch.view.list_key != list_key) {
        ch.view.list_key = list_key;
        ch.view.key.clear();  // the rows point into the old list: rebuilt below
        ch.view.looked = mheads ? 0 : faces::count_looks(list);
    }
    const size_t looked = ch.view.looked;
    const bool looks3d = looked > 0;
    const bool only3d = looks3d && ch.only3d;
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
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Heads of the managers in the database, and game heads whose name matches one of them (\"name match\": check before you apply)");
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(S(200.0f));
    ImGui::InputTextWithHint("##facesearch", "name or ID", ch.search, sizeof(ch.search));
    ImGui::SameLine();
    ImGui::Checkbox("Real faces only", &ch.real_only);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", mheads ? "Only managers with head class 0 (a real face)"
                                       : "Only players with headclasscode 0 (a real face head model), high quality or not");
    ImGui::SameLine();
    ImGui::Checkbox("High-quality heads only", &ch.hq_only);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Only heads with hashighqualityhead 1 (the HQ badge). A real face with 0 is a scanned head too.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(200.0f));
    if (ImGui::BeginCombo("Sort##facesort", faces::sort_title(static_cast<faces::Sort>(ch.sort)))) {
        for (int s = 0; s < faces::kSortCount; ++s)
            if (ImGui::Selectable(faces::sort_title(static_cast<faces::Sort>(s)), ch.sort == s)) ch.sort = s;
        ImGui::EndCombo();
    }
    if (game_heads_on) {
        ImGui::SameLine();
        asset_scope_combo("##facescope", ch.scope, S(130.0f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("All heads, the heads of this save, or Hidden: the %zu 3D heads in the game files that no player or manager\n"
                              "of this save uses%s (asset catalog %s)",
                              game_count, mheads ? " and whose name matches a manager" : "", g_heads_cat.build_key.c_str());
    }
    if (looks3d) {
        bool on = only3d;
        bool not_first = false;
        flow(not_first, ImGui::CalcTextSize("Only heads with a 3D look").x + ImGui::GetFrameHeight() + S(8.0f), row_end);
        if (ImGui::Checkbox("Only heads with a 3D look", &on)) ch.only3d = on;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%zu of %zu heads have a 3D look: skin, hair, facial hair and headwear judged from the game's 3D render.\n"
                              "In those filters the other heads are \"not classified yet\", and a value the classification\n"
                              "was unsure of (\"?\" in the head's tooltip) matches no filter value.",
                              looked, list.size());
    }

    // heads matching the search and "real faces only"; the filters below narrow them
    const std::string q = lower_text(ch.search);
    const bool numeric = !q.empty() && std::all_of(q.begin(), q.end(), ::isdigit);
    // 2.17 gui/perf-4: everything that narrows or orders the heads, with the list's own key; the facet filters are part of it
    std::string view_key = ch.view.list_key + "|" + q + "|" + (ch.real_only ? "r" : "-") + (ch.hq_only ? "h" : "-") + (only3d ? "3" : "-") +
                           "|" + std::to_string(int(ch.scope)) + "|" + std::to_string(ch.sort) + "|" + (game_heads_on ? "g" : "-");
    for (int i = 0; i < faces::kFacetCount; ++i) view_key += "|" + std::to_string(ch.filter.sel[i]);
    const bool view_fresh = ch.view.key == view_key;
    std::vector<size_t>& pool = ch.view.pool;
    faces::PoolOptions po;
    po.real_only = ch.real_only;
    po.hq_only = ch.hq_only;
    po.only3d = only3d;  // game heads have no 3D look: "Only heads with a 3D look" keeps them
    if (!view_fresh) pool.clear();
    for (size_t i = 0; !view_fresh && i < list.size(); ++i) {
        const Face& f = list[i];
        if (game_heads_on && !assets::scope_shows(ch.scope, f.game_head)) continue;
        if (!faces::in_pool(f, po)) continue;
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
    ImGui::SameLine();
    ImGui::TextDisabled("| the miniface always comes with the head");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", manager_target ? "The head's picture is written as his own heads_staff_<managerid> (512 x 512).\n"
                                                 "While it is loading nothing is written; when the game has none, the message says so."
                                               : "The head's miniface is written as his own p<playerid>.\n"
                                                 "While it is loading nothing is written; when the game has none, the message says so.");

    // the filters may have changed in the facet popups above this frame: the key is taken again before the rows are kept
    std::string rows_key = ch.view.list_key + "|" + q + "|" + (ch.real_only ? "r" : "-") + (ch.hq_only ? "h" : "-") + (only3d ? "3" : "-") +
                           "|" + std::to_string(int(ch.scope)) + "|" + std::to_string(ch.sort) + "|" + (game_heads_on ? "g" : "-");
    for (int i = 0; i < faces::kFacetCount; ++i) rows_key += "|" + std::to_string(ch.filter.sel[i]);
    std::vector<const Face*>& rows = ch.view.rows;
    if (!view_fresh || rows_key != view_key) {
        rows.clear();
        for (size_t i : pool)
            if (faces::matches(list[i], ch.filter)) rows.push_back(&list[i]);
        faces::sort_faces(rows, static_cast<faces::Sort>(ch.sort));
    }
    // the pool only depends on what view_key holds before the filters: kept under the key of this frame's end state
    ch.view.key = rows_key == view_key ? view_key : std::string();
    if (!q.empty() && !numeric && q.size() < 2) ImGui::TextDisabled("Type at least 2 letters");
    {
        size_t done = 0, total = 0;  // 2.17 gui/perf-4: the player heads are being read (a few frames), the list follows
        if (!mheads && player_faces_loading(&done, &total)) ImGui::TextDisabled("Reading the save's heads: %zu of %zu", done, total);
    }
    ImGui::TextDisabled("%zu of %zu heads (%s)", rows.size(), pool.size(),
                        mheads ? "managers' heads with their minifaces"
                               : (ch.real_only ? "real-face players with their minifaces" : "every player with a head model"));
    if (game_heads_on) {
        ImGui::SameLine();
        ImGui::TextDisabled("| %zu %s%s", game_count, mheads ? "game heads match a manager's name" : "hidden game heads",
                            g_heads_cat.current() ? "" : " (catalog for another game build: they cannot be given)");
    }

    const bool use3d = capture_usable(app);
    head3d_bar(app, rows);  // the renders themselves: tick_faces3d, every frame from App::draw
    const float cell = S(96.0f);
    const Face* picked = nullptr;
    // below the grid: the "pictures loading" line and Close, plus the game-head confirmation (Apply / Cancel and its
    // warnings) while one is pending, so Apply is never below the fold of the fixed-size modal. Its height is measured
    // each frame (game_head_confirm); the first frame estimates it.
    float below = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    if (ch.pending_game_head > 0)
        below += ch.confirm_h > 0.0f ? ch.confirm_h : ImGui::GetTextLineHeightWithSpacing() * 7.0f + ImGui::GetFrameHeightWithSpacing();
    // a resizable child keeps its own height (ImGui uses size.y only at first): while a confirmation is pending the grid is not
    // resizable, so it shrinks and Apply / Cancel stay visible (2.1 in-game check)
    ImGui::BeginChild("##facegrid", ImVec2(0, -below), ch.pending_game_head > 0 ? ImGuiChildFlags_Borders : (ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY));
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
                ImGui::PushID(static_cast<int>(f->id ^ (f->match_manager << 20)));
                char bid[64];
                if (f->match_manager)  // one head can match two managers: the manager keeps the ids apart
                    std::snprintf(bid, sizeof(bid), "gface%lld_m%lld", static_cast<long long>(f->id), static_cast<long long>(f->match_manager));
                else
                    std::snprintf(bid, sizeof(bid), "%s%lld", f->game_head ? "gface" : f->manager ? "mface" : "face", static_cast<long long>(f->id));
                const int64_t rid = head3d_id(*f);
                const bool has3d = !f->game_head && use3d && rid > 0 && face3d_cached(app, f->manager, rid);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                const game_heads::HeadInfo* gh = f->game_head ? g_heads_cat.find(f->id) : nullptr;
                const bool game3d = f->game_head && use3d && face3d_cached(app, false, f->id);  // rendered while his player was in a save
                if (f->game_head) {
                    // a game head: the 3D render of its player when one is cached, else its game miniface when the game
                    // ships one (it cannot be rendered before Apply)
                    // its legend picture when the game ships no miniface for it (G1 2026-10-17)
                    const bool clicked = game3d                  ? face3d_cell(app, face3d_file(app, false, f->id), cell, f->name, bid)
                                         : gh && gh->miniface ? picture_cell(app, legacy_path::player_miniface(f->id), false, cell, f->name, bid, false)
                                         : gh && gh->legend   ? picture_cell(app, legacy_path::legend_picture(f->id), false, cell, f->name, bid, false)
                                                              : no_picture_cell(cell, f->name, bid);
                    if (clicked) picked = f;
                    ImGui::GetWindowDrawList()->AddText(ImVec2(cp.x + 3.0f, cp.y + 2.0f), ImGui::GetColorU32(ImGuiCol_PlotHistogram),
                                                        asset_badge_label(AssetBadge::Hidden));
                    if (f->match_manager) {
                        const char* nm = "name match";
                        ImGui::GetWindowDrawList()->AddText(ImVec2(cp.x + cell - ImGui::CalcTextSize(nm).x - 3.0f, cp.y + 2.0f),
                                                            ImGui::GetColorU32(ImGuiCol_PlotLinesHovered), nm);
                    }
                } else if (has3d) {
                    if (face3d_cell(app, face3d_file(app, f->manager, rid), cell, f->name, bid)) picked = f;
                    // the cell shows the 3D render, not the miniface: with "His miniface too" the miniface is asked from
                    // the game all the same, so a click finds it at hand to copy
                    if (!f->manager) app.legacy.locate(face_picture(app, *f), nullptr, true);
                } else {
                    if (picture_cell(app, face_picture(app, *f), false, cell, f->name, bid, false)) picked = f;
                    if (use3d && rid > 0) {
                        queue_head3d(app, *f, true);  // on screen: before the rest of the queue
                        const bool given_up = head3d_given_up(head3d_key(f->manager, rid));
                        ImGui::GetWindowDrawList()->AddText(ImVec2(cp.x + 3.0f, cp.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                                            given_up ? "3D failed" : "3D pending");
                    }
                }
                if (!f->game_head && f->hq == 1) {
                    const char* hq = "HQ";
                    ImGui::GetWindowDrawList()->AddText(ImVec2(cp.x + cell - ImGui::CalcTextSize(hq).x - 3.0f, cp.y + 2.0f),
                                                        ImGui::GetColorU32(ImGuiCol_PlotHistogram), hq);
                }
                if (f->game_head && ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", f->name.c_str());
                    ImGui::SameLine();
                    asset_badge(AssetBadge::Hidden, "In the game files; no player or manager of this save uses it");
                    if (f->match_manager)
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Name match: its name matches manager %s of this save. A guess from the names: check that it is him.",
                                           f->match_label.c_str());
                    ImGui::TextDisabled("game head %lld%s%s", static_cast<long long>(f->id), gh && !gh->toc.empty() ? ", bundle " : "",
                                        gh ? gh->toc.c_str() : "");
                    if (f->look) {
                        if (game3d) draw_file_picture(app, face3d_file(app, false, f->id), S(256.0f));
                        const std::string traits = faces::head_traits(*f, looks3d);
                        ImGui::TextUnformatted(traits.c_str() + (traits.empty() || traits[0] != '\n' ? 0 : 1));
                    }
                    const std::string src = assets::name_source_label(f->name_source);
                    if (!src.empty()) ImGui::TextDisabled("%s", src.c_str());
                    if (gh && !gh->miniface && gh->legend && !game3d) ImGui::TextDisabled("Shown with its legend picture (the game ships no miniface for it).");
                    else if (gh && !gh->miniface && !game3d) ImGui::TextDisabled("The game ships no miniface for it, and it cannot be rendered before it is given.");
                    ImGui::TextDisabled("Click to give it (you confirm first).");
                    ImGui::EndTooltip();
                } else if (ImGui::IsItemHovered()) {
                    const std::string traits = faces::head_traits(*f, looks3d);  // "3D look: ..." first when the list has looks
                    ImGui::BeginTooltip();
                    if (has3d) draw_file_picture(app, face3d_file(app, f->manager, rid), S(256.0f));  // big preview of the 3D render
                    ImGui::Text("%s, %s%s", f->name.c_str(), f->manager ? "manager's face" : f->real ? "real face" : "generic head", traits.c_str());
                    ImGui::TextDisabled("%s ID %lld, face #%lld%s", f->manager ? "manager" : "player", static_cast<long long>(f->id),
                                        static_cast<long long>(f->headassetid),
                                        f->hq == 1 ? ", high quality" : "");
                    if (!has3d && use3d && rid > 0)
                        ImGui::TextDisabled(head3d_given_up(head3d_key(f->manager, rid))
                                                ? "3D failed: the game could not render this head, so the flat picture stays."
                                                : "3D pending: the game has not rendered this head yet, so the flat picture shows meanwhile.");
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

// A game head was clicked: what will be written, why it may not be, Apply / Cancel. True when it was given.
static bool game_head_confirm(App& app, Chooser& ch, int64_t target_id, bool target_is_manager) {
    if (ch.pending_game_head <= 0) return false;
    const game_heads::HeadInfo* h = g_heads_cat.find(ch.pending_game_head);
    if (!h) {
        ch.pending_game_head = 0;
        return false;
    }
    std::string why;
    const bool ok = game_heads::may_give(g_heads_cat, h->id, &why);
    const float y0 = ImGui::GetCursorPosY();
    ImGui::Separator();
    ImGui::Text("Give the game head %s?", game_heads::display_name(*h).c_str());
    ImGui::SameLine();
    asset_badge(AssetBadge::Hidden);
    // wrapped: the modal is 880 wide and these lines are longer
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("This replaces his face with this one. Build and skin tone stay.");
    ImGui::PopStyleColor();
    // the miniface that comes with it (asks the game for the picture now, so Apply finds it at hand)
    const MinifaceMove mv = target_is_manager ? to_manager(target_id, game_head_sources(*h)) : to_player(target_id, game_head_sources(*h));
    std::string src;
    const game_heads::PictureState ps = find_source(app, mv, &src, nullptr);
    if (ps == game_heads::PictureState::Ready)
        ImGui::TextWrapped("Miniface: %s comes with it, written as %s.", file_part(src).c_str(), mv.label.c_str());
    else if (ps == game_heads::PictureState::Loading)
        ImGui::TextDisabled("Miniface: loading the head's picture from the game... (Apply waits for it)");
    else
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Miniface: %s.", game_heads::miniface_plan(ps, mv.label).note.c_str());
    for (const Face& mf : manager_match_faces(app))
        if (mf.id == h->id && mf.match_manager) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Name match: its name matches manager %s; check that it is him.", mf.match_label.c_str());
            break;
        }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
    ImGui::TextWrapped("%s Not yet checked in a match: try it on a copy of your save first.", game_heads::gender_note());
    ImGui::PopStyleColor();
    bool given = false;
    ImGui::BeginDisabled(!ok || ps == game_heads::PictureState::Loading);
    if (ImGui::Button("Apply##gamehead")) {
        std::string msg;
        bool in_step = false;
        given = apply_game_head(app, h->id, target_id, target_is_manager, ch.opts, &msg, &in_step);
        app.notify(msg, !given || !in_step);
        ch.pending_game_head = 0;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel##gamehead")) ch.pending_game_head = 0;
    if (!ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", why.c_str());
        ImGui::PopStyleColor();
    }
    ch.confirm_h = ImGui::GetCursorPosY() - y0;  // the grid of the next frame leaves this much room
    return given;
}

void real_face_picker(App& app, int64_t target_pid) {
    ImGui::SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Choose a real face", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (const Face* f = chooser_body(app, g_player_ch, false)) {
        if (f->game_head) {
            g_player_ch.pending_game_head = f->id;  // asks first (game_head_confirm)
        } else {
            const int64_t owner = f->id;
            std::string msg;
            bool in_step = false;
            const bool ok = apply_real_face(app, target_pid, owner, g_player_ch.opts, &msg, &in_step);
            app.notify(msg, !ok || !in_step);
            if (ok) ImGui::CloseCurrentPopup();
        }
    }
    if (game_head_confirm(app, g_player_ch, target_pid, false)) ImGui::CloseCurrentPopup();
    if (ImGui::Button("Close##faces")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void manager_real_face_picker(App& app, int64_t managerid) {
    ImGui::SetNextWindowSize(ImVec2(S(880.0f), S(640.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kManagerFacePopup, nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    if (const Face* f = chooser_body(app, g_manager_ch, true)) {
        if (f->game_head) {
            g_manager_ch.pending_game_head = f->id;  // asks first (game_head_confirm)
        } else {
            const int64_t owner = f->id;
            const bool owner_mgr = f->manager;
            std::string msg;
            bool in_step = false;
            const bool ok = apply_real_face_to_manager(app, managerid, owner, owner_mgr, g_manager_ch.opts, &msg, &in_step);
            app.notify(msg, !ok || !in_step);
            if (ok) ImGui::CloseCurrentPopup();
        }
    }
    if (game_head_confirm(app, g_manager_ch, managerid, true)) ImGui::CloseCurrentPopup();
    if (ImGui::Button("Close##mfaces")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Managers > Appearance
void manager_appearance(App& app, const Table& t, const ManagerRow& m) {
    ImGui::BeginChild("##mapp");
    const int64_t head = app.db.get_int(t, m.rec, "headassetid", 0);
    // heads_staff_<managerid>, else heads_staff_<headassetid>, else his generic manager head's picture (G1 2026-10-17)
    const std::string pic = app.legacy.first_present(legacy_path::staff_pictures(m.managerid, head));
    if (!pic.empty()) draw_legacy_picture(app, pic, S(96.0f), true);
    else ImGui::Dummy(ImVec2(S(96.0f), S(96.0f)));
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (ImGui::Button("Choose a real face...")) ImGui::OpenPopup(kManagerFacePopup);
    ImGui::TextDisabled("%s", labels::code_text("headclasscode", app.db.get_int(t, m.rec, "headclasscode", 0), "Head").c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Head model %lld", static_cast<long long>(head));
    ImGui::TextDisabled("Picture: %s (edit or render it in the Miniface tab, saved as heads_staff_%lld)",
                        pic.empty() ? "none in the game files" : file_part(pic).c_str(), static_cast<long long>(m.managerid));
    ImGui::EndGroup();
    manager_real_face_picker(app, m.managerid);
    ImGui::SeparatorText("Hair & beard (3D)");
    faces3d_manager_section(app, t, m);  // ui_faces3d_gate.cpp
    ImGui::SeparatorText("Outfit");
    item_galleries(app, t, m.rec, true);  // the game's outfit pictures (ui_images.cpp)
    ImGui::SeparatorText("Appearance fields");
    ImGui::TextDisabled("Changing headassetid / headclasscode here leaves his picture as it is: Choose a real face changes both together.");
    field_grid(app, t, m.rec,
               {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation", "skintonecode", "skintypecode",
                "skincomplexion", "haircolorcode", "hairtypecode", "hairstylecode", "facialhairtypecode", "facialhaircolorcode",
                "eyecolorcode", "eyebrowcode", "bodytypecode", "height", "weight", "outfitid"},
               "##mappgrid", 2);
    ImGui::EndChild();
}

}  // namespace turbo
