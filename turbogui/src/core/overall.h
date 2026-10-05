// FC 27 LE Turbo GUI - the overall rating of a player as a function of his attributes (platform independent).
// FC's overall is a position specific weighted sum of the 34 attributes plus a small position bonus. The weights below were
// fitted (non-negative least squares, one table per position) on ~21.7k real FC 27 players; compute_overall() matches the
// stored overallrating exactly for ~87-96% of them and is within +-1 for >99.7% (tests/overall/test_overall.cpp prints the
// figures). adjust() moves attributes so that compute_overall() changes by exactly +1 / -1.
#pragma once
#include <array>
#include <string>
#include <vector>

namespace turbo {
namespace overall {

// The 34 attributes in a fixed order (names = the players table's field names, see kAttrNames)
enum Attr {
    Acceleration, SprintSpeed, Agility, Balance, Jumping, Stamina, Strength, Reactions, Aggression, Composure,
    Interceptions, Positioning, Vision, BallControl, Crossing, Dribbling, Finishing, FKAccuracy, Heading, LongPassing,
    ShortPassing, ShotPower, LongShots, StandingTackle, SlidingTackle, Volleys, Curve, Penalties, GKDiving, GKHandling,
    GKKicking, GKReflexes, GKPositioning, DefAwareness,
    kAttrCount
};
extern const char* const kAttrNames[kAttrCount];   // "acceleration", ..., "defensiveawareness"
int attr_index(const std::string& field_name);     // -1 when it is not one of the 34

using Attrs = std::array<int, kAttrCount>;

// Position groups (preferredposition1 ids: 0 GK, 1 SW, 2 RWB, 3 RB, 4 RCB, 5 CB, 6 LCB, 7 LB, 8 LWB, 9 RDM, 10 CDM, 11 LDM,
// 12 RM, 13 RCM, 14 CM, 15 LCM, 16 LM, 17 RAM, 18 CAM, 19 LAM, 20 RF, 21 CF, 22 LF, 23 RW, 24 RS, 25 ST, 26 LS, 27 LW)
enum Group { GroupGK, GroupCB, GroupFB, GroupDM, GroupCM, GroupAM, GroupWide, GroupST, kGroupCount };
Group group_of_position(int position);
const char* group_name(Group g);        // "Goalkeeper", "Centre-back", ...

// The unrounded formula value for this position (weights . attributes + bonus)
double raw_overall(int position, const Attrs& a);
// The rounded overall, 1..99
int compute_overall(int position, const Attrs& a);
// Weight of one attribute in the position's formula (0 when it does not count), for the UI
double attr_weight(int position, int attr);

struct Move {
    int attr = 0;
    int from = 0, to = 0;
};

// Change the attributes so compute_overall() goes up (delta = +1) or down (delta = -1) by exactly one. The points go to the
// attributes that weigh most for the position and are spread over them (never beyond 1..99). Returns false, leaving `a`
// untouched, when the overall is already at 1 / 99 or no attribute can move any more. `moves` (optional) lists the changes.
bool adjust(int position, Attrs& a, int delta, std::vector<Move>* moves = nullptr);
inline bool can_adjust(int position, const Attrs& a, int delta) {
    Attrs t = a;
    return adjust(position, t, delta, nullptr);
}

}  // namespace overall
}  // namespace turbo
