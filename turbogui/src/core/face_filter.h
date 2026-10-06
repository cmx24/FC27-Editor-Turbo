// FC 27 LE Turbo GUI - filters of the real-face chooser (players and managers): ethnicity, skin tone, hair colour,
// hair, facial hair, facial hair colour, eye colour and gender of each head, with readable labels, counts and sort orders.
// Skin tone, hair colour, facial hair colour and eye colour filter on the game's exact codes (Live Editor's names); the
// hair and facial hair styles are grouped into kMaxBuckets looks from the game's own previews (face_looks.h); ethnicity
// groups the head type ranges.
// Platform independent (no ImGui): the chooser in ui_faces.cpp draws it, the native tests check it.
//
// Fields (players and manager tables, FC 27): headtypecode, skintonecode, haircolorcode, hairtypecode,
// facialhairtypecode, facialhaircolorcode, eyecolorcode, gender (0 male, 1 female). FC 27 has no ethnicity field on players or managers:
// "Ethnicity" groups the head type (headtypecode) in the game's ranges of 500 (see kEthnicGroups in face_filter.cpp).
// Labels: Live Editor's own (loc/eng_us/localize.json: skintonecode_1..10, haircolor_0..14, eyecolor_1..10).
//
// 3D looks (core/face3d_looks.h): for real faces the game takes hair, beard, skin and eyes from the face scan, so the codes
// above do not describe the 3D head the chooser shows. Player heads with a 3D look (set_look) carry the 3D facets: Skin
// tone, Hair colour, Hair length, Hair type, Facial hair, Beard colour, Headwear, judged from the game's 3D render; a head
// without a look yet has the value kUnclassified ("not classified yet") in each of them, so a filter on a look value never
// matches it. When the list has 3D looks the chooser shows the 3D facets, Gender, and Eyes / Ethnicity "(database)" last;
// manager heads (no 3D looks) keep the database facets.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/face3d_looks.h"

