// FC 27 LE Turbo GUI - lit pictures of the hair and beard styles that ship a 3D model but no picture (5.0c E10, brief D3)
//
// Modelled on bodytype_thumbs.h (the store, the resumable manifest, the Tester's request file) and player_capture.h (the
// capture::Sequence: write -> request -> poll -> save, the manifest after every change, resume, restore at the end).
//
// A reference player with a GENERIC head (headclasscode != 0: a scanned head carries its own hair / stubble in its textures,
// fc27-modding reference/disproven.md 10-30) is given every model-only hair id, then every model-only beard id, and the game's
// head-and-shoulders picture (capture::kCameraBody, mode 1) is kept per id. At the end his own hair and beard are written back.
//
// Store: the folder <turbo_output>/cache/hairbeard_thumbs holds
//     hair_<id>.png, beard_<id>.png  the pictures (the picker's last source before the drawn class icon, ui_visual_picker.cpp)
//     manifest.json                  the capture::Sequence manifest (resume after a crash or a cancel)
//     request.json                   the Tester's / dev-service start request (op "hairbeard_thumbs"), renamed request.taken.json
//     run.log                        one line per event
//
// One id may take at most kStepDeadlineSec (45 s, the brief): the "rendering..." state was seen to last 52 s once (W2202), so a
// step that has not delivered by then is failed (attempts 2) and the run moves on.
//
// IN-GAME PART = HYPOTHESIS (brief D3): nothing here has been run in the game. Proven offline only (test_hairbeard_thumbs.h).
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "player_capture.h"

namespace turbo::hairbeard_thumbs {

namespace fs = std::filesystem;

enum class Field { Hair, Beard };
constexpr double kStepDeadlineSec = 45.0;
// one Sequence runs both fields: a beard id is stepped as kBeardBase + id (hair ids stay as they are)
constexpr int64_t kBeardBase = 1000000;

const char* db_field(Field f);        // "hairtypecode" / "facialhairtypecode"
const char* file_word(Field f);       // "hair" / "beard"
bool field_of(const std::string& db_field, Field& out);

fs::path folder(const fs::path& turbo_output);
fs::path picture_file(const fs::path& turbo_output, Field f, int64_t id);
fs::path manifest_file(const fs::path& turbo_output);
fs::path request_file(const fs::path& turbo_output);
fs::path log_file(const fs::path& turbo_output);

// the step code of one id and back
int64_t step_code(Field f, int64_t id);
void split_code(int64_t code, Field& f, int64_t& id);

// ---------------------------------------------------------------- the pictures already made (the picker's tile source)
// Process-wide: configure() names turbo_output (cheap when unchanged); thumb() is the picture of that id or empty. The folder is
// listed once and looked at again at most every 5 s, or at once after invalidate() (a picture was just kept).
void configure(const fs::path& turbo_output);
fs::path thumb(Field f, int64_t id);
void invalidate();

// ---------------------------------------------------------------- the step list
// Every model-only hair id (sorted, once, no 0 = bald), then every model-only beard id (no 0 = none). max_steps 0 = all.
std::vector<capture::SequenceStep> plan_steps(const std::vector<int64_t>& hair_model_only, const std::vector<int64_t>& beard_model_only,
                                              size_t max_steps = 0);
// the ids of the plan that have no picture yet (the "Render missing pictures (N)" count)
size_t missing_count(const fs::path& turbo_output, const std::vector<int64_t>& hair_model_only, const std::vector<int64_t>& beard_model_only);

// ---------------------------------------------------------------- the start request (request.json, dev-service op "hairbeard_thumbs")
// {"op":"hairbeard_thumbs","player":<playerid, 0 = the selected one>,"max":<0 = all>,"fresh":false}
struct StartRequest {
    int32_t player = 0;
    size_t max_steps = 0;
    bool fresh = false;
};
bool parse_request(const std::string& text, StartRequest& out, std::string* err);
bool take_request(const fs::path& turbo_output, StartRequest& out, std::string* err);

bool append_log(const fs::path& turbo_output, const std::string& stamp, const std::string& line);

// ---------------------------------------------------------------- the generator
struct GeneratorEnv {
    fs::path turbo_output;
    capture::CaptureService* svc = nullptr;
    std::function<bool(int32_t player, Field f, int64_t* code, std::string* err)> read_code;
    std::function<bool(int32_t player, Field f, int64_t code, std::string* err)> write_code;
    std::function<double()> clock;
    std::function<std::string()> stamp;
    std::function<void(const std::string&)> log;
};

struct Reference {
    int32_t player = 0;
    int32_t team = -1;
    int head_class = -1;  // must be != 0 (a generic head)
    std::string name;
};

class Generator {
public:
    explicit Generator(GeneratorEnv env) : env_(std::move(env)) {}
    ~Generator();
    // refuses (false, *err) without a service, a generic-head player, ids to step, or while a capture is busy. Continues an earlier
    // manifest of the same player unless rq.fresh.
    bool start(const std::vector<int64_t>& hair_model_only, const std::vector<int64_t>& beard_model_only, const Reference& ref,
               const StartRequest& rq, std::string* err);
    void tick();
    void cancel();
    bool running() const { return seq_ && seq_->running(); }
    std::string status() const;
    const capture::Sequence* sequence() const { return seq_.get(); }
    const std::vector<capture::SequenceStep>& plan() const { return plan_; }
    int64_t own_hair() const { return own_hair_; }
    int64_t own_beard() const { return own_beard_; }

private:
    capture::StepResult write_step(int64_t code, std::string* err);
    capture::StepResult restore_step(std::string* err);
    void note(const std::string& s);

    GeneratorEnv env_;
    Reference ref_;
    std::vector<capture::SequenceStep> plan_;
    std::unique_ptr<capture::Sequence> seq_;
    int64_t own_hair_ = -1, own_beard_ = -1;
    bool counted_ = false;
};

bool generator_running();

}  // namespace turbo::hairbeard_thumbs
