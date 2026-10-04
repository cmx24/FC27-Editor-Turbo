// FC 27 LE Turbo GUI - live team names (see teamname_override.h)
#include "teamname_override.h"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#include "nlohmann/json.hpp"
#include "teamnames.h"

namespace turbo {
namespace tnames {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* const kJsonKey[kKinds] = {"name", "abbr15", "abbr10", "abbr3"};

std::string stamp_now() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &tmv);
    return b;
}

inline char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Consumes `lit` (lower case) at p, ignoring case; p moves only on a full match. Stops at the key's NUL.
inline bool eat_ci(const char*& p, const char* lit) noexcept {
    const char* q = p;
    for (; *lit; ++lit, ++q)
        if (lower_ascii(*q) != *lit) return false;
    p = q;
    return true;
}

// ---- UTF-8
size_t decode(const std::string& s, size_t i, uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t n = 0;
    if (c < 0x80) {
        cp = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        n = 2;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        n = 3;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        n = 4;
    } else {
        return 0;  // not a lead byte
    }
    if (i + n > s.size()) return 0;
    for (size_t k = 1; k < n; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3F);
    }
    return n;
}

void encode(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// pairs where the upper case is the even code point and the lower case the odd one (or the other way round)
inline bool odd(uint32_t cp) { return (cp & 1u) != 0; }

uint32_t upper_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp < 0x80) return cp;
    // Latin-1
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
    if (cp == 0xFF) return 0x178;
    // Latin Extended-A
    if (cp >= 0x100 && cp <= 0x12F) return odd(cp) ? cp - 1 : cp;
    if (cp == 0x131) return 'I';
    if (cp >= 0x132 && cp <= 0x137) return odd(cp) ? cp - 1 : cp;
    if (cp >= 0x139 && cp <= 0x148) return odd(cp) ? cp : cp - 1;
    if (cp >= 0x14A && cp <= 0x177) return odd(cp) ? cp - 1 : cp;
    if (cp >= 0x179 && cp <= 0x17E) return odd(cp) ? cp : cp - 1;
    if (cp == 0x17F) return 'S';
    // Latin Extended-B: the Romanian and other pairs at 0x200..0x233
    if ((cp >= 0x200 && cp <= 0x21F) || (cp >= 0x222 && cp <= 0x233)) return odd(cp) ? cp - 1 : cp;
    // Greek
    if (cp == 0x3AC) return 0x386;
    if (cp >= 0x3AD && cp <= 0x3AF) return cp - 0x25;
    if (cp >= 0x3B1 && cp <= 0x3C1) return cp - 0x20;
    if (cp == 0x3C2) return 0x3A3;
    if (cp >= 0x3C3 && cp <= 0x3CB) return cp - 0x20;
    if (cp == 0x3CC) return 0x38C;
    if (cp == 0x3CD || cp == 0x3CE) return cp - 0x3F;
    // Cyrillic
    if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;
    if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
    if ((cp >= 0x460 && cp <= 0x481) || (cp >= 0x48A && cp <= 0x4BF) || (cp >= 0x4D0 && cp <= 0x52F)) return odd(cp) ? cp - 1 : cp;
    if (cp >= 0x4C1 && cp <= 0x4CE) return odd(cp) ? cp : cp - 1;
    if (cp == 0x4CF) return 0x4C0;
    // Latin Extended Additional (Vietnamese and others)
    if ((cp >= 0x1E00 && cp <= 0x1E95) || (cp >= 0x1EA0 && cp <= 0x1EFF)) return odd(cp) ? cp - 1 : cp;
    return cp;
}

bool entry_of(const json& o, Entry& e) {
    if (!o.is_object()) return false;
    auto id = o.find("teamid");
    if (id == o.end() || !id->is_number_integer()) return false;
    e.teamid = id->get<int64_t>();
    if (e.teamid <= 0 || e.teamid > INT32_MAX) return false;
    for (int k = 0; k < kKinds; ++k) {
        auto v = o.find(kJsonKey[k]);
        if (v == o.end() || v->is_null()) continue;
        if (!v->is_string()) return false;
        e.text[k] = clean_value(v->get<std::string>(), static_cast<Kind>(k));
    }
    if (auto w = o.find("when"); w != o.end() && w->is_string()) e.when = w->get<std::string>();
    return !e.empty();
}

}  // namespace

