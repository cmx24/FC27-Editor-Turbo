// FC 27 LE Turbo GUI - the spoken set asked from the game's audio service (see commentary_audio.h)
#include "commentary_audio.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "callnames.h"
#include "nlohmann/json.hpp"

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
    r_.probe = req_.probe;
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
            if (err.find("canary") != std::string::npos) {
                std::lock_guard<std::mutex> lock(m_);
                r_.no_filter = true;
            }
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
        {
            std::lock_guard<std::mutex> lock(m_);
            r_.unbound = true;
        }
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

// ---------------------------------------------------------------- the probe sample
std::vector<int64_t> probe_sample(const std::vector<int64_t>& ids, const std::vector<int64_t>& preferred, size_t spread, size_t preferred_max) {
    std::vector<int64_t> out;
    std::set<int64_t> seen;
    auto add = [&](int64_t id) {
        if (id <= 0 || id >= (1 << 20) || !seen.insert(id).second) return;
        out.push_back(id);
    };
    size_t taken = 0;
    for (int64_t id : preferred) {
        if (taken >= preferred_max) break;
        const size_t before = out.size();
        add(id);
        if (out.size() > before) ++taken;
    }
    if (!ids.empty() && spread > 0) {
        const size_t n = std::min(spread, ids.size());
        for (size_t i = 0; i < n; ++i) add(ids[(i * ids.size()) / n]);
    }
    return out;
}

// ---------------------------------------------------------------- the id list cache
using json = nlohmann::json;

std::filesystem::path id_cache_path(const std::filesystem::path& le_root) { return le_root / "turbo_output" / "callnames" / "ids.json"; }

std::string id_cache_json(const IdCache& c) {
    json j;
    j["turbo_ids"] = 1;
    j["when"] = c.when;
    j["session"] = c.session;
    j["source"] = c.source;
    j["names"] = c.names;
    j["preview"] = c.preview;
    j["players"] = c.players;
    return j.dump();
}

static bool read_id_array(const json& j, const char* key, std::vector<int64_t>& out, int64_t lo, int64_t hi) {
    out.clear();
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) return false;
    for (const auto& v : *it) {
        if (!v.is_number_integer()) continue;
        const int64_t id = v.get<int64_t>();
        if (id >= lo && id <= hi) out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

bool parse_id_cache_json(const std::string& text, IdCache& out, std::string* err) {
    out = IdCache{};
    json j = json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("turbo_ids", 0) != 1) {
        if (err) *err = "not an id cache written by Turbo";
        return false;
    }
    out.when = j.value("when", std::string());
    out.session = j.value("session", std::string());
    out.source = j.value("source", std::string());
    read_id_array(j, "names", out.names, 1, (1 << 20) - 1);
    read_id_array(j, "preview", out.preview, 1, (1 << 20) - 1);
    read_id_array(j, "players", out.players, 1, 0x7FFFFFFF);
    if (out.names.empty()) {
        if (err) *err = "the id cache holds no commentary ids";
        return false;
    }
    return true;
}

