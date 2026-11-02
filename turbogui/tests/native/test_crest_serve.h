// Native tests (2.18 crest serve, core/crest_serve.h): the turbo_config.json block, the path filter, the per-call decision (native files
// and every non-crest path go to the original), the buffer fill, the caller classes (NULL only to callers that test it), the decoy
// choice, and the nine cs_* signatures resolving on a synthetic image built from the real 6AC07E31 bytes with their cross-checks.
// Included by test_main.cpp; crest_serve_test::run() is called from main next to test_capture_sequence().
#pragma once
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/crest_serve.h"

namespace crest_serve_test {

namespace cs = turbo::crest_serve;

inline bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// Put pattern text at buf[at] (?? -> 0)
inline void put(std::vector<uint8_t>& buf, size_t at, const char* pattern) {
    std::vector<uint8_t> b;
    std::vector<bool> m;
    turbo::parse_pattern(pattern, b, m);
    for (size_t i = 0; i < b.size(); ++i) buf[at + i] = m[i] ? b[i] : 0;
}
inline void rel32(std::vector<uint8_t>& buf, size_t insn, size_t target) {  // E8 at buf[insn]
    const int32_t d = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(insn + 5));
    std::memcpy(&buf[insn + 1], &d, 4);
}

inline cs::Probe present(uint64_t size) {
    cs::Probe p;
    p.file_exists = true;
    p.file_size = size;
    return p;
}

// X 2.0.1: locate() on any build. A synthetic image: BeginLoadAsync's real first bytes, the three call sites (exact or with other stack
// displacements), rel32s re-pointed inside the buffer; the scanner is resolve_signature over that buffer.
struct LocImage {
    std::vector<uint8_t> img = std::vector<uint8_t>(0x4000, 0xCC);
    uint64_t base = 0x140001000;
    size_t o_bla = 0x100, o_tryfind = 0x300, o_loader = 0x500, o_promise = 0x900, o_capture = 0xD00;
    explicit LocImage(bool moved_locals) {
        put(img, o_bla, cs::kBeginLoadAsyncPattern);
        put(img, o_tryfind, "FF 25 8E 24 08 80 48 83 66 90");
        put(img, o_loader, cs::kLoaderSitePattern);
        put(img, o_promise, cs::kPromiseSitePattern);
        put(img, o_capture, cs::kCaptureSitePattern);
        if (moved_locals) {  // a recompile moved the locals: every wildcarded displacement of the relaxed patterns changes
            img[o_loader + 4] = 0x50;    // lea rdx,[rsp+50] (was 40)
            img[o_loader + 46] = 0x78;   // mov rdx,[rsp+78] (was 68)
            img[o_loader + 68] = 0x78;   // mov r8,[rsp+78]
            img[o_loader + 73] = 0x50;   // lea rcx,[rsp+50]
            img[o_loader + 83] = 0x48;   // mov [rsp+48],rax (was 38)
            img[o_promise + 3] = 0xE0;   // lea rdx,[rbp-20] (was F0)
            img[o_promise + 23] = 0x28;  // mov r12,[rbp+28] (was 18)
            img[o_promise + 71] = 0xE0;  // lea rcx,[rbp-20]
            img[o_capture + 4] = 0x70;   // mov rax,[rsp+70] (was 60)
            img[o_capture + 21] = 0x68;  // mov r8,[rsp+68] (was 58)
            img[o_capture + 26] = 0x40;  // lea rcx,[rsp+40] (was 30)
        }
        point();
    }
    void point() {
        rel32(img, o_loader + cs::kLoaderTryFindCall, o_tryfind);
        rel32(img, o_loader + cs::kLoaderAsyncCall, o_bla);
        rel32(img, o_promise + cs::kPromiseTryFindCall, o_tryfind);
        rel32(img, o_promise + cs::kPromiseAsyncCall, o_bla);
        rel32(img, o_capture + cs::kCaptureAsyncCall, o_bla);
    }
    cs::Located locate() const {
        return cs::locate([this](const turbo::Signature& s) { return turbo::resolve_signature(s, img.data(), img.size(), base); },
                          [this](uint64_t a, uint8_t* out, size_t n) {
                              if (a < base || a - base + n > img.size()) return false;
                              std::memcpy(out, img.data() + (a - base), n);
                              return true;
                          });
    }
};