namespace turbo::faces {

// Database facets (kEthnicity .. kGender), then the 3D facets (kSkin3d .. kHeadwear3d, in face3d::Trait order)
enum Facet {
    kEthnicity, kSkin, kHairColour, kHair, kBeard, kBeardColour, kEyes, kGender,
    kSkin3d, kHairColour3d, kHairLength3d, kHairType3d, kFacialHair3d, kBeardColour3d, kHeadwear3d, kFacetCount
};
constexpr int kDbFacetCount = kGender + 1;  // the database facets (what manager heads have)
constexpr int kMaxBuckets = 5;         // looks per style menu (hair, facial hair), ethnicity groups, sort menu entries
                                       // (the 3D facets are exact: every value of the look is offered, up to face3d::kMaxValues)

constexpr int64_t kAny = INT64_MIN;    // no filter on that facet
constexpr int64_t kNoValue = -1;       // the table has no such field (the head never matches a filter on it)
constexpr int64_t kUnclassified = 255;  // 3D facets: the head has no 3D look yet ("not classified yet"; sorts last)
constexpr size_t kOnly3dDefaultMin = 200;  // "Only heads with a 3D look" starts on when the list has this many looks
constexpr size_t kNoSample = static_cast<size_t>(-1);  // Count::sample of a value no head has

struct Face {
    int64_t id = 0;           // playerid, or managerid for a manager head
    int64_t headassetid = 0;
    bool manager = false;     // a manager's head (heads_staff miniface)
    bool real = false;        // real face: headclasscode 0 (and hashighqualityhead 1 for players)
    std::string name, lname;  // lname: lower case, for the search box
    int overall = 0;          // players: overall rating (sort)
    int64_t raw[kFacetCount];  // field values (database facets), look values or kUnclassified (3D facets); kNoValue: none
    const face3d::Look* look = nullptr;  // the head's 3D look (players only; set_look)
    Face() {
        for (auto& r : raw) r = kNoValue;
    }
};

// Gives a player head its 3D look (nullptr: not classified yet): the 3D facets take the look's values or kUnclassified
void set_look(Face& f, const face3d::Look* look);
// Heads of the list with a 3D look; the chooser shows the 3D facets when there is at least one
size_t count_looks(const std::vector<Face>& faces);

const char* facet_title(Facet f);   // "Ethnicity", "Skin tone", ..., "Gender"; 3D facets: "Skin tone", ..., "Headwear"
// Title in the chooser: with 3D looks the database facets left (eyes, ethnicity) say so: "Eyes (database)"
const char* facet_title(Facet f, bool looks3d);
bool facet_3d(Facet f);             // kSkin3d .. kHeadwear3d
const char* facet_field(Facet f);   // the database field: "headtypecode", "skintonecode", ...
// The filter key of a field value: skin tone, hair / facial hair / eye colour: the exact code; ethnicity and the styles:
// their group (1 .. kMaxBuckets; facial hair: 0 clean-shaven .. 4 full beard); gender: the value
int64_t facet_key(Facet f, int64_t raw);
// true when the facet filters on the game's exact codes (skin tone, hair colour, facial hair colour, eye colour)
bool facet_exact(Facet f);
// Readable label of a key: "Caucasian 2", "Dark Brown", "Hazel", "Tied, braids & dreads", "Clean-shaven", "Female", ...
std::string key_label(Facet f, int64_t key);
// A head's own value for its tooltip: the label and the code ("Dark Brown (code 3)", "Short beard (style 250)")
std::string trait_label(Facet f, int64_t raw);
// The order the chooser shows the filter buttons in (gender first): database facets, or with 3D looks Gender, the 7 3D
// facets, Eyes and Ethnicity; facet_order_size() entries
const Facet* facet_order(bool looks3d = false);
int facet_order_size(bool looks3d = false);
bool facet_shown(Facet f, bool looks3d);
// true when the facet is a style with a game preview picture (hair, facial hair)
bool facet_has_pictures(Facet f);
// Game preview picture of a hair / facial hair style code (legacy path), "" for other facets or code <= 0
std::string facet_picture(Facet f, int64_t raw);
// true when a hair / facial hair style code is in the looks table (face_looks.h: the game has a preview of it); codes
// not listed take the look of the nearest listed one
bool style_listed(Facet f, int64_t raw);

struct Filter {
    int64_t sel[kFacetCount];
    Filter() { clear(); }
    void clear() { for (auto& s : sel) s = kAny; }
    bool active() const {
        for (auto s : sel) if (s != kAny) return true;
        return false;
    }
};

// Head passes the filter (facet `skip` ignored: counts per facet are taken over the other filters)
bool matches(const Face& f, const Filter& flt, int skip = -1);
// Clears the filters of facets the chooser does not show (so a hidden facet never narrows the list)
void drop_hidden(Filter& flt, bool looks3d);
// The head's traits for its tooltip, one "\n"-led line each: with 3D looks "3D look: light skin, ..." (or "not classified
// yet") then Gender, Eyes (database), Ethnicity (database); else every database facet the head has
std::string head_traits(const Face& f, bool looks3d);

struct Count {
    int64_t key = 0;
    int n = 0;
    size_t sample = 0;  // index (into the faces given) of the first head with that key (for a style group: the first whose
                        // style is in the looks table, so it has a preview picture): its picture illustrates the value
};
// Values of facet `fc` among `faces[i]` for i in `pool` that pass the other filters, with counts; sorted by key
// (ethnicity by group order). 3D facets list every value of the trait (n 0, sample kNoSample when no head has it), then
// kUnclassified when heads without a look are in the pool.
std::vector<Count> facet_counts(const std::vector<Face>& faces, const std::vector<size_t>& pool, const Filter& flt, Facet fc);

enum Sort { kSortName, kSortOverall, kSortSkin, kSortHairColour, kSortNewest, kSortCount };
const char* sort_title(Sort s);
void sort_faces(std::vector<const Face*>& rows, Sort s);

}  // namespace turbo::faces
