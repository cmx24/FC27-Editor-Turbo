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
#include <functional>
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

// One head of a batch request: what a single Request's id / second_id / manager / label say, per head
struct BatchEntry {
    int32_t id = 0;
    int32_t second_id = -1;
    bool manager = false;
    std::string label;
};

struct Request {
    int32_t id = 0;            // player id (players) or head id (managers)
    int32_t second_id = -1;    // team id (players: their club; managers: the club they manage, like the game's builder), -1 = none
    bool manager = false;
    int camera = 0;            // index into cameras(), or kCameraLearned
    int mode_override = -1;    // >= 0: explicit mode (advanced)
    int extra_override = -1;   // >= 0: explicit extra
    bool use_template = true;  // start from the learned descriptor when there is one (else the default one)
    std::string label;         // for messages ("Bukayo Saka")
    // A batch (the real-face chooser's 3D heads): when not empty the game renders these heads in ONE request (the
    // controller renders up to kMaxPlayersPerRequest per batch, docs/re/player_capture.md 2.6), each planned like a single
    // request with the camera / overrides / use_template above; id, second_id, manager above are then unused. The
    // service hands back one Result per picture (Result::id), then a failed Result for each head that got none.
    std::vector<BatchEntry> batch;
};

// The descriptor and (mode, extra) a request resolves to, given the template
struct Plan {
    PlayerDesc desc;
    int mode = 0;
    int extra = 0;
    std::string note;  // "learned descriptor" / "default descriptor"
};
Plan plan_request(const Request& r, const Template* t);
// The heads a request renders: its batch, or the single request as one entry
std::vector<BatchEntry> request_entries(const Request& r);
// One plan per entry, in order (all share the camera, so mode / extra are the same)
std::vector<Plan> plan_batch(const Request& r, const Template* t);
// Before a request goes to the game: 1..max_entries heads, every id > 0 and no id twice (the game hands each picture back
// with the id only). false and err when not.
bool check_request(const Request& r, size_t max_entries, std::string* err);
// "6 heads: Saka, Rice, Odegaard, ..." (or the single label) for logs and the status
std::string request_label(const Request& r);

// What a request waits for and what came back (the host's per-request book, tested natively). Not thread-safe: the
// host guards it with its mutex. Pictures are matched to entries by id (first entry with that id still waiting).
class BatchBook {
public:
    struct Picture {
        size_t entry = 0;
        std::vector<uint8_t> bytes;
    };
    BatchBook() = default;
    explicit BatchBook(std::vector<BatchEntry> entries);
    const std::vector<BatchEntry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }
    // a picture for `id` arrived: kept for the first entry with that id still waiting; -1 when none waits for it
    int add_picture(int32_t id, std::vector<uint8_t> bytes);
    // the game handed over something unusable for `id`: that entry fails with `why`; -1 when none waits for it
    int add_error(int32_t id, const std::string& why);
    size_t received() const { return received_; }  // entries that got a picture
    // the next picture not handed out yet
    bool take_picture(Picture& out);
    // the next entry to report as failed, once each: one with an error at any time, one still waiting only once `over`
    // (the done callback fired, the request failed or timed out). `why` = the entry's error ("" when it simply got none)
    bool take_failed(bool over, size_t& entry, std::string& why);
    bool all_handed() const;  // every entry has had its Result
    void drop_pictures();     // release pictures not handed out (cancel)

private:
    enum class St { Waiting, Picture, Error, Handed };
    std::vector<BatchEntry> entries_;
    std::vector<St> st_;
    std::vector<std::string> err_;
    std::vector<Picture> pics_;  // in arrival order, handed out from the front
    size_t received_ = 0;
};

