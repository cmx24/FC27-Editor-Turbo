// Native tests: attribulator EBX two-copy write (track A0): a preset entry may name a second copy ("also": the databases/simulation/
// partition next to groups/), each copy is patched on its own with its own expect value, and every location gets a write log.
// Fixture: the real G-T1 asset of test_attrib_ebx.h (kLLWRaw), and a "simulation twin" built from it: the same bytes under the
// databases/simulation/ asset name (the Name string is longer, so the EFIX chunk moves), with the length value at 0.9 instead of 0.84
// (the inventory's "differs" case). Included by test_main.cpp after test_attrib_ebx.h; attrib_two_copies_test::run().
#pragma once
#include <cstring>

#include "core/attrib_ebx.h"
#include "test_attrib_ebx.h"

namespace attrib_two_copies_test {

using namespace turbo;
using namespace turbo::attrib;
using attrib_ebx_test::kLLWAsset;
using attrib_ebx_test::kLLWRaw;

static const char* const kSimAsset =
    "fifa/attribulator/gameplay/databases/simulation/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime";
static const uint32_t kSig = 522591529;

static void put32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + size_t(i)] = uint8_t(v >> (8 * i));
}
static uint32_t get32(const std::vector<uint8_t>& d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}
// The G-T1 bytes under another asset name: the Name string is rewritten (it is the last thing in the data chunk), the chunk and RIFF sizes follow
static std::vector<uint8_t> renamed(const std::vector<uint8_t>& raw, const std::string& name, bool* ok) {
    *ok = false;
    EbxView v;
    std::string why;
    if (!parse_ebx(raw.data(), raw.size(), v, &why)) return raw;
    const auto insts = instances_of(raw.data(), raw.size(), v, kSig);
    if (insts.size() != 1) return raw;
    const size_t slot = insts[0] + kNameSlot;
    const size_t a = size_t(int64_t(slot) + int32_t(get32(raw, slot)));
    const std::string old = instance_name(raw.data(), raw.size(), v, insts[0]);
    const size_t old_end = a + old.size() + 1;
    if (old_end != v.data_end) return raw;  // the name must close the data chunk
    std::vector<uint8_t> out(raw.begin(), raw.begin() + long(a));
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(0);
    const size_t dsz = out.size() - 20;
    if (out.size() & 1) out.push_back(0);
    const size_t old_efix = 20 + (v.data_end - 20) + ((v.data_end - 20) & 1);
    out.insert(out.end(), raw.begin() + long(old_efix), raw.end());
    put32(out, 16, uint32_t(dsz));
    put32(out, 4, uint32_t(out.size() - 8));
    *ok = true;
    return out;
}

