// FC 27 LE Turbo GUI - pictures shown in the Turbo window (Dear ImGui user textures, created by the renderer backend).
// Files are decoded on demand (a few per frame), scaled down to the size they are shown at, and the least recently
// used ones are released, so a grid of thousands of faces never holds more than kMaxTextures pictures.
#pragma once
#include <cstdint>
#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/image.h"

struct ImTextureData;

namespace turbo {

class TextureCache {
public:
    static constexpr size_t kMaxTextures = 320;
    static constexpr int kDecodesPerFrame = 4;

    ~TextureCache();

    struct Pic {
        ImTextureData* tex = nullptr;  // null: not ready (decoding later this frame budget) or failed
        int w = 0, h = 0;
        bool failed = false;
        std::string error;
    };
    // Picture of a file at most max_side pixels on its longer side. The file's time stamp is checked so a replaced file
    // is shown anew.
    Pic file(const std::filesystem::path& p, int max_side);
    // Picture made by the caller (e.g. the framed miniface preview). `key` names it, `version` changes when the pixels
    // change (the old texture is released).
    Pic pixels(const std::string& key, uint64_t version, const Rgba& img);
    // Forget a file (after writing it)
    void forget(const std::filesystem::path& p);
    // Call once per frame, before drawing (frame budget, releasing old textures)
    void new_frame(double now);
    // Release everything (window closed, cache emptied)
    void clear();
    size_t size() const { return entries_.size(); }
    // Counters (tests): textures created, and released ones the renderer has destroyed and Turbo has freed
    size_t created() const { return created_; }
    size_t freed() const { return freed_; }

    // ---- background decoding (ui/preload.h): files are decoded on a worker thread to at most `side` pixels and kept
    // in memory (at most kPreloadBytes; past it, later ones are decoded when shown). file() then takes the decoded picture without reading
    // the disk: kUploadsPerFrame such uploads per frame, besides the kDecodesPerFrame decodes on the spot.
    static constexpr size_t kPreloadBytes = size_t(128) << 20;
    static constexpr int kUploadsPerFrame = 16;
    void preload(const std::filesystem::path& p, int side);  // queued; nothing when already decoded or queued
    size_t preload_queued() const;   // waiting for the worker
    size_t preload_decoded() const;  // decoded pictures in memory
    size_t preload_done() const;     // decoded or unreadable since the start (progress)
    // Tests: wait until the worker has nothing left (at most `seconds`); true when idle
    bool preload_wait(double seconds);

private:
    struct Entry {
        ImTextureData* tex = nullptr;
        int w = 0, h = 0;
        std::filesystem::file_time_type mtime{};
        uintmax_t fsize = 0;
        uint64_t version = 0;
        double last_used = 0.0;
        bool failed = false;
        std::string error;
    };
    ImTextureData* upload(const Rgba& img);
    void release(ImTextureData* t);
    void evict();

    std::unordered_map<std::string, Entry> entries_;
    std::vector<ImTextureData*> dying_;
    double now_ = 0.0;
    int decodes_left_ = kDecodesPerFrame;
    int uploads_left_ = kUploadsPerFrame;
    size_t created_ = 0, freed_ = 0;

    // background decoding (worker thread; everything below is guarded by pre_->m)
    struct Decoded {
        std::filesystem::file_time_type mtime{};
        uintmax_t fsize = 0;
        int side = 0;       // asked size; the picture is complete when it was not larger than that
        bool whole = false; // the file's picture was not scaled down (fits any size it is drawn at)
        Rgba img;
    };
    struct Preload;
    Preload* pre_ = nullptr;
    // A decoded picture for this file at max_side (moved out of the store); false when there is none
    bool take_decoded(const std::filesystem::path& p, std::filesystem::file_time_type mtime, uintmax_t fsize, int max_side,
                      Rgba& out);
};

}  // namespace turbo
