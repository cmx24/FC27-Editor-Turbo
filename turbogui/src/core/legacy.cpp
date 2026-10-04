#include "legacy.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace turbo {

namespace fs = std::filesystem;

constexpr size_t kMaxWanted = 3000;

namespace legacy_path {
std::string player_miniface(int64_t id) { return "data/ui/imgAssets/heads/p" + std::to_string(id) + ".dds"; }
std::string staff_miniface(int64_t id) { return "data/ui/imgAssets/heads_staff/heads_staff_" + std::to_string(id) + ".dds"; }
std::string youth_face(int64_t id) { return "data/ui/imgAssets/youthheads/p" + std::to_string(id) + ".dds"; }
std::string tattoo_preview(int64_t id) { return "data/ui/imgAssets/tattoo/item_" + std::to_string(id) + "_0.dds"; }
std::string crest(int64_t teamid, int size, const char* folder) {
    std::string dir = size > 0 ? "crest" + std::to_string(size) + "x" + std::to_string(size) : std::string("crest");
    return "data/ui/imgAssets/" + dir + "/" + folder + "/l" + std::to_string(teamid) + ".dds";
}
bool valid(const std::string& p) {
    if (p.size() < 6 || p.size() > 200 || p.compare(0, 5, "data/") != 0) return false;
    if (p.find("..") != std::string::npos) return false;
    for (char c : p)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '.' || c == '-')) return false;
    return true;
}
}  // namespace legacy_path

std::vector<CrestVariant> crest_variants(int64_t teamid) {
    static const int sizes[] = {0, 16, 32, 50, 512, 1024};
    static const char* folders[] = {"light", "dark", "custom"};
    std::vector<CrestVariant> out;
    for (int sz : sizes)
        for (const char* f : folders) out.push_back({sz, f, legacy_path::crest(teamid, sz, f)});
    return out;
}

std::string crest_main_path(int64_t teamid) { return legacy_path::crest(teamid, 0, "light"); }

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static fs::path under(const fs::path& base, const std::string& rel) {
    fs::path p = base;
    size_t start = 0;
    while (start < rel.size()) {
        size_t e = rel.find('/', start);
        if (e == std::string::npos) e = rel.size();
        if (e > start) p /= rel.substr(start, e - start);
        start = e + 1;
    }
    return p;
}

static bool write_atomic(const fs::path& p, const std::string& data, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        // Windows: rename over an existing file can fail while it is open elsewhere; replace in two steps
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + p.string();
            return false;
        }
    }
    return true;
}

LegacyImages::LegacyImages(fs::path le_root) : root_(std::move(le_root)) {
    gen_ = static_cast<uint64_t>(std::time(nullptr));
}

fs::path LegacyImages::cache_dir() const { return root_ / "turbo_output" / "cache" / "legacy"; }
fs::path LegacyImages::mods_dir() const { return root_ / "mods" / "legacy"; }
fs::path LegacyImages::backup_dir() const { return root_ / "turbo_output" / "miniface_backups"; }
fs::path LegacyImages::crest_backup_dir() const { return root_ / "turbo_output" / "crest_backups"; }
fs::path LegacyImages::backup_dir_for(const std::string& path) const {
    return path.compare(0, 22, "data/ui/imgAssets/crest") == 0 ? crest_backup_dir() : backup_dir();
}

fs::path LegacyImages::custom_file(const std::string& path) const {
    if (!legacy_path::valid(path)) return {};
    fs::path p = under(mods_dir(), path);
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return p;
    // other tools write P123.DDS / p123.DDS: match the name in any letter case
    fs::path dir = p.parent_path();
    if (!fs::is_directory(dir, ec)) return {};
    std::string want = lower(p.filename().string());
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && lower(it->path().filename().string()) == want) return it->path();
    }
    return {};
}

void LegacyImages::want(const std::string& path, bool front) {
    if (!legacy_path::valid(path) || missing_.count(path)) return;
    if (want_set_.count(path)) {
        if (!front) return;
        auto it = std::find(want_.begin(), want_.end(), path);
        if (it == want_.begin()) return;
        if (it != want_.end()) want_.erase(it);
    } else {
        want_set_.insert(path);
    }
    if (front) want_.insert(want_.begin(), path);
    else want_.push_back(path);
    while (want_.size() > kMaxWanted) {
        want_set_.erase(want_.back());
        want_.pop_back();
    }
    dirty_ = true;
}

LegacyImages::State LegacyImages::locate(const std::string& path, fs::path* file, bool custom_first) {
    if (!legacy_path::valid(path)) return State::Invalid;
    std::error_code ec;
    if (custom_first) {
        fs::path c = custom_file(path);
        if (!c.empty()) {
            if (file) *file = c;
            return State::Custom;
        }
    }
    fs::path g = under(cache_dir(), path);
    if (fs::is_regular_file(g, ec) && fs::file_size(g, ec) > 0) {
        if (file) *file = g;
        if (want_set_.erase(path)) {
            want_.erase(std::remove(want_.begin(), want_.end(), path), want_.end());
            dirty_ = true;
        }
        return State::Game;
    }
    if (missing_.count(path)) return State::Missing;
    want(path, true);
    return State::Waiting;
}

