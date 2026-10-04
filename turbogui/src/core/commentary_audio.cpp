// FC 27 LE Turbo GUI - the spoken set asked from the game's audio service (see commentary_audio.h)
#include "commentary_audio.h"

#include <algorithm>
#include <cstdio>

namespace turbo {
namespace caudio {

static std::string hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
    return b;
}

const std::vector<FnSignature>& fn_signatures() {
    static const std::vector<FnSignature> v = {
        {"commentary_service_registry", &Fns::registry},
        {"commentary_service_get", &Fns::get_service},
        {"commentary_filter_names", &Fns::filter_names},
        {"commentary_service_vtable", &Fns::service_vtable},
        {"commentary_names_vtable", &Fns::names_vtable},
        {"speech_query_ctor", &Fns::query_ctor},
        {"speech_query_set_int", &Fns::query_set_int},
        {"speech_query_dtor", &Fns::query_dtor},
        {"scratch_scope_ctor", &Fns::scope_ctor},
        {"scratch_scope_dtor", &Fns::scope_dtor},
        {"commentary_str_bridge", &Fns::s_bridge},
        {"commentary_str_db_events", &Fns::s_db_events},
        {"commentary_str_player_name_fe", &Fns::s_player_name_fe},
        {"commentary_str_player_intensity", &Fns::s_player_intensity},
        {"commentary_str_surname_id", &Fns::s_surname_id},
        {"commentary_str_player_low_simple", &Fns::s_player_low_simple},
        {"commentary_str_player_low_link", &Fns::s_player_low_link},
        {"commentary_str_player_db_pid", &Fns::s_player_db_pid},
    };
    return v;
}

const char* Fns::missing_names() const {
    if (!registry) return "commentary_service_registry";
    if (!get_service) return "commentary_service_get";
    if (!filter_names) return "commentary_filter_names";
    return nullptr;
}

const char* Fns::missing_players() const {
    if (const char* m = missing_names()) return m;
    if (!query_ctor) return "speech_query_ctor";
    if (!query_set_int) return "speech_query_set_int";
    if (!query_dtor) return "speech_query_dtor";
    if (!scope_ctor) return "scratch_scope_ctor";
    if (!scope_dtor) return "scratch_scope_dtor";
    if (!s_bridge) return "commentary_str_bridge";
    if (!s_db_events) return "commentary_str_db_events";
    if (!s_player_intensity) return "commentary_str_player_intensity";
    if (!s_player_low_simple) return "commentary_str_player_low_simple";
    if (!s_player_low_link) return "commentary_str_player_low_link";
    if (!s_player_db_pid) return "commentary_str_player_db_pid";
    return nullptr;
}

bool resolve_chain(Memory& mem, uint64_t service, const Fns& f, Chain& out, std::string& err) {
    out = Chain{};
    if (!is_ptr(service, 8)) {
        err = "the audio service pointer " + hex(service) + " is not a pointer";
        return false;
    }
    uint64_t vt = 0;
    if (!mem.rd(service, vt) || !is_ptr(vt, 8)) {
        err = "the audio service at " + hex(service) + " is not readable";
        return false;
    }
    if (f.service_vtable && vt != f.service_vtable) {
        err = "the object at " + hex(service) + " is not the commentary service (vtable " + hex(vt) + ", expected " + hex(f.service_vtable) + ")";
        return false;
    }
    out.service = service;
    out.names = mem.ptr(service + kServiceNames);
    if (!out.names) {
        err = "the commentary service has no names object (service+0x40)";
        return false;
    }
    uint64_t nvt = 0;
    if (!mem.rd(out.names, nvt) || !is_ptr(nvt, 8)) {
        err = "the names object at " + hex(out.names) + " is not readable";
        return false;
    }
    if (f.names_vtable && nvt != f.names_vtable) {
        err = "the object at " + hex(out.names) + " is not the commentary names object (vtable " + hex(nvt) + ", expected " + hex(f.names_vtable) + ")";
        return false;
    }
    out.inner = mem.ptr(out.names + kNamesInner);
    if (!out.inner) {
        err = "the commentary names object is not bound to the audio yet (names+0x08 is null): the game's audio is not up";
        return false;
    }
    out.audio = mem.ptr(out.inner + kInnerAudio);
    uint64_t avt = 0;
    if (!out.audio || !mem.rd(out.audio, avt) || !is_ptr(avt, 8)) {
        err = "the audio system (inner+0x10) is not readable";
        return false;
    }
    return true;
}

std::vector<uint64_t> pack_name_batch(const std::vector<int64_t>& ids) {
    std::vector<uint64_t> out;
    out.reserve(std::min(ids.size(), kBatchMax) + 1);
    uint32_t row = 0;
    for (int64_t id : ids) {
        if (out.size() >= kBatchMax) break;
        if (id <= 0 || id >= (1 << 20)) {
            ++row;  // keeps the row numbering aligned with `ids` (unpack looks the id up by row)
            continue;
        }
        out.push_back(batch_elem(row, static_cast<uint32_t>(id)));
        ++row;
    }
    out.push_back(batch_elem(static_cast<uint32_t>(ids.size()), static_cast<uint32_t>(kCanaryId)));
    return out;
}

bool unpack_name_batch(const std::vector<uint64_t>& batch, size_t survivors, const std::vector<int64_t>& asked,
                       std::unordered_set<int64_t>& kept, std::string& err) {
    kept.clear();
    if (survivors > batch.size()) {
        err = "the filter left more elements (" + std::to_string(survivors) + ") than the batch had (" + std::to_string(batch.size()) + ")";
        return false;
    }
    int64_t last_row = -1;
    for (size_t i = 0; i < survivors; ++i) {
        const uint32_t row = batch_row(batch[i]);
        const int64_t id = batch_id(batch[i]);
        if (id == kCanaryId || row >= asked.size()) {
            err = "the canary id survived: the game's filter did not run (no commentary bridge in this screen?)";
            return false;
        }
        if (asked[row] != id) {
            err = "element " + std::to_string(i) + " holds id " + std::to_string(id) + " but row " + std::to_string(row) + " asked for " +
                  std::to_string(asked[row]) + " (layout mismatch)";
            return false;
        }
        if (static_cast<int64_t>(row) <= last_row) {
            err = "the filter reordered the batch (row " + std::to_string(row) + " after " + std::to_string(last_row) + ")";
            return false;
        }
        last_row = row;
        kept.insert(id);
    }
    return true;
}

// ---------------------------------------------------------------- the build
Build::Build(BuildRequest req, std::function<double()> clock) : req_(std::move(req)), clock_(std::move(clock)) {
    if (req_.batch_min < 1) req_.batch_min = 1;
    if (req_.batch_max < req_.batch_min) req_.batch_max = req_.batch_min;
    batch_ = std::min(std::max(req_.batch_start, req_.batch_min), req_.batch_max);
    if (batch_ > kBatchMax) batch_ = kBatchMax;
}

BuildResult Build::result() const {
    std::lock_guard<std::mutex> lock(m_);
    return r_;
}

std::string Build::progress() const {
    std::lock_guard<std::mutex> lock(m_);
    char buf[200];
    if (names_pos_ < req_.names.size() || req_.players.empty())
        std::snprintf(buf, sizeof(buf), "names %zu / %zu (%zu with audio), batch %zu", names_pos_, req_.names.size(), r_.names.size(), batch_);
    else
        std::snprintf(buf, sizeof(buf), "names %zu with audio; players %zu / %zu (%zu with own recordings), batch %zu", r_.names.size(),
                      players_pos_, req_.players.size(), r_.players.size(), batch_);
    return buf;
}

void Build::finish(bool ok, const std::string& note) {
    std::lock_guard<std::mutex> lock(m_);
    if (done_.load()) return;
    r_.ok = ok;
    r_.note = note;
    if (clock_ && started_ >= 0.0) r_.elapsed = clock_() - started_;
    done_ = true;
}

bool Build::step(Caller& c) {
    if (done_.load()) return false;
    if (cancel_.load()) {
        {
            std::lock_guard<std::mutex> lock(m_);
            r_.cancelled = true;
        }
        finish(false, "cancelled");
        return false;
    }
    const double t0 = clock_ ? clock_() : 0.0;
    if (started_ < 0.0) started_ = t0;
    if (req_.names.empty() && req_.players.empty()) {
        finish(false, "nothing to check: no commentary ids and no players");
        return false;
    }
    std::string err;
    if (names_pos_ < req_.names.size()) {
        const size_t n = std::min(batch_, req_.names.size() - names_pos_);
        std::vector<int64_t> asked(req_.names.begin() + static_cast<std::ptrdiff_t>(names_pos_),
                                   req_.names.begin() + static_cast<std::ptrdiff_t>(names_pos_ + n));
        std::vector<uint64_t> batch = pack_name_batch(asked);
        size_t survivors = 0;
        if (!c.filter_names(batch, survivors, err)) {
            finish(false, "the game's name filter could not run: " + err);
            return false;
        }
        std::unordered_set<int64_t> kept;
        if (!unpack_name_batch(batch, survivors, asked, kept, err)) {
            finish(false, err);
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(m_);
            for (int64_t id : kept) r_.names.insert(id);
            r_.names_checked += n;
            ++r_.steps;
        }
        names_pos_ += n;
    } else if (players_pos_ < req_.players.size()) {
        const size_t n = std::min(batch_, req_.players.size() - players_pos_);
        std::vector<int64_t> pids(req_.players.begin() + static_cast<std::ptrdiff_t>(players_pos_),
                                  req_.players.begin() + static_cast<std::ptrdiff_t>(players_pos_ + n));
        std::vector<int> flags;
        if (!c.player_audio(pids, flags, err)) {
            // the names stand on their own: note why the players were not checked and stop the player pass
            std::lock_guard<std::mutex> lock(m_);
            r_.players_note = err;
            ++r_.steps;
            players_pos_ = req_.players.size();
        } else {
            std::lock_guard<std::mutex> lock(m_);
            for (size_t i = 0; i < pids.size() && i < flags.size(); ++i)
                if (flags[i] != 0) r_.players[pids[i]] = flags[i];
            r_.players_checked += n;
            ++r_.steps;
            players_pos_ += n;
        }
    }
    // adapt the batch to the time this tick took inside the game
    if (clock_) {
        const double dt = clock_() - t0;
        {
            std::lock_guard<std::mutex> lock(m_);
            r_.seconds += dt;
        }
        if (dt > req_.tick_budget) batch_ = std::max(req_.batch_min, batch_ / 2);
        else if (dt < req_.tick_budget * 0.5) batch_ = std::min(req_.batch_max, batch_ * 2);
    }
    if (names_pos_ < req_.names.size() || players_pos_ < req_.players.size()) return true;
    // done: an empty answer means the bank is not bound in this screen, not a bank without names
    BuildResult snap = result();
    if (snap.names.empty() && snap.names_checked > 0) {
        finish(false, "the game answered 'no audio' for every one of the " + std::to_string(snap.names_checked) +
                          " commentary ids: the loaded bank is not bound in this screen (the Create Player screen or a match binds it)");
        return false;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%zu of %zu commentary ids have audio, %zu of %zu players have their own recordings (%zu steps, %.2f s in the game)",
                  snap.names.size(), snap.names_checked, snap.players.size(), snap.players_checked, snap.steps, snap.seconds);
    std::string note = buf;
    if (!snap.players_note.empty()) note += "; players not checked: " + snap.players_note;
    finish(!snap.names.empty() || !snap.players.empty(), note);
    return false;
}

BankCapture to_capture(const BuildResult& r) {
    BankCapture c;
    c.ok = r.ok;
    c.note = r.note;
    c.source = kAudioSource;
    c.surnames = r.names;
    c.players = r.players;
    c.rows = r.names_checked;
    c.checked_names = r.names_checked;
    c.checked_players = r.players_checked;
    c.steps = r.steps;
    c.seconds = r.elapsed > 0.0 ? r.elapsed : r.seconds;
    c.cancelled = r.cancelled;
    return c;
}

}  // namespace caudio
}  // namespace turbo
