// FC 27 LE Turbo GUI - background loading of what Turbo's screens show (1.1.1).
// Starts at the first show (F8) and when a career connects, never blocks a frame:
//   * game pictures: real-face minifaces (the "Choose a real face" grid), tattoo previews, hair / facial hair / boots /
//     gloves / accessory previews, club crests. Missing ones are asked from Turbo's Lua side after everything on screen
//     (LegacyImages::want_background); while Turbo is shown the host nudges Lua with its synthetic career event
//     (App::lua_images_wanted), so they arrive without lua\scripts\turbo_images.lua. Exported files stay in
//     turbo_output\cache\legacy for later sessions (the disk cache), so the next start only decodes them.
//   * decoding: files at hand go to TextureCache::preload (worker thread); the grids then only upload them.
//   * callnames: the loaded language and the spoken set (Callnames::refresh) before the Callname tab is opened.
// A few paths are checked per frame (file stats), so thousands of pictures cost no visible frame time.
// Header-only: included by app.cpp only.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "app.h"
#include "ui_images.h"

namespace turbo {

class Preloader {
public:
    static constexpr size_t kChecksPerFrame = 64;  // file stats per frame
    static constexpr float kSide = 100.0f;         // decoded size (unscaled px): the largest grid cell (S(100))

    struct Item {
        std::string path;
        bool settled = false;  // decoded (queued to the worker), missing in the game, or invalid
    };

    bool started() const { return started_; }
    bool collected() const { return collected_; }
    size_t total() const { return items_.size(); }
    size_t settled() const { return settled_; }
    size_t missing() const { return missing_; }
    bool callnames_done() const { return callnames_done_; }
    const std::string& why() const { return why_; }
    bool loading() const { return started_ && (!collected_ || settled_ < items_.size()); }

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
        if (!collected_) {
            if (!app.connected()) return;  // the lists come from the career's database
            collect(app);
            collected_ = true;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "loading %zu pictures in the background (%s)", items_.size(), why_.c_str());
            app.log(buf);
            if (app.log_hook) app.log_hook(buf);
        }
        if (settled_ >= items_.size()) return;
        const int side = static_cast<int>(std::ceil(S(kSide)));
        size_t checks = 0;
        for (size_t n = 0; n < items_.size() && checks < kChecksPerFrame; ++n) {
            if (pos_ >= items_.size()) pos_ = 0;
            Item& it = items_[pos_++];
            if (it.settled) continue;
            ++checks;
            std::filesystem::path f;
            switch (app.legacy.peek(it.path, &f)) {
                case LegacyImages::State::Game:
                    app.textures.preload(f, side);
                    settle(it);
                    break;
                case LegacyImages::State::Missing:
                case LegacyImages::State::Invalid:
                    ++missing_;
                    settle(it);
                    break;
                default:
                    if (!asked_.count(it.path)) {
                        asked_.insert(it.path);
                        app.legacy.want_background(it.path);
                    }
                    break;
            }
        }
        if (settled_ >= items_.size()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "background loading done: %zu pictures (%zu not in the game)", items_.size(), missing_);
            app.log(buf);
            if (app.log_hook) app.log_hook(buf);
        }
    }

    // Top bar ("" when nothing is loading)
    std::string line(const App& app) const {
        if (!started_) return "";
        const size_t decoding = app.textures.preload_queued();
        if (!collected_) return app.connected() ? "Loading..." : "";
        if (settled_ >= items_.size() && decoding == 0) return "";
        char buf[160];
        if (settled_ < items_.size())
            std::snprintf(buf, sizeof(buf), "Loading pictures: %zu of %zu", settled_, items_.size());
        else
            std::snprintf(buf, sizeof(buf), "Preparing pictures: %zu left", decoding);
        return buf;
    }

private:
    void settle(Item& it) {
        it.settled = true;
        ++settled_;
    }
    void add(const std::string& p) {
        if (seen_.insert(p).second) items_.push_back({p, false});
    }
    // Most wanted first: the real-face grid, tattoos, item previews, crests
    void collect(App& app) {
        if (const Table* t = app.db.table("players")) {
            const Field* hc = t->field("headclasscode");
            const Field* ha = t->field("headassetid");
            const Field* pid = t->field("playerid");
            const Field* hq = t->field("hashighqualityhead");
            Snapshot snap;
            if (hc && ha && pid && snap.load(app.db.memory(), *t)) {
                std::vector<std::pair<std::string, int64_t>> faces;  // sorted by name like the grid
                for (uint32_t idx : snap.valid) {
                    if (snap.get_int(idx, *ha) <= 0 || snap.get_int(idx, *hc) != 0) continue;
                    if (hq && snap.get_int(idx, *hq) == 0) continue;
                    const int64_t id = snap.get_int(idx, *pid);
                    faces.emplace_back(app.model.player_name(id), id);
                }
                std::sort(faces.begin(), faces.end());
                for (const auto& f : faces) add(legacy_path::player_miniface(f.second));
            }
        }
        if (const Table* t = app.db.table("tattoo")) {
            const Field* idf = t->field("tattooid");
            Snapshot snap;
            if (idf && snap.load(app.db.memory(), *t)) {
                std::vector<int64_t> ids;
                for (uint32_t idx : snap.valid)
                    if (snap.get_int(idx, *idf) > 0) ids.push_back(snap.get_int(idx, *idf));
                std::sort(ids.begin(), ids.end());
                for (int64_t id : ids) add(legacy_path::tattoo_preview(id));
            }
        }
        // item previews (ui_images.cpp kGalleries: same file names). gallery_ids reads Live Editor's hash list once per
        // session: without the list there is nothing to load, and asking would keep the galleries empty until restart
        static const struct { const char* folder; const char* prefix; bool variant; } kItems[] = {
            {"hairstyle", "item_", true}, {"facialhairstyle", "item_", true}, {"boots", "item_", true},
            {"gkglove", "gkglove_", false}, {"accessories", "item_", true}};
        std::error_code ec;
        const std::filesystem::path root = app.bridge.root();
        const bool have_list = std::filesystem::is_regular_file(root / "legacy_filename_hash_list.csv", ec) ||
                               std::filesystem::is_regular_file(root / "extensions" / "legacy_filename_hash_list.csv", ec);
        for (const auto& g : kItems) {
            if (!have_list) break;
            for (int64_t id : gallery_ids(app, g.folder)) {
                char buf[200];
                if (g.variant) std::snprintf(buf, sizeof(buf), "data/ui/imgAssets/%s/%s%lld_0.dds", g.folder, g.prefix, static_cast<long long>(id));
                else std::snprintf(buf, sizeof(buf), "data/ui/imgAssets/%s/%s%lld.dds", g.folder, g.prefix, static_cast<long long>(id));
                add(buf);
            }
        }
        for (const auto& tr : app.model.teams()) add(crest_main_path(tr.teamid));
    }

    bool started_ = false, collected_ = false, callnames_done_ = false;
    std::string why_;
    std::vector<Item> items_;
    std::unordered_set<std::string> seen_, asked_;
    size_t pos_ = 0, settled_ = 0, missing_ = 0;
};

}  // namespace turbo
