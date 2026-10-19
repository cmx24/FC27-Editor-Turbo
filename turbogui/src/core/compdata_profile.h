// FC 27 LE Turbo GUI - 2.21 compdata profile guard (read-only: it decides, logs one line and raises one red line; it writes nothing).
//
// The Eerste Divisie compdata override is 11 files in <Live Editor>\mods\legacy\dlc\dlc_FootballCompEng\dlc\FootballCompEng\data\compdata
// (turbo_dev\squads_pack\install_eerste_state.json). The game reads them at every career entry, ~30 s BEFORE Turbo can identify the
// career, and a career made without them loads with an empty schedule while they are in place (H1, 2026-10-19). So the profile is
// chosen before the game starts (the launcher, turbo_dev\tools\compdata_profile.ps1, which records it in
// turbo_output\compdata_profile.json) and Turbo only checks, once the career is identified, that the installed profile is the one the
// career needs. A mismatch = one log line + a red line in the overlay's message area ("do not save"). Never a write.
//
// Installed profile: the launcher's record (compdata_profile.json {"profile": "stock"|"eerste", "set_at": ..., "files": [...]}) AND a
// stat-only look at the 11 files on disk (all 11 present = eerste, none = stock). Both must agree: a missing or unreadable record,
// a partial set on disk or a record the disk contradicts (the older eerste_compdata_aside.ps1 moves the files without writing the
// record) is UNKNOWN: logged, never a banner.
// Career need: the identity (core/reapply.h career_identity_id) looked up in turbo_config.json "compdata_profile": {"careers":
// {"<id>": "eerste"}}; every other identified career needs "stock". Built-in fallback when the block is absent (the live
// turbo_config.json of an older install): d91d1b4e90a27eda (Cassio's SPARTA career, created 10-18 with the override) = eerste.
// Not identified = UNKNOWN.
#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace turbo {
namespace compdata {

enum class Profile { Unknown, Stock, Eerste };
const char* profile_name(Profile p);           // "stock" / "eerste" / "unknown"
Profile parse_profile_name(const std::string& s);  // "stock" / "eerste" (any case, spaces trimmed); anything else Unknown

// A UTF-8 byte-order mark at the start is dropped (Windows PowerShell 5.1 writes one with -Encoding utf8)
std::string strip_bom(const std::string& text);

// <Live Editor>\turbo_output\compdata_profile.json
std::filesystem::path record_path(const std::filesystem::path& le_root);
// <Live Editor>\mods\legacy\dlc\dlc_FootballCompEng\dlc\FootballCompEng\data\compdata
std::filesystem::path compdata_dir(const std::filesystem::path& le_root);
// The 11 files of the Eerste override, relative to compdata_dir (install_eerste_state.json "compdata.files", 2026-10-18)
const std::vector<std::string>& eerste_files();

// The launcher's record
struct Record {
    bool present = false;              // the file exists
    Profile profile = Profile::Unknown;
    std::string set_at;                // as written ("" = not given)
    std::string why;                   // why the profile is Unknown ("" when it is known)
};
// `present` = the file exists; `text` its contents. Missing, not JSON, no "profile" or an unknown value -> Unknown with a reason.
Record parse_record(const std::string& text, bool present);

// The 11 files on disk (stat only)
struct DiskState {
    size_t live = 0, total = 0;
    Profile profile = Profile::Unknown;  // all live = Eerste, none = Stock, else Unknown (partial set)
    std::string why;                     // "11/11 Eerste files in mods\legacy" ...
};
DiskState disk_state_from_count(size_t live, size_t total);
DiskState read_disk_state(const std::filesystem::path& le_root);

// The installed profile: record and disk must agree
struct Installed {
    Profile profile = Profile::Unknown;
    std::string why;
};
Installed installed_profile(const Record& r, const DiskState& d);

// The seeded career list (turbo_config.json "compdata_profile.careers"; built-in fallback when the block is absent)
struct Careers {
    std::map<std::string, Profile> map;  // identity (16 lower-case hex digits) -> the profile it needs
    std::string source;                  // "turbo_config.json compdata_profile.careers" / "built-in list (...)"
    std::string note;                    // what was wrong in the block ("" = nothing)
};
extern const char* const kSpartaIdentity;  // "d91d1b4e90a27eda"
Careers parse_careers(const std::string& turbo_config_text);
// Eerste when listed so, Unknown for an empty id (not identified), else Stock
Profile career_need(const Careers& c, const std::string& career_id);

enum class Verdict { Ok, Mismatch, Unknown };
struct Decision {
    Verdict verdict = Verdict::Unknown;
    std::string line;    // the whole log line, starting "compdata profile: "
    std::string banner;  // the red line (only for Mismatch; "" otherwise)
};
// The log line's head is fixed (the Tester's live check greps it):
//   "compdata profile: <installed> installed, career needs <need>, OK|MISMATCH|UNKNOWN (...)"
//   "compdata profile: <installed> installed, career not identified, UNKNOWN (...)"
Decision decide(const Installed& inst, Profile need, const std::string& career_id, const std::string& career_label, const std::string& need_source);

}  // namespace compdata
}  // namespace turbo
