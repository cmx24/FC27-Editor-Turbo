#include "devops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

static const char* kHex = "0123456789ABCDEF";

std::string to_hex(const uint8_t* p, size_t n) {
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += kHex[p[i] >> 4];
        s += kHex[p[i] & 15];
    }
    return s;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_pattern(const std::string& text, std::vector<uint8_t>& bytes, std::vector<bool>& mask) {
    bytes.clear();
    mask.clear();
    std::string t;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) t += c;
    if (t.empty() || t.size() % 2) return false;
    for (size_t i = 0; i < t.size(); i += 2) {
        if (t[i] == '?' && t[i + 1] == '?') {
            bytes.push_back(0);
            mask.push_back(false);
            continue;
        }
        int a = hexval(t[i]), b = hexval(t[i + 1]);
        if (a < 0 || b < 0) return false;
        bytes.push_back(uint8_t(a * 16 + b));
        mask.push_back(true);
    }
    return true;
}

bool parse_hex_bytes(const std::string& text, std::vector<uint8_t>& out) {
    std::vector<bool> mask;
    if (!parse_pattern(text, out, mask)) return false;
    return std::all_of(mask.begin(), mask.end(), [](bool m) { return m; });
}

static bool get_addr(const json& j, const char* key, uint64_t& out) {
    if (!j.contains(key)) return false;
    const json& v = j[key];
    if (v.is_number_unsigned()) {
        out = v.get<uint64_t>();
        return true;
    }
    if (v.is_number_integer()) {
        long long x = v.get<long long>();
        if (x < 0) return false;
        out = uint64_t(x);
        return true;
    }
    if (!v.is_string()) return false;
    std::string s = v.get<std::string>();
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    if (s.empty() || s.size() > 16) return false;
    uint64_t x = 0;
    for (char c : s) {
        int d = hexval(c);
        if (d < 0) return false;
        x = x * 16 + uint64_t(d);
    }
    out = x;
    return true;
}

