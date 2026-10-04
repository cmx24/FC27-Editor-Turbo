// FC 27 LE Turbo GUI - miniface from the game's 3D model: platform-independent part (see player_capture.h)
#include "player_capture.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace turbo {
namespace capture {

// ---------------------------------------------------------------- PlayerDesc
int32_t PlayerDesc::i32(size_t off) const {
    int32_t v = 0;
    if (off + 4 <= kPlayerDescSize) std::memcpy(&v, b + off, 4);
    return v;
}

void PlayerDesc::set_i32(size_t off, int32_t v) {
    if (off + 4 <= kPlayerDescSize) std::memcpy(b + off, &v, 4);
}

// The descriptor the game's own builders write (manager portrait 0x147D94218, pro card 0x1485DF1AC):
//   +00 id        +04 -1        +08 second id  +0C -1   +10 -1   +14..+1F 0
//   +20 -1        +24 1         +28 0          +2C 0    +30..+4B 0
//   +4C -1        +50 1         +54 -1         +58 -1   +5C..+63 0
//   +64 byte flag (pro card 1, manager 0)      +65..+67 0      +68 byte flag (manager id 9999)   +69..+6B 0
PlayerDesc default_desc(int32_t id, int32_t second_id, bool flag64, bool flag68) {
    PlayerDesc d;
    d.set_i32(0x00, id);
    d.set_i32(0x04, -1);
    d.set_i32(0x08, second_id);
    d.set_i32(0x0C, -1);
    d.set_i32(0x10, -1);
    d.set_i32(0x20, -1);
    d.set_i32(0x24, 1);
    d.set_i32(0x4C, -1);
    d.set_i32(0x50, 1);
    d.set_i32(0x54, -1);
    d.set_i32(0x58, -1);
    d.set_flag64(flag64);
    d.set_flag68(flag68);
    return d;
}

std::string describe_desc(const PlayerDesc& d) {
    std::string s;
    char buf[48];
    for (size_t off = 0; off + 4 <= kPlayerDescSize; off += 4) {
        int32_t v = d.i32(off);
        if (v == 0) continue;
        std::snprintf(buf, sizeof(buf), "%s+%02zX=%d", s.empty() ? "" : " ", off, v);
        s += buf;
    }
    if (s.empty()) s = "(all zero)";
    return s;
}

std::string hex_bytes(const uint8_t* p, size_t n) {
    static const char* h = "0123456789ABCDEF";
    std::string s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        if (i) s += (i % 16 == 0) ? '|' : ' ';
        s += h[p[i] >> 4];
        s += h[p[i] & 15];
    }
    return s;
}

// ---------------------------------------------------------------- cameras / plan
const std::vector<Camera>& cameras() {
    static const std::vector<Camera> k = {
        {"Portrait (mode 0, extra 0: cards and manager heads)", 0, 0},
        {"Large slice (mode 1, extra 0)", 1, 0},
        {"Avatar outfit (mode 1, extra 3)", 1, 3},
        {"Mode 0, extra 3", 0, 3},
    };
    return k;
}

Plan plan_request(const Request& r, const Template* t) {
    Plan p;
    const bool from_template = r.use_template && t && t->learned;
    // the game's manager-head builder 0x147D94218 (docs/re/manager_rules.md section 6): +0x64 = 0 for a staff head,
    // +0x68 = (id == 9999, the user's created avatar), second id = the manager's team id
    const bool avatar = r.manager && r.id == kUserAvatarHeadId;
    if (from_template) {
        p.desc = t->desc;
        p.desc.set_id(r.id);
        if (r.second_id >= 0) p.desc.set_second_id(r.second_id);
        if (r.manager) {  // a template learned from a player request: make it a staff head
            p.desc.set_flag64(false);
            p.desc.set_flag68(avatar);
        }
        p.note = "learned descriptor";
    } else {
        p.desc = default_desc(r.id, r.second_id, !r.manager, avatar);
        p.note = "default descriptor";
    }
    const auto& cams = cameras();
    if (r.camera == kCameraLearned && t && t->learned) {
        p.mode = t->mode;
        p.extra = t->extra;
        p.note += ", learned mode/extra";
    } else {
        size_t ci = (r.camera >= 0 && static_cast<size_t>(r.camera) < cams.size()) ? static_cast<size_t>(r.camera) : 0;
        p.mode = cams[ci].mode;
        p.extra = cams[ci].extra;
    }
    if (r.mode_override >= 0) p.mode = r.mode_override;
    if (r.extra_override >= 0) p.extra = r.extra_override;
    return p;
}