// When the host's request in flight is over for Turbo: every head has had its Result AND the game's done callback fired (or
// the request failed); a request whose heads were all handed out but whose done callback never came is over (TimedOut)
// `timeout_s` after it ran. poll() ends a request when it hands out the last Result after the done callback; when the last
// picture went out BEFORE the done callback, the caller got its picture and stopped polling, so nothing ended the request:
// the 3D model tab showed "rendering... (52 s)" until Cancel (W2202, mode 1). status() and request() settle it with this.
enum class Settle { Running, Over, TimedOut };
Settle settle_request(bool all_handed, bool finished, bool failed, bool ran, double since_ran_s, double timeout_s);

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
    int max_batch = 1;         // heads one request may carry (Request::batch); 1 = single requests only
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

// ---------------------------------------------------------------- full-body pictures (2.17, research_20261017\FULLBODY_FAST_ROUTE.md)
// Mode 1 answered 1024x1024 PNG pictures on 2026-10-04 (player 208596, 0.5-0.6 s each, extra 0 and extra 3). Whether that
// picture shows the whole body is the Tester's check (route note section 5): NOT proven. Indices into cameras():
constexpr int kCameraBody = 1;     // "Large slice (mode 1, extra 0)"
constexpr int kCameraBodyKit = 2;  // "Avatar outfit (mode 1, extra 3)"

// A player's full-body picture request: the default descriptor (never the learned one: in a career the first game capture
// is the hub's avatar portrait, whose template once drew the avatar's head for every id), id = playerid, second id = his
// team (-1 = none), player flags. camera: kCameraBody or kCameraBodyKit (anything else -> kCameraBody). Label "player <id>"
// when none is given (the log lines then read "request player <id>: ...").
Request body_request(int32_t id, int32_t team, int camera = kCameraBody, const std::string& label = "");

// ---------------------------------------------------------------- Sequence: write -> picture -> save, one code at a time
// Runs a list of codes on ONE reference player (track T: one picture per body type; any other per-player code works the
// same). Per step:
//   manifest "pending" (attempt n) -> hooks.write(code) -> request(body_request) -> poll until his Result -> hooks.save
//   -> manifest "done" / "failed" -> next step
// One request in flight (the capture controller is a singleton); a request is sent only while status() says available and
// not busy (waiting at most step_timeout).
// The manifest text goes to hooks.write_manifest after EVERY change, so a crash (the game's, which takes Turbo with it)
// leaves "pending" on the code that was being drawn: resume() counts that as an attempt and marks the code "crashed" (never
// tried again) once it has max_attempts of them. hooks.restore runs once at the end, on cancel and on abort (too many
// failures in a row, or the manifest cannot be written); the reference's original code is kept in the manifest, so a run
// resumed after a crash restores HIS code, not the test code the crash left on him (original() after resume()).
// Not thread-safe: start / tick / cancel from one thread (Turbo's UI thread, like CaptureService::poll). While a sequence
// runs nothing else may poll the service (the faces card and the Miniface editor poll only for their own pending request).
enum class StepResult { Done, Wait, Failed };

struct SequenceStep {
    int64_t code = 0;
    std::string label;  // plain words for the manifest and the messages ("Tall and Stocky")
};

struct SequenceHooks {
    // put `code` on the reference player; called again on later ticks while it answers Wait (first = first call for this step)
    std::function<StepResult(int64_t code, bool first, std::string* err)> write;
    // keep the picture; *file = what the manifest records for it (e.g. a path relative to the manifest's folder)
    std::function<bool(int64_t code, const Result& r, std::string* file, std::string* err)> save;
    // put the reference's original code back (original()); called again on later ticks while it answers Wait
    std::function<StepResult(bool first, std::string* err)> restore;
    // persist the manifest text; false = the run aborts (a run that cannot record itself cannot be resumed safely)
    std::function<bool(const std::string& text, std::string* err)> write_manifest;
    std::function<double()> clock;       // seconds, monotonic (required)
    std::function<std::string()> stamp;  // wall-clock text for the manifest ("2026-10-17 22:05:31"); optional
};

