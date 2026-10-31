// FC 27 LE Turbo GUI - hear a callname from inside Turbo (the play buttons of Players > Callname), like the Play cells
// of the user's master workbook.
//
// The FC 27 master (turbo\callnames\masters\<lang>.json, turbo/tools/build_callname_master.py --wav-dir) gives the
// folder of the segment wavs ("wav_dir", holding real\, real_link\, real_high\ and generic\) and the segments of every id
// ("segments"; its generic_high key is not played here):
//   generic id  -> <wav_dir>\generic\pSIMPLE_SURNAME_<seg>_<seg>.wav
//   own (real)  -> <wav_dir>\real\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav, then real_link\pPLAYER_NAMES_LINK_<seg>_<seg>.wav
//                  (the master lists LINK segments only when their wavs exist), then
//                  real_high\pPLAYER_NAMES_HIGH_<seg>_<seg>.wav (the high-intensity variations: heard in the preview only; the
//                  kick-off name check never asks them, so a HIGH-only player is still not a voice source)
// Segments >= 100000 are set index * 100000 + segment of a later commentary set (callname_coverage.py: dr_full 0,
// dr_licensed 1, dr_demo 2, gm_full 3, gm_licensed 4, gm_demo 5); the file name carries that number as it is.
//   a wav missing there is looked for straight in <wav_dir> (a flat folder, as an extraction tool writes them)
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
// Club (5.0c E15): the Preview of a club name in Team > Commentary name; its wavs are <wav_dir>\<club id>.wav (never renumber the others)
#define TURBO_E15_CLUB_AUDIO 1  // the native test builds on the old tree without it
enum class CallnameAudioKind { Generic, Own, Club };

// The audio part of a master list (MasterList::audio)
struct MasterAudio {
    std::string wav_dir;  // UTF-8; "" = the master names no wav folder
    std::unordered_map<int64_t, std::vector<int64_t>> generic;    // commentary id -> segments (pSIMPLE_SURNAME)
    std::unordered_map<int64_t, std::vector<int64_t>> real;       // player id -> segments (pPLAYER_NAMES_SIMPLE)
    std::unordered_map<int64_t, std::vector<int64_t>> real_link;  // player id -> segments (pPLAYER_NAMES_LINK)
    std::unordered_map<int64_t, std::vector<int64_t>> real_high;  // player id -> segments (pPLAYER_NAMES_HIGH; preview only)
    // set by every parse_master_audio (a new number each time, 0 = never parsed): the play buttons check the wav files
    // again when it changes, so Refresh finds a wav added since (even a "missing" one) in the same wav folder
    uint64_t gen = 0;
    std::string lang;  // Club only: the loaded commentary language the folder belongs to (the tooltip names it)
    bool any() const { return !generic.empty() || !real.empty() || !real_link.empty() || !real_high.empty(); }
};

// Reads "wav_dir" and "segments" of a parsed master (anything of the wrong type is skipped; never throws); gives the
// result a new `gen`
void parse_master_audio(const nlohmann::json& j, MasterAudio& out);

// <wav_dir>\generic\pSIMPLE_SURNAME_<seg>_<seg>.wav / <wav_dir>\real\pPLAYER_NAMES_SIMPLE|LINK_<seg>_<seg>.wav /
// <wav_dir>\real_high\pPLAYER_NAMES_HIGH_<seg>_<seg>.wav (`high` wins over `link`)
std::filesystem::path callname_wav_path(const std::string& wav_dir, CallnameAudioKind kind, int64_t segment, bool link = false, bool high = false);
// Every wav of an id, in play order (SIMPLE, then LINK, then HIGH for an own recording); empty without wav_dir or segments
std::vector<std::filesystem::path> callname_wavs(const MasterAudio& a, CallnameAudioKind kind, int64_t id);

// Length in seconds of a PCM wav from its first bytes (RIFF, fmt, data chunks) and its file size; 0 when unknown
double wav_seconds(const std::string& head, uint64_t file_size);

// Plays one wav file without blocking the caller (the Windows host: PlaySoundW on a worker thread)
class WavPlayer {
public:
    virtual ~WavPlayer() = default;
    virtual bool play(const std::filesystem::path& wav) = 0;  // replaces what is playing; false = not started
    virtual void stop() = 0;
    // The last play(): has the host since found it did not play (PlaySoundW returned FALSE)? Its worker reports a frame or
    // two after play() returned true; `why` names the error. The default (fakes, no host report) is "never"
    virtual bool failed(std::string* why) const { (void)why; return false; }
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
    // A click: stops this id when it plays, else plays its next wav. Returns the wav started ("" = none). `name` is what
    // the status line calls it ("" = the wav's file name)
    std::string click(const MasterAudio& a, CallnameAudioKind kind, int64_t id, double now, const std::string& name = std::string());
    void stop();
    bool playing(CallnameAudioKind kind, int64_t id, double now) const { return now < until_ && key_ == key(kind, id); }
    // the next wav index of an id (tests)
    size_t next_index(CallnameAudioKind kind, int64_t id) const;

    // The last click that tried to play (kept until the next one): the proof on the screen that something played
    struct Last {
        int64_t key = 0;
        std::string name;     // what was played, in words
        std::string file;     // the wav, UTF-8
        double seconds = 0.0; // its length from the RIFF header (0 = unknown)
        double started = 0.0; // `now` of the click
        bool stopped = false; // a click stopped it before its end
        std::string error;    // "" = it started; else why it did not play
    };
    const Last& last() const { return last_; }
    // The status line of the last play, in words: "Playing Abate (0.8 s)" while it plays, "Played Abate, 0.8 s" after,
    // "Stopped Abate" when a click ended it, "Could not play: <why>" (error = true) when it did not play; "" before the
    // first click. Also picks up a failure the host reported since (WavPlayer::failed): the button is "Play" again
    std::string status(double now, bool* error = nullptr);

private:
    static int64_t key(CallnameAudioKind kind, int64_t id) {
        return kind == CallnameAudioKind::Own ? -id : kind == CallnameAudioKind::Club ? id + (1LL << 40) : id;  // Club N never shares the state of Generic N
    }
    bool file_ok(const std::filesystem::path& p);
    std::vector<std::filesystem::path> playable(const MasterAudio& a, CallnameAudioKind kind, int64_t id);
    void poll_failure();  // a failure the host reported for the last play: into last_.error, and it plays no more
    std::shared_ptr<WavPlayer> player_;
    Exists exists_;
    Seconds seconds_;
    std::string files_dir_;                          // wav_dir the cache below belongs to
    uint64_t files_gen_ = 0;                         // and the MasterAudio::gen it was filled for
    std::unordered_map<std::string, bool> files_;    // wav path -> exists
    std::unordered_map<int64_t, size_t> next_;       // key -> next wav index
    int64_t key_ = 0;
    double until_ = 0.0;
    Last last_;
};

}  // namespace turbo
