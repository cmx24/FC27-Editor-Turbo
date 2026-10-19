// FC 27 LE Turbo GUI - the picture sets of the visual pickers (2.20 track V): run styles and free-kick run-ups as animated
// GIFs, hair / beard / eyebrows / colours / kit / boots / gloves / tattoos as pictures, each code with its in-game name.
//
// The names are our own facts (core/visual_names_data.h, generated from data/visual_names.json by scripts/gen_visual_names.py).
// The pictures are NOT ours: they are read at runtime from the user's own RDBM26 install (by Doctor+), the folder set in
// Settings (default C:\FC_Tools\RDBM26; its Resources subfolder holds the sets). They are never copied into Turbo, its
// package or git. With no such folder the pickers are names-only lists.
//
// GIFs are decoded on a worker thread (AnimStore), frame by frame, each frame scaled down at once, into ONE picture per GIF
// (an atlas of its frames: one texture per animation, never one per frame; the window's texture heap has 512 slots).
// Pure (no Dear ImGui): the native tests run all of it.
#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/image.h"
#include "core/visual_names_data.h"

namespace turbo::visual {

using data::CodeDef;
using data::SetDef;

// ---- the names (compiled in)
size_t set_count();
const SetDef& set_at(size_t i);
const SetDef* find_set(const std::string& id);
// The picture set of a players/managers field ("runstylecode" -> run styles); nullptr when it has none
const SetDef* set_for_field(const std::string& field);
// Every field with a picture set, in the order of the mapping
std::vector<std::string> mapped_fields();
const CodeDef* codes_begin(const SetDef& s);
const CodeDef* find_code(const SetDef& s, int64_t code);
// The in-game name of a code: the set's name, else `fallback` (the name the editors already show), else "<title> <code>"
std::string code_name(const SetDef& s, int64_t code, const std::string& fallback = "");
// Codes whose name (or fallback name) or number contains `query` (case-insensitive); all when empty
std::vector<int> filter_codes(const SetDef& s, const std::string& query, const std::vector<std::string>* fallback_names = nullptr);

// ---- the RDBM26 folder
constexpr const char* kDefaultRoot = "C:\\FC_Tools\\RDBM26";
// The folder holding the sets: <root>\Resources when it has them, or <root> itself when it is the Resources folder;
// empty when neither has a run-style or hair set
std::filesystem::path resources_dir(const std::filesystem::path& root);
// The user's choice (turbo_output\visual_pictures.json {"rdbm_folder": "..."}); "" = not set (the default is tried)
std::filesystem::path setting_file(const std::filesystem::path& turbo_output);
std::string load_folder_setting(const std::filesystem::path& turbo_output);
bool save_folder_setting(const std::filesystem::path& turbo_output, const std::string& folder, std::string* err = nullptr);
// The folder to use: the setting when set, else the default; then resources_dir() of it
std::filesystem::path find_resources(const std::filesystem::path& turbo_output);
// The picture of a code in that folder (the set's own folder, then its fallback folder); empty when there is none.
// Looks at the disk: callers cache it.
std::filesystem::path picture_file(const std::filesystem::path& resources, const SetDef& s, const CodeDef& c);

// ---- animated GIFs
struct Anim {
    int frame_w = 0, frame_h = 0;  // one frame in the atlas (scaled down)
    int src_w = 0, src_h = 0;      // the GIF's own size
    int cols = 0, frames = 0;
    std::vector<int> delays_ms;    // per frame (a delay under 20 ms is shown as 100 ms, as browsers do)
    int total_ms = 0;
    Rgba atlas;                    // frames left to right, top to bottom
    // The frame shown `ms` after the start (loops)
    int frame_at(double ms) const;
    // That frame's rectangle in the atlas, as texture coordinates
    void uv(int frame, float& u0, float& v0, float& u1, float& v1) const;
    size_t bytes() const { return atlas.px.size(); }
};
// Decode every frame, each scaled to at most `max_side` pixels on its longer side (smaller still when the atlas would pass
// `max_bytes`), into one atlas. false with a reason when the bytes are not a GIF or hold no frame.
bool decode_gif(const std::vector<uint8_t>& bytes, int max_side, size_t max_bytes, Anim& out, std::string* err = nullptr);
bool decode_gif_file(const std::filesystem::path& p, int max_side, size_t max_bytes, Anim& out, std::string* err = nullptr);
// Any picture file: a .gif as above; a still (PNG, JPG, DDS...) as one frame at most max_side pixels on its longer side
bool decode_picture_file(const std::filesystem::path& p, int max_side, size_t max_bytes, Anim& out, std::string* err = nullptr);
// Atlas width and height limit (texture side)
constexpr int kMaxAtlasSide = 8192;

// Decoded animations, made on a worker thread and kept least-recently-used under a byte budget.
// The budget counts each atlas twice: the window's texture keeps a copy of the pixels besides the one on the graphics card.
class AnimStore {
public:
    static constexpr size_t kDefaultBudget = size_t(300) << 20;
    explicit AnimStore(size_t budget_bytes = kDefaultBudget);
    ~AnimStore();  // stops and joins the worker
    AnimStore(const AnimStore&) = delete;
    AnimStore& operator=(const AnimStore&) = delete;

    struct Item {
        std::string key;     // stable name of this file at this size (the texture key)
        const Anim* anim = nullptr;  // null until decoded (or failed)
        bool failed = false;
        std::string error;
    };
    // The animation of a file at `side` pixels: queued for the worker when new (the newest asks are decoded first), the
    // decoded one when ready. The pointer stays valid until the next call of get(), drop() or clear() on this thread.
    Item get(const std::filesystem::path& p, int side, size_t max_bytes);
    // Forget one (its texture was lost: the next get() decodes it again)
    void drop(const std::string& key);
    // Drop the queued asks nobody has started (a picker closed)
    void cancel_queued();
    void clear();
    // Keys of animations dropped to stay in the budget since the last call (the caller forgets their textures)
    std::vector<std::string> take_evicted();
    size_t bytes() const;         // budget used (2 x atlas bytes)
    size_t ready_count() const;
    size_t queued_count() const;
    size_t decoded_total() const; // decodes finished since the start (tests, perf)
    size_t budget() const { return budget_; }
    // Tests: wait until the worker has nothing left (at most `seconds`); true when idle
    bool wait_idle(double seconds);

private:
    struct Entry {
        std::string key;
        std::filesystem::path path;
        int side = 0;
        size_t max_bytes = 0;
        bool done = false, failed = false;
        std::string error;
        Anim anim;
        uint64_t used = 0;
    };
    void run();
    void evict_locked();
    static std::string key_of(const std::filesystem::path& p, int side);

    const size_t budget_;
    mutable std::mutex m_;
    std::condition_variable cv_, idle_cv_;
    std::unordered_map<std::string, Entry> entries_;
    std::deque<std::string> queue_;  // newest at the front
    std::vector<std::string> evicted_;
    size_t bytes_ = 0;
    size_t decoded_ = 0;
    uint64_t tick_ = 0;
    bool busy_ = false, stop_ = false;
    std::thread worker_;
};

}  // namespace turbo::visual
