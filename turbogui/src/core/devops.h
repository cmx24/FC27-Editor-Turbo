// FC 27 LE Turbo GUI - dev service: memory tools for finding game data while the game runs (used to locate the
// structures behind Turbo's features: league tables, results, job offers, ...). Offline career only.
//
// Requests: turbo_output\turbo_dev_request.json  {"id": <n>, "op": "...", ...}
// Results:  turbo_output\turbo_dev_result.json   {"id": <n>, "op": "...", "ok": true|false, "error": "...", ...}
// A request runs once per new id. Switched off by the environment variable TURBO_GUI_NO_DEVTOOLS=1 or the file
// turbo_output\turbo_dev_disable.txt.
//
// Operations (addresses and byte strings are hex text):
//   ping                                          -> version
//   read / bytes {addr, len<=65536}               -> hex
//   ptrs {addr, count<=512}                       -> u64 values
//   write {addr, hex}                             -> bytes written (read back to check)
//   region {addr}                                 -> the readable region holding addr
//   regions {writable?, min_size?}                -> region list (at most 5000)
//   modules                                       -> loaded modules (name, base, size)
//   find {pattern "48 8B ?? 05", max<=1000, from?, to?, writable?}   -> addresses (bounded by time)
//   scan {type i8|i16|i32|i64|f32|f64, value, from?, to?, writable?} -> candidate count (kept for refine)
//   refine {value} | {cmp: changed|unchanged|increased|decreased}   -> candidates left
//   changed                                       -> refine {cmp: changed}
//   list {max<=1000}                              -> candidates with current values
//   dump {addr, len<=1 GiB, file}                 -> raw bytes into turbo_output\<file>
//   key {vk, ms<=2000}                            -> key pressed for the game (only when the game is in front)
//   multi {ops: [...]}                            -> results of each (stops at none)
//   seq {steps: [... {op: "sleep", ms}]}          -> like multi, with pauses
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "mem.h"
#include "memmap.h"
#include "nlohmann/json.hpp"

namespace turbo {

struct DevRegion {
    uint64_t start = 0, end = 0;
    bool writable = false;
    std::string kind;  // "image", "private", "mapped"
};

struct DevEnv {
    Memory* mem = nullptr;
    std::function<std::vector<DevRegion>()> regions;                 // readable regions, sorted
    std::function<std::string(int vk, int ms, bool* sent)> key;      // "" or a reason; sent = key went to the game
    std::function<nlohmann::json()> modules;
    std::function<void(int ms)> sleep;
    std::function<double()> clock;  // seconds
    std::filesystem::path out_dir;
    double time_budget = 45.0;      // seconds per find / scan
};

class DevService {
public:
    explicit DevService(DevEnv env) : env_(std::move(env)) {}
    nlohmann::json run(const nlohmann::json& req);
    size_t candidates() const { return cand_.size(); }

    static constexpr size_t kMaxCandidates = 4u << 20;

private:
    nlohmann::json op(const nlohmann::json& req, int depth);
    std::vector<DevRegion> pick_regions(const nlohmann::json& req);

    DevEnv env_;
    std::vector<uint64_t> cand_;
    std::vector<uint64_t> cand_val_;  // raw bits of the last value
    std::string cand_type_;
};

// "48 8B ?? 05" / "488B??05" -> bytes + mask (false = wildcard). false when malformed.
bool parse_pattern(const std::string& text, std::vector<uint8_t>& bytes, std::vector<bool>& mask);
bool parse_hex_bytes(const std::string& text, std::vector<uint8_t>& out);
std::string to_hex(const uint8_t* p, size_t n);

}  // namespace turbo
