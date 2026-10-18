// FC 27 LE Turbo GUI - body-type thumbnails (see bodytype_thumbs.h)
#include "bodytype_thumbs.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstring>
#include <fstream>
#include <sstream>

#include "looks_fields_data.h"
#include "nlohmann/json.hpp"

namespace turbo::bodytype_thumbs {

using json = nlohmann::json;

namespace {

std::atomic<int> g_running{0};

bool digits_only(const std::string& s) {
    if (s.empty() || s.size() > 6) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string code_text(int64_t code) { return std::to_string(code); }

}  // namespace

// ---------------------------------------------------------------- paths
fs::path folder(const fs::path& t) { return t / "looks" / "bodytypes"; }
fs::path manifest_file(const fs::path& t) { return folder(t) / "manifest.json"; }
fs::path run_file(const fs::path& t) { return folder(t) / "run.json"; }
fs::path log_file(const fs::path& t) { return folder(t) / "run.log"; }
fs::path request_file(const fs::path& t) { return folder(t) / "make.json"; }
fs::path grid_file(const fs::path& t, int64_t code) { return folder(t) / (code_text(code) + ".png"); }
fs::path full_file(const fs::path& t, int64_t code) { return folder(t) / "full" / (code_text(code) + ".png"); }

// ---------------------------------------------------------------- Index
void Index::set_root(const fs::path& r) {
    if (r == root_) return;
    root_ = r;
    codes_.clear();
    listed_at_ = -1e18;
}

bool Index::refresh(double now, bool force) {
    if (!force && now >= listed_at_ && now - listed_at_ < kRescanSec) return false;
    listed_at_ = now;
    codes_.clear();
    if (root_.empty()) return true;
    std::error_code ec;
    const fs::path dir = folder(root_);
    if (!fs::is_directory(dir, ec)) return true;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".png") continue;
        const std::string stem = p.stem().string();
        if (!digits_only(stem)) continue;
        codes_[std::stoll(stem)] = p;
    }
    Manifest m;
    if (!codes_.empty() && load_manifest(manifest_file(root_), m, nullptr)) {
        for (const capture::ManifestEntry& e : m.entries)
            if (e.state == "failed" || e.state == "crashed") codes_.erase(e.code);
    }
    return true;
}

fs::path Index::thumb(int64_t code, bool manager) const {
    if (manager) return {};  // no manager pictures: the managers' bodies are not drawn on a player
    const auto it = codes_.find(code);
    return it == codes_.end() ? fs::path() : it->second;
}

std::vector<int64_t> Index::missing(const std::vector<int64_t>& codes) const {
    std::vector<int64_t> out;
    for (int64_t c : codes)
        if (!codes_.count(c)) out.push_back(c);
    return out;
}

std::vector<int64_t> Index::codes() const {
    std::vector<int64_t> out;
    for (const auto& kv : codes_) out.push_back(kv.first);
    return out;
}

namespace {
Index g_index;
bool g_index_dirty = true;
double steady_now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
}  // namespace

void configure(const fs::path& turbo_output) {
    if (turbo_output != g_index.root()) {
        g_index.set_root(turbo_output);
        g_index_dirty = true;
    }
}

fs::path thumb(int64_t code, bool manager) {
    if (manager) return {};
    g_index.refresh(steady_now(), g_index_dirty);
    g_index_dirty = false;
    return g_index.thumb(code, false);
}

void invalidate() { g_index_dirty = true; }

// ---------------------------------------------------------------- manifest
const capture::ManifestEntry* Manifest::entry(int64_t code) const {
    for (const capture::ManifestEntry& e : entries)
        if (e.code == code) return &e;
    return nullptr;
}

size_t Manifest::count(const std::string& state) const {
    size_t n = 0;
    for (const capture::ManifestEntry& e : entries)
        if (e.state == state) ++n;
    return n;
}

