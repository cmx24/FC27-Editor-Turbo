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

// X 2.0 review fix (9): a hand-edited profile with a non-string "name" / "sha256" / ... must not throw on every frame of Changes & undo
// (nlohmann's value() throws type_error when the key exists with another type): a wrong type reads as the default.
static std::string str_of(const nlohmann::json& j, const char* key, const std::string& dflt) {
    if (!j.is_object()) return dflt;
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : dflt;
}
// X 2.0 review fix (3): write a file through a temp file and a rename; false when it could not be written whole
static bool write_whole(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        f.flush();
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {  // a C runtime whose rename does not replace an existing file: remove it, then rename (as save_profile does)
        std::error_code ec2;
        fs::remove(path, ec2);
        ec.clear();
        fs::rename(tmp, path, ec);
    }
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}


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
        if (fn == "_pre_apply.turbox.json") continue;  // X 2.0 review fix (11): the Revert snapshot is not a profile (Apply / Delete on it lost Revert)
        std::ifstream f(entry.path(), std::ios::binary);
        if (!f) continue;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object() || !j.contains("payload") || !j["payload"].is_object()) continue;
        ProfileSlotInfo info;
        info.slug = fn.substr(0, fn.size() - 12);
        info.name = str_of(j, "name", info.slug);
        info.career = str_of(j, "career", std::string());
        info.updated_at = str_of(j, "updated_at", std::string());
        info.sha256 = str_of(j, "sha256", std::string());
        const nlohmann::json& p = j["payload"];
        info.valid_checksum = !info.sha256.empty() && sha256_hex(p.dump()) == info.sha256;
        if (p.contains("reapply") && p["reapply"].is_object()) {
            const auto& r = p["reapply"];
            if (r.contains("players") && r["players"].is_array()) info.players = r["players"].size();
            if (r.contains("kits") && r["kits"].is_array()) info.kits = r["kits"].size();
            if (r.contains("playernamemap") && r["playernamemap"].is_array()) info.callnames = r["playernamemap"].size();
            else if (r.contains("callnames") && r["callnames"].is_array()) info.callnames = r["callnames"].size();
        }
        if (p.contains("club_voice") && p["club_voice"].is_object()) {
            const auto& cv = p["club_voice"];
            if (cv.contains("clubs") && cv["clubs"].is_array()) info.club_voices = cv["clubs"].size();
            else if (cv.contains("entries") && cv["entries"].is_array()) info.club_voices = cv["entries"].size();
        }
        if (p.contains("team_names") && p["team_names"].is_object()) {
            const auto& tn = p["team_names"];
            if (tn.contains("teams") && tn["teams"].is_array()) info.team_names = tn["teams"].size();
            else if (tn.contains("entries") && tn["entries"].is_array()) info.team_names = tn["entries"].size();
        }
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const ProfileSlotInfo& a, const ProfileSlotInfo& b) { return a.slug < b.slug; });
    return out;
}

static std::string s_last_applied_name;
static fs::path s_last_le_root;

static fs::path pre_apply_file(const fs::path& le_root) {
    return profiles_dir(le_root) / "_pre_apply.turbox.json";
}

