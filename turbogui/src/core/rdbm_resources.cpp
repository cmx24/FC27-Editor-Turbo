#include "rdbm_resources.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

#include "nlohmann/json.hpp"

// stb_image's GIF reader in this file only (core/image.cpp builds stb without GIF): every stb function here is static, so the
// two copies never meet. The frames are read one at a time (stbi__gif_load_next) and scaled down at once: a 134-frame free-kick
// GIF never sits whole in memory (stbi_load_gif_from_memory would hold 117 MB of full-size frames).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include "stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fs = std::filesystem;

namespace turbo::visual {

// ---------------------------------------------------------------- names

size_t set_count() { return sizeof(data::kSets) / sizeof(data::kSets[0]); }
const SetDef& set_at(size_t i) { return data::kSets[i]; }

const SetDef* find_set(const std::string& id) {
    for (const SetDef& s : data::kSets)
        if (id == s.id) return &s;
    return nullptr;
}

const SetDef* set_for_field(const std::string& field) {
    for (const data::FieldDef& f : data::kFields)
        if (field == f.field) return &data::kSets[f.set];
    return nullptr;
}

std::vector<std::string> mapped_fields() {
    std::vector<std::string> v;
    for (const data::FieldDef& f : data::kFields) v.emplace_back(f.field);
    return v;
}

const CodeDef* codes_begin(const SetDef& s) { return &data::kCodes[s.first]; }

const CodeDef* find_code(const SetDef& s, int64_t code) {
    const CodeDef* b = codes_begin(s);
    const CodeDef* e = b + s.count;
    const CodeDef* it = std::lower_bound(b, e, code, [](const CodeDef& c, int64_t v) { return c.code < v; });
    return it != e && it->code == code ? it : nullptr;
}

static bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') return false;
    return true;
}

std::string code_name(const SetDef& s, int64_t code, const std::string& fallback) {
    if (const CodeDef* c = find_code(s, code); c && c->name && c->name[0]) return c->name;
    if (!fallback.empty() && !all_digits(fallback)) return fallback;
    return std::string(s.title) + " " + std::to_string(code);
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<int> filter_codes(const SetDef& s, const std::string& query, const std::vector<std::string>* fallback_names) {
    std::vector<int> out;
    const std::string q = lower(query);
    const CodeDef* b = codes_begin(s);
    for (int i = 0; i < s.count; ++i) {
        const CodeDef& c = b[i];
        if (q.empty()) {
            out.push_back(c.code);
            continue;
        }
        const std::string fb = fallback_names && static_cast<size_t>(i) < fallback_names->size() ? (*fallback_names)[static_cast<size_t>(i)] : "";
        const std::string name = lower(code_name(s, c.code, fb));
        if (name.find(q) != std::string::npos || std::to_string(c.code).find(q) != std::string::npos) out.push_back(c.code);
    }
    return out;
}

// ---------------------------------------------------------------- the folder

static bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

static bool holds_sets(const fs::path& d) { return is_dir(d / "runstyle") || is_dir(d / "hairstyle") || is_dir(d / "fkanim"); }

fs::path resources_dir(const fs::path& root) {
    if (root.empty()) return {};
    if (holds_sets(root / "Resources")) return root / "Resources";
    if (holds_sets(root)) return root;
    return {};
}

fs::path setting_file(const fs::path& turbo_output) { return turbo_output / "visual_pictures.json"; }

std::string load_folder_setting(const fs::path& turbo_output) {
    std::ifstream f(setting_file(turbo_output), std::ios::binary);
    if (!f) return "";
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        auto it = j.find("rdbm_folder");
        if (it != j.end() && it->is_string()) return it->get<std::string>();
    } catch (...) {
    }
    return "";
}

bool save_folder_setting(const fs::path& turbo_output, const std::string& folder, std::string* err) {
    std::error_code ec;
    fs::create_directories(turbo_output, ec);
    nlohmann::json j = nlohmann::json::object();
    j["rdbm_folder"] = folder;
    const fs::path p = setting_file(turbo_output);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write the setting";
            return false;
        }
        f << j.dump(1) << "\n";
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    return true;
}

fs::path find_resources(const fs::path& turbo_output) {
    const std::string s = load_folder_setting(turbo_output);
    return resources_dir(s.empty() ? fs::path(kDefaultRoot) : fs::u8path(s));
}

