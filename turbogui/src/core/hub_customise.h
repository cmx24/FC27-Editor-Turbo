// FC 27 LE Turbo GUI - "Reopen club customisation" (Manager Career, Turbo 1.1.1).
//
// The career hub's "Customise club" tile (kits, crest and stadium designer for the Create-a-Club club, the stadium hub
// for every other club) is gated by two bytes of the career's MainHubManager (research/edit_unlock_plan.md section 2,
// FC27.exe 1.0.140.64835, every address [H] in the static image):
//   +0x511 licensedStadium        1 = the club plays in a licensed stadium: the tile is never shown
//   +0x512 customisedThisSeason   1 = the created club used its designer this season: the tile is hidden for it
//   +0x513 set by SEASON_RESET when +0x511 == 0, cleared by DAY_PASSED (0x147DD3369); Turbo never writes it
// The tile 0x27130 is shown when !licensed && !(created club && customised) (0x14846AA93; created club = the hub's
// +0xDB1 = team 115486 / 0x1C31E, 0x1484418E8). The dispatcher 0x14847C26A sends the created club to
// EnterCustomizeKitCrestStadiumHub and every other club to EnterCustomizeStadiumHub. SEASON_RESET (0x147DD374F) clears
// +0x512 and recomputes +0x511, so the button is simply pressed again in a new season (Turbo never re-applies).
// The hub view model copies the flags when it is built: the user leaves the hub and comes back.
//
// MainHubManager: Live Editor manager type 58, allocated as "MainHubManager" (0x8A8 bytes, 0x147F18D66); its
// constructor 0x147DB6EF4 stores the manager table at +0x8 and the vtable 0x14B016730 at +0x0 (signature "mhm_vtable":
// "48 89 51 08 48 8D 05 ?? ?? ?? ?? 48 89 01 83 CE FF 89 71 10 48 8D 05", unique, rip at +4). Turbo writes only after
// the object in slot 58 of the published manager table carries that vtable, points back at the table and holds 0/1 in
// the three flag bytes. +0x511 = 0 for a club with a licensed stadium is an explicit opt-in (it can replace the real
// stadium in the save), offered after a save backup.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "mem.h"

namespace turbo {
namespace mhm {

constexpr int kTypeId = 58;                 // ENUM_FCEGameModesFCECareerModeMainHubManager
constexpr uint64_t kSize = 0x8A8;           // allocation size (0x147F18D66)
constexpr uint64_t kTable = 0x08;           // the career manager table (constructor's second argument)
constexpr uint64_t kLicensed = 0x511, kCustomised = 0x512, kNewSeason = 0x513;
constexpr uint64_t kRvaVtable = 0xB016730;  // 0x14B016730 - image base (fallback when the signature is not resolved)
constexpr int64_t kCreatedClubTeam = 115486;  // 0x1C31E: the Create-a-Club club (0x1484418E8)
constexpr const char* kSignature = "mhm_vtable";

struct State {
    uint64_t obj = 0;
    uint8_t licensed = 0, customised = 0, new_season = 0;
    int64_t team = 0;           // the user's club (bridge_state.json user_team)
    bool created_club = false;  // team == kCreatedClubTeam
    // What the hub shows when it is built again
    bool tile_shown() const { return !licensed && !(created_club && customised); }
};

// Finds the MainHubManager in slot 58 of `managers` and checks it: vtable == `vtable` (0 = unknown: refused), +0x8 ==
// `managers`, the whole object readable, the three flag bytes 0 or 1. "" and `out` filled, or why not.
std::string locate(Memory& mem, uint64_t managers, uint64_t vtable, int64_t user_team, State& out);

// One line for the GUI: what the hub shows now and why
std::string describe(const State& s);

struct Result {
    bool ok = false;
    bool changed = false;  // a byte was written
    State before, after;
    std::string message;
};

// Writes +0x512 = 0 (and +0x511 = 0 when `licensed_too`) after locate(), then reads the bytes back. Refused without
// writing: the checks fail, or the club has a licensed stadium and `licensed_too` is false.
Result reopen(Memory& mem, uint64_t managers, uint64_t vtable, int64_t user_team, bool licensed_too);

// The vtable to check against: the host's resolved signature (set_signature_lookup), else image_base + kRvaVtable
// (the build this was verified on), else 0
void set_signature_lookup(std::function<uint64_t(const char*)> fn);
uint64_t vtable(uint64_t image_base);

// ---- save backup before the licensed-stadium opt-in: copies every CmMgrC* (Manager Career) save of the game's
// settings folder (default %LOCALAPPDATA%\EA SPORTS FC 27\settings) to <out_root>\<stamp>\. Never writes to the
// save folder.
struct SaveBackup {
    bool ok = false;
    int files = 0;
    std::filesystem::path dir;
    std::string message;
};
std::filesystem::path default_save_dir();  // empty when LOCALAPPDATA is not set
SaveBackup backup_saves(const std::filesystem::path& save_dir, const std::filesystem::path& out_root, const std::string& stamp);

}  // namespace mhm
}  // namespace turbo
