// FC 27 LE Turbo GUI - real time for the GUI's timers (host.h real_seconds): Live Editor's speedhack scales
// QueryPerformanceCounter and GetTickCount64, not the system time.
#include <atomic>
#include <cstdint>
#include <mutex>

#include "host.h"

namespace host {

double real_seconds() {
    static std::mutex m;
    static bool started = false;
    static uint64_t last_raw = 0;
    static double value = 0.0;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const uint64_t raw = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;  // 100 ns units
    std::lock_guard<std::mutex> lock(m);
    if (started && raw > last_raw) value += static_cast<double>(raw - last_raw) / 1e7;
    started = true;
    last_raw = raw;  // set back: the clock stands still and goes on from the new time
    return value;
}

double qpc_seconds() {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? static_cast<double>(f.QuadPart) : 1.0;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / freq;
}

namespace {
std::atomic<double> g_speed{1.0};
}

void qpc_speed_update() {
    static double real0 = -1.0, qpc0 = 0.0;
    const double r = real_seconds(), q = qpc_seconds();
    if (real0 < 0.0) {
        real0 = r;
        qpc0 = q;
        return;
    }
    const double dr = r - real0;
    if (dr < 1.0) return;
    const double s = (q - qpc0) / dr;
    if (s > 0.01 && s < 1000.0) g_speed = s;
    real0 = r;
    qpc0 = q;
}

double qpc_speed() { return g_speed.load(); }

}  // namespace host
