// FC 27 LE Turbo GUI - body type catalogue (see bodytype_catalog.h)
#include "core/bodytype_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "core/field_labels.h"
#include "nlohmann/json.hpp"

namespace turbo::bodytype {

namespace fs = std::filesystem;
using nlohmann::json;

static bool read_text(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// an integer JSON value (a number with no fraction), else false
static bool as_int(const json& j, int64_t& v) {
    if (j.is_number_integer()) { v = j.get<int64_t>(); return true; }
    if (j.is_number_float()) {
        double d = j.get<double>();
        if (d > -1e15 && d < 1e15 && d == static_cast<double>(static_cast<int64_t>(d))) { v = static_cast<int64_t>(d); return true; }
    }
    return false;
}

// [min, max] or {"min":..,"max":..}; other shapes leave the range unknown
static void read_range(const json& j, const char* key, int& lo, int& hi) {
    auto it = j.find(key);
    if (it == j.end()) return;
    int64_t a = 0, b = 0;
    if (it->is_array() && it->size() == 2 && as_int((*it)[0], a) && as_int((*it)[1], b)) {
        lo = static_cast<int>(std::min(a, b));
        hi = static_cast<int>(std::max(a, b));
    } else if (it->is_object() && it->contains("min") && it->contains("max") && as_int((*it)["min"], a) && as_int((*it)["max"], b)) {
        lo = static_cast<int>(std::min(a, b));
        hi = static_cast<int>(std::max(a, b));
    }
    if (lo < 0 || hi < 0 || hi > 1000) lo = hi = 0;
}

// a 0/1 code, or an array of them that all agree; everything else (mixed, other values) is -1
static int read_flag(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end()) return -1;
    int64_t v = 0;
    if (as_int(*it, v)) return v == 0 || v == 1 ? static_cast<int>(v) : -1;
    if (it->is_array() && !it->empty()) {
        std::set<int64_t> seen;
        for (const auto& e : *it) {
            if (!as_int(e, v)) return -1;
            seen.insert(v);
        }
        if (seen.size() == 1 && (*seen.begin() == 0 || *seen.begin() == 1)) return static_cast<int>(*seen.begin());
    }
    return -1;
}

static bool read_entry(const json& j, int64_t forced_code, bool use_forced, Entry& e) {
    if (!j.is_object()) return false;
    int64_t code = forced_code;
    if (!use_forced) {
        auto it = j.find("code");
        if (it == j.end() || !as_int(*it, code)) return false;
    }
    if (code < 0 || code > 100000) return false;
    e = Entry{};
    e.code = code;
    e.probed = true;
    int64_t n = 0;
    for (const char* k : {"players", "player_count", "count"}) {
        auto it = j.find(k);
        if (it != j.end() && as_int(*it, n)) { e.players = std::max<int64_t>(0, n); break; }
    }
    read_range(j, "height", e.height_min, e.height_max);
    read_range(j, "weight", e.weight_min, e.weight_max);
    e.headclass = read_flag(j, "headclasscode");
    e.gender = read_flag(j, "gender");
    auto ex = j.find("examples");
    if (ex != j.end() && ex->is_array()) {
        for (const auto& s : *ex) {
            if (!s.is_string() || e.examples.size() >= kMaxExamples) continue;
            std::string t = s.get<std::string>();
            if (!t.empty()) e.examples.push_back(t);
        }
    }
    return true;
}

void Catalog::clear() {
    probed_.clear();
    names_.clear();
    probe_loaded_ = false;
    skipped_ = 0;
    probe_error_.clear();
}

bool Catalog::load_probe(const fs::path& file, std::string* err) {
    probed_.clear();
    probe_loaded_ = false;
    skipped_ = 0;
    probe_error_.clear();
    auto fail = [&](const std::string& why) {
        probe_error_ = why;
        if (err) *err = why;
        return false;
    };
    std::string text;
    if (!read_text(file, text)) return fail("probe file not found: " + file.string());
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded()) return fail("probe file is not valid JSON");
    if (!root.is_object()) return fail("probe file: the top level is not an object");
    auto bt = root.find("bodytypes");
    if (bt == root.end() || !(bt->is_array() || bt->is_object())) return fail("probe file: no \"bodytypes\" list");
    std::map<int64_t, Entry> got;
    if (bt->is_array()) {
        for (const auto& j : *bt) {
            Entry e;
            if (read_entry(j, 0, false, e)) got[e.code] = e;
            else ++skipped_;
        }
    } else {
        for (auto it = bt->begin(); it != bt->end(); ++it) {
            char* endp = nullptr;
            const long long code = std::strtoll(it.key().c_str(), &endp, 10);
            Entry e;
            if (!it.key().empty() && endp && *endp == 0 && read_entry(it.value(), code, true, e)) got[e.code] = e;
            else ++skipped_;
        }
    }
    probed_ = std::move(got);
    probe_loaded_ = true;
    return true;
}