inline void locate_cases() {
    using turbo::Signature;
    using turbo::SigResult;
    using turbo::SigState;
    run_case("crest serve X 2.0.1: any build - the call sites found on the running game (exact, then relaxed), only the cross-check decides", [] {
        // the real 6AC07E31 bytes: every site by its exact pattern
        LocImage a(false);
        cs::Located L = a.locate();
        CHECK(L.error.empty(), "exact image: consistent (" + L.error + ")");
        CHECK(L.how == "loader exact, promise exact, capture exact", "exact image: how = " + L.how);
        CHECK(L.res.begin_load_async == a.base + a.o_bla && L.res.loader_async == a.base + a.o_bla && L.res.promise_async == a.base + a.o_bla &&
                  L.res.capture_async == a.base + a.o_bla,
              "BeginLoadAsync = the target of the three async calls");
        CHECK(L.res.loader_tryfind == a.base + a.o_tryfind && L.res.promise_tryfind == a.base + a.o_tryfind, "TryFindFile from both calls");
        CHECK(L.res.loader_ret == a.base + a.o_loader + 79 && L.res.promise_ret == a.base + a.o_promise + 77 && L.res.capture_ret == a.base + a.o_capture + 32,
              "return addresses as on 6AC07E31");
        CHECK(cs::classify_caller(L.res, L.res.promise_ret) == cs::Caller::Promise && !cs::caller_checks_null(cs::Caller::Promise),
              "CallPromise recognised (never gets NULL)");

        // a title update that moved the locals: the exact patterns miss, the relaxed ones find the same sites
        LocImage b(true);
        L = b.locate();
        CHECK(L.error.empty(), "moved locals: consistent (" + L.error + ")");
        CHECK(L.how == "loader relaxed, promise relaxed, capture relaxed", "moved locals: how = " + L.how);
        CHECK(L.res.begin_load_async == b.base + b.o_bla && L.res.loader_ret == b.base + b.o_loader + 79 && L.res.capture_ret == b.base + b.o_capture + 32,
              "moved locals: the same addresses");
        SigResult ex = turbo::resolve_signature(Signature("x", cs::kLoaderSitePattern, "none", 0), b.img.data(), b.img.size(), b.base);
        CHECK(ex.state == SigState::Missing, "moved locals: the exact loader pattern indeed misses");

        // only one site moved: exact where it holds, relaxed where not
        LocImage c(false);
        c.img[c.o_capture + 26] = 0x40;
        L = c.locate();
        CHECK(L.error.empty() && L.how == "loader exact, promise exact, capture relaxed", "mixed: " + L.how + " " + L.error);

        // the async calls go to two functions: refused
        LocImage d(false);
        put(d.img, 0x2000, cs::kBeginLoadAsyncPattern);
        rel32(d.img, d.o_promise + cs::kPromiseAsyncCall, 0x2000);
        L = d.locate();
        CHECK(has(L.error, "do not go to one function") && L.res.begin_load_async == 0, "two targets: refused (" + L.error + ")");

        // the common target is not BeginLoadAsync's first bytes: refused
        LocImage e(false);
        e.img[e.o_bla] = 0xE9;  // someone else's jmp at the function
        L = e.locate();
        CHECK(has(L.error, "is not BeginLoadAsync"), "a hooked or other function: refused (" + L.error + ")");

        // the TryFindFile calls disagree: refused
        LocImage f(false);
        rel32(f.img, f.o_promise + cs::kPromiseTryFindCall, 0x3100);
        L = f.locate();
        CHECK(has(L.error, "TryFindFile calls disagree"), "TryFindFile disagreement: refused (" + L.error + ")");

        // a site twice (exact and relaxed both ambiguous): refused, named
        LocImage g(false);
        put(g.img, 0x2400, cs::kCaptureSitePattern);
        rel32(g.img, 0x2400 + cs::kCaptureAsyncCall, g.o_bla);
        L = g.locate();
        CHECK(has(L.error, "capture call site") && has(L.error, "ambiguous"), "a site twice: refused (" + L.error + ")");

        // a site missing altogether: refused, named
        LocImage h(false);
        for (size_t i = 0; i < 40; ++i) h.img[h.o_capture + i] = 0x90;
        L = h.locate();
        CHECK(has(L.error, "capture call site") && has(L.error, "missing"), "a site missing: refused (" + L.error + ")");

        // a call byte that is not E8 (unreadable call): refused
        LocImage k(false);
        k.img[k.o_loader + cs::kLoaderAsyncCall] = 0x90;
        L = k.locate();
        CHECK(!L.error.empty() && L.res.begin_load_async == 0, "a broken call: refused (" + L.error + ")");

        // the relaxed patterns are the exact ones with only displacement bytes opened, same length
        std::vector<uint8_t> eb, rb;
        std::vector<bool> em, rm;
        bool same = true;
        const char* pairs[3][2] = {{cs::kLoaderSitePattern, cs::kLoaderSiteRelaxed},
                                   {cs::kPromiseSitePattern, cs::kPromiseSiteRelaxed},
                                   {cs::kCaptureSitePattern, cs::kCaptureSiteRelaxed}};
        for (auto& p : pairs) {
            same = same && turbo::parse_pattern(p[0], eb, em) && turbo::parse_pattern(p[1], rb, rm) && eb.size() == rb.size();
            for (size_t i = 0; same && i < eb.size(); ++i)
                if (rm[i] && (!em[i] || eb[i] != rb[i])) same = false;  // every fixed relaxed byte is a fixed exact byte
        }
        CHECK(same, "relaxed patterns = the exact bytes with some opened (same length, so the call offsets hold)");
        // every call opcode the offsets point at stays fixed in the relaxed patterns
        turbo::parse_pattern(cs::kLoaderSiteRelaxed, rb, rm);
        CHECK(rm[cs::kLoaderTryFindCall] && rb[cs::kLoaderTryFindCall] == 0xE8 && rm[cs::kLoaderAsyncCall] && rb[cs::kLoaderAsyncCall] == 0xE8, "loader: E8 fixed");
        turbo::parse_pattern(cs::kPromiseSiteRelaxed, rb, rm);
        CHECK(rm[cs::kPromiseTryFindCall] && rb[cs::kPromiseTryFindCall] == 0xE8 && rm[cs::kPromiseAsyncCall] && rb[cs::kPromiseAsyncCall] == 0xE8,
              "promise: E8 fixed");
        turbo::parse_pattern(cs::kCaptureSiteRelaxed, rb, rm);
        CHECK(rm[cs::kCaptureAsyncCall] && rb[cs::kCaptureAsyncCall] == 0xE8, "capture: E8 fixed");
    });
}