bool save_profile(App& app, const std::string& name, std::string* out_slug, std::string* err) {
    if (name.empty()) {
        if (err) *err = "Profile name cannot be empty";
        return false;
    }
    const std::string slug = sanitize_slug(name);
    if (out_slug) *out_slug = slug;

    const fs::path le_root = app.bridge.root();
    s_last_le_root = le_root;
    const fs::path dir = profiles_dir(le_root);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "Failed to create profiles folder: " + ec.message();
        return false;
    }

    nlohmann::json payload = nlohmann::json::object();
    try {
        payload["reapply"] = nlohmann::json::parse(reapply_json(app.reapply));
    } catch (...) {
        payload["reapply"] = nlohmann::json::object();
    }
    try {
        payload["club_voice"] = nlohmann::json::parse(clubvoice::store_json(app.club_voice));
    } catch (...) {
        payload["club_voice"] = nlohmann::json::object();
    }
    try {
        payload["team_names"] = nlohmann::json::parse(tnames::store_json(app.team_names));
    } catch (...) {
        payload["team_names"] = nlohmann::json::object();
    }

    const std::string payload_dump = payload.dump();
    const std::string sha = sha256_hex(payload_dump);

    std::time_t t = std::time(nullptr);
    char tbuf[64] = {};
    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    nlohmann::json root = nlohmann::json::object();
    root["format"] = "turbo-profile";
    root["version"] = 1;
    root["slug"] = slug;
    root["name"] = name;
    root["career"] = app.reapply.career;
    root["updated_at"] = tbuf;
    root["sha256"] = sha;
    root["payload"] = payload;

    const fs::path target = dir / (slug + ".turbox.json");
    const fs::path tmp = dir / (slug + ".turbox.tmp");

    std::ofstream f(tmp, std::ios::binary);
    if (!f) {
        if (err) *err = "Failed to open temporary file for writing: " + tmp.string();
        return false;
    }
    f << root.dump(2);
    f.close();
    if (!f) {
        fs::remove(tmp, ec);
        if (err) *err = "Failed to flush profile data to disk";
        return false;
    }

    ec.clear();
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        ec.clear();
        fs::rename(tmp, target, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "Failed to rename temporary profile file to " + target.string();
            return false;
        }
    }
    return true;
}

bool delete_profile(const fs::path& le_root, const std::string& slug, std::string* err) {
    const fs::path target = profiles_dir(le_root) / (slug + ".turbox.json");
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        if (err) *err = "Profile not found";
        return false;
    }
    fs::remove(target, ec);
    if (ec) {
        if (err) *err = "Failed to delete profile: " + ec.message();
        return false;
    }
    return true;
}

bool diff_profile(App& app, const std::string& slug, ProfileDiff& out, std::string* err) {
    out = ProfileDiff{};
    out.slug = slug;
    const fs::path le_root = app.bridge.root();
    s_last_le_root = le_root;
    const fs::path target = profiles_dir(le_root) / (slug + ".turbox.json");
    std::ifstream f(target, std::ios::binary);
    if (!f) {
        if (err) *err = "Profile file not found: " + target.string();
        return false;
    }
    nlohmann::json root = nlohmann::json::parse(f, nullptr, false);
    if (!root.is_object() || !root.contains("payload") || !root["payload"].is_object()) {
        if (err) *err = "Profile corrupted: invalid JSON structure";
        return false;
    }
    out.name = str_of(root, "name", slug);
    const std::string expected_sha = str_of(root, "sha256", "");
    const nlohmann::json& payload = root["payload"];
    const std::string actual_sha = sha256_hex(payload.dump());
    if (expected_sha != actual_sha) {
        out.valid_checksum = false;
        if (err) *err = "Profile checksum mismatch: expected " + expected_sha + ", got " + actual_sha;
        return false;
    }
    out.valid_checksum = true;

    if (payload.contains("reapply") && payload["reapply"].is_object()) {
        const auto& r = payload["reapply"];
        if (r.contains("players") && r["players"].is_array()) {
            out.players_changed = r["players"].size();
            if (out.players_changed > 0)
                out.lines.push_back("+ " + std::to_string(out.players_changed) + " player attribute, growth & role edits");
        }
        if (r.contains("kits") && r["kits"].is_array()) {
            out.kits_changed = r["kits"].size();
            if (out.kits_changed > 0)
                out.lines.push_back("+ " + std::to_string(out.kits_changed) + " kit colour and font modifications");
        }
        if (r.contains("playernamemap") && r["playernamemap"].is_array()) {
            out.callnames_changed = r["playernamemap"].size();
        } else if (r.contains("callnames") && r["callnames"].is_array()) {
            out.callnames_changed = r["callnames"].size();
        }
        if (out.callnames_changed > 0)
            out.lines.push_back("+ " + std::to_string(out.callnames_changed) + " player callname assignments");
    }
    if (payload.contains("club_voice") && payload["club_voice"].is_object()) {
        const auto& cv = payload["club_voice"];
        if (cv.contains("clubs") && cv["clubs"].is_array()) {
            out.club_voices_changed = cv["clubs"].size();
        } else if (cv.contains("entries") && cv["entries"].is_array()) {
            out.club_voices_changed = cv["entries"].size();
        }
        if (out.club_voices_changed > 0)
            out.lines.push_back("+ " + std::to_string(out.club_voices_changed) + " club commentary name mappings");
    }
    if (payload.contains("team_names") && payload["team_names"].is_object()) {
        const auto& tn = payload["team_names"];
        if (tn.contains("teams") && tn["teams"].is_array()) {
            out.team_names_changed = tn["teams"].size();
        } else if (tn.contains("entries") && tn["entries"].is_array()) {
            out.team_names_changed = tn["entries"].size();
        }
        if (out.team_names_changed > 0)
            out.lines.push_back("+ " + std::to_string(out.team_names_changed) + " live team name overrides");
    }
    if (out.lines.empty()) {
        out.lines.push_back("Profile is empty or has no changes.");
    }
    return true;
}