struct SequenceOptions {
    int32_t player = 0;           // the reference player's id (> 0)
    int32_t team = -1;            // his team id (the descriptor's second id), -1 = none
    int camera = kCameraBody;     // kCameraBody or kCameraBodyKit
    int64_t original = -1;        // his own code before the run (kept in the manifest; -1 = unknown)
    double step_timeout = 25.0;   // seconds from the request to his picture (also the longest wait for a busy service)
    double write_timeout = 10.0;  // seconds a write / restore may answer Wait
    int max_attempts = 2;         // a code that crashed or failed this often is not tried again
    int max_failures_in_row = 3;  // the game stopped answering: stop (and restore) instead of writing every code
};

struct ManifestEntry {
    int64_t code = 0;
    std::string label;
    // "todo" (not tried), "pending" (recorded before its write: being drawn, or the run died here), "done", "failed",
    // "crashed" (pending when a run died, max_attempts times: never tried again), "cancelled" (stopped by the user)
    std::string state = "todo";
    int attempts = 0;
    std::string file;  // from hooks.save
    int w = 0, h = 0;
    size_t bytes = 0;
    std::string format;  // "PNG 1024x1024"
    std::string error;
    std::string time;  // hooks.stamp() at the last change
};

class Sequence {
public:
    enum class Phase { Idle, Next, Writing, Requesting, Waiting, Restoring, Finished, Cancelled, Aborted };

    Sequence(CaptureService& svc, SequenceOptions opt, std::vector<SequenceStep> steps, SequenceHooks hooks);
    // Take the state of a manifest an earlier run wrote (call before start()). false (+err) and nothing taken when it is
    // unreadable, or was made for another player or camera. Codes not in this run's list are kept in the manifest as they
    // were. A manifest original (>= 0) replaces options.original (after a crash he still wears a test code).
    bool resume(const std::string& manifest_text, std::string* err);
    // false (+err) when a hook is missing, the player id is not > 0, his own code is not known (original < 0 after resume()),
    // or it already ran
    bool start(std::string* err);
    void tick();    // advance (call once per frame); does nothing unless running
    void cancel();  // stop: drops the request in flight, then restores
    Phase phase() const { return phase_; }
    bool running() const;
    bool restored() const { return restored_; }
    int64_t original() const { return opt_.original; }
    const SequenceOptions& options() const { return opt_; }
    // progress
    size_t total() const { return order_.size(); }
    size_t count(const std::string& state) const;  // this run's entries in that state
    int64_t current_code() const;                  // the code being written / drawn, 0 when none
    const std::string& message() const { return message_; }
    const std::vector<ManifestEntry>& entries() const { return entries_; }  // this run's codes in step order, then the others
    const ManifestEntry* entry(int64_t code) const;
    std::string manifest_text() const;

    // Read a manifest (the thumbnail grid): entries in the file's order; *player / *camera / *original when given
    static bool parse_manifest(const std::string& text, std::vector<ManifestEntry>& out, std::string* err, int32_t* player = nullptr,
                               int* camera = nullptr, int64_t* original = nullptr);

private:
    bool save_manifest();
    void next_step();
    void step_done();
    void step_failed(const std::string& why);
    void begin_restore(Phase end, const std::string& why);
    void end_run();
    ManifestEntry& cur() { return entries_[order_[cursor_]]; }
    std::string now_stamp() const { return hooks_.stamp ? hooks_.stamp() : std::string(); }
    double now() const { return hooks_.clock(); }

    CaptureService& svc_;
    SequenceOptions opt_;
    SequenceHooks hooks_;
    std::vector<ManifestEntry> entries_;  // this run's codes first (step order), then codes only the resumed manifest had
    std::vector<size_t> order_;           // indices into entries_ of this run's steps
    size_t cursor_ = 0;                   // into order_
    Phase phase_ = Phase::Idle;
    Phase end_phase_ = Phase::Finished;
    bool started_ = false, first_call_ = true, restored_ = false, finished_ = false;
    int failures_in_row_ = 0;
    double t_phase_ = 0.0;
    std::string message_;
};

}  // namespace capture
}  // namespace turbo
