#include "teamnames.h"

#include <cctype>
#include <ctime>
#include <string>
#include <fstream>
#include <sstream>
#include <system_error>

namespace turbo {

namespace fs = std::filesystem;

TeamNameKeys team_name_keys(int64_t teamid) {
    std::string id = std::to_string(teamid);
    return {"TeamName_" + id, "TeamName_Abbr3_" + id, "TeamName_Abbr10_" + id, "TeamName_Abbr15_" + id};
}

fs::path team_names_file(const fs::path& le_root) { return le_root / "extensions" / "global" / "custom_team_names.csv"; }
fs::path team_names_backup_dir(const fs::path& le_root) { return le_root / "turbo_output" / "team_name_backups"; }

std::string clean_team_name(const std::string& s, size_t max_bytes) {
    std::string out;
    for (char c : s) {
        if (c == ';' || c == '\r' || c == '\n' || c == '\0') continue;
        out += c;
    }
    // trim
    size_t b = out.find_first_not_of(" \t"), e = out.find_last_not_of(" \t");
    out = b == std::string::npos ? std::string() : out.substr(b, e - b + 1);
    if (out.size() > max_bytes) {
        out.resize(max_bytes);
        // do not cut a UTF-8 sequence in half: drop an incomplete last sequence
        size_t lead = out.size();
        while (lead > 0 && (static_cast<unsigned char>(out[lead - 1]) & 0xC0) == 0x80) --lead;
        if (lead > 0) {
            unsigned char c = static_cast<unsigned char>(out[lead - 1]);
            size_t need = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
            if (out.size() - (lead - 1) < need) out.resize(lead - 1);
        }
    }
    return out;
}

bool TeamNamesCsv::load(const fs::path& file, std::string* err) {
    rows_.clear();
    loaded_ = false;
    crlf_ = false;
    bom_ = false;
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        loaded_ = true;  // nothing yet: Live Editor accepts a new file
        return true;
    }
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot read " + file.string();
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        bom_ = true;
        text.erase(0, 3);
    }
    if (text.find("\r\n") != std::string::npos) crlf_ = true;
    size_t start = 0;
    bool first = true;
    while (start < text.size()) {
        size_t e = text.find('\n', start);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(start, e - start);
        start = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Row r;
        r.raw = line;
        size_t semi = line.find(';');
        bool header = first && line.size() >= 3 && (line.compare(0, 3, "key") == 0 || line.compare(0, 3, "Key") == 0);
        first = false;
        if (!header && semi != std::string::npos && semi > 0 && line[0] != '#') {
            r.key = line.substr(0, semi);
            r.value = line.substr(semi + 1);
        }
        rows_.push_back(std::move(r));
    }
    loaded_ = true;
    return true;
}

bool TeamNamesCsv::has(const std::string& key) const {
    for (const auto& r : rows_)
        if (!r.key.empty() && r.key == key) return true;
    return false;
}

std::string TeamNamesCsv::get(const std::string& key) const {
    for (const auto& r : rows_)
        if (!r.key.empty() && r.key == key) return r.value;
    return {};
}

void TeamNamesCsv::set(const std::string& key, const std::string& value) {
    if (key.empty()) return;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].key != key) continue;
        if (value.empty()) {
            rows_.erase(rows_.begin() + static_cast<long>(i));
        } else {
            rows_[i].value = value;
            rows_[i].raw = key + ";" + value;
        }
        return;
    }
    if (value.empty()) return;
    Row r;
    r.key = key;
    r.value = value;
    r.raw = key + ";" + value;
    rows_.push_back(std::move(r));
}

std::string TeamNamesCsv::text() const {
    const char* nl = crlf_ ? "\r\n" : "\n";
    std::string out;
    if (bom_) out += "\xEF\xBB\xBF";
    bool has_header = !rows_.empty() && rows_[0].key.empty() && rows_[0].raw.compare(0, 3, "key") == 0;
    if (!has_header) out += std::string("key;value") + nl;
    for (const auto& r : rows_) {
        out += r.raw;
        out += nl;
    }
    return out;
}

