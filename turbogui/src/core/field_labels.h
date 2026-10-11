// FC 27 LE Turbo GUI - readable text for code-valued fields (roles, body type, emotion, colours, accessories, hair styles,
// boots, weather, morale, injuries...). Platform independent, header only (head type groups: face_filter.cpp; hair:
// hair_catalog.cpp). Where the text comes from:
//   * Named: Live Editor's own localization (loc/eng_us/localize.json: player_role_N, bodytype_N, emotion_N,
//     skintonecode_N, haircolor_N, eyecolor_N, accessorycode_N, accessorycolourcode_N, shoecolorcode1_N,
//     hasseasonaljersey_N, jersey*/sock*/shortstyle_N; weather_N, morale_N, injuryname_N through le_loc_labels_data.h,
//     scripts/gen_loc_labels.py), and the small enumerations Live Editor does not name (enum_defs: stars, work rates...).
//   * Described: what the data says about a code Live Editor does not name. appearance_labels_data.h
//     (scripts/gen_appearance_labels.py) has who uses each player-specific body type, stance and running animation in
//     the FC 27 database ("Unique: Lionel Messi") and the brand of each boot and GK glove id; hair colours 15..27 are
//     named after the colour of each code's preview; hair styles by the hair catalog, facial hair by their look
//     (face_looks.h), head types by their group (face_filter.h), eyebrows by their parts.
//   * Unnamed: nothing describes it: "Unnamed N" / "Boots #N" (never an empty text, never a bare number, never "code N").
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "appearance_labels_data.h"
#include "face_filter.h"
#include "face_looks.h"
#include "hair_catalog.h"
#include "le_loc_labels_data.h"

