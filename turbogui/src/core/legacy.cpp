#include "legacy.h"

#include "image.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace turbo {

namespace fs = std::filesystem;

constexpr size_t kMaxWanted = 3000;

namespace legacy_path {
std::string player_miniface(int64_t id) { return "data/ui/imgAssets/heads/p" + std::to_string(id) + ".dds"; }
std::string staff_miniface(int64_t id) { return "data/ui/imgAssets/heads_staff/heads_staff_" + std::to_string(id) + ".dds"; }
std::string youth_face(int64_t id) { return "data/ui/imgAssets/youthheads/p" + std::to_string(id) + ".dds"; }
std::string legend_picture(int64_t id) { return "data/ui/imgAssets/cmLegendaryPlayers/legend_" + std::to_string(id) + ".dds"; }
std::string generic_manager_head(int64_t id) { return "data/ui/imgAssets/genericManagerHeads/gmh" + std::to_string(id) + ".dds"; }
std::vector<std::string> staff_pictures(int64_t managerid, int64_t headassetid) {
    std::vector<std::string> out;
    if (managerid > 0) out.push_back(staff_miniface(managerid));
    if (headassetid > 0 && headassetid != managerid) out.push_back(staff_miniface(headassetid));
    if (headassetid > 0) out.push_back(generic_manager_head(headassetid));
    return out;
}
std::string staff_miniface_for(int64_t managerid, int64_t headassetid) {
    return staff_miniface(managerid > 0 ? managerid : headassetid);
}
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
fs::path LegacyImages::kit_backup_dir() const { return root_ / "turbo_output" / "kit_backups"; }
fs::path LegacyImages::backup_dir_for(const std::string& path) const {
    // game editor configs (core/edit_unlock.h) keep their own backups
    if (path.compare(0, 12, "data/avatar/") == 0 || path.compare(0, 18, "data/gamesettings/") == 0)
        return root_ / "turbo_output" / "edit_unlock" / "backups";
    if (path.compare(0, 23, "data/ui/imgAssets/kits/") == 0) return kit_backup_dir();  // minikits (Teams > Kits)
    return path.compare(0, 23, "data/ui/imgAssets/crest") == 0 ? crest_backup_dir() : backup_dir();
}

fs::path LegacyImages::custom_file(const std::string& path) const {
    if (!legacy_path::valid(path)) return {};
    fs::path p = under(mods_dir(), path);
    // other tools write P123.DDS / p123.DDS: match the name in any letter case. 2.16 gui/perf: the folder's listing is kept
    // until its last-write time changes. 2.16 gui/perf-3: the exact name is answered from the listing too, and a stable
    // folder's time stamp is read once per tick: no file call per picture drawn per frame (it was 2)
    const fs::path dir = p.parent_path();
    const std::string want = lower(p.filename().string());
    std::lock_guard<std::mutex> lock(custom_dirs_m_);
    CustomDir& d = custom_dirs_[dir.string()];
    if (!(frame_ != 0 && d.listed && !d.recent && d.looked_frame == frame_)) {
        d.looked_frame = frame_;
        std::error_code ec;
        const auto mtime = fs::last_write_time(dir, ec);
        if (ec) {  // no such folder
            d.listed = true;
            d.recent = false;
            d.by_lower.clear();
            return {};
        }
        // a folder changed in the last 2 s is listed again every time (its time stamp may not move for a second change
        // within the file system clock's tick)
        d.recent = fs::file_time_type::clock::now() - mtime < std::chrono::seconds(2);
        if (!d.listed || d.mtime != mtime || d.recent) {
            d.listed = true;
            d.mtime = mtime;
            d.by_lower.clear();
            std::error_code le;
            for (fs::directory_iterator it(dir, le), end; !le && it != end; it.increment(le)) {
                std::error_code e2;
                if (it->is_regular_file(e2)) d.by_lower.emplace(lower(it->path().filename().string()), it->path());
            }
        }
    }
    auto it = d.by_lower.find(want);
    return it == d.by_lower.end() ? fs::path() : it->second;
}

fs::path LegacyImages::cache_file(const std::string& path) const {
    return legacy_path::valid(path) ? under(cache_dir(), path) : fs::path();
}

double LegacyImages::waiting_for(const std::string& path) const {
    auto it = asked_at_.find(path);
    if (it == asked_at_.end() || !want_set_.count(path)) return -1.0;
    return std::max(0.0, now_ - it->second);
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
        asked_at_[path] = now_;
        last_progress_ = now_;  // a new picture on screen: Lua is prompted again (even after it went quiet)
    }
    if (front) want_.insert(want_.begin(), path);
    else want_.push_back(path);
    while (want_.size() > kMaxWanted) {
        want_set_.erase(want_.back());
        want_.pop_back();
    }
    dirty_ = true;
}