// every "bodytype_<n>" key with a string value, at any depth (the file's layout is Live Editor's, not ours)
static void scan_names(const json& j, std::map<int64_t, std::string>& out, int depth) {
    if (depth > 4) return;
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string& k = it.key();
            if (it.value().is_string() && k.rfind("bodytype_", 0) == 0 && k.size() > 9) {
                char* endp = nullptr;
                const long long code = std::strtoll(k.c_str() + 9, &endp, 10);
                const std::string v = it.value().get<std::string>();
                if (endp && *endp == 0 && code >= 0 && !v.empty()) out[code] = v;
            } else if (it.value().is_object() || it.value().is_array()) {
                scan_names(it.value(), out, depth + 1);
            }
        }
    } else if (j.is_array()) {
        for (const auto& e : j) scan_names(e, out, depth + 1);
    }
}

size_t Catalog::load_names(const fs::path& localize_json) {
    names_.clear();
    std::string text;
    if (!read_text(localize_json, text)) return 0;
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded()) return 0;
    scan_names(root, names_, 0);
    return names_.size();
}

void Catalog::load_for_root(const fs::path& le_root) {
    load_probe(le_root / "turbo_output" / "bodytypes_fc27.json");
    load_names(le_root / "loc" / "eng_us" / "localize.json");
}

const Entry* Catalog::find(int64_t code) const {
    auto it = probed_.find(code);
    return it == probed_.end() ? nullptr : &it->second;
}

bool Catalog::named(int64_t code) const {
    if (names_.count(code)) return true;
    const labels::NamedCodes* t = labels::named_codes("bodytypecode");
    return t && labels::find_name(*t, code) != nullptr;
}

std::string Catalog::name(int64_t code) const {
    auto it = names_.find(code);
    if (it != names_.end()) return it->second;
    if (const labels::NamedCodes* t = labels::named_codes("bodytypecode")) {
        if (const char* n = labels::find_name(*t, code)) return n;
    }
    char b[48];
    std::snprintf(b, sizeof(b), "Specific body #%lld", static_cast<long long>(code));
    return b;
}

std::string Catalog::examples_text(int64_t code) const {
    const Entry* e = find(code);
    std::string out;
    if (e)
        for (const auto& x : e->examples) out += (out.empty() ? "" : ", ") + x;
    return out;
}

std::string Catalog::describe(int64_t code) const {
    std::string s = name(code);
    const std::string ex = examples_text(code);
    if (kind(code) == Kind::Specific && !ex.empty()) s += " (examples: " + ex + ")";
    return s;
}

int64_t Catalog::players(int64_t code) const {
    const Entry* e = find(code);
    return e ? e->players : 0;
}

std::vector<int64_t> Catalog::codes() const {
    std::set<int64_t> s;
    for (const auto& kv : names_) s.insert(kv.first);
    if (const labels::NamedCodes* t = labels::named_codes("bodytypecode"))
        for (const auto& e : *t) s.insert(e.first);
    for (const auto& kv : probed_) s.insert(kv.first);
    return std::vector<int64_t>(s.begin(), s.end());
}

static bool overlaps(int lo, int hi, int qlo, int qhi) {
    if (qlo > 0 && hi < qlo) return false;
    if (qhi > 0 && lo > qhi) return false;
    return true;
}

std::vector<int64_t> Catalog::filter(const Filter& f) const {
    std::vector<int64_t> out;
    const std::string q = lower(f.text);
    const bool hf = f.height_min > 0 || f.height_max > 0, wf = f.weight_min > 0 || f.weight_max > 0;
    for (int64_t c : codes()) {
        const Kind k = kind(c);
        if (f.group == Group::Generic && k != Kind::Generic) continue;
        if (f.group == Group::Specific && k != Kind::Specific) continue;
        const Entry* e = find(c);
        if (f.used_only && (!e || e->players <= 0)) continue;
        if (hf && (!e || !e->has_height() || !overlaps(e->height_min, e->height_max, f.height_min, f.height_max))) continue;
        if (wf && (!e || !e->has_weight() || !overlaps(e->weight_min, e->weight_max, f.weight_min, f.weight_max))) continue;
        if (!q.empty()) {
            const bool hit = lower(name(c)).find(q) != std::string::npos || std::to_string(c) == q ||
                             lower(examples_text(c)).find(q) != std::string::npos;
            if (!hit) continue;
        }
        out.push_back(c);
    }
    return out;
}

bool risky_pairing(const Catalog& c, int64_t code, int head_class) {
    return c.kind(code) == Kind::Specific && head_class != 0;
}

}  // namespace turbo::bodytype