namespace turbo::labels {

using NamedCodes = std::vector<std::pair<int64_t, const char*>>;

inline const std::map<std::string, NamedCodes>& named_tables() {
    static const std::map<std::string, NamedCodes> m = [] {
    std::map<std::string, NamedCodes> t = {
    {"role", {{0, "None"}, {1, "GK Goalkeeper+"}, {2, "GK Sweeper Keeper +"}, {3, "RB Fullback+"}, {4, "RB Falseback+"}, {5, "RB Wingback+"}, {6, "RB Attacking Wingback+"}, {7, "LB Fullback+"}, {8, "LB Falseback+"}, {9, "LB Wingback+"}, {10, "LB Attacking Wingback+"}, {11, "CB Defender+"}, {12, "CB Stopper+"}, {13, "CB Ball-Playing Defender+"}, {14, "CDM Holding+"}, {15, "CDM Centre Half+"}, {16, "CDM Deep-Lying Playmaker+"}, {17, "CDM Wide Half+"}, {18, "CM Box-to-Box+"}, {19, "CM Holding+"}, {20, "CM Deep-Lying Playmaker+"}, {21, "CM Playmaker+"}, {22, "CM Half-Winger+"}, {23, "RM Winger+"}, {24, "RM Wide Midfielder+"}, {25, "RM Wide Playmaker+"}, {26, "RM Inside Forward+"}, {27, "LM Winger+"}, {28, "LM Wide Midfielder+"}, {29, "LM Wide Playmaker+"}, {30, "LM Inside Forward+"}, {31, "CAM Playmaker+"}, {32, "CAM Shadow Striker+"}, {33, "CAM Half Winger+"}, {34, "CAM Classic 10+"}, {35, "RW Winger+"}, {36, "RW Inside Forward+"}, {37, "RW Wide Playmaker+"}, {38, "LW Winger+"}, {39, "LW Inside Forward+"}, {40, "LW Wide Playmaker+"}, {41, "ST Advanced Forward+"}, {42, "ST Poacher+"}, {43, "ST False 9+"}, {44, "ST Target Forward+"}, {45, "GK Ball-Playing Keeper+"}, {46, "RB Inverted Wingback+"}, {47, "LB Inverted Wingback+"}, {48, "CB Wide Back+"}, {49, "CDM Box Crasher+"}, {50, "ST Roaming Striker+"}, {51, "RW False Winger+"}, {52, "LW False Winger+"}, {101, "GK Goalkeeper++"}, {102, "GK Sweeper Keeper ++"}, {103, "RB Fullback++"}, {104, "RB Falseback++"}, {105, "RB Wingback++"}, {106, "RB Attacking Wingback++"}, {107, "LB Fullback++"}, {108, "LB Falseback++"}, {109, "LB Wingback++"}, {110, "LB Attacking Wingback++"}, {111, "CB Defender++"}, {112, "CB Stopper++"}, {113, "CB Ball-Playing Defender++"}, {114, "CDM Holding++"}, {115, "CDM Centre Half++"}, {116, "CDM Deep-Lying Playmaker++"}, {117, "CDM Wide Half++"}, {118, "CM Box-to-Box++"}, {119, "CM Holding++"}, {120, "CM Deep-Lying Playmaker++"}, {121, "CM Playmaker++"}, {122, "CM Half-Winger++"}, {123, "RM Winger++"}, {124, "RM Wide Midfielder++"}, {125, "RM Wide Playmaker++"}, {126, "RM Inside Forward++"}, {127, "LM Winger++"}, {128, "LM Wide Midfielder++"}, {129, "LM Wide Playmaker++"}, {130, "LM Inside Forward++"}, {131, "CAM Playmaker++"}, {132, "CAM Shadow Striker++"}, {133, "CAM Half Winger++"}, {134, "CAM Classic 10++"}, {135, "RW Winger++"}, {136, "RW Inside Forward++"}, {137, "RW Wide Playmaker++"}, {138, "LW Winger++"}, {139, "LW Inside Forward++"}, {140, "LW Wide Playmaker++"}, {141, "ST Advanced Forward++"}, {142, "ST Poacher++"}, {143, "ST False 9++"}, {144, "ST Target Forward++"}, {145, "GK Ball-Playing Keeper++"}, {146, "RB Inverted Wingback++"}, {147, "LB Inverted Wingback++"}, {148, "CB Wide Back++"}, {149, "CDM Box Crasher++"}, {150, "ST Roaming Striker++"}, {151, "RW False Winger++"}, {152, "LW False Winger++"}}},
    {"bodytypecode", {{1, "Average and Lean"}, {2, "Average and Normal"}, {3, "Average and Stocky"}, {4, "Tall and Lean"}, {5, "Tall and Normal"}, {6, "Tall and Stocky"}, {7, "Short and Lean"}, {8, "Short and Normal"}, {9, "Short and Stocky"}, {11, "Very Tall and Lean"}}},
    {"emotion", {{1, "Ice Cold"}, {2, "Calm"}, {3, "Average"}, {4, "Hot Temper"}, {5, "Volcano"}}},
    // FC 27 stores the skin tone as 10, 20 .. 100 (Live Editor's skintonecode_1..10 times 10; every player of the
    // database has one of these ten)
    {"skintonecode", {{10, "Caucasian 1"}, {20, "Caucasian 2"}, {30, "Caucasian 3"}, {40, "Latin Asian 1"}, {50, "Latin Asian 2"}, {60, "Latin Asian 3"}, {70, "Latin Asian 4"}, {80, "African 1"}, {90, "African 2"}, {100, "African 3"}}},
    // hair colours 0..14; FC 27 has 0..27 (15..27: described_tables) and the facial hair colour uses the same palette
    // (table_key): its previews show the same 28 colours in the same order, and a beard has the hair's code more often
    // than any other in the database. Live Editor's facialhaircolor_N (Black, Blonde, Medium Brown, Dark Brown, Red) is
    // an older 5-colour list that no longer matches codes 2 and 4.
    {"haircolorcode", {{0, "Black"}, {1, "Blonde"}, {2, "Dirty Blonde"}, {3, "Dark Brown"}, {4, "Light Blonde"}, {5, "Light Brown"}, {6, "Medium Brown"}, {7, "Red"}, {8, "White"}, {9, "Silver"}, {10, "Green"}, {11, "Blue"}, {12, "Ginger"}, {13, "Dark Red"}, {14, "Pink"}}},
    {"eyecolorcode", {{1, "Blue"}, {2, "Light Blue"}, {3, "Brown"}, {4, "Light Brown"}, {5, "Hazel"}, {6, "Green"}, {7, "Light Green"}, {8, "Medium Blue"}, {9, "Dark Brown"}, {10, "Saturated Green"}}},
    {"accessorycode", {{0, "None"}, {2, "Hearphone"}, {4, "Left Watch"}, {5, "Right Watch"}, {6, "Left Hand Tape"}, {7, "Right Hand Tape"}, {8, "Left Wristle Tape"}, {9, "Right Wristle Tape"}, {10, "Left Knee Tape"}, {11, "Right Knee Tape"}, {12, "Left Knee Tutor"}, {13, "Right Knee Tutor"}, {14, "Ankle Tape"}, {16, "Gloves"}, {22, "Left Finger Tape"}, {23, "Right Finger Tape"}, {24, "Left Wide Wristle Tape"}, {25, "Right Wide Wristle Tape"}, {26, "Left Bracelet"}, {27, "Right Bracelet"}, {28, "Left Yellow Card"}, {29, "Right Yellow Card"}, {30, "Left Red Card"}, {31, "Right Red Card"}, {32, "Left Ref Pen"}, {33, "Right Ref Pen"}, {34, "Left Ref Book"}, {35, "Right Ref Book"}, {36, "Left Vanishing Spray"}, {37, "Right Vanishing Spray"}, {38, "Left Cellphone"}, {39, "Right Cellphone"}, {150, "Facemask_150"}, {151, "Facemask_151"}, {154, "Facemask_154"}, {156, "Osimhen Facemask"}, {158, "Facemask_158"}, {160, "Nose_attribute_160"}, {164, "Facemask_154"}, {190, "Knee brace"}, {200, "Gloves"}, {240, "Fitted Suit Tie"}}},
    {"accessorycolourcode", {{0, "White"}, {1, "Black"}, {2, "Blue"}, {3, "Red"}, {4, "Yellow"}, {5, "Dark Green"}, {6, "Orange"}, {7, "Violet"}, {8, "Brown"}, {9, "Pink"}, {10, "Bordeaux"}, {11, "Cyan"}, {12, "Dark Blue"}, {99, "None"}}},
    // shoecolorcode1 and shoecolorcode2 (Live Editor names both the same 32 colours)
    {"shoecolorcode", {{0, "Black"}, {1, "Violet"}, {2, "Dark Blue"}, {3, "Blue"}, {4, "Light Blue"}, {5, "Green"}, {6, "Lime"}, {7, "Yellow"}, {8, "Dark Yellow"}, {9, "Orange"}, {10, "Dark Pink"}, {11, "Red"}, {12, "Dark Purple"}, {13, "Gold"}, {14, "Grey"}, {15, "White"}, {16, "Mint"}, {17, "Sky Blue"}, {18, "Dark Orange"}, {19, "Dark Green"}, {20, "Purple"}, {21, "Coral"}, {22, "Tuscany Yellow"}, {23, "Lemon"}, {24, "Pistacho"}, {25, "Teal"}, {26, "Pink"}, {27, "Light Mint"}, {28, "Light Yellow"}, {29, "Fuchsia"}, {30, "Dark Red"}, {31, "Light Green"}}},
    {"jerseysleevelengthcode", {{0, "Short"}, {1, "Long"}, {2, "Long and Turtleneck"}, {3, "Seasonal Undershirt"}, {4, "Seasonal Undershirt and Turtleneck"}}},
    {"hasseasonaljersey", {{0, "Short"}, {1, "Long"}, {2, "Long and Turtleneck"}, {3, "Seasonal Undershirt"}, {4, "Seasonal Undershirt and Turtleneck"}}},
    {"jerseyfit", {{0, "Normal"}, {1, "Tight"}, {2, "Team Kit Fit"}}},
    {"jerseystylecode", {{0, "Tucked In"}, {1, "Untucked"}}},
    {"socklengthcode", {{0, "Medium"}, {1, "Short"}, {2, "Long"}, {3, "Low"}}},
    {"sockstylecode", {{0, "No Shin Pad"}, {1, "Regular"}, {2, "Small Shin Pad"}, {3, "Micro Shin Pad"}, {5, "One Hole"}, {6, "Two Holes"}, {7, "Three Holes"}, {8, "Multi Holes"}}},
    {"shortstyle", {{0, "GK Shorts"}, {1, "GK Pants"}}},
    {"preferredfoot", {{1, "Right"}, {2, "Left"}}},
    {"gender", {{0, "Male"}, {1, "Female"}}},
    {"headclasscode", {{0, "Specific"}, {1, "Generic"}}},
    };
    // Live Editor's localization: the match weather (0..4), player morale (0..4), injury types (0..84)
    for (const data::LocNamed& e : data::kLocNamed) t[e.table].emplace_back(e.code, e.name);
    return t;
    }();
    return m;
}

// Codes Live Editor does not name, described from the data: FC 27's hair colours 15..27 after the colour of each code's
// preview (checked against the 3D looks of the real faces: 21 renders black, 19 and 20 brown, 27 red)
inline const std::map<std::string, NamedCodes>& described_tables() {
    static const std::map<std::string, NamedCodes> m = {
    {"haircolorcode", {{15, "Pale Blonde"}, {16, "Golden Blonde"}, {17, "Honey Blonde"}, {18, "Dark Blonde"}, {19, "Brown"}, {20, "Chestnut Brown"}, {21, "Jet Black"}, {22, "Cool Black"}, {23, "Dark Grey"}, {24, "Platinum"}, {25, "Orange Red"}, {26, "Auburn"}, {27, "Copper"}}},
    };
    return m;
}

// the label table a field uses: role1..role9 share "role", accessorycode1..4 share "accessorycode", the facial hair
// colour uses the hair colours, shoecolorcode1/2 share "shoecolorcode"
inline std::string table_key(const std::string& field) {
    std::string k = field;
    while (!k.empty() && std::isdigit(static_cast<unsigned char>(k.back()))) k.pop_back();
    if (k == "role" || k == "accessorycode" || k == "accessorycolourcode" || k == "shoecolorcode") return k;
    if (field == "facialhaircolorcode") return "haircolorcode";
    return field;
}

// the named codes of a field (nullptr when Live Editor names none)
inline const NamedCodes* named_codes(const std::string& field) {
    const auto& m = named_tables();
    auto it = m.find(table_key(field));
    return it == m.end() ? nullptr : &it->second;
}

inline const NamedCodes* described_codes(const std::string& field) {
    const auto& m = described_tables();
    auto it = m.find(table_key(field));
    return it == m.end() ? nullptr : &it->second;
}

inline const char* find_name(const NamedCodes& t, int64_t v) {
    for (const auto& e : t)
        if (e.first == v) return e.second;
    return nullptr;
}

// look of a hair / facial hair style: the nearest listed id (a tie goes to the lower id), as face_filter.cpp does
template <size_t N>
inline uint8_t style_look(const faces::looks::StyleLook (&tab)[N], int64_t v) {
    size_t best = 0;
    int64_t bestd = INT64_MAX;
    for (size_t i = 0; i < N; ++i) {
        int64_t d = tab[i].id > v ? tab[i].id - v : v - tab[i].id;
        if (d < bestd) { bestd = d; best = i; }
    }
    return tab[best].look;
}

// the look of a hair style: the hair catalog's text ("Long, curly, headband") where the game's preview pictures were
// classified, else the coarse look of face_looks.h. No code number: that goes to the style's own line / tooltip.
inline std::string hair_text(int64_t v) {
    if (const hair::Style* s = hair::find(v); s && s->classified) return hair::describe(*s);
    static const char* looks[] = {"?", "Bald / buzz cut", "Short hair", "Medium hair", "Long hair", "Tied / braids / dreads"};
    uint8_t l = style_look(faces::looks::kHair, v);
    return l < 6 ? looks[l] : "Hair";
}

inline std::string facial_hair_text(int64_t v) {
    static const char* looks[] = {"Clean-shaven", "Stubble", "Moustache / goatee", "Short beard", "Full beard"};
    uint8_t l = style_look(faces::looks::kFacialHair, v);
    return l < 5 ? looks[l] : "Facial hair";
}

// generic descriptors for appearance item codes the game has hundreds of without names
inline const char* item_noun(const std::string& field) {
    static const std::map<std::string, const char*> m = {
        {"shoetypecode", "Boots"}, {"smallsidedshoetypecode", "Small-sided boots"}, {"gkglovetypecode", "GK gloves"},
        {"headtypecode", "Head type"}, {"eyebrowcode", "Eyebrows"}, {"skintypecode", "Skin type"}, {"hairstylecode", "Hair styling"},
        {"sideburnscode", "Sideburns"}, {"hairpartcode", "Hair parting"}, {"hairlinecode", "Hairline"}, {"hairstateid", "Hair state"},
        {"faceposerpreset", "Face pose"}, {"headvariation", "Head variation"}, {"lipcolor", "Lip colour"}, {"skinsurfacepack", "Skin surface"},
        {"skinmakeup", "Skin makeup"}, {"skincomplexion", "Skin complexion"}, {"animfreekickstartposcode", "Free kick stance"},
        {"animpenaltieskickstylecode", "Penalty kick style"}, {"animpenaltiesmotionstylecode", "Penalty run-up"},
        {"animpenaltiesstartposcode", "Penalty stance"}, {"runningcode1", "Running animation"}, {"runningcode2", "Running animation 2"},
        {"bodytypecode", "Body type"}, {"facialhairtypecode", "Facial hair"}, {"hairtypecode", "Hair"},
        {"tattoohead", "Tattoo"}, {"tattoofront", "Tattoo"}, {"tattooback", "Tattoo"}, {"tattooleftarm", "Tattoo"},
        {"tattoorightarm", "Tattoo"}, {"tattooleftleg", "Tattoo"}, {"tattoorightleg", "Tattoo"},
    };
    auto it = m.find(field);
    return it == m.end() ? nullptr : it->second;
}

// true when the field holds a code that must be shown as text rather than as a bare number
inline bool is_code_field(const std::string& field) {
    if (named_codes(field) || item_noun(field)) return true;
    std::string k = field;
    while (!k.empty() && std::isdigit(static_cast<unsigned char>(k.back()))) k.pop_back();
    if (k.size() > 4 && k.compare(k.size() - 4, 4, "code") == 0) return true;  // eyebrowcode, shoecolorcode1, ...
    return k.rfind("tattoo", 0) == 0;  // tattoo ids per body part
}

// ---- enumerated fields Live Editor's localization does not name (stars, work rates, ...): base = value of labels[0]
struct EnumDef {
    const char* field;
    int64_t base;
    std::vector<const char*> labels;
};
inline const std::vector<EnumDef>& enum_defs() {
    static const std::vector<EnumDef> d = {
        {"weakfootabilitytypecode", 1, {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}},
        {"skillmoves", 0, {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}},
        {"internationalrep", 1, {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}},
        {"attackingworkrate", 0, {"Low", "Medium", "High"}},
        {"defensiveworkrate", 0, {"Low", "Medium", "High"}},
        {"gkkickstyle", 0, {"Default", "Power", "Precision", "Mixed"}},
        {"skillmoveslikelihood", 0, {"Low", "Medium", "High", "Very high"}},
        {"personality", 1, {"Neutral", "Maverick", "Heartbeat", "Virtuoso"}},
        {"undershortstyle", 0, {"None", "Visible"}},
        {"shoedesigncode", 0, {"Standard", "Laced", "Laceless", "High-cut"}},
        {"muscularitycode", 0, {"Regular", "Muscular"}},
        {"runstylecode", 0, {"Default", "Short step", "Long step", "Smooth", "Upright", "Hunched", "Bouncy", "Mixed"}},
        {"growthprofile", 0, {"Default", "Early", "Normal", "Late", "Very late"}},
    };
    return d;
}
inline const EnumDef* enum_def(const std::string& field) {
    for (const auto& d : enum_defs())
        if (field == d.field) return &d;
    return nullptr;
}
// the name of an enumerated value (enum_defs, else Live Editor's named codes); nullptr = no name
inline const char* enum_label(const std::string& field, int64_t v) {
    if (const EnumDef* d = enum_def(field)) {
        int64_t k = v - d->base;
        return k >= 0 && k < static_cast<int64_t>(d->labels.size()) ? d->labels[static_cast<size_t>(k)] : nullptr;
    }
    if (const NamedCodes* t = named_codes(field)) return find_name(*t, v);
    return nullptr;
}

// ---- field titles ("overallrating" -> "Overall")
inline const std::map<std::string, std::string>& title_table() {
    static const std::map<std::string, std::string> m = {
        {"overallrating", "Overall"}, {"potential", "Potential"}, {"modifier", "OVR modifier"}, {"acceleration", "Acceleration"},
        {"sprintspeed", "Sprint Speed"}, {"positioning", "Att. Position"}, {"finishing", "Finishing"},
        {"shotpower", "Shot Power"}, {"longshots", "Long Shots"}, {"volleys", "Volleys"}, {"penalties", "Penalties"},
        {"vision", "Vision"}, {"crossing", "Crossing"}, {"freekickaccuracy", "FK Accuracy"},
        {"shortpassing", "Short Passing"}, {"longpassing", "Long Passing"}, {"curve", "Curve"}, {"agility", "Agility"},
        {"balance", "Balance"}, {"reactions", "Reactions"}, {"ballcontrol", "Ball Control"}, {"dribbling", "Dribbling"},
        {"composure", "Composure"}, {"interceptions", "Interceptions"}, {"headingaccuracy", "Heading Acc."},
        {"defensiveawareness", "Def. Awareness"}, {"marking", "Marking"}, {"standingtackle", "Standing Tackle"},
        {"slidingtackle", "Sliding Tackle"}, {"jumping", "Jumping"}, {"stamina", "Stamina"}, {"strength", "Strength"},
        {"aggression", "Aggression"}, {"gkdiving", "GK Diving"}, {"gkhandling", "GK Handling"},
        {"gkkicking", "GK Kicking"}, {"gkpositioning", "GK Positioning"}, {"gkreflexes", "GK Reflexes"},
        {"preferredfoot", "Preferred Foot"}, {"weakfootabilitytypecode", "Weak Foot"}, {"skillmoves", "Skill Moves"},
        {"attackingworkrate", "Att. Work Rate"}, {"defensiveworkrate", "Def. Work Rate"}, {"height", "Height (cm)"},
        {"weight", "Weight (kg)"}, {"nationality", "Nationality"}, {"birthdate", "Birth Date"},
        {"contractvaliduntil", "Contract Until"}, {"wage", "Wage"}, {"releaseclause", "Release Clause"}, {"isretiring", "Retiring"}, {"playerjointeamdate", "Joined Club"},
        {"internationalrep", "Int. Reputation"}, {"teamname", "Team Name"}, {"jerseynumber", "Jersey"},
        {"headassetid", "Head model"}, {"hashighqualityhead", "Real Face"}, {"headclasscode", "Head type"},
        {"trait1", "PlayStyles"}, {"icontrait1", "PlayStyles+"}, {"trait2", "Traits"}, {"icontrait2", "Traits+"},
        {"homewins", "Home wins"}, {"awaywins", "Away wins"}, {"homedraws", "Home draws"}, {"awaydraws", "Away draws"},
        {"homelosses", "Home losses"}, {"awaylosses", "Away losses"}, {"homegf", "Home goals for"},
        {"awaygf", "Away goals for"}, {"homega", "Home goals against"}, {"awayga", "Away goals against"},
        {"points", "Points"}, {"nummatchesplayed", "Played"}, {"currenttableposition", "Table position"},
        {"teamform", "Form"}, {"lastgameresult", "Last result"},
        {"firstnameid", "First name ID"}, {"lastnameid", "Last name ID"}, {"commonnameid", "Common name ID"},
        {"playerjerseynameid", "Jersey name ID"}, {"bodytypecode", "Body type"}, {"gender", "Gender"},
        {"skillmoveslikelihood", "Skill moves likelihood"}, {"gkkickstyle", "GK kick style"}, {"runstylecode", "Run style"},
        {"socklengthcode", "Sock length"}, {"sockstylecode", "Sock style"}, {"shoetypecode", "Boots"}, {"shoecolorcode1", "Boot colour 1"},
        {"shoecolorcode2", "Boot colour 2"}, {"shoedesigncode", "Boot design"}, {"gkglovetypecode", "GK gloves"},
        {"hairtypecode", "Hair"}, {"haircolorcode", "Hair colour"}, {"hairstylecode", "Hair style"},
        {"facialhairtypecode", "Facial hair"}, {"facialhaircolorcode", "Facial hair colour"}, {"eyecolorcode", "Eye colour"},
        {"skintonecode", "Skin tone"}, {"headtypecode", "Head type"}, {"jerseyfit", "Jersey fit"},
        {"jerseysleevelengthcode", "Sleeves"}, {"jerseystylecode", "Jersey style"}, {"shortstyle", "Shorts"},
        {"growthprofile", "Growth profile"}, {"emotion", "Emotion"}, {"personality", "Personality"},
        // appearance fields that printed as "Headtypecode", "Eyebrowcode" ...
        {"eyebrowcode", "Eyebrows"}, {"sideburnscode", "Sideburns"}, {"hairpartcode", "Hair parting"}, {"hairlinecode", "Hairline"},
        {"hairstateid", "Hair state"}, {"faceposerpreset", "Face pose"}, {"headvariation", "Head variation"}, {"lipcolor", "Lip colour"},
        {"skintypecode", "Skin type"}, {"skinsurfacepack", "Skin surface"}, {"skinmakeup", "Skin makeup"},
        {"skincomplexion", "Skin complexion"}, {"smallsidedshoetypecode", "Small-sided boots"}, {"muscularitycode", "Muscularity"},
        {"undershortstyle", "Under-shorts"}, {"animfreekickstartposcode", "Free kick stance"}, {"animpenaltieskickstylecode", "Penalty kick style"},
        {"animpenaltiesmotionstylecode", "Penalty run-up"}, {"animpenaltiesstartposcode", "Penalty stance"},
        {"runningcode1", "Running animation"}, {"runningcode2", "Running animation 2"}, {"outfitid", "Outfit"},
        {"hasseasonaljersey", "Seasonal jersey"}, {"hasseasonalsock", "Seasonal socks"}, {"hasseasonalshoes", "Seasonal boots"},
        {"jerseynamecolorr", "Jersey name colour (red)"}, {"jerseynamecolorg", "Jersey name colour (green)"},
        {"jerseynamecolorb", "Jersey name colour (blue)"}, {"personalityid", "Personality"},
        {"rivalteam", "Rival club"}, {"cityid", "City"}, {"teamid", "Club"}, {"accessorycode1", "Accessory 1"},
        {"accessorycode2", "Accessory 2"}, {"accessorycode3", "Accessory 3"}, {"accessorycode4", "Accessory 4"},
        {"accessorycolourcode1", "Accessory 1 colour"}, {"accessorycolourcode2", "Accessory 2 colour"},
        {"accessorycolourcode3", "Accessory 3 colour"}, {"accessorycolourcode4", "Accessory 4 colour"},
    };
    return m;
}

// "headtypecode" -> "Head type", "shoecolorcode2" -> "Shoe colour 2": a field with no entry in title_table() is split into
// the words the game's field names are made of; "" when it cannot be split completely
inline std::string humanize(const std::string& field) {
    static const char* words[] = {"smallsided", "animfreekick", "animpenalties", "faceposer", "complexion", "muscularity",
                                  "variation", "accessory", "sideburns", "freekick", "penalties", "facial", "eyebrow", "surface",
                                  "tattoo", "jersey", "running", "makeup", "preset", "sleeve", "length", "colour", "color",
                                  "design", "motion", "gkglove", "glove", "stance", "state", "style", "start", "shoe", "sock",
                                  "skin", "tone", "type", "head", "hair", "line", "part", "kick", "pose", "pack", "pos", "eye",
                                  "lip", "nose", "ear", "body", "code", "id", "anim", "left", "right", "arm", "leg", "neck",
                                  "back", "chest", "hand", "foot", "shoulder", "forearm", "bicep", "tricep", "calf", "thigh", "role", "trait"};
    std::string k = field, digits;
    while (!k.empty() && std::isdigit(static_cast<unsigned char>(k.back()))) {
        digits.insert(digits.begin(), k.back());
        k.pop_back();
    }
    std::string out;
    size_t i = 0;
    while (i < k.size()) {
        size_t best = 0;
        const char* bw = nullptr;
        for (const char* w : words) {
            const size_t n = std::char_traits<char>::length(w);
            if (n > best && k.compare(i, n, w) == 0) {
                best = n;
                bw = w;
            }
        }
        if (!bw) return "";
        std::string w = bw;
        if (w == "color") w = "colour";
        const bool trailing = i + best == k.size();
        if (!((w == "code" || w == "id") && trailing && !out.empty())) {  // "...code" / "...id" say nothing to a person
            if (!out.empty()) out += ' ';
            out += w;
        }
        i += best;
    }
    if (out.empty()) return "";
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return digits.empty() ? out : out + " " + digits;
}

inline std::string field_title(const std::string& field) {
    const auto& m = title_table();
    auto it = m.find(field);
    if (it != m.end()) return it->second;
    std::string h = humanize(field);
    if (!h.empty()) return h;
    std::string out = field;
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// ---------------------------------------------------------------- player-specific codes and brands
// Fields whose codes belong to players (anything but the standard 0, or for the body type the generic ones)
inline bool is_signature_field(const std::string& field) {
    return field == "bodytypecode" || field == "animfreekickstartposcode" || field == "animpenaltiesstartposcode" ||
           field == "runningcode1" || field == "runningcode2";
}

inline const data::Users* find_users(const std::string& field, int64_t v) {
    for (const data::Users& u : data::kUsers)
        if (u.code == v && field == u.field) return &u;
    return nullptr;
}

// Who uses a code, as the loaded database says (for codes appearance_labels_data.h does not know)
struct CodeUsers {
    int players = 0;
    std::string names;  // the best-rated ones, "Lionel Messi, ..."
};

inline std::string users_text(int players, const std::string& names) {
    const int listed = static_cast<int>(std::count(names.begin(), names.end(), ',')) + 1;
    return players > listed ? names + " +" + std::to_string(players - listed) + " more" : names;
}

template <size_t N>
inline const char* brand_of(const data::Brand (&tab)[N], const char* const* names, int64_t v) {
    for (const data::Brand& b : tab)
        if (b.id == v) return names[b.brand];
    return nullptr;
}

// ---------------------------------------------------------------- labels
enum class Kind { Named, Described, Unnamed };

struct Label {
    std::string text;
    Kind kind = Kind::Unnamed;
};

// the fallback for a code nothing names: never a bare number and never the word "code" before it (the UI walk test
// fails on "code N")
inline std::string unnamed_n(int64_t v) { return "Unnamed " + std::to_string(v); }

inline std::string item_n(const std::string& noun, int64_t v) { return noun + " #" + std::to_string(v); }

// Readable text of a code value and where it comes from; `noun` is the fallback descriptor (the field's label) for item
// codes with no names, `live` who uses the code in the loaded database (player-specific codes the data does not know)
inline Label code_label(const std::string& field, int64_t v, const std::string& noun, const CodeUsers* live = nullptr) {
    if (field == "headclasscode") {
        if (v == 0) return {"Real face (own head model)", Kind::Named};
        if (v == 1) return {"Generic head", Kind::Named};
    }
    const NamedCodes* named = named_codes(field);
    if (named)
        if (const char* n = find_name(*named, v)) return {n, Kind::Named};
    if (const NamedCodes* d = described_codes(field))
        if (const char* n = find_name(*d, v)) return {n, Kind::Described};
    if (is_signature_field(field)) {
        const char* item = item_noun(field);
        if (field != "bodytypecode" && v == 0) return {"Standard", Kind::Described};  // most players
        std::string who;
        if (const data::Users* u = find_users(field, v)) who = users_text(u->players, u->names);
        else if (live && live->players > 0 && !live->names.empty()) who = users_text(live->players, live->names);
        if (!who.empty()) return {field == "bodytypecode" ? "Unique: " + who : std::string(item) + " " + std::to_string(v) + ": " + who, Kind::Described};
        return field == "bodytypecode" ? Label{"Player-specific body model " + std::to_string(v), Kind::Unnamed} : Label{item_n(item, v), Kind::Described};
    }
    if (field == "skintonecode" && named) {  // between two of the ten: the nearest one
        const int64_t k = std::clamp<int64_t>((v + 5) / 10, 1, 10) * 10;
        return {"Skin tone " + std::to_string(v) + " (near " + find_name(*named, k) + ")", Kind::Described};
    }
    // accessories are items the game has pictures for (the gallery offers ids Live Editor does not name)
    if (named && table_key(field) == "accessorycode") return {item_n("Accessory", v), Kind::Described};
    if (named) return {unnamed_n(v), Kind::Unnamed};
    if (field == "hairtypecode") return {hair_text(v), Kind::Described};
    if (field == "facialhairtypecode") return {facial_hair_text(v), Kind::Described};
    if (field == "shoetypecode") {
        if (const char* b = brand_of(data::kBoots, data::kBootBrandNames, v)) return {std::string(b) + " boots #" + std::to_string(v), Kind::Described};
        return {item_n("Boots", v), Kind::Described};
    }
    if (field == "gkglovetypecode") {
        if (v == 0) return {"Default", Kind::Described};  // every outfield player
        if (const char* b = brand_of(data::kGloves, data::kGloveBrandNames, v)) return {std::string(b) + " GK gloves #" + std::to_string(v), Kind::Described};
        return {item_n("GK gloves", v), Kind::Described};
    }
    if (field == "headtypecode") {  // the game's head type ranges of 500 (face_filter.cpp kEthnicGroups)
        const std::string g = faces::key_label(faces::kEthnicity, faces::facet_key(faces::kEthnicity, v));
        return {"Head type " + std::to_string(v) + " (" + g + ")", Kind::Described};
    }
    if (field == "eyebrowcode" && v >= 10000) {  // <style> * 10000 + <a> * 100 + <b>: FC 27 has 37 styles, a 0..5, b 0..3
        const int64_t style = v / 10000, a = (v / 100) % 100, b = v % 100;
        if (a == 0 && b == 0) return {"Eyebrows " + std::to_string(style), Kind::Described};
        return {"Eyebrows " + std::to_string(style) + ", variant " + std::to_string(a) + "-" + std::to_string(b), Kind::Described};
    }
    if (v == 0 && table_key(field).rfind("tattoo", 0) == 0) return {"None", Kind::Described};
    const char* n = item_noun(field);
    return {item_n(n ? n : noun, v), Kind::Described};
}

// readable text of a code value; `noun` is the fallback descriptor (the field's label) for codes with no names
inline std::string code_text(const std::string& field, int64_t v, const std::string& noun, const CodeUsers* live = nullptr) {
    return code_label(field, v, noun, live).text;
}

// The codes this file knows for a field besides Live Editor's names: described hair colours, player-specific codes of
// the database, boot and glove ids with a brand
inline std::vector<int64_t> known_codes(const std::string& field) {
    std::vector<int64_t> out;
    if (const NamedCodes* d = described_codes(field))
        for (const auto& e : *d) out.push_back(e.first);
    for (const data::Users& u : data::kUsers)
        if (field == u.field) out.push_back(u.code);
    if (field == "shoetypecode")
        for (const data::Brand& b : data::kBoots) out.push_back(b.id);
    if (field == "gkglovetypecode")
        for (const data::Brand& b : data::kGloves) out.push_back(b.id);
    if (is_signature_field(field) && field != "bodytypecode") out.push_back(0);
    return out;
}

// The codes a combo offers: Live Editor's named ones in their order, then by code the ones this file knows, `more` (the
// codes the loaded table uses, the game's pictures) and the current value, which is always offered
inline std::vector<int64_t> offered_codes(const std::string& field, const std::vector<int64_t>& more, int64_t current) {
    std::vector<int64_t> out;
    std::set<int64_t> seen;
    if (const NamedCodes* t = named_codes(field))
        for (const auto& e : *t)
            if (seen.insert(e.first).second) out.push_back(e.first);
    std::vector<int64_t> rest = known_codes(field);
    rest.insert(rest.end(), more.begin(), more.end());
    rest.push_back(current);
    std::sort(rest.begin(), rest.end());
    for (int64_t v : rest)
        if (seen.insert(v).second) out.push_back(v);
    return out;
}

// true when a field's combo should list its descriptors alphabetically (it has names: roles, colours, weather...);
// item catalogues ("Boots #1432") stay in code order
inline bool has_descriptors(const std::string& field) { return enum_def(field) != nullptr || named_codes(field) != nullptr; }

// ---------------------------------------------------------------- positions
// The game's 28 position codes, in code order. A copy of core/model.cpp kPositions (this header stays free of the model;
// test_describe.h checks that the two tables agree). FC 27's own screens name twelve of them: the roles (players.role1..9,
// "role" above) exist for GK RB LB CB CDM CM RM LM CAM RW LW ST only. The other sixteen (SW, RWB, LWB, RCB/LCB, RDM/LDM,
// RCM/LCM, RAM/LAM, RF/CF/LF, RS/LS) appear in formations and preferredposition2..7 and no game menu names them: they read
// "RCB (internal, plays as CB)" when the pitch lays them out as a visible position (core/tactics_db.h), "SW (internal)" otherwise.
inline const char* position_code_name(int64_t v) {
    static const char* const k[] = {"GK",  "SW",  "RWB", "RB",  "RCB", "CB", "LCB", "LB", "LWB", "RDM", "CDM", "LDM", "RM",  "RCM",
                                    "CM",  "LCM", "LM",  "RAM", "CAM", "LAM", "RF", "CF", "LF",  "RW",  "RS",  "ST",  "LS",  "LW"};
    return v >= 0 && v < static_cast<int64_t>(sizeof(k) / sizeof(k[0])) ? k[v] : nullptr;
}
inline int position_name_count() { return 28; }
inline bool position_visible_code(int64_t v) {
    for (int64_t c : {0, 3, 7, 5, 10, 14, 12, 16, 18, 23, 27, 25})  // GK RB LB CB CDM CM RM LM CAM RW LW ST
        if (c == v) return true;
    return false;
}
// "ST" for a visible position, "RCB (internal, plays as CB)" / "SW (internal)" for an internal code, "None" for -1 (an empty
// preferredposition2..7 slot), "Unknown position N" outside the table. Never a bare number.
inline std::string position_text(int64_t v) {
    if (v < 0) return "None";
    const char* n = position_code_name(v);
    if (!n) return "Unknown position " + std::to_string(v);
    if (position_visible_code(v)) return n;
    const std::string s = n;
    const char* as = nullptr;
    if (s == "RCB" || s == "LCB") as = "CB";
    else if (s == "RDM" || s == "LDM") as = "CDM";
    else if (s == "RCM" || s == "LCM") as = "CM";
    else if (s == "RAM" || s == "LAM") as = "CAM";
    else if (s == "RS" || s == "LS") as = "ST";
    return as ? s + " (internal, plays as " + as + ")" : s + " (internal)";
}
// true for a field that stores a position code: preferredposition1..7 of the player tables (players, smplayers, cmplayers,
// trainingteamplayers, temp_players, career_scoutmission) and the eleven slots position0..10 of the formation and tactic
// tables. Not "positioning" (an attribute) and not teamplayerlinks.position (a squad slot index).
inline bool is_position_field(const std::string& table, const std::string& field) {
    if (field.rfind("preferredposition", 0) == 0) return true;
    if (field.size() < 9 || field.size() > 10 || field.compare(0, 8, "position") != 0) return false;
    for (size_t i = 8; i < field.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(field[i]))) return false;
    for (const char* t : {"formations", "customformations", "temp_formations", "mentalities", "cm_mentalities", "default_mentalities",
                          "cm_default_mentalities", "defaultteamdata"})
        if (table == t) return true;
    return false;
}

// One value of one field in words: the name where the game or Live Editor has one, "Unnamed N" / "Boots style 1432 (no name)"
// where it has not, the plain number for a number. `table` is the table the field is in ("players", "managers", ...).
// Values that need the database (a nation or club name, a date) are described by App::describe_value, which falls back to this.
inline std::string describe(const std::string& table, const std::string& field, int64_t v) {
    if (is_position_field(table, field)) return position_text(v);  // preferredposition1..7, formation slots: GK, ST, RCB (internal, plays as CB)
    if (enum_def(field)) {
        if (const char* l = enum_label(field, v)) return l;
        return unnamed_n(v);
    }
    if (is_code_field(field)) return code_text(field, v, field_title(field));  // also every field with a named table
    if (field == "personalityid") return "Personality type " + std::to_string(v);
    return std::to_string(v);
}

// "Overall (0-99)" for a tooltip: the field's title and its range
inline std::string describe_range(const std::string& field, int64_t lo, int64_t hi) {
    return field_title(field) + " (" + std::to_string(lo) + "-" + std::to_string(hi) + ")";
}

// "Overall changed 84 -> 87 (Player X)"; before / after are the texts describe() gave
inline std::string describe_change(const std::string& field, const std::string& before, const std::string& after,
                                   const std::string& subject = std::string()) {
    std::string s = field_title(field) + " changed " + before + " -> " + after;
    if (!subject.empty()) s += " (" + subject + ")";
    return s;
}

}  // namespace turbo::labels
