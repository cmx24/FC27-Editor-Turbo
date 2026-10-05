// FC 27 LE Turbo GUI - filters of the real-face chooser (players and managers): ethnicity, skin tone, hair colour,
// hair, facial hair, facial hair colour, eye colour and gender of each head, with readable labels, counts and sort orders.
// Every menu has at most kMaxBuckets choices besides "Any": the game's values are grouped into a few looks (skin tones,
// hair colours, eye colours, and the hair and facial hair styles, whose looks come from the game's own previews: face_looks.h).
// Platform independent (no ImGui): the chooser in ui_faces.cpp draws it, the native tests check it.
//
// Fields (players and manager tables, FC 27): headtypecode, skintonecode, haircolorcode, hairtypecode,
// facialhairtypecode, facialhaircolorcode, eyecolorcode, gender (0 male, 1 female). FC 27 has no ethnicity field on players or managers:
// "Ethnicity" groups the head type (headtypecode) in the game's ranges of 500 (see kEthnicGroups in face_filter.cpp).
// Labels: Live Editor's own (loc/eng_us/localize.json: skintonecode_1..10, haircolor_0..14, eyecolor_1..10).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace turbo::faces {

enum Facet { kEthnicity, kSkin, kHairColour, kHair, kBeard, kBeardColour, kEyes, kGender, kFacetCount };
constexpr int kMaxBuckets = 5;         // choices per filter menu besides "Any" (and per sort menu)

constexpr int64_t kAny = INT64_MIN;    // no filter on that facet
constexpr int64_t kNoValue = -1;       // the table has no such field (the head never matches a filter on it)

struct Face {
    int64_t id = 0;           // playerid, or managerid for a manager head
    int64_t headassetid = 0;
    bool manager = false;     // a manager's head (heads_staff miniface)
    bool real = false;        // real face: headclasscode 0 (and hashighqualityhead 1 for players)
    std::string name, lname;  // lname: lower case, for the search box
    int overall = 0;          // players: overall rating (sort)
    int64_t raw[kFacetCount] = {kNoValue, kNoValue, kNoValue, kNoValue, kNoValue, kNoValue, kNoValue, kNoValue};  // field values
};

const char* facet_title(Facet f);   // "Ethnicity", "Skin tone", ..., "Gender"
const char* facet_field(Facet f);   // the database field: "headtypecode", "skintonecode", ...
// The bucket a field value falls in (1 .. kMaxBuckets; gender: the value; facial hair: 0 clean-shaven .. 4 full beard)
int64_t facet_key(Facet f, int64_t raw);
// Readable label of a key: "Very light", "Brown", "Tied, braids & dreads", "Clean-shaven", "Female", ...
std::string key_label(Facet f, int64_t key);
// A head's own value for its tooltip: the bucket and, for codes, the code ("Brown (Dark Brown)", "Short beard (style 250)")
std::string trait_label(Facet f, int64_t raw);
// The order the chooser shows the filter buttons in (gender first)
const Facet* facet_order();
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

struct Count {
    int64_t key = 0;
    int n = 0;
    size_t sample = 0;  // index (into the faces given) of the first head with that key (for a style group: the first whose
                        // style is in the looks table, so it has a preview picture): its picture illustrates the value
};
// Values of facet `fc` among `faces[i]` for i in `pool` that pass the other filters, with counts; sorted by key
// (ethnicity by group order).
std::vector<Count> facet_counts(const std::vector<Face>& faces, const std::vector<size_t>& pool, const Filter& flt, Facet fc);

enum Sort { kSortName, kSortOverall, kSortSkin, kSortHairColour, kSortNewest, kSortCount };
const char* sort_title(Sort s);
void sort_faces(std::vector<const Face*>& rows, Sort s);

}  // namespace turbo::faces