bool LegacyImages::flush() {
    std::string text = "#gen " + std::to_string(gen_) + "\n";
    for (const auto& p : want_) text += p + "\n";
    dirty_ = false;
    return write_atomic(cache_dir() / "want.txt", text, nullptr);
}

void LegacyImages::read_missing() {
    std::error_code ec;
    fs::path mp = cache_dir() / "missing.txt";
    uintmax_t sz = fs::exists(mp, ec) ? fs::file_size(mp, ec) : 0;
    if (ec) sz = 0;
    if (sz != missing_size_) {
        missing_size_ = sz;
        missing_.clear();
        std::ifstream f(mp, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (legacy_path::valid(line)) missing_.insert(line);
        }
        size_t before = want_.size();
        want_.erase(std::remove_if(want_.begin(), want_.end(), [&](const std::string& p) { return missing_.count(p) > 0; }),
                    want_.end());
        if (want_.size() != before) {
            want_set_.clear();
            want_set_.insert(want_.begin(), want_.end());
            dirty_ = true;
        }
    }
    std::ifstream sf(cache_dir() / "status.txt", std::ios::binary);
    if (sf) {
        std::stringstream ss;
        ss << sf.rdbuf();
        status_ = ss.str();
    }
}

void LegacyImages::tick(double now) {
    if (now >= next_read_) {
        next_read_ = now + 1.0;
        read_missing();
        // drop what has arrived meanwhile (pictures asked for but no longer on screen)
        std::error_code ec;
        size_t before = want_.size();
        want_.erase(std::remove_if(want_.begin(), want_.end(),
                                   [&](const std::string& p) { return fs::is_regular_file(under(cache_dir(), p), ec); }),
                    want_.end());
        if (want_.size() != before) {
            want_set_.clear();
            want_set_.insert(want_.begin(), want_.end());
            dirty_ = true;
        }
    }
    if (dirty_ && (last_write_ < 0.0 || now - last_write_ >= 0.5)) {
        last_write_ = now;
        flush();
    }
}

size_t LegacyImages::cached_files() const {
    size_t n = 0;
    std::error_code ec;
    if (!fs::is_directory(cache_dir(), ec)) return 0;
    for (fs::recursive_directory_iterator it(cache_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".dds") ++n;
    }
    return n;
}

bool LegacyImages::clear_cache(std::string* err) {
    std::error_code ec;
    fs::path d = cache_dir();
    if (fs::is_directory(d, ec)) {
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code rec;
            fs::remove_all(it->path(), rec);
            if (rec) {
                if (err) *err = "cannot delete " + it->path().string() + ": " + rec.message();
                return false;
            }
        }
    }
    missing_.clear();
    missing_size_ = 0;
    status_.clear();
    gen_ = std::max<uint64_t>(gen_ + 1, static_cast<uint64_t>(std::time(nullptr)));
    dirty_ = true;
    return flush();
}

bool LegacyImages::backup(const fs::path& f, const fs::path& dir, fs::path* out, std::string* err) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    std::string stem = f.stem().string(), ext = f.extension().string();
    fs::path dst = dir / (stem + "_" + stamp + ext);
    for (int i = 2; fs::exists(dst, ec) && i < 1000; ++i)
        dst = dir / (stem + "_" + stamp + "_" + std::to_string(i) + ext);
    fs::copy_file(f, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (err) *err = "backup of " + f.filename().string() + " failed: " + ec.message();
        return false;
    }
    if (out) *out = dst;
    return true;
}

bool LegacyImages::save_custom(const std::string& path, const std::vector<uint8_t>& bytes, std::string* err,
                               fs::path* backup_out) {
    if (!legacy_path::valid(path)) {
        if (err) *err = "invalid game file path";
        return false;
    }
    if (bytes.empty()) {
        if (err) *err = "nothing to write";
        return false;
    }
    fs::path target = under(mods_dir(), path);
    fs::path existing = custom_file(path);
    if (!existing.empty()) {
        if (!backup(existing, backup_dir_for(path), backup_out, err)) return false;
        // a differently-cased old file (P123.DDS) would shadow or duplicate the new one: remove it
        if (existing != target) {
            std::error_code ec;
            fs::remove(existing, ec);
        }
    }
    return write_atomic(target, std::string(bytes.begin(), bytes.end()), err);
}

bool LegacyImages::remove_custom(const std::string& path, std::string* err, fs::path* backup_out) {
    fs::path existing = custom_file(path);
    if (existing.empty()) {
        if (err) *err = "there is no custom file";
        return false;
    }
    while (!existing.empty()) {
        if (!backup(existing, backup_dir_for(path), backup_out, err)) return false;
        std::error_code ec;
        fs::remove(existing, ec);
        if (ec) {
            if (err) *err = "cannot delete " + existing.string() + ": " + ec.message();
            return false;
        }
        existing = custom_file(path);
    }
    return true;
}

}  // namespace turbo
