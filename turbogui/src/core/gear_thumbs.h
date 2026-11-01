// FC 27 LE Turbo GUI - Turbo X: pictures of any look item the game ships without one (hair, beard, boots, gloves, accessories,
// tattoos, body types), drawn by the game itself.
//
// Generalises hairbeard_thumbs.h / bodytype_thumbs.h: a reference player in a loaded (throwaway) career is given each requested id
// of each requested field in turn, the game's capture service draws him (head and shoulders for hair / beard, the full body for
// the rest), the picture is kept, and at the end every field the run touched is written back to his own value (also on cancel).
// BATCHED: up to 6 reference players ("players"), one item each per round, ONE capture request per round (Request::batch, the
// controller renders up to kMaxPlayersPerRequest; the host's max_batch caps it, 1 while player_capture_batch1.txt exists). Rounds of
// the head camera first, then the body camera. A single player is the proven single request (no batch).
//
// Store (the picture index of ui_fcedit_pictures reads <turbo_output>/pictures/assets like FC Editor's assets folder):
//     <turbo_output>/pictures/assets/<folder>/<id>.png    the pictures (folder = the field; accessorycode1 -> "accessorycode")
//     <turbo_output>/pictures/want.json                   the steps the "Make missing pictures" button starts from
//     <turbo_output>/pictures/gear_thumbs/request.json    a start request (taken = renamed request.taken.json)
//     <turbo_output>/pictures/gear_thumbs/manifest.json   every item's state (resume after a crash: an item that was being
//                                                         drawn twice when Turbo died is "crashed" and never tried again)
//     <turbo_output>/pictures/gear_thumbs/own.json        the players' own values while a run is going (removed after a clean restore)
//     <turbo_output>/pictures/gear_thumbs/run.log, status.txt
//
// Request / want.json:
//     {"op":"gear_thumbs","player":<playerid, 0 = the selected player>,"players":[<up to 6 ids: wins over "player">],
//      "fresh":false,"career":"<optional: must match>",
//      "steps":[{"field":"shoetypecode","id":421,"camera":"body"}, ...]}
// "camera" is optional ("head" | "body"; default per field). An existing <id>.png is never drawn again unless "fresh": true.
//
// IN-GAME PART = UNPROVEN: the capture service and both cameras are proven separately (hair/beard head pictures reach a capture,
// the full-body camera W2228, batches of 6 portraits for the chooser's 3D heads), but this generator has not been run in the game,
// and a BATCH of full-body pictures has never been asked of the game.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "player_capture.h"

