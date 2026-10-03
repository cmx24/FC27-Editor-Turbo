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
    size_t created_ = 0, freed_ = 0;
};

}  // namespace turbo