bool read_manifest(const std::string& text, Manifest& out, std::string* err) {
    out = Manifest{};
    int32_t player = 0;
    int camera = -1;
    int64_t original = -1;
    if (!capture::Sequence::parse_manifest(text, out.entries, err, &player, &camera, &original)) return false;
    out.player = player;
    out.camera = camera;
    out.original = original;
    try {
        const json j = json::parse(text);
        out.team = j.value("team", -1);
        out.finished = j.value("finished", false);
        out.restored = j.value("restored", false);
        out.updated = j.value("updated", std::string());
    } catch (const std::exception& e) {
        if (err) *err = std::string("the manifest has a field of the wrong type: ") + e.what();
        return false;
    }
    return true;
}

bool load_manifest(const fs::path& file, Manifest& out, std::string* err) {
    std::string text;
    if (!read_text(file, text)) {
        if (err) *err = "no manifest";
        return false;
    }
    return read_manifest(text, out, err);
}

bool write_run_info(const fs::path& t, const RunInfo& r, std::string* err) {
    json j = json::object();
    j["career"] = r.career;
    j["player"] = r.player;
    j["camera"] = r.camera;
    j["source"] = r.source;
    j["started"] = r.started;
    return write_file_atomic(run_file(t), j.dump(1) + "\n", err);
}

