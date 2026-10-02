// FC 27 LE Turbo GUI - Live Editor log reader (see le_log.h)
#include "core/le_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace turbo {

static const char* const kSetupDone = "Initial setup done";

std::string le_log_header(uint64_t le_base) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Module <FCLiveEditor.DLL> 0x%llX-", static_cast<unsigned long long>(le_base));
    return buf;
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

LeLogState le_log_state(const std::string& text, uint64_t le_base) {
    if (le_base == 0 || text.empty()) return LeLogState::NoSession;
    // Case-insensitive on the whole text (hex digits may be printed either way)
    const std::string hay = upper(text);
    const std::string header = upper(le_log_header(le_base));
    const size_t at = hay.rfind(header);
    if (at == std::string::npos) return LeLogState::NoSession;
    // Only this session's lines count: stop at the next session's header, whatever its module base
    const size_t next = hay.find("MODULE <FCLIVEEDITOR.DLL>", at + header.size());
    const size_t done = hay.find(upper(kSetupDone), at + header.size());
    return done != std::string::npos && (next == std::string::npos || done < next) ? LeLogState::Done : LeLogState::Waiting;
}

}  // namespace turbo
