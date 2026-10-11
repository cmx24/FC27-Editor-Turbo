// FC 27 LE Turbo GUI - background loading of what Turbo's screens show (1.1.1; order and cancellation 2026-10-11).
// Starts at the first show (F8) and when a career connects, never blocks a frame:
//   * game pictures, in the order of ui/preload_queue.h: the user's club's minifaces, the listed players' minifaces, the
//     real-face chooser (real-face minifaces and the 3D head renders already in turbo_output\cache\faces3d), tattoo
//     previews, hair / facial hair / boots / gloves / accessory previews, club crests, managers' real-face heads.
//     Missing ones are asked from Turbo's Lua side after everything on screen (LegacyImages::ask_background); while
//     Turbo is shown the host nudges Lua with its synthetic career event (App::lua_images_wanted), and Lua exports for
//     at most 0.25 s per career event (bridge.lua pump(0.25), unchanged). Exported files stay in turbo_output\cache\legacy
//     for later sessions (the disk cache), so the next start only decodes them. Lua's background list holds at most
//     LegacyImages::kMaxBackground (20 000) paths: a picture refused because the list is full stays queued and unasked,
//     is asked again as the list drains (a few retries per frame, so a full list costs no frame time), and the progress
//     line says "waiting: N refused" meanwhile.
//   * decoding: files at hand go to TextureCache::preload (worker thread); the grids then only upload them.
//   * callnames: the loaded language and the spoken set (Callnames::refresh) before the Callname tab is opened.
//   * the setting gui.preload_images (ui/gui_prefs.h, default true) switches the pictures off; when the game leaves the
//     career the queue is cancelled and collected again for the next career.
// A few paths are checked per frame (file stats), so thousands of pictures cost no visible frame time.
// Header-only: included by app.cpp only.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "app.h"
#include "preload_queue.h"
#include "ui_faces.h"
#include "ui_images.h"

namespace turbo {

class Preloader {
public:
    static constexpr size_t kChecksPerFrame = 64;  // file stats per frame
    static constexpr size_t kRefusedPerFrame = 4;  // retries of refused pictures per frame while Lua's list is full
    static constexpr float kSide = 100.0f;         // decoded size (unscaled px): the largest grid cell (S(100))

    bool started() const { return started_; }
    bool collected() const { return collected_; }
    size_t total() const { return queue_.size(); }
    size_t settled() const { return queue_.settled(); }
    size_t missing() const { return missing_; }
    size_t refused() const { return queue_.refused(); }  // pending pictures Lua's full background list refused (asked again)
    size_t refusals() const { return refusals_; }        // how often a picture was refused in all (every retry counts)
    bool callnames_done() const { return callnames_done_; }
    const std::string& why() const { return why_; }
    bool loading() const { return started_ && (!collected_ || !queue_.done()); }
    const PreloadQueue& queue() const { return queue_; }

    void start(const char* why) {
        if (started_) return;
        started_ = true;
        why_ = why ? why : "";
    }