bool FolderIndex::has(const fs::path& dir, const std::string& file) {
    const std::string key = lower(dir.u8string());
    auto it = dirs_.find(key);
    if (it == dirs_.end()) {
        std::unordered_set<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator d(dir, ec), end; !ec && d != end; d.increment(ec)) names.insert(lower(d->path().filename().u8string()));
        it = dirs_.emplace(key, std::move(names)).first;
    }
    return it->second.count(lower(file)) != 0;
}

void FolderIndex::reset() { dirs_.clear(); }

static bool file_there(const fs::path& dir, const std::string& file, FolderIndex* idx) {
    if (idx) return idx->has(dir, file);
    std::error_code ec;
    return fs::is_regular_file(dir / file, ec);
}

fs::path picture_file(const fs::path& resources, const SetDef& s, const CodeDef& c, FolderIndex* idx) {
    if (resources.empty() || !c.file || !s.dir || !s.dir[0]) return {};
    if (file_there(resources / s.dir, c.file, idx)) return resources / s.dir / c.file;
    if (s.fallback_dir && file_there(resources / s.fallback_dir, c.file, idx)) return resources / s.fallback_dir / c.file;
    return {};
}

int64_t eyebrow_style(int64_t code) { return code / 10000 * 10000; }

const CodeDef* find_code_or_style(const SetDef& s, int64_t code) {
    if (const CodeDef* c = find_code(s, code)) return c;
    if (std::strcmp(s.id, "eyebrow") == 0) return find_code(s, eyebrow_style(code));
    return nullptr;
}

fs::path accessory_picture(const fs::path& resources, int64_t colour, int64_t accessory, FolderIndex* idx) {
    if (resources.empty() || accessory <= 0) return {};
    const fs::path dir = resources / "Accessory";
    const std::string coloured = "Accessory_" + std::to_string(colour) + "_" + std::to_string(accessory) + ".png";
    if (colour >= 0 && file_there(dir, coloured, idx)) return dir / coloured;
    const std::string plain = "Accessory_" + std::to_string(accessory) + ".png";
    if (file_there(dir, plain, idx)) return dir / plain;
    return {};
}

bool accessory_colour_swatch(int64_t colour, Rgb8& out) {
    static const Rgb8 kColours[] = {{255, 255, 255}, {20, 20, 20},   {30, 90, 220},  {210, 35, 35},  {245, 215, 40},
                                    {20, 100, 50},   {240, 130, 20}, {130, 60, 190}, {110, 70, 40},  {240, 130, 175},
                                    {110, 15, 40},   {40, 205, 225}, {15, 30, 110}};
    if (colour < 0 || colour >= static_cast<int64_t>(sizeof(kColours) / sizeof(kColours[0]))) return false;
    out = kColours[colour];
    return true;
}

uint8_t linear_to_srgb8(double v) {
    v = std::min(1.0, std::max(0.0, v));
    const double s = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
    return static_cast<uint8_t>(std::lround(s * 255.0));
}

bool decode_colour_json(const std::vector<uint8_t>& bytes, Rgb8& out, std::string* err) {
    try {
        const nlohmann::json j = nlohmann::json::parse(bytes.begin(), bytes.end());
        const nlohmann::json& c = j.at("Color");
        out.r = linear_to_srgb8(c.at("x").get<double>());
        out.g = linear_to_srgb8(c.at("y").get<double>());
        out.b = linear_to_srgb8(c.at("z").get<double>());
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("not a colour file (") + e.what() + ")";
        return false;
    }
}

bool is_colour_file(const fs::path& p) { return lower(p.extension().u8string()) == ".json"; }

// ---------------------------------------------------------------- GIF

int Anim::frame_at(double ms) const {
    if (frames <= 1 || total_ms <= 0) return 0;
    double t = std::fmod(ms, static_cast<double>(total_ms));
    if (t < 0) t += total_ms;
    for (int i = 0; i < frames; ++i) {
        t -= delays_ms[static_cast<size_t>(i)];
        if (t < 0) return i;
    }
    return frames - 1;
}

void Anim::uv(int frame, float& u0, float& v0, float& u1, float& v1) const {
    if (atlas.empty() || cols <= 0) {
        u0 = v0 = 0.0f;
        u1 = v1 = 1.0f;
        return;
    }
    frame = std::clamp(frame, 0, std::max(0, frames - 1));
    const int cx = frame % cols, cy = frame / cols;
    u0 = static_cast<float>(cx * frame_w) / static_cast<float>(atlas.w);
    v0 = static_cast<float>(cy * frame_h) / static_cast<float>(atlas.h);
    u1 = static_cast<float>((cx + 1) * frame_w) / static_cast<float>(atlas.w);
    v1 = static_cast<float>((cy + 1) * frame_h) / static_cast<float>(atlas.h);
}

