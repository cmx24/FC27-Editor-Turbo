// FC 27 LE Turbo GUI - the Callname tab's play buttons on Windows (see callname_audio_win.h)
#include "callname_audio_win.h"

#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "host.h"

namespace host {

namespace {

// PlaySoundW flags (mmsystem.h)
constexpr DWORD kSndAsync = 0x0001, kSndNoDefault = 0x0002, kSndFilename = 0x00020000;
using PlaySoundFn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);

// One command slot: the newest click wins (a play replaces the sound playing, a stop ends it). Never freed: the worker
// may still wait on it while the process exits.
struct Worker {
    std::mutex m;
    std::condition_variable cv;
    bool pending = false;
    bool stop = false;
    std::wstring path;
    bool started = false;
};

Worker* worker() {
    static Worker* w = new Worker();
    return w;
}

void run(Worker* w) {
    PlaySoundFn play = nullptr;
    bool tried = false;
    for (;;) {
        std::wstring path;
        bool stop = false;
        {
            std::unique_lock<std::mutex> lock(w->m);
            w->cv.wait(lock, [w] { return w->pending; });
            w->pending = false;
            path.swap(w->path);
            stop = w->stop;
        }
        if (!tried) {
            tried = true;
            HMODULE mm = GetModuleHandleW(L"winmm.dll");
            if (!mm) mm = LoadLibraryW(L"winmm.dll");
            if (mm) play = reinterpret_cast<PlaySoundFn>(reinterpret_cast<void*>(GetProcAddress(mm, "PlaySoundW")));
            log("callname play: PlaySoundW %s", play ? "loaded" : "not available (winmm.dll)");
        }
        if (!play) continue;
        if (stop)
            play(nullptr, nullptr, 0);
        else if (!play(path.c_str(), nullptr, kSndAsync | kSndFilename | kSndNoDefault))
            log("callname play: PlaySoundW could not play a file");
    }
}

void post(bool stop, std::wstring path) {
    Worker* w = worker();
    std::lock_guard<std::mutex> lock(w->m);
    if (!w->started) {
        w->started = true;
        std::thread(run, w).detach();
    }
    w->stop = stop;
    w->path = std::move(path);
    w->pending = true;
    w->cv.notify_one();
}

class WinWavPlayer : public turbo::WavPlayer {
public:
    bool play(const std::filesystem::path& wav) override {
        if (wav.empty()) return false;
        post(false, wav.wstring());
        return true;
    }
    void stop() override { post(true, std::wstring()); }
};

}  // namespace

std::shared_ptr<turbo::WavPlayer> callname_wav_player() { return std::make_shared<WinWavPlayer>(); }

}  // namespace host
