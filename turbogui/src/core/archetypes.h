// FC 27 LE Turbo GUI - player archetypes: a role (Poacher, Ball Playing Defender, Sweeper Keeper ...) is a profile of the 34
// attributes. Applying one reshapes a player's attributes to that profile while his overall rating stays the same
// (platform independent; overall.h has the rating formula).
#pragma once
#include <array>
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

// Reshape `cur` (a player of `position`) to the archetype: profile values scaled by one factor so that compute_overall()
// stays what it was, rounded and clamped to 1..99; attributes outside the profile keep their value; a last few points on
// the heaviest attributes fix any rounding difference. false only when the archetype has no profile values.
bool apply(const Archetype& a, int position, const overall::Attrs& cur, Result& out);

}  // namespace archetypes
}  // namespace turbo
