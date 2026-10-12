// FC 27 LE Turbo GUI - player archetypes: a role (Poacher, Ball Playing Defender, Sweeper Keeper ...) is a profile of the 34
// attributes. Applying one reshapes a player's attributes to that profile while his overall rating stays the same, or (2.3)
// so that the overall becomes a target 1..99 (platform independent; overall.h has the rating formula).
#pragma once
#include <array>
#include <string>
#include <vector>

#include "core/overall.h"

namespace turbo {
namespace archetypes {

struct Archetype {
    const char* name;
    overall::Group group;      // the position group it belongs to
    const char* blurb;         // one line for the tooltip
    // Relative target per attribute (order of overall::Attr), 0..100; 0 = the attribute is not part of the profile and is
    // left as it is. The profile is scaled so the player's overall is kept, so only the ratios matter.
    std::array<int, overall::kAttrCount> profile;
};

const std::vector<Archetype>& all();
// Archetypes of one position group, in display order
std::vector<const Archetype*> of_group(overall::Group g);

struct Result {
    overall::Attrs attrs;   // the new attributes
    int overall_before = 0; // compute_overall of the old attributes (position formula)
    int overall_after = 0;  // ... of the new ones: equal to overall_before unless the profile cannot reach it
    bool exact = false;     // overall_after == overall_before
};

// Reshape `cur` (a player of `position`) to the archetype keeping his overall: apply_target() with the target
// compute_overall(position, cur) (always reachable: his own attributes give it). false only when the archetype has no
// profile values.
bool apply(const Archetype& a, int position, const overall::Attrs& cur, Result& out);

// ---- 2.3: an archetype with a target overall (Players > Attributes, Create player)
// The profile is walked from "every profile attribute at 1" to "every profile attribute at 99" one point at a time, in the
// order their scaled values p * s pass each whole number as the scale s grows (equal values: the attribute that weighs most
// for the position first). Every point of that walk keeps the profile's shape (keeps_shape below) and each step moves the
// formula by one attribute's weight (< 0.25), so the rounded overall never skips a value: every overall between the two
// ends of the walk is hit exactly. Of the points that give the target, the one whose unrounded formula value is nearest
// the target is taken (the game's own rounding then agrees best). Attributes outside the profile keep their value, unless
// the target lies beyond the walk (the profile clamped at 1 or 99): then they move too, the heaviest for the position first
// and spread (as overall::reach does), and `outside_moved` lists them. A target that not even every weighted attribute at
// its limit reaches is refused, with the closest overall that can be reached.
struct TargetResult {
    overall::Attrs attrs{};              // the new attributes (= the old ones when refused)
    int target = 0;                      // the overall asked for
    int overall_before = 0;              // compute_overall(position, old attributes)
    int overall_after = 0;               // compute_overall(position, attrs): == target when ok
    double raw_after = 0.0;              // raw_overall(position, attrs)
    bool ok = false;                     // overall_after == target
    int closest = 0;                     // the reachable overall nearest the target (== target when ok)
    int profile_lo = 0, profile_hi = 0;  // the overalls of the walk's two ends (the profile alone, the others kept)
    int lo = 0, hi = 0;                  // the overalls this position can have at all (every weighted attribute at 1 / 99)
    std::vector<int> outside_moved;      // attributes outside the profile that had to move (overall::Attr order)
    int at_limit = 0;                    // profile attributes at 1 or 99 in the result (clamped)
    std::string why;                     // refused: the reason in words ("" when ok)
};
// false (attrs = cur, why and closest set) when the target cannot be reached or the archetype has no profile values
bool apply_target(const Archetype& a, int position, const overall::Attrs& cur, int target, TargetResult& out);
// The Target overall box's default: the stored overall the game shows, unless it is outside 1..99 or broken (OVR-1, the
// rule of overall::follow), then the formula's value
int default_target(int stored, int formula);
// The profile's shape: an attribute with a higher profile value is never below one with a lower value, and two with the
// same value differ by one point at most (attributes outside the profile are not looked at)
bool keeps_shape(const Archetype& a, const overall::Attrs& attrs);
// True when the attribute is part of the archetype's profile
inline bool in_profile(const Archetype& a, int attr) {
    return attr >= 0 && attr < overall::kAttrCount && a.profile[static_cast<size_t>(attr)] > 0;
}

}  // namespace archetypes
}  // namespace turbo
