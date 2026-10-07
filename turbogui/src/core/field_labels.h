// FC 27 LE Turbo GUI - readable text for code-valued player fields (roles, body type, emotion, colours, accessories,
// hair styles...). Platform independent, header only. Names come from Live Editor's own localization
// (loc/eng_us/localize.json: player_role_N, bodytype_N, emotion_N, skintonecode_N, haircolor_N, facialhaircolor_N,
// eyecolor_N, accessorycode_N, accessorycolourcode_N, jersey*/sock*/shortstyle_N); hair and facial hair styles get
// their look class from face_looks.h. A code with no known name reads "Unknown (code N)".
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "face_looks.h"
#include "hair_catalog.h"

namespace turbo::labels {

using NamedCodes = std::vector<std::pair<int64_t, const char*>>;

inline const std::map<std::string, NamedCodes>& named_tables() {
    static const std::map<std::string, NamedCodes> m = {
    {"role", {{0, "None"}, {1, "GK Goalkeeper+"}, {2, "GK Sweeper Keeper +"}, {3, "RB Fullback+"}, {4, "RB Falseback+"}, {5, "RB Wingback+"}, {6, "RB Attacking Wingback+"}, {7, "LB Fullback+"}, {8, "LB Falseback+"}, {9, "LB Wingback+"}, {10, "LB Attacking Wingback+"}, {11, "CB Defender+"}, {12, "CB Stopper+"}, {13, "CB Ball-Playing Defender+"}, {14, "CDM Holding+"}, {15, "CDM Centre Half+"}, {16, "CDM Deep-Lying Playmaker+"}, {17, "CDM Wide Half+"}, {18, "CM Box-to-Box+"}, {19, "CM Holding+"}, {20, "CM Deep-Lying Playmaker+"}, {21, "CM Playmaker+"}, {22, "CM Half-Winger+"}, {23, "RM Winger+"}, {24, "RM Wide Midfielder+"}, {25, "RM Wide Playmaker+"}, {26, "RM Inside Forward+"}, {27, "LM Winger+"}, {28, "LM Wide Midfielder+"}, {29, "LM Wide Playmaker+"}, {30, "LM Inside Forward+"}, {31, "CAM Playmaker+"}, {32, "CAM Shadow Striker+"}, {33, "CAM Half Winger+"}, {34, "CAM Classic 10+"}, {35, "RW Winger+"}, {36, "RW Inside Forward+"}, {37, "RW Wide Playmaker+"}, {38, "LW Winger+"}, {39, "LW Inside Forward+"}, {40, "LW Wide Playmaker+"}, {41, "ST Advanced Forward+"}, {42, "ST Poacher+"}, {43, "ST False 9+"}, {44, "ST Target Forward+"}, {45, "GK Ball-Playing Keeper+"}, {46, "RB Inverted Wingback+"}, {47, "LB Inverted Wingback+"}, {48, "CB Wide Back+"}, {49, "CDM Box Crasher+"}, {50, "ST Roaming Striker+"}, {51, "RW False Winger+"}, {52, "LW False Winger+"}, {101, "GK Goalkeeper++"}, {102, "GK Sweeper Keeper ++"}, {103, "RB Fullback++"}, {104, "RB Falseback++"}, {105, "RB Wingback++"}, {106, "RB Attacking Wingback++"}, {107, "LB Fullback++"}, {108, "LB Falseback++"}, {109, "LB Wingback++"}, {110, "LB Attacking Wingback++"}, {111, "CB Defender++"}, {112, "CB Stopper++"}, {113, "CB Ball-Playing Defender++"}, {114, "CDM Holding++"}, {115, "CDM Centre Half++"}, {116, "CDM Deep-Lying Playmaker++"}, {117, "CDM Wide Half++"}, {118, "CM Box-to-Box++"}, {119, "CM Holding++"}, {120, "CM Deep-Lying Playmaker++"}, {121, "CM Playmaker++"}, {122, "CM Half-Winger++"}, {123, "RM Winger++"}, {124, "RM Wide Midfielder++"}, {125, "RM Wide Playmaker++"}, {126, "RM Inside Forward++"}, {127, "LM Winger++"}, {128, "LM Wide Midfielder++"}, {129, "LM Wide Playmaker++"}, {130, "LM Inside Forward++"}, {131, "CAM Playmaker++"}, {132, "CAM Shadow Striker++"}, {133, "CAM Half Winger++"}, {134, "CAM Classic 10++"}, {135, "RW Winger++"}, {136, "RW Inside Forward++"}, {137, "RW Wide Playmaker++"}, {138, "LW Winger++"}, {139, "LW Inside Forward++"}, {140, "LW Wide Playmaker++"}, {141, "ST Advanced Forward++"}, {142, "ST Poacher++"}, {143, "ST False 9++"}, {144, "ST Target Forward++"}, {145, "GK Ball-Playing Keeper++"}, {146, "RB Inverted Wingback++"}, {147, "LB Inverted Wingback++"}, {148, "CB Wide Back++"}, {149, "CDM Box Crasher++"}, {150, "ST Roaming Striker++"}, {151, "RW False Winger++"}, {152, "LW False Winger++"}}},
    {"bodytypecode", {{1, "Average and Lean"}, {2, "Average and Normal"}, {3, "Average and Stocky"}, {4, "Tall and Lean"}, {5, "Tall and Normal"}, {6, "Tall and Stocky"}, {7, "Short and Lean"}, {8, "Short and Normal"}, {9, "Short and Stocky"}, {11, "Very Tall and Lean"}}},
    {"emotion", {{1, "Ice Cold"}, {2, "Calm"}, {3, "Average"}, {4, "Hot Temper"}, {5, "Volcano"}}},
    {"skintonecode", {{1, "Caucasian 1"}, {2, "Caucasian 2"}, {3, "Caucasian 3"}, {4, "Latin Asian 1"}, {5, "Latin Asian 2"}, {6, "Latin Asian 3"}, {7, "Latin Asian 4"}, {8, "African 1"}, {9, "African 2"}, {10, "African 3"}}},
    {"haircolorcode", {{0, "Black"}, {1, "Blonde"}, {2, "Dirty Blonde"}, {3, "Dark Brown"}, {4, "Light Blonde"}, {5, "Light Brown"}, {6, "Medium Brown"}, {7, "Red"}, {8, "White"}, {9, "Silver"}, {10, "Green"}, {11, "Blue"}, {12, "Ginger"}, {13, "Dark Red"}, {14, "Pink"}}},
    {"facialhaircolorcode", {{0, "Black"}, {1, "Blonde"}, {2, "Medium Brown"}, {3, "Dark Brown"}, {4, "Red"}}},
    {"eyecolorcode", {{1, "Blue"}, {2, "Light Blue"}, {3, "Brown"}, {4, "Light Brown"}, {5, "Hazel"}, {6, "Green"}, {7, "Light Green"}, {8, "Medium Blue"}, {9, "Dark Brown"}, {10, "Saturated Green"}}},
    {"accessorycode", {{0, "None"}, {2, "Hearphone"}, {4, "Left Watch"}, {5, "Right Watch"}, {6, "Left Hand Tape"}, {7, "Right Hand Tape"}, {8, "Left Wristle Tape"}, {9, "Right Wristle Tape"}, {10, "Left Knee Tape"}, {11, "Right Knee Tape"}, {12, "Left Knee Tutor"}, {13, "Right Knee Tutor"}, {14, "Ankle Tape"}, {16, "Gloves"}, {22, "Left Finger Tape"}, {23, "Right Finger Tape"}, {24, "Left Wide Wristle Tape"}, {25, "Right Wide Wristle Tape"}, {26, "Left Bracelet"}, {27, "Right Bracelet"}, {28, "Left Yellow Card"}, {29, "Right Yellow Card"}, {30, "Left Red Card"}, {31, "Right Red Card"}, {32, "Left Ref Pen"}, {33, "Right Ref Pen"}, {34, "Left Ref Book"}, {35, "Right Ref Book"}, {36, "Left Vanishing Spray"}, {37, "Right Vanishing Spray"}, {38, "Left Cellphone"}, {39, "Right Cellphone"}, {150, "Facemask_150"}, {151, "Facemask_151"}, {154, "Facemask_154"}, {156, "Osimhen Facemask"}, {158, "Facemask_158"}, {160, "Nose_attribute_160"}, {164, "Facemask_154"}, {190, "Knee brace"}, {200, "Gloves"}, {240, "Fitted Suit Tie"}}},
    {"accessorycolourcode", {{0, "White"}, {1, "Black"}, {2, "Blue"}, {3, "Red"}, {4, "Yellow"}, {5, "Dark Green"}, {6, "Orange"}, {7, "Violet"}, {8, "Brown"}, {9, "Pink"}, {10, "Bordeaux"}, {11, "Cyan"}, {12, "Dark Blue"}, {99, "None"}}},
    {"jerseysleevelengthcode", {{0, "Short"}, {1, "Long"}, {2, "Long and Turtleneck"}, {3, "Seasonal Undershirt"}, {4, "Seasonal Undershirt and Turtleneck"}}},
    {"jerseyfit", {{0, "Normal"}, {1, "Tight"}, {2, "Team Kit Fit"}}},
    {"jerseystylecode", {{0, "Tucked In"}, {1, "Untucked"}}},
    {"socklengthcode", {{0, "Medium"}, {1, "Short"}, {2, "Long"}, {3, "Low"}}},
    {"sockstylecode", {{0, "No Shin Pad"}, {1, "Regular"}, {2, "Small Shin Pad"}, {3, "Micro Shin Pad"}, {5, "One Hole"}, {6, "Two Holes"}, {7, "Three Holes"}, {8, "Multi Holes"}}},
    {"shortstyle", {{0, "GK Shorts"}, {1, "GK Pants"}}},
    {"preferredfoot", {{1, "Right"}, {2, "Left"}}},
    {"gender", {{0, "Male"}, {1, "Female"}}},
    {"headclasscode", {{0, "Specific"}, {1, "Generic"}}},
    };
    return m;
}

// the label table a field uses: role1..role9 share "role", accessorycode1..4 share "accessorycode", ...
inline std::string table_key(const std::string& field) {
    std::string k = field;
    while (!k.empty() && std::isdigit(static_cast<unsigned char>(k.back()))) k.pop_back();
    if (k == "role" || k == "accessorycode" || k == "accessorycolourcode") return k;
    return field;
}

// the named codes of a field (nullptr when Live Editor names none)
inline const NamedCodes* named_codes(const std::string& field) {
    const auto& m = named_tables();
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
        {"headtypecode", "Head"}, {"eyebrowcode", "Eyebrows"}, {"skintypecode", "Skin type"}, {"shoecolorcode1", "Boot colour"},
        {"shoecolorcode2", "Boot colour 2"}, {"hairstylecode", "Hair styling"}, {"sideburnscode", "Sideburns"},
        {"hairpartcode", "Hair parting"}, {"hairlinecode", "Hairline"}, {"hairstateid", "Hair state"}, {"faceposerpreset", "Face pose"},
        {"headvariation", "Head variation"}, {"lipcolor", "Lip colour"}, {"skinsurfacepack", "Skin surface"},
        {"skinmakeup", "Skin makeup"}, {"skincomplexion", "Skin complexion"}, {"animfreekickstartposcode", "Free kick stance"},
        {"animpenaltieskickstylecode", "Penalty kick style"}, {"animpenaltiesmotionstylecode", "Penalty run-up"},
        {"animpenaltiesstartposcode", "Penalty stance"}, {"runningcode1", "Running animation"}, {"runningcode2", "Running animation 2"},
        {"bodytypecode", "Body type"}, {"facialhairtypecode", "Facial hair"}, {"hairtypecode", "Hair"},
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

// ---- values
// the descriptor of an item catalogue the game has hundreds of unnamed entries of ("Boots"), nullptr for the codes that
// already read as "Boot colour 12" and so need no "style"
inline const char* item_style_noun(const std::string& field) {
    const char* n = item_noun(field);
    if (!n) return nullptr;
    const std::string s = n;
    for (const char* tail : {"colour", "colour 2", "animation", "animation 2", "stance", "state", "pose", "variation", "surface",
                             "makeup", "complexion", "parting", "kick style", "run-up", "type"}) {
        const size_t l = std::char_traits<char>::length(tail);
        if (s.size() >= l && s.compare(s.size() - l, std::string::npos, tail) == 0) return nullptr;
    }
    return n;
}

// readable text of a code value; `noun` is the fallback descriptor (the field's label) for codes with no names
inline std::string code_text(const std::string& field, int64_t v, const std::string& noun) {
    char b[160];
    const long long n = static_cast<long long>(v);
    if (field == "headclasscode") {
        if (v == 0) return "Real face (own head model)";
        if (v == 1) return "Generic head";
    }
    if (const NamedCodes* t = named_codes(field)) {
        if (const char* nm = find_name(*t, v)) return nm;
        if (table_key(field) == "bodytypecode") std::snprintf(b, sizeof(b), "Player-specific body model %lld", n);
        else std::snprintf(b, sizeof(b), "Unnamed %lld", n);
        return b;
    }
    if (field == "hairtypecode") return hair_text(v);
    if (field == "facialhairtypecode") return facial_hair_text(v);
    const char* nn = item_noun(field);
    if (const char* sn = item_style_noun(field)) std::snprintf(b, sizeof(b), "%s style %lld (no name)", sn, n);
    else std::snprintf(b, sizeof(b), "%s %lld (no name)", nn ? nn : noun.c_str(), n);
    return b;
}

// One value of one field in words: the name where the game or Live Editor has one, "Unnamed N" / "Boots style 1432 (no name)"
// where it has not, the plain number for a number. `table` is the table the field is in ("players", "managers", ...).
// Values that need the database (a nation or club name, a date) are described by App::describe_value, which falls back to this.
inline std::string describe(const std::string& table, const std::string& field, int64_t v) {
    (void)table;
    if (enum_def(field)) {
        if (const char* l = enum_label(field, v)) return l;
        return "Unnamed " + std::to_string(v);
    }
    if (is_code_field(field)) return code_text(field, v, field_title(field));
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
