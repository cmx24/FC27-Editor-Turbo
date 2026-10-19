// FC 27 LE Turbo GUI - 2.21 compdata profile guard (see compdata_profile.h). Reads only.
#include "compdata_profile.h"

#include <cctype>
#include <system_error>

#include "nlohmann/json.hpp"

namespace turbo {
namespace compdata {

namespace fs = std::filesystem;
using nlohmann::json;

const char* const kSpartaIdentity = "d91d1b4e90a27eda";

namespace {

std::string trim_lower(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    std::string out = s.substr(b, e - b);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool identity_ok(const std::string& id) {
    if (id.size() != 16) return false;
    for (char c : id)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

}  // namespace

const char* profile_name(Profile p) {
    switch (p) {
        case Profile::Stock: return "stock";
        case Profile::Eerste: return "eerste";
        default: return "unknown";
    }
}

Profile parse_profile_name(const std::string& s) {
    const std::string v = trim_lower(s);
    if (v == "stock") return Profile::Stock;
    if (v == "eerste") return Profile::Eerste;
    return Profile::Unknown;
}

std::string strip_bom(const std::string& text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        return text.substr(3);
    return text;
}

fs::path record_path(const fs::path& le_root) { return le_root / "turbo_output" / "compdata_profile.json"; }

fs::path compdata_dir(const fs::path& le_root) {
    return le_root / "mods" / "legacy" / "dlc" / "dlc_FootballCompEng" / "dlc" / "FootballCompEng" / "data" / "compdata";
}

const std::vector<std::string>& eerste_files() {
    static const std::vector<std::string> k = {
        "activeteams.txt",  "compids.txt",   "compobj.txt",  "initteams.txt", "LIVE_EDITOR_LIMITS.json", "objectives.txt",
        "schedule.txt",     "schedules/C2256_S1_2026.txt",   "settings.txt", "standings.txt",           "tasks.txt",
    };
    return k;
}

Record parse_record(const std::string& text, bool present) {
    Record r;
    r.present = present;
    if (!present) {
        r.why = "no turbo_output\\compdata_profile.json (the launcher's profile step has not run)";
        return r;
    }
    const json j = json::parse(strip_bom(text), nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) {
        r.why = "turbo_output\\compdata_profile.json is not readable JSON";
        return r;
    }
    if (j.contains("set_at") && j["set_at"].is_string()) r.set_at = j["set_at"].get<std::string>();
    if (!j.contains("profile") || !j["profile"].is_string()) {
        r.why = "turbo_output\\compdata_profile.json has no \"profile\" text";
        return r;
    }
    const std::string raw = j["profile"].get<std::string>();
    r.profile = parse_profile_name(raw);
    if (r.profile == Profile::Unknown) r.why = "turbo_output\\compdata_profile.json profile '" + raw + "' is neither stock nor eerste";
    return r;
}

DiskState disk_state_from_count(size_t live, size_t total) {
    DiskState d;
    d.live = live;
    d.total = total;
    const std::string n = std::to_string(live) + "/" + std::to_string(total) + " Eerste compdata files in mods\\legacy";
    if (total > 0 && live == total) {
        d.profile = Profile::Eerste;
        d.why = n;
    } else if (live == 0) {
        d.profile = Profile::Stock;
        d.why = n;
    } else {
        d.why = n + " (a partial set)";
    }
    return d;
}

DiskState read_disk_state(const fs::path& le_root) {
    const fs::path dir = compdata_dir(le_root);
    size_t live = 0;
    for (const auto& rel : eerste_files()) {
        std::error_code ec;
        if (fs::is_regular_file(dir / fs::path(rel), ec)) ++live;
    }
    return disk_state_from_count(live, eerste_files().size());
}

Installed installed_profile(const Record& r, const DiskState& d) {
    Installed i;
    const std::string rec = r.profile == Profile::Unknown ? r.why
                                                          : std::string("launcher record: ") + profile_name(r.profile) +
                                                                (r.set_at.empty() ? std::string() : " set " + r.set_at);
    if (r.profile == Profile::Unknown) {
        i.why = rec + "; disk: " + d.why;
        return i;
    }
    if (d.profile == Profile::Unknown) {
        i.why = rec + "; disk: " + d.why;
        return i;
    }
    if (d.profile != r.profile) {
        i.why = rec + ", but the disk says " + profile_name(d.profile) + " (" + d.why +
                "): the files were moved without the launcher; record and disk disagree";
        return i;
    }
    i.profile = r.profile;
    i.why = rec + "; disk agrees (" + d.why + ")";
    return i;
}

Careers parse_careers(const std::string& turbo_config_text) {
    Careers c;
    auto builtin = [&](const std::string& note) {
        c.map.clear();
        c.map[kSpartaIdentity] = Profile::Eerste;
        c.source = std::string("built-in list (") + kSpartaIdentity + " = eerste)";
        c.note = note;
        return c;
    };
    if (turbo_config_text.empty()) return builtin("");
    const json j = json::parse(strip_bom(turbo_config_text), nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) return builtin("turbo_config.json is not readable JSON");
    if (!j.contains("compdata_profile")) return builtin("");
    const json& b = j["compdata_profile"];
    if (!b.is_object() || !b.contains("careers") || !b["careers"].is_object())
        return builtin("turbo_config.json compdata_profile.careers is not an object");
    std::string bad;
    for (auto it = b["careers"].begin(); it != b["careers"].end(); ++it) {
        if (!it.key().empty() && it.key()[0] == '_') continue;  // "_help" and similar
        const std::string id = trim_lower(it.key());
        const Profile p = it.value().is_string() ? parse_profile_name(it.value().get<std::string>()) : Profile::Unknown;
        if (!identity_ok(id) || p == Profile::Unknown) {
            bad += (bad.empty() ? "" : ", ") + it.key();
            continue;
        }
        c.map[id] = p;
    }
    c.source = "turbo_config.json compdata_profile.careers (" + std::to_string(c.map.size()) + " listed)";
    if (!bad.empty()) c.note = "entries not used (identity is 16 hex digits, profile stock or eerste): " + bad;
    return c;
}

Profile career_need(const Careers& c, const std::string& career_id) {
    const std::string id = trim_lower(career_id);
    if (id.empty()) return Profile::Unknown;
    auto it = c.map.find(id);
    return it == c.map.end() ? Profile::Stock : it->second;
}

Decision decide(const Installed& inst, Profile need, const std::string& career_id, const std::string& career_label,
                const std::string& need_source) {
    Decision d;
    const std::string head = std::string("compdata profile: ") + profile_name(inst.profile) + " installed, ";
    const std::string career = career_id.empty() ? std::string() : "career " + career_id + (career_label.empty() ? "" : " " + career_label);
    if (need == Profile::Unknown || career_id.empty()) {
        d.line = head + "career not identified, UNKNOWN (no banner; installed: " + inst.why + ")";
        return d;
    }
    const std::string tail = career + "; need from " + need_source + "; installed: " + inst.why;
    if (inst.profile == Profile::Unknown) {
        d.line = head + "career needs " + profile_name(need) + ", UNKNOWN (no banner; " + tail + ")";
        return d;
    }
    if (inst.profile == need) {
        d.verdict = Verdict::Ok;
        d.line = head + "career needs " + profile_name(need) + ", OK (" + tail + ")";
        return d;
    }
    d.verdict = Verdict::Mismatch;
    d.line = head + "career needs " + profile_name(need) + ", MISMATCH (" + tail + "): do not save";
    if (inst.profile == Profile::Eerste)
        d.banner = "DO NOT SAVE: the Eerste Divisie (Netherlands) league data is installed, but this career needs the standard data. "
                   "Quit to the main menu without saving, close the game, start LaunchFC27 again and pick a country other than Netherlands.";
    else
        d.banner = "DO NOT SAVE: the standard league data is installed, but this career needs the Eerste Divisie (Netherlands) data. "
                   "Quit to the main menu without saving, close the game, start LaunchFC27 again and pick Netherlands.";
    return d;
}

}  // namespace compdata
}  // namespace turbo