    // Once per frame (App::tick)
    void tick(App& app) {
        if (!started_) return;
        if (!callnames_done_ && !app.game_root.empty()) {
            callnames_done_ = true;
            if (!app.callnames.refreshed)
                app.callnames.refresh(app.bridge.root(), app.game_root, app.chosen_commentary_language());
        }
        if (!app.prefs.preload_images) {  // switched off: forget what was asked, nothing more is asked
            if (collected_) cancel(app, "switched off");
            return;
        }
        const bool in_career = app.connected() && (!app.bridge.state().loaded || app.bridge.state().in_cm);
        if (!collected_) {
            if (!in_career) return;  // the lists come from the career's database
            queue_.reset();
            collect(app);
            queue_.finalize();
            collected_ = true;
            listed_seen_ = app.list_player_ids.size();
            char buf[200];
            std::snprintf(buf, sizeof(buf), "loading %zu pictures in the background (%s; club %zu, listed %zu, faces %zu, tattoos %zu, galleries %zu, crests %zu, managers %zu)",
                          queue_.size(), why_.c_str(), queue_.count(PreloadKind::ClubMiniface), queue_.count(PreloadKind::ListedMiniface),
                          queue_.count(PreloadKind::FaceChooser), queue_.count(PreloadKind::Tattoo), queue_.count(PreloadKind::Gallery),
                          queue_.count(PreloadKind::Crest), queue_.count(PreloadKind::ManagerHead));
            app.log(buf);
            if (app.log_hook) app.log_hook(buf);
        }
        if (!in_career) {  // the career left: stop, and collect again for the next one
            cancel(app, "career left");
            return;
        }
        // The Players list changed (a filter, the first draw): its new faces join the queue
        if (app.list_player_ids.size() != listed_seen_) {
            listed_seen_ = app.list_player_ids.size();
            for (int64_t pid : app.list_player_ids) queue_.add(PreloadKind::ListedMiniface, legacy_path::player_miniface(pid));
        }
        if (queue_.done()) return;
        const int side = static_cast<int>(std::ceil(S(kSide)));
        size_t refused_now = 0;  // back off: a full Lua list is tried kRefusedPerFrame times per frame, then next frame
        for (size_t checks = 0; checks < kChecksPerFrame && refused_now < kRefusedPerFrame; ++checks) {
            PreloadQueue::Item* it = queue_.next_pending();
            if (!it) break;
            if (it->file) {  // a cache file at hand (3D head renders): decode it
                app.textures.preload(std::filesystem::path(it->path), side);
                queue_.settle(*it);
                continue;
            }
            std::filesystem::path f;
            switch (app.legacy.peek(it->path, &f)) {
                case LegacyImages::State::Game:
                    app.textures.preload(f, side);
                    queue_.settle(*it);
                    break;
                case LegacyImages::State::Missing:
                case LegacyImages::State::Invalid:
                    ++missing_;
                    queue_.settle(*it);
                    break;
                default:
                    if (it->asked) break;
                    // Only an accepted (or already known) ask marks the picture asked: a refusal (Lua's list is full) keeps it
                    // queued and unasked, and the round robin brings it back once the list has drained a little
                    if (app.legacy.ask_background(it->path) == LegacyImages::Ask::Full) {
                        queue_.refuse(*it);
                        ++refusals_;
                        ++refused_now;
                    } else {
                        queue_.accept(*it);
                    }
                    break;
            }
        }
        if (queue_.done()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "background loading done: %zu pictures (%zu not in the game)", queue_.size(), missing_);
            app.log(buf);
            if (app.log_hook) app.log_hook(buf);
        }
    }

    // Top bar ("" when nothing is loading)
    std::string line(const App& app) const {
        if (!started_ || !app.prefs.preload_images) return "";
        const size_t decoding = app.textures.preload_queued();
        if (!collected_) return app.connected() ? "Loading..." : "";
        if (queue_.done() && decoding == 0) return "";
        char buf[160];
        if (!queue_.done() && queue_.refused() > 0)
            std::snprintf(buf, sizeof(buf), "Loading pictures: %zu of %zu (waiting: %zu refused)", queue_.settled(), queue_.size(), queue_.refused());
        else if (!queue_.done())
            std::snprintf(buf, sizeof(buf), "Loading pictures: %zu of %zu", queue_.settled(), queue_.size());
        else
            std::snprintf(buf, sizeof(buf), "Preparing pictures: %zu left", decoding);
        return buf;
    }

    // Status tab: one line per kind, most wanted first
    std::vector<std::string> status(const App& app) const {
        std::vector<std::string> out;
        if (!app.prefs.preload_images) {
            out.push_back("Pictures load when a screen shows them (background loading is off)");
            return out;
        }
        if (!started_ || !collected_) {
            out.push_back(app.connected() ? "Background loading starts with the career" : "Background loading waits for a career");
            return out;
        }
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s: %zu of %zu pictures ready (%zu not in the game, %zu still decoding)",
                      queue_.done() ? "Background loading done" : "Loading pictures in the background", queue_.settled(), queue_.size(),
                      missing_, app.textures.preload_queued());
        out.push_back(buf);
        if (queue_.refused() > 0) {
            std::snprintf(buf, sizeof(buf), "  waiting: %zu refused (Turbo's Lua side takes %zu pictures at a time; they are asked again as pictures arrive)",
                          queue_.refused(), app.legacy.background_limit());
            out.push_back(buf);
        }
        for (int k = 0; k < kPreloadKinds; ++k) {
            const PreloadKind kind = static_cast<PreloadKind>(k);
            if (queue_.count(kind) == 0) continue;
            std::snprintf(buf, sizeof(buf), "  %s: %zu of %zu", preload_kind_name(kind), queue_.settled(kind), queue_.count(kind));
            out.push_back(buf);
        }
        return out;
    }

