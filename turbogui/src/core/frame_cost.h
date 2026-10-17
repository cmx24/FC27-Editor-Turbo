// FC 27 LE Turbo GUI - what Turbo's own frame costs (2.16 gui/perf). The overlay builds its frame inside the game's Present
// (win/overlay_dx12.cpp render_frame), so this time is added to every game frame, and while Turbo is shown the pointer is
// drawn by Dear ImGui at that frame rate (Cassio 2026-10-17: "the status screen lags so bad it seems to freeze").
// The host feeds one sample per frame: the page drawn ("hidden" while Turbo is not shown), App::tick's time and the rest
// (input, the ImGui frame, App::draw, Render). Lines for turbo_gui.log come back:
//   - for the page left, when the page changes (if it ran kFrameCostMinFrames frames),
//   - every kFrameCostShownSec while the same page stays shown, every kFrameCostHiddenSec while hidden,
//   - at once for a frame over kFrameSlowMs (at most one such line every kFrameSlowLogSec).
// Header-only, no Win32: tested natively (tests/native/test_gui_perf.h).
#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace turbo {

constexpr double kFrameCostShownSec = 30.0;
constexpr double kFrameCostHiddenSec = 600.0;
constexpr int kFrameCostMinFrames = 60;
constexpr double kFrameSlowMs = 50.0;
constexpr double kFrameSlowLogSec = 10.0;

class FrameCost {
public:
    // One frame. `now` in seconds; `page` = the page drawn or "hidden".
    std::vector<std::string> add(const std::string& page, double now, double tick_ms, double draw_ms) {
        std::vector<std::string> out;
        if (page != page_) {
            if (frames_ >= kFrameCostMinFrames) out.push_back(line("left"));
            reset(page, now);
        }
        const double ms = tick_ms + draw_ms;
        ++frames_;
        sum_tick_ += tick_ms;
        sum_draw_ += draw_ms;
        max_ = std::max(max_, ms);
        if (ms >= kFrameSlowMs) {
            ++slow_;
            if (now - last_slow_log_ >= kFrameSlowLogSec) {
                last_slow_log_ = now;
                char buf[200];
                std::snprintf(buf, sizeof(buf), "slow Turbo frame on %s: %.1f ms (App::tick %.1f ms, input + draw + render %.1f ms)",
                              page_.c_str(), ms, tick_ms, draw_ms);
                out.push_back(buf);
            }
        }
        const double every = page_ == "hidden" ? kFrameCostHiddenSec : kFrameCostShownSec;
        if (now - since_ >= every && frames_ >= kFrameCostMinFrames) {
            out.push_back(line("so far"));
            reset(page_, now);
        }
        return out;
    }
    int frames() const { return frames_; }
    const std::string& page() const { return page_; }

private:
    std::string page_;
    double since_ = 0.0;
    int frames_ = 0, slow_ = 0;
    double sum_tick_ = 0.0, sum_draw_ = 0.0, max_ = 0.0;
    double last_slow_log_ = -1e9;

    void reset(const std::string& page, double now) {
        page_ = page;
        since_ = now;
        frames_ = slow_ = 0;
        sum_tick_ = sum_draw_ = max_ = 0.0;
    }
    std::string line(const char* why) const {
        char buf[260];
        const double n = frames_ > 0 ? static_cast<double>(frames_) : 1.0;
        std::snprintf(buf, sizeof(buf),
                      "Turbo frame cost on %s (%s): %.3f ms per frame (App::tick %.3f, input + draw + render %.3f), max %.1f ms, "
                      "%d frames over %.0f ms, %d frames",
                      page_.c_str(), why, (sum_tick_ + sum_draw_) / n, sum_tick_ / n, sum_draw_ / n, max_, slow_, kFrameSlowMs, frames_);
        return buf;
    }
};

}  // namespace turbo
