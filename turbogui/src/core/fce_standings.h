// FC 27 LE Turbo GUI - live league tables of the game's competition engine (FCE).
//
// FC 27's Standings screen does not read leagueteamlinks: it asks the FCE StandingsManager, which re-reads and re-sorts
// the rows of FCE::DataManager's StandingsDataList on every request (docs/re/standings.md, verified on the
// 1.0.140.64835 image). Match results update those rows incrementally; nothing recomputes them from the fixtures.
// So editing a row here changes what the game shows the next time the screen is opened, and a result edit must patch
// the fixture AND both rows (old outcome removed, new one added).
//
// Pointer chain (all offsets verified in code):
//   FCE::FCEInterfaceImpl (= Live Editor's GetPlugin(ENUM_djb2IFCEInterface_CLSS), bridge_state.json "ifce")
//     +0x18 ManagerHub   -> +0x18 DataConnector -> +0x80 DataManager  (DataManager +0x28 points back to the DataConnector)
//   DataManager +0x60 FixtureDataList {i32 count; +0x08 FixtureData* data}      FixtureData  = 0x18 bytes, id == index
//   DataManager +0x88 StandingsDataList {StandingData* begin; StandingData* end}  StandingData = 0x18 bytes, id == index
//
// Everything here is pure: it only uses the Memory interface, so the native tests run it on synthetic memory.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mem.h"

namespace turbo {
namespace fce {

constexpr uint64_t kOffImplHub = 0x18, kOffHubConnector = 0x18, kOffConnectorManager = 0x80, kOffManagerConnector = 0x28;
constexpr uint64_t kOffFixtureList = 0x60, kOffStandingsList = 0x88;
constexpr uint32_t kStandingSize = 0x18, kFixtureSize = 0x18;
constexpr uint32_t kMaxRows = 65535, kMaxFixtures = 20000;
// vtables inside FC27.exe (image-relative; checked only when the image base is known)
constexpr uint64_t kRvaInterfaceVtable = 0xB180DF0, kRvaDataManagerVtable = 0xB180CA8;

// One StandingData row (0x18 bytes). Played games, goal difference and the position are not stored: the UI derives them.
struct StandingRow {
    uint64_t addr = 0;
    uint16_t id = 0;         // +0x00 standing id (== index in the list)
    uint16_t compobj = 0;    // +0x02 competition object (group / stage node), not the league id
    uint32_t teamid = 0;     // +0x04
    uint8_t teamindex = 0;   // +0x08
    uint8_t hw = 0, hd = 0, hl = 0, hgf = 0, hga = 0;  // +0x09 .. +0x0D home wins / draws / losses / goals for / against
    uint8_t aw = 0, ad = 0, al = 0, agf = 0, aga = 0;  // +0x0E .. +0x12 away
    int16_t points = 0;      // +0x14
    uint8_t used = 0;        // +0x16 (1 = valid)
    int played() const { return hw + hd + hl + aw + ad + al; }
    int wins() const { return hw + aw; }
    int draws() const { return hd + ad; }
    int losses() const { return hl + al; }
    int gf() const { return hgf + agf; }
    int ga() const { return hga + aga; }
    int gd() const { return gf() - ga(); }
};

// One FixtureData (0x18 bytes)
struct Fixture {
    uint64_t addr = 0;
    uint32_t date = 0;        // +0x00 YYYYMMDD
    uint16_t time = 0;        // +0x04
    uint16_t id = 0;          // +0x06 (== index)
    uint16_t compobj = 0;     // +0x08
    int16_t home_sid = -1;    // +0x0A standing id of the home team
    int16_t away_sid = -1;    // +0x0C
    uint8_t group = 0;        // +0x0E match group
    int8_t home_score = -1;   // +0x0F (-1 = not played)
    int8_t home_pens = -1;    // +0x10
    int8_t away_score = -1;   // +0x11
    int8_t away_pens = -1;    // +0x12
    uint8_t completion = 0;   // +0x13 0 = not played, 1 = full time, 2 = extra time, 3 = penalties
    uint8_t used = 0;         // +0x14
    bool played() const { return used == 1 && completion != 0 && home_score >= 0 && away_score >= 0; }
};

// Points per outcome (FCE comp settings 0x1F win / 0x20 draw / 0x21 loss; 3 / 1 / 0 for leagues)
struct Points {
    int win = 3, draw = 1, loss = 0;
};

enum class Outcome { HomeWin, AwayWin, Draw };
inline Outcome outcome_of(int home_score, int away_score) {
    if (home_score > away_score) return Outcome::HomeWin;
    if (home_score < away_score) return Outcome::AwayWin;
    return Outcome::Draw;
}

// The located engine objects (addresses in the game process)
struct Located {
    uint64_t impl = 0, hub = 0, connector = 0, manager = 0;
    uint64_t standings_list = 0, rows_begin = 0, rows_end = 0;
    uint64_t fixture_list = 0, fixtures_data = 0;
    uint32_t row_count = 0, fixture_count = 0;
    bool ok() const { return manager != 0; }
};

// Follow the chain from the FCE interface object. image_base = FC27.exe base for the vtable checks, 0 = skip them.
// Returns "" and fills `out` on success, otherwise the reason (out is cleared).
std::string locate(Memory& mem, uint64_t ifce, uint64_t image_base, Located& out);
// Re-check a located set before use (same back-pointers, list sizes unchanged); false = locate again
bool validate(Memory& mem, Located& loc, uint64_t image_base);

bool decode_row(const uint8_t* p, uint64_t addr, StandingRow& out);
void encode_counters(const StandingRow& row, uint8_t* p);  // writes +0x09..+0x15 of a 0x18-byte row image
bool decode_fixture(const uint8_t* p, uint64_t addr, Fixture& out);

// All rows / fixtures (unused entries included; filter on `used`)
bool read_rows(Memory& mem, const Located& loc, std::vector<StandingRow>& out);
bool read_fixtures(Memory& mem, const Located& loc, std::vector<Fixture>& out);

// Limits of a row: every counter 0..255, points -32768..32767. "" = fine
std::string check_row(const StandingRow& row);
// Write the counters and points of `row` (identity bytes id/compobj/team must still match in memory). "" = done
std::string write_row(Memory& mem, const Located& loc, const StandingRow& row);

// Add (sign +1) or remove (sign -1) one result from the two rows. Returns "" or the limit that would be broken
// (nothing is changed then).
std::string apply_result(StandingRow& home, StandingRow& away, int home_score, int away_score, const Points& pts, int sign);
// Change the score of a played fixture: the fixture bytes and both rows are patched consistently. "" = done
std::string edit_result(Memory& mem, const Located& loc, uint16_t fixture_id, int new_home, int new_away, const Points& pts);

}  // namespace fce
}  // namespace turbo