bool apply_profile(App& app, const std::string& slug, std::string* summary, std::string* err) {
    const fs::path le_root = app.bridge.root();
    s_last_le_root = le_root;
    const fs::path target = profiles_dir(le_root) / (slug + ".turbox.json");
    std::ifstream f(target, std::ios::binary);
    if (!f) {
        if (err) *err = "Profile file not found: " + target.string();
        return false;
    }
    nlohmann::json root = nlohmann::json::parse(f, nullptr, false);
    if (!root.is_object() || !root.contains("payload") || !root["payload"].is_object()) {
        if (err) *err = "Corrupt profile structure";
        return false;
    }
    const std::string expected_sha = str_of(root, "sha256", "");
    const nlohmann::json& payload = root["payload"];
    if (sha256_hex(payload.dump()) != expected_sha) {
        if (err) *err = "Profile integrity check failed (SHA-256 mismatch)";
        return false;
    }

    // 1. Pre-apply snapshot for 1-click revert
    const fs::path pre_path = pre_apply_file(le_root);
    {
        nlohmann::json snap_root = nlohmann::json::object();
        snap_root["format"] = "turbo-profile";
        snap_root["version"] = 1;
        snap_root["slug"] = "_pre_apply";
        snap_root["name"] = "_pre_apply";
        snap_root["career"] = app.reapply.career;
        nlohmann::json snap_p = nlohmann::json::object();
        try { snap_p["reapply"] = nlohmann::json::parse(reapply_json(app.reapply)); } catch (...) {}
        try { snap_p["club_voice"] = nlohmann::json::parse(clubvoice::store_json(app.club_voice)); } catch (...) {}
        try { snap_p["team_names"] = nlohmann::json::parse(tnames::store_json(app.team_names)); } catch (...) {}
        snap_root["sha256"] = sha256_hex(snap_p.dump());
        snap_root["payload"] = snap_p;

        // X 2.0 review fix (3): no Apply without a Revert snapshot written whole
        if (!write_whole(pre_path, snap_root.dump(2))) {
            if (err) *err = "Could not write the Revert snapshot (" + pre_path.string() + "): nothing was applied";
            return false;
        }
    }

    size_t items_applied = 0;
    // 2. Reapply store merge
    if (payload.contains("reapply") && payload["reapply"].is_object()) {
        ReapplyStore incoming;
        std::string parse_err;
        if (parse_reapply_json(payload["reapply"].dump(), incoming, &parse_err)) {
            for (const auto& kv : incoming.players) {
                app.reapply.players[kv.first] = kv.second;
                ++items_applied;
            }
            for (const auto& kv : incoming.kits) {
                app.reapply.kits[kv.first] = kv.second;
                ++items_applied;
            }
            for (const auto& kv : incoming.callnames) {
                app.reapply.callnames[kv.first] = kv.second;
                ++items_applied;
            }
            // X 2.0 review fix (2): through App::save_reapply (career prune, set-aside of an unreadable file, the error line)
            if (!app.save_kept_edits()) {
                if (err) *err = "Profile merged for this session, but the kept edits were not saved: " + app.reapply_error;
                return false;
            }
        }
    }
    // 3. Club voice merge
    if (payload.contains("club_voice") && payload["club_voice"].is_object()) {
        clubvoice::Store incoming_cv;
        std::string parse_err;
        if (clubvoice::parse_store_json(payload["club_voice"].dump(), incoming_cv, &parse_err)) {
            for (const auto& e : incoming_cv.entries) {
                app.club_voice.upsert(e);
                ++items_applied;
            }
            app.club_voice.save(clubvoice::store_path(le_root), nullptr);
        }
    }
    // 4. Team names merge
    if (payload.contains("team_names") && payload["team_names"].is_object()) {
        tnames::Store incoming_tn;
        std::string parse_err;
        if (tnames::parse_store_json(payload["team_names"].dump(), incoming_tn, &parse_err)) {
            for (const auto& e : incoming_tn.entries) {
                app.team_names.upsert(e);
                ++items_applied;
            }
            app.team_names_publish();
            // X 2.0 review fix (4): the merged names are saved (they were lost at the next start)
            std::string why;
            if (!app.save_team_names(&why)) {
                if (err) *err = "Profile applied, but the team names were not saved: " + why;
                return false;
            }
        }
    }

    s_last_applied_name = str_of(root, "name", slug);
    if (summary) {
        *summary = "Profile '" + s_last_applied_name + "' applied (" + std::to_string(items_applied) + " items merged).";
    }
    return true;
}