inline void run() {
    using turbo::SignatureTable;
    using turbo::Signature;
    using turbo::SigResult;
    using turbo::SigState;
    std::printf("native crest serve (2.18)\n");

    run_case("crest serve: turbo_config.json block (defaults, values, refused paths, wrong types, the shipped file)", [] {
        std::string err;
        cs::Config c = cs::parse_config("", &err);
        CHECK(c.enabled && c.prefixes.size() == 1 && c.prefixes[0] == "data/ui/imgAssets/crest/" && err.empty(), "no file: on, crests only");
        c = cs::parse_config(R"({"gui": {"autoload": true}})", &err);
        CHECK(c.enabled && c.prefixes.size() == 1 && err.empty(), "no block: defaults");
        c = cs::parse_config(R"({"crest_serve": {"enabled": false, "paths": ["data/ui/imgAssets/crest/", "data\\ui\\imgAssets\\crest512x512\\"]}})", &err);
        CHECK(!c.enabled && c.prefixes.size() == 2 && c.prefixes[1] == "data/ui/imgAssets/crest512x512/" && err.empty() && has(c.note, "off"),
              "values, backslashes made '/': " + c.note);
        err.clear();
        c = cs::parse_config(R"({"crest_serve": {"paths": ["../x/", "C:/x/", "/data/", "data/ui", "", 5, "data/ok/"]}})", &err);
        CHECK(c.prefixes.size() == 1 && c.prefixes[0] == "data/ok/" && has(err, "refused path(s)") && has(err, "'../x/'") && has(err, "'data/ui'"),
              "unsafe or unterminated prefixes refused: " + err);
        err.clear();
        c = cs::parse_config(R"({"crest_serve": {"paths": []}})", &err);
        CHECK(c.prefixes.empty() && err.empty(), "an empty list serves nothing");
        err.clear();
        c = cs::parse_config(R"({"crest_serve": {"enabled": "yes", "paths": "data/"}})", &err);
        CHECK(c.enabled && c.prefixes.size() == 1 && has(err, "enabled must be") && has(err, "paths must be a list"), "wrong types keep defaults: " + err);
        err.clear();
        c = cs::parse_config(R"({"crest_serve": true})", &err);
        CHECK(c.enabled && has(err, "not an object"), "not an object");
        err.clear();
        c = cs::parse_config("{ not json", &err);
        CHECK(c.enabled && c.prefixes.size() == 1 && !err.empty(), "unreadable: defaults");
        std::ifstream f("../turbo/package/turbo_config.json", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(!text.empty(), "the shipped turbo_config.json is readable from turbogui/");
        err.clear();
        c = cs::parse_config(text, &err);
        CHECK(c.enabled && c.prefixes.size() == 1 && c.prefixes[0] == "data/ui/imgAssets/crest/" && err.empty(), "shipped: on, crests only: " + c.note + " " + err);
    });

    run_case("crest serve: path filter - crest prefix only, any case, either slash; traversal and absolute paths refused", [] {
        cs::Config c;
        CHECK(cs::matches_prefix("data/ui/imgAssets/crest/dark/l1909.dds", c), "the LE log's crest path");
        CHECK(cs::matches_prefix("Data/UI/imgassets/CREST/light/l1909.dds", c) && cs::matches_prefix("data\\ui\\imgAssets\\crest\\custom\\l1.dds", c),
              "case and backslashes");
        CHECK(!cs::matches_prefix("data/ui/imgAssets/crest512x512/l1909.png", c) && !cs::matches_prefix("data/ui/imgAssets/crest", c) &&
                  !cs::matches_prefix("data/ui/imgAssets/kits/notfound.dds", c) && !cs::matches_prefix("", c) && !cs::matches_prefix(nullptr, c),
              "other folders, a shorter path, empty, null");
        CHECK(cs::safe_relative("data/ui/imgAssets/crest/dark/l1909.dds") == "data/ui/imgAssets/crest/dark/l1909.dds", "plain path kept");
        CHECK(cs::safe_relative("data\\ui\\x.dds") == "data/ui/x.dds", "backslashes made '/'");
        CHECK(cs::safe_relative("data/ui/imgAssets/crest/../../../../turbo/Turbo.dll").empty() && cs::safe_relative("C:/x.dds").empty() &&
                  cs::safe_relative("/data/x.dds").empty() && cs::safe_relative("").empty() && cs::safe_relative(nullptr).empty(),
              "traversal, drive, leading slash, empty, null refused");
        CHECK(cs::safe_relative("data/a..b/x.dds") == "data/a..b/x.dds", "'..' inside a name is fine");
        std::string longp(600, 'a');
        CHECK(cs::safe_relative(longp.c_str()).empty() && cs::safe_relative(longp.c_str(), 1000) == longp, "longer than the limit refused");
    });

    run_case("crest serve: decision - serve only a modded crest of exactly the requested size; everything else to the original", [] {
        cs::Config c;
        uint8_t buf[4] = {};
        const char* crest = "data/ui/imgAssets/crest/dark/l1909.dds";
        std::string why;
        CHECK(cs::decide(c, crest, buf, 0x55080, present(0x55080), &why) == cs::Decision::Serve, "modded crest, LE's size: serve");
        why.clear();
        CHECK(cs::decide(c, "data/ui/imgAssets/kits/notfound.dds", buf, 0x55080, present(0x55080), &why) == cs::Decision::Original && why.empty(),
              "another folder (even present in mods): original, not logged");
        CHECK(cs::decide(c, crest, buf, 0x55080, cs::Probe{}, &why) == cs::Decision::Original && has(why, "native"), "native crest: original");
        CHECK(cs::decide(c, crest, buf, 0x9000, present(0x55080), &why) == cs::Decision::Original && has(why, "size on disk differs"),
              "size differs (LE did not serve it): original");
        cs::Probe k = present(0x55080);
        k.killed = true;
        CHECK(cs::decide(c, crest, buf, 0x55080, k, &why) == cs::Decision::Original && has(why, "crest_serve_off.txt"), "kill switch: original");
        CHECK(cs::decide(c, crest, nullptr, 0x55080, present(0x55080), &why) == cs::Decision::Original && has(why, "no caller buffer"), "no buffer");
        CHECK(cs::decide(c, crest, buf, 0, present(0), &why) == cs::Decision::Original && has(why, "no caller buffer"), "size 0");
        CHECK(cs::decide(c, "data/ui/imgAssets/crest/../../db/x.db", buf, 4, present(4), &why) == cs::Decision::Original && has(why, "path refused"),
              "traversal under the prefix: original");
        cs::Config off;
        off.enabled = false;
        why.clear();
        CHECK(cs::decide(off, crest, buf, 0x55080, present(0x55080), &why) == cs::Decision::Original && why.empty(), "off in turbo_config.json");
        cs::Config none;
        none.prefixes.clear();
        CHECK(cs::decide(none, crest, buf, 0x55080, present(0x55080), &why) == cs::Decision::Original, "no prefixes: nothing served");
        CHECK(cs::decide(c, nullptr, buf, 4, present(4), &why) == cs::Decision::Original, "null path");
    });

    run_case("crest serve: buffer fill - exact size only, the caller's buffer untouched on any failure", [] {
        std::vector<uint8_t> file = {1, 2, 3, 4, 5};
        std::vector<uint8_t> buf(5, 0xEE);
        std::string why;
        CHECK(cs::fill_buffer([&](std::vector<uint8_t>& o) { o = file; return true; }, buf.data(), 5, &why) && buf == file, "the file's bytes");
        std::vector<uint8_t> keep(8, 0xEE), before = keep;
        CHECK(!cs::fill_buffer([&](std::vector<uint8_t>& o) { o = file; return true; }, keep.data(), 8, &why) && keep == before && has(why, "read 5 bytes"),
              "shorter than asked: refused, untouched");
        CHECK(!cs::fill_buffer([&](std::vector<uint8_t>& o) { o = file; return true; }, keep.data(), 4, &why) && keep == before, "longer: refused");
        CHECK(!cs::fill_buffer([&](std::vector<uint8_t>& o) { o = {9}; return false; }, keep.data(), 1, &why) && keep == before && has(why, "could not be read"),
              "unreadable: refused, untouched");
        CHECK(!cs::fill_buffer(nullptr, keep.data(), 1, &why), "no reader");
    });

    run_case("crest serve: callers - NULL only to ChunkFileLoaderData / ChunkFileAsyncLoad (they test it), never to CallPromise", [] {
        cs::Resolved r;
        r.loader_ret = 0x141C3D58C;
        r.promise_ret = 0x1446AD38E;
        r.capture_ret = 0x1438887BE;
        CHECK(cs::classify_caller(r, 0x141C3D58C) == cs::Caller::Loader && cs::classify_caller(r, 0x1446AD38E) == cs::Caller::Promise &&
                  cs::classify_caller(r, 0x1438887BE) == cs::Caller::Capture && cs::classify_caller(r, 0x140001000) == cs::Caller::Other &&
                  cs::classify_caller(cs::Resolved{}, 0) == cs::Caller::Other,
              "by return address");
        CHECK(cs::caller_checks_null(cs::Caller::Loader) && cs::caller_checks_null(cs::Caller::Capture) && !cs::caller_checks_null(cs::Caller::Promise) &&
                  !cs::caller_checks_null(cs::Caller::Other),
              "null-safe callers");
        CHECK(std::string(cs::caller_name(cs::Caller::Capture)) == "ChunkFileAsyncLoad", "names for the log");
    });

    run_case("crest serve: decoy - first native candidate with a valid descriptor; modded, missing or oversized ones skipped", [] {
        const auto& cands = cs::decoy_candidates();
        CHECK(cands.size() >= 3, "several candidates");
        for (const auto& c : cands) CHECK(!cs::safe_relative(c.c_str()).empty(), c + ": a safe relative path");
        auto fill = [](uint64_t size) {
            cs::Desc d;
            std::memset(&d, 0, sizeof(d));
            d.guid[0] = 0x34;
            d.guid[15] = 0x45;
            d.end = 0x91E6;
            d.offset = 0x1000;
            d.size = size;
            return d;
        };
        cs::Desc out;
        std::string why;
        // the first is modded, the second not found, the third oversized, the fourth fine
        std::vector<std::string> list = {"a/modded.dds", "b/missing.dds", "c/huge.dds", "d/ok.dds", "e/never.dds"};
        int asked = 0;
        const int i = cs::choose_decoy(
            list, [](const std::string& rel) { return rel == "a/modded.dds"; },
            [&](const std::string& rel, cs::Desc* d) {
                ++asked;
                if (rel == "b/missing.dds") return false;
                *d = fill(rel == "c/huge.dds" ? cs::kScratchSize + 1 : 0x2C0);
                return true;
            },
            &out, &why);
        CHECK(i == 3 && out.size == 0x2C0 && asked == 3, fmt("d/ok.dds chosen after 3 lookups (%d, %d)", i, asked));
        CHECK(has(why, "a/modded.dds: present in mods\\legacy") && has(why, "b/missing.dds: TryFindFile false") && has(why, "c/huge.dds: size"),
              "each skip named: " + why);
        cs::Desc zero = fill(0x2C0);
        std::memset(zero.guid, 0, sizeof(zero.guid));
        CHECK(!cs::decoy_desc_valid(zero, &why) && has(why, "zero chunk GUID"), "zero GUID refused");
        CHECK(!cs::decoy_desc_valid(fill(0), &why), "size 0 refused");
        CHECK(cs::decoy_desc_valid(fill(cs::kScratchSize), &why), "exactly the scratch size is fine");
        CHECK(cs::choose_decoy(list, [](const std::string&) { return true; }, nullptr, &out, &why) == -1 && has(why, "present in mods"), "all modded: none");
        CHECK(cs::choose_decoy({}, nullptr, nullptr, &out, &why) == -1 && why == "no candidates", "empty list");
    });

    run_case("crest serve: the cs_* signatures - 6AC07E31 only (exact_build), unique on a synthetic image of the real bytes, cross-checked", [] {
        const SignatureTable* nb = turbo::builtin_signature_table("6AC07E31-2145C000");
        const SignatureTable* ob = turbo::builtin_signature_table("6AB9813C-211EF000");
        CHECK(nb && ob, "both tables");
        if (!nb || !ob) return;
        const char* names[] = {"cs_begin_load_async",    "cs_loader_tryfind_call", "cs_loader_async_call",  "cs_loader_async_ret", "cs_promise_tryfind_call",
                               "cs_promise_async_call", "cs_promise_async_ret",   "cs_capture_async_call", "cs_capture_async_ret"};
        for (const char* n : names) {
            const Signature* s = nb->find(n);
            CHECK(s && s->exact_build && !s->adapt_optional && turbo::signature_offset_checked(*s), std::string(n) + ": built in, exact_build, offset checked");
            CHECK(ob->find(n) == nullptr, std::string(n) + ": not on 6AB9813C");
        }
        const SignatureTable a = turbo::adapt_signature_table(*nb, "6B000000-21500000");
        for (const char* n : names) CHECK(a.find(n) == nullptr, std::string(n) + ": never carried to another build");
        // a synthetic image: BeginLoadAsync's real first bytes (+ the real tail of the function), the three call sites' real bytes, the
        // rel32s re-pointed inside the buffer
        std::vector<uint8_t> img(0x3000, 0xCC);
        const uint64_t base = 0x140001000;
        const size_t o_bla = 0x100, o_tryfind = 0x300, o_loader = 0x500, o_promise = 0x900, o_capture = 0xD00;
        put(img, o_bla, cs::kBeginLoadAsyncPattern);
        put(img, o_bla + 24, "E8 33 1A 93 FE 48 8B 54 24 60 4C 8B CF 48 89 54 24 28 45 33 C0 48 8B D6 48 89 5C 24 20 48 8B C8 E8 A3 FE FF FF 48 8B 5C 24 40 48 8B "
                             "74 24 48 48 83 C4 30 5F C3");
        put(img, o_tryfind, "FF 25 8E 24 08 80 48 83 66 90");  // TryFindFile as the running game shows it: Live Editor's jump first
        put(img, o_loader, cs::kLoaderSitePattern);
        rel32(img, o_loader + cs::kLoaderTryFindCall, o_tryfind);
        rel32(img, o_loader + cs::kLoaderAsyncCall, o_bla);
        put(img, o_promise, cs::kPromiseSitePattern);
        rel32(img, o_promise + cs::kPromiseTryFindCall, o_tryfind);
        rel32(img, o_promise + cs::kPromiseAsyncCall, o_bla);
        put(img, o_capture, cs::kCaptureSitePattern);
        rel32(img, o_capture + cs::kCaptureAsyncCall, o_bla);
        uint64_t at[9] = {};
        for (int i = 0; i < 9; ++i) {
            SigResult r = turbo::resolve_signature(*nb->find(names[i]), img.data(), img.size(), base);
            CHECK(r.state == SigState::Found && r.hits == 1, std::string(names[i]) + ": found once (" + turbo::sig_state_name(r.state) + " " + r.error + ")");
            at[i] = r.address;
        }
        cs::Resolved r;
        r.begin_load_async = at[0];
        r.loader_tryfind = at[1];
        r.loader_async = at[2];
        r.loader_ret = at[3];
        r.promise_tryfind = at[4];
        r.promise_async = at[5];
        r.promise_ret = at[6];
        r.capture_async = at[7];
        r.capture_ret = at[8];
        CHECK(r.begin_load_async == base + o_bla && r.loader_async == base + o_bla && r.promise_async == base + o_bla && r.capture_async == base + o_bla,
              "all three callers call BeginLoadAsync");
        CHECK(r.loader_tryfind == base + o_tryfind && r.promise_tryfind == base + o_tryfind, "both TryFindFile calls agree");
        CHECK(r.loader_ret == base + o_loader + 79 && r.promise_ret == base + o_promise + 77 && r.capture_ret == base + o_capture + 32,
              "return addresses = the instruction after each call");
        CHECK(img[o_loader + 79] == 0x48 && img[o_promise + 77] == 0x48 && img[o_capture + 32] == 0x4C, "the bytes after the calls (mov ..., rax)");
        CHECK(cs::check_resolved(r).empty(), "consistent: " + cs::check_resolved(r));
        CHECK(cs::classify_caller(r, r.capture_ret) == cs::Caller::Capture, "the capture path is recognised by its return address");
        cs::Resolved bad = r;
        bad.promise_async = base + 0x10;
        CHECK(has(cs::check_resolved(bad), "does not go to the BeginLoadAsync pattern"), "a caller elsewhere: refused");
        bad = r;
        bad.promise_tryfind = base + 0x20;
        CHECK(has(cs::check_resolved(bad), "TryFindFile calls disagree"), "TryFindFile disagreement: refused");
        bad = r;
        bad.capture_ret = 0;
        CHECK(has(cs::check_resolved(bad), "cs_capture_async_ret"), "a missing signature is named");
        CHECK(cs::is_begin_load_async(img.data() + o_bla, 32), "the install's byte check accepts the real function");
        std::vector<uint8_t> hooked(img.begin() + o_bla, img.begin() + o_bla + 32);
        hooked[0] = 0xE9;  // someone else's jmp
        CHECK(!cs::is_begin_load_async(hooked.data(), hooked.size()) && !cs::is_begin_load_async(img.data() + o_bla, 8), "a hooked or short copy is refused");
        // a second copy of the function makes the pattern ambiguous: the host then gets 0 and the serve stays off
        put(img, 0x2800, cs::kBeginLoadAsyncPattern);
        SigResult amb = turbo::resolve_signature(*nb->find("cs_begin_load_async"), img.data(), img.size(), base);
        CHECK(amb.state == SigState::Ambiguous && amb.address == 0, std::string("two matches: ambiguous (") + turbo::sig_state_name(amb.state) + ")");
    });
    locate_cases();
}

}  // namespace crest_serve_test