// Size and frame count from the block structure (no pixels decoded): image descriptors are counted
static bool gif_scan(const std::vector<uint8_t>& d, int& w, int& h, int& frames) {
    w = h = frames = 0;
    if (d.size() < 13 || std::memcmp(d.data(), "GIF8", 4) != 0) return false;
    w = d[6] | d[7] << 8;
    h = d[8] | d[9] << 8;
    size_t i = 13;
    if (d[10] & 0x80) i += 3u * (2u << (d[10] & 7));
    auto skip_subblocks = [&]() -> bool {
        while (i < d.size() && d[i]) i += d[i] + 1u;
        if (i >= d.size()) return false;
        ++i;
        return true;
    };
    while (i < d.size()) {
        const uint8_t b = d[i];
        if (b == 0x3B) break;
        if (b == 0x21) {
            i += 2;
            if (!skip_subblocks()) break;
        } else if (b == 0x2C) {
            if (i + 10 > d.size()) break;
            const uint8_t fl = d[i + 9];
            i += 10;
            if (fl & 0x80) i += 3u * (2u << (fl & 7));
            ++i;  // LZW minimum code size
            if (!skip_subblocks()) break;
            ++frames;
        } else {
            break;
        }
    }
    return w > 0 && h > 0;
}

// Alpha-weighted area average of a w x h RGBA frame into a dw x dh rectangle of `dst` at (ox, oy)
static void shrink_into(const uint8_t* src, int w, int h, Rgba& dst, int ox, int oy, int dw, int dh) {
    const double sx = static_cast<double>(w) / dw, sy = static_cast<double>(h) / dh;
    for (int y = 0; y < dh; ++y) {
        const int y0 = static_cast<int>(y * sy), y1 = std::max(y0 + 1, std::min(h, static_cast<int>((y + 1) * sy)));
        for (int x = 0; x < dw; ++x) {
            const int x0 = static_cast<int>(x * sx), x1 = std::max(x0 + 1, std::min(w, static_cast<int>((x + 1) * sx)));
            uint32_t acc[4] = {0, 0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* p = src + (static_cast<size_t>(yy) * w + x0) * 4;
                for (int xx = x0; xx < x1; ++xx, p += 4) {
                    const uint32_t a = p[3];
                    acc[0] += p[0] * a;
                    acc[1] += p[1] * a;
                    acc[2] += p[2] * a;
                    acc[3] += a;
                    ++n;
                }
            }
            uint8_t* o = dst.at(ox + x, oy + y);
            if (acc[3]) {
                o[0] = static_cast<uint8_t>(acc[0] / acc[3]);
                o[1] = static_cast<uint8_t>(acc[1] / acc[3]);
                o[2] = static_cast<uint8_t>(acc[2] / acc[3]);
            } else {
                o[0] = o[1] = o[2] = 0;
            }
            o[3] = static_cast<uint8_t>(n ? acc[3] / n : 0);
        }
    }
}

