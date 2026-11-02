// FC 27 LE Turbo GUI - crest serve (see crest_serve.h)
#include "crest_serve.h"

#include <cstdio>

#include "devops.h"
#include "nlohmann/json.hpp"

namespace turbo {
namespace crest_serve {

using nlohmann::json;

namespace {

char lower_slash(char c) {
    if (c == '\\') return '/';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

// A relative legacy path Turbo may open under mods\legacy: no drive, no leading slash, no ".." segment
bool relative_ok(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        if (p.compare(start, end - start, "..") == 0) return false;
        start = end + 1;
    }
    return true;
}

}  // namespace

bool is_begin_load_async(const uint8_t* code, size_t n) {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    if (!code || !parse_pattern(kBeginLoadAsyncPattern, bytes, mask) || n < bytes.size()) return false;
    for (size_t i = 0; i < bytes.size(); ++i)
        if (mask[i] && code[i] != bytes[i]) return false;
    return true;
}

std::string check_resolved(const Resolved& r) {
    std::string missing;
    auto need = [&](const char* n, uint64_t v) {
        if (!v) missing += std::string(missing.empty() ? "" : ", ") + n;
    };
    need("cs_begin_load_async", r.begin_load_async);
    need("cs_loader_tryfind_call", r.loader_tryfind);
    need("cs_loader_async_call", r.loader_async);
    need("cs_loader_async_ret", r.loader_ret);
    need("cs_promise_tryfind_call", r.promise_tryfind);
    need("cs_promise_async_call", r.promise_async);
    need("cs_promise_async_ret", r.promise_ret);
    need("cs_capture_async_call", r.capture_async);
    need("cs_capture_async_ret", r.capture_ret);
    if (!missing.empty()) return "signature(s) not found exactly once: " + missing;
    if (r.loader_async != r.begin_load_async || r.promise_async != r.begin_load_async || r.capture_async != r.begin_load_async)
        return "a loader's async call does not go to the BeginLoadAsync pattern";
    if (r.loader_tryfind != r.promise_tryfind) return "the two TryFindFile calls disagree";
    if (r.loader_ret == r.promise_ret || r.loader_ret == r.capture_ret || r.promise_ret == r.capture_ret) return "two call sites coincide";
    return {};
}

Located locate(const ScanFn& scan, const ReadFn& read) {
    Located out;
    if (!scan || !read) {
        out.error = "no scanner";
        return out;
    }
    struct Site {
        const char* name;
        const char* exact;
        const char* relaxed;
        uint64_t match = 0;
    };
    Site sites[3] = {{"loader", kLoaderSitePattern, kLoaderSiteRelaxed},
                     {"promise", kPromiseSitePattern, kPromiseSiteRelaxed},
                     {"capture", kCaptureSitePattern, kCaptureSiteRelaxed}};
    for (Site& s : sites) {
        const SigResult a = scan(Signature(std::string("cs_") + s.name + "_site", s.exact, "none", 0));
        if (a.state == SigState::Found && a.match) {
            s.match = a.match;
            out.how += std::string(out.how.empty() ? "" : ", ") + s.name + " exact";
            continue;
        }
        const SigResult b = scan(Signature(std::string("cs_") + s.name + "_site_relaxed", s.relaxed, "none", 0));
        if (b.state == SigState::Found && b.match) {
            s.match = b.match;
            out.how += std::string(out.how.empty() ? "" : ", ") + s.name + " relaxed";
            continue;
        }
        out.error = std::string("the ") + s.name + " call site: exact pattern " + sig_state_name(a.state) + " (" + std::to_string(a.hits) +
                    " match(es)), relaxed pattern " + sig_state_name(b.state) + " (" + std::to_string(b.hits) + " match(es))";
        return out;
    }
    // E8 rel32 at `at`: the call's target, 0 when the bytes are not a call or unreadable
    auto call_target = [&](uint64_t at) -> uint64_t {
        uint8_t b[5] = {};
        if (!read(at, b, sizeof(b)) || b[0] != 0xE8) return 0;
        int32_t rel = 0;
        std::memcpy(&rel, b + 1, sizeof(rel));
        return at + 5 + static_cast<int64_t>(rel);
    };
    Resolved& r = out.res;
    r.loader_tryfind = call_target(sites[0].match + kLoaderTryFindCall);
    r.loader_async = call_target(sites[0].match + kLoaderAsyncCall);
    r.loader_ret = sites[0].match + kLoaderAsyncReturn;
    r.promise_tryfind = call_target(sites[1].match + kPromiseTryFindCall);
    r.promise_async = call_target(sites[1].match + kPromiseAsyncCall);
    r.promise_ret = sites[1].match + kPromiseAsyncReturn;
    r.capture_async = call_target(sites[2].match + kCaptureAsyncCall);
    r.capture_ret = sites[2].match + kCaptureAsyncReturn;
    if (r.loader_async && r.loader_async == r.promise_async && r.loader_async == r.capture_async) {
        uint8_t code[32] = {};
        if (read(r.loader_async, code, sizeof(code)) && is_begin_load_async(code, sizeof(code)))
            r.begin_load_async = r.loader_async;
        else {
            out.error = "the function the three async calls go to is not BeginLoadAsync (its first bytes differ)";
            return out;
        }
    } else {
        out.error = "the three async calls do not go to one function (or a call could not be read)";
        return out;
    }
    out.error = check_resolved(r);
    return out;
}

Config parse_config(const std::string& json_text, std::string* err) {
    Config c;
    auto describe = [](const Config& k) {
        std::string s = k.enabled ? "on" : "off";
        s += ", paths:";
        for (const auto& p : k.prefixes) s += " " + p;
        return s;
    };
    c.note = "defaults (" + describe(c) + ")";
    std::string why;
    auto finish = [&]() {
        if (!why.empty()) {
            c.note = "turbo_config.json crest_serve: " + why + "; defaults kept where it was wrong (" + describe(c) + ")";
            if (err) *err = why;
        } else {
            c.note = describe(c);
        }
        return c;
    };
    if (json_text.empty()) return c;
    json j = json::parse(json_text, nullptr, false, true);  // comments allowed, never throws
    if (j.is_discarded() || !j.is_object()) {
        why = "turbo_config.json is not readable JSON";
        return finish();
    }
    if (!j.contains("crest_serve")) return c;
    const json& g = j["crest_serve"];
    if (!g.is_object()) {
        why = "not an object";
        return finish();
    }
    if (g.contains("enabled")) {
        if (g["enabled"].is_boolean()) c.enabled = g["enabled"].get<bool>();
        else why = "enabled must be true or false";
    }
    if (g.contains("paths")) {
        if (!g["paths"].is_array()) {
            why += std::string(why.empty() ? "" : "; ") + "paths must be a list";
        } else {
            std::vector<std::string> ok;
            std::string refused;
            for (const auto& e : g["paths"]) {
                std::string p = e.is_string() ? e.get<std::string>() : std::string();
                for (char& ch : p)
                    if (ch == '\\') ch = '/';
                if (p.empty() || p.back() != '/' || !relative_ok(p)) {
                    refused += std::string(refused.empty() ? "" : ", ") + (e.is_string() ? "'" + e.get<std::string>() + "'" : e.dump());
                    continue;
                }
                ok.push_back(p);
            }
            if (!refused.empty()) why += std::string(why.empty() ? "" : "; ") + "refused path(s) (relative, ending in '/', no '..'): " + refused;
            c.prefixes = ok;  // an empty list serves nothing
        }
    }
    return finish();
}

std::string safe_relative(const char* path, size_t max_len) {
    if (!path) return {};
    size_t n = 0;
    while (n <= max_len && path[n] != 0) ++n;
    if (n == 0 || n > max_len) return {};
    std::string s(path, n);
    for (char& ch : s)
        if (ch == '\\') ch = '/';
    if (!relative_ok(s)) return {};
    return s;
}

bool matches_prefix(const char* path, const Config& cfg) {
    if (!path) return false;
    for (const auto& p : cfg.prefixes) {
        size_t i = 0;
        for (; i < p.size(); ++i) {
            const char c = path[i];
            if (c == 0 || lower_slash(c) != lower_slash(p[i])) break;
        }
        if (i == p.size() && !p.empty()) return true;
    }
    return false;
}

Decision decide(const Config& cfg, const char* path, const void* buf, uint64_t size, const Probe& probe, std::string* why) {
    auto no = [&](const char* w) {
        if (why) *why = w;
        return Decision::Original;
    };
    if (!cfg.enabled || !matches_prefix(path, cfg)) return Decision::Original;  // the common case: no reason logged
    if (safe_relative(path).empty()) return no("path refused (absolute, '..' or too long)");
    if (probe.killed) return no("turbo_output\\crest_serve_off.txt present");
    if (!probe.file_exists) return no("not in mods\\legacy (native)");
    if (!buf || size == 0) return no("no caller buffer");
    if (probe.file_size != size) return no("size on disk differs from the requested size (Live Editor did not serve this file)");
    return Decision::Serve;
}

const char* caller_name(Caller c) {
    switch (c) {
        case Caller::Loader: return "ChunkFileLoaderData";
        case Caller::Promise: return "CallPromise";
        case Caller::Capture: return "ChunkFileAsyncLoad";
        default: return "other";
    }
}

Caller classify_caller(const Resolved& r, uint64_t ret) {
    if (ret != 0 && ret == r.loader_ret) return Caller::Loader;
    if (ret != 0 && ret == r.promise_ret) return Caller::Promise;
    if (ret != 0 && ret == r.capture_ret) return Caller::Capture;
    return Caller::Other;
}

bool fill_buffer(const std::function<bool(std::vector<uint8_t>&)>& read_all, void* buf, uint64_t size, std::string* why) {
    std::vector<uint8_t> data;
    if (!buf || !read_all || !read_all(data)) {
        if (why) *why = "the file could not be read";
        return false;
    }
    if (data.size() != size) {
        if (why) *why = "read " + std::to_string(data.size()) + " bytes, the caller asked for " + std::to_string(size);
        return false;
    }
    std::memcpy(buf, data.data(), data.size());
    return true;
}

const std::vector<std::string>& decoy_candidates() {
    static const std::vector<std::string> c = {
        "data/ui/imgAssets/cmSocialMedia/emoji/stars.dds",      // 0x2C0
        "data/ui/imgAssets/cmSocialMedia/emoji/shocked.dds",    // 0x2C0
        "data/ui/imgAssets/cmSocialMedia/Verified.dds",         // 0x480
        "data/ui/imgAssets/fluxStroke/top_inactive.dds",        // 0x8C0
        "data/db/fifa_ng_db-allow.xml",                         // 0x24C (LE_ASYNC_CREST.md)
    };
    return c;
}

bool decoy_desc_valid(const Desc& d, std::string* why) {
    bool guid = false;
    for (uint8_t b : d.guid) guid = guid || b != 0;
    if (!guid) {
        if (why) *why = "zero chunk GUID";
        return false;
    }
    if (d.size == 0 || d.size > kScratchSize) {
        if (why) *why = "size " + std::to_string(d.size) + " not in 1.." + std::to_string(kScratchSize);
        return false;
    }
    return true;
}

int choose_decoy(const std::vector<std::string>& candidates, const std::function<bool(const std::string&)>& modded,
                 const std::function<bool(const std::string&, Desc*)>& try_find, Desc* out, std::string* why) {
    std::string w;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::string& c = candidates[i];
        std::string r;
        if (modded && modded(c)) {
            r = "present in mods\\legacy";
        } else {
            Desc d;
            std::memset(&d, 0, sizeof(d));
            if (!try_find || !try_find(c, &d)) {
                r = "TryFindFile false";
            } else if (decoy_desc_valid(d, &r)) {
                if (out) *out = d;
                if (why) *why = w;
                return static_cast<int>(i);
            }
        }
        w += std::string(w.empty() ? "" : "; ") + c + ": " + r;
    }
    if (why) *why = w.empty() ? std::string("no candidates") : w;
    return -1;
}

}  // namespace crest_serve
}  // namespace turbo
