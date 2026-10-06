// FC 27 LE Turbo GUI - readable text for code-valued player fields (roles, body type, emotion, colours, accessories,
// hair styles, boots...). Platform independent, header only (head type groups: face_filter.cpp; hair: hair_catalog.cpp).
// Where the text comes from:
//   * Named: Live Editor's own localization (loc/eng_us/localize.json: player_role_N, bodytype_N, emotion_N,
//     skintonecode_N, haircolor_N, eyecolor_N, accessorycode_N, accessorycolourcode_N, shoecolorcode1_N,
//     hasseasonaljersey_N, jersey*/sock*/shortstyle_N).
//   * Described: what the data says about a code Live Editor does not name. appearance_labels_data.h
//     (scripts/gen_appearance_labels.py) has who uses each player-specific body type, stance and running animation in
//     the FC 27 database ("Unique: Lionel Messi") and the brand of each boot and GK glove id; hair colours 15..27 are
//     named after the colour of each code's preview; hair styles by the hair catalog, facial hair by their look
//     (face_looks.h), head types by their group (face_filter.h), eyebrows by their parts.
//   * Code: nothing describes it: "Code N" (never an empty text, never a bare number).
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

namespace turbo::labels {

using NamedCodes = std::vector<std::pair<int64_t, const char*>>;

inline const std::map<std::string, NamedCodes>& named_tables() {
    static const std::map<std::string, NamedCodes> m = {
    {"role", {{0, "None"}, {1, "GK Goalkeeper+"}, {2, "GK Sweeper Keeper +"}, {3, "RB Fullback+"}, {4, "RB Falseback+"}, {5, "RB Wingback+"}, {6, "RB Attacking Wingback+"}, {7, "LB Fullback+"}, {8, "LB Falseback+"}, {9, "LB Wingback+"}, {10, "LB Attacking Wingback+"}, {11, "CB Defender+"}, {12, "CB Stopper+"}, {13, "CB Ball-Playing Defender+"}, {14, "CDM Holding+"}, {15, "CDM Centre Half+"}, {16, "CDM Deep-Lying Playmaker+"}, {17, "CDM Wide Half+"}, {18, "CM Box-to-Box+"}, {19, "CM Holding+"}, {20, "CM Deep-Lying Playmaker+"}, {21, "CM Playmaker+"}, {22, "CM Half-Winger+"}, {23, "RM Winger+"}, {24, "RM Wide Midfielder+"}, {25, "RM Wide Playmaker+"}, {26, "RM Inside Forward+"}, {27, "LM Winger+"}, {28, "LM Wide Midfielder+"}, {29, "LM Wide Playmaker+"}, {30, "LM Inside Forward+"}, {31, "CAM Playmaker+"}, {32, "CAM Shadow Striker+"}, {33, "CAM Half Winger+"}, {34, "CAM Classic 10+"}, {35, "RW Winger+"}, {36, "RW Inside Forward+"}, {37, "RW Wide Playmaker+"}, {38, "LW Winger+"}, {39, "LW Inside Forward+"}, {40, "LW Wide Playmaker+"}, {41, "ST Advanced Forward+"}, {42, "ST Poacher+"}, {43, "ST False 9+"}, {44, "ST Target Forward+"}, {45, "GK Ball-Playing Keeper+"}, {46, "RB Inverted Wingback+"}, {47, "LB Inverted Wingback+"}, {48, "CB Wide Back+"}, {49, "CDM Box Crasher+"}, {50, "ST Roaming Striker+"}, {51, "RW False Winger+"}, {52, "LW False Winger+"}, {101, "GK Goalkeeper++"}, {102, "GK Sweeper Keeper ++"}, {103, "RB Fullback++"}, {104, "RB Falseback++"}, {105, "RB Wingback++"}, {106, "RB Attacking Wingback++"}, {107, "LB Fullback++"}, {108, "LB Falseback++"}, {109, "LB Wingback++"}, {110, "LB Attacking Wingback++"}, {111, "CB Defender++"}, {112, "CB Stopper++"}, {113, "CB Ball-Playing Defender++"}, {114, "CDM Holding++"}, {115, "CDM Centre Half++"}, {116, "CDM Deep-Lying Playmaker++"}, {117, "CDM Wide Half++"}, {118, "CM Box-to-Box++"}, {119, "CM Holding++"}, {120, "CM Deep-Lying Playmaker++"}, {121, "CM Playmaker++"}, {122, "CM Half-Winger++"}, {123, "RM Winger++"}, {124, "RM Wide Midfielder++"}, {125, "RM Wide Playmaker++"}, {126, "RM Inside Forward++"}, {127, "LM Winger++"}, {128, "LM Wide Midfielder++"}, {129, "LM Wide Playmaker++"}, {130, "LM Inside Forward++"}, {131, "CAM Playmaker++"}, {132, "CAM Shadow Striker++"}, {133, "CAM Half Winger++"}, {134, "CAM Classic 10++"}, {135, "RW Winger++"}, {136, "RW Inside Forward++"}, {137, "RW Wide Playmaker++"}, {138, "LW Winger++"}, {139, "LW Inside Forward++"}, {140, "LW Wide Playmaker++"}, {141, "ST Advanced Forward++"}, {142, "ST Poacher++"}, {143, "ST False 9++"}, {144, "ST Target Forward++"}, {145, "GK Ball-Playing Keeper++"}, {146, "RB Inverted Wingback++"}, {147, "LB Inverted Wingback++"}, {148, "CB Wide Back++"}, {149, "CDM Box Crasher++"}, {150, "ST Roaming Striker++"}, {151, "RW False Winger++"}, {152, "LW False Winger++"}}},
    // the generic body types; the player-specific ones (10, 13, 16, 251...) read "Unique: <who uses it>" (code_label)
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

// a hair style: the hair catalog's look from the game's preview pictures ("Long, curly, headband #233"), else the
// face_looks.h look class of the nearest listed style
inline std::string hair_text(int64_t v) {
    char b[96];
    if (const hair::Style* s = hair::find(v); s && s->classified) {
        std::snprintf(b, sizeof(b), "%s #%lld", hair::describe(*s).c_str(), static_cast<long long>(v));
        return b;
    }
    static const char* looks[] = {"?", "Bald / buzz cut", "Short hair", "Medium hair", "Long hair", "Tied / braids / dreads"};
    uint8_t l = style_look(faces::looks::kHair, v);
    std::snprintf(b, sizeof(b), "%s #%lld", l < 6 && l > 0 ? looks[l] : "Hair", static_cast<long long>(v));
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

// Who uses a code, as the loaded database says (widgets.cpp reads it for codes appearance_labels_data.h does not know)
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
enum class Kind { Named, Described, Code };

struct Label {
    std::string text;
    Kind kind = Kind::Code;
};

inline std::string code_n(int64_t v) { return "Code " + std::to_string(v); }

inline std::string item_n(const std::string& noun, int64_t v) { return noun + " #" + std::to_string(v); }

// Readable text of a code value and where it comes from; `noun` is the fallback descriptor (the field's label) for item
// codes with no names, `live` who uses the code in the loaded database (player-specific codes the data does not know)
inline Label code_label(const std::string& field, int64_t v, const std::string& noun, const CodeUsers* live = nullptr) {
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
        return field == "bodytypecode" ? Label{code_n(v), Kind::Code} : Label{item_n(item, v), Kind::Described};
    }
    if (field == "skintonecode") {  // between two of the ten: the nearest one
        const int64_t k = std::clamp<int64_t>((v + 5) / 10, 1, 10) * 10;
        return {"Skin tone " + std::to_string(v) + " (near " + find_name(*named, k) + ")", Kind::Described};
    }
    // accessories are items the game has pictures for (the gallery offers ids Live Editor does not name)
    if (named && table_key(field) == "accessorycode") return {item_n("Accessory", v), Kind::Described};
    if (named) return {code_n(v), Kind::Code};
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

}  // namespace turbo::labels