bool decode_gif(const std::vector<uint8_t>& bytes, int max_side, size_t max_bytes, Anim& out, std::string* err) {
    out = Anim();
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        out = Anim();
        return false;
    };
    int w = 0, h = 0, n = 0;
    if (!gif_scan(bytes, w, h, n)) return fail("not an animated picture");
    if (n <= 0) return fail("no frames");
    if (w > STBI_MAX_DIMENSIONS || h > STBI_MAX_DIMENSIONS) return fail("picture too large");
    if (bytes.size() > static_cast<size_t>(INT32_MAX)) return fail("file too large");
    // the frame size: at most max_side, then smaller until the atlas fits the byte limit and the texture side limit
    double scale = std::min(1.0, static_cast<double>(std::max(1, max_side)) / std::max(w, h));
    int fw = 0, fh = 0, cols = 0, rows = 0;
    for (int tries = 0; tries < 64; ++tries) {
        fw = std::max(1, static_cast<int>(std::lround(w * scale)));
        fh = std::max(1, static_cast<int>(std::lround(h * scale)));
        cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n) * fh / fw))));
        cols = std::min(cols, std::max(1, kMaxAtlasSide / fw));
        rows = (n + cols - 1) / cols;
        const size_t need = static_cast<size_t>(fw) * fh * 4u * static_cast<size_t>(n);
        if ((max_bytes == 0 || need <= max_bytes) && rows * fh <= kMaxAtlasSide && cols * fw <= kMaxAtlasSide) break;
        scale *= 0.9;
    }
    out.src_w = w;
    out.src_h = h;
    out.frame_w = fw;
    out.frame_h = fh;
    out.cols = cols;
    out.atlas.w = cols * fw;
    out.atlas.h = rows * fh;
    out.atlas.px.assign(static_cast<size_t>(out.atlas.w) * out.atlas.h * 4u, 0);

    stbi__context s;
    stbi__start_mem(&s, bytes.data(), static_cast<int>(bytes.size()));
    if (!stbi__gif_test(&s)) return fail("not an animated picture");
    stbi__gif g;
    std::memset(&g, 0, sizeof(g));
    std::vector<uint8_t> back1, back2;  // the last two full-size frames ("restore to previous" disposal reads the one before last)
    const size_t stride = static_cast<size_t>(w) * h * 4u;
    int comp = 0, got = 0;
    for (;;) {
        stbi_uc* two_back = back2.size() == stride ? back2.data() : nullptr;
        stbi_uc* u = stbi__gif_load_next(&s, &g, &comp, 4, two_back);
        if (u == reinterpret_cast<stbi_uc*>(&s)) u = nullptr;  // end of the animation
        if (!u) break;
        if (g.w != w || g.h != h || got >= n) break;  // never past what the scan sized
        const int cx = got % cols, cy = got / cols;
        shrink_into(u, w, h, out.atlas, cx * fw, cy * fh, fw, fh);
        int delay = g.delay;
        if (delay < 20) delay = 100;
        out.delays_ms.push_back(delay);
        out.total_ms += delay;
        ++got;
        back2.swap(back1);
        back1.assign(u, u + stride);
    }
    STBI_FREE(g.out);
    STBI_FREE(g.history);
    STBI_FREE(g.background);
    if (got == 0) {
        const char* why = stbi__g_failure_reason;
        return fail(std::string("cannot read the animation") + (why ? std::string(" (") + why + ")" : ""));
    }
    out.frames = got;
    return true;
}

bool decode_gif_file(const fs::path& p, int max_side, size_t max_bytes, Anim& out, std::string* err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open the file";
        out = Anim();
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return decode_gif(bytes, max_side, max_bytes, out, err);
}

bool decode_picture_file(const fs::path& p, int max_side, size_t max_bytes, Anim& out, std::string* err) {
    std::string ext = p.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".gif") return decode_gif_file(p, max_side, max_bytes, out, err);
    out = Anim();
    if (ext == ".json") {  // a colour file: one pixel of that colour (swatch drawn from it, no texture)
        std::ifstream f(p, std::ios::binary);
        if (!f) {
            if (err) *err = "cannot open the file";
            return false;
        }
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        Rgb8 c;
        if (bytes.size() > (size_t(1) << 20)) {
            if (err) *err = "colour file too large";
            return false;
        }
        if (!decode_colour_json(bytes, c, err)) return false;
        out.src_w = out.frame_w = 1;
        out.src_h = out.frame_h = 1;
        out.cols = 1;
        out.frames = 1;
        out.delays_ms.push_back(0);
        out.atlas.w = out.atlas.h = 1;
        out.atlas.px = {c.r, c.g, c.b, 255};
        return true;
    }
    Rgba img;
    if (!load_image_file(p, img, err)) return false;
    img = fit_image(img, std::max(1, max_side));
    if (img.empty()) {
        if (err) *err = "empty picture";
        return false;
    }
    if (max_bytes && img.px.size() > max_bytes) {
        if (err) *err = "picture too large";
        return false;
    }
    out.src_w = out.frame_w = img.w;
    out.src_h = out.frame_h = img.h;
    out.cols = 1;
    out.frames = 1;
    out.delays_ms.push_back(0);
    out.total_ms = 0;
    out.atlas = std::move(img);
    return true;
}

// ---------------------------------------------------------------- the store

AnimStore::AnimStore(size_t budget_bytes) : budget_(budget_bytes) { worker_ = std::thread([this] { run(); }); }

AnimStore::~AnimStore() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::string AnimStore::key_of(const fs::path& p, int side) { return "anim:" + p.u8string() + "@" + std::to_string(side); }

