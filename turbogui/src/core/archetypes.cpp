// FC 27 LE Turbo GUI - player archetypes (archetypes.h)
#include "core/archetypes.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace turbo {
namespace archetypes {

using namespace overall;

namespace {

using Pairs = std::initializer_list<std::pair<Attr, int>>;
using Profile = std::array<int, kAttrCount>;

// Outfield profile: the listed attributes; every other attribute is `base` (all callers pass 0 = left as the player has it)
Profile outfield(int base, Pairs list) {
    Profile p{};
    for (int i = 0; i < kAttrCount; ++i) p[static_cast<size_t>(i)] = (i >= GKDiving && i <= GKPositioning) ? 0 : base;
    for (const auto& kv : list) p[static_cast<size_t>(kv.first)] = kv.second;
    return p;
}

// Goalkeeper profile: the listed attributes, the other goalkeeping ones at `base`, the outfield ones untouched (0)
Profile keeper(int base, Pairs list) {
    Profile p{};
    for (int i = GKDiving; i <= GKPositioning; ++i) p[static_cast<size_t>(i)] = base;
    for (const auto& kv : list) p[static_cast<size_t>(kv.first)] = kv.second;
    return p;
}

std::vector<Archetype> build() {
    std::vector<Archetype> v;
    auto add = [&](const char* name, Group g, const char* blurb, const Profile& p) { v.push_back({name, g, blurb, p}); };

    // ---- goalkeepers
    add("Shot Stopper", GroupGK, "Diving and reflexes first: saves what other keepers only touch",
        keeper(60, {{GKDiving, 95}, {GKReflexes, 95}, {GKHandling, 85}, {GKPositioning, 85}, {GKKicking, 58}, {Reactions, 88}}));
    add("Sweeper Keeper", GroupGK, "Plays high, reads the through ball and starts attacks with his feet",
        keeper(60, {{GKKicking, 92}, {GKReflexes, 88}, {GKDiving, 84}, {GKHandling, 78}, {GKPositioning, 88}, {Reactions, 86}}));
    add("Commanding Keeper", GroupGK, "Handling and positioning: claims crosses and organises the box",
        keeper(60, {{GKHandling, 94}, {GKPositioning, 93}, {GKDiving, 84}, {GKReflexes, 84}, {GKKicking, 62}, {Reactions, 82}}));
    add("Distributor", GroupGK, "Kicking and throwing as a weapon, solid rather than spectacular",
        keeper(60, {{GKKicking, 96}, {GKHandling, 80}, {GKDiving, 80}, {GKReflexes, 80}, {GKPositioning, 82}, {Reactions, 80}}));

    // ---- centre-backs
    add("Stopper", GroupCB, "Aggressive, strong and good in the air: wins the duel before the ball arrives",
        outfield(0, {{StandingTackle, 92}, {SlidingTackle, 85}, {Aggression, 92}, {Strength, 92}, {Heading, 90}, {Jumping, 88},
                      {Interceptions, 82}, {DefAwareness, 86}, {Reactions, 78}, {Composure, 70}, {SprintSpeed, 62}, {Acceleration, 58}}));
    add("Ball Playing Defender", GroupCB, "Calm on the ball, passes through the lines and still defends",
        outfield(0, {{ShortPassing, 88}, {LongPassing, 85}, {BallControl, 80}, {Vision, 70}, {Composure, 88}, {Interceptions, 88},
                      {DefAwareness, 88}, {StandingTackle, 85}, {SlidingTackle, 72}, {Reactions, 85}, {Heading, 78}, {Strength, 78},
                      {Jumping, 75}, {Aggression, 65}}));
    add("Cover Defender", GroupCB, "Quick and anticipating: sweeps up behind the line",
        outfield(0, {{Interceptions, 95}, {DefAwareness, 92}, {SprintSpeed, 86}, {Acceleration, 82}, {Reactions, 88}, {StandingTackle, 85},
                      {SlidingTackle, 88}, {Composure, 85}, {Strength, 70}, {Heading, 70}, {Stamina, 75}, {Jumping, 70}, {ShortPassing, 65}}));
    add("Aerial Dominator", GroupCB, "Height, jump and strength: owns every ball in the air",
        outfield(0, {{Heading, 97}, {Jumping, 95}, {Strength, 95}, {Aggression, 85}, {StandingTackle, 85}, {DefAwareness, 85},
                      {Interceptions, 80}, {Reactions, 75}, {Composure, 65}, {SprintSpeed, 50}, {Acceleration, 45}, {ShortPassing, 52}}));

    // ---- full-backs and wing-backs
    add("Defensive Full-back", GroupFB, "Holds the flank: tackling, interceptions and recovery pace",
        outfield(0, {{StandingTackle, 90}, {SlidingTackle, 85}, {Interceptions, 88}, {DefAwareness, 88}, {Aggression, 80}, {Strength, 78},
                      {Stamina, 85}, {SprintSpeed, 78}, {Acceleration, 78}, {Heading, 70}, {Reactions, 75}, {Crossing, 52},
                      {Dribbling, 55}, {ShortPassing, 66}, {BallControl, 62}}));
    add("Attacking Wing-back", GroupFB, "Runs the whole flank: pace, stamina and crosses",
        outfield(0, {{Crossing, 90}, {Stamina, 95}, {SprintSpeed, 90}, {Acceleration, 90}, {Dribbling, 82}, {BallControl, 80},
                      {ShortPassing, 78}, {Agility, 80}, {Vision, 65}, {StandingTackle, 70}, {Interceptions, 70}, {DefAwareness, 65},
                      {SlidingTackle, 62}, {Reactions, 78}, {Curve, 70}}));
    add("Playmaking Full-back", GroupFB, "Tucks inside and builds play: passing, vision and composure",
        outfield(0, {{ShortPassing, 90}, {LongPassing, 80}, {Vision, 78}, {BallControl, 85}, {Composure, 82}, {Interceptions, 80},
                      {StandingTackle, 78}, {DefAwareness, 78}, {Stamina, 85}, {Reactions, 82}, {Crossing, 65}, {Dribbling, 70},
                      {SprintSpeed, 75}, {Acceleration, 74}}));

    // ---- defensive midfielders
    add("Anchor", GroupDM, "Sits in front of the back line: reads the game and breaks up attacks",
        outfield(0, {{Interceptions, 95}, {DefAwareness, 92}, {StandingTackle, 90}, {Strength, 85}, {Aggression, 80}, {Composure, 82},
                      {Reactions, 85}, {ShortPassing, 75}, {LongPassing, 65}, {Heading, 78}, {SlidingTackle, 80}, {Stamina, 75},
                      {BallControl, 65}, {Vision, 55}}));
    add("Ball Winner", GroupDM, "Presses and tackles all game: aggression and engine",
        outfield(0, {{Aggression, 95}, {StandingTackle, 95}, {SlidingTackle, 92}, {Interceptions, 88}, {Stamina, 92}, {Strength, 88},
                      {DefAwareness, 82}, {Reactions, 80}, {ShortPassing, 65}, {BallControl, 60}, {Heading, 70}}));
    add("Deep-Lying Playmaker", GroupDM, "Dictates from deep: long and short passing, vision, composure",
        outfield(0, {{ShortPassing, 92}, {LongPassing, 92}, {Vision, 90}, {BallControl, 85}, {Composure, 88}, {Reactions, 88},
                      {Interceptions, 78}, {StandingTackle, 70}, {DefAwareness, 72}, {Curve, 75}, {FKAccuracy, 70}, {Stamina, 70},
                      {Strength, 65}}));

    // ---- central midfielders
    add("Box to Box", GroupCM, "Covers every blade of grass: stamina, strength and a long shot",
        outfield(0, {{Stamina, 95}, {Strength, 82}, {Aggression, 78}, {ShortPassing, 85}, {BallControl, 82}, {Dribbling, 75},
                      {LongShots, 78}, {Interceptions, 75}, {StandingTackle, 78}, {Reactions, 85}, {SprintSpeed, 78}, {Positioning, 75},
                      {Composure, 78}, {Vision, 72}, {Acceleration, 72}}));
    add("Playmaker", GroupCM, "The creative hub: vision, passing range and ball control",
        outfield(0, {{Vision, 95}, {ShortPassing, 93}, {LongPassing, 90}, {BallControl, 90}, {Dribbling, 80}, {Composure, 88},
                      {Reactions, 90}, {Curve, 82}, {FKAccuracy, 75}, {Agility, 80}, {Balance, 78}, {Positioning, 75}, {Stamina, 75},
                      {Interceptions, 55}, {StandingTackle, 50}}));
    add("Ball Winning Midfielder", GroupCM, "Wins it back in midfield and keeps it simple",
        outfield(0, {{StandingTackle, 90}, {Interceptions, 90}, {Aggression, 90}, {Stamina, 90}, {Strength, 85}, {DefAwareness, 82},
                      {SlidingTackle, 82}, {ShortPassing, 75}, {Reactions, 80}, {BallControl, 70}, {LongPassing, 68}, {Vision, 62}}));
    add("Technical Midfielder", GroupCM, "Dribbles through the middle: agility, balance and close control",
        outfield(0, {{Dribbling, 90}, {BallControl, 92}, {Agility, 88}, {Balance, 85}, {ShortPassing, 85}, {Vision, 80}, {Composure, 82},
                      {Reactions, 85}, {Acceleration, 82}, {SprintSpeed, 75}, {Positioning, 78}, {LongShots, 75}, {Stamina, 80},
                      {Finishing, 65}, {StandingTackle, 55}, {Interceptions, 55}}));

    // ---- attacking midfielders
    add("Advanced Playmaker", GroupAM, "The classic number 10: vision, final pass, close control",
        outfield(0, {{Vision, 95}, {ShortPassing, 92}, {BallControl, 92}, {Dribbling, 88}, {Agility, 86}, {Composure, 88}, {Reactions, 90},
                      {Positioning, 82}, {Curve, 80}, {LongShots, 80}, {Finishing, 75}, {Balance, 82}, {Acceleration, 78},
                      {SprintSpeed, 70}, {Stamina, 65}, {FKAccuracy, 72}}));
    add("Shadow Striker", GroupAM, "Arrives late in the box: positioning, pace and finishing",
        outfield(0, {{Positioning, 95}, {Finishing, 88}, {SprintSpeed, 85}, {Acceleration, 88}, {Dribbling, 85}, {BallControl, 85},
                      {Composure, 85}, {Reactions, 88}, {ShotPower, 82}, {LongShots, 78}, {ShortPassing, 75}, {Vision, 72}, {Agility, 82},
                      {Stamina, 75}, {Volleys, 75}}));
    add("Trequartista", GroupAM, "Free-roaming artist: dribbling, agility, curl and flair",
        outfield(0, {{Dribbling, 94}, {BallControl, 94}, {Agility, 92}, {Balance, 88}, {Vision, 88}, {ShortPassing, 85}, {Curve, 85},
                      {Composure, 85}, {Finishing, 78}, {Reactions, 85}, {Positioning, 78}, {Acceleration, 78}, {SprintSpeed, 70},
                      {FKAccuracy, 80}, {LongShots, 78}}));
    add("Long-Range Shooter", GroupAM, "Unlocks defences from distance: shot power, long shots, set pieces",
        outfield(0, {{LongShots, 95}, {ShotPower, 92}, {Curve, 85}, {FKAccuracy, 85}, {Vision, 78}, {ShortPassing, 80}, {BallControl, 82},
                      {Dribbling, 78}, {Composure, 80}, {Reactions, 82}, {Positioning, 80}, {Finishing, 78}, {Penalties, 80}}));

    // ---- wide midfielders and wingers
    add("Winger", GroupWide, "Beats the full-back on pace and delivers: speed, dribbling, crossing",
        outfield(0, {{SprintSpeed, 95}, {Acceleration, 95}, {Crossing, 90}, {Dribbling, 90}, {Agility, 90}, {BallControl, 85}, {Balance, 85},
                      {Stamina, 80}, {Vision, 70}, {ShortPassing, 75}, {Curve, 78}, {Reactions, 80}, {Finishing, 65}, {Positioning, 78}}));
    add("Inside Forward", GroupWide, "Cuts in from the flank to score: finishing, positioning, dribbling",
        outfield(0, {{Finishing, 90}, {Positioning, 90}, {Dribbling, 90}, {Acceleration, 90}, {SprintSpeed, 90}, {BallControl, 88},
                      {ShotPower, 85}, {Composure, 85}, {Reactions, 85}, {Agility, 88}, {LongShots, 82}, {Curve, 78}, {Crossing, 60},
                      {Vision, 70}, {ShortPassing, 72}}));
    add("Playmaking Winger", GroupWide, "Creates from the wing: vision, passing, curl, close control",
        outfield(0, {{Vision, 90}, {ShortPassing, 90}, {Crossing, 88}, {Curve, 88}, {BallControl, 90}, {Dribbling, 88}, {Agility, 85},
                      {Reactions, 85}, {Composure, 82}, {Positioning, 78}, {SprintSpeed, 78}, {Acceleration, 80}, {FKAccuracy, 78},
                      {LongPassing, 78}, {Stamina, 78}, {Finishing, 65}}));
    add("Wide Midfielder", GroupWide, "The work-rate winger: stamina, crossing and tracking back",
        outfield(0, {{Stamina, 92}, {Crossing, 90}, {ShortPassing, 82}, {LongPassing, 75}, {BallControl, 78}, {Dribbling, 75}, {SprintSpeed, 80},
                      {Acceleration, 78}, {Vision, 75}, {Reactions, 78}, {Positioning, 70}, {Strength, 68}, {Interceptions, 55},
                      {StandingTackle, 52}, {Aggression, 60}}));

    // ---- strikers
    add("Poacher", GroupST, "Lives in the box: finishing, positioning, composure",
        outfield(0, {{Finishing, 97}, {Positioning, 95}, {Composure, 92}, {Reactions, 92}, {ShotPower, 88}, {Volleys, 85}, {BallControl, 80},
                      {Heading, 75}, {Acceleration, 80}, {SprintSpeed, 78}, {Dribbling, 70}, {Strength, 65}, {ShortPassing, 65},
                      {LongShots, 70}, {Penalties, 85}}));
    add("Target Man", GroupST, "Holds the ball up and wins aerial duels: strength, heading, jumping",
        outfield(0, {{Strength, 97}, {Heading, 95}, {Jumping, 94}, {Finishing, 85}, {Positioning, 85}, {ShotPower, 85}, {Aggression, 80},
                      {BallControl, 72}, {ShortPassing, 68}, {Reactions, 82}, {Composure, 80}, {SprintSpeed, 55}, {Acceleration, 50},
                      {Dribbling, 55}, {Volleys, 78}, {Balance, 55}, {Agility, 50}}));
    add("Complete Forward", GroupST, "Does everything: finishing, link-up play, pace and power",
        outfield(0, {{Finishing, 90}, {Positioning, 90}, {BallControl, 88}, {Dribbling, 85}, {ShotPower, 88}, {Heading, 82}, {Strength, 82},
                      {SprintSpeed, 82}, {Acceleration, 82}, {Reactions, 88}, {Composure, 88}, {ShortPassing, 78}, {Vision, 72},
                      {Volleys, 82}, {Jumping, 75}, {LongShots, 80}, {Agility, 75}}));
    add("Pressing Forward", GroupST, "First defender: stamina, aggression and pace to harry the back line",
        outfield(0, {{Stamina, 95}, {Aggression, 90}, {SprintSpeed, 88}, {Acceleration, 86}, {Finishing, 80}, {Positioning, 82}, {Strength, 80},
                      {Reactions, 82}, {BallControl, 75}, {Dribbling, 70}, {ShortPassing, 70}, {StandingTackle, 55}, {Interceptions, 52}}));
    add("Advanced Forward", GroupST, "Runs in behind: sprint speed, acceleration and a clinical finish",
        outfield(0, {{SprintSpeed, 97}, {Acceleration, 97}, {Finishing, 88}, {Positioning, 90}, {Dribbling, 85}, {BallControl, 82},
                      {Agility, 82}, {Reactions, 85}, {Composure, 82}, {ShotPower, 82}, {Balance, 78}, {Strength, 62}, {Heading, 62}}));
    add("Deep-Lying Forward", GroupST, "False nine: drops off to link play with vision and touch",
        outfield(0, {{ShortPassing, 90}, {Vision, 88}, {BallControl, 92}, {Dribbling, 88}, {Composure, 88}, {Finishing, 82}, {Positioning, 85},
                      {Agility, 82}, {Balance, 82}, {Reactions, 88}, {LongPassing, 75}, {Curve, 75}, {Acceleration, 75}, {SprintSpeed, 72},
                      {Strength, 55}, {Heading, 50}, {Aggression, 45}}));
    return v;
}

}  // namespace

const std::vector<Archetype>& all() {
    static const std::vector<Archetype> v = build();
    return v;
}

std::vector<const Archetype*> of_group(Group g) {
    std::vector<const Archetype*> out;
    for (const Archetype& a : all())
        if (a.group == g) out.push_back(&a);
    return out;
}

bool apply(const Archetype& arch, int position, const Attrs& cur, Result& out) {
    bool any = false;
    for (int p : arch.profile) any = any || p > 0;
    if (!any) return false;
    out.overall_before = compute_overall(position, cur);
    const int target = out.overall_before;
    // profile values times one factor s, clamped to 1..99; attributes outside the profile stay. The overall never falls
    // when s grows, so the smallest s that reaches the target comes from bisection.
    auto at = [&](double s) {
        Attrs r = cur;
        for (int i = 0; i < kAttrCount; ++i) {
            const int p = arch.profile[static_cast<size_t>(i)];
            if (p > 0) r[static_cast<size_t>(i)] = std::max(1, std::min(99, static_cast<int>(std::floor(p * s + 0.5))));
        }
        return r;
    };
    double lo = 0.0, hi = 4.0;
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (compute_overall(position, at(mid)) < target) lo = mid;
        else hi = mid;
    }
    out.attrs = at(hi);
    // rounding leaves the overall off by a point at most: a few points on the heaviest attributes close the gap
    for (int guard = 0; guard < 8; ++guard) {
        const int now = compute_overall(position, out.attrs);
        if (now == target || !adjust(position, out.attrs, now < target ? 1 : -1)) break;
    }
    out.overall_after = compute_overall(position, out.attrs);
    out.exact = out.overall_after == target;
    return true;
}

}  // namespace archetypes
}  // namespace turbo
