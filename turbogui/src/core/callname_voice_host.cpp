// FC 27 LE Turbo GUI - voice swaps: cached switches, observe ring and log lines (see callname_voice_host.h)
#include "callname_voice_host.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace turbo {
namespace voice {

namespace fs = std::filesystem;

namespace {

// Descriptor fields only observe mode reads (docs/re/inmatch-callnames.json "param descriptor")
constexpr uint64_t kDescList = 0x18;    // allowed values (float array, u32 count at -4); read only when +0x44 is set
constexpr uint64_t kDescStrict = 0x45;  // u8

uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
int32_t rdi32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
const uint8_t* rdptr(const uint8_t* p) {
    const uint8_t* v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Case-insensitive, bounded (the game's FindParam compares names without case)
bool same_name(const char* a, const char* b) {
    for (int i = 0; i < kLogName; ++i) {
        const unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
        if (!x) return true;
    }
    return false;
}

// A name as one token of the log line: no spaces, ',', ':' or '='
void append_name(std::string& s, const char* name) {
    const size_t at = s.size();
    for (int i = 0; i < kLogName && name[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        s += (c <= 0x20 || c >= 0x7F || c == ',' || c == ':' || c == '=') ? '?' : static_cast<char>(c);
    }
    if (s.size() == at) s += '?';
}

std::string pid_list(const LogEntry& e, bool after) {
    if (e.n == 0) return "-";
    std::string s;
    for (int i = 0; i < e.n && i < kLogPids; ++i) {
        if (i) s += ',';
        append_name(s, e.pids[i].name);
        s += ':';
        s += std::to_string(after ? e.pids[i].after : e.pids[i].before);
    }
    return s;
}

}  // namespace

// ---------------------------------------------------------------- switches
fs::path feature_off_path(const fs::path& out_dir) { return out_dir / "callname_voice_off.txt"; }
fs::path observe_on_path(const fs::path& out_dir) { return out_dir / "callname_voice_log_on.txt"; }
fs::path observe_log_path(const fs::path& out_dir) { return out_dir / "callname_voice_log.txt"; }

bool Every::due(uint64_t now_ms) {
    uint64_t next = next_.load();
    if (now_ms < next) return false;
    return next_.compare_exchange_strong(next, now_ms + period_);
}

bool FileSwitches::refresh(uint64_t now_ms, const fs::path& out_dir, bool (*exists)(const fs::path&), bool force) {
    if (force) every.force();
    if (!every.due(now_ms) || !exists) return false;
    feature_off.store(exists(feature_off_path(out_dir)));
    observe.store(exists(observe_on_path(out_dir)));
    return true;
}

Live live_from(const SwitchState& s) {
    Live l;
    if (!s.installed || s.feature_off) return l;
    l.observe = s.observe;
    l.voice = s.voices || s.observe;
    l.kickoff = s.kickoffs || s.observe;
    return l;
}

// ---------------------------------------------------------------- observe capture
bool observe_before(const uint8_t* query, LogEntry& e, const uint8_t* pvs[kLogPids]) {
    if (!query) return false;
    const uint8_t* ctx = rdptr(query + kQueryCtx);
    if (!ctx || rd32(ctx + kCtxGroup) != kGroupCommentaryDb) return false;
    e.kind = LogEntry::Query;
    e.ev = rd32(ctx + kCtxEventId);
    e.q = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(query));
    const uint32_t n = rd32(query + kQueryCount);
    if (n > kMaxParams) {
        e.flags |= kFlagBounded;
        return true;
    }
    const uint8_t* arr = rdptr(query + kQueryParams);
    if (!arr) return true;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* pv = rdptr(arr + i * sizeof(void*));
        if (!pv) continue;
        const uint8_t* desc = rdptr(pv + kPvDesc);
        if (!desc) continue;
        const char* name = reinterpret_cast<const char*>(rdptr(desc + kDescName));
        if (!name) continue;
        const bool one_int = rd32(desc + kDescType) == 1 && desc[kDescMulti] == 0;
        if (contains_pid(name)) {
            if (desc[kDescStrict] != 0) e.flags |= kFlagStrict;
            if (desc[kDescMulti] != 0) {  // the game reads the list itself only then
                const uint8_t* list = rdptr(desc + kDescList);
                if (list && rd32(list - 4) != 0) e.flags |= kFlagValueList;
            }
            if (!one_int) {
                e.flags |= kFlagLeftOut;
                continue;
            }
            if (e.n >= kLogPids) {
                ++e.more;
                continue;
            }
            LogEntry::Pid& p = e.pids[e.n];
            int k = 0;
            for (; k < kLogName - 1 && name[k]; ++k) p.name[k] = name[k];
            p.name[k] = 0;
            p.before = p.after = rdi32(pv + kPvValue);
            pvs[e.n++] = pv;
        } else if (one_int && same_name(name, "surname_ID")) {
            e.has_surname = true;
            e.surname = rdi32(pv + kPvValue);
        } else if (one_int && same_name(name, "player_intensity")) {
            e.has_intensity = true;
            e.intensity = rdi32(pv + kPvValue);
        }
    }
    return true;
}

void observe_after(LogEntry& e, const uint8_t* const pvs[kLogPids]) {
    for (int i = 0; i < e.n && i < kLogPids; ++i)
        if (pvs[i]) e.pids[i].after = rdi32(pvs[i] + kPvValue);
}

// ---------------------------------------------------------------- log lines
std::string format_entry(const LogEntry& e) {
    char b[160];
    if (e.kind == LogEntry::Kickoff) {
        char ov[16] = "-";
        if (e.has_override) std::snprintf(ov, sizeof(ov), "%d", e.override_id);
        std::snprintf(b, sizeof(b), "t=%llu kickoff pid=%d game=%d override=%s tid=%u", static_cast<unsigned long long>(e.t_ms), e.pid,
                      e.game, ov, e.tid);
        return b;
    }
    if (e.kind != LogEntry::Query) return std::string();
    std::snprintf(b, sizeof(b), "t=%llu tid=%u ev=0x%08X q=0x%llX pid_before=", static_cast<unsigned long long>(e.t_ms), e.tid, e.ev,
                  static_cast<unsigned long long>(e.q));
    std::string s = b;
    s += pid_list(e, false);
    s += " pid_after=";
    s += pid_list(e, true);
    s += " surname=";
    s += e.has_surname ? std::to_string(e.surname) : "-";
    s += " intensity=";
    s += e.has_intensity ? std::to_string(e.intensity) : "-";
    std::snprintf(b, sizeof(b), " flags=0x%X guard=%d", e.flags, e.guard ? 1 : 0);
    s += b;
    if (e.more) s += " more=" + std::to_string(e.more);
    return s;
}

// ---------------------------------------------------------------- the ring (bounded MPMC sequence ring, one reader)
LogRing::LogRing(uint32_t size) {
    uint32_t n = 2;
    while (n < size && n < (1u << 30)) n <<= 1;
    cells_.reset(new Cell[n]);
    mask_ = n - 1;
    for (uint32_t i = 0; i < n; ++i) cells_[i].seq.store(i, std::memory_order_relaxed);
}

bool LogRing::push(const LogEntry& e) {
    uint64_t pos = head_.load(std::memory_order_relaxed);
    for (;;) {
        Cell& c = cells_[pos & mask_];
        const uint64_t seq = c.seq.load(std::memory_order_acquire);
        const int64_t diff = static_cast<int64_t>(seq - pos);
        if (diff == 0) {
            if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                c.e = e;
                c.seq.store(pos + 1, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {  // the reader has not taken this cell yet: full
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        } else {
            pos = head_.load(std::memory_order_relaxed);
        }
    }
}

bool LogRing::pop(LogEntry& out) {
    const uint64_t pos = tail_.load(std::memory_order_relaxed);
    Cell& c = cells_[pos & mask_];
    if (c.seq.load(std::memory_order_acquire) != pos + 1) return false;  // empty, or a writer is still filling it
    out = c.e;
    c.seq.store(pos + mask_ + 1, std::memory_order_release);
    tail_.store(pos + 1, std::memory_order_relaxed);
    return true;
}

size_t drain_lines(LogRing& ring, std::string& text, size_t max) {
    size_t lines = 0;
    LogEntry e;
    while (lines < max && ring.pop(e)) {
        const std::string line = format_entry(e);
        if (line.empty()) continue;
        text += line;
        text += '\n';
        ++lines;
    }
    return lines;
}

}  // namespace voice
}  // namespace turbo
