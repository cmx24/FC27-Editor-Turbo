// FC 27 LE Turbo GUI - layout store (5.0b-B; Cassio 10-29 23:00: "window resizes and column/row resizes need to be SAVED").
//
// Owns the Dear ImGui ini text. Until now the overlay handed ImGui a file name (io.IniFilename) and trusted its timed autosave; nothing in
// Turbo ever wrote, read back or logged that file, and a failed write was invisible. This store takes the file over:
//   * io.IniFilename is nullptr (the overlay sets it); the same file, the same format, the same name keep today's layouts loading:
//     <le_root>\turbo_output\turbo_gui_layout_v2.ini, read once at start with ImGui::LoadIniSettingsFromMemory.
//   * pump(now), called every frame after NewFrame (App::draw): when ImGui says io.WantSaveIniSettings (its timer, io.IniSavingRate, set to
//     1 s by the overlay) the text is taken with SaveIniSettingsToMemory, written atomically (tmp + rename) through the sink, the flag is
//     cleared, and one log line says so: "layout saved: <path> <bytes> tables=<n> windows=<n> (<why>)". Autosave lines are limited to one
//     per 10 s (kLogEvery); a save at hide, after Undo and every failure is always logged.
//   * save_now("hide") when the panel is hidden (frame(visible, now) sees the transition) so the toggle key never loses a drag.
//   * NOT done: a save at game exit. The overlay has no frame at process exit (dllmain.cpp DLL_PROCESS_DETACH runs under the loader lock,
//     where no ImGui call is safe), so the last second of changes before the game closes is the one thing that can be lost (IniSavingRate 1 s).
//   * reset(stamp): Settings > Layout > Reset layout. Parks the stored file as "<name>.bak-<stamp>", clears ImGui's settings and PAUSES the
//     saves, because the live windows and tables still hold their sizes and the next save would write them straight back. The defaults are
//     in effect the next time the game starts (ImGui has no call that resets a table that already exists). undo_reset() puts the parked file
//     back, loads it and resumes the saves, while the overlay runs.
// A Sink is a set of callbacks so the tests run without a disk (tests/native/test_layout_store.h) and the overlay passes file_sink().
// Header only: no source list (run_native.sh, build_win.sh) changes.
#pragma once
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

#include "imgui.h"

namespace ImGui {
IMGUI_API void ClearIniSettings();  // imgui_internal.h
}

namespace turbo {

class LayoutStore {
public:
    static constexpr double kLogEvery = 10.0;   // seconds between two autosave log lines
    static constexpr double kRetryEvery = 5.0;  // seconds before a failed write is tried again

    struct Sink {
        std::function<bool(std::string&)> read;                    // the stored text; false when there is none
        std::function<bool(const std::string&)> write;             // replace the stored text atomically
        std::function<std::string(const std::string&)> park;       // move the stored text aside as "<name>.bak-<stamp>"; returns its name ("" = none)
        std::function<bool(const std::string&)> unpark;            // move a parked text back over the stored one (optional; write() is the fallback)
        std::function<void(const std::string&)> log;               // one line to the in-app Log and turbo_gui.log
        std::string label;                                         // the stored text's name in log lines (the path)
    };

    void set_sink(Sink s) { sink_ = std::move(s); }
    bool configured() const { return static_cast<bool>(sink_.write); }
    const std::string& label() const { return sink_.label; }

    // At start, after CreateContext and before the first NewFrame (a later call is safe: live windows and tables are re-applied).
    bool load() {
        if (!sink_.read) return false;
        std::string text;
        if (!sink_.read(text) || text.empty()) {
            say("layout loaded: none at " + sink_.label + " (defaults)");
            return false;
        }
        ImGui::LoadIniSettingsFromMemory(text.c_str(), text.size());
        last_text_ = text;
        int tables = 0, windows = 0;
        count_blocks(text, tables, windows);
        say("layout loaded: " + sink_.label + " " + std::to_string(text.size()) + " tables=" + std::to_string(tables) + " windows=" + std::to_string(windows));
        return true;
    }

    // Every frame after NewFrame. True when a save was written.
    bool pump(double now) {
        if (!sink_.write) return false;  // an unconfigured store never eats ImGui's flag (the tests read it)
        ImGuiIO& io = ImGui::GetIO();
        const bool wanted = io.WantSaveIniSettings;
        io.WantSaveIniSettings = false;  // ImGui never clears it itself
        if (paused_) return false;
        if (!wanted && !(failing_ && now >= retry_at_)) return false;
        return save("autosave", now, true);
    }

    // The panel hidden: save at once (the transition is seen by frame()).
    void frame(bool visible, double now) {
        if (seen_visible_ && !visible && sink_.write && !paused_) save("hide", now, false);
        seen_visible_ = visible;
        pump(now);
    }

    bool save_now(const char* why, double now) { return sink_.write && !paused_ && save(why, now, false); }

    // Settings > Layout > Reset layout (after its confirm). False when the store has no sink or a reset is already waiting for Undo.
    bool reset(const std::string& stamp) {
        if (!sink_.write || reset_done_) return false;
        parked_text_.clear();
        if (sink_.read) {
            std::string t;
            if (sink_.read(t)) parked_text_ = t;
        }
        parked_ = sink_.park ? sink_.park(stamp) : std::string();
        ImGui::ClearIniSettings();
        paused_ = true;
        reset_done_ = true;
        failing_ = false;
        say("layout reset: " + sink_.label + " parked as " + (parked_.empty() ? std::string("(nothing stored)") : parked_) +
            "; the default sizes apply when the game starts again, saves are paused until then or until Undo");
        return true;
    }

