// FC 27 LE Turbo GUI - filters of the real-face chooser (players and managers): ethnicity, skin tone, hair colour,
// hair, facial hair, facial hair colour, eye colour and gender of each head, with readable labels, counts and sort orders.
// Every menu offers at most kMaxBuckets choices (Cassio, 2026-10-11): skin tone, hair colour, facial hair colour and eye
// colour are grouped (a head's tooltip keeps the exact name, Live Editor's); the hair and facial hair styles are grouped
// into looks from the game's own previews (face_looks.h); ethnicity groups the head type ranges; the 3D looks are grouped
// too (kGroups3d in face_filter.cpp).
// Platform independent (no ImGui): the chooser in ui_faces.cpp draws it, the native tests check it.
//
// Fields (players and manager tables, FC 27): headtypecode, skintonecode, haircolorcode, hairtypecode,
// facialhairtypecode, facialhaircolorcode, eyecolorcode, gender (0 male, 1 female). FC 27 has no ethnicity field on players or managers:
// "Ethnicity" groups the head type (headtypecode) in the game's ranges of 500 (see kEthnicGroups in face_filter.cpp).
// Labels: Live Editor's own (loc/eng_us/localize.json: skintonecode_1..10, haircolor_0..14, eyecolor_1..10); FC 27's
// hair colours 15..27 as core/field_labels.h describes them.
//
// 3D looks (core/face3d_looks.h): for real faces the game takes hair, beard, skin and eyes from the face scan, so the codes
// above do not describe the 3D head the chooser shows. Player heads with a 3D look (set_look) carry the 3D facets: Skin
// tone, Hair colour, Hair length, Hair type, Facial hair, Beard colour, Headwear, judged from the game's 3D render; a head
// without a look yet has the value kUnclassified ("not classified yet") in each of them, so a filter on a look value never
// matches it; a trait the classification is unsure of (face3d::unsure) has the value kUnsure, which no filter value matches
// either. The hidden game heads take the look of their head id (a real face's head id is its playerid). When the list has
// 3D looks the chooser shows the 3D facets, Gender, and Eyes / Ethnicity "(database)" last; manager heads (no 3D looks)
// keep the database facets. check3d compares the table with the hand check for the chooser's confidence note.
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
constexpr int kMaxBuckets = 5;         // choices per menu (Cassio, 2026-10-11): style looks, ethnicity, colour and eye
                                       // groups, 3D look groups, sort entries; a head's tooltip keeps its exact value

constexpr int64_t kAny = INT64_MIN;    // no filter on that facet
constexpr int64_t kNoValue = -1;       // the table has no such field (the head never matches a filter on it)
constexpr int64_t kUnclassified = 255;  // 3D facets: the head has no 3D look yet ("not classified yet"; sorts last)
constexpr int64_t kUnsure = 254;        // 3D facets: the look's value of that trait is a guess ("unsure": never matched)
// Chooser defaults (G1 2026-10-17, HEADS3D_CENSUS.md section 5): both optional filters start off, so every real face
// shows: "Only heads with a 3D look" (it hid 140 real faces with no look yet) and "High-quality heads only"
// (hashighqualityhead 1; 1,176 of the 4,023 real faces of a career have 0)
constexpr bool kOnly3dDefaultOn = false;
constexpr bool kHqOnlyDefaultOn = false;
constexpr size_t kNoSample = static_cast<size_t>(-1);  // Count::sample of a value no head has

struct Face {
    int64_t id = 0;           // playerid, or managerid for a manager head
    int64_t headassetid = 0;
    bool manager = false;     // a manager's head (heads_staff miniface)
    bool real = false;        // real face: headclasscode 0 (any hashighqualityhead)
    int hq = -1;              // hashighqualityhead: 1, 0, -1 when the table has none (a badge, and an optional filter)
    std::string name, lname;  // lname: fold_search(name) (core/text_fold.h: lower case, accents folded), for the search box
    int overall = 0;          // players: overall rating (sort)
    int64_t raw[kFacetCount];  // field values (database facets), look values or kUnclassified (3D facets); kNoValue: none
    const face3d::Look* look = nullptr;  // the head's 3D look (players only; set_look)
    bool game_head = false;   // a head of the game files no row of the save uses (asset catalog): id = headassetid
    std::string name_source;  // game heads: assets::Name::source of the name ("" = no name, "Head #id")
    int64_t match_manager = 0;  // a game head whose name matches this manager of the save ("name match"; 0 = none)
    std::string match_label;    // that manager's name
    Face() {
        for (auto& r : raw) r = kNoValue;
    }
};

