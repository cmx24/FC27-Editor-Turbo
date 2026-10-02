// FC 27 LE Turbo GUI - PlayStyle bit names (trait1/icontrait1 and trait2/icontrait2).
// Names follow FC 26's bit order; FC 27 may have added or re-ordered bits, so unnamed bits show as "Bit N".
#pragma once
#include <vector>

namespace turbo {

inline const std::vector<const char*>& playstyle1_names() {
    static const std::vector<const char*> n = {
        "Finesse Shot", "Chip Shot", "Power Shot", "Dead Ball", "Precision Header", "Acrobatic", "Low Driven Shot",
        "Game Changer", "Incisive Pass", "Pinged Pass", "Long Ball Pass", "Tiki Taka", "Whipped Pass", "Inventive",
        "Jockey", "Block", "Intercept", "Anticipate", "Slide Tackle", "Aerial Fortress", "Technical", "Rapid",
        "First Touch", "Trickster", "Press Proven", "Quick Step", "Relentless", "Long Throw", "Bruiser", "Enforcer"};
    return n;
}

inline const std::vector<const char*>& playstyle2_names() {
    static const std::vector<const char*> n = {
        "GK Far Throw", "GK Footwork", "GK Cross Claimer", "GK Rush Out", "GK Far Reach", "GK Deflector",
        "CPU AI: Long Shot Taker", "CPU AI: Early Crosser", "Solid Player", "Team Player", "One Club Player",
        "Injury Prone", "Leadership", "Super Sub"};
    return n;
}

}  // namespace turbo
