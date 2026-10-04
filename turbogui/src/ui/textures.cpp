#include "textures.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_set>

#include "imgui.h"
#include "imgui_internal.h"

namespace turbo {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- background decoding
struct TextureCache::Preload {
    std::mutex m;
    std::condition_variable cv;
    std::thread worker;
    bool stop = false;
    bool working = false;
    std::deque<std::pair<fs::path, int>> queue;
    std::unordered_set<std::string> queued;          // path strings in `queue`
    std::unordered_map<std::string, Decoded> store;  // path string -> decoded picture
    size_t bytes = 0;
    size_t done = 0;

    void run() {
        for (;;) {
            std::pair<fs::path, int> job;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&] { return stop || !queue.empty(); });
                if (stop) return;
                job = std::move(queue.front());
                queue.pop_front();
                working = true;
            }
            Decoded d;
            std::error_code ec;
            d.mtime = fs::last_write_time(job.first, ec);
            if (!ec) d.fsize = fs::file_size(job.first, ec);
            d.side = job.second;
            Rgba img;
            const bool ok = !ec && load_image_file(job.first, img, nullptr);
            if (ok) {
                d.whole = std::max(img.w, img.h) <= job.second;
                d.img = d.whole ? std::move(img) : fit_image(img, job.second);
            }
            std::lock_guard<std::mutex> lock(m);
            const std::string key = job.first.string();
            queued.erase(key);
            working = false;
            ++done;
            if (!ok || d.img.empty()) continue;
            auto it = store.find(key);
            const size_t old_size = it != store.end() ? it->second.img.px.size() : 0;
            // full: the first ones asked (the most wanted, ui/preload.h orders them) stay; later ones load on demand
            if (bytes - old_size + d.img.px.size() > kPreloadBytes) continue;
            bytes = bytes - old_size + d.img.px.size();
            store[key] = std::move(d);
        }
    }
};

void TextureCache::preload(const fs::path& p, int side) {
    if (side <= 0) return;
    if (!pre_) {
        pre_ = new Preload();
        pre_->worker = std::thread([this] { pre_->run(); });
    }
    const std::string key = p.string();
    std::lock_guard<std::mutex> lock(pre_->m);
    if (pre_->queued.count(key)) return;
    auto it = pre_->store.find(key);
    if (it != pre_->store.end() && (it->second.whole || it->second.side >= side)) return;
    pre_->queued.insert(key);
    pre_->queue.emplace_back(p, side);
    pre_->cv.notify_one();
}

size_t TextureCache::preload_queued() const {
    if (!pre_) return 0;
    std::lock_guard<std::mutex> lock(pre_->m);
    return pre_->queue.size() + (pre_->working ? 1 : 0);
}

size_t TextureCache::preload_decoded() const {
    if (!pre_) return 0;
    std::lock_guard<std::mutex> lock(pre_->m);
    return pre_->store.size();
}

size_t TextureCache::preload_done() const {
    if (!pre_) return 0;
    std::lock_guard<std::mutex> lock(pre_->m);
    return pre_->done;
}

bool TextureCache::preload_wait(double seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (preload_queued() > 0) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

bool TextureCache::take_decoded(const fs::path& p, fs::file_time_type mtime, uintmax_t fsize, int max_side, Rgba& out) {
    if (!pre_) return false;
    std::lock_guard<std::mutex> lock(pre_->m);
    auto it = pre_->store.find(p.string());
    if (it == pre_->store.end()) return false;
    const Decoded& d = it->second;
    if (d.mtime != mtime || d.fsize != fsize) {  // the file changed since: decode it again
        pre_->bytes -= d.img.px.size();
        pre_->store.erase(it);
        return false;
    }
    if (!d.whole && d.side < max_side) return false;  // decoded smaller than it is drawn now
    out = fit_image(d.img, max_side);  // copy (the store keeps it for other sizes and after an eviction)
    return !out.empty();
}

TextureCache::~TextureCache() {
    if (pre_) {
        {
            std::lock_guard<std::mutex> lock(pre_->m);
            pre_->stop = true;
        }
        pre_->cv.notify_all();
        if (pre_->worker.joinable()) pre_->worker.join();
        delete pre_;
        pre_ = nullptr;
    }
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
    ++created_;
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
    uploads_left_ = kUploadsPerFrame;
    for (size_t i = 0; i < dying_.size();) {
        ImTextureData* t = dying_[i];
        if (t->Status == ImTextureStatus_Destroyed) {
            if (ImGui::GetCurrentContext()) ImGui::UnregisterUserTexture(t);
            IM_DELETE(t);
            ++freed_;
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
    Rgba img;
    bool decoded = false;
    // decoded in the background already (ui/preload.h): an upload only, no disk read
    if (uploads_left_ > 0 && take_decoded(path, mtime, fsize, max_side, img)) {
        --uploads_left_;
        decoded = true;
    }
    if (!decoded) {
        if (decodes_left_ <= 0) return pic_of(nullptr, 0, 0, false, "");
        --decodes_left_;
    }
    Entry e;
    e.mtime = mtime;
    e.fsize = fsize;
    e.last_used = now_;
    std::string err;
    if (!decoded && !load_image_file(path, img, &err)) {
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
