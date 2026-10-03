// FC 27 LE Turbo GUI - bridge to Turbo's Lua side running inside Live Editor's Lua engine.
//
// Files in <Live Editor folder>\turbo_output (or the Live Editor folder itself as a fallback):
//   bridge_meta.json   written by Lua: database meta from GetDBMeta()
//   bridge_state.json  written by Lua: plugin addresses (hex strings), career state, in-game date
//   bridge_dll.json    written by the GUI: mailbox address + session
//
// Mailbox (allocated by the GUI, polled by Lua on every career-mode event):
//   +0x00 u32 magic 'TRBO'   +0x04 u32 version   +0x08 i32 command seq   +0x0C i32 ack seq
//   +0x10 i32 status (1 ok, 0 failed)   +0x14 i32 Lua heartbeat
//   +0x20 char[4096] command JSON        +0x1020 char[4096] result text
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include "mem.h"
#include "t3db.h"
#include "model.h"

namespace turbo {

constexpr uint32_t kMailboxMagic = 0x4F425254;  // "TRBO" little-endian
constexpr uint32_t kMailboxVersion = 1;
constexpr uint64_t kMbCmdSeq = 0x08, kMbAckSeq = 0x0C, kMbStatus = 0x10, kMbHeartbeat = 0x14;
constexpr uint64_t kMbCmd = 0x20, kMbResult = 0x1020, kMbTextSize = 0x1000, kMailboxSize = 0x2020;

struct BridgeState {
    bool loaded = false;
    std::string session;
    long long seq = 0;
    long long db_gen = 0;  // changes when a save is loaded / career entered or left
    std::string le_version;
    uint64_t db_service = 0;
    uint64_t comm_service = 0;
    uint64_t ifce = 0;
    bool in_cm = false;
    int64_t user_team = 0;
    GameDate date;
    // Effective Turbo settings (turbo_config.json + gui_settings.json) as Lua last saw them
    bool has_settings = false;
    bool dry_run = false;
    bool auto_form_enabled = false;
    int auto_form = 100, auto_morale = 100, auto_fitness = 0;
    bool auto_playstyles_enabled = false;
    std::string meta_error;  // why Turbo's Lua side could not write bridge_meta.json (empty = no problem reported)
    std::string error;
};

uint64_t parse_hex_addr(const std::string& s);
std::string hex_addr(uint64_t v);

class Bridge {
public:
    explicit Bridge(std::filesystem::path le_root) : root_(std::move(le_root)) {}

    std::filesystem::path dir() const { return root_ / "turbo_output"; }
    // Existing file in dir(), else in the Live Editor folder
    std::filesystem::path locate(const char* name) const;
    const std::filesystem::path& root() const { return root_; }

    // Re-read Lua files when they changed. Returns true if anything new was loaded.
    bool poll_files();
    const BridgeState& state() const { return state_; }
    const DbMeta& meta() const { return meta_; }
    bool meta_loaded() const { return !meta_.empty(); }
    std::string meta_error() const { return meta_error_; }

    // Parse helpers (also used by tests)
    static bool parse_meta(const std::string& json_text, DbMeta& out, std::string* err);
    static bool parse_state(const std::string& json_text, BridgeState& out);

    // Publish the mailbox so Lua can find it
    bool publish_mailbox(uint64_t mailbox_addr, const std::string& session, const std::string& gui_version);

    // Turbo GUI settings merged by Lua on top of turbo_config.json
    bool write_gui_settings(const std::string& json_text);

private:
    std::filesystem::path root_;
    BridgeState state_;
    DbMeta meta_;
    std::string meta_error_;
    std::filesystem::file_time_type meta_time_{}, state_time_{};
    bool have_meta_time_ = false, have_state_time_ = false;
    int meta_failures_ = 0, state_failures_ = 0;
};

// GUI side of the mailbox protocol
class Mailbox {
public:
    Mailbox(Memory& mem, uint64_t addr) : mem_(mem), addr_(addr) {}
    bool init();                         // writes magic/version, clears state
    uint64_t addr() const { return addr_; }

    // Queue a command (JSON text). Fails while a previous command is still pending.
    bool submit(const std::string& json, std::string* err = nullptr);
    bool pending();
    // When the last command finished: true and fills ok/result
    bool take_result(bool& ok, std::string& result);
    // Withdraw a command Lua has not picked up; a late answer to it is ignored
    void cancel();
    int32_t heartbeat();
    int32_t seq() const { return seq_; }

private:
    Memory& mem_;
    uint64_t addr_;
    int32_t seq_ = 0;
    int32_t taken_ = 0;
};

}  // namespace turbo