static std::string hx(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

static int type_size(const std::string& t) {
    if (t == "i8") return 1;
    if (t == "i16") return 2;
    if (t == "i32" || t == "f32") return 4;
    if (t == "i64" || t == "f64") return 8;
    return 0;
}

// raw bits of `v` as type t
static bool encode_value(const std::string& t, const json& v, uint64_t& raw) {
    if (!v.is_number()) return false;
    if (t == "f32") {
        float f = v.get<float>();
        uint32_t u;
        std::memcpy(&u, &f, 4);
        raw = u;
    } else if (t == "f64") {
        double d = v.get<double>();
        std::memcpy(&raw, &d, 8);
    } else {
        long long x = v.is_number_integer() ? v.get<long long>() : static_cast<long long>(v.get<double>());
        int sz = type_size(t);
        raw = sz == 8 ? uint64_t(x) : (uint64_t(x) & ((uint64_t(1) << (sz * 8)) - 1));
    }
    return true;
}

static uint64_t load_raw(const uint8_t* p, int sz) {
    uint64_t v = 0;
    std::memcpy(&v, p, size_t(sz));
    return v;
}

static double as_number(const std::string& t, uint64_t raw) {
    if (t == "f32") {
        float f;
        uint32_t u = uint32_t(raw);
        std::memcpy(&f, &u, 4);
        return f;
    }
    if (t == "f64") {
        double d;
        std::memcpy(&d, &raw, 8);
        return d;
    }
    int sz = type_size(t);
    if (sz == 8) return double(int64_t(raw));
    uint64_t sign = uint64_t(1) << (sz * 8 - 1);
    int64_t v = (raw & sign) ? int64_t(raw | ~((sign << 1) - 1)) : int64_t(raw);
    return double(v);
}

static json value_json(const std::string& t, uint64_t raw) {
    if (t == "f32" || t == "f64") return as_number(t, raw);
    return static_cast<long long>(as_number(t, raw));
}

std::vector<DevRegion> DevService::pick_regions(const json& req) {
    std::vector<DevRegion> all = env_.regions ? env_.regions() : std::vector<DevRegion>();
    uint64_t from = 0, to = ~uint64_t(0);
    get_addr(req, "from", from);
    get_addr(req, "to", to);
    bool writable = req.value("writable", false);
    std::string kind = req.value("kind", std::string());
    std::vector<DevRegion> out;
    for (auto r : all) {
        if (r.end <= from || r.start >= to) continue;
        if (writable && !r.writable) continue;
        if (!kind.empty() && r.kind != kind) continue;
        r.start = std::max(r.start, from);
        r.end = std::min(r.end, to);
        out.push_back(r);
    }
    return out;
}

json DevService::run(const json& req) {
    json res;
    try {
        res = op(req, 0);
    } catch (const std::exception& e) {
        res = json::object();
        res["ok"] = false;
        res["error"] = std::string("bad request: ") + e.what();
    }
    if (req.contains("id")) res["id"] = req["id"];
    if (req.contains("op")) res["op"] = req["op"];
    return res;
}

json DevService::op(const json& req, int depth) {
    json r = json::object();
    r["ok"] = false;
    auto fail = [&](const std::string& e) {
        r["error"] = e;
        return r;
    };
    if (!req.is_object()) return fail("request is not an object");
    std::string name = req.value("op", std::string());
    if (name.empty() && req.contains("ms") && depth > 0) name = "sleep";
    if (name.empty()) return fail("no op");
    if (name != "ping" && name != "sleep" && name != "multi" && name != "seq" && name != "modules" && name != "key" && !env_.mem)
        return fail("no memory access");
    Memory& mem = *env_.mem;
    auto now = [&]() { return env_.clock ? env_.clock() : 0.0; };

    if (name == "ping") {
        r["ok"] = true;
        r["service"] = "Turbo dev service 2";
        return r;
    }
    if (name == "sleep") {
        int ms = std::clamp(req.value("ms", 0), 0, 10000);
        if (env_.sleep) env_.sleep(ms);
        r["ok"] = true;
        return r;
    }
    if (name == "read" || name == "bytes") {
        uint64_t a = 0;
        if (!get_addr(req, "addr", a)) return fail("addr missing");
        size_t n = size_t(std::clamp(req.value("len", 16), 1, 65536));
        std::vector<uint8_t> buf;
        if (!mem.read_block(a, n, buf)) return fail("unreadable at " + hx(a));
        r["ok"] = true;
        r["addr"] = hx(a);
        r["hex"] = to_hex(buf.data(), buf.size());
        return r;
    }
    if (name == "ptrs") {
        uint64_t a = 0;
        if (!get_addr(req, "addr", a)) return fail("addr missing");
        int n = std::clamp(req.value("count", 8), 1, 512);
        json arr = json::array();
        for (int i = 0; i < n; ++i) {
            uint64_t v = 0;
            if (!mem.rd(a + uint64_t(i) * 8, v)) {
                arr.push_back(nullptr);
                continue;
            }
            arr.push_back(hx(v));
        }
        r["ok"] = true;
        r["values"] = arr;
        return r;
    }
    if (name == "write") {
        uint64_t a = 0;
        if (!get_addr(req, "addr", a)) return fail("addr missing");
        std::vector<uint8_t> bytes;
        if (!parse_hex_bytes(req.value("hex", std::string()), bytes) || bytes.empty() || bytes.size() > 4096)
            return fail("hex: 1..4096 bytes of hex text");
        if (!mem.write(a, bytes.data(), bytes.size())) return fail("cannot write at " + hx(a));
        std::vector<uint8_t> back;
        if (!mem.read_block(a, bytes.size(), back) || back != bytes) return fail("written bytes read back different");
        r["ok"] = true;
        r["written"] = bytes.size();
        return r;
    }
    if (name == "region" || name == "regions") {
        auto regs = env_.regions ? env_.regions() : std::vector<DevRegion>();
        if (name == "region") {
            uint64_t a = 0;
            if (!get_addr(req, "addr", a)) return fail("addr missing");
            for (const auto& g : regs)
                if (a >= g.start && a < g.end) {
                    r["ok"] = true;
                    r["start"] = hx(g.start);
                    r["end"] = hx(g.end);
                    r["writable"] = g.writable;
                    r["kind"] = g.kind;
                    return r;
                }
            return fail("not in a readable region");
        }
        bool wr = req.value("writable", false);
        uint64_t min_size = req.value("min_size", 0ull);
        json arr = json::array();
        uint64_t total = 0;
        for (const auto& g : regs) {
            if (wr && !g.writable) continue;
            if (g.end - g.start < min_size) continue;
            total += g.end - g.start;
            if (arr.size() < 5000) arr.push_back({{"start", hx(g.start)}, {"size", hx(g.end - g.start)}, {"w", g.writable}, {"kind", g.kind}});
        }
        r["ok"] = true;
        r["count"] = arr.size();
        r["bytes"] = total;
        r["regions"] = arr;
        return r;
    }
    if (name == "modules") {
        if (!env_.modules) return fail("not available");
        r["ok"] = true;
        r["modules"] = env_.modules();
        return r;
    }
    if (name == "find") {
        std::vector<uint8_t> pat;
        std::vector<bool> mask;
        if (!parse_pattern(req.value("pattern", std::string()), pat, mask)) return fail("pattern: hex bytes, ?? = any");
        size_t max = size_t(std::clamp(req.value("max", 100), 1, 1000));
        size_t first = 0;
        while (first < mask.size() && !mask[first]) ++first;
        if (first == mask.size()) return fail("pattern has no fixed byte");
        json hits = json::array();
        double t0 = now();
        bool timed_out = false;
        uint64_t scanned = 0;
        const size_t chunk = 1 << 20;
        std::vector<uint8_t> buf;
        for (const auto& g : pick_regions(req)) {
            for (uint64_t a = g.start; a < g.end && hits.size() < max;) {
                size_t n = size_t(std::min<uint64_t>(chunk + pat.size() - 1, g.end - a));
                if (n < pat.size() || !mem.read_block(a, n, buf)) {
                    a += chunk;
                    continue;
                }
                scanned += n;
                const uint8_t* p = buf.data();
                for (size_t i = 0; i + pat.size() <= n && hits.size() < max;) {
                    const void* f = std::memchr(p + i + first, pat[first], n - pat.size() + 1 - i);
                    if (!f) break;
                    size_t at = size_t(static_cast<const uint8_t*>(f) - p) - first;
                    bool ok = true;
                    for (size_t k = 0; k < pat.size(); ++k)
                        if (mask[k] && p[at + k] != pat[k]) {
                            ok = false;
                            break;
                        }
                    if (ok) hits.push_back(hx(a + at));
                    i = at + 1;
                }
                a += chunk;
                if (now() - t0 > env_.time_budget) {
                    timed_out = true;
                    break;
                }
            }
            if (timed_out || hits.size() >= max) break;
        }
        r["ok"] = true;
        r["hits"] = hits;
        r["scanned"] = scanned;
        r["timed_out"] = timed_out;
        return r;
    }
    if (name == "scan") {
        std::string t = req.value("type", std::string("i32"));
        int sz = type_size(t);
        if (!sz) return fail("type: i8 i16 i32 i64 f32 f64");
        uint64_t raw = 0;
        if (!req.contains("value") || !encode_value(t, req["value"], raw)) return fail("value missing");
        int align = std::clamp(req.value("align", std::min(sz, 4)), 1, 8);
        double tol = req.value("tol", (t == "f32" || t == "f64") ? 0.0001 : 0.0);
        double target = as_number(t, raw);
        cand_.clear();
        cand_val_.clear();
        cand_type_ = t;
        double t0 = now();
        bool timed_out = false, truncated = false;
        uint64_t scanned = 0;
        const size_t chunk = 1 << 20;
        std::vector<uint8_t> buf;
        for (const auto& g : pick_regions(req)) {
            uint64_t start = (g.start + uint64_t(align) - 1) / uint64_t(align) * uint64_t(align);
            for (uint64_t a = start; a < g.end;) {
                size_t n = size_t(std::min<uint64_t>(chunk, g.end - a));
                if (n < size_t(sz) || !mem.read_block(a, n, buf)) {
                    a += chunk;
                    continue;
                }
                scanned += n;
                for (size_t i = 0; i + size_t(sz) <= n; i += size_t(align)) {
                    uint64_t v = load_raw(buf.data() + i, sz);
                    bool hit = (t == "f32" || t == "f64") ? std::fabs(as_number(t, v) - target) <= tol : v == raw;
                    if (hit) {
                        if (cand_.size() >= kMaxCandidates) {
                            truncated = true;
                            break;
                        }
                        cand_.push_back(a + i);
                        cand_val_.push_back(v);
                    }
                }
                if (truncated) break;
                a += chunk;
                if (now() - t0 > env_.time_budget) {
                    timed_out = true;
                    break;
                }
            }
            if (truncated || timed_out) break;
        }
        r["ok"] = true;
        r["count"] = cand_.size();
        r["scanned"] = scanned;
        r["timed_out"] = timed_out;
        r["truncated"] = truncated;
        json first = json::array();
        for (size_t i = 0; i < cand_.size() && i < 50; ++i) first.push_back(hx(cand_[i]));
        r["first"] = first;
        return r;
    }
    if (name == "refine" || name == "changed") {
        if (cand_type_.empty()) return fail("no scan yet");
        std::string cmp = name == "changed" ? "changed" : req.value("cmp", std::string(req.contains("value") ? "eq" : ""));
        if (cmp.empty()) return fail("value or cmp needed");
        int sz = type_size(cand_type_);
        uint64_t raw = 0;
        if (cmp == "eq" && !encode_value(cand_type_, req["value"], raw)) return fail("value");
        double tol = req.value("tol", (cand_type_ == "f32" || cand_type_ == "f64") ? 0.0001 : 0.0);
        std::vector<uint64_t> keep, keep_val;
        // read in windows: candidates are sorted by address
        std::vector<uint8_t> win;
        uint64_t wstart = 0, wend = 0;
        for (size_t i = 0; i < cand_.size(); ++i) {
            uint64_t a = cand_[i];
            uint64_t v = 0;
            bool have = false;
            if (a >= wstart && a + uint64_t(sz) <= wend) {
                v = load_raw(win.data() + (a - wstart), sz);
                have = true;
            } else {
                size_t want = 65536;
                if (mem.read_block(a, want, win)) {
                    wstart = a;
                    wend = a + want;
                    v = load_raw(win.data(), sz);
                    have = true;
                } else {
                    wstart = wend = 0;
                    uint64_t one = 0;
                    if (mem.read(a, &one, size_t(sz))) {
                        v = one;
                        have = true;
                    }
                }
            }
            if (!have) continue;
            double now_v = as_number(cand_type_, v), old_v = as_number(cand_type_, cand_val_[i]);
            bool k = false;
            if (cmp == "eq") k = (cand_type_[0] == 'f') ? std::fabs(now_v - as_number(cand_type_, raw)) <= tol : v == raw;
            else if (cmp == "changed") k = v != cand_val_[i];
            else if (cmp == "unchanged") k = v == cand_val_[i];
            else if (cmp == "increased") k = now_v > old_v;
            else if (cmp == "decreased") k = now_v < old_v;
            else return fail("cmp: changed unchanged increased decreased");
            if (k) {
                keep.push_back(a);
                keep_val.push_back(v);
            }
        }
        cand_.swap(keep);
        cand_val_.swap(keep_val);
        r["ok"] = true;
        r["count"] = cand_.size();
        json first = json::array();
        for (size_t i = 0; i < cand_.size() && i < 50; ++i) first.push_back(hx(cand_[i]));
        r["first"] = first;
        return r;
    }
    if (name == "list") {
        size_t max = size_t(std::clamp(req.value("max", 100), 1, 1000));
        json arr = json::array();
        int sz = type_size(cand_type_);
        for (size_t i = 0; i < cand_.size() && i < max; ++i) {
            uint64_t v = 0;
            json e = {{"addr", hx(cand_[i])}};
            if (sz && mem.read(cand_[i], &v, size_t(sz))) e["value"] = value_json(cand_type_, v);
            arr.push_back(e);
        }
        r["ok"] = true;
        r["count"] = cand_.size();
        r["type"] = cand_type_;
        r["candidates"] = arr;
        return r;
    }
    if (name == "dump") {
        uint64_t a = 0;
        if (!get_addr(req, "addr", a)) return fail("addr missing");
        uint64_t len = req.value("len", 0ull);
        if (len == 0 || len > (1ull << 30)) return fail("len: 1 .. 1 GiB");
        std::string name = req.value("file", std::string());
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos)
            return fail("file: a plain file name (written to turbo_output)");
        std::ofstream f(env_.out_dir / name, std::ios::binary | std::ios::trunc);
        if (!f) return fail("cannot write " + name);
        std::vector<uint8_t> buf, zero(1 << 16, 0);
        uint64_t unreadable = 0;
        for (uint64_t off = 0; off < len;) {
            size_t n = size_t(std::min<uint64_t>(1 << 16, len - off));
            if (mem.read_block(a + off, n, buf)) {
                f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(n));
            } else {
                f.write(reinterpret_cast<const char*>(zero.data()), std::streamsize(n));  // unreadable: zeros
                unreadable += n;
            }
            off += n;
        }
        r["ok"] = bool(f);
        r["file"] = name;
        r["bytes"] = len;
        r["unreadable"] = unreadable;
        return r;
    }
    if (name == "key") {
        if (!env_.key) return fail("not available");
        int vk = req.value("vk", 0);
        int ms = std::clamp(req.value("ms", 120), 10, 2000);
        if (vk <= 0 || vk > 255) return fail("vk: 1..255");
        bool sent = false;
        std::string e = env_.key(vk, ms, &sent);
        if (!e.empty()) return fail(e);
        r["ok"] = true;
        r["game_in_front"] = sent;
        r["ms"] = ms;
        return r;
    }
    if (name == "multi" || name == "seq") {
        if (depth > 0) return fail("multi / seq cannot be nested");
        const char* key = name == "multi" ? "ops" : "steps";
        if (!req.contains(key) || !req[key].is_array() || req[key].size() > 200) return fail(std::string(key) + ": a list of at most 200");
        json out = json::array();
        bool all = true;
        for (const auto& sub : req[key]) {
            json x = op(sub, depth + 1);
            all = all && x.value("ok", false);
            out.push_back(x);
        }
        r["ok"] = all;
        r["results"] = out;
        return r;
    }
    return fail("unknown op " + name);
}

}  // namespace turbo
