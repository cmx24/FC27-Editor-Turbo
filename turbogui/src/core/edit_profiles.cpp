// FC 27 LE Turbo GUI - Named transactional multi-save edit profiles (core/edit_profiles.h).
// Initial W-A baseline implementation; W-E expands full diff/apply/revert & native tests.
#include "core/edit_profiles.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "ui/app.h"

namespace fs = std::filesystem;

namespace turbo::profiles {

namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

}  // namespace

std::string sha256_hex(const std::string& data) {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::vector<uint8_t> msg(data.begin(), data.end());
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8ull;
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) msg.push_back(0x00u);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));

    for (size_t off = 0; off < msg.size(); off += 64u) {
        uint32_t w[64] = {};
        for (int i = 0; i < 16; ++i) {
            const size_t j = off + static_cast<size_t>(i) * 4u;
            w[i] = (uint32_t(msg[j]) << 24) | (uint32_t(msg[j + 1]) << 16) | (uint32_t(msg[j + 2]) << 8) | uint32_t(msg[j + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = hh + S1 + ch + kK[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    out[64] = '\0';
    return out;
}

fs::path profiles_dir(const fs::path& le_root) { return le_root / "turbo_output" / "profiles"; }

std::string sanitize_slug(const std::string& name) {
    std::string s;
    for (unsigned char c : name) {
        if (std::isalnum(c)) s.push_back(static_cast<char>(std::tolower(c)));
        else if (!s.empty() && s.back() != '_') s.push_back('_');
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    if (s.size() > 48) s.resize(48);
    return s.empty() ? "profile" : s;
}

std::vector<ProfileSlotInfo> list_profiles(const fs::path& le_root) {
    std::vector<ProfileSlotInfo> out;
    std::error_code ec;
    const fs::path dir = profiles_dir(le_root);
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fn = entry.path().filename().u8string();
        if (fn.size() <= 12 || fn.compare(fn.size() - 12, 12, ".turbox.json") != 0) continue;
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object() || !j.contains("payload") || !j["payload"].is_object()) continue;
        ProfileSlotInfo info;
        info.slug = fn.substr(0, fn.size() - 12);
        info.name = j.value("name", info.slug);
        info.career = j.value("career", std::string());
        info.updated_at = j.value("updated_at", std::string());
        info.sha256 = j.value("sha256", std::string());
        const nlohmann::json& p = j["payload"];
        info.valid_checksum = !info.sha256.empty() && sha256_hex(p.dump()) == info.sha256;
        if (p.contains("reapply") && p["reapply"].is_object()) {
            const auto& r = p["reapply"];
            if (r.contains("players") && r["players"].is_array()) info.players = r["players"].size();
            if (r.contains("kits") && r["kits"].is_array()) info.kits = r["kits"].size();
            if (r.contains("callnames") && r["callnames"].is_array()) info.callnames = r["callnames"].size();
        }
        if (p.contains("club_voice") && p["club_voice"].is_object() && p["club_voice"].contains("entries") && p["club_voice"]["entries"].is_array())
            info.club_voices = p["club_voice"]["entries"].size();
        if (p.contains("team_names") && p["team_names"].is_object() && p["team_names"].contains("entries") && p["team_names"]["entries"].is_array())
            info.team_names = p["team_names"]["entries"].size();
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const ProfileSlotInfo& a, const ProfileSlotInfo& b) { return a.slug < b.slug; });
    return out;
}

bool save_profile(App&, const std::string&, std::string*, std::string* err) {
    if (err) *err = "save_profile stub";
    return false;
}
bool diff_profile(App&, const std::string&, ProfileDiff&, std::string* err) {
    if (err) *err = "diff_profile stub";
    return false;
}
bool apply_profile(App&, const std::string&, std::string*, std::string* err) {
    if (err) *err = "apply_profile stub";
    return false;
}
bool delete_profile(const fs::path&, const std::string&, std::string* err) {
    if (err) *err = "delete_profile stub";
    return false;
}
bool can_revert_last_apply() { return false; }
std::string last_applied_profile_name() { return {}; }
bool revert_last_apply(App&, std::string*, std::string* err) {
    if (err) *err = "revert_last_apply stub";
    return false;
}
void reset_session_for_tests() {}

}  // namespace turbo::profiles
