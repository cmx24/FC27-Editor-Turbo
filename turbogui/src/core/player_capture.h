// FC 27 LE Turbo GUI - miniface from the game's 3D model: the platform-independent part of the player-capture feature
// (docs/re/player_capture.md). The game renders a player (or a manager's head) through its PlayerCaptureController and
// hands the picture bytes to a callback; the Windows host (src/win/player_capture_win.cpp) calls the game, this file
// holds what can be built and tested without the game:
//   * PlayerDesc: the 0x6C-byte descriptor the game wants per player (layout from the game's own builders);
//   * Template: what a detour on PlayerCaptureController::Start learns from the game's own captures;
//   * Delegate: the game's eastl::function-shaped callback (16-byte storage, manager, invoker) with a manager Turbo owns;
//   * decode_slice: the picture bytes a capture delivers (DDS / PNG / raw RGBA) -> Rgba;
//   * CaptureService: the interface the Miniface editor talks to (the host implements it; tests use a fake).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "image.h"

namespace turbo {
namespace capture {

// ---------------------------------------------------------------- PlayerDesc (0x6C bytes)
// Layout as the game's own builders fill it (cm avatar manager portrait 0x147D94218, pro card 0x1485DF1AC on build
// 1.0.140.64835; docs/re/player_capture.md 2.3). Only +0x00 (the id the picture is for, passed back to the slot callback)
// is certain; the rest is the game's default descriptor.
constexpr size_t kPlayerDescSize = 0x6C;
constexpr size_t kMaxPlayersPerRequest = 6;

struct PlayerDesc {
    uint8_t b[kPlayerDescSize] = {};
    int32_t i32(size_t off) const;
    void set_i32(size_t off, int32_t v);
    int32_t id() const { return i32(0); }
    void set_id(int32_t v) { set_i32(0, v); }
    // +0x08: second id (the pro card stores a service value there, the manager builder its object's +0x4C) [M: team id]
    int32_t second_id() const { return i32(0x08); }
    void set_second_id(int32_t v) { set_i32(0x08, v); }
    // +0x64 byte: 1 in the pro-card builder, 0 in the manager builder [L: "a player, not a staff head"]
    bool flag64() const { return b[0x64] != 0; }
    void set_flag64(bool v) { b[0x64] = v ? 1 : 0; }
    // +0x68 byte: the manager builder sets it when the id is 9999 (the user's created avatar)
    bool flag68() const { return b[0x68] != 0; }
    void set_flag68(bool v) { b[0x68] = v ? 1 : 0; }
};

// The game's default descriptor: ints -1/0/1 where the builders put them, id at +0, second id at +8
PlayerDesc default_desc(int32_t id, int32_t second_id, bool flag64, bool flag68);
// One line per non-zero dword / interesting byte: "+00=1234 +04=-1 ..." (for the log and the Status text)
std::string describe_desc(const PlayerDesc& d);
std::string hex_bytes(const uint8_t* p, size_t n);

// ---------------------------------------------------------------- what the Start detour learns
struct Template {
    bool learned = false;
    PlayerDesc desc;          // first player of the game's own request
    int count = 0;            // players in that request
    int type = 0;             // controller +0x18
    int mode = 0;             // controller +0x40 (1 = 0x2FAF08-byte slices, else 0x2F880)
    int extra = 0;            // controller +0x44
    std::string name;         // controller +0xA8 (UI image registry name, empty on the callback path)
    std::string first_image;  // first entry of the names vector (+0x48), when any
    std::string source;       // where it was seen ("game", "turbo")
    uint64_t seen_at_ms = 0;
};

// Camera / render options. The game's meaning of mode / extra is not recovered (docs/re/player_capture.md 6): the
// presets are the (mode, extra) pairs the game's own flows use; "learned" takes the template's values.
struct Camera {
    const char* label;
    int mode;
    int extra;
};
const std::vector<Camera>& cameras();
constexpr int kCameraLearned = -1;  // use the template's mode / extra (falls back to preset 0)

// Head id the game's manager-head builder treats as the user's created avatar (+0x68 = 1)
constexpr int32_t kUserAvatarHeadId = 9999;

struct Request {
    int32_t id = 0;            // player id (players) or head id (managers)
    int32_t second_id = -1;    // team id (players: their club; managers: the club they manage, like the game's builder), -1 = none
    bool manager = false;
    int camera = 0;            // index into cameras(), or kCameraLearned
    int mode_override = -1;    // >= 0: explicit mode (advanced)
    int extra_override = -1;   // >= 0: explicit extra
    bool use_template = true;  // start from the learned descriptor when there is one (else the default one)
    std::string label;         // for messages ("Bukayo Saka")
};

// The descriptor and (mode, extra) a request resolves to, given the template
struct Plan {
    PlayerDesc desc;
    int mode = 0;
    int extra = 0;
    std::string note;  // "learned descriptor" / "default descriptor"
};
Plan plan_request(const Request& r, const Template* t);

// ---------------------------------------------------------------- the game's callback object
// eastl::function-shaped: 16 bytes of functor storage, a manager called as mgr(to, from, op) with op 0 = destruct,
// 1 = copy, 2 = move, and an invoker called with the arguments followed by a pointer to the storage
// (PlayerCaptureController_Request_B copies it with 0x1470DCD08 and destroys it with 0x1470DD9E4).
struct Delegate {
    void* storage[2] = {nullptr, nullptr};
    void* manager = nullptr;
    void* invoker = nullptr;
};
static_assert(sizeof(Delegate) == 0x20, "Delegate must be 0x20 bytes");
enum : int { kMgrDestruct = 0, kMgrCopy = 1, kMgrMove = 2 };
// Turbo's manager: copies / moves the 16-byte storage, destructs nothing, returns nullptr for the query ops
void* delegate_manager(void* to, void* from, int op);
// Build a delegate whose storage holds `context` (a pointer Turbo owns)
Delegate make_delegate(void* context, void* invoker);
void* delegate_context(const void* storage);

// eastl::vector<PlayerDesc> as the game reads it: {begin, end, capacity, allocator}; Start only reads begin / end
struct DescVector {
    const PlayerDesc* begin = nullptr;
    const PlayerDesc* end = nullptr;
    const PlayerDesc* cap = nullptr;
    void* allocator = nullptr;
};

// ---------------------------------------------------------------- picture bytes
// A capture hands Turbo `size` bytes. The game wraps them as an image stream for its UI image registry, so they are a
// picture file in memory (DDS expected; PNG / BMP / TGA also understood) or, failing that, raw 8-bit RGBA / BGRA of a
// square picture. `format` receives what was recognised ("DDS DXT5 256x256", "raw RGBA 540x540", ...).
bool decode_slice(const uint8_t* data, size_t size, Rgba& out, std::string* format = nullptr, std::string* err = nullptr);
// Sizes the game's slices have (controller mode 0 / 1): a hint for the raw-buffer guess
constexpr size_t kSliceSizeMode0 = 0x2F880, kSliceSizeMode1 = 0x2FAF08;
// Raw-buffer guess: w*h*4 == size with a square or a few known shapes; 0 when nothing fits
int raw_square_side(size_t size);

// ---------------------------------------------------------------- the service the editor uses
struct Status {
    bool installed = false;    // hooks resolved and installed (feature is usable at all)
    bool available = false;    // the game can capture right now (gates pass, not busy)
    std::string reason;        // why not (first failing check), or a short summary
    bool learned = false;      // a template was learned from the game's own captures
    int seen = 0;              // captures the game did by itself (template source)
    int done = 0, failed = 0;  // Turbo's own captures
    bool busy = false;         // a request is in flight
    std::string busy_label;
    double busy_for = 0.0;     // seconds
    std::string last_format;   // of the last picture received
    std::string dispatch;      // "hook" / "lua" / "" (how the request reaches the game thread)
};

struct Result {
    bool ok = false;
    std::string label;
    std::string error;
    std::string format;
    Rgba image;
    size_t bytes = 0;
    int32_t id = 0;
};

class CaptureService {
public:
    virtual ~CaptureService() = default;
    virtual Status status() = 0;
    // Queue one capture; false (and err) when refused (unavailable, busy, bad id)
    virtual bool request(const Request& r, std::string* err) = 0;
    // One finished capture (success or failure) if any arrived since the last poll
    virtual bool poll(Result& out) = 0;
    // Forget the request in flight (the game may still answer; the answer is dropped)
    virtual void cancel() = 0;
    virtual const Template* learned() = 0;
};

}  // namespace capture
}  // namespace turbo
