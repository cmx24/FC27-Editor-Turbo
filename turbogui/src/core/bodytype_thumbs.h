// FC 27 LE Turbo GUI - body-type thumbnails (2.17 looks, track T; research_20261017\FULLBODY_FAST_ROUTE.md point 7).
// Platform independent (tested natively). Two halves:
//   * THE STORE the body-type grid reads: turbo_output\looks\bodytypes\<code>.png (256 wide, the grid copy),
//     full\<code>.png (the game's picture, at most 1024), manifest.json (track C's capture::Sequence manifest), run.json (which
//     career and player the run belongs to), run.log (one line per code, written and flushed BEFORE its request: the crash record).
//     thumb(code) is a lookup in a cached listing (rescanned at most every kRescanSec), never a file check per tile and frame.
//   * THE GENERATOR (one-time, a Tester window on LEICESTER): a real-face reference player stepped through the probed
//     bodytypecode list (generic codes first, the other gender's codes and the manager body set skipped), one full-body picture
//     per code through capture::Sequence, the manifest after every step, resumable after a crash, his own code put back at the end.
//     The database is reached through hooks (the UI binds a RAW range-checked write: no undo step, no kept-edit row).
// Nothing here is proven in game: the full-body framing and the live body type are the Tester's route check (section 5).
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bodytype_catalog.h"
#include "image.h"
#include "player_capture.h"