bool revert_last_apply(App& app, std::string* summary, std::string* err) {
    const fs::path le_root = app.bridge.root();
    s_last_le_root = le_root;
    const fs::path pre = pre_apply_file(le_root);
    std::error_code ec;
    if (!fs::exists(pre, ec)) {
        if (err) *err = "No pre-apply profile snapshot found to revert";
        return false;
    }
    nlohmann::json root;
    {
        std::ifstream f(pre, std::ios::binary);
        if (!f) {
            if (err) *err = "Could not open pre-apply profile snapshot";
            return false;
        }
        root = nlohmann::json::parse(f, nullptr, false);
    }
    if (!root.is_object() || !root.contains("payload") || !root["payload"].is_object()) {
        if (err) *err = "Corrupt pre-apply profile snapshot";
        return false;
    }
    const nlohmann::json& payload = root["payload"];

    if (payload.contains("reapply") && payload["reapply"].is_object()) {
        // X 2.0 review fix (1, blocker): ReapplyStore::career is run time only (the loaded career, never in the file): the snapshot's store
        // has none, and moving it in left new edits without a career scope (re-applied in every career) and published no team names of
        // this career. The loaded career is kept across the revert.
        const std::string career = app.reapply.career;
        ReapplyStore snap;
        snap.career = career;
        if (parse_reapply_json(payload["reapply"].dump(), snap, nullptr)) {
            app.reapply = std::move(snap);
            app.reapply.career = career;
            if (!app.save_kept_edits()) {
                if (err) *err = "Reverted for this session, but the kept edits were not saved: " + app.reapply_error;
                return false;
            }
        }
    }
    if (payload.contains("club_voice") && payload["club_voice"].is_object()) {
        clubvoice::Store snap_cv;
        if (clubvoice::parse_store_json(payload["club_voice"].dump(), snap_cv, nullptr)) {
            app.club_voice = std::move(snap_cv);
            app.club_voice.save(clubvoice::store_path(le_root), nullptr);
        }
    }
    if (payload.contains("team_names") && payload["team_names"].is_object()) {
        tnames::Store snap_tn;
        if (tnames::parse_store_json(payload["team_names"].dump(), snap_tn, nullptr)) {
            app.team_names = std::move(snap_tn);
            app.team_names_publish();
            std::string why;
            if (!app.save_team_names(&why)) {
                if (err) *err = "Reverted, but the team names were not saved: " + why;
                return false;
            }
        }
    }

    fs::remove(pre, ec);
    const std::string was = s_last_applied_name.empty() ? "last applied profile" : ("'" + s_last_applied_name + "'");
    s_last_applied_name.clear();
    if (summary) {
        *summary = "Reverted edits back to state prior to applying " + was + ".";
    }
    return true;
}

bool can_revert_last_apply() {
    if (!s_last_applied_name.empty()) return true;
    if (!s_last_le_root.empty()) {
        std::error_code ec;
        return fs::exists(pre_apply_file(s_last_le_root), ec);
    }
    return false;
}

std::string last_applied_profile_name() {
    return s_last_applied_name;
}

void reset_session_for_tests() {
    s_last_applied_name.clear();
    if (!s_last_le_root.empty()) {
        std::error_code ec;
        fs::remove(pre_apply_file(s_last_le_root), ec);
    }
}

}  // namespace turbo::profiles