bool read_run_info(const fs::path& t, RunInfo& r, std::string* err) {
    r = RunInfo{};
    std::string text;
    if (!read_text(run_file(t), text)) {
        if (err) *err = "no run record";
        return false;
    }
    try {
        const json j = json::parse(text);
        r.career = j.value("career", std::string());
        r.player = j.value("player", 0);
        r.camera = j.value("camera", static_cast<int>(capture::kCameraBody));
        r.source = j.value("source", std::string());
        r.started = j.value("started", std::string());
    } catch (const std::exception& e) {
        if (err) *err = std::string("the run record is not readable: ") + e.what();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- plan
namespace {
const looks::data::LookValue* census_value(int64_t code) {
    const looks::data::LookField* f = looks::data::find_look_field(looks::data::LookSubject::Player, "bodytypecode");
    if (!f || code < INT32_MIN || code > INT32_MAX) return nullptr;
    return looks::data::find_look_value(*f, static_cast<int32_t>(code));
}
}  // namespace

int code_gender(const bodytype::Catalog& cat, int64_t code) {
    if (const bodytype::Entry* e = cat.find(code))
        if (e->gender == 0 || e->gender == 1) return e->gender;
    if (const looks::data::LookValue* v = census_value(code)) {
        if (v->male > 0 && v->female == 0) return 0;
        if (v->female > 0 && v->male == 0) return 1;
    }
    return -1;
}

std::string code_label(const bodytype::Catalog& cat, int64_t code) {
    if (const looks::data::LookValue* v = census_value(code))
        if (v->label && *v->label && v->src != looks::data::LookSrc::StyleN) return v->label;
    return cat.name(code);
}

Plan plan_steps(const bodytype::Catalog& cat, const PlanOptions& o) {
    Plan p;
    std::vector<int64_t> generic, specific;
    for (int64_t c : cat.codes()) (cat.kind(c) == bodytype::Kind::Generic ? generic : specific).push_back(c);
    auto take = [&](int64_t c) {
        if (c < o.field_min || c > o.field_max) {
            p.skipped.push_back({c, "outside the database's range"});
            return false;
        }
        if (manager_body(c) && !o.include_manager_set) {
            p.skipped.push_back({c, "a manager body"});
            return false;
        }
        const int g = code_gender(cat, c);
        if (g >= 0 && g != o.gender) {
            p.skipped.push_back({c, g == 1 ? "a women's body" : "a men's body"});
            return false;
        }
        if (!o.allow_risky && bodytype::risky_pairing(cat, c, o.head_class)) {
            p.skipped.push_back({c, "a player-specific body on a head it may not fit"});
            return false;
        }
        if (o.max_steps > 0 && p.steps.size() >= o.max_steps) {
            p.skipped.push_back({c, "over the limit of this run"});
            return false;
        }
        p.steps.push_back({c, code_label(cat, c)});
        return true;
    };
    for (int64_t c : generic)
        if (take(c)) ++p.generic;
    for (int64_t c : specific) take(c);
    return p;
}

// ---------------------------------------------------------------- PNG
namespace {

uint32_t crc_table_entry(uint32_t n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    return c;
}
uint32_t crc32_png(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) t[i] = crc_table_entry(i);
        return t;
    }();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    while (n > 0) {
        size_t k = std::min<size_t>(n, 5552);
        n -= k;
        while (k--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int n = 0;
    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
    void put(uint32_t v, int bits) {  // LSB first
        acc |= v << n;
        n += bits;
        while (n >= 8) {
            out.push_back(static_cast<uint8_t>(acc & 0xFF));
            acc >>= 8;
            n -= 8;
        }
    }
    void huff(uint32_t code, int len) {  // Huffman codes go most significant bit first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1u) << (len - 1 - i);
        put(r, len);
    }
    void flush() {
        if (n > 0) out.push_back(static_cast<uint8_t>(acc & 0xFF));
        acc = 0;
        n = 0;
    }
};

void lit_len_symbol(BitWriter& w, int s) {
    if (s <= 143) w.huff(0x30 + s, 8);
    else if (s <= 255) w.huff(0x190 + (s - 144), 9);
    else if (s <= 279) w.huff(s - 256, 7);
    else w.huff(0xC0 + (s - 280), 8);
}

constexpr int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                               6145, 8193, 12289, 16385, 24577};
constexpr int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void put_match(BitWriter& w, int len, int dist) {
    int li = 28;
    while (li > 0 && kLenBase[li] > len) --li;
    lit_len_symbol(w, 257 + li);
    if (kLenExtra[li]) w.put(static_cast<uint32_t>(len - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (di > 0 && kDistBase[di] > dist) --di;
    w.huff(static_cast<uint32_t>(di), 5);
    if (kDistExtra[di]) w.put(static_cast<uint32_t>(dist - kDistBase[di]), kDistExtra[di]);
}

// zlib stream: one fixed-Huffman block, LZ77 with hash chains (window 32 KiB, a few candidates per position)
std::vector<uint8_t> zlib_compress(const std::vector<uint8_t>& d) {
    std::vector<uint8_t> out;
    out.reserve(d.size() / 4 + 64);
    out.push_back(0x78);
    out.push_back(0x01);
    BitWriter w(out);
    w.put(1, 1);  // BFINAL
    w.put(1, 2);  // BTYPE 01: fixed Huffman
    constexpr int kWin = 32768, kMask = kWin - 1, kHashBits = 15, kMaxChain = 6, kMaxLen = 258;
    std::vector<int32_t> head(size_t(1) << kHashBits, -1), prev(kWin, -1);
    const size_t n = d.size();
    auto hash3 = [&](size_t i) {
        const uint32_t v = (uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8) | d[i + 2];
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    auto insert = [&](size_t i) {
        if (i + 3 > n) return;
        const uint32_t h = hash3(i);
        prev[i & kMask] = head[h];
        head[h] = static_cast<int32_t>(i);
    };
    size_t i = 0;
    while (i < n) {
        int best = 0, best_dist = 0;
        if (i + 3 <= n) {
            const uint32_t h = hash3(i);
            int32_t cand = head[h];
            const size_t limit = std::min<size_t>(kMaxLen, n - i);
            for (int chain = 0; cand >= 0 && chain < kMaxChain; ++chain) {
                const size_t c = static_cast<size_t>(cand);
                if (c >= i || i - c > static_cast<size_t>(kWin)) break;
                size_t l = 0;
                while (l < limit && d[c + l] == d[i + l]) ++l;
                if (static_cast<int>(l) > best) {
                    best = static_cast<int>(l);
                    best_dist = static_cast<int>(i - c);
                    if (l == limit) break;
                }
                const int32_t next = prev[c & kMask];
                if (next >= cand) break;  // a slot reused by a newer position: the chain ends here
                cand = next;
            }
        }
        if (best >= 3) {
            put_match(w, best, best_dist);
            for (int k = 0; k < best; ++k) insert(i + static_cast<size_t>(k));
            i += static_cast<size_t>(best);
        } else {
            lit_len_symbol(w, d[i]);
            insert(i);
            ++i;
        }
    }
    lit_len_symbol(w, 256);
    w.flush();
    const uint32_t a = adler32(d.data(), d.size());
    out.push_back(static_cast<uint8_t>(a >> 24));
    out.push_back(static_cast<uint8_t>(a >> 16));
    out.push_back(static_cast<uint8_t>(a >> 8));
    out.push_back(static_cast<uint8_t>(a));
    return out;
}

void put_u32be(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(static_cast<uint8_t>(v >> 24));
    o.push_back(static_cast<uint8_t>(v >> 16));
    o.push_back(static_cast<uint8_t>(v >> 8));
    o.push_back(static_cast<uint8_t>(v));
}

void put_chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
    put_u32be(o, static_cast<uint32_t>(data.size()));
    const size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    put_u32be(o, crc32_png(o.data() + start, o.size() - start));
}

}  // namespace

std::vector<uint8_t> encode_png(const Rgba& img) {
    if (img.empty()) return {};
    const size_t stride = size_t(img.w) * 4;
    std::vector<uint8_t> raw;
    raw.reserve((stride + 1) * size_t(img.h));
    for (int y = 0; y < img.h; ++y) {
        const uint8_t* row = img.at(0, y);
        raw.push_back(1);  // filter Sub
        for (size_t x = 0; x < stride; ++x) raw.push_back(static_cast<uint8_t>(row[x] - (x >= 4 ? row[x - 4] : 0)));
    }
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put_u32be(ihdr, static_cast<uint32_t>(img.w));
    put_u32be(ihdr, static_cast<uint32_t>(img.h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit, RGBA, deflate, adaptive filters, no interlace
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", zlib_compress(raw));
    put_chunk(png, "IEND", {});
    return png;
}

bool write_file_atomic(const fs::path& p, const std::string& bytes, std::string* err) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.u8string();
            return false;
        }
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if (!f) {
            if (err) *err = "writing " + tmp.u8string() + " failed";
            f.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, p, ec);  // libstdc++ on Windows: MoveFileExW(MOVEFILE_REPLACE_EXISTING)
    if (ec) {
        std::error_code ec2;
        fs::remove(p, ec2);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec2);
            if (err) *err = "rename to " + p.u8string() + " failed: " + ec.message();
            return false;
        }
    }
    return true;
}

bool save_pictures(const fs::path& t, int64_t code, const Rgba& img, std::string* file, std::string* err) {
    if (img.empty()) {
        if (err) *err = "the picture is empty";
        return false;
    }
    const Rgba full = (img.w > kFullMax || img.h > kFullMax) ? fit_image(img, kFullMax) : img;
    const Rgba grid = fit_image(img, kGridWidth);
    const std::vector<uint8_t> a = encode_png(full), b = encode_png(grid);
    if (a.empty() || b.empty()) {
        if (err) *err = "the picture could not be encoded";
        return false;
    }
    if (!write_file_atomic(full_file(t, code), std::string(a.begin(), a.end()), err)) return false;
    if (!write_file_atomic(grid_file(t, code), std::string(b.begin(), b.end()), err)) return false;
    if (file) *file = code_text(code) + ".png";
    return true;
}

bool append_log(const fs::path& t, const std::string& stamp, const std::string& line) {
    std::error_code ec;
    fs::create_directories(folder(t), ec);
    std::ofstream f(log_file(t), std::ios::binary | std::ios::app);
    if (!f) return false;
    f << (stamp.empty() ? std::string() : stamp + " ") << line << "\n";
    f.flush();
    const bool ok = static_cast<bool>(f);
    f.close();
    return ok;
}

// ---------------------------------------------------------------- source
std::string SourceChoice::text() const {
    if (kind == SourceKind::None) return "none";
    std::string t = "camera " + std::to_string(camera);
    if (mode >= 0) t += " mode " + std::to_string(mode);
    if (extra >= 0) t += " extra " + std::to_string(extra);
    return t;
}

namespace {
bool source_from_json(const json& j, SourceChoice& out, std::string* err) {
    out = SourceChoice{};
    const std::string k = j.value("source", std::string("camera"));
    if (k == "camera") out.kind = SourceKind::Camera;
    else if (k == "none") out.kind = SourceKind::None;
    else {
        if (err) *err = "unknown picture source '" + k + "' (camera or none)";
        return false;
    }
    if (out.kind == SourceKind::Camera) {
        out.camera = j.value("camera", kCameraBodyFull);
        out.mode = j.value("mode", -1);
        out.extra = j.value("extra", -1);
        if (out.camera < kCameraBodyFull || (out.camera == -1 && out.mode < 0)) {
            if (err) *err = "a camera source needs a camera index, -2 (the full-body camera) or a mode";
            return false;
        }
        if (out.camera >= static_cast<int>(capture::cameras().size())) {
            if (err) *err = "camera " + std::to_string(out.camera) + " is not one of this build's cameras";
            return false;
        }
    }
    return true;
}
}  // namespace

bool parse_source(const std::string& text, SourceChoice& out, std::string* err) {
    try {
        const json j = json::parse(text);
        if (!j.is_object()) {
            if (err) *err = "the source is not an object";
            return false;
        }
        return source_from_json(j, out, err);
    } catch (const std::exception& e) {
        if (err) *err = std::string("the source is not readable: ") + e.what();
        return false;
    }
}

bool read_source_file(const fs::path& t, SourceChoice& out, std::string* err) {
    std::string text;
    if (!read_text(folder(t) / "source.json", text)) {
        if (err) *err = "no source file";
        return false;
    }
    return parse_source(text, out, err);
}

SourceAdapter::SourceAdapter(SourceChoice choice, capture::CaptureService* game) : choice_(choice), game_(game) {}

bool SourceAdapter::usable(std::string* why) const {
    if (choice_.kind == SourceKind::None) {
        if (why) *why = "no full-body picture source chosen (the head-and-shoulders picture shows no body type): nothing to make";
        return false;
    }
    if (!game_) {
        if (why) *why = "the game's pictures are not available here";
        return false;
    }
    return true;
}

capture::Status SourceAdapter::status() {
    std::string why;
    if (!usable(&why)) {
        capture::Status st;
        st.reason = why;
        return st;
    }
    return game_->status();
}

bool SourceAdapter::request(const capture::Request& r, std::string* err) {
    if (!usable(err)) return false;
    capture::Request q = r;
    if (choice_.camera != -1) q.camera = choice_.camera;  // kCameraBodyFull, or an index
    if (choice_.mode >= 0) q.mode_override = choice_.mode;
    if (choice_.extra >= 0) q.extra_override = choice_.extra;
    q.use_template = false;  // never the learned hub-avatar descriptor
    return game_->request(q, err);
}

bool SourceAdapter::poll(capture::Result& out) { return choice_.kind == SourceKind::Camera && game_ && game_->poll(out); }

void SourceAdapter::cancel() {
    if (game_) game_->cancel();
}

// ---------------------------------------------------------------- request
bool parse_request(const std::string& text, StartRequest& out, std::string* err) {
    out = StartRequest{};
    try {
        const json j = json::parse(text);
        if (!j.is_object()) {
            if (err) *err = "the request is not an object";
            return false;
        }
        out.player = j.value("player", 0);
        out.source_given = j.contains("source");
        if (out.source_given && !source_from_json(j, out.source, err)) return false;
        const int64_t mx = j.value("max", static_cast<int64_t>(0));
        out.max_steps = mx > 0 ? static_cast<size_t>(mx) : 0;
        out.fresh = j.value("fresh", false);
    } catch (const std::exception& e) {
        if (err) *err = std::string("the request is not readable: ") + e.what();
        return false;
    }
    if (out.player < 0) {
        if (err) *err = "the request names no player";
        return false;
    }
    return true;
}

bool take_request(const fs::path& t, StartRequest& out, std::string* err) {
    const fs::path p = request_file(t);
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    std::string text;
    const bool read = read_text(p, text);
    fs::path taken = p;
    taken.replace_filename("make.taken.json");
    fs::remove(taken, ec);
    fs::rename(p, taken, ec);
    if (ec) fs::remove(p, ec);  // never run the same request twice
    if (!read) {
        if (err) *err = "the request could not be read";
        return false;
    }
    if (!parse_request(text, out, err)) {
        if (err && err->empty()) *err = "the request is not readable";
        return false;
    }
    return true;
}

std::string start_over_refusal(const Manifest& old, int64_t old_current) {
    if (old.restored) return "";
    if (old.original < 0) return "";  // nothing was ever written on him by that run (no original recorded)
    if (old_current >= 0 && old_current == old.original) return "";
    return "the last run (player " + std::to_string(old.player) + ") did not give him his own body type back (" +
           std::to_string(old.original) + "): continue that run, or put his own body type back first";
}

// ---------------------------------------------------------------- Generator
Generator::Generator(GeneratorEnv env) : env_(std::move(env)) {}

Generator::~Generator() {
    if (running()) --g_running;  // the run is dropped (Turbo closing): the manifest keeps "pending" for the next resume
}

bool generator_running() { return g_running.load() > 0; }

bool Generator::running() const { return seq_ && seq_->running(); }

std::string Generator::now_stamp() const { return env_.stamp ? env_.stamp() : std::string(); }

void Generator::note(const std::string& line) {
    append_log(env_.turbo_output, now_stamp(), line);
    if (env_.log) env_.log("body-type pictures: " + line);
}

bool Generator::same_career(std::string* err) const {
    const std::string now = env_.career ? env_.career() : std::string();
    if (!now.empty() && now == run_.career) return true;
    if (err) *err = "another career is loaded (the run belongs to " + run_.career + "): nothing written";
    return false;
}

capture::StepResult Generator::write_step(int64_t code, bool first, std::string* err) {
    if (first) {
        const capture::ManifestEntry* e = seq_ ? seq_->entry(code) : nullptr;
        note("code " + std::to_string(code) + (e && !e->label.empty() ? " (" + e->label + ")" : std::string()) + ", attempt " +
             std::to_string(e ? e->attempts : 0) + ": writing it on player " + std::to_string(run_.player) +
             ", then asking the game for his picture");
    }
    if (!same_career(err)) return capture::StepResult::Failed;
    std::string e;
    if (!env_.write_code(run_.player, code, &e)) {
        if (err) *err = e;
        return capture::StepResult::Failed;
    }
    return capture::StepResult::Done;
}

capture::StepResult Generator::restore_step(bool first, std::string* err) {
    const int64_t orig = seq_ ? seq_->original() : -1;
    if (first) note("giving player " + std::to_string(run_.player) + " his own body type back (" + std::to_string(orig) + ")");
    if (orig < 0) {
        if (err) *err = "his own body type is not known";
        return capture::StepResult::Failed;
    }
    if (!same_career(err)) return capture::StepResult::Failed;
    std::string e;
    if (!env_.write_code(run_.player, orig, &e)) {
        if (err) *err = e;
        return capture::StepResult::Failed;
    }
    return capture::StepResult::Done;
}

bool Generator::start(const bodytype::Catalog& cat, const Reference& ref, const StartRequest& rq, std::string* err) {
    auto refuse = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    if (running()) return refuse("a run is already going");
    if (generator_running()) return refuse("another run is already going");
    if (!env_.read_code || !env_.write_code || !env_.clock || !env_.career) return refuse("the game's database is not available here");
    if (ref.player <= 0) return refuse("pick a reference player first");
    if (ref.head_class != 0) return refuse("the reference player must have a real face (a scanned head): pick another player");
    const std::string career = env_.career();
    if (career.empty()) return refuse("the loaded career is not identified yet: open the career hub first");
    // the picture source: the request's, else source.json, else the full-body camera (kCameraBodyFull, proven 22:31)
    SourceChoice choice = rq.source;
    if (!rq.source_given) {
        std::string se;
        if (!read_source_file(env_.turbo_output, choice, &se)) choice = SourceChoice{};  // the full-body camera
    }
    auto adapter = std::make_unique<SourceAdapter>(choice, env_.svc);
    std::string why;
    if (!adapter->usable(&why)) return refuse(why);
    const capture::Status st = adapter->status();
    if (st.busy) return refuse("another picture is being drawn (" + st.busy_label + "): wait until it is done");
    if (env_.svc && env_.svc->status().busy) return refuse("another picture is being drawn: wait until it is done");
    int64_t current = -1;
    std::string e;
    if (!env_.read_code(ref.player, &current, &e) || current < 0) return refuse("his body type could not be read: " + e);
    const int camera = capture::kCameraBody;  // the Sequence's own camera; the adapter sends the chosen source's

    // an earlier run: continue it (same career, player and camera) or start over only when it gave his own code back
    Manifest old;
    std::string old_text;
    const bool have_old = read_text(manifest_file(env_.turbo_output), old_text) && read_manifest(old_text, old, nullptr);
    RunInfo old_run;
    const bool have_old_run = read_run_info(env_.turbo_output, old_run, nullptr);
    bool resume = false;
    if (have_old) {
        const bool same_place = old.player == ref.player && old.camera == camera &&
                                (!have_old_run || (old_run.career == career && (old_run.source.empty() || old_run.source == choice.text())));
        if (same_place && !rq.fresh) {
            resume = true;
        } else {
            int64_t old_current = -1;
            if (!have_old_run || old_run.career == career) {
                std::string e2;
                if (!env_.read_code(old.player, &old_current, &e2)) old_current = -1;
            }
            const std::string why = start_over_refusal(old, old_current);
            if (!why.empty()) return refuse(why);
            // keep the old record next to the new one
            std::error_code ec;
            std::string suffix = now_stamp();
            for (char& c : suffix)
                if (c == ' ' || c == ':') c = '-';
            if (suffix.empty()) suffix = std::to_string(static_cast<long long>(env_.clock()));
            fs::rename(manifest_file(env_.turbo_output), folder(env_.turbo_output) / ("manifest." + suffix + ".json"), ec);
            if (have_old_run) fs::rename(run_file(env_.turbo_output), folder(env_.turbo_output) / ("run." + suffix + ".json"), ec);
        }
    }

    PlanOptions po;
    po.gender = ref.gender;
    po.head_class = ref.head_class;
    po.allow_risky = true;  // the generator only (a real-face head is required above)
    po.max_steps = rq.max_steps;
    plan_ = plan_steps(cat, po);
    if (plan_.steps.empty()) return refuse("no body type to draw (is the body type list loaded?)");

    run_ = RunInfo{career, ref.player, camera, choice.text(), now_stamp()};
    capture::SequenceOptions so;
    so.player = ref.player;
    so.team = ref.team;
    so.camera = camera;
    so.original = current;
    capture::SequenceHooks h;
    h.write = [this](int64_t code, bool first, std::string* er) { return write_step(code, first, er); };
    h.restore = [this](bool first, std::string* er) { return restore_step(first, er); };
    h.save = [this](int64_t code, const capture::Result& r, std::string* file, std::string* er) {
        const bool ok = save_pictures(env_.turbo_output, code, r.image, file, er);
        invalidate();
        note("code " + std::to_string(code) + (ok ? ": picture kept (" + r.format + ")" : ": the picture could not be kept"));
        return ok;
    };
    h.write_manifest = [this](const std::string& text, std::string* er) { return write_file_atomic(manifest_file(env_.turbo_output), text, er); };
    h.clock = env_.clock;
    h.stamp = env_.stamp;
    source_ = std::move(adapter);
    seq_ = std::make_unique<capture::Sequence>(*source_, so, plan_.steps, h);
    if (resume && !seq_->resume(old_text, &e)) {
        seq_.reset();
        return refuse("the earlier run could not be continued: " + e);
    }
    if (!write_run_info(env_.turbo_output, run_, &e)) {
        seq_.reset();
        return refuse("the run could not be recorded: " + e);
    }
    note(std::string(resume ? "continuing" : "starting") + ": " + std::to_string(plan_.steps.size()) + " body types (" +
         std::to_string(plan_.generic) + " generic) on player " + std::to_string(ref.player) + (ref.name.empty() ? "" : " (" + ref.name + ")") +
         ", source " + choice.text() + ", his own body type " + std::to_string(seq_->original()) + ", " +
         std::to_string(plan_.skipped.size()) + " skipped; career " + career);
    if (!seq_->start(&e)) {
        seq_.reset();
        return refuse(e);
    }
    ++g_running;
    ended_noted_ = false;
    tick();
    return true;
}

void Generator::tick() {
    if (!seq_) return;
    const bool was = seq_->running();
    if (was) seq_->tick();
    if (was && !seq_->running()) {
        --g_running;
        invalidate();
    }
    if (!seq_->running() && !ended_noted_) {
        ended_noted_ = true;
        note("ended: " + seq_->message() + (seq_->restored() ? " (his own body type is back)" : " (HIS OWN BODY TYPE WAS NOT PUT BACK)"));
    }
}

void Generator::cancel() {
    if (!running()) return;
    seq_->cancel();
    tick();
}

bool Generator::put_back_leftover(std::string* err) {
    if (running()) {
        if (err) *err = "a run is going: stop it first";
        return false;
    }
    Manifest m;
    std::string text;
    if (!read_text(manifest_file(env_.turbo_output), text) || !read_manifest(text, m, nullptr)) return true;  // nothing recorded
    if (m.restored || m.original < 0) return true;
    RunInfo r;
    read_run_info(env_.turbo_output, r, nullptr);
    const std::string career = env_.career ? env_.career() : std::string();
    if (career.empty() || (!r.career.empty() && r.career != career)) {
        if (err) *err = "load the career that run was made in (" + (r.career.empty() ? std::string("unknown") : r.career) + ") first";
        return false;
    }
    std::string e;
    if (!env_.write_code || !env_.write_code(m.player, m.original, &e)) {
        if (err) *err = "his own body type could not be written: " + e;
        return false;
    }
    try {
        json j = json::parse(text);
        j["restored"] = true;
        j["updated"] = now_stamp();
        if (!write_file_atomic(manifest_file(env_.turbo_output), j.dump(1), &e)) {
            if (err) *err = e;
            return false;
        }
    } catch (const std::exception& ex) {
        if (err) *err = ex.what();
        return false;
    }
    note("player " + std::to_string(m.player) + " has his own body type back (" + std::to_string(m.original) + ") after an unfinished run");
    return true;
}

std::string Generator::status() const {
    if (!seq_) return last_.empty() ? std::string("No run yet.") : last_;
    const capture::Sequence& s = *seq_;
    std::string line = std::to_string(s.count("done")) + " of " + std::to_string(s.total()) + " pictures made";
    const size_t f = s.count("failed") + s.count("crashed");
    if (f) line += ", " + std::to_string(f) + " without a picture";
    if (s.running()) return "Making body-type pictures: " + line + ". " + s.message();
    return "Last run: " + line + ". " + s.message() + (s.restored() ? "" : " His own body type was NOT put back.");
}

}  // namespace turbo::bodytype_thumbs