LegacyImages::Ask LegacyImages::ask_background(const std::string& path) {
    if (!legacy_path::valid(path) || missing_.count(path) || want_set_.count(path) || bg_set_.count(path)) return Ask::Known;
    std::error_code ec;
    if (fs::is_regular_file(under(cache_dir(), path), ec)) return Ask::Known;
    if (bg_.size() >= bg_limit_) return Ask::Full;  // the caller keeps the path and asks again later (ui/preload.h)
    bg_.push_back(path);
    bg_set_.insert(path);
    dirty_ = true;
    return Ask::Accepted;
}

LegacyImages::State LegacyImages::peek(const std::string& path, fs::path* file) const {
    if (!legacy_path::valid(path)) return State::Invalid;
    std::error_code ec;
    fs::path g = under(cache_dir(), path);
    if (fs::is_regular_file(g, ec) && fs::file_size(g, ec) > 0) {
        if (file) *file = g;
        return State::Game;
    }
    return missing_.count(path) ? State::Missing : State::Waiting;
}

void LegacyImages::drop_background(const std::function<bool(const std::string&)>& gone) {
    size_t before = bg_.size();
    bg_.erase(std::remove_if(bg_.begin(), bg_.end(), [&](const std::string& p) {
                  if (!gone(p)) return false;
                  bg_set_.erase(p);
                  return true;
              }),
              bg_.end());
    if (bg_.size() != before) dirty_ = true;
}

void LegacyImages::drop_wanted(const std::function<bool(const std::string&)>& gone) {
    size_t before = want_.size();
    want_.erase(std::remove_if(want_.begin(), want_.end(), [&](const std::string& p) {
                    if (!gone(p)) return false;
                    want_set_.erase(p);
                    asked_at_.erase(p);
                    return true;
                }),
                want_.end());
    if (want_.size() != before) {
        dirty_ = true;
        last_progress_ = now_;  // Lua answered (delivered or refused)
    }
}

