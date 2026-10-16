// Native tests: the shared Oodle decompress dispatcher (core/oodle_dispatch.h) - kits only, attrib only, both, neither. Included by
// test_main.cpp after test_attrib_ebx.h (its REAL fixtures: the G-T1 attribulator group's compressed block as stored in the cas and its raw
// EBX; there is no Oodle here, the fake original decompress maps that one block to its raw bytes). oodle_dispatch_test::run().
//
// What is proven here: the dispatcher calls the original first and returns exactly what it returned; each slot gets on_call on every call
// and on_block only while its own gate is open; slots run in order and a block one slot overwrote is never offered to the next (no double
// patching); an exception stays in its slot; with only the attrib handler registered the bytes written are identical to calling
// attrib::Runtime::on_decompressed directly. NOT proven here: a block overwritten by a real kitswap::Runtime through the dispatcher (the
// kit Runtime's own recognise-and-overwrite is covered by tests/catalog/test_kit_swap.cpp; here its adaptor is exercised against a Runtime
// without a table, and the kit slot's overwrite is exercised with a stand-in handler).
#pragma once
#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>

#include "core/attrib_ebx.h"
#include "core/kit_swap.h"
#include "core/oodle_dispatch.h"

namespace oodle_dispatch_test {

using namespace turbo;
using namespace turbo::oodle;
namespace ax = attrib_ebx_test;

// ---------------------------------------------------------------- the fake original (what the game's Oodle would write)
static std::vector<uint8_t> g_fake_raw;  // what the original "decompresses" to (copied into the output buffer)
static int64_t g_fake_ret = -1;          // -1 = return raw_len (a full decode); anything else is returned as is (a short / failed decode)
static std::atomic<int> g_orig_calls{0};

static int64_t fake_orig(const void*, int64_t, void* raw, int64_t raw_len, int, int, int, void*, int64_t, void*, void*, void*, int64_t, int) {
    ++g_orig_calls;
    if (raw && raw_len > 0 && !g_fake_raw.empty()) std::memcpy(raw, g_fake_raw.data(), size_t(std::min<int64_t>(raw_len, int64_t(g_fake_raw.size()))));
    return g_fake_ret == -1 ? raw_len : g_fake_ret;
}

// one detour call as the game would make it
static int64_t call(Dispatcher& d, const uint8_t* comp, int64_t clen, std::vector<uint8_t>& out, int64_t rlen, void* cb = nullptr, void* dec_base = nullptr,
                    void* dec_mem = nullptr, int phase = 3) {
    return d.run(&fake_orig, comp, clen, out.data(), rlen, 1, 0, 0, dec_base, 0, cb, nullptr, dec_mem, 0, phase);
}

// ---------------------------------------------------------------- the real attrib Runtime with the G-T1 block registered
struct Attrib {
    std::vector<uint8_t> stream, raw;
    std::unique_ptr<attrib::Runtime> rt;
    std::atomic<uint64_t> errors{0};
    AttribBinding binding;
    const uint8_t* payload() const { return stream.data() + 8; }
    explicit Attrib(attrib::Mode mode = attrib::Mode::Patch, bool publish = true) {
        stream = ax::unhex(ax::kLLWComp);
        raw = ax::unhex(ax::kLLWRaw);
        rt.reset(new attrib::Runtime());
        if (publish) {
            const uint8_t* p = payload();
            const std::vector<uint8_t>& rv = raw;
            kitswap::OodleFn oodle = [p, &rv](const uint8_t* s, size_t n, uint8_t* d, size_t dn) {
                if (n != 271 || dn != rv.size() || std::memcmp(s, p, n) != 0) return false;
                std::memcpy(d, rv.data(), dn);
                return true;
            };
            attrib::Preset pr;
            std::string why;
            attrib::parse_preset(ax::read_text(ax::sample_preset()), pr, &why);
            kitswap::TableBuilder tb;
            attrib::ItemInfo it;
            attrib::add_asset(tb, stream, oodle, ax::kLLWAsset, "t", pr.patches, mode, it, &why);
            rt->publish(tb.finish(nullptr), {it});
        }
        binding.rt = rt.get();
        binding.errors = &errors;
    }
};

// ---------------------------------------------------------------- stand-in handlers
struct Stub {
    std::atomic<int> calls{0}, blocks{0}, live_checks{0};
    std::atomic<bool> live{true};
    int answer = 0;                    // what on_block returns
    uint8_t stamp = 0;                 // when answer == 2: written to raw[0]
    const uint8_t* only = nullptr;     // when set: answers `answer` only for this compressed pointer, else 0
    bool throw_std = false, throw_other = false, keep = true;  // keep: remember the last Call (off in the threaded case)
    std::atomic<int> errors{0};
    std::string last_error;
    Call last;
    int order_tag = 0;
    std::vector<int>* log = nullptr;   // order_tag pushed on every on_block
};
static Handler stub_handler(Stub* s) {
    Handler h;
    h.name = "stub";
    h.user = s;
    h.on_call = [](void* u, const Call&) { ++static_cast<Stub*>(u)->calls; };
    h.live = [](void* u) {
        Stub* s = static_cast<Stub*>(u);
        ++s->live_checks;
        return s->live.load();
    };
    h.on_block = [](void* u, const Call& c) -> int {
        Stub* s = static_cast<Stub*>(u);
        ++s->blocks;
        if (s->keep) s->last = c;
        if (s->log) s->log->push_back(s->order_tag);
        if (s->throw_std) throw std::runtime_error("boom");
        if (s->throw_other) throw 7;
        if (s->only && s->only != c.comp) return 0;
        if (s->answer == 2 && c.raw) c.raw[0] = s->stamp;
        return s->answer;
    };
    h.on_error = [](void* u, const char* what) {
        Stub* s = static_cast<Stub*>(u);
        ++s->errors;
        s->last_error = what ? what : "";
    };
    return h;
}

static void run() {
    std::printf("native shared Oodle dispatcher (oodle_dispatch)\n");

    run_case("oodle dispatch: slots - registered once, a handler needs on_block, retire is final", [] {
        Dispatcher d;
        CHECK(d.registered() == 0 && !d.has(kSlotKits) && !d.has(kSlotAttrib) && !d.has(-1) && !d.has(kSlots), "empty");
        Stub a, b;
        Handler none;  // no on_block
        CHECK(!d.add(kSlotKits, none) && !d.has(kSlotKits), "a handler without on_block is refused");
        CHECK(!d.add(-1, stub_handler(&a)) && !d.add(kSlots, stub_handler(&a)), "no such slot");
        CHECK(d.add(kSlotKits, stub_handler(&a)) && d.has(kSlotKits) && d.registered() == 1, "kits registered");
        CHECK(!d.add(kSlotKits, stub_handler(&b)), "the slot is taken: a second handler is refused");
        CHECK(d.add(kSlotAttrib, stub_handler(&b)) && d.registered() == 2, "attrib registered");
        d.retire(kSlotAttrib);
        CHECK(!d.has(kSlotAttrib) && d.registered() == 1 && d.stats(kSlotAttrib).retired && !d.stats(kSlotAttrib).registered, "retired: not called any more");
        CHECK(!d.add(kSlotAttrib, stub_handler(&b)) && !d.has(kSlotAttrib), "a retired slot is never registered again (its storage may still be read)");
        d.retire(kSlotKits);
        d.retire(-3);
        Dispatcher e;
        e.retire(kSlotKits);
        CHECK(!e.add(kSlotKits, stub_handler(&a)), "a slot retired before it was ever used stays closed");
        CHECK(std::string(slot_name(kSlotKits)) == "3D kit swap" && std::string(slot_name(kSlotAttrib)) == "attrib EBX patch", "slot names");
    });

    run_case("oodle dispatch: neither registered - the original runs, its return value and output pass untouched", [] {
        Dispatcher d;
        std::vector<uint8_t> raw0 = ax::unhex(ax::kLLWRaw), out(raw0.size(), 0xAB);
        g_fake_raw = raw0;
        g_fake_ret = -1;
        const int before = g_orig_calls;
        uint8_t comp[16] = {1, 2, 3};
        CHECK(call(d, comp, 16, out, int64_t(out.size())) == int64_t(out.size()) && out == raw0 && g_orig_calls == before + 1, "full decode: returned, written by the original only");
        g_fake_ret = 5;
        CHECK(call(d, comp, 16, out, int64_t(out.size())) == 5, "a short decode: 5 returned as is");
        g_fake_ret = -77;
        CHECK(call(d, comp, 16, out, int64_t(out.size())) == -77, "a failure code: returned as is");
        CHECK(d.calls() == 3 && d.dispatch(Call{}) == 0, "calls counted; nothing recognised");
        g_fake_ret = -1;
    });

    run_case("oodle dispatch: what a handler sees - the pointers, lengths, ret, flags (callback 1, decBufBase 2, decoderMemory 4) and phase", [] {
        Dispatcher d;
        Stub s;
        CHECK(d.add(kSlotKits, stub_handler(&s)), "register");
        std::vector<uint8_t> out(64, 0);
        g_fake_raw = {9, 8, 7};
        g_fake_ret = 33;
        uint8_t comp[40] = {0};
        int x = 0;
        call(d, comp, 40, out, 64, nullptr, nullptr, nullptr, 3);
        CHECK(s.last.comp == comp && s.last.comp_len == 40 && s.last.raw == out.data() && s.last.raw_len == 64 && s.last.ret == 33 && s.last.flags == 0 && s.last.phase == 3,
              "plain call");
        call(d, comp, 40, out, 64, &x, nullptr, nullptr, 1);
        CHECK(s.last.flags == 1 && s.last.phase == 1, "callback = 1, phase passed");
        call(d, comp, 40, out, 64, &x, &x, &x, 2);
        CHECK(s.last.flags == 7 && s.last.phase == 2, "all three = 7");
        call(d, comp, 40, out, 64, nullptr, &x, nullptr, 3);
        CHECK(s.last.flags == 2, "decBufBase = 2");
        call(d, comp, 40, out, 64, nullptr, nullptr, &x, 3);
        CHECK(s.last.flags == 4, "decoderMemory = 4");
        g_fake_raw.clear();
        g_fake_ret = -1;
    });

    run_case("oodle dispatch: order, gates and no double patching (stand-in handlers)", [] {
        Dispatcher d;
        std::vector<int> order;
        Stub kit, att;
        kit.order_tag = 1;
        att.order_tag = 2;
        kit.log = att.log = &order;
        CHECK(d.add(kSlotAttrib, stub_handler(&att)) && d.add(kSlotKits, stub_handler(&kit)), "register attrib first, kits second: the order is the slot number, not the call order");
        std::vector<uint8_t> out(32, 0x11);
        g_fake_raw.clear();
        g_fake_ret = -1;
        uint8_t comp[8] = {0};
        // nothing recognised: both asked, kits first
        CHECK(call(d, comp, 8, out, 32) == 32 && order == std::vector<int>({1, 2}) && kit.calls == 1 && att.calls == 1, "nobody recognises: both run, kits first");
        // kits overwrites: attrib gets on_call but not on_block
        order.clear();
        kit.answer = 2;
        kit.stamp = 0xEE;
        call(d, comp, 8, out, 32);
        CHECK(out[0] == 0xEE && order == std::vector<int>({1}) && att.calls == 2 && att.blocks == 1 && d.stats(kSlotAttrib).skipped == 1 && d.stats(kSlotKits).overwritten == 1,
              "kits overwrote: attrib counted the call, was NOT offered the block, skipped = 1");
        // kits only recognises (1): attrib still runs and may overwrite
        order.clear();
        kit.answer = 1;
        att.answer = 2;
        att.stamp = 0xDD;
        out[0] = 0x11;
        call(d, comp, 8, out, 32);
        CHECK(out[0] == 0xDD && order == std::vector<int>({1, 2}) && d.stats(kSlotKits).seen == 1 && d.stats(kSlotAttrib).overwritten == 1, "kits only recognised: attrib overwrites");
        // the kit gate closed: on_call still runs, on_block does not, attrib still works
        order.clear();
        kit.live = false;
        const int kb = kit.blocks, kc = kit.calls;
        out[0] = 0x11;
        call(d, comp, 8, out, 32);
        CHECK(kit.calls == kc + 1 && kit.blocks == kb && out[0] == 0xDD && order == std::vector<int>({2}), "kit gate closed: counted, not asked, and it does not gate the attrib handler");
        // the attrib gate closed with kits open and overwriting
        kit.live = true;
        kit.answer = 2;
        att.live = false;
        const int ab = att.blocks;
        call(d, comp, 8, out, 32);
        CHECK(att.blocks == ab && out[0] == 0xEE, "attrib gate closed and kits overwriting: attrib not asked");
        // retired slot: nothing
        d.retire(kSlotKits);
        const int kc2 = kit.calls;
        att.live = true;
        att.answer = 0;
        call(d, comp, 8, out, 32);
        CHECK(kit.calls == kc2, "a retired slot is not called, not even on_call");
    });

    run_case("oodle dispatch: an exception stays in its slot - counted, passed to on_error, the other slot still runs, the return value is the original's", [] {
        Dispatcher d;
        Stub kit, att;
        kit.throw_std = true;
        att.answer = 2;
        att.stamp = 0xAA;
        CHECK(d.add(kSlotKits, stub_handler(&kit)) && d.add(kSlotAttrib, stub_handler(&att)), "register");
        std::vector<uint8_t> out(16, 0);
        g_fake_raw.clear();
        g_fake_ret = 9;
        uint8_t comp[8] = {0};
        CHECK(call(d, comp, 8, out, 16) == 9, "return value");
        CHECK(kit.errors == 1 && kit.last_error == "boom" && d.stats(kSlotKits).errors == 1, "std::exception counted with its text");
        CHECK(out[0] == 0xAA && att.blocks == 1 && d.stats(kSlotAttrib).overwritten == 1, "the attrib slot ran and overwrote");
        kit.throw_std = false;
        kit.throw_other = true;
        call(d, comp, 8, out, 16);
        CHECK(kit.errors == 2 && kit.last_error == "unknown exception" && d.stats(kSlotKits).errors == 2, "a non-std exception too");
        // a throwing on_error cannot escape either
        Dispatcher e;
        Handler h = stub_handler(&kit);
        h.on_error = [](void*, const char*) { throw std::runtime_error("again"); };
        CHECK(e.add(kSlotKits, h), "register");
        kit.throw_other = false;
        kit.throw_std = true;
        CHECK(call(e, comp, 8, out, 16) == 9 && e.stats(kSlotKits).errors == 1, "an on_error that throws is swallowed");
        g_fake_ret = -1;
    });

    run_case("oodle dispatch: ATTRIB ONLY on the real G-T1 block - byte-identical to calling the Runtime directly; calls counted like the old detour", [] {
        Attrib a, ref;  // ref: the same Runtime used the way the old attrib detour used it
        Dispatcher d;
        CHECK(d.add(kSlotAttrib, make_attrib_handler(&a.binding)), "register");
        const int64_t rl = int64_t(a.raw.size());
        g_fake_raw = a.raw;
        g_fake_ret = -1;
        // direct, as the old detour did
        std::vector<uint8_t> want = a.raw;
        ref.rt->count_call(271, uint64_t(rl));
        CHECK(ref.rt->on_decompressed(ref.payload(), 271, want.data(), uint64_t(rl), rl) == 2, "direct call overwrites");
        // through the dispatcher
        std::vector<uint8_t> got(size_t(rl), 0);
        CHECK(call(d, a.payload(), 271, got, rl) == rl, "dispatcher returns the original's value");
        CHECK(got == want && got != a.raw && ax::f32_at(got, 0x58) == 0.5f && ax::f32_at(got, 0x60) == 0.4f, "same bytes as the direct call: width 0.5, length 0.4");
        CHECK(a.rt->calls() == ref.rt->calls() && a.rt->hits() == ref.rt->hits() && a.rt->patched() == ref.rt->patched() && a.rt->patched() == 1, "same counters");
        // a block that is not ours, the original returning a short size, a kill switch
        std::vector<uint8_t> other_comp(271, 0x5A), o2(size_t(rl), 0);
        call(d, other_comp.data(), 271, o2, rl);
        CHECK(o2 == a.raw && a.rt->patched() == 1, "another block: untouched (the original's bytes)");
        g_fake_ret = rl - 1;
        std::vector<uint8_t> o3(size_t(rl), 0);
        CHECK(call(d, a.payload(), 271, o3, rl) == rl - 1 && ax::diff_bytes(o3, a.raw) == 0, "the original returned another size: recognised, not overwritten, value passed on");
        g_fake_ret = -1;
        a.rt->set_killed(true);
        std::vector<uint8_t> o4(size_t(rl), 0);
        call(d, a.payload(), 271, o4, rl);
        CHECK(o4 == a.raw, "attrib_ebx_off.txt (Runtime killed): passed through");
        CHECK(d.stats(kSlotAttrib).overwritten == 1 && d.stats(kSlotAttrib).seen == 1, "dispatcher counters: 1 overwritten, 1 recognised-only");
        // observe mode: recognised, never written
        Attrib ob(attrib::Mode::Observe);
        Dispatcher d2;
        CHECK(d2.add(kSlotAttrib, make_attrib_handler(&ob.binding)), "register observe");
        std::vector<uint8_t> o5(size_t(rl), 0);
        call(d2, ob.payload(), 271, o5, rl);
        CHECK(o5 == ob.raw && ob.rt->hits() == 1 && ob.rt->patched() == 0, "observe: recognised, bytes unchanged");
    });

    run_case("oodle dispatch: ATTRIB ONLY before its table exists - the early-call detection still counts (the G-T1b timing answer)", [] {
        Attrib a(attrib::Mode::Patch, false);
        a.rt->set_expected_sizes({{271, 412}});
        Dispatcher d;
        CHECK(d.add(kSlotAttrib, make_attrib_handler(&a.binding)), "register");
        std::vector<uint8_t> out(412, 0);
        g_fake_raw.clear();
        g_fake_ret = -1;
        uint8_t comp[271] = {0};
        call(d, comp, 271, out, 412);
        call(d, comp, 271, out, 413);
        call(d, comp, 5000, out, 412);
        CHECK(a.rt->calls() == 3 && a.rt->early_calls() == 1 && a.rt->hits() == 0, "3 calls, one of the expected sizes, nothing recognised");
        // negative lengths (a failed call) are clamped to 0 for the Runtime and never crash
        call(d, comp, -4, out, -9);
        CHECK(a.rt->calls() == 4 && a.rt->early_calls() == 1, "negative lengths: counted, no crash");
    });

    run_case("oodle dispatch: KITS ONLY - the real kit Runtime's adaptor: count_call on every call, on_block gated by the host's live check, errors to the host", [] {
        const fs::path tmp = fs::temp_directory_path() / "turbo_oodle_dispatch_kit";
        kitswap::Runtime rt(tmp, tmp / "game", nullptr);  // no table (no staged kit): on_decompressed answers 0
        static bool live = true;
        static std::string last_err;
        kitswap::Runtime* rtp = &rt;
        KitBinding b;
        b.rt = rtp;
        b.live = [] { return live; };
        b.error = [](const char* w) { last_err = w; };
        Dispatcher d;
        CHECK(d.add(kSlotKits, make_kit_handler(&b)), "register");
        std::vector<uint8_t> out(64, 0x42), want = out;
        g_fake_raw.clear();
        g_fake_ret = -1;
        uint8_t comp[2048] = {0};
        CHECK(call(d, comp, 2048, out, 64) == 64 && out == want, "no table: return value and output untouched");
        CHECK(rt.status().calls == 1, "count_call ran");
        live = false;
        call(d, comp, 2048, out, 64);
        CHECK(rt.status().calls == 2 && out == want, "gate closed (hook_kit_oodle_off.txt / game_hooks_off.txt): the call is still counted");
        live = true;
        call(d, comp, -1, out, 64, &live);
        CHECK(rt.status().calls == 3 && out == want, "a callback pointer / negative length: no crash");
        // the host's error counter: a handler built on the same binding whose on_block throws is reported through b.error
        Handler h = make_kit_handler(&b);
        h.on_block = [](void*, const Call&) -> int { throw std::runtime_error("kit boom"); };
        Dispatcher e;
        CHECK(e.add(kSlotKits, h), "register throwing");
        call(e, comp, 2048, out, 64);
        CHECK(last_err == "kit boom" && e.stats(kSlotKits).errors == 1, "exception reported to the host's error hook");
        std::error_code ec;
        fs::remove_all(tmp, ec);
    });

    run_case("oodle dispatch: BOTH - attrib block patched exactly as attrib-only, a kit block overwritten by the kit slot, a block both want goes to the kit slot only", [] {
        Attrib solo, a;
        const int64_t rl = int64_t(a.raw.size());
        // reference: attrib alone
        Dispatcher ds;
        CHECK(ds.add(kSlotAttrib, make_attrib_handler(&solo.binding)), "attrib alone");
        g_fake_raw = a.raw;
        g_fake_ret = -1;
        std::vector<uint8_t> want(size_t(rl), 0);
        call(ds, solo.payload(), 271, want, rl);

        // both: the real attrib adaptor and a kit stand-in that overwrites one particular block (the kit overwrite itself is covered by test_kit_swap)
        Dispatcher d;
        std::vector<uint8_t> kit_block(2048, 0x33);
        Stub kit;
        kit.answer = 2;
        kit.stamp = 0x77;
        kit.only = kit_block.data();
        CHECK(d.add(kSlotKits, stub_handler(&kit)) && d.add(kSlotAttrib, make_attrib_handler(&a.binding)) && d.registered() == 2, "both registered");
        std::vector<uint8_t> got(size_t(rl), 0);
        CHECK(call(d, a.payload(), 271, got, rl) == rl && got == want, "the attrib block: byte-identical to attrib alone (kits answered 0)");
        CHECK(a.rt->patched() == 1 && kit.blocks == 1 && d.stats(kSlotKits).overwritten == 0, "attrib patched once; the kit slot was asked and did not recognise it");
        std::vector<uint8_t> o2(size_t(rl), 0);
        call(d, kit_block.data(), 2048, o2, rl);
        CHECK(o2[0] == 0x77 && std::memcmp(o2.data() + 1, a.raw.data() + 1, size_t(rl) - 1) == 0, "a kit block: overwritten by the kit slot");
        CHECK(a.rt->calls() == 2 && a.rt->patched() == 1 && d.stats(kSlotAttrib).skipped == 1, "attrib still counted the call, never saw the block (skipped 1)");
        // the same compressed block wanted by both: the kit slot wins, attrib is not offered it, nothing is patched twice
        Stub greedy;
        greedy.answer = 2;
        greedy.stamp = 0x55;
        Attrib a2;
        Dispatcher d3;
        CHECK(d3.add(kSlotKits, stub_handler(&greedy)) && d3.add(kSlotAttrib, make_attrib_handler(&a2.binding)), "register");
        std::vector<uint8_t> o3(size_t(rl), 0);
        call(d3, a2.payload(), 271, o3, rl);
        CHECK(o3[0] == 0x55 && std::memcmp(o3.data() + 1, a2.raw.data() + 1, size_t(rl) - 1) == 0 && a2.rt->hits() == 0 && a2.rt->patched() == 0 && d3.stats(kSlotAttrib).skipped == 1,
              "one writer: the kit slot's bytes, attrib not offered the block");
        // retire the kit slot (its install failed): the attrib patch is as before
        d3.retire(kSlotKits);
        std::vector<uint8_t> o4(size_t(rl), 0);
        call(d3, a2.payload(), 271, o4, rl);
        CHECK(o4 == want && a2.rt->patched() == 1, "kit slot retired: attrib patches the same block, identical bytes");

        // both real adaptors (the kit Runtime has no table): the attrib output is still identical and both Runtimes counted the call
        const fs::path tmp = fs::temp_directory_path() / "turbo_oodle_dispatch_both";
        kitswap::Runtime krt(tmp, tmp / "game", nullptr);
        KitBinding kb;
        kb.rt = &krt;
        Attrib a3;
        Dispatcher d4;
        CHECK(d4.add(kSlotKits, make_kit_handler(&kb)) && d4.add(kSlotAttrib, make_attrib_handler(&a3.binding)), "both real adaptors");
        std::vector<uint8_t> o5(size_t(rl), 0);
        CHECK(call(d4, a3.payload(), 271, o5, rl) == rl && o5 == want && krt.status().calls == 1 && a3.rt->calls() == 1 && a3.rt->patched() == 1,
              "attrib block through both adaptors: identical bytes, both counted");
        std::error_code ec;
        fs::remove_all(tmp, ec);
        g_fake_ret = -1;
    });

    run_case("oodle dispatch: many threads - every call dispatched once, the attrib block patched every time, registration races with the detour are safe", [] {
        Attrib a;
        const int64_t rl = int64_t(a.raw.size());
        g_fake_raw = a.raw;
        g_fake_ret = -1;
        Dispatcher d;
        Stub kit;
        kit.answer = 0;
        kit.keep = false;
        constexpr int kThreads = 6, kIters = 400;
        std::atomic<int> bad{0}, go{0};
        std::vector<std::thread> th;
        for (int t = 0; t < kThreads; ++t)
            th.emplace_back([&] {
                while (!go.load()) std::this_thread::yield();
                std::vector<uint8_t> out(size_t(rl), 0);
                for (int i = 0; i < kIters; ++i) {
                    std::fill(out.begin(), out.end(), 0);
                    if (call(d, a.payload(), 271, out, rl) != rl) ++bad;
                    // once the attrib slot is registered (it is, from the start) the block is always patched
                    if (ax::f32_at(out, 0x60) != 0.4f) ++bad;
                }
            });
        CHECK(d.add(kSlotAttrib, make_attrib_handler(&a.binding)), "attrib registered before the threads start");
        go = 1;
        // the kit slot registers and retires while the others run
        std::thread reg([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            d.add(kSlotKits, stub_handler(&kit));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            d.retire(kSlotKits);
        });
        for (auto& t : th) t.join();
        reg.join();
        CHECK(bad == 0, "every call returned the original's value and carried the patch");
        CHECK(d.calls() == uint64_t(kThreads * kIters) && a.rt->patched() == uint64_t(kThreads * kIters) && a.rt->calls() == uint64_t(kThreads * kIters),
              "no call lost or counted twice: " + std::to_string(d.calls()) + " / " + std::to_string(a.rt->patched()));
        g_fake_ret = -1;
    });
}

}  // namespace oodle_dispatch_test
