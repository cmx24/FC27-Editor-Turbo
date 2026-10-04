// FC 27 LE Turbo GUI - hear a callname from inside Turbo (the play buttons of Players > Callname), like the Play cells
// of the user's master workbook.
//
// The FC 27 master (turbo\callnames\masters\<lang>.json, turbo/tools/build_callname_master.py --wav-dir) gives the
// folder of the segment wavs ("wav_dir", holding real\ and generic\) and the segments of every id ("segments"):
//   generic id  -> <wav_dir>\generic\pSIMPLE_SURNAME_<seg>_<seg>.wav
//   own (real)  -> <wav_dir>\real\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav, then real\pPLAYER_NAMES_LINK_<seg>_<seg>.wav
//                  (the master lists LINK segments only when their wavs exist)
// Every click plays the id's next segment (its variations, in turn); a click while it plays stops it. The sound is
// played by Windows (PlaySoundW, src/win/callname_audio_win.cpp) behind WavPlayer, so native tests use a fake one.
// Nothing here touches the game.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace turbo {

// What a play button plays: a generic surname (by commentary id) or a player's own recording (by player id)
enum class CallnameAudioKind { Generic, Own };

// The audio part of a master list (MasterList::audio)
struct MasterAudio {
    std::string wav_dir;  // UTF-8; "" = the master names no wav folder
    std::unordered_map<int64_t, std::vector<int64_t>> generic;    // commentary id -> segments (pSIMPLE_SURNAME)
    std::unordered_map<int64_t, std::vector<int64_t>> real;       // player id -> segments (pPLAYER_NAMES_SIMPLE)
    std::unordered_map<int64_t, std::vector<int64_t>> real_link;  // player id -> segments (pPLAYER_NAMES_LINK)
    bool any() const { return !generic.empty() || !real.empty() || !real_link.empty(); }
};

// Reads "wav_dir" and "segments" of a parsed master (anything of the wrong type is skipped; never throws)
void parse_master_audio(const nlohmann::json& j, MasterAudio& out);

// <wav_dir>\generic\pSIMPLE_SURNAME_<seg>_<seg>.wav / <wav_dir>\real\pPLAYER_NAMES_SIMPLE|LINK_<seg>_<seg>.wav
std::filesystem::path callname_wav_path(const std::string& wav_dir, CallnameAudioKind kind, int64_t segment, bool link = false);
// Every wav of an id, in play order (SIMPLE before LINK for an own recording); empty without wav_dir or segments
std::vector<std::filesystem::path> callname_wavs(const MasterAudio& a, CallnameAudioKind kind, int64_t id);

// Length in seconds of a PCM wav from its first bytes (RIFF, fmt, data chunks) and its file size; 0 when unknown
double wav_seconds(const std::string& head, uint64_t file_size);

// Plays one wav file without blocking the caller (the Windows host: PlaySoundW on a worker thread)
class WavPlayer {
public:
    virtual ~WavPlayer() = default;
    virtual bool play(const std::filesystem::path& wav) = 0;  // replaces what is playing; false = not started
    virtual void stop() = 0;
};

// The play buttons' state: which id plays, until when, and the next segment of every id
class CallnamePlayer {
public:
    using Exists = std::function<bool(const std::filesystem::path&)>;
    using Seconds = std::function<double(const std::filesystem::path&)>;  // 0 = unknown
    CallnamePlayer();
    void set_player(std::shared_ptr<WavPlayer> p) { player_ = std::move(p); }
    bool has_player() const { return player_ != nullptr; }
    // tests: file checks and lengths without disk access
    void set_exists(Exists e) { exists_ = std::move(e); files_.clear(); }
    void set_seconds(Seconds s) { seconds_ = std::move(s); }

    struct Button {
        bool enabled = false;
        bool playing = false;  // this id is playing: a click stops it
        std::string tip;       // why it is disabled, or what a click does
    };
    // The button of an id (file checks are cached: cheap every frame)
    Button button(const MasterAudio& a, CallnameAudioKind kind, int64_t id, double now);
    // A click: stops this id when it plays, else plays its next wav. Returns the wav started ("" = none)
    std::string click(const MasterAudio& a, CallnameAudioKind kind, int64_t id, double now);
    void stop();
    bool playing(CallnameAudioKind kind, int64_t id, double now) const { return now < until_ && key_ == key(kind, id); }
    // the next wav index of an id (tests)
    size_t next_index(CallnameAudioKind kind, int64_t id) const;

private:
    static int64_t key(CallnameAudioKind kind, int64_t id) { return kind == CallnameAudioKind::Own ? -id : id; }
    bool file_ok(const std::filesystem::path& p);
    std::vector<std::filesystem::path> playable(const MasterAudio& a, CallnameAudioKind kind, int64_t id);
    std::shared_ptr<WavPlayer> player_;
    Exists exists_;
    Seconds seconds_;
    std::string files_dir_;                          // wav_dir the cache below belongs to
    std::unordered_map<std::string, bool> files_;    // wav path -> exists
    std::unordered_map<int64_t, size_t> next_;       // key -> next wav index
    int64_t key_ = 0;
    double until_ = 0.0;
};

}  // namespace turbo