const char* kind_key_part(Kind k) {
    switch (k) {
        case Abbr15: return "Abbr15_";
        case Abbr10: return "Abbr10_";
        case Abbr3: return "Abbr3_";
        default: return "";
    }
}

std::string key_of(int64_t teamid, Kind k) { return std::string("TeamName_") + kind_key_part(k) + std::to_string(teamid); }

std::string clean_value(const std::string& s, Kind k) {
    return k == Full ? clean_team_name(s, kMaxLen[Full]) : clean_team_abbr(s, kMaxLen[k]);
}

bool Entry::empty() const {
    for (const auto& s : text)
        if (!s.empty()) return false;
    return true;
}

// ---------------------------------------------------------------- store
void Store::upsert(const Entry& e) {
    if (e.empty()) {
        forget(e.teamid);
        return;
    }
    auto it = std::lower_bound(entries.begin(), entries.end(), e.teamid, [](const Entry& a, int64_t id) { return a.teamid < id; });
    if (it != entries.end() && it->teamid == e.teamid) *it = e;
    else entries.insert(it, e);
}

bool Store::forget(int64_t teamid) {
    auto it = std::lower_bound(entries.begin(), entries.end(), teamid, [](const Entry& a, int64_t id) { return a.teamid < id; });
    if (it == entries.end() || it->teamid != teamid) return false;
    entries.erase(it);
    return true;
}

const Entry* Store::find(int64_t teamid) const {
    auto it = std::lower_bound(entries.begin(), entries.end(), teamid, [](const Entry& a, int64_t id) { return a.teamid < id; });
    return it != entries.end() && it->teamid == teamid ? &*it : nullptr;
}

fs::path store_path(const fs::path& le_root) { return le_root / "turbo_output" / "team_names.json"; }

