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
#include "sigscan.h"

namespace turbo {
namespace fce {

constexpr uint64_t kOffImplHub = 0x18, kOffHubConnector = 0x18, kOffConnectorManager = 0x80, kOffManagerConnector = 0x28;
constexpr uint64_t kOffFixtureList = 0x60, kOffStandingsList = 0x88;
constexpr uint32_t kStandingSize = 0x18, kFixtureSize = 0x18;
constexpr uint32_t kMaxRows = 65535, kMaxFixtures = 20000;
// DataManager +0x50 CompObjectDataList {i32 capacity; i32 count; +0x08 CompObjData* data}: the competition tree as a flat
// list, id == index (live 04-10-2026: capacity 2500, 1926 entries). CompObjData = 0x30 bytes: +0x00 u16 id, +0x02 u16 id
// again, +0x04 u16 parent id (0xFFFF for the root), +0x06 u8 type (0 root "FIFA", 2 nation, 3 competition, 4 stage,
// 5 group), +0x07 char[7] short name ("C31", "S1", "G1"), +0x0E char[33] description ("TrophyName_Abbr15_31",
// "FCE_League_Stage", "FCE_Setup_Stage"), +0x2F u8 used. A standing row's compObjId (+0x02) is a group node (type 5);
// the group's competition is reached through the parents (group -> stage -> competition "C<leagueid>" -> nation).
constexpr uint64_t kOffCompObjList = 0x50;
constexpr uint32_t kCompObjSize = 0x30, kMaxCompObjs = 65535;
constexpr uint32_t kCompObjShortLen = 7, kCompObjDescLen = 33;
constexpr uint8_t kCompTypeRoot = 0, kCompTypeNation = 2, kCompTypeCompetition = 3, kCompTypeStage = 4, kCompTypeGroup = 5;
// vtables of build kRvaBuild (6AB9813C-211EF000, core/sigscan.h; docs/re/C3-live-standings.md), image-relative. The
// October 2026 title update (6AC07E31-2145C000) moved them: comparing every build against image base + these refused the
// real objects there ("the FCE interface object has another vtable (game update?)" in Competitions and Match setup).
// They are now only the documented set of their own build (Anchors); any other build identifies the objects through
// its signatures.
constexpr uint64_t kRvaInterfaceVtable = 0xB180DF0, kRvaDataManagerVtable = 0xB180CA8;
// FCEInterfaceImpl vtable slot 4 = Post(request) (0x148A35D3C on kRvaBuild, the fce_iface_post signature; the
// StandingsViewManager's request path goes through it, core/standings_refresh.h)
constexpr uint64_t kIfaceSlotPost = 0x20;

// What identifies the engine objects on the running game build. The Windows host fills it once its signature scan is
// done (win/standings_refresh_win.cpp fce_anchors); tests fill it by hand or use unchecked().
//   * FCEInterfaceImpl: its vtable lies in FC27.exe and holds Post (signature "fce_iface_post") in slot 4; on kRvaBuild
//     it must also be the documented vtable. Neither known = refused.
//   * DataManager: its vtable lies in FC27.exe and is the one the signature "fce_datamanager_vtable" resolved (else the
//     documented one on kRvaBuild). Neither known = accepted on the chain alone for READING: it is reached through the
//     game's own pointers from an identified interface and must point back to its DataConnector (+0x28). Such a set is
//     never written (write_refusal, Located::read_only: 2.2 review).
struct Anchors {
    bool check = true;              // false = no identity checks at all (synthetic memory in tests)
    std::string pending;            // not "" = the host has no anchors yet (signature scan still running): why; every locate is refused
    std::string build;              // build key of the running game ("" = unknown), for the messages
    uint64_t image_base = 0, image_size = 0;  // FC27.exe: the vtables must lie inside (size 0 = the range is not checked)
    uint64_t iface_post = 0;        // FCEInterfaceImpl::Post (signature "fce_iface_post"), 0 = not resolved
    uint64_t iface_vtable = 0;      // FCEInterfaceImpl vtable: the documented one on kRvaBuild, else 0
    uint64_t manager_vtable = 0;    // DataManager vtable: signature "fce_datamanager_vtable", else the documented one on kRvaBuild
    bool manager_by_signature = false;  // manager_vtable came from the signature
    static Anchors unchecked();
};
// The anchors of the running build from what its signature scan resolved (0 = not found); kRvaBuild adds its documented
// vtables. image_size 0 = the range is not checked.
Anchors anchors_for(const std::string& build, uint64_t image_base, uint64_t image_size, uint64_t sig_iface_post, uint64_t sig_manager_vtable);
// One line for the log: what each object is checked against
std::string describe_anchors(const Anchors& a);
// The identity checks alone ("" = the object is the FCE interface / a DataManager; `how` says by what)
std::string check_interface(Memory& mem, uint64_t ifce, const Anchors& a, std::string* how = nullptr);
std::string check_manager(Memory& mem, uint64_t manager, const Anchors& a, std::string* how = nullptr);
// Whether a DataManager identified by `a` may be written: "" = yes (its vtable is the signature's or the documented one, or
// the anchors are unchecked), else why the views are read-only. 2.2 review: a DataManager accepted on its back-pointer alone
// (fce_datamanager_vtable not found on the build) is never written. Many FCE managers keep their DataConnector at +0x28,
// so another manager reached through a changed +0x80 slot could pass the chain checks; on 6AB9813C a write always needed
// the exact DataManager vtable. locate / validate put this into Located::read_only.
std::string write_refusal(const Anchors& a);

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

// One CompObjectDataList entry (0x30 bytes): a node of the competition tree
struct CompObj {
    uint16_t id = 0;        // +0x00 (== index)
    uint16_t parent = 0;    // +0x04 (0xFFFF = none)
    uint8_t type = 0;       // +0x06 (kCompType*)
    uint8_t used = 0;       // +0x2F
    std::string short_name; // +0x07 "C31" (competition: 'C' + the league / cup id of the database), "S1", "G1"
    std::string desc;       // +0x0E "TrophyName_Abbr15_31", "FCE_League_Stage", "FCE_Setup_Stage", ...
    // "C31" -> 31; -1 when the short name is not C<number>
    int comp_number() const;
};

// Where a standings group sits in the competition tree (describe_group)
struct GroupInfo {
    uint16_t group = 0, stage = 0, comp = 0, nation = 0;  // node ids (0 = not found above the group)
    std::string stage_desc;   // "FCE_League_Stage", "FCE_Setup_Stage", "FCE_Group_Stage", ...
    std::string comp_short;   // "C31"
    std::string comp_desc;    // "TrophyName_Abbr15_31"
    std::string nation_short; // "ITAL"
    int comp_number = -1;     // 31 (the leagues / cups id of the database), -1 when unknown
    // A league table: the stage the competition's table lives in. Setup stages (the pools a cup draws from, which hold the
    // same clubs as a league) and knockout groups are not tables the game shows as standings.
    bool league_stage() const { return stage_desc == "FCE_League_Stage"; }
    bool setup_stage() const { return stage_desc.rfind("FCE_Setup_Stage", 0) == 0; }
};

// The located engine objects (addresses in the game process)
struct Located {
    uint64_t impl = 0, hub = 0, connector = 0, manager = 0;
    uint64_t standings_list = 0, rows_begin = 0, rows_end = 0;
    uint64_t fixture_list = 0, fixtures_data = 0;
    uint64_t compobj_list = 0, compobjs_data = 0;  // 0 when the list is absent (older layout): the tree is optional
    uint32_t row_count = 0, fixture_count = 0, compobj_count = 0;
    std::string identity;  // how the two objects were identified (for the log), e.g. "interface: Post in vtable slot 4; ..."
    // "" = the rows and fixtures may be written; else why the views show them read-only (write_refusal: the DataManager
    // was identified by its back-pointer only). Every write function refuses with this text.
    std::string read_only;
    bool ok() const { return manager != 0; }
};

// Follow the chain from the FCE interface object, identifying the interface and the DataManager by `anchors`.
// Returns "" and fills `out` on success, otherwise the reason (out is cleared).
std::string locate(Memory& mem, uint64_t ifce, const Anchors& anchors, Located& out);
// Re-check a located set before use (identities, same back-pointers, list sizes unchanged); false = locate again
bool validate(Memory& mem, Located& loc, const Anchors& anchors);

bool decode_row(const uint8_t* p, uint64_t addr, StandingRow& out);
void encode_counters(const StandingRow& row, uint8_t* p);  // writes +0x09..+0x15 of a 0x18-byte row image
bool decode_fixture(const uint8_t* p, uint64_t addr, Fixture& out);

// All rows / fixtures (unused entries included; filter on `used`)
bool read_rows(Memory& mem, const Located& loc, std::vector<StandingRow>& out);
bool read_fixtures(Memory& mem, const Located& loc, std::vector<Fixture>& out);
// The competition tree (CompObjectDataList; index == id; empty when the list is absent). false = not readable
bool read_compobjs(Memory& mem, const Located& loc, std::vector<CompObj>& out);
bool decode_compobj(const uint8_t* p, CompObj& out);
// Where group `group` sits: its stage, competition and nation (parents walked, at most 8 levels). false when the group
// is not a used node of the list; the fields found so far are still filled.
bool describe_group(const std::vector<CompObj>& objs, uint16_t group, GroupInfo& out);

// ---- the gate in front of every write (2.2 review). These writes come from the GUI's render thread, not the game's: the
// FCE thread does its own read-modify-write of the same rows on a match day, so nothing is written while one is processed.
// Kill switch of every row / fixture write of Live standings and Match setup and of their Undo: this file in turbo_output
constexpr const char* kWriteOffFile = "live_standings_write_off.txt";
// What the views and the gate say while that file exists
std::string write_off_reason();
// Looked at right before each write: "" = Turbo may write now, else why not: `switched_off` (kWriteOffFile exists), a
// read-only located set (Located::read_only), or the game processing a match day (the SimDayManager's state, type 103,
// in the career's manager table: `managers` (bridge_state.json "managers"), else [[comm+0x20]+0x10] of `comm`
// (bridge_state.json "comm_service"), as the standings refresh finds it). Neither table readable, or no readable
// SimDayManager in it: refused, "match-day state unknown: ...".
std::string write_gate(Memory& mem, const Located& loc, bool switched_off, uint64_t managers, uint64_t comm = 0);

// One engine block Turbo changed (a 0x18-byte StandingData or FixtureData): its bytes before and the bytes Turbo wrote
static_assert(kStandingSize == kFixtureSize, "a Patch holds a row or a fixture");
struct Patch {
    uint64_t addr = 0;
    uint8_t before[kStandingSize] = {};
    uint8_t after[kStandingSize] = {};
    std::string what;  // "the home row", "the fixture" (messages)
};
// The blocks of one multi-block edit (a result: both rows and the fixture; a venue swap or a new opponent: the fixture),
// kept for Undo. Every write goes block by block: the block must still hold `before`, only the bytes that differ are
// written, then read back; a failure part-way puts the blocks already written back as they were.
struct Change {
    std::vector<Patch> patches;  // in write order
    bool empty() const { return patches.empty(); }
};
// Undo `c`: every block must still lie in the located lists and hold what Turbo wrote (else nothing is written: a match
// day or another edit came in between); written back in reverse order, each read back. "" = undone
std::string undo_change(Memory& mem, const Located& loc, const Change& c);

// Limits of a row: every counter 0..255, points -32768..32767. "" = fine
std::string check_row(const StandingRow& row);
// Write the counters and points of `row` (identity bytes id/compobj/team must still match in memory), read them back (a
// mismatch puts the row back as it was). "" = done
std::string write_row(Memory& mem, const Located& loc, const StandingRow& row);

// Add (sign +1) or remove (sign -1) one result from the two rows. Returns "" or the limit that would be broken
// (nothing is changed then).
std::string apply_result(StandingRow& home, StandingRow& away, int home_score, int away_score, const Points& pts, int sign);
// Change the score of a played fixture: the fixture bytes and both rows are patched consistently (home row, away row, the
// fixture's two scores; all or nothing). "" = done; `done` (optional) gets the blocks for Undo (empty when nothing changed)
std::string edit_result(Memory& mem, const Located& loc, uint16_t fixture_id, int new_home, int new_away, const Points& pts,
                        Change* done = nullptr);

// ---- match setup: edits of fixtures that have not been played yet (docs/re/match_setup.md section 4). FixtureData is
// what the engine's scheduler plays or simulates on its date (+0x0A / +0x0C are the two standing rows, so the home side
// is the venue). The career's hub keeps its own list of the user's fixtures, re-requested from FCE on career events and
// match days, so the hub may show the old pairing until the next day advance: the callers say so.
// Swap the home and away rows of an unplayed fixture (the venue changes side). "" = done; `done` (optional) gets the
// fixture block for Undo
std::string swap_fixture_sides(Memory& mem, const Located& loc, uint16_t fixture_id, Change* done = nullptr);
// Put the rows `home_sid` / `away_sid` on an unplayed fixture (two different used rows of the same group as the rows the
// fixture holds now). "" = done; `done` (optional) gets the fixture block for Undo (empty when nothing changed)
std::string set_fixture_teams(Memory& mem, const Located& loc, uint16_t fixture_id, int16_t home_sid, int16_t away_sid,
                              Change* done = nullptr);
// A new pairing must not put a club into two fixtures on one day, in any competition (clubs compared by team id through
// `rows`): "" or the clash (fixtures and rows indexed by id)
std::string pairing_conflict(const std::vector<Fixture>& fixtures, const std::vector<StandingRow>& rows, uint16_t fixture_id, int16_t home_sid,
                             int16_t away_sid);
// The team's next fixture on or after `today` (YYYYMMDD): the earliest unplayed used fixture whose home or away row belongs
// to `team`. nullptr when none. `rows` resolves the row ids.
const Fixture* next_fixture(const std::vector<Fixture>& fixtures, const std::vector<StandingRow>& rows, uint32_t team, uint32_t today);

// ---- inline table edits (Competitions > Live standings, ui/ui_standings.cpp): one cell of a row set to a typed value.
// P (W + D + L, home + away) and GD (GF - GA) are not stored: they follow the other cells.
enum class Cell { P, W, D, L, GF, GA, GD, Pts, HW, HD, HL, HGF, HGA, AW, AD, AL, AGF, AGA };
const char* cell_code(Cell c);   // "W", "HGF", ... (column header, test labels)
const char* cell_name(Cell c);   // "W", "home GF", ... (status lines)
bool cell_editable(Cell c);      // false for P and GD
int cell_value(const StandingRow& r, Cell c);
// The games a row can have at home / away: the used fixtures of the list naming it on that side, at least a double round
// robin of its group (group_size - 1 a side), so a fixture list that is not complete never blocks a real value
struct FixtureCap {
    int home = 255, away = 255;
};
FixtureCap fixture_cap(const std::vector<Fixture>& fixtures, uint16_t sid, int group_size);
// Set cell `c` of `row` to `value`. A total (W, D, L, GF, GA) is split home / away the way the game stores it: a game
// added goes to the side with fewer games played (that still has a fixture free), one removed comes off the side with
// more; goals go to a side with games played, fewer goals first, and come off the side with more. Pts moves by
// pts.win / draw / loss per W / D / L added or removed (never below 0) unless keep_points; Pts typed is kept as typed.
// Refused (row unchanged, the reason returned): negatives, P (or a side's P) past the fixtures of `cap`, a counter past
// 255, goals while no game is played, P / GD (they follow the other cells).
std::string set_cell(StandingRow& row, Cell c, int value, const Points& pts, const FixtureCap& cap, bool keep_points);
// "W 2 -> 3, Pts 0 -> 3": the totals (W, D, L, GF, GA, Pts) that differ; "home / away split" when only the split moved;
// "" when nothing changed
std::string describe_change(const StandingRow& before, const StandingRow& after);
// The counters and points of two row states are the same
bool same_counters(const StandingRow& a, const StandingRow& b);

}  // namespace fce
}  // namespace turbo
