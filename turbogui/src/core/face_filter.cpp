// FC 27 LE Turbo GUI - real-face chooser filters (see face_filter.h)
#include "core/face_filter.h"

#include "core/face_looks.h"
#include "core/hair_catalog.h"

#include <algorithm>
#include <map>

namespace turbo::faces {

static const char* kTitles[kFacetCount] = {"Ethnicity", "Skin tone", "Hair colour", "Hair", "Facial hair", "Facial hair colour", "Eyes", "Gender",
                                           // 3D facets (face3d::trait_title)
                                           "Skin tone", "Hair colour", "Hair length", "Hair type", "Facial hair", "Beard colour", "Headwear"};
// 3D facets have no database field ("")
static const char* kFields[kFacetCount] = {"headtypecode", "skintonecode", "haircolorcode", "hairtypecode",
                                           "facialhairtypecode", "facialhaircolorcode", "eyecolorcode", "gender",
                                           "", "", "", "", "", "", ""};
static const Facet kOrder[kDbFacetCount] = {kGender, kEthnicity, kSkin, kHairColour, kHair, kBeard, kBeardColour, kEyes};
// with 3D looks: what the render shows, then the database facets the render cannot judge (eyes, ethnicity) last
static const Facet kOrder3d[] = {kGender,        kSkin3d,        kHairColour3d, kHairLength3d, kHairType3d,
                                 kFacialHair3d, kBeardColour3d, kHeadwear3d,   kEyes,         kEthnicity};
static_assert(kHeadwear3d - kSkin3d + 1 == face3d::kTraitCount, "one 3D facet per face3d trait");

static int trait_of(Facet f) { return int(f) - int(kSkin3d); }  // face3d::Trait of a 3D facet

bool facet_3d(Facet f) { return f >= kSkin3d && f < kFacetCount; }
const char* facet_title(Facet f) { return int(f) >= 0 && int(f) < kFacetCount ? kTitles[f] : "?"; }
const char* facet_title(Facet f, bool looks3d) {
    if (looks3d && f == kEyes) return "Eyes (database)";
    if (looks3d && f == kEthnicity) return "Ethnicity (database)";
    return facet_title(f);
}
const char* facet_field(Facet f) { return int(f) >= 0 && int(f) < kFacetCount ? kFields[f] : ""; }
const Facet* facet_order(bool looks3d) { return looks3d ? kOrder3d : kOrder; }
int facet_order_size(bool looks3d) { return looks3d ? int(sizeof(kOrder3d) / sizeof(kOrder3d[0])) : kDbFacetCount; }
bool facet_shown(Facet f, bool looks3d) {
    const Facet* o = facet_order(looks3d);
    return std::find(o, o + facet_order_size(looks3d), f) != o + facet_order_size(looks3d);
}

void set_look(Face& f, const face3d::Look* look) {
    f.look = look;
    for (int t = 0; t < face3d::kTraitCount; ++t) f.raw[kSkin3d + t] = look ? int64_t(look->v[t]) : kUnclassified;
}

size_t count_looks(const std::vector<Face>& faces) {
    size_t n = 0;
    for (const Face& f : faces) n += f.look ? 1 : 0;
    return n;
}

// Ethnicity groups of the head type ranges (headtypecode / 500). FC 27 has no ethnicity field for players: the groups
// come from the game's own head types, named after the skin tones and nations of the players using each range in the
// FC 27 database (2847 real faces, 21340 players, 2026-10-04): 0-499 light skin, European nations; 500-999 Korea,
// China, Japan; 1000-1499 dark skin (skin tone 70-100), France, England, Nigeria; 1500-1999 Argentina, Spain, Brazil...
// The Mediterranean range (7000-7499) counts as European, and the unnamed ranges as "Mixed & other" (5 groups).
enum Group { kEuropean = 1, kLatin, kAfrican, kAsian, kMixed };
static const char* kGroupNames[] = {"", "European", "Latin", "African", "Asian", "Mixed & other"};
static const int kEthnicGroups[] = {
    kEuropean, kAsian,   kAfrican, kLatin,         kEuropean, kLatin,    kAfrican,  // 0 .. 3499
    kEuropean, kEuropean, kAfrican, kAfrican,      kEuropean, kAsian,    kAfrican,  // 3500 .. 6999
    kEuropean, kEuropean, kMixed,   kAfrican,      kEuropean, kEuropean, kMixed,    // 7000 .. 10499
    kEuropean,                                                                      // 10500 .. 10999
};

// Live Editor's labels (loc/eng_us/localize.json). FC 27 stores the skin tone as 10, 20 ... 100 (Live Editor's 1 .. 10).
static const char* kSkinNames[] = {"", "Caucasian 1", "Caucasian 2", "Caucasian 3", "Latin Asian 1", "Latin Asian 2", "Latin Asian 3",
                                   "Latin Asian 4", "African 1", "African 2", "African 3"};
// Hair colours; facial hair uses the same codes (0 .. 27 in FC 27, same spread as the hair)
static const char* kHairColours[] = {"Black", "Blonde", "Dirty Blonde", "Dark Brown", "Light Blonde", "Light Brown", "Medium Brown", "Red",
                                     "White", "Silver", "Green", "Blue", "Ginger", "Dark Red", "Pink"};
static const char* kEyeColours[] = {"", "Blue", "Light Blue", "Brown", "Light Brown", "Hazel", "Green", "Light Green", "Medium Blue",
                                    "Dark Brown", "Saturated Green"};
// Hair and facial hair styles: grouped by look (face_looks.h, from the game's own previews), 5 groups each. The keys are
// the looks' values: hair 1 .. 5, facial hair 0 (clean-shaven, also the codes the game shows without facial hair) .. 4.
static const char* kHairBuckets[] = {"", "Bald & buzz cut", "Short", "Medium", "Long", "Tied, braids & dreads"};
static const char* kBeardBuckets[] = {"Clean-shaven", "Stubble", "Moustache & goatee", "Short beard", "Full beard"};

// The look of a style code: its own entry in the table, or else the nearest listed id (a tie goes to the lower id), so
// codes added by later updates or mods still fall in one of the 5 groups.
template <size_t N>
static int64_t style_look(const looks::StyleLook (&t)[N], int64_t raw) {
    const looks::StyleLook* hi = std::lower_bound(t, t + N, raw, [](const looks::StyleLook& e, int64_t v) { return e.id < v; });
    if (hi == t + N) return t[N - 1].look;
    if (hi->id == raw || hi == t) return hi->look;
    const looks::StyleLook* lo = hi - 1;
    return raw - lo->id <= hi->id - raw ? lo->look : hi->look;
}
template <size_t N>
static bool style_in(const looks::StyleLook (&t)[N], int64_t raw) {
    const looks::StyleLook* it = std::lower_bound(t, t + N, raw, [](const looks::StyleLook& e, int64_t v) { return e.id < v; });
    return it != t + N && it->id == raw;
}

// The Hair facet's bucket of a classified hair catalog style (hair_catalog.h): bald & buzz cut, tied/braided/dreaded
// styles of any length, else its length; -1 when the catalog has not classified the id or its preview hides the hair
// (face_looks.h decides).
static int64_t hair_bucket(int64_t raw) {
    const hair::Style* s = hair::find(raw);
    if (!s || !s->classified || s->length == hair::kUnknownLength) return -1;
    if (s->length <= hair::kBuzz) return 1;
    if (s->type == hair::kBraids || s->type == hair::kDreads || s->type == hair::kTwists || s->type == hair::kTied) return 5;
    return s->length == hair::kShort ? 2 : s->length == hair::kMedium ? 3 : 4;
}

int64_t facet_key(Facet f, int64_t raw) {
    if (raw < 0) return kNoValue;
    if (facet_3d(f)) return raw;  // the look's value, or kUnclassified
    switch (f) {
        case kEthnicity: {
            int64_t b = raw / 500;
            return b < int64_t(sizeof(kEthnicGroups) / sizeof(kEthnicGroups[0])) ? kEthnicGroups[b] : kMixed;
        }
        // exact values (the coarse groups were too inaccurate to find a look-alike): the key is the game's own code
        case kSkin:
        case kHairColour:
        case kBeardColour:
        case kEyes: return raw;
        case kHair: {
            int64_t b = hair_bucket(raw);
            return b > 0 ? b : style_look(looks::kHair, raw);
        }
        case kBeard: return style_look(looks::kFacialHair, raw);
        default: return raw;
    }
}

template <size_t N>
static std::string bucket_name(const char* const (&names)[N], int64_t key) {
    return key >= 1 && key < int64_t(N) ? std::string(names[key]) : "Group " + std::to_string(key);
}

// Exact value labels (Live Editor's names; codes Live Editor has no name for keep their number)
static std::string exact_label(Facet f, int64_t raw) {
    const std::string n = std::to_string(raw);
    switch (f) {
        case kSkin:
            if (raw >= 10 && raw <= 100 && raw % 10 == 0) return kSkinNames[raw / 10];
            if (raw >= 1 && raw <= 10) return kSkinNames[raw];  // Live Editor's own scale
            return "Skin tone " + n;
        case kHairColour:
        case kBeardColour:
            if (raw >= 0 && raw < int64_t(sizeof(kHairColours) / sizeof(kHairColours[0]))) return kHairColours[raw];
            return "Colour " + n;
        case kEyes:
            if (raw >= 1 && raw < int64_t(sizeof(kEyeColours) / sizeof(kEyeColours[0]))) return kEyeColours[raw];
            return "Eye colour " + n;
        default: return n;
    }
}

std::string key_label(Facet f, int64_t key) {
    if (key == kAny) return "Any";
    if (key == kNoValue) return "not in this table";
    if (facet_3d(f)) {
        if (key == kUnclassified) return "not classified yet";
        if (key >= 0 && key < face3d::value_count(trait_of(f))) return face3d::value_name(trait_of(f), int(key));
        return "Value " + std::to_string(key);
    }
    switch (f) {
        case kEthnicity: return bucket_name(kGroupNames, key);
        case kSkin: return exact_label(f, key);
        case kHairColour:
        case kBeardColour: return exact_label(f, key);
        case kHair: return bucket_name(kHairBuckets, key);
        case kBeard:
            return key >= 0 && key < int64_t(sizeof(kBeardBuckets) / sizeof(kBeardBuckets[0])) ? std::string(kBeardBuckets[key])
                                                                                               : "Group " + std::to_string(key);
        case kEyes: return exact_label(f, key);
        case kGender: return key == 0 ? "Male" : key == 1 ? "Female" : "Gender " + std::to_string(key);
        default: return std::to_string(key);
    }
}

std::string trait_label(Facet f, int64_t raw) {
    if (raw < 0) return key_label(f, kNoValue);
    if (facet_3d(f)) return key_label(f, raw);
    const int64_t key = facet_key(f, raw);
    // no "(code N)" / "(style N)": the name is what a person reads (the number stays in the game's database)
    switch (f) {
        case kHair:
            if (const hair::Style* s = hair::find(raw); s && s->classified) return key_label(f, key) + " (" + hair::describe(*s) + ")";
            return key_label(f, key);
        default: return key_label(f, key);
    }
}

bool facet_exact(Facet f) { return f == kSkin || f == kHairColour || f == kBeardColour || f == kEyes || facet_3d(f); }

bool facet_has_pictures(Facet f) { return f == kHair || f == kBeard; }

bool style_listed(Facet f, int64_t raw) {
    if (f == kHair) return style_in(looks::kHair, raw);
    if (f == kBeard) return style_in(looks::kFacialHair, raw);
    return false;
}

std::string facet_picture(Facet f, int64_t raw) {
    if (raw <= 0) return "";
    if (f == kHair) return "data/ui/imgAssets/hairstyle/item_" + std::to_string(raw) + "_0.dds";
    if (f == kBeard) return "data/ui/imgAssets/facialhairstyle/item_" + std::to_string(raw) + "_0.dds";
    return "";
}

static bool facet_ok(const Face& face, Facet f, int64_t sel) {
    if (sel == kAny) return true;
    const int64_t raw = face.raw[f];
    if (raw < 0) return false;
    return facet_key(f, raw) == sel;
}

bool matches(const Face& f, const Filter& flt, int skip) {
    for (int i = 0; i < kFacetCount; ++i)
        if (i != skip && !facet_ok(f, static_cast<Facet>(i), flt.sel[i])) return false;
    return true;
}

void drop_hidden(Filter& flt, bool looks3d) {
    for (int i = 0; i < kFacetCount; ++i)
        if (!facet_shown(static_cast<Facet>(i), looks3d)) flt.sel[i] = kAny;
}

std::string head_traits(const Face& f, bool looks3d) {
    std::string out;
    if (looks3d) out += std::string("\n3D look: ") + (f.look ? face3d::describe(*f.look) : std::string("not classified yet"));
    const Facet* o = facet_order(looks3d);
    for (int k = 0; k < facet_order_size(looks3d); ++k) {
        const Facet fc = o[k];
        if (facet_3d(fc) || f.raw[fc] < 0) continue;
        out += std::string("\n") + facet_title(fc, looks3d) + ": " + trait_label(fc, f.raw[fc]);
    }
    return out;
}

std::vector<Count> facet_counts(const std::vector<Face>& faces, const std::vector<size_t>& pool, const Filter& flt, Facet fc) {
    std::map<int64_t, Count> by_key;
    std::map<int64_t, bool> pictured;  // style groups: the sample's style is in the looks table (it has a preview)
    const bool styles = facet_has_pictures(fc);
    for (size_t i : pool) {
        if (i >= faces.size()) continue;
        const Face& f = faces[i];
        if (f.raw[fc] < 0 || !matches(f, flt, fc)) continue;
        const int64_t k = facet_key(fc, f.raw[fc]);
        auto it = by_key.find(k);
        if (it == by_key.end()) {
            Count c;
            c.key = k;
            c.n = 1;
            c.sample = i;
            by_key.emplace(k, c);
            pictured[k] = styles && f.raw[fc] > 0 && style_listed(fc, f.raw[fc]);
        } else {
            ++it->second.n;
            // a representative style with a picture: the first head of the group whose style is a listed one
            if (styles && !pictured[k] && f.raw[fc] > 0 && style_listed(fc, f.raw[fc])) {
                it->second.sample = i;
                pictured[k] = true;
            }
        }
    }
    std::vector<Count> out;
    if (facet_3d(fc)) {
        // every value of the trait is offered (no head: n 0), then "not classified yet" when the pool has such heads
        for (int v = 0; v < face3d::value_count(trait_of(fc)); ++v) {
            auto it = by_key.find(v);
            Count c;
            c.key = v;
            c.sample = kNoSample;
            if (it != by_key.end()) c = it->second;
            out.push_back(c);
        }
        if (auto it = by_key.find(kUnclassified); it != by_key.end()) out.push_back(it->second);
        return out;
    }
    for (const auto& kv : by_key) out.push_back(kv.second);
    return out;
}

static const char* kSortTitles[kSortCount] = {"Name", "Overall (highest first)", "Skin tone (light to dark)", "Hair colour", "Newest heads (ID)"};
const char* sort_title(Sort s) { return int(s) >= 0 && int(s) < kSortCount ? kSortTitles[s] : "?"; }

void sort_faces(std::vector<const Face*>& rows, Sort s) {
    auto by_name = [](const Face* a, const Face* b) { return a->name != b->name ? a->name < b->name : a->id < b->id; };
    switch (s) {
        case kSortOverall:
            std::stable_sort(rows.begin(), rows.end(), [&](const Face* a, const Face* b) {
                return a->overall != b->overall ? a->overall > b->overall : by_name(a, b);
            });
            break;
        case kSortSkin:
            std::stable_sort(rows.begin(), rows.end(), [&](const Face* a, const Face* b) {
                return a->raw[kSkin] != b->raw[kSkin] ? a->raw[kSkin] < b->raw[kSkin] : by_name(a, b);
            });
            break;
        case kSortHairColour:
            std::stable_sort(rows.begin(), rows.end(), [&](const Face* a, const Face* b) {
                return a->raw[kHairColour] != b->raw[kHairColour] ? a->raw[kHairColour] < b->raw[kHairColour] : by_name(a, b);
            });
            break;
        case kSortNewest:
            std::stable_sort(rows.begin(), rows.end(), [](const Face* a, const Face* b) {
                return a->headassetid != b->headassetid ? a->headassetid > b->headassetid : a->id > b->id;
            });
            break;
        default: std::stable_sort(rows.begin(), rows.end(), by_name); break;
    }
}

}  // namespace turbo::faces