bool TeamNamesCsv::save(const fs::path& file, const fs::path& backup_dir, std::string* err, fs::path* backup) const {
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
        fs::create_directories(backup_dir, ec);
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
        fs::path dst = backup_dir / (file.stem().string() + "_" + stamp + file.extension().string());
        for (int i = 2; fs::exists(dst, ec) && i < 1000; ++i)
            dst = backup_dir / (file.stem().string() + "_" + stamp + "_" + std::to_string(i) + file.extension().string());
        fs::copy_file(file, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (err) *err = "backup of " + file.filename().string() + " failed: " + ec.message();
            return false;
        }
        if (backup) *backup = dst;
    }
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        std::string data = text();
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        ec.clear();
        fs::rename(tmp, file, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + file.string();
            return false;
        }
    }
    return true;
}

void TeamNamesCsv::team_names(int64_t teamid, std::string& full, std::string& a3, std::string& a10, std::string& a15) const {
    TeamNameKeys k = team_name_keys(teamid);
    full = get(k.full);
    a3 = get(k.abbr3);
    a10 = get(k.abbr10);
    a15 = get(k.abbr15);
}

void TeamNamesCsv::set_team_names(int64_t teamid, const std::string& full, const std::string& a3, const std::string& a10,
                                  const std::string& a15) {
    TeamNameKeys k = team_name_keys(teamid);
    set(k.full, clean_team_name(full));
    set(k.abbr3, clean_team_name(a3, 3));
    set(k.abbr10, clean_team_name(a10, 10));
    set(k.abbr15, clean_team_name(a15, 15));
}

// ---------------------------------------------------------------- readable club names
bool is_unresolved_team_name(const std::string& s) {
    auto ident = [](const std::string& t, size_t from) {
        if (from >= t.size()) return false;
        for (size_t i = from; i < t.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (!std::isalnum(c) && c != '_') return false;
        }
        return true;
    };
    if (!s.empty() && s[0] == '*') return ident(s, 1) && s.find('_') != std::string::npos;
    return s.compare(0, 9, "TeamName_") == 0 && ident(s, 9);
}

std::unordered_map<int64_t, std::string> readable_team_names(const TeamNamesCsv& csv) {
    // rank: 0 full name, 1 Abbr15, 2 Abbr10, 3 Abbr3 (lower wins); the prefixes are tried longest first
    static const std::pair<const char*, int> kinds[] = {
        {"TeamName_Abbr15_", 1}, {"TeamName_Abbr10_", 2}, {"TeamName_Abbr3_", 3}, {"TeamName_", 0}};
    std::unordered_map<int64_t, std::pair<int, std::string>> best;
    for (const auto& r : csv.rows()) {
        if (r.key.empty() || r.value.empty() || is_unresolved_team_name(r.value)) continue;
        for (const auto& k : kinds) {
            const size_t n = std::char_traits<char>::length(k.first);
            if (r.key.compare(0, n, k.first) != 0) continue;
            const std::string id = r.key.substr(n);
            if (id.empty() || id.size() > 12 || id.find_first_not_of("0123456789") != std::string::npos) break;
            const int64_t tid = std::stoll(id);
            auto it = best.find(tid);
            if (it == best.end() || k.second < it->second.first) best[tid] = {k.second, r.value};
            break;
        }
    }
    std::unordered_map<int64_t, std::string> out;
    for (auto& kv : best) out.emplace(kv.first, std::move(kv.second.second));
    return out;
}

std::shared_ptr<const std::unordered_map<int64_t, std::string>> load_readable_team_names(const fs::path& le_root) {
    auto out = std::make_shared<std::unordered_map<int64_t, std::string>>();
    TeamNamesCsv csv;
    if (csv.load(team_names_file(le_root))) *out = readable_team_names(csv);
    return out;
}

}  // namespace turbo
