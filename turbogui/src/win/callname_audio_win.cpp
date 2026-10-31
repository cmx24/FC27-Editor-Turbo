// FC 27 LE Turbo GUI - the Callname tab's play buttons on Windows (see callname_audio_win.h)
#include "callname_audio_win.h"

#include <windows.h>

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
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
    uint64_t seq = 0;       // number of the newest play posted (a stop keeps it)
    uint64_t done_seq = 0;  // the play PlaySoundW last answered for, and its answer
    bool done_ok = true;
    std::string done_why;   // when !done_ok: "PlaySoundW failed (error 2)" / "PlaySoundW not available"
};

Worker* worker() {
    static Worker* w = new Worker();
    return w;
}

// The proof of a play in the log: the file as PlaySoundW gets it, its size and its length from the RIFF header
std::string describe(const std::filesystem::path& p) {
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size(p, ec);
    std::string head;
    if (!ec) {
        std::ifstream f(p, std::ios::binary);
        head.assign(512, '\0');
        f.read(&head[0], static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<size_t>(f.gcount() > 0 ? f.gcount() : 0));
    }
    char buf[64];
    if (ec)
        std::snprintf(buf, sizeof(buf), "missing (%s)", ec.message().c_str());
    else
        std::snprintf(buf, sizeof(buf), "%llu bytes, %.1f s", static_cast<unsigned long long>(size), turbo::wav_seconds(head, size));
    return p.u8string() + ": " + buf;
}

void run(Worker* w) {
    PlaySoundFn play = nullptr;
    bool tried = false;
    for (;;) {
        std::wstring path;
        bool stop = false;
        uint64_t seq = 0;
        {
            std::unique_lock<std::mutex> lock(w->m);
            w->cv.wait(lock, [w] { return w->pending; });
            w->pending = false;
            path.swap(w->path);
            stop = w->stop;
            seq = w->seq;
        }
        if (!tried) {
            tried = true;
            HMODULE mm = GetModuleHandleW(L"winmm.dll");
            if (!mm) mm = LoadLibraryW(L"winmm.dll");
            if (mm) play = reinterpret_cast<PlaySoundFn>(reinterpret_cast<void*>(GetProcAddress(mm, "PlaySoundW")));
            log("callname play: PlaySoundW %s", play ? "loaded" : "not available (winmm.dll)");
        }
        if (stop) {
            if (play) play(nullptr, nullptr, 0);
            continue;
        }
        const std::string what = describe(std::filesystem::path(path));
        BOOL ok = FALSE;
        DWORD err = 0;
        if (play) {
            SetLastError(0);
            ok = play(path.c_str(), nullptr, kSndAsync | kSndFilename | kSndNoDefault);
            if (!ok) err = GetLastError();
        }
        if (!play)
            log("callname play: %s, PlaySoundW not available", what.c_str());
        else if (ok)
            log("callname play: %s, PlaySoundW TRUE", what.c_str());
        else
            log("callname play: %s, PlaySoundW FALSE (GetLastError %lu)", what.c_str(), static_cast<unsigned long>(err));
        char why[64] = "";
        if (!play)
            std::snprintf(why, sizeof(why), "PlaySoundW not available");
        else if (!ok)
            std::snprintf(why, sizeof(why), "PlaySoundW %s (error %lu)", err ? "failed" : "returned FALSE", static_cast<unsigned long>(err));
        std::lock_guard<std::mutex> lock(w->m);
        w->done_seq = seq;
        w->done_ok = play && ok;
        w->done_why = why;
    }
}

uint64_t post(bool stop, std::wstring path) {
    Worker* w = worker();
    std::lock_guard<std::mutex> lock(w->m);
    if (!w->started) {
        w->started = true;
        std::thread(run, w).detach();
    }
    w->stop = stop;
    w->path = std::move(path);
    w->pending = true;
    if (!stop) ++w->seq;
    w->cv.notify_one();
    return w->seq;
}

class WinWavPlayer : public turbo::WavPlayer {
public:
    bool play(const std::filesystem::path& wav) override {
        if (wav.empty()) return false;
        seq_ = post(false, wav.wstring());
        return true;
    }
    void stop() override { post(true, std::wstring()); }
    bool failed(std::string* why) const override {
        Worker* w = worker();
        std::lock_guard<std::mutex> lock(w->m);
        if (!seq_ || w->done_seq != seq_ || w->done_ok) return false;
        if (why) *why = w->done_why;
        return true;
    }

private:
    uint64_t seq_ = 0;  // the play this player posted last (0 = none)
};

}  // namespace

std::shared_ptr<turbo::WavPlayer> callname_wav_player() { return std::make_shared<WinWavPlayer>(); }

}  // namespace host
