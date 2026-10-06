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

inline std::string hair_text(int64_t v) {
    static const char* looks[] = {"?", "Bald / buzz cut", "Short hair", "Medium hair", "Long hair", "Tied / braids / dreads"};
    uint8_t l = style_look(faces::looks::kHair, v);
    char b[64];
    std::snprintf(b, sizeof(b), "%s #%lld", l < 6 ? looks[l] : "Hair", static_cast<long long>(v));
    return b;
}

inline std::string facial_hair_text(int64_t v) {
    static const char* looks[] = {"Clean-shaven", "Stubble", "Moustache / goatee", "Short beard", "Full beard"};
    uint8_t l = style_look(faces::looks::kFacialHair, v);
    char b[64];
    std::snprintf(b, sizeof(b), "%s #%lld", l < 5 ? looks[l] : "Facial hair", static_cast<long long>(v));
    return b;
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

// readable text of a code value; `noun` is the fallback descriptor (the field's label) for codes with no names
inline std::string code_text(const std::string& field, int64_t v, const std::string& noun) {
    if (const NamedCodes* t = named_codes(field)) {
        if (const char* n = find_name(*t, v)) return n;
        char b[48];
        std::snprintf(b, sizeof(b), "Unknown (code %lld)", static_cast<long long>(v));
        return b;
    }
    if (field == "hairtypecode") return hair_text(v);
    if (field == "facialhairtypecode") return facial_hair_text(v);
    const char* n = item_noun(field);
    char b[96];
    std::snprintf(b, sizeof(b), "%s #%lld", n ? n : noun.c_str(), static_cast<long long>(v));
    return b;
}

}  // namespace turbo::labels
