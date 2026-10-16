// Native tests: attribulator EBX patch at load (core/attrib_ebx.h). Fixtures: three REAL attribulator group assets of the FC 27 install
// (build 6AC07E31-2145C000, Patch layer, read offline 2026-10-15; vanilla game data, no mod's values): the G-T1 group
// gp_positioningformation_attackinglinelengthwidth (its compressed Oodle block as stored in the cas, and its raw EBX), gp_actor_action
// (float arrays) and gp_positioning_zonal_defense_playoffsides (FloatCurves). There is no Oodle here: the fake decompressor maps the one real
// compressed block to its raw bytes. Included by test_main.cpp; attrib_ebx_test::run().
#pragma once
#include <cmath>
#include <cstring>

#include "core/attrib_ebx.h"

namespace attrib_ebx_test {

using namespace turbo;
using namespace turbo::attrib;

static const char* const kLLWComp =
    "0000019c1970010f8c0c000109880107524946469401000080800b040a110e0c18102825042c00000e000d0f040032220d010a0a0f17030000b84542"
    "58440f00001dd652180d8cf7ed37a7258314497a5c02b16800803f1f856b3f3d0a573fe17a543fcdcc4c3fae47613f9a9959333333466966612f4174"
    "74726962756c61746f722f47616d65706c61792f67726f7570732f67705f706f736974696f6e696e67666f726d615f7461636b696e676c696e656c65"
    "6e67746877696474685f72756e74696d6500004546495860013581fc772d77d56df62f6e77ca64babb291d261f102870905808000000000000000000"
    "000000001efaf13244fe260a080a2ffafb7a43e647dae7c8ff09dabaa22def29ea83e38230809e";
static const char* const kLLWRaw =
    "524946469401000045425800454258440f0100000000000000000000000000001dd652180d8cf7ed37a7258314497a5c000000000000000000000000"
    "000000000200000000b10000680000000000000000000000000000000000803f1f856b3f3d0a573fe17a543fcdcc4c3f0000803fae47613f9a99593f"
    "cdcc4c3f3333333fcdcc4c3fae47613f0000000000000000000000000000000000000000000000000000000000000000000000000000000046696661"
    "2f417474726962756c61746f722f47616d65706c61792f67726f7570732f67705f706f736974696f6e696e67666f726d6174696f6e2f67705f706f73"
    "6974696f6e696e67666f726d6174696f6e5f61747461636b696e676c696e656c656e67746877696474685f72756e74696d6500004546495860000000"
    "1dd652180d8cf7ed37a7258314497a5c010000003581fc772d77d56df62f6e77ca64babb01000000291d261f01000000010000001000000001000000"
    "28000000000000000000000000000000000000007000000090000000900000000000000045425858080000000000000000000000";
static const char* const kActorRaw =
    "52494646a003000045425800454258448f02000000000000000000000000000027e5621a197d1f2c90296921b6a2ab24000000000000000000000000"
    "000000000200000000b1000018020000000000000000000000000000ec010000000000008401000000000000bc000000000000004401000000000000"
    "0c01000000000000d4000000000000008c0100000000000000001042d3dddd3e0000b4420000003f0000e6c20000803f000058420000a0410000003f"
    "000008419a99193f0000a04000005c42000010410000e8410000803f0000b0400000004000002041faeeee3e9488883e0000c03f0000c0400000003f"
    "0000803f00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000b000000cdcccc3e6666e63e"
    "0000003fcdcc0c3f9a99193f6666263f3333333f0000403fcdcc4c3f6666663f0000803f0b00000000000000cdcc4c3e0000803e9a99993ecdcccc3e"
    "0000003f9a99193f3333333fcdcc4c3f6666663f0000803f0b0000000000a03f3333933f6666863f48e17a3f6666663f9a99593f3d0a573f85eb513f"
    "295c4f3fb81e453f0000403f0b000000cdccac3f0000a03f3333933f713d8a3f0000803f3333733fd7a3703f1f856b3fc3f5683f52b85e3f9a99593f"
    "0b0000000000744200007442000074420000204200002042000020429a991d42cdcc28420000f0410000f0410000f0410b0000000000744200007442"
    "000074420000f0410000f0410000f0410000f0410000f041cdccac41cdccac41cdccac410600000000000000000020400000a0409a99e94033330b41"
    "0000204100000000466966612f417474726962756c61746f722f47616d65706c61792f67726f7570732f67705f6163746f722f67705f6163746f725f"
    "616374696f6e5f72756e74696d650000454649587c00000027e5621a197d1f2c90296921b6a2ab240100000057de0f5d7c5f458523ca30c3542c77ad"
    "01000000942e00ff01000000010000001000000008000000280000003800000040000000480000005000000058000000600000006800000000000000"
    "000000000000000000000000e000000040020000400200000000000045425858780000000700000000000000040100000b000000eacc62176802ffff"
    "340100000b000000c14c605b6802ffff640100000b000000fd0e56646802ffff940100000b0000008c461d696802ffffc40100000b000000081674a3"
    "6802fffff40100000b0000007cb357856802ffff24020000060000003443a6f06802ffff";
static const char* const kOffsideRaw =
    "52494646380a000045425800454258441f0800000000000000000000000000000906c42cf68911b52f1629b844312184000000000000000000000000"
    "000000000200000000b1000088070000000000000000000000000000c802000000000000c8000000000000006801000000000000d001000000000000"
    "9001000000000000d80200000000000008030000000000000801000000000000e001000000000000c000000000000000400200000000000000020000"
    "00000000380000000000000085eb313f85eb313f9a99193f9a99193f000040400000003f9a99193f0001000029c5e80facd1bc74e3f046868387b03e"
    "010000000000000000000000000000000200000000b10000dc020000000000000000803f000000002a782cbc40b1de7d0f6ce22fe36e477c01000000"
    "0000000000000000000000000200000000b1000034030000000000000000803f0000000037bf27a3a30bf3df434ecc8ce4439cd50100000000000000"
    "00000000000000000200000000b100001c030000000000000000803f000000004417c922bf85e776764a3c809c75f39e010000000000000000000000"
    "000000000200000000b1000074030000000000000000803f0000000054ce1982340bcbf57e7faf5b7861db6a01000000000000000000000000000000"
    "0200000000b10000cc030000000000000000803f0000000077269c0b90bbc9318422f6db2660cb750100000000000000000000000000000002000000"
    "00b10000b4030000000000000000803f000000007f4e9277e8d389416aa3751c3059df3b010000000000000000000000000000000200000000b10000"
    "d4030000000000000000803f00000000804f131efac2a196336dbc9a1b30cb9a010000000000000000000000000000000200000000b100002c040000"
    "000000000000803f000000008e28fd5df7cace0118871d84e84b945d010000000000000000000000000000000200000000b100001404000000000000"
    "0000803f00000000a177687493881ffedf0912b508495e11010000000000000000000000000000000200000000b10000fc030000000000000000803f"
    "00000000cd98fdc6398073522fb70e311e97b28e010000000000000000000000000000000200000000b1000038040000000000000000803f00000000"
    "dec4060c61e84abdecb0c37d53254963010000000000000000000000000000000200000000b1000020040000000000000000803f00000000e6431657"
    "b3bb1adb058a8852357e3a5f010000000000000000000000000000000200000000b1000008040000000000000000803f000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000005000000020000000000000000000000000000000000803fc8ccccbd"
    "d0cccc3d020000009a99993e00000000000000003333733fccccccbdcccccc3d020000009a99193f00000000000000006666663fd0ccccbdd0cccc3d"
    "02000000cb44663f0000000000000000cdcc4c3fd0ccccbd0858c73d020000000000803f00000000000000000000003f0858c7bdd0cccc3d01000000"
    "0200000085eb313f000000000000000000008041d0ccccbdd0cccc3d05000000020000000000000000000000000000000000003fc8ccccbdd0cccc3d"
    "020000009a99993e00000000000000006666e63eccccccbdcccccc3d020000009a99193f0000000000000000cdcccc3ed0ccccbdd0cccc3d02000000"
    "6666663f00000000000000009a99993ed0ccccbd0858c73d020000000000803f0000000000000000cdcccc3d0858c7bdd0cccc3d0500000002000000"
    "0000000000000000000000000000803fc8ccccbdd0cccc3d020000009a99993e00000000000000003333733fccccccbdcccccc3d020000009a99193f"
    "00000000000000006666663fd0ccccbdd0cccc3d02000000cb44663f0000000000000000cdcc4c3fd0ccccbd0858c73d020000000000803f00000000"
    "000000000000003f0858c7bdd0cccc3d010000000200000085eb313f0000000000000000000080c0d0ccccbdd0cccc3d030000000200000076bd193f"
    "f28ce73e000000000317383bd0ccccbd0884c53d020000003333333ffc19cf3e549bdabe0000003f601cbdbdd06c5a3e020000000000803f00000000"
    "18a575be6666663fe8b850be00cdcc3d05000000020000000000000000000000000000000000003fc8ccccbdd0cccc3d020000009a99993e00000000"
    "000000006666e63eccccccbdcccccc3d020000009a99193f0000000000000000cdcccc3ed0ccccbdd0cccc3d020000006666663f0000000000000000"
    "9a99993ed0ccccbd0858c73d020000000000803f0000000000000000cdcccc3d0858c7bdd0cccc3d010000000200000085eb313f0000000000000000"
    "0000a040c8ccccbdd0cccc3d010000000200000085eb313f000000000000000000002041d0ccccbdd0cccc3d04000000020000000000803e00000000"
    "000000000000803fccccccbdd0cccc3d020000000000003f00000000000000000000003fccccccbdd0cccc3d0200000085eb313f0000000000000000"
    "0000803ed0ccccbdd0cccc3d020000000000803f000000000000000000000000d0ccccbdd0cccc3d010000000200000085eb313f0000000000000000"
    "000040c0d0ccccbdd0cccc3d010000000200000085eb313f000000000000000000004040c8ccccbdd0cccc3d010000000200000085eb313f00000000"
    "0000000000000040d0ccccbdd0cccc3d00000000466966612f417474726962756c61746f722f47616d65706c61792f67726f7570732f67705f706f73"
    "6974696f6e696e672f67705f706f736974696f6e696e675f7a6f6e616c5f646566656e73655f706c61796f666673696465735f72756e74696d650000"
    "45464958240100000906c42cf68911b52f1629b84431218403000000527d762bce92c571d92b4ad684ef24b168f1260edfa24ed7807b19103d97b179"
    "0a80ae1a3bac499b990ec0474bb8782403000000929e9264c514ee7c2c70b28c0e0000000e00000010000000d0000000080100004001000078010000"
    "b0010000e8010000200200005802000090020000c80200000003000038030000700300001b0000002800000038000000400000004800000050000000"
    "580000006000000068000000700000007800000080000000880000009000000098000000e8000000200100005801000090010000c801000000020000"
    "3802000070020000a8020000e002000018030000500300008803000000000000000000000000000000000000a0030000b0070000b007000000000000"
    "45425858d80000000d00000000000000c403000005000000f58f3032480002005404000001000000f58f3032480002007404000005000000f58f3032"
    "480002000405000005000000f58f3032480002009405000001000000f58f303248000200b405000003000000f58f3032480002000c06000005000000"
    "f58f3032480002009c06000001000000f58f303248000200bc06000001000000f58f303248000200dc06000004000000f58f30324800020050070000"
    "01000000f58f3032480002007007000001000000f58f3032480002009007000001000000f58f303248000200";

static const char* const kLLWAsset = "fifa/attribulator/gameplay/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime";
static const char* const kActorAsset = "fifa/attribulator/gameplay/groups/gp_actor/gp_actor_action_runtime";
static const char* const kOffsideAsset = "fifa/attribulator/gameplay/groups/gp_positioning/gp_positioning_zonal_defense_playoffsides_runtime";
constexpr uint32_t kLLWSig = 522591529u, kActorSig = 4278202004u, kOffsideSig = 1687330450u;

static std::vector<uint8_t> unhex(const char* h) {
    std::vector<uint8_t> v;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) v.push_back(uint8_t(nib(h[i]) << 4 | nib(h[i + 1])));
    return v;
}
static float f32_at(const std::vector<uint8_t>& d, size_t at) {
    float f = 0;
    std::memcpy(&f, d.data() + at, 4);
    return f;
}
static size_t diff_bytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return size_t(-1);
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}
static Patch mk(const char* asset, uint32_t sig, const char* path, uint32_t off, Kind k, double value, bool has_expect = false, double expect = 0) {
    Patch p;
    p.asset = asset;
    p.type_sig = sig;
    p.path = path;
    p.offset = off;
    p.kind = k;
    p.value = value;
    p.has_expect = has_expect;
    p.expect = expect;
    return p;
}
static std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
// the G-T1 preset of the repository (tools/attrib_preset_g_t1.json; the tests run in turbogui/)
static fs::path sample_preset() { return fs::path("..") / "tools" / "attrib_preset_g_t1.json"; }