void LegacyImages::reload(const std::string& path) {
    if (!legacy_path::valid(path)) return;
    game_present_.erase(path);
    waiting_seen_.erase(path);
    std::error_code ec;
    fs::remove(cache_file(path), ec);  // an old custom file exported as the game's, or a broken copy
    // "not in the game" set aside: the lines missing.txt has for it now no longer count, a new one does
    auto ml = missing_lines_.find(path);
    if (ml != missing_lines_.end()) missing_set_aside_[path] = ml->second;
    missing_.erase(path);
    // Lua skips what it handled in this generation (exported or refused): a new one makes it look again
    gen_ = std::max<uint64_t>(gen_ + 1, static_cast<uint64_t>(std::time(nullptr)));
    if (want_set_.erase(path)) want_.erase(std::remove(want_.begin(), want_.end(), path), want_.end());
    asked_at_.erase(path);
    want(path, true);  // first in the list; the wait (and Lua's prompting) starts again
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
    // 2.16 gui/perf-3: a game picture seen in the last second of ticks is taken as there (Lua exports it once and never
    // rewrites it): no file call per picture drawn per frame (it was 2); looked at again once a second
    {
        auto gp = game_present_.find(path);
        if (gp != game_present_.end()) {
            if (frame_ != 0 && now_ >= gp->second && now_ - gp->second < 1.0) {
                if (file) *file = g;
                return State::Game;
            }
            game_present_.erase(gp);
        }
    }
    // 2.20: a picture found absent less than a second ago (waiting for its export, or one the game does not have): no file
    // call. tick() drops an asked picture from the list once it arrives; a new missing.txt, reload() and clear_cache() forget
    {
        auto ws = waiting_seen_.find(path);
        if (ws != waiting_seen_.end()) {
            if (frame_ != 0 && now_ >= ws->second && now_ - ws->second < 1.0) {
                if (missing_.count(path)) return State::Missing;
                if (want_set_.count(path)) {
                    want(path, true);
                    return State::Waiting;
                }
            }
            waiting_seen_.erase(ws);
        }
    }
    if (fs::is_regular_file(g, ec) && fs::file_size(g, ec) > 0) {
        if (file) *file = g;
        game_present_[path] = now_;
        if (want_set_.count(path)) drop_wanted([&](const std::string& p) { return p == path; });
        return State::Game;
    }
    if (frame_ != 0) waiting_seen_[path] = now_;
    if (missing_.count(path)) return State::Missing;
    want(path, true);
    return State::Waiting;
}

std::string LegacyImages::first_present(const std::vector<std::string>& paths, fs::path* file, State* st) {
    for (const std::string& p : paths) {
        const State s = locate(p, file, true);
        if (s == State::Custom || s == State::Game || s == State::Waiting) {
            if (st) *st = s;
            return p;
        }
    }
    if (st) *st = State::Missing;
    return std::string();
}

bool LegacyImages::flush() {
    std::string text = "#gen " + std::to_string(gen_) + "\n";
    for (const auto& p : want_) text += p + "\n";
    for (const auto& p : bg_)  // background loading: after everything on screen
        if (!want_set_.count(p)) text += p + "\n";
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
        game_present_.clear();
        waiting_seen_.clear();
        missing_lines_.clear();
        std::ifstream f(mp, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (legacy_path::valid(line)) ++missing_lines_[line];
        }
        missing_.clear();
        for (const auto& kv : missing_lines_) {
            auto aside = missing_set_aside_.find(kv.first);
            if (aside == missing_set_aside_.end() || kv.second > aside->second) missing_.insert(kv.first);
        }
        drop_wanted([&](const std::string& p) { return missing_.count(p) > 0; });
        drop_background([&](const std::string& p) { return missing_.count(p) > 0; });
    }
    std::ifstream sf(cache_dir() / "status.txt", std::ios::binary);
    if (sf) {
        std::stringstream ss;
        ss << sf.rdbuf();
        if (ss.str() != status_) last_progress_ = now_;  // Lua ran and wrote its counts
        status_ = ss.str();
    }
}

void LegacyImages::tick(double now) {
    if (now < now_) next_bg_check_ = now;  // the clock went back (a tick ahead of the frames, the tests do that): no old schedule
    now_ = now;
    if (now >= next_read_) {
        next_read_ = now + 1.0;
        read_missing();
        // drop what has arrived meanwhile (pictures asked for but no longer on screen)
        std::error_code ec;
        drop_wanted([&](const std::string& p) { return fs::is_regular_file(under(cache_dir(), p), ec); });
        for (auto it = asked_at_.begin(); it != asked_at_.end();)  // cut from the list (kMaxWanted): no longer waiting
            it = want_set_.count(it->first) ? std::next(it) : asked_at_.erase(it);
    }
    // background list: a slice every quarter second (thousands of paths), round robin. 2.16 gui/perf: 100 paths every 0.25 s
    // (the same 400 a second as before) so that no single frame looks at 400 files (this runs in the game's Present)
    if (!bg_.empty() && now >= next_bg_check_) {
        next_bg_check_ = now + 0.25;
        constexpr size_t kSlice = 100;
        std::error_code ec;
        if (bg_check_ >= bg_.size()) bg_check_ = 0;
        const size_t from = bg_check_, to = std::min(bg_.size(), from + kSlice);
        std::unordered_set<std::string> arrived;
        for (size_t i = from; i < to; ++i)
            if (fs::is_regular_file(under(cache_dir(), bg_[i]), ec)) arrived.insert(bg_[i]);
        bg_check_ = to - arrived.size();
        if (!arrived.empty()) drop_background([&](const std::string& p) { return arrived.count(p) > 0; });
    }
    if (dirty_ && (last_write_ < 0.0 || now - last_write_ >= 0.5)) {
        last_write_ = now;
        flush();
    }
}

