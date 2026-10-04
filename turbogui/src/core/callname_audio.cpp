// FC 27 LE Turbo GUI - the play buttons of Players > Callname (see callname_audio.h)
#include "callname_audio.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

namespace turbo {

namespace fs = std::filesystem;

namespace {

// a segment number: a non-negative integer (a whole-number float is accepted)
bool segment_of(const nlohmann::json& v, int64_t& out) {
    if (v.is_number_integer()) {
        out = v.get<int64_t>();
    } else if (v.is_number_float()) {
        const double d = v.get<double>();
        if (!(d >= 0.0 && d < 9.0e15) || d != static_cast<double>(static_cast<int64_t>(d))) return false;
        out = static_cast<int64_t>(d);
    } else {
        return false;
    }
    return out >= 0;
}

void read_bank(const nlohmann::json& seg, const char* name, std::unordered_map<int64_t, std::vector<int64_t>>& out) {
    auto it = seg.find(name);
    if (it == seg.end() || !it->is_object()) return;
    for (auto e = it->begin(); e != it->end(); ++e) {
        if (!e.value().is_array()) continue;
        char* stop = nullptr;
        const long long id = std::strtoll(e.key().c_str(), &stop, 10);
        if (!stop || *stop != '\0' || id <= 0) continue;
        std::vector<int64_t> v;
        int64_t s = 0;
        for (const auto& x : e.value())
            if (segment_of(x, s) && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
        if (!v.empty()) out[static_cast<int64_t>(id)] = std::move(v);
    }
}

uint32_t le32(const std::string& b, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(b[at])) | static_cast<uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8 |
           static_cast<uint32_t>(static_cast<unsigned char>(b[at + 2])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(b[at + 3])) << 24;
}

bool disk_exists(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

double disk_seconds(const fs::path& p) {
    std::error_code ec;
    const uint64_t size = fs::file_size(p, ec);
    if (ec) return 0.0;
    std::ifstream f(p, std::ios::binary);
    std::string head(512, '\0');
    f.read(&head[0], static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(std::max<std::streamsize>(f.gcount(), 0)));
    return wav_seconds(head, size);
}

}  // namespace

void parse_master_audio(const nlohmann::json& j, MasterAudio& out) {
    static std::atomic<uint64_t> parses{0};
    out = MasterAudio{};
    out.gen = ++parses;
    if (!j.is_object()) return;
    try {
        if (auto it = j.find("wav_dir"); it != j.end() && it->is_string()) out.wav_dir = it->get<std::string>();
        if (auto it = j.find("segments"); it != j.end() && it->is_object()) {
            read_bank(*it, "generic", out.generic);
            read_bank(*it, "real", out.real);
            read_bank(*it, "real_link", out.real_link);
        }
    } catch (const std::exception&) {
        const uint64_t gen = out.gen;
        out = MasterAudio{};
        out.gen = gen;
    }
}

fs::path callname_wav_path(const std::string& wav_dir, CallnameAudioKind kind, int64_t segment, bool link) {
    const std::string seg = std::to_string(segment);
    const char* prefix = kind == CallnameAudioKind::Generic ? "pSIMPLE_SURNAME_" : link ? "pPLAYER_NAMES_LINK_" : "pPLAYER_NAMES_SIMPLE_";
    return fs::u8path(wav_dir) / (kind == CallnameAudioKind::Generic ? "generic" : "real") / (prefix + seg + "_" + seg + ".wav");
}

std::vector<fs::path> callname_wavs(const MasterAudio& a, CallnameAudioKind kind, int64_t id) {
    std::vector<fs::path> out;
    if (a.wav_dir.empty()) return out;
    auto add = [&](const std::unordered_map<int64_t, std::vector<int64_t>>& bank, bool link) {
        auto it = bank.find(id);
        if (it == bank.end()) return;
        for (int64_t s : it->second) out.push_back(callname_wav_path(a.wav_dir, kind, s, link));
    };
    if (kind == CallnameAudioKind::Generic) {
        add(a.generic, false);
    } else {
        add(a.real, false);
        add(a.real_link, true);
    }
    return out;
}

double wav_seconds(const std::string& head, uint64_t file_size) {
    if (head.size() < 12 || head.compare(0, 4, "RIFF") != 0 || head.compare(8, 4, "WAVE") != 0) return 0.0;
    uint32_t byte_rate = 0;
    size_t at = 12;
    while (at + 8 <= head.size()) {
        const std::string id = head.substr(at, 4);
        const uint32_t size = le32(head, at + 4);
        if (id == "fmt " && at + 8 + 12 <= head.size()) byte_rate = le32(head, at + 8 + 8);
        if (id == "data") {
            if (!byte_rate) return 0.0;
            const uint64_t start = at + 8;
            uint64_t n = size;
            // a streamed wav has 0 / 0xFFFFFFFF here; a cut file is shorter than its header says
            if (n == 0 || n == 0xFFFFFFFFu || start + n > file_size) n = file_size > start ? file_size - start : 0;
            return static_cast<double>(n) / byte_rate;
        }
        at += 8 + static_cast<size_t>(size) + (size & 1u);
    }
    return 0.0;
}

CallnamePlayer::CallnamePlayer() : exists_(disk_exists), seconds_(disk_seconds) {}

bool CallnamePlayer::file_ok(const fs::path& p) {
    const std::string k = p.u8string();
    auto it = files_.find(k);
    if (it != files_.end()) return it->second;
    const bool ok = exists_ && exists_(p);
    files_[k] = ok;
    return ok;
}

std::vector<fs::path> CallnamePlayer::playable(const MasterAudio& a, CallnameAudioKind kind, int64_t id) {
    // another wav folder, or the master read again (Refresh, another language): check the files again, the missing
    // ones too (a wav copied into the same folder since is found)
    if (a.wav_dir != files_dir_ || a.gen != files_gen_) {
        files_.clear();
        files_dir_ = a.wav_dir;
        files_gen_ = a.gen;
    }
    std::vector<fs::path> out;
    for (auto& p : callname_wavs(a, kind, id))
        if (file_ok(p)) out.push_back(std::move(p));
    return out;
}

size_t CallnamePlayer::next_index(CallnameAudioKind kind, int64_t id) const {
    auto it = next_.find(key(kind, id));
    return it == next_.end() ? 0 : it->second;
}

CallnamePlayer::Button CallnamePlayer::button(const MasterAudio& a, CallnameAudioKind kind, int64_t id, double now) {
    Button b;
    if (!player_) {
        b.tip = "Sound playback is not available";
        return b;
    }
    if (a.wav_dir.empty()) {
        b.tip = "The master list has no wav folder (build it with --wav-dir)";
        return b;
    }
    const auto all = callname_wavs(a, kind, id);
    if (all.empty()) {
        b.tip = kind == CallnameAudioKind::Generic ? "No recording of this callname in the master list"
                                                   : "No own recording of this player in the master list";
        return b;
    }
    const auto ok = playable(a, kind, id);
    if (ok.empty()) {
        b.tip = "Wav not found: " + all.front().u8string();
        return b;
    }
    b.enabled = true;
    b.playing = playing(kind, id, now);
    if (b.playing) {
        b.tip = "Stop";
    } else if (ok.size() == 1) {
        b.tip = "Play";
    } else {
        b.tip = "Play (" + std::to_string(next_index(kind, id) % ok.size() + 1) + " of " + std::to_string(ok.size()) + ")";
    }
    return b;
}

std::string CallnamePlayer::click(const MasterAudio& a, CallnameAudioKind kind, int64_t id, double now) {
    if (!player_) return "";
    if (playing(kind, id, now)) {
        stop();
        return "";
    }
    const auto ok = playable(a, kind, id);
    if (ok.empty()) return "";
    const int64_t k = key(kind, id);
    const size_t i = next_[k] % ok.size();
    if (!player_->play(ok[i])) return "";
    double secs = seconds_ ? seconds_(ok[i]) : 0.0;
    if (!(secs > 0.0)) secs = 2.0;  // length unknown: a click within two seconds still stops it
    key_ = k;
    until_ = now + secs;
    next_[k] = i + 1;
    return ok[i].u8string();
}

void CallnamePlayer::stop() {
    if (player_) player_->stop();
    key_ = 0;
    until_ = 0.0;
}

}  // namespace turbo