// The chooser's own filters before the facets: "Real faces only" (headclasscode 0; game heads count as real), "High-quality
// heads only" (hashighqualityhead 1; game heads pass, they have no row) and "Only heads with a 3D look" (game heads pass)
struct PoolOptions {
    bool real_only = true;
    bool hq_only = kHqOnlyDefaultOn;
    bool only3d = kOnly3dDefaultOn;
};
bool in_pool(const Face& f, const PoolOptions& o);

// Gives a player or game head its 3D look (nullptr: not classified yet): the 3D facets take the look's values, kUnsure
// for an unsure trait, or kUnclassified
void set_look(Face& f, const face3d::Look* look);
// Heads of the list with a 3D look; the chooser shows the 3D facets when there is at least one
size_t count_looks(const std::vector<Face>& faces);

const char* facet_title(Facet f);   // "Ethnicity", "Skin tone", ..., "Gender"; 3D facets: "Skin tone", ..., "Headwear"
// Title in the chooser: with 3D looks the database facets left (eyes, ethnicity) say so: "Eyes (database)"
const char* facet_title(Facet f, bool looks3d);
bool facet_3d(Facet f);             // kSkin3d .. kHeadwear3d
const char* facet_field(Facet f);   // the database field: "headtypecode", "skintonecode", ...
// The filter key of a field value: at most kMaxBuckets groups per facet. Skin tone, hair / facial hair / eye colour,
// ethnicity and the styles: their group (1 .. kMaxBuckets; facial hair: 0 clean-shaven .. 4 full beard); the 3D facets:
// the group of the look value (0 .. group_count3d - 1) or kUnclassified; gender: the value
int64_t facet_key(Facet f, int64_t raw);
// Groups of a 3D facet (every one is offered, at most kMaxBuckets); 0 for a database facet
int group_count3d(Facet f);
// Always false since 2026-10-11 (every menu is grouped); kept for callers of the old exact colour menus
bool facet_exact(Facet f);
// Readable label of a key: "Caucasian 2", "Dark Brown", "Hazel", "Tied, braids & dreads", "Clean-shaven", "Female", ...
std::string key_label(Facet f, int64_t key);
// A head's own exact value for its tooltip ("Dark Brown", "Hazel", "Short beard", 3D: "Dark brown"), not its group
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
// (ethnicity by group order); at most kMaxBuckets. 3D facets list every group of the trait (n 0, sample kNoSample when no
// head has it); heads without a look and unsure values are no choice (facet_unknowns counts them for the chooser's note).
std::vector<Count> facet_counts(const std::vector<Face>& faces, const std::vector<size_t>& pool, const Filter& flt, Facet fc);
// 3D facets: the heads of the pool passing the other filters whose value is unsure / that have no 3D look (game heads
// without one included)
struct Unknowns {
    int unsure = 0;
    int unclassified = 0;
};
Unknowns facet_unknowns(const std::vector<Face>& faces, const std::vector<size_t>& pool, const Filter& flt, Facet fc);

// The hand check of a 3D facet (face3d::check_looks against the table): checked heads the table has, how many of them it
// answers (not unsure) and how many answers fall in the hand check's filter group. Database facets: all 0.
struct CheckResult {
    int heads = 0;
    int answered = 0;
    int right = 0;
};
CheckResult check3d(Facet f);
// The chooser's confidence note of a 3D facet: "Hand check of 185 heads: 93% in the right group, 4% unsure" ("" without
// a hand check; the percentage of right answers counts the answered heads only)
std::string check_note(Facet f);

enum Sort { kSortName, kSortOverall, kSortSkin, kSortHairColour, kSortNewest, kSortCount };
const char* sort_title(Sort s);
void sort_faces(std::vector<const Face*>& rows, Sort s);

}  // namespace turbo::faces
