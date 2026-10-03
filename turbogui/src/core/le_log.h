// FC 27 LE Turbo GUI - reads Live Editor's own log (<Live Editor>\Logs\live_editor_<date>.log) to tell when Live Editor has
// finished setting up the game in this process.
//
// Live Editor writes, for every game process it is injected into:
//   "<time>\tINFO\tModule <FCLiveEditor.DLL> 0x<base>-0x<end>"   when it starts (before its Lua autorun scripts run)
//   "<time>\tINFO\tInitial setup done"                           after its own Direct3D 12 hooks and the main menu
// The module base identifies the session: Turbo.dll knows it from GetModuleHandle("FCLiveEditor.DLL"). The same base can
// be reused by a later game session, so the LAST header with that base is this process's session.
#pragma once

#include <cstdint>
#include <string>

namespace turbo {

enum class LeLogState {
    NoSession,  // no header for this Live Editor module in the text
    Waiting,    // this session has started, setup not reported yet
    Done,       // this session reported "Initial setup done"
};

LeLogState le_log_state(const std::string& text, uint64_t le_base);

// "Module <FCLiveEditor.DLL> 0x<BASE>-" as Live Editor prints it (upper-case hex)
std::string le_log_header(uint64_t le_base);

}  // namespace turbo