bool parse_commentary_list(const std::string& text, std::vector<int64_t>& ids, std::string* session, std::string* err) {
    ids.clear();
    size_t pos = text.find('\n');
    std::string header = text.substr(0, pos == std::string::npos ? text.size() : pos);
    if (!header.empty() && header.back() == '\r') header.pop_back();
    const std::string tag = "#turbo-commentary ";
    if (header.rfind(tag, 0) != 0) {
        if (err) *err = "not a commentary list written by Turbo's Lua side (no #turbo-commentary header)";
        return false;
    }
    size_t a = tag.size(), b = header.find(' ', a);
    if (session) *session = header.substr(a, b == std::string::npos ? std::string::npos : b - a);
    long long expected = b == std::string::npos ? -1 : std::strtoll(header.c_str() + b + 1, nullptr, 10);
    long long lines = 0;
    while (pos != std::string::npos && pos + 1 < text.size()) {
        size_t start = pos + 1;
        pos = text.find('\n', start);
        size_t end = pos == std::string::npos ? text.size() : pos;
        if (end > start && text[end - 1] == '\r') --end;
        size_t tab = text.find('\t', start);
        if (tab == std::string::npos || tab >= end || tab == start) continue;
        char* stop = nullptr;
        std::string id_text = text.substr(start, tab - start);
        long long id = std::strtoll(id_text.c_str(), &stop, 10);
        if (!stop || *stop != '\0') continue;
        ++lines;
        if (id >= kCallnameMin && id <= kCallnameMax) ids.push_back(static_cast<int64_t>(id));
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (expected >= 0 && lines != expected) {
        if (err) *err = "the list announces " + std::to_string(expected) + " rows but holds " + std::to_string(lines) + " (being rewritten?)";
        ids.clear();
        return false;
    }
    if (ids.empty()) {
        if (err) *err = "the list holds no commentary id in the name range";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the watcher
SpokenWatch::Action SpokenWatch::tick(double now, const Inputs& in) {
    if (in.verified) {
        if (state_ != State::Idle) {
            state_ = State::Idle;
            pending_ = false;
            current_interval_ = 0.0;
        }
        line_.clear();
        return Action::None;
    }
    if (!in.service || !in.have_ids) {
        state_ = State::Idle;
        pending_ = false;
        line_ = "spoken set not built yet: " + (in.why_not.empty() ? std::string(in.service ? "no commentary id list at hand" : "no audio-service call") : in.why_not);
        return Action::None;
    }
    if (state_ == State::Idle) {
        state_ = State::Waiting;
        pending_ = false;
        current_interval_ = interval;
        next_probe_ = now;  // the first check runs at once
    }
    std::string tail;
    if (state_ == State::Building)
        tail = "building the set now";
    else if (!last_probe.empty())
        tail = "last check" + (last_probe_clock.empty() ? std::string() : " " + last_probe_clock) + ": " + last_probe + "; checking again every " +
               std::to_string(static_cast<int>(current_interval_ + 0.5)) + " s";
    else
        tail = "checking whether the bank is bound in this screen";
    line_ = "spoken set not built yet: open the game's Create Player screen (Customise > Create Player > Commentary name) or start a match, "
            "Turbo builds it there (" + tail + ")";
    if (pending_) return Action::None;  // a probe or a build (ours or the user's) is running: wait for its result
    if (!in.available) return Action::None;
    if (state_ == State::Building) return Action::Build;  // a probe answered "bound": the full build starts now
    if (now < next_probe_) return Action::None;
    return Action::Probe;
}

void SpokenWatch::refused(double now, const std::string& why) {
    pending_ = false;
    if (current_interval_ <= 0.0) current_interval_ = interval;
    if (state_ == State::Building) last_build = "not started: " + why;
    else last_probe = "not started: " + why;
    state_ = State::Waiting;
    current_interval_ = std::min(max_interval, current_interval_ * 2.0);
    next_probe_ = now + current_interval_;
}

void SpokenWatch::started(double now, bool probe) {
    pending_ = true;
    pending_probe_ = probe;
    if (probe) {
        ++probes_;
        state_ = State::Waiting;
    } else {
        state_ = State::Building;
    }
    next_probe_ = now + (current_interval_ > 0.0 ? current_interval_ : interval);
}

void SpokenWatch::on_result(double now, const BuildResult& r) {
    pending_ = false;
    if (current_interval_ <= 0.0) current_interval_ = interval;
    if (r.probe) {
        if (r.ok) {
            last_probe = "bound: " + std::to_string(r.names.size()) + " of " + std::to_string(r.names_checked) + " sample ids have audio";
            ++bound_seen_;
            current_interval_ = interval;
            state_ = State::Building;  // the App starts the full build at once (Action::Build on the next tick)
            next_probe_ = now;
        } else if (r.cancelled) {
            last_probe = "cancelled";
            state_ = State::Waiting;
            next_probe_ = now + current_interval_;
        } else if (r.unbound) {
            last_probe = "the bank is not bound in this screen";
            current_interval_ = interval;
            state_ = State::Waiting;
            next_probe_ = now + current_interval_;
        } else if (r.no_filter) {
            last_probe = "the game's name filter did not run here (no commentary bridge in this screen)";
            current_interval_ = interval;
            state_ = State::Waiting;
            next_probe_ = now + current_interval_;
        } else {
            last_probe = "error: " + r.note;
            current_interval_ = std::min(max_interval, current_interval_ * 2.0);
            state_ = State::Waiting;
            next_probe_ = now + current_interval_;
        }
        return;
    }
    last_build = (r.ok ? "ok: " : "failed: ") + r.note;
    if (r.ok) {
        state_ = State::Idle;  // the App applied the cache: the next tick sees `verified`
        current_interval_ = interval;
    } else if (r.unbound || r.no_filter || r.cancelled) {
        state_ = State::Waiting;
        current_interval_ = interval;
        next_probe_ = now + current_interval_;
    } else {
        state_ = State::Waiting;
        current_interval_ = std::min(max_interval, current_interval_ * 2.0);
        next_probe_ = now + current_interval_;
    }
}

}  // namespace caudio
}  // namespace turbo
