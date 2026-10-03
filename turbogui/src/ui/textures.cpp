#include "textures.h"

#include <algorithm>
#include <cstring>
#include <system_error>

#include "imgui.h"
#include "imgui_internal.h"

namespace turbo {

namespace fs = std::filesystem;

TextureCache::~TextureCache() {
    std::vector<ImTextureData*> all = dying_;
    for (auto& kv : entries_)
        if (kv.second.tex) all.push_back(kv.second.tex);
    for (ImTextureData* t : all) {
        if (ImGui::GetCurrentContext()) ImGui::UnregisterUserTexture(t);
        IM_DELETE(t);
    }
}

ImTextureData* TextureCache::upload(const Rgba& img) {
    if (img.empty() || !ImGui::GetCurrentContext()) return nullptr;
    ImTextureData* t = IM_NEW(ImTextureData)();
    t->Create(ImTextureFormat_RGBA32, img.w, img.h);
    std::memcpy(t->GetPixels(), img.px.data(), img.px.size());
    t->UseColors = true;
    t->UsedRect.x = t->UsedRect.y = 0;
    t->UsedRect.w = static_cast<unsigned short>(img.w);
    t->UsedRect.h = static_cast<unsigned short>(img.h);
    ImGui::RegisterUserTexture(t);
    return t;
}

void TextureCache::release(ImTextureData* t) {
    if (!t) return;
    t->WantDestroyNextFrame = true;
    dying_.push_back(t);
}

void TextureCache::new_frame(double now) {
    now_ = now;
    decodes_left_ = kDecodesPerFrame;
    for (size_t i = 0; i < dying_.size();) {
        ImTextureData* t = dying_[i];
        if (t->Status == ImTextureStatus_Destroyed) {
            if (ImGui::GetCurrentContext()) ImGui::UnregisterUserTexture(t);
            IM_DELETE(t);
            dying_[i] = dying_.back();
            dying_.pop_back();
        } else {
            ++i;
        }
    }
    evict();
}

void TextureCache::evict() {
    if (entries_.size() <= kMaxTextures) return;
    std::vector<std::pair<double, std::string>> order;
    order.reserve(entries_.size());
    for (const auto& kv : entries_) order.emplace_back(kv.second.last_used, kv.first);
    std::sort(order.begin(), order.end());
    size_t excess = entries_.size() - kMaxTextures;
    for (size_t i = 0; i < order.size() && excess > 0; ++i) {
        if (order[i].first >= now_ - 0.5) break;  // shown right now: keep (the grid shows fewer than the maximum)
        auto it = entries_.find(order[i].second);
        release(it->second.tex);
        entries_.erase(it);
        --excess;
    }
}

void TextureCache::clear() {
    for (auto& kv : entries_) release(kv.second.tex);
    entries_.clear();
}

void TextureCache::forget(const fs::path& p) {
    std::string prefix = p.string() + "|";
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
            release(it->second.tex);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

static TextureCache::Pic pic_of(ImTextureData* tex, int w, int h, bool failed, const std::string& err) {
    TextureCache::Pic p;
    // a texture being released is never drawn again (Dear ImGui asserts on it)
    p.tex = (tex && !tex->WantDestroyNextFrame) ? tex : nullptr;
    p.w = w;
    p.h = h;
    p.failed = failed;
    p.error = err;
    return p;
}

TextureCache::Pic TextureCache::file(const fs::path& path, int max_side) {
    std::string key = path.string() + "|" + std::to_string(max_side);
    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);
    uintmax_t fsize = ec ? 0 : fs::file_size(path, ec);
    if (ec) {
        return pic_of(nullptr, 0, 0, true, "file not found");
    }
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        Entry& e = it->second;
        if (e.mtime == mtime && e.fsize == fsize) {
            e.last_used = now_;
            return pic_of(e.tex, e.w, e.h, e.failed, e.error);
        }
        release(e.tex);
        entries_.erase(it);
    }
    if (decodes_left_ <= 0) return pic_of(nullptr, 0, 0, false, "");
    --decodes_left_;
    Entry e;
    e.mtime = mtime;
    e.fsize = fsize;
    e.last_used = now_;
    Rgba img;
    std::string err;
    if (!load_image_file(path, img, &err)) {
        e.failed = true;
        e.error = err;
    } else {
        img = fit_image(img, max_side);
        e.w = img.w;
        e.h = img.h;
        e.tex = upload(img);
        if (!e.tex) {
            e.failed = true;
            e.error = "no texture";
        }
    }
    Pic p = pic_of(e.tex, e.w, e.h, e.failed, e.error);
    entries_[key] = std::move(e);
    return p;
}

TextureCache::Pic TextureCache::pixels(const std::string& key0, uint64_t version, const Rgba& img) {
    std::string key = "#" + key0;
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        if (it->second.version == version) {
            it->second.last_used = now_;
            return pic_of(it->second.tex, it->second.w, it->second.h, it->second.failed, it->second.error);
        }
        release(it->second.tex);
        entries_.erase(it);
    }
    Entry e;
    e.version = version;
    e.last_used = now_;
    e.w = img.w;
    e.h = img.h;
    e.tex = upload(img);
    e.failed = e.tex == nullptr;
    if (e.failed) e.error = "empty picture";
    Pic p = pic_of(e.tex, e.w, e.h, e.failed, e.error);
    entries_[key] = std::move(e);
    return p;
}

}  // namespace turbo