namespace turbo::gear_thumbs {

namespace fs = std::filesystem;

constexpr double kStepDeadlineSec = 90.0;  // per round (one request), from the request to the last picture
constexpr int kMaxAttempts = 2;
constexpr int kMaxEmptyRounds = 3;  // rounds in a row without one picture: the game stopped answering, stop and restore
constexpr int64_t kFieldBase = 100000000;  // step code = (field index + 1) * kFieldBase + id

enum class Cam { Head, Body };

struct FieldSpec {
    const char* db;      // the players column written
    const char* folder;  // the picture folder under pictures/assets
    Cam cam;             // the default camera
};
const std::vector<FieldSpec>& fields();
// The spec of a field name (aliases: "accessorycode" -> accessorycode1); nullptr when unknown. *index = its position in fields().
const FieldSpec* find_field(const std::string& name, int* index = nullptr);

int64_t step_code(int field_index, int64_t id);
bool split_code(int64_t code, int& field_index, int64_t& id);

fs::path pictures_root(const fs::path& turbo_output);
fs::path assets_dir(const fs::path& turbo_output);
fs::path picture_file(const fs::path& turbo_output, const FieldSpec& f, int64_t id);
fs::path run_dir(const fs::path& turbo_output);
fs::path request_file(const fs::path& turbo_output);
fs::path want_file(const fs::path& turbo_output);
fs::path manifest_file(const fs::path& turbo_output);
fs::path own_file(const fs::path& turbo_output);
fs::path log_file(const fs::path& turbo_output);
fs::path status_file(const fs::path& turbo_output);

// Pictures saved by any run of this process: the picture index refreshes when it changes
uint64_t generation();

struct Step {
    std::string field;  // the name as given (checked against fields())
    int64_t id = 0;
    Cam cam = Cam::Body;
    bool cam_given = false;
};

struct Request {
    int32_t player = 0;
    std::vector<int32_t> players;  // up to kMaxPlayersPerRequest, each > 0, no repeats; empty = {player}
    bool fresh = false;
    // Turbo X: "real_face": true = the hair / beard steps go on SCANNED heads (headclasscode 0) through Turbo's live band codes
    // (faces3d: kHairBandBase + i / kBeardBandBase + i, the path the Looks picker writes for a real face) and the pictures are kept in
    // pictures\realface_test\<field>_<id>.png (never in the picture pack).
    bool real_face = false;
    // Turbo X: "body_mode": the capture mode of the full-body pictures (-1 = the proven 0 = 256x256; 1 = the big 1024 slices, unproven)
    int body_mode = -1;
    std::string career;  // optional: the request is refused in another career
    std::vector<Step> steps;
};
// false (+err) when unreadable, not an object, another op, a negative player or no "steps" array. A step without a string
// "field" or an integer "id" >= 0 is skipped and named in *bad (one line each); an unknown field is kept (it fails at start).
bool parse_request(const std::string& text, Request& out, std::string* err, std::vector<std::string>* bad = nullptr);
// Takes request.json (renamed request.taken.json first, so a bad file never starts twice); false when there is none or it is bad
bool take_request(const fs::path& turbo_output, Request& out, std::string* err, std::vector<std::string>* bad = nullptr);
// Reads want.json (left in place)
bool read_want(const fs::path& turbo_output, Request& out, std::string* err, std::vector<std::string>* bad = nullptr);

bool append_log(const fs::path& turbo_output, const std::string& stamp, const std::string& line);

// A planned step (field known, picture missing unless fresh, not a duplicate)
struct Planned {
    int field_index = 0;
    int64_t id = 0;
    Cam cam = Cam::Body;
};
struct Plan {
    std::vector<Planned> head, body;
    std::vector<std::string> rejected;  // "field id: reason"
    size_t skipped_existing = 0;
};
// `check` (optional) answers whether the field exists on the players table and the id is in its range (false + reason)
Plan make_plan(const fs::path& turbo_output, const Request& rq, const std::function<bool(const FieldSpec&, int64_t, std::string*)>& check);

// ---------------------------------------------------------------- the generator
struct GeneratorEnv {
    fs::path turbo_output;
    capture::CaptureService* svc = nullptr;
    std::function<bool(int32_t player, const std::string& field, int64_t* value, std::string* err)> read_field;
    std::function<bool(int32_t player, const std::string& field, int64_t value, std::string* err)> write_field;
    std::function<bool(const FieldSpec&, int64_t id, std::string* err)> check;  // optional (see make_plan)
    std::function<double()> clock;
    std::function<std::string()> stamp;
    std::function<void(const std::string&)> log;
};

struct Reference {
    int32_t player = 0;
    int32_t team = -1;
    int head_class = -1;  // 0 = a scanned head: no hair / beard item is drawn on him (its textures carry its own hair)
    std::string name;
};

bool generator_running();

class Generator {
public:
    explicit Generator(GeneratorEnv env) : env_(std::move(env)) {}
    ~Generator();
    // refs: 1..kMaxPlayersPerRequest reference players (distinct, > 0). false (+err, logged) without a service / hooks / player /
    // item to draw, while another run or a capture is busy, or when a player's values cannot be read
    bool start(const Request& rq, const std::vector<Reference>& refs, std::string* err);
    void tick();
    void cancel();
    bool running() const { return phase_ != Phase::Idle && phase_ != Phase::Ended; }
    std::string status() const;
    size_t done() const;
    size_t failed() const;
    size_t total() const { return items_.size() + rejected_; }
    size_t requests() const { return requests_; }  // capture requests sent by this run

    struct Item {
        int field_index = 0;
        int64_t id = 0;
        Cam cam = Cam::Body;
        std::string state = "todo";  // todo, pending (in a round), done, failed, crashed
        int attempts = 0;
        int stale = 0;  // captures refused as stale (the same pixels as the player's previous capture, another look)
        std::string error;
        std::string file;
        int w = 0, h = 0;
    };
    const std::vector<Item>& items() const { return items_; }

private:
    enum class Phase { Idle, Pick, Requesting, Waiting, Restoring, Ended };
    struct Slot {
        size_t ref = 0;   // index into refs_
        size_t item = 0;  // index into items_
        bool answered = false;
    };
    bool write_item(size_t ref, size_t item, std::string* err);
    bool restore_all(std::string* err);
    void pick_round();
    void send_round();
    void poll_round();
    void end_round(const std::string& why_unanswered, bool count_attempt);
    void item_failed(size_t item, const std::string& why, bool count_attempt);
    void begin_restore(const std::string& why);
    void finish();
    void save_manifest();
    void load_manifest();
    void write_status();
    void note(const std::string& s);
    std::string where(size_t item) const;
    double now() const { return env_.clock ? env_.clock() : 0.0; }

    GeneratorEnv env_;
    std::vector<Reference> refs_;
    std::vector<Item> items_;
    std::vector<Slot> round_;
    Phase phase_ = Phase::Idle;
    Cam cam_ = Cam::Head;
    bool counted_ = false;
    bool cancelled_ = false;
    bool aborted_ = false;
    bool restored_ = false;
    bool fresh_ = false;
    bool real_face_ = false;
    int body_mode_ = -1;
    size_t rejected_ = 0;
    size_t requests_ = 0;
    int empty_rounds_ = 0;
    size_t round_pictures_ = 0;
    double t_phase_ = 0.0;
    std::map<int32_t, std::map<std::string, int64_t>> own_;  // player -> field -> his own value
    std::map<int32_t, std::set<std::string>> dirty_;         // player -> fields that carry a test value now
    std::map<int32_t, std::pair<uint64_t, size_t>> last_capture_;  // player -> pixel hash + item of his last SAVED capture this run
    std::string last_status_;
    std::string end_message_;
};

}  // namespace turbo::gear_thumbs