    // Settings > Layout > Undo reset, while the overlay runs. Puts the parked text back, loads it and resumes the saves.
    bool undo_reset(double now) {
        if (!reset_done_) return false;
        bool restored = true;
        if (!parked_text_.empty()) {
            restored = (sink_.unpark && !parked_.empty() && sink_.unpark(parked_)) || (sink_.write && sink_.write(parked_text_));
            if (restored) ImGui::LoadIniSettingsFromMemory(parked_text_.c_str(), parked_text_.size());
        }
        paused_ = false;
        reset_done_ = false;
        last_text_ = parked_text_;
        say(std::string("layout reset undone: ") + sink_.label + (restored ? " restored" : " NOT restored (could not write it back)") +
            ", saves resumed");
        parked_.clear();
        parked_text_.clear();
        if (restored) save("undo reset", now, false);
        return restored;
    }

    bool paused() const { return paused_; }
    bool can_undo() const { return reset_done_; }
    const std::string& parked_name() const { return parked_; }
    const std::string& last_text() const { return last_text_; }
    int saves_written() const { return saves_; }

    // "[Table][...]" and "[Window][...]" blocks of an ini text
    static void count_blocks(const std::string& text, int& tables, int& windows) {
        tables = 0;
        windows = 0;
        size_t p = 0;
        while (p < text.size()) {
            size_t e = text.find('\n', p);
            if (e == std::string::npos) e = text.size();
            if (text.compare(p, 8, "[Table][") == 0) ++tables;
            else if (text.compare(p, 9, "[Window][") == 0) ++windows;
            p = e + 1;
        }
    }

    // The file sink of the overlay: the ini at `path`, the .bak next to it, lines to `log`.
    static Sink file_sink(const std::filesystem::path& path, std::function<void(const std::string&)> log) {
        Sink s;
        s.label = path.generic_string();
        s.log = std::move(log);
        s.read = [path](std::string& out) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return true;
        };
        s.write = [path](const std::string& text) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::filesystem::path tmp = path;
            tmp += ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return false;
                f << text;
                f.flush();
                if (!f) return false;
            }
            std::filesystem::rename(tmp, path, ec);
            if (ec) {  // a platform whose rename does not replace
                std::filesystem::remove(path, ec);
                ec.clear();
                std::filesystem::rename(tmp, path, ec);
            }
            return !ec;
        };
        s.park = [path](const std::string& stamp) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) return std::string();
            std::filesystem::path bak = path;
            bak += ".bak-" + stamp;
            std::filesystem::rename(path, bak, ec);
            return ec ? std::string() : bak.generic_string();
        };
        s.unpark = [path](const std::string& parked) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(std::filesystem::path(parked), path, ec);
            return !ec;
        };
        return s;
    }

    // "20261030_011542" (local time), the stamp of a .bak
    static std::string stamp_now() {
        const std::time_t t = std::time(nullptr);
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char b[32];
        std::strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &tm_buf);
        return b;
    }

private:
    Sink sink_;
    std::string last_text_;      // what the stored file holds (as last written or loaded)
    std::string parked_;         // name of the parked file after a reset
    std::string parked_text_;    // its text, kept so Undo works whatever the sink is
    bool paused_ = false;
    bool reset_done_ = false;
    bool failing_ = false;
    bool seen_visible_ = false;
    double retry_at_ = 0.0;
    double last_log_ = -1e9;
    int suppressed_ = 0;
    int saves_ = 0;

    void say(const std::string& line) {
        if (sink_.log) sink_.log(line);
    }

    bool save(const char* why, double now, bool autosave) {
        size_t n = 0;
        const char* d = ImGui::SaveIniSettingsToMemory(&n);
        const std::string text = d ? std::string(d, n) : std::string();
        const bool unchanged = !text.empty() && text == last_text_ && !failing_;
        if (unchanged && autosave) return false;  // a timer with nothing new: no write, no line
        const bool was_failing = failing_;
        if (!unchanged) {
            if (!sink_.write(text)) {
                failing_ = true;
                retry_at_ = now + kRetryEvery;
                if (now - last_log_ >= kLogEvery) {
                    last_log_ = now;
                    say("layout save FAILED: could not write " + sink_.label + " (" + why + "), trying again in " + std::to_string(static_cast<int>(kRetryEvery)) + " s");
                }
                return false;
            }
            failing_ = false;
            last_text_ = text;
            ++saves_;
        }
        if (!autosave || was_failing || now - last_log_ >= kLogEvery) {  // a recovery after a failure is always said
            int tables = 0, windows = 0;
            count_blocks(text, tables, windows);
            std::string line = "layout saved: " + sink_.label + " " + std::to_string(text.size()) + " tables=" + std::to_string(tables) +
                               " windows=" + std::to_string(windows) + " (" + why + (unchanged ? ", unchanged" : "");
            if (suppressed_ > 0) line += ", +" + std::to_string(suppressed_) + " earlier saves not logged";
            line += ")";
            last_log_ = now;
            suppressed_ = 0;
            say(line);
        } else {
            ++suppressed_;
        }
        return !unchanged;
    }
};

}  // namespace turbo