namespace turbo::bodytype_thumbs {

namespace fs = std::filesystem;

constexpr int kGridWidth = 256;     // the grid copy, <code>.png
constexpr int kFullMax = 1024;      // the game's picture, full\<code>.png (never upscaled)
constexpr double kRescanSec = 5.0;  // thumb(): the folder is listed again at most this often
// Managers carry their own body set (looks_fields_data.h: "Manager body 81".."89"); a player picture never stands for one
constexpr int64_t kManagerBodyFirst = 81, kManagerBodyLast = 89;
inline bool manager_body(int64_t code) { return code >= kManagerBodyFirst && code <= kManagerBodyLast; }

// ---------------------------------------------------------------- where things are (all under <turbo_output>\looks\bodytypes)
fs::path folder(const fs::path& turbo_output);
fs::path manifest_file(const fs::path& turbo_output);
fs::path run_file(const fs::path& turbo_output);
fs::path log_file(const fs::path& turbo_output);
fs::path request_file(const fs::path& turbo_output);  // make.json: the Tester's start request (see take_request)
fs::path grid_file(const fs::path& turbo_output, int64_t code);  // <code>.png
fs::path full_file(const fs::path& turbo_output, int64_t code);  // full\<code>.png

// ---------------------------------------------------------------- the grid's lookup
// A listing of the folder: the codes with a grid picture, less the ones the manifest says failed / crashed / were cancelled
// (a picture left from an older run stays usable while the manifest says "done" or does not know the code).
class Index {
public:
    void set_root(const fs::path& turbo_output);  // a new root empties the index (next refresh lists it)
    const fs::path& root() const { return root_; }
    // List the folder again when `force`, or when kRescanSec passed since the last listing. true when it listed.
    bool refresh(double now, bool force = false);
    // The grid picture of a player body type, or an empty path (no picture yet, a manager, a code the last run could not draw)
    fs::path thumb(int64_t code, bool manager = false) const;
    bool has(int64_t code) const { return codes_.count(code) != 0; }
    size_t size() const { return codes_.size(); }
    std::vector<int64_t> missing(const std::vector<int64_t>& codes) const;  // of `codes`, those without a picture (managers too)
    std::vector<int64_t> codes() const;

private:
    fs::path root_;
    std::map<int64_t, fs::path> codes_;
    double listed_at_ = -1e18;
};

// Process-wide index for the pages (U's body-type grid). configure() once per frame or per page draw is cheap (a path compare);
// thumb() rescans at most every kRescanSec (steady clock) and is otherwise a map lookup. invalidate(): the next thumb() lists again.
void configure(const fs::path& turbo_output);
fs::path thumb(int64_t code, bool manager = false);
void invalidate();

// ---------------------------------------------------------------- the manifest (track C's format, read back)
struct Manifest {
    int32_t player = 0;
    int32_t team = -1;
    int camera = -1;
    int64_t original = -1;
    bool finished = false, restored = false;
    std::string updated;
    std::vector<capture::ManifestEntry> entries;
    const capture::ManifestEntry* entry(int64_t code) const;
    size_t count(const std::string& state) const;
};
bool read_manifest(const std::string& text, Manifest& out, std::string* err = nullptr);
bool load_manifest(const fs::path& file, Manifest& out, std::string* err = nullptr);  // false (+err) when missing / unreadable

// run.json: the career and player a run belongs to (the same playerid exists in every career)
struct RunInfo {
    std::string career;  // App's career identity ("" = unknown: never written)
    int32_t player = 0;
    int camera = capture::kCameraBody;
    std::string started;
};
bool write_run_info(const fs::path& turbo_output, const RunInfo& r, std::string* err = nullptr);
bool read_run_info(const fs::path& turbo_output, RunInfo& r, std::string* err = nullptr);

// ---------------------------------------------------------------- the step list
struct PlanOptions {
    int gender = 0;            // the reference player's players.gender (0 men, 1 women)
    int head_class = 0;        // his headclasscode (0 real face)
    bool allow_risky = false;  // the generator passes true: a player-specific body is written on any head (never the gallery)
    bool include_manager_set = false;  // 81-89 are the managers' bodies: not drawn on a player unless asked
    int64_t field_min = 1, field_max = 512;  // the database's range for bodytypecode
    size_t max_steps = 0;      // 0 = every code; else the first N (a short Tester proof)
};
struct Plan {
    std::vector<capture::SequenceStep> steps;  // generic codes (named, ascending) first, then the player-specific ones ascending
    size_t generic = 0;                        // how many of the steps are generic
    std::vector<std::pair<int64_t, std::string>> skipped;  // code, why (other gender, manager body, out of range, risky, limit)
};
// Gender of a code: the probe's (catalog), else the census (looks_fields_data.h: only men or only women carry it), else -1
int code_gender(const bodytype::Catalog& cat, int64_t code);
// Plain label for the manifest and the messages: the census label of looks_fields_data.h ("Tall, stocky"), else the catalog's name
std::string code_label(const bodytype::Catalog& cat, int64_t code);
Plan plan_steps(const bodytype::Catalog& cat, const PlanOptions& o);

// ---------------------------------------------------------------- pictures
// PNG (8-bit RGBA, zlib with fixed-Huffman LZ77, row filter Sub). Empty for an empty picture.
std::vector<uint8_t> encode_png(const Rgba& img);
// full\<code>.png (the picture, at most kFullMax) and <code>.png (kGridWidth wide), each through <file>.tmp + rename.
// *file = "<code>.png" (what the manifest records, relative to the folder)
bool save_pictures(const fs::path& turbo_output, int64_t code, const Rgba& img, std::string* file, std::string* err);
// <path>.tmp + rename; creates the folder
bool write_file_atomic(const fs::path& p, const std::string& bytes, std::string* err);
// One line appended to run.log with the time, flushed and closed at once (survives the game going down)
bool append_log(const fs::path& turbo_output, const std::string& stamp, const std::string& line);

// ---------------------------------------------------------------- the Tester's start request: <folder>\make.json
//   {"player": 254470, "camera": 1, "max": 0, "fresh": false}
// player 0 / absent = the player selected in the window; camera 1 body (default) or 2 body in kit; max 0 = every code;
// fresh = start over (only when the old run gave his own code back, see start_over_refusal).
struct StartRequest {
    int32_t player = 0;
    int camera = capture::kCameraBody;
    size_t max_steps = 0;
    bool fresh = false;
};
bool parse_request(const std::string& text, StartRequest& out, std::string* err);
// Reads and removes make.json (renamed to make.taken.json). false when there is none; *err set when there was one but unreadable
bool take_request(const fs::path& turbo_output, StartRequest& out, std::string* err);

// A new run over an old manifest of ANOTHER player or camera (or fresh): allowed only when the old run gave his own code back
// (restored) or his code is his original again (old_current = his code now, -1 unknown). "" when allowed, else why not.
std::string start_over_refusal(const Manifest& old, int64_t old_current);

// ---------------------------------------------------------------- the generator
struct GeneratorEnv {
    fs::path turbo_output;
    capture::CaptureService* svc = nullptr;
    // his bodytypecode now / write it (range-checked, raw, read back); false + err when the record is gone or refused
    std::function<bool(int32_t player, int64_t* code, std::string* err)> read_code;
    std::function<bool(int32_t player, int64_t code, std::string* err)> write_code;
    std::function<std::string()> career;  // the loaded career's identity ("" = none identified)
    std::function<double()> clock;        // seconds (required)
    std::function<std::string()> stamp;   // "2026-10-17 22:31:05"
    std::function<void(const std::string&)> log;  // the host log (turbo_gui.log); optional
};

struct Reference {
    int32_t player = 0;
    int32_t team = -1;
    int gender = 0;
    int head_class = -1;
    std::string name;
};

class Generator {
public:
    explicit Generator(GeneratorEnv env);
    ~Generator();
    // Plan, bind to the career, take the old manifest (resume) or refuse to overwrite it, start. false + err when refused.
    bool start(const bodytype::Catalog& cat, const Reference& ref, const StartRequest& rq, std::string* err);
    void tick();    // once per frame on the UI thread (does nothing unless running)
    void cancel();  // stop and give his own code back
    bool running() const;
    // An old run that ended without giving his code back (finished or not, restored false): write the manifest's original
    // back (same career only). true when written (or nothing was due); false + err when refused.
    bool put_back_leftover(std::string* err);
    std::string status() const;  // one plain line for the panel
    const capture::Sequence* sequence() const { return seq_.get(); }
    const Plan& plan() const { return plan_; }
    const RunInfo& run() const { return run_; }

private:
    capture::StepResult write_step(int64_t code, bool first, std::string* err);
    capture::StepResult restore_step(bool first, std::string* err);
    bool same_career(std::string* err) const;
    std::string now_stamp() const;
    void note(const std::string& line);  // run.log + host log

    GeneratorEnv env_;
    std::unique_ptr<capture::Sequence> seq_;
    Plan plan_;
    RunInfo run_;
    std::string last_;
    bool ended_noted_ = false;
};

// True while a generator runs (any instance): every other capture request (faces card, Looks studio, chooser) must wait,
// or its poll could take the generator's picture. The UI shows "Making body-type pictures..." instead.
bool generator_running();

}  // namespace turbo::bodytype_thumbs