static void run() {
    std::printf("native attribulator EBX two-copy write (A0)\n");
    using attrib_ebx_test::mk;
    using attrib_ebx_test::unhex;
    using attrib_ebx_test::f32_at;

    static const char* const kHead = R"("schema": "turbo-attrib-preset/1", "build": "6AC07E31-2145C000")";
    auto entry = [](const std::string& extra) {
        return std::string(R"({"knob": "shape.length_in", "asset": ")") + kLLWAsset + R"(", "type_sig": 522591529, "path": "LineLengthModifierForBalanced", "offset": "0x30", )" +
               R"("kind": "f32", "expect": 0.84, "value": 0.4)" + extra + "}";
    };
    auto doc = [&](const std::string& e) { return "{" + std::string(kHead) + R"(, "patches": [)" + e + "]}"; };
    // ", \"also\": [{\"asset\": \"<asset>\"<extra>}]" built outside the CHECK macro (commas and quotes in its arguments)
    auto also = [](const std::string& asset, const std::string& extra) { return ", \"also\": [{\"asset\": \"" + asset + "\"" + extra + "}]"; };

    run_case("attrib two copies: a preset entry's \"also\" becomes one more Patch per copy; presets without it parse as before", [] {
        Preset p;
        std::string why;
        // the repository's presets: no "also", the very same two patches and one asset
        for (const char* f : {"attrib_preset_g_t1.json", "attrib_preset_sentinel.json"}) {
            CHECK(parse_preset(attrib_ebx_test::read_text(fs::path("..") / "tools" / f), p, &why) && p.patches.size() == 2 && p.assets().size() == 1 && p.patches[0].knob == "shape.length_in",
                  std::string(f) + " unchanged: " + why);
        }
    });

    run_case("attrib two copies: parse - full and short names, defaults from the entry, own expect, refusals", [&] {
        Preset p;
        std::string why;
        const std::string full = also(kSimAsset, ", \"expect\": 0.9");
        CHECK(parse_preset(doc(entry(full)), p, &why) && p.patches.size() == 2, "full sim name: " + why);
        CHECK(p.patches[1].asset == kSimAsset && p.patches[1].has_expect && p.patches[1].expect == 0.9 && p.patches[1].value == 0.4 && p.patches[1].offset == 0x30 &&
                  p.patches[1].type_sig == kSig && p.patches[1].path == "LineLengthModifierForBalanced" && p.patches[1].knob == "shape.length_in.also1",
              "the copy: the entry's field, offset, value and signature, its own expect");
        CHECK(p.assets() == std::vector<std::string>({kLLWAsset, kSimAsset}), "two assets, groups/ first");
        CHECK(parse_preset(doc(entry(R"(, "also": [{"asset": "databases/simulation/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime"}])")), p, &why) &&
                  p.patches.size() == 2 && p.patches[1].asset == kSimAsset && p.patches[1].expect == 0.84 && p.patches[1].has_expect,
              "short name completed; expect defaults to the entry's: " + why);
        CHECK(parse_preset(doc(entry(R"(, "also": [{"asset": "gameplay/databases/simulation/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime", "value": 0.5}])")),
                           p, &why) &&
                  p.patches[1].asset == kSimAsset && p.patches[1].value == 0.5,
              "gameplay/ short name, own value: " + why);
        auto bad = [&](const std::string& extra) { return !parse_preset(doc(entry(extra)), p, &why); };
        CHECK(bad(R"(, "also": [{"asset": "fifa/kits/x/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime"}])"), "not an attribulator asset: refused");
        CHECK(bad(R"(, "also": [{"asset": "databases/simulation/groups/gp_positioning/gp_positioning_zonal_defense_runtime"}])") && why.find("end like") != std::string::npos,
              "another group's tail (a typo cannot reach another group): refused: " + why);
        const bool own = bad(also(kLLWAsset, ""));
        CHECK(own, "the entry's own asset: refused");
        const bool shapes = bad(", \"also\": []") && bad(", \"also\": {}") && bad(", \"also\": [3]");
        CHECK(shapes, "empty / not a list / not objects: refused");
        const bool ex = bad(also(kSimAsset, ", \"expect\": \"x\""));
        CHECK(ex, "expect not a number: refused");
        const bool va = bad(also(kSimAsset, ", \"value\": \"x\""));
        CHECK(va, "value not a number: refused");
        // the same copy named twice (in "also" and as an entry of its own)
        const std::string two = std::string(R"({"asset": ")") + kSimAsset + R"(", "type_sig": 522591529, "path": "LineLengthModifierForBalanced", "offset": "0x30", "kind": "f32", "value": 0.4})" + "," +
                                entry(also(kSimAsset, ""));
        const bool dup = !parse_preset(doc(two), p, &why);
        CHECK(dup && why.find("twice") != std::string::npos, "the same value in the same copy twice (an entry, then an also naming it): refused: " + why);
        // an explicit entry for the sim copy needs no "also": the old way keeps working
        const std::string sep = entry("") + "," + std::string(R"({"asset": ")") + kSimAsset + R"(", "type_sig": 522591529, "path": "LineLengthModifierForBalanced", "offset": "0x30", "kind": "f32", "value": 0.4})";
        CHECK(parse_preset(doc(sep), p, &why) && p.patches.size() == 2 && p.assets().size() == 2, "two plain entries, one per copy: " + why);
    });

    run_case("attrib two copies: each copy is patched on its own - own expect, one write record per value, the copies do not touch each other", [&] {
        const auto base = unhex(kLLWRaw);
        bool ok = false;
        auto sim = renamed(base, kSimAsset, &ok);
        CHECK(ok && sim.size() > base.size(), "the simulation twin was built");
        EbxView v;
        std::string why;
        CHECK(parse_ebx(sim.data(), sim.size(), v, &why), "the twin parses: " + why);
        const auto insts = instances_of(sim.data(), sim.size(), v, kSig);
        CHECK(insts.size() == 1 && instance_name(sim.data(), sim.size(), v, insts[0]) == kSimAsset, "the twin's Name is the simulation asset");
        // the sim copy carries 0.9 where the groups/ copy has 0.84 (the +0x30 float: P + dc + 0x30 = the same absolute offset as in the base)
        const float nine = 0.9f;
        std::memcpy(&sim[insts[0] + 0x30], &nine, 4);

        Preset p;
        const std::string both = ", \"also\": [{\"asset\": \"" + std::string(kSimAsset) + "\", \"expect\": 0.9}]";
        const bool pp = parse_preset(doc(entry(both)), p, &why);
        CHECK(pp, "preset: " + why);

        // groups/ copy: only its own patches apply (the sim patch names another asset)
        auto b = base;
        std::vector<std::string> ch;
        std::vector<WriteRec> wr;
        CHECK(patch_ebx(b, kLLWAsset, p.patches, &ch, &why, &wr) && wr.size() == 1 && wr[0].path == "LineLengthModifierForBalanced" && wr[0].at == 0x60 &&
                  float(wr[0].before) == 0.84f && wr[0].after == 0.4 && f32_at(b, 0x60) == 0.4f && ch.size() == 1 && ch[0] == "LineLengthModifierForBalanced 0.84 -> 0.4",
              "groups/ copy: 0.84 -> 0.4, one record, the old change text: " + why);
        // sim copy: its own expect (0.9) holds, 0.9 -> 0.4 at its own offset
        auto s = sim;
        CHECK(patch_ebx(s, kSimAsset, p.patches, &ch, &why, &wr) && wr.size() == 1 && float(wr[0].before) == 0.9f && wr[0].after == 0.4 && wr[0].at == insts[0] + 0x30 &&
                  f32_at(s, insts[0] + 0x30) == 0.4f,
              "simulation copy: 0.9 -> 0.4: " + why);
        size_t changed = 0;
        for (size_t i = 0; i < s.size(); ++i) changed += s[i] != sim[i];
        CHECK(changed <= 4, fmt("only the float changed in the sim copy (%zu bytes)", changed));
        // the entry's expect (0.84) would have refused the sim copy (0.9): that is why a copy needs its own expect
        Preset q;
        const bool q_ok = parse_preset(doc(entry(also(kSimAsset, ""))), q, &why);
        CHECK(q_ok, "preset without the copy's own expect");
        auto s2 = sim;
        CHECK(!patch_ebx(s2, kSimAsset, q.patches, &ch, &why, &wr) && s2 == sim && wr.empty() && why.find("holds 0.9") != std::string::npos,
              "default expect 0.84 on the 0.9 copy: refused, unchanged: " + why);
        // the copies' patches each find only their own asset
        auto b2 = base;
        CHECK(!patch_ebx(b2, kSimAsset, p.patches, &ch, &why) && b2 == base, "the sim patch on the groups/ bytes: refused (Name differs): " + why);
    });

    run_case("attrib two copies: the write log names asset, location, field, old and new value; none for a watch-only copy", [&] {
        const auto base = unhex(kLLWRaw);
        Preset p;
        std::string why;
        CHECK(parse_preset(doc(entry("")), p, &why), why);
        ItemInfo it;
        it.asset = kLLWAsset;
        it.toc = "Patch/Win32/fc/fcgame/fcgame.toc";
        it.cas = "0000a3a00de30001";
        it.off = 148793182;
        it.size = 279;
        auto b = base;
        CHECK(patch_ebx(b, kLLWAsset, p.patches, nullptr, &why, &it.writes), why);
        it.patch = true;
        const auto lines = write_lines(it);
        CHECK(lines.size() == 1 &&
                  lines[0] == std::string("attrib write ") + kLLWAsset + " [Patch/Win32/fc/fcgame/fcgame.toc cas 0000a3a00de30001 off 148793182 size 279] LineLengthModifierForBalanced @0x60 0.84 -> 0.4",
              "one line: " + (lines.empty() ? std::string("none") : lines[0]));
        it.cas.clear();
        const auto l2 = write_lines(it);
        CHECK(l2.size() == 1 && l2[0].find("[Patch/Win32/fc/fcgame/fcgame.toc] ") != std::string::npos, "location unknown: the toc only");
        it.patch = false;
        CHECK(write_lines(it).empty(), "a watch-only copy writes nothing: no lines");
    });
}

}  // namespace attrib_two_copies_test
