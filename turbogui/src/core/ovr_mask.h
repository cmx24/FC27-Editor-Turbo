// FC 27 LE Turbo GUI - the "Shown OVR" store (3.2 calc-ovr, Pit wall addition 2026-10-25).
//
// A per-career, per-player number the player header lets the user type next to the OVR ("Shown OVR"). This header is the store only:
// nothing here writes the players table or game memory. Turbo X (11-01, tx-ovr2): core/ovr_pin.h + App::ovr_pin_tick read this file
// and pin the player's stored players.overallrating to the value (the screens show the stored overall; the game rewrites it at its
// growth updates, so the pin is written again then). Not a display-only mask: the game's own uses of the stored overall see it.
//
// File: <LE>\turbo_output\careers\<career id>\ovr_mask.json, the LOCKED schema
//     {"version":1,"players":{"<pid decimal string>":<int 1..99>}}
// Atomic write (<file>.tmp + replace; the real file is never deleted first). Clear removes the pid, and removes the file when no pid is left.
// career id = the 16 lower-case hex digits of core/reapply.h career_identity_id (career_faces::is_career_id).
// Header-only so no build list changes.
#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include "nlohmann/json.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace turbo {
namespace ovr_mask {

constexpr int kMinShown = 1;
constexpr int kMaxShown = 99;

inline bool valid_value(int v) { return v >= kMinShown && v <= kMaxShown; }

// A career folder name is exactly 16 lower-case hex digits (the same rule as career_faces::is_career_id; kept local so the
// header stands alone)
inline bool valid_career(const std::string& id) {
    if (id.size() != 16) return false;
    for (char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline std::filesystem::path file_for(const std::filesystem::path& turbo_output, const std::string& career) {
    return turbo_output / "careers" / career / "ovr_mask.json";
}

using Map = std::map<int64_t, int>;

// Reads the file. False with *err when it exists but is not the locked schema (never acted on); a missing file is an empty map, true.
inline bool load(const std::filesystem::path& f, Map& out, std::string* err = nullptr) {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::exists(f, ec)) return true;
    std::ifstream in(f, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot read " + f.string();
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        const nlohmann::json j = nlohmann::json::parse(ss.str());
        if (!j.is_object() || j.value("version", 0) != 1 || !j.contains("players") || !j["players"].is_object())
            throw std::runtime_error("not the version 1 schema");
        for (auto it = j["players"].begin(); it != j["players"].end(); ++it) {
            const std::string& k = it.key();
            if (k.empty() || k.find_first_not_of("0123456789") != std::string::npos || !it.value().is_number_integer())
                throw std::runtime_error("bad entry '" + k + "'");
            const int v = it.value().get<int>();
            if (!valid_value(v)) throw std::runtime_error("value out of 1..99 for " + k);
            out[std::stoll(k)] = v;
        }
    } catch (const std::exception& e) {
        out.clear();
        if (err) *err = std::string("ovr_mask.json unreadable: ") + e.what();
        return false;
    }
    return true;
}

inline std::string dump(const Map& m) {
    nlohmann::json j;
    j["version"] = 1;
    j["players"] = nlohmann::json::object();
    for (const auto& kv : m) j["players"][std::to_string(kv.first)] = kv.second;
    return j.dump() + "\n";
}

// Replaces <to> by <from> in one step. Windows: MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH) (the data is on disk when it returns);
// elsewhere std::filesystem::rename. A test can swap it through replace_hook() to make the replacement fail. Never deletes <to>.
using ReplaceFn = bool (*)(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);
inline ReplaceFn& replace_hook() {
    static ReplaceFn h = nullptr;
    return h;
}
inline bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
    if (replace_hook()) return replace_hook()(from, to, ec);
    ec.clear();
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    return true;
#else
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

// <path>.tmp + replace; creates the folder. The real file <p> is NEVER deleted before its replacement is in place: when the replacement
// fails twice (one retry after 50 ms: a virus scanner or the game can hold the file for a moment), <p> keeps its old bytes, only the
// .tmp is removed, and the result is false with the Windows error in *err. (3.2 review fix: the old fallback deleted <p> first and
// lost every saved value when the second rename failed too.)
inline bool write_atomic(const std::filesystem::path& p, const std::string& bytes, std::string* err) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.flush();
        if (!f) {
            f.close();
            std::filesystem::remove(tmp, ec);
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    if (replace_file(tmp, p, ec)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ec.clear();
    if (replace_file(tmp, p, ec)) return true;
    const std::string why = ec.message();
    std::error_code ec2;
    std::filesystem::remove(tmp, ec2);  // only the temp file: <p> stays exactly as it was
    if (err) *err = "replacing " + p.string() + " failed: " + why + " (the old file is untouched)";
    return false;
}

// Sets the shown OVR of one player in one career. Refused (nothing written) for a bad career id, a pid <= 0, a value outside 1..99,
// or an existing file that is not the locked schema.
inline bool set(const std::filesystem::path& turbo_output, const std::string& career, int64_t pid, int value, std::string* err) {
    if (!valid_career(career)) {
        if (err) *err = "not inside an identified career: nothing written";
        return false;
    }
    if (pid <= 0) {
        if (err) *err = "no player picked";
        return false;
    }
    if (!valid_value(value)) {
        if (err) *err = "Shown OVR must be a whole number from 1 to 99";
        return false;
    }
    const std::filesystem::path f = file_for(turbo_output, career);
    Map m;
    if (!load(f, m, err)) return false;
    m[pid] = value;
    return write_atomic(f, dump(m), err);
}

// Clears the shown OVR of one player: the pid is removed, and the file when it is empty. True when nothing was there either.
inline bool clear(const std::filesystem::path& turbo_output, const std::string& career, int64_t pid, std::string* err) {
    if (!valid_career(career)) {
        if (err) *err = "not inside an identified career: nothing written";
        return false;
    }
    const std::filesystem::path f = file_for(turbo_output, career);
    Map m;
    if (!load(f, m, err)) return false;
    if (m.erase(pid) == 0) return true;
    if (m.empty()) {
        std::error_code ec;
        std::filesystem::remove(f, ec);
        if (ec) {
            if (err) *err = "cannot remove " + f.string();
            return false;
        }
        return true;
    }
    return write_atomic(f, dump(m), err);
}

// The shown OVR of a player, 0 = none (or no career / unreadable file)
inline int get(const std::filesystem::path& turbo_output, const std::string& career, int64_t pid) {
    if (!valid_career(career)) return 0;
    Map m;
    if (!load(file_for(turbo_output, career), m, nullptr)) return 0;
    const auto it = m.find(pid);
    return it == m.end() ? 0 : it->second;
}

}  // namespace ovr_mask
}  // namespace turbo