private:
    void cancel(App& app, const char* why) {
        queue_.cancel();
        app.legacy.clear_background();
        collected_ = false;
        missing_ = 0;
        listed_seen_ = 0;
        char buf[120];
        std::snprintf(buf, sizeof(buf), "background loading stopped (%s)", why);
        app.log(buf);
    }

    void add(PreloadKind k, const std::string& p) { queue_.add(k, p); }

    // Most wanted first (preload_queue.h): the user's club, the listed players, the real-face chooser, tattoos, the item
    // galleries, crests, managers' real-face heads
    void collect(App& app) {
        // 1. the user's club
        if (const int64_t tid = app.bridge.state().user_team; tid > 0)
            for (const auto& l : app.model.links_of_team(tid)) add(PreloadKind::ClubMiniface, legacy_path::player_miniface(l.playerid));
        // 2. the Players list as filtered now (empty before the tab was drawn: topped up in tick)
        for (int64_t pid : app.list_player_ids) add(PreloadKind::ListedMiniface, legacy_path::player_miniface(pid));
        // 3. the real-face chooser: real-face minifaces sorted by name like the grid, and the 3D head renders at hand
        if (const Table* t = app.db.table("players")) {
            const Field* hc = t->field("headclasscode");
            const Field* ha = t->field("headassetid");
            const Field* pid = t->field("playerid");
            const Field* hq = t->field("hashighqualityhead");
            Snapshot snap;
            if (hc && ha && pid && snap.load(app.db.memory(), *t)) {
                std::vector<std::pair<std::string, int64_t>> faces;
                for (uint32_t idx : snap.valid) {
                    if (snap.get_int(idx, *ha) <= 0 || snap.get_int(idx, *hc) != 0) continue;
                    if (hq && snap.get_int(idx, *hq) == 0) continue;
                    const int64_t id = snap.get_int(idx, *pid);
                    faces.emplace_back(app.model.player_name(id), id);
                }
                std::sort(faces.begin(), faces.end());
                for (const auto& f : faces) add(PreloadKind::FaceChooser, legacy_path::player_miniface(f.second));
            }
        }
        {
            // ui_faces.cpp saves the game's renders as turbo_output\cache\faces3d\p_<playerid>.dds / m_<headid>.dds; the
            // renders themselves are only made on request (Render all filtered heads), never here
            std::error_code ec;
            const std::filesystem::path dir = app.legacy.cache_dir().parent_path() / "faces3d";
            std::vector<std::string> files;
            if (std::filesystem::is_directory(dir, ec))
                for (const auto& e : std::filesystem::directory_iterator(dir, ec))
                    if (e.is_regular_file(ec) && e.path().extension() == ".dds") files.push_back(e.path().string());
            std::sort(files.begin(), files.end());
            for (const std::string& f : files) queue_.add(PreloadKind::FaceChooser, f, true);
        }
        // 4. tattoos: from the table only without the hash list (with it, gallery_preload_paths has the ones with a picture)
        if (const Table* t = gear_pictures(app).empty() ? app.db.table("tattoo") : nullptr) {
            const Field* idf = t->field("tattooid");
            Snapshot snap;
            if (idf && snap.load(app.db.memory(), *t)) {
                std::vector<int64_t> ids;
                for (uint32_t idx : snap.valid)
                    if (snap.get_int(idx, *idf) > 0) ids.push_back(snap.get_int(idx, *idf));
                std::sort(ids.begin(), ids.end());
                for (int64_t id : ids) add(PreloadKind::Tattoo, legacy_path::tattoo_preview(id));
            }
        }
        // 5. gear previews (players' hair, boots, gloves, accessories, tattoos; managers' outfits): exactly the files Live
        // Editor's hash list names (ui_images.cpp, same paths as the galleries); none without the list. Tattoo previews
        // among them keep their own kind.
        for (const std::string& p : gallery_preload_paths(app))
            add(p.find("tattoo") != std::string::npos ? PreloadKind::Tattoo : PreloadKind::Gallery, p);
        // 6. crests
        for (const auto& tr : app.model.teams()) add(PreloadKind::Crest, crest_main_path(tr.teamid));
        // 7. Managers > Appearance > Choose a real face, "Manager heads" (ui_faces.cpp): their heads_staff pictures
        for (const auto& f : manager_faces(app))
            if (f.real) add(PreloadKind::ManagerHead, legacy_path::staff_miniface(f.headassetid));
    }

    bool started_ = false, collected_ = false, callnames_done_ = false;
    std::string why_;
    PreloadQueue queue_;
    size_t missing_ = 0, listed_seen_ = 0, refusals_ = 0;
};

}  // namespace turbo