static void run() {
    std::printf("native attribulator EBX patch (attrib_ebx)\n");

    run_case("attrib ebx: the real G-T1 asset parses - one instance of AttribSchema_gp_positioningformation_linelengthwidth, its Name is the asset, 1.0 / 0.84 at +0x28 / +0x30", [] {
        const auto raw = unhex(kLLWRaw);
        CHECK(raw.size() == 412, "412 raw bytes");
        EbxView v;
        std::string why;
        CHECK(parse_ebx(raw.data(), raw.size(), v, &why), "parses: " + why);
        CHECK(v.payload == 0x20 && v.type_sigs == std::vector<uint32_t>({kLLWSig}) && v.dc == std::vector<uint32_t>({16}) && v.ptrs == std::vector<uint32_t>({40}) && v.exported == 1,
              "EFIX: one type, one instance at P+16, one pointer slot (the Name)");
        const auto in = instances_of(raw.data(), raw.size(), v, kLLWSig);
        CHECK(in.size() == 1 && in[0] == 0x30, "the instance at 0x30");
        CHECK(instances_of(raw.data(), raw.size(), v, kActorSig).empty(), "no instance of another type");
        const std::string nm = in.empty() ? std::string() : instance_name(raw.data(), raw.size(), v, in[0]);
        CHECK(nm == "Fifa/Attribulator/Gameplay/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime", "Name: " + nm);
        CHECK(f32_at(raw, 0x30 + 0x28) == 1.0f && std::fabs(f32_at(raw, 0x30 + 0x30) - 0.84f) < 1e-6f && std::fabs(f32_at(raw, 0x30 + 0x2C) - 0.92f) < 1e-6f,
              "lineWidthModifier 1.0, LineLengthModifierForBalanced 0.84, ...ForLongBall 0.92 (the inventory's values)");
        // damaged containers are refused
        auto cut = raw;
        cut.resize(200);
        CHECK(!parse_ebx(cut.data(), cut.size(), v, &why), "a cut asset is refused: " + why);
        auto bad = raw;
        bad[12] = 'X';
        CHECK(!parse_ebx(bad.data(), bad.size(), v, &why), "no EBXD: refused");
        auto big = raw;
        big[4] ^= 0x10;
        CHECK(!parse_ebx(big.data(), big.size(), v, &why), "a RIFF size that does not match: refused");
    });

    run_case("attrib ebx: the G-T1 patch - length 0.84 -> 0.4, width 1.0 -> 0.5, nothing else changes (8 bytes at most), no clamping to the slider bounds", [] {
        Preset pr;
        std::string why;
        CHECK(parse_preset(read_text(sample_preset()), pr, &why), "the repository's G-T1 preset parses: " + why);
        CHECK(pr.build == "6AC07E31-2145C000" && pr.mode == Mode::Patch && pr.patches.size() == 2 && pr.assets() == std::vector<std::string>({kLLWAsset}),
              "build, patch mode, two values, one asset");
        const auto orig = unhex(kLLWRaw);
        auto raw = orig;
        std::vector<std::string> ch;
        CHECK(patch_ebx(raw, kLLWAsset, pr.patches, &ch, &why), "patched: " + why);
        CHECK(raw.size() == orig.size() && f32_at(raw, 0x58) == 0.5f && f32_at(raw, 0x60) == 0.4f, "width 0.5 at 0x58, length 0.4 at 0x60");
        CHECK(diff_bytes(raw, orig) <= 8 && f32_at(raw, 0x5C) == f32_at(orig, 0x5C), fmt("only the two floats changed (%zu bytes)", diff_bytes(raw, orig)));
        CHECK(ch.size() == 2 && ch[0] == "LineLengthModifierForBalanced 0.84 -> 0.4" && ch[1] == "lineWidthModifier 1 -> 0.5",
              "the changes are listed: " + (ch.empty() ? std::string() : ch[0]));
        // patched bytes are not patched again: the expect check sees 0.4, not 0.84
        auto again = raw;
        CHECK(!patch_ebx(again, kLLWAsset, pr.patches, &ch, &why) && again == raw, "a second patch on patched bytes is refused (expect 0.84): " + why);
        // the preset's offsets are the knobs file's offsets for the same knob, group and field, and its expect values the knobs' defaults
        const nlohmann::json k = nlohmann::json::parse(read_text(fs::path("..") / "turbo" / "package" / "turbo" / "tuning" / "knobs_6AC07E31.json"), nullptr, false);
        int matched = 0;
        if (!k.is_discarded() && k.contains("knobs"))
            for (const auto& kn : k["knobs"])
                for (const auto& p : pr.patches)
                    if (kn.value("id", std::string()) == p.knob && kn.value("field", std::string()) == p.path &&
                        kn.value("collection", std::string()) == "gp_positioningformation_attackinglinelengthwidth" &&
                        std::strtoul(kn.value("offset", std::string()).c_str(), nullptr, 0) == p.offset && std::fabs(kn.value("default", -1.0) - p.expect) < 1e-9)
                        ++matched;
        CHECK(matched == 2, fmt("both knobs agree with knobs_6AC07E31.json (offset, field, default): %d", matched));
    });

    run_case("attrib ebx: all or nothing - another install value, another type, another asset name, a pointer slot, a bad value: nothing is written", [] {
        const auto orig = unhex(kLLWRaw);
        std::string why;
        std::vector<std::string> ch;
        auto raw = orig;
        // the first patch fits, the second expects another value: neither is written
        std::vector<Patch> ps = {mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::F32, 0.5, true, 1.0),
                                 mk(kLLWAsset, kLLWSig, "LineLengthModifierForBalanced", 0x30, Kind::F32, 0.4, true, 0.9)};
        CHECK(!patch_ebx(raw, kLLWAsset, ps, &ch, &why) && raw == orig && why.find("holds 0.84") != std::string::npos, "expect 0.9 vs 0.84: refused, unchanged: " + why);
        CHECK(!patch_ebx(raw, kLLWAsset, {mk(kLLWAsset, kActorSig, "lineWidthModifier", 0x28, Kind::F32, 0.5)}, &ch, &why) && raw == orig, "another type sig: refused: " + why);
        CHECK(!patch_ebx(raw, kLLWAsset, {mk(kLLWAsset, kLLWSig, "Name", 0x18, Kind::F32, 0.5)}, &ch, &why) && raw == orig && why.find("pointer slot") != std::string::npos,
              "a value written over the Name pointer: refused: " + why);
        CHECK(!patch_ebx(raw, kLLWAsset, {mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::F32, std::nan(""))}, &ch, &why) && raw == orig, "NaN: refused: " + why);
        CHECK(!patch_ebx(raw, kLLWAsset, {mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x400, Kind::F32, 0.5)}, &ch, &why) && raw == orig, "an offset past the data: refused: " + why);
        CHECK(!patch_ebx(raw, kLLWAsset, {mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::F32, 0.5), mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::F32, 0.6)}, &ch, &why) &&
                  raw == orig,
              "the same value twice: refused: " + why);
        CHECK(!patch_ebx(raw, kLLWAsset, {mk(kActorAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::F32, 0.5)}, &ch, &why) && raw == orig, "no patch for this asset: refused: " + why);
        // the bytes of one asset under another asset's name: the Name check refuses them
        const std::string other = "fifa/attribulator/gameplay/groups/gp_positioningformation/gp_positioningformation_defensivelinelengthwidth_runtime";
        CHECK(!patch_ebx(raw, other, {mk(other.c_str(), kLLWSig, "lineWidthModifier", 0x28, Kind::F32, 0.5, true, 1.0)}, &ch, &why) && raw == orig && why.find("named") != std::string::npos,
              "the defensive group's patch on the attacking group's bytes: refused by Name: " + why);
        // integers and switches: range checks
        auto r2 = orig;
        CHECK(!patch_ebx(r2, kLLWAsset, {mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::I32, 1.5)}, &ch, &why) && r2 == orig, "1.5 into an i32: refused");
        CHECK(!patch_ebx(r2, kLLWAsset, {mk(kLLWAsset, kLLWSig, "lineWidthModifier", 0x28, Kind::Bool, 2)}, &ch, &why) && r2 == orig, "2 into a bool: refused");
    });

    run_case("attrib ebx: arrays - gp_actor_action StopDistanceFn[3] 7.3 -> 7.0 through the pointer slot; out of range and a value slot refused", [] {
        const auto orig = unhex(kActorRaw);
        std::string why;
        std::vector<std::string> ch;
        EbxView v;
        CHECK(parse_ebx(orig.data(), orig.size(), v, &why) && v.type_sigs == std::vector<uint32_t>({kActorSig}), "gp_actor_action parses: " + why);
        auto raw = orig;
        CHECK(patch_ebx(raw, kActorAsset, {mk(kActorAsset, kActorSig, "StopDistanceFn[3]", 40, Kind::ArrF32, 7.0, true, 7.3)}, &ch, &why), "array element patched: " + why);
        // the array's data: slot P+16+40 = 0x58, +492 -> 0x244
        CHECK(f32_at(raw, 0x244 + 12) == 7.0f && f32_at(raw, 0x244 + 8) == 5.0f && diff_bytes(raw, orig) <= 4, "element 3 only");
        raw = orig;
        CHECK(patch_ebx(raw, kActorAsset, {mk(kActorAsset, kActorSig, "ACTION_IntensityToSpeed[10]", 80, Kind::ArrF32, 0.95, true, 1.0)}, &ch, &why), "the last of 11: " + why);
        raw = orig;
        CHECK(!patch_ebx(raw, kActorAsset, {mk(kActorAsset, kActorSig, "StopDistanceFn[6]", 40, Kind::ArrF32, 1.0)}, &ch, &why) && raw == orig && why.find("out of range") != std::string::npos,
              "index 6 of 6: refused: " + why);
        CHECK(!patch_ebx(raw, kActorAsset, {mk(kActorAsset, kActorSig, "RunnerAcceleration[0]", 96, Kind::ArrF32, 1.0)}, &ch, &why) && raw == orig, "a float field as an array: refused: " + why);
        CHECK(patch_ebx(raw, kActorAsset,
                        {mk(kActorAsset, kActorSig, "RunnerAcceleration", 96, Kind::F32, 40.0, true, 36.0), mk(kActorAsset, kActorSig, "LowIntensityMoveMode", 196, Kind::Bool, 1, true, 0)}, &ch,
                        &why) &&
                  f32_at(raw, 0x30 + 96) == 40.0f && raw[0x30 + 196] == 1,
              "a float and a switch: " + why);
    });

    run_case("attrib ebx: curves - playoffsides DefDepthThresholdForMaxPushup.Points[1] (X 0.7, Y 0.5): a point's X and Y; the field must point to an instance", [] {
        const auto orig = unhex(kOffsideRaw);
        std::string why;
        std::vector<std::string> ch;
        auto raw = orig;
        CHECK(patch_ebx(raw, kOffsideAsset,
                        {mk(kOffsideAsset, kOffsideSig, "DefDepthThresholdForMaxPushup.Points[1].Y", 72, Kind::Curve, 0.6, true, 0.5),
                         mk(kOffsideAsset, kOffsideSig, "DefDepthThresholdForMaxPushup.Points[2].Y", 72, Kind::Curve, 0.95, true, 0.9),
                         mk(kOffsideAsset, kOffsideSig, "PossessorInterceptDistBuffer.Points[0].Y", 40, Kind::Curve, -2.0, true, -3.0),
                         mk(kOffsideAsset, kOffsideSig, "DefDepthThresholdForMaxPushup.Points[0].X", 72, Kind::Curve, 0.6, true, 0.600547)},
                        &ch, &why),
              "three Ys (the inventory's curve values) and an X: " + why);
        CHECK(ch.size() == 4 && diff_bytes(raw, orig) <= 16, fmt("four floats (%zu bytes changed)", diff_bytes(raw, orig)));
        raw = orig;
        CHECK(!patch_ebx(raw, kOffsideAsset, {mk(kOffsideAsset, kOffsideSig, "DefDepthThresholdForMaxPushup.Points[3].Y", 72, Kind::Curve, 0.6)}, &ch, &why) && raw == orig,
              "point 3 of 3: refused: " + why);
        CHECK(!patch_ebx(raw, kOffsideAsset, {mk(kOffsideAsset, kOffsideSig, "MinDepthToUseKickInfo.Points[0].Y", 144, Kind::Curve, 0.6)}, &ch, &why) && raw == orig,
              "a float field as a curve: refused: " + why);
        CHECK(!patch_ebx(raw, kOffsideAsset, {mk(kOffsideAsset, kOffsideSig, "MinDepthToUseKickInfo", 144, Kind::F32, 0.7, true, 0.6)}, &ch, &why) && raw == orig,
              "expect 0.6 where the install has 0.695: refused: " + why);
        CHECK(patch_ebx(raw, kOffsideAsset, {mk(kOffsideAsset, kOffsideSig, "MinDepthToUseKickInfo", 144, Kind::F32, 0.7, true, 0.695)}, &ch, &why), "the scalar beside the curves: " + why);
    });

    run_case("attrib ebx: paths and the preset file's checks", [] {
        PathSpec ps;
        std::string why;
        CHECK(parse_path("lineWidthModifier", Kind::F32, ps, &why) && ps.field == "lineWidthModifier" && ps.index == -1, "a field");
        CHECK(parse_path("ACTION_IntensityToSpeed[10]", Kind::ArrF32, ps, &why) && ps.field == "ACTION_IntensityToSpeed" && ps.index == 10, "an array element");
        CHECK(parse_path("StopDistanceFn.Points[2].Y", Kind::Curve, ps, &why) && ps.field == "StopDistanceFn" && ps.index == 2 && ps.axis == 'Y', "a curve point");
        CHECK(!parse_path("x[1]", Kind::F32, ps, &why) && !parse_path("x", Kind::ArrF32, ps, &why) && !parse_path("x[]", Kind::ArrF32, ps, &why) &&
                  !parse_path("x[1]y", Kind::ArrF32, ps, &why) && !parse_path("x.Points[1].Z", Kind::Curve, ps, &why) && !parse_path("x.Points[a].Y", Kind::Curve, ps, &why) &&
                  !parse_path("", Kind::F32, ps, &why),
              "bad paths are refused");
        const std::string a = kLLWAsset;
        const std::string head_ok = R"("schema": "turbo-attrib-preset/1", "build": "6AC07E31-2145C000")";
        auto doc = [&](const std::string& patch, const std::string& head) { return "{" + head + R"(, "patches": [)" + patch + "]}"; };
        const std::string good = R"({"asset": ")" + a + R"(", "type_sig": 522591529, "path": "lineWidthModifier", "offset": "0x28", "kind": "f32", "value": 0.5})";
        Preset p;
        CHECK(parse_preset(doc(good, head_ok), p, &why) && p.patches.size() == 1 && p.patches[0].offset == 0x28 && !p.patches[0].has_expect && p.mode == Mode::Patch, "minimal: " + why);
        CHECK(parse_preset(doc(good, head_ok + R"(, "mode": "observe")"), p, &why) && p.mode == Mode::Observe, "observe mode");
        CHECK(!parse_preset(doc(good, R"("schema": "x", "build": "6AC07E31-2145C000")"), p, &why), "another schema: refused");
        CHECK(!parse_preset(doc(good, R"("schema": "turbo-attrib-preset/1")"), p, &why), "no build: refused");
        CHECK(!parse_preset(doc(good, head_ok + R"(, "mode": "write")"), p, &why), "unknown mode: refused");
        auto with = [&](const std::string& from, const std::string& to) {
            std::string s = good;
            s.replace(s.find(from), from.size(), to);
            return doc(s, head_ok);
        };
        CHECK(!parse_preset(with("\"0x28\"", "\"0x10\""), p, &why), "offset below 0x20: refused");
        CHECK(!parse_preset(with("\"0x28\"", "\"28h\""), p, &why), "offset not a number: refused");
        CHECK(!parse_preset(with("\"f32\"", "\"f64\""), p, &why), "unknown kind: refused");
        CHECK(!parse_preset(with("fifa/attribulator/", "fifa/kits/"), p, &why), "not an attribulator asset: refused");
        CHECK(!parse_preset(with("522591529", "-1"), p, &why), "negative type sig: refused");
        CHECK(!parse_preset(with("\"value\": 0.5", "\"value\": \"0.5\""), p, &why), "a string value: refused");
        CHECK(!parse_preset(with("\"path\": \"lineWidthModifier\"", "\"path\": \"lineWidthModifier[2]\""), p, &why), "an array path on a float: refused");
        CHECK(parse_preset(with("\"offset\": \"0x28\"", "\"offset\": 40"), p, &why) && p.patches[0].offset == 40, "a decimal offset: " + why);
        CHECK(!parse_preset("{", p, &why) && !parse_preset(doc("", head_ok), p, &why), "not JSON / no patches: refused");
    });

    run_case("attrib ebx: table + runtime on the real compressed block - patch mode overwrites, observe mode only sees, one 'attrib block seen' line per asset", [] {
        const auto stream = unhex(kLLWComp);
        const auto rawv = unhex(kLLWRaw);
        CHECK(stream.size() == 279, "279 bytes in the cas (8-byte block header + 271)");
        std::vector<kitswap::Block> bl;
        CHECK(kitswap::block_headers(stream, bl) && bl.size() == 1 && bl[0].ctype == 0x19 && bl[0].csize == 271 && bl[0].dsize == 412, "one Oodle block 271 -> 412");
        const uint8_t* payload = stream.data() + 8;
        kitswap::OodleFn oodle = [&](const uint8_t* s, size_t n, uint8_t* d, size_t dn) {
            if (n != 271 || dn != rawv.size() || std::memcmp(s, payload, n) != 0) return false;
            std::memcpy(d, rawv.data(), dn);
            return true;
        };
        Preset pr;
        std::string why;
        CHECK(parse_preset(read_text(sample_preset()), pr, &why), "preset: " + why);

        // patch mode
        kitswap::TableBuilder tb;
        ItemInfo it;
        CHECK(add_asset(tb, stream, oodle, kLLWAsset, "Patch/Win32/fc/fcgame/fcgame.toc", pr.patches, Mode::Patch, it, &why) && it.patch && it.csize == 271 && it.dsize == 412 &&
                  it.what.find("patched: LineLengthModifierForBalanced 0.84 -> 0.4") == 0,
              "registered as a patch: " + it.what + why);
        Runtime rt;
        CHECK(!rt.ready() && rt.take_seen_lines().empty(), "nothing before the table");
        std::vector<uint8_t> out = rawv;
        CHECK(rt.on_decompressed(payload, 271, out.data(), out.size(), 412) == 0 && out == rawv, "before the table: passed through");
        rt.publish(tb.finish(nullptr), {it});
        CHECK(rt.ready(), "published");
        CHECK(rt.on_decompressed(payload, 271, out.data(), out.size(), 411) == 1 && out == rawv, "the original returned another size: recognised, not overwritten");
        CHECK(rt.on_decompressed(payload, 271, out.data(), out.size(), 412) == 2 && f32_at(out, 0x58) == 0.5f && f32_at(out, 0x60) == 0.4f && diff_bytes(out, rawv) <= 8,
              "overwritten: width 0.5, length 0.4");
        auto other = stream;
        other[8 + 100] ^= 1;
        std::vector<uint8_t> o2 = rawv;
        CHECK(rt.on_decompressed(other.data() + 8, 271, o2.data(), o2.size(), 412) == 0 && o2 == rawv, "a block one bit off: not ours");
        CHECK(rt.on_decompressed(payload, 270, o2.data(), o2.size(), 412) == 0 && rt.on_decompressed(payload, 271, o2.data(), 413, 413) == 0, "other sizes: not ours");
        auto lines = rt.take_seen_lines();
        CHECK(lines.size() == 1 && lines[0].rfind("attrib block seen 20", 0) == 0 && lines[0].find(kLLWAsset) != std::string::npos &&
                  lines[0].find("Patch/Win32/fc/fcgame/fcgame.toc; patched:") != std::string::npos,
              "one seen line: " + (lines.empty() ? std::string("none") : lines[0]));
        CHECK(rt.take_seen_lines().empty(), "reported once");
        CHECK(rt.hits() == 2 && rt.patched() == 1, fmt("counters: %llu recognised, %llu overwritten", (unsigned long long)rt.hits(), (unsigned long long)rt.patched()));
        rt.set_killed(true);
        o2 = rawv;
        CHECK(rt.on_decompressed(payload, 271, o2.data(), o2.size(), 412) == 0 && o2 == rawv, "kill switch: passed through");
        const std::string log = rt.log_text("head");
        CHECK(log.find("table published") != std::string::npos && log.find("  patch  " + std::string(kLLWAsset)) != std::string::npos && log.find("seen 2") != std::string::npos &&
                  log.find("kill switch") != std::string::npos,
              "the log: " + log);

        // observe mode: recognised, never written
        kitswap::TableBuilder tb2;
        ItemInfo it2;
        CHECK(add_asset(tb2, stream, oodle, kLLWAsset, "Patch/Win32/fc/fcgame/fcgame.toc", pr.patches, Mode::Observe, it2, &why) && !it2.patch && it2.what == "watch only: observe mode",
              "observe");
        Runtime rt2;
        rt2.publish(tb2.finish(nullptr), {it2});
        o2 = rawv;
        CHECK(rt2.on_decompressed(payload, 271, o2.data(), o2.size(), 412) == 1 && o2 == rawv, "observe: recognised, unchanged");
        lines = rt2.take_seen_lines();
        CHECK(lines.size() == 1 && lines[0].find("watch only: observe mode") != std::string::npos, "observe: the seen line says watch only");

        // a patch that does not fit (another install value): watch only, with the reason
        auto bad = pr.patches;
        bad[0].expect = 0.9;
        kitswap::TableBuilder tb3;
        ItemInfo it3;
        CHECK(add_asset(tb3, stream, oodle, kLLWAsset, "Patch/Win32/fc/fcgame/fcgame.toc", bad, Mode::Patch, it3, &why) && !it3.patch &&
                  it3.what.find("watch only: LineLengthModifierForBalanced") == 0,
              "a refused patch registers the copy watch only: " + it3.what);
        // block streams that are not one Oodle block
        const std::vector<uint8_t> stored = {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 1, 2, 3, 4};
        kitswap::TableBuilder tb4;
        CHECK(!add_asset(tb4, stored, oodle, kLLWAsset, "t", pr.patches, Mode::Patch, it3, &why), "a stored block: refused: " + why);
        auto two = stream;
        two.insert(two.end(), stream.begin(), stream.end());
        CHECK(!add_asset(tb4, two, oodle, kLLWAsset, "t", pr.patches, Mode::Patch, it3, &why) && why.find("2 blocks") != std::string::npos, "two blocks: refused: " + why);
        auto cut = stream;
        cut.pop_back();
        CHECK(!add_asset(tb4, cut, oodle, kLLWAsset, "t", pr.patches, Mode::Patch, it3, &why), "a cut stream: refused");
    });

    run_case("attrib ebx: the locations cache - written, read back, refused for another signature or another asset list", [] {
        const fs::path dir = test_tmp_dir("attrib_ebx");
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path f = dir / "attrib_ebx_locs.json";
        const std::vector<std::string> names = {kLLWAsset};
        std::vector<std::vector<AssetLoc>> found(1);
        AssetLoc a;
        a.toc = "Patch/Win32/fc/fcgame/fcgame.toc";
        a.loc.cas = "0000a3a00de30001";
        a.loc.off = 148793182;
        a.loc.size = 279;
        found[0].push_back(a);
        CHECK(save_locs(f, "sig1", names, found), "saved");
        std::vector<std::vector<AssetLoc>> back;
        CHECK(load_locs(f, "sig1", names, back) && back.size() == 1 && back[0].size() == 1 && back[0][0].loc.cas == a.loc.cas && back[0][0].loc.off == a.loc.off &&
                  back[0][0].loc.size == 279 && back[0][0].toc == a.toc,
              "read back");
        CHECK(!load_locs(f, "sig2", names, back), "another toc signature: searched again");
        CHECK(!load_locs(f, "sig1", {kLLWAsset, kActorAsset}, back), "an asset the cache never looked for: searched again");
        CHECK(!load_locs(dir / "missing.json", "sig1", names, back), "no file");
        CHECK(attrib_tocs().size() == 2 && attrib_tocs()[0].rfind("Patch/", 0) == 0, "Patch layer first");
    });

    run_case("attrib ebx: clock text and the process runtime pointer", [] {
        CHECK(clock_text(0) == "-", "0 = never");
        const std::string t = clock_text(1760000000123ULL);
        CHECK(t.size() == 23 && t[4] == '-' && t[10] == ' ' && t[19] == '.' && t.substr(20) == "123", "YYYY-MM-DD HH:MM:SS.mmm: " + t);
        CHECK(active_runtime() == nullptr, "no runtime in the tests");
        Runtime r;
        set_active_runtime(&r);
        CHECK(active_runtime() == &r, "set");
        set_active_runtime(nullptr);
    });
}

}  // namespace attrib_ebx_test