// ---------------------------------------------------------------- delegate
void* delegate_manager(void* to, void* from, int op) {
    if ((op == kMgrCopy || op == kMgrMove) && to && from && to != from) std::memcpy(to, from, sizeof(void*) * 2);
    return nullptr;
}

Delegate make_delegate(void* context, void* invoker) {
    Delegate d;
    d.storage[0] = context;
    d.storage[1] = nullptr;
    d.manager = reinterpret_cast<void*>(&delegate_manager);
    d.invoker = invoker;
    return d;
}

void* delegate_context(const void* storage) {
    void* ctx = nullptr;
    if (storage) std::memcpy(&ctx, storage, sizeof(ctx));
    return ctx;
}

// ---------------------------------------------------------------- picture bytes
int raw_square_side(size_t size) {
    if (size == 0 || size % 4 != 0) return 0;
    size_t px = size / 4;
    auto side = static_cast<size_t>(std::llround(std::sqrt(static_cast<double>(px))));
    for (size_t s = side > 0 ? side - 1 : 0; s <= side + 1; ++s)
        if (s > 0 && s * s == px && s <= static_cast<size_t>(kMaxImageSide)) return static_cast<int>(s);
    return 0;
}

static bool is_png(const uint8_t* p, size_t n) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return n >= 8 && std::memcmp(p, sig, 8) == 0;
}

bool decode_slice(const uint8_t* data, size_t size, Rgba& out, std::string* format, std::string* err) {
    if (!data || size < 16) {
        if (err) *err = "no picture bytes";
        return false;
    }
    std::vector<uint8_t> bytes(data, data + size);
    std::string e;
    if (size >= 4 && std::memcmp(data, "DDS ", 4) == 0) {
        DdsFormat f;
        std::string fe;
        bool known = parse_dds_format(bytes, f, &fe);
        if (decode_dds(bytes, out, &e)) {
            if (format) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "DDS %s %dx%d", known ? f.name() : "?", out.w, out.h);
                *format = buf;
            }
            return true;
        }
        if (err) *err = "DDS: " + e;
        return false;
    }
    if (is_png(data, size) || (size > 2 && data[0] == 'B' && data[1] == 'M') || (size > 3 && data[0] == 0xFF && data[1] == 0xD8)) {
        if (decode_image(bytes, out, &e)) {
            if (format) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s %dx%d", is_png(data, size) ? "PNG" : data[0] == 'B' ? "BMP" : "JPG", out.w, out.h);
                *format = buf;
            }
            return true;
        }
        if (err) *err = e;
        return false;
    }
    // raw 8-bit, 4 channels, square
    if (int side = raw_square_side(size)) {
        out.w = out.h = side;
        out.px.assign(data, data + size);
        // the game's textures are usually BGRA: a plain-background portrait has far more "blue" than "red" in BGRA order
        // when the background is skin-coloured, so keep RGBA (the editor's background removal does not care) but note it
        if (format) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "raw RGBA %dx%d", side, side);
            *format = buf;
        }
        return true;
    }
    // last resort: stb_image sniffing (TGA has no magic)
    if (decode_image(bytes, out, &e)) {
        if (format) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "image %dx%d", out.w, out.h);
            *format = buf;
        }
        return true;
    }
    if (err) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "unknown picture format (%zu bytes, starts %02X %02X %02X %02X)", size, data[0], data[1], data[2], data[3]);
        *err = buf;
    }
    return false;
}

}  // namespace capture
}  // namespace turbo