AnimStore::Item AnimStore::get(const fs::path& p, int side, size_t max_bytes) {
    Item it;
    it.key = key_of(p, side);
    bool wake = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        ++tick_;
        auto e = entries_.find(it.key);
        if (e == entries_.end()) {
            Entry ne;
            ne.key = it.key;
            ne.path = p;
            ne.side = side;
            ne.max_bytes = max_bytes;
            ne.used = tick_;
            entries_.emplace(it.key, std::move(ne));
            queue_.push_front(it.key);
            wake = true;
        } else {
            Entry& en = e->second;
            en.used = tick_;
            if (en.done) {
                it.failed = en.failed;
                it.error = en.error;
                if (!en.failed) it.anim = &en.anim;
            } else {
                // asked again while waiting: to the front (what is on screen now goes first)
                auto q = std::find(queue_.begin(), queue_.end(), it.key);
                if (q != queue_.end() && q != queue_.begin()) {
                    queue_.erase(q);
                    queue_.push_front(it.key);
                } else if (q == queue_.end() && !busy_) {
                    queue_.push_front(it.key);  // cancelled earlier: ask again
                    wake = true;
                }
            }
        }
        evict_locked();
    }
    if (wake) cv_.notify_one();
    return it;
}

// Least recently used first; never the one returned by the last get() (its pointer is in use this frame)
void AnimStore::evict_locked() {
    while (bytes_ > budget_) {
        Entry* oldest = nullptr;
        for (auto& kv : entries_) {
            Entry& e = kv.second;
            if (!e.done || e.failed || e.used == tick_) continue;
            if (!oldest || e.used < oldest->used) oldest = &e;
        }
        if (!oldest) break;
        bytes_ -= 2 * oldest->anim.bytes();
        evicted_.push_back(oldest->key);
        entries_.erase(oldest->key);
    }
}

void AnimStore::drop(const std::string& key) {
    std::lock_guard<std::mutex> lk(m_);
    auto e = entries_.find(key);
    if (e == entries_.end() || !e->second.done) return;  // a decode in progress finishes into its entry
    bytes_ -= 2 * e->second.anim.bytes();
    entries_.erase(e);
}

void AnimStore::cancel_queued() {
    std::lock_guard<std::mutex> lk(m_);
    for (const std::string& k : queue_) {
        auto e = entries_.find(k);
        if (e != entries_.end() && !e->second.done) entries_.erase(e);
    }
    queue_.clear();
}

void AnimStore::clear() {
    std::lock_guard<std::mutex> lk(m_);
    queue_.clear();
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.done) {
            evicted_.push_back(it->first);
            it = entries_.erase(it);
        } else {
            ++it;  // the one being decoded
        }
    }
    bytes_ = 0;
}

std::vector<std::string> AnimStore::take_evicted() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::string> v;
    v.swap(evicted_);
    return v;
}

size_t AnimStore::bytes() const {
    std::lock_guard<std::mutex> lk(m_);
    return bytes_;
}

size_t AnimStore::ready_count() const {
    std::lock_guard<std::mutex> lk(m_);
    size_t n = 0;
    for (const auto& kv : entries_) n += kv.second.done && !kv.second.failed;
    return n;
}

size_t AnimStore::queued_count() const {
    std::lock_guard<std::mutex> lk(m_);
    return queue_.size();
}

size_t AnimStore::decoded_total() const {
    std::lock_guard<std::mutex> lk(m_);
    return decoded_;
}

bool AnimStore::wait_idle(double seconds) {
    std::unique_lock<std::mutex> lk(m_);
    return idle_cv_.wait_for(lk, std::chrono::duration<double>(seconds), [this] { return queue_.empty() && !busy_; });
}

void AnimStore::run() {
    for (;;) {
        std::string key;
        fs::path path;
        int side = 0;
        size_t max_bytes = 0;
        {
            std::unique_lock<std::mutex> lk(m_);
            idle_cv_.notify_all();
            cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            key = queue_.front();
            queue_.pop_front();
            auto e = entries_.find(key);
            if (e == entries_.end() || e->second.done) continue;
            path = e->second.path;
            side = e->second.side;
            max_bytes = e->second.max_bytes;
            busy_ = true;
        }
        Anim a;
        std::string err;
        const bool ok = decode_picture_file(path, side, max_bytes, a, &err);
        {
            std::lock_guard<std::mutex> lk(m_);
            busy_ = false;
            ++decoded_;
            auto e = entries_.find(key);
            if (e != entries_.end() && !e->second.done) {
                Entry& en = e->second;
                en.done = true;
                en.failed = !ok;
                en.error = err;
                if (ok) {
                    bytes_ += 2 * a.bytes();
                    en.anim = std::move(a);
                }
            }
        }
    }
}

}  // namespace turbo::visual