size_t LegacyImages::cached_files() const { return count_cached_files(cache_dir()); }

size_t LegacyImages::count_cached_files(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".dds") ++n;
    }
    return n;
}

bool LegacyImages::clear_cache(std::string* err) {
    game_present_.clear();
    waiting_seen_.clear();
    {
        std::lock_guard<std::mutex> lock(custom_dirs_m_);
        custom_dirs_.clear();
    }
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
    missing_lines_.clear();
    missing_set_aside_.clear();
    missing_size_ = 0;
    status_.clear();
    gen_ = std::max<uint64_t>(gen_ + 1, static_cast<uint64_t>(std::time(nullptr)));
    last_progress_ = now_;  // everything is asked again
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
    {  // 2.16 gui/perf-3: Turbo's own write: no folder look kept from this tick
        std::lock_guard<std::mutex> lock(custom_dirs_m_);
        custom_dirs_.clear();
    }
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
    {  // 2.16 gui/perf-3: Turbo's own delete: no folder look kept from this tick
        std::lock_guard<std::mutex> lock(custom_dirs_m_);
        custom_dirs_.clear();
    }
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
        {  // the folder look of this tick no longer holds (else the deleted file would be found again)
            std::lock_guard<std::mutex> lock(custom_dirs_m_);
            custom_dirs_.clear();
        }
        existing = custom_file(path);
    }
    return true;
}

std::vector<std::string> LegacyImages::repair_dds_files() {
    std::vector<std::string> out;
    std::error_code ec;
    fs::path root = mods_dir() / "data" / "ui" / "imgAssets";
    if (!fs::is_directory(root, ec)) return out;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".dds") continue;
        const fs::path file = it->path();
        uint8_t head[32] = {};
        {
            std::ifstream f(file, std::ios::binary);
            if (!f.read(reinterpret_cast<char*>(head), sizeof(head))) continue;
        }
        const uint32_t claimed = uint32_t(head[28]) | uint32_t(head[29]) << 8 | uint32_t(head[30]) << 16 | uint32_t(head[31]) << 24;
        if (claimed <= 1) continue;  // the only case the repair handles: a count of several levels
        std::vector<uint8_t> bytes;
        {
            std::ifstream f(file, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        int was = 0, now_levels = 0;
        if (!repair_dds_mip_count(bytes, &was, &now_levels)) continue;
        std::string rel = fs::relative(file, mods_dir(), ec).generic_string();
        std::string err;
        if (!backup(file, backup_dir_for(rel), nullptr, &err)) {
            out.push_back("legacy repair: " + rel + ": not repaired, backup failed: " + err);
            continue;
        }
        if (!write_atomic(file, std::string(bytes.begin(), bytes.end()), &err)) {
            out.push_back("legacy repair: " + rel + ": writing failed: " + err);
            continue;
        }
        out.push_back("legacy repair: " + rel + ": mip count " + std::to_string(was) + " -> " + std::to_string(now_levels) +
                      " (the file holds " + std::to_string(now_levels) + " level" + (now_levels == 1 ? "" : "s") +
                      "; the game read past it while loading)");
    }
    return out;
}

}  // namespace turbo