std::string store_json(const Store& s) {
    json list = json::array();
    for (const Entry& e : s.entries) {
        json o = json::object();
        o["teamid"] = e.teamid;
        for (int k = 0; k < kKinds; ++k) o[kJsonKey[k]] = e.text[k];
        o["when"] = e.when;
        list.push_back(o);
    }
    json j = json::object();
    j["turbo_team_names"] = 1;
    j["teams"] = list;
    // error_handler::replace: a name read from game memory with broken UTF-8 must not throw
    return j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool parse_store_json(const std::string& text, Store& out, std::string* err) {
    out = Store{};
    size_t bad = 0;
    try {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        auto ver = j.find("turbo_team_names");
        if (ver == j.end() || !ver->is_number_integer() || ver->get<int64_t>() != 1) {
            if (err) *err = "not a Turbo team names file (no \"turbo_team_names\": 1)";
            return false;
        }
        auto list = j.find("teams");
        if (list != j.end() && !list->is_null()) {
            if (!list->is_array()) {
                if (err) *err = "\"teams\" is not a list";
                return false;
            }
            for (const json& o : *list) {
                Entry e;
                if (!entry_of(o, e)) {
                    ++bad;
                    continue;
                }
                out.upsert(e);  // one per club: the last one wins
            }
        }
    } catch (const std::exception& e) {
        out = Store{};
        if (err) *err = std::string("cannot read the file: ") + e.what();
        return false;
    }
    if (bad && err) *err = std::to_string(bad) + (bad == 1 ? " bad entry dropped" : " bad entries dropped");
    return true;
}

bool Store::load(const fs::path& file, std::string* err) {
    entries.clear();
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + file.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    f.close();
    std::string why;
    Store read;
    if (parse_store_json(ss.str(), read, &why)) {
        entries = std::move(read.entries);
        if (!why.empty() && err) *err = file.filename().string() + ": " + why;
        return true;
    }
    // a bad file is set aside (never overwritten by the next save) and the store starts empty
    const std::string stamp = stamp_now();
    fs::path aside = file;
    aside += ".bad-" + stamp;
    for (int i = 2; fs::exists(aside, ec) && i < 1000; ++i) {
        aside = file;
        aside += ".bad-" + stamp + "_" + std::to_string(i);
    }
    ec.clear();
    fs::rename(file, aside, ec);
    if (ec) {
        if (err) *err = file.filename().string() + ": " + why + "; cannot set it aside: " + ec.message();
        return false;
    }
    if (err) *err = file.filename().string() + ": " + why + "; set aside as " + aside.filename().string();
    return true;
}

bool Store::save(const fs::path& file, std::string* err) const {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << store_json(*this);
        f.close();  // a failed flush must not replace the good file
        if (!f) {
            fs::remove(tmp, ec);
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    ec.clear();
    fs::rename(tmp, file, ec);
    if (ec) {
        // Windows: a rename over a file another program holds open can fail; replace in two steps
        ec.clear();
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

// ---------------------------------------------------------------- keys
bool match_key(const char* key, KeyMatch& out) noexcept {
    if (!key) return false;
    const char* p = key;
    const char c0 = lower_ascii(*p);
    if (c0 != 't' && c0 != 'i') return false;  // the fast way out for every other string the game asks for
    if (c0 == 'i' && !eat_ci(p, "iwl_")) return false;
    if (!eat_ci(p, "teamname_")) return false;
    Kind k = Full;
    if (lower_ascii(*p) == 'a') {
        if (eat_ci(p, "abbr15_")) k = Abbr15;
        else if (eat_ci(p, "abbr10_")) k = Abbr10;
        else if (eat_ci(p, "abbr3_")) k = Abbr3;
        else return false;
    }
    if (*p == '0') return false;  // "%d" never writes a leading zero
    int64_t id = 0;
    int n = 0;
    while (*p >= '0' && *p <= '9') {
        if (++n > 10) return false;
        id = id * 10 + (*p - '0');
        ++p;
    }
    if (n == 0 || *p != '\0' || id > INT32_MAX) return false;
    out.teamid = id;
    out.kind = k;
    return true;
}

std::string utf8_upper_basic(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = 0;
        const size_t n = decode(s, i, cp);
        if (n == 0) {  // not UTF-8: the byte as it is
            out += s[i];
            ++i;
            continue;
        }
        encode(upper_cp(cp), out);
        i += n;
    }
    return out;
}

// ---------------------------------------------------------------- snapshot
const char* Snapshot::lookup(const KeyMatch& m, bool upper) const noexcept {
    size_t lo = 0, hi = items.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (items[mid].teamid < m.teamid) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= items.size() || items[lo].teamid != m.teamid) return nullptr;
    const int k = static_cast<int>(m.kind);
    if (k < 0 || k >= kKinds) return nullptr;
    const std::string& s = upper ? items[lo].upper[k] : items[lo].text[k];
    return s.empty() ? nullptr : s.c_str();
}

const char* Snapshot::lookup(const char* key, int mode) const noexcept {
    KeyMatch m;
    if (!match_key(key, m)) return nullptr;
    return lookup(m, mode == 0);
}

Snapshot build_snapshot(const Store& s, UpperFn upper) {
    Snapshot snap;
    snap.items.reserve(s.entries.size());
    for (const Entry& e : s.entries) {
        if (e.teamid <= 0 || e.empty()) continue;
        Snapshot::Item it;
        it.teamid = e.teamid;
        for (int k = 0; k < kKinds; ++k) {
            it.text[k] = e.text[k];
            if (!e.text[k].empty()) it.upper[k] = upper ? upper(e.text[k]) : e.text[k];
        }
        snap.items.push_back(std::move(it));
    }
    std::sort(snap.items.begin(), snap.items.end(), [](const Snapshot::Item& a, const Snapshot::Item& b) { return a.teamid < b.teamid; });
    return snap;
}

const Snapshot* SnapshotSlot::publish(Snapshot s) {
    std::lock_guard<std::mutex> lock(m_);
    all_.push_back(std::make_unique<const Snapshot>(std::move(s)));
    const Snapshot* p = all_.back().get();
    cur_.store(p, std::memory_order_release);
    return p;
}

size_t SnapshotSlot::kept() const {
    std::lock_guard<std::mutex> lock(m_);
    return all_.size();
}

StatsSnapshot snapshot(const Stats& s) {
    StatsSnapshot o;
    o.calls = s.calls.load(std::memory_order_relaxed);
    o.team_keys = s.team_keys.load(std::memory_order_relaxed);
    o.given = s.given.load(std::memory_order_relaxed);
    return o;
}

fs::path hook_off_path(const fs::path& out_dir) { return out_dir / "team_names_hook_off.txt"; }

}  // namespace tnames
}  // namespace turbo
