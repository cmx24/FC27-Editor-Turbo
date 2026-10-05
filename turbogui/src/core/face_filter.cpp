// FC 27 LE Turbo GUI - real-face chooser filters (see face_filter.h)
#include "core/face_filter.h"

#include <algorithm>
#include <map>

namespace turbo::faces {

static const char* kTitles[kFacetCount] = {"Ethnicity", "Skin tone", "Hair colour", "Hair", "Facial hair", "Facial hair colour", "Eyes", "Gender"};
static const char* kFields[kFacetCount] = {"headtypecode", "skintonecode", "haircolorcode", "hairtypecode",
                                           "facialhairtypecode", "facialhaircolorcode", "eyecolorcode", "gender"};
static const Facet kOrder[kFacetCount] = {kGender, kEthnicity, kSkin, kHairColour, kHair, kBeard, kBeardColour, kEyes};

const char* facet_title(Facet f) { return int(f) >= 0 && int(f) < kFacetCount ? kTitles[f] : "?"; }
const char* facet_field(Facet f) { return int(f) >= 0 && int(f) < kFacetCount ? kFields[f] : ""; }
const Facet* facet_order() { return kOrder; }

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
static const char* kSkinBuckets[] = {"", "Very light", "Light", "Medium", "Dark", "Very dark"};  // 0-20, 30-40, 50-60, 70-80, 90-100
// Hair colours; facial hair uses the same codes (0 .. 27 in FC 27, same spread as the hair)
static const char* kHairColours[] = {"Black", "Blonde", "Dirty Blonde", "Dark Brown", "Light Blonde", "Light Brown", "Medium Brown", "Red",
                                     "White", "Silver", "Green", "Blue", "Ginger", "Dark Red", "Pink"};
// Bucket of each hair colour code: 1 black, 2 brown, 3 blonde, 4 red / ginger (pink too), 5 grey, white and the rest
// (dyed green / blue, and the codes Live Editor has no name for)
static const char* kHairColourBuckets[] = {"", "Black", "Brown", "Blonde", "Red & ginger", "Grey, white & other"};
static const int kHairColourBucket[] = {1, 3, 3, 2, 3, 2, 2, 4, 5, 5, 5, 5, 4, 4, 4};
static const char* kEyeColours[] = {"", "Blue", "Light Blue", "Brown", "Light Brown", "Hazel", "Green", "Light Green", "Medium Blue",
                                    "Dark Brown", "Saturated Green"};
static const char* kEyeBuckets[] = {"", "Blue", "Green", "Brown", "Light brown & hazel", "Other"};
static const int kEyeBucket[] = {5, 1, 1, 3, 4, 4, 2, 2, 1, 3, 2};  // code 0 .. 10; others: 5
// Hair styles: no names or looks are known for the codes, so the menu groups them by code family (thousands), each
// shown with the picture of one of its styles: 0-999, 1000-1999, 2000-2999, 3000-3999, 4000 and up
static const char* kHairBuckets[] = {"", "Styles 0-999", "Styles 1000-1999", "Styles 2000-2999", "Styles 3000-3999", "Styles 4000+"};
// Facial hair styles: the looks of the styles with a game preview (data/ui/imgAssets/facialhairstyle, 2026-10-04);
// the others are listed under "Any facial hair" only
enum BeardLook { kStubble = 1, kMoustache, kFullBeard };
static const char* kBeardBuckets[] = {"Clean-shaven", "Stubble", "Moustache & goatee", "Beard"};
static const std::map<int64_t, int>& beard_looks() {
    static const std::map<int64_t, int> m = {
        {30, kStubble},    {68, kStubble},    {80, kMoustache},  {284, kStubble},   {303, kStubble},   {31, kMoustache},
        {78, kMoustache},  {88, kMoustache},  {89, kMoustache},  {240, kMoustache}, {261, kMoustache}, {262, kMoustache},
        {297, kMoustache}, {298, kMoustache}, {299, kMoustache}, {302, kMoustache}, {304, kMoustache}, {79, kFullBeard},
        {81, kFullBeard},  {87, kFullBeard},  {250, kFullBeard}, {263, kFullBeard}, {264, kFullBeard}, {265, kFullBeard},
        {267, kFullBeard}, {276, kFullBeard}, {283, kFullBeard},
    };
    return m;
}

template <size_t N>
static int64_t table_bucket(const int (&t)[N], int64_t raw, int64_t other) {
    return raw >= 0 && raw < int64_t(N) ? t[raw] : other;
}

int64_t facet_key(Facet f, int64_t raw) {
    if (raw < 0) return kNoValue;
    switch (f) {
        case kEthnicity: {
            int64_t b = raw / 500;
            return b < int64_t(sizeof(kEthnicGroups) / sizeof(kEthnicGroups[0])) ? kEthnicGroups[b] : kMixed;
        }
        case kSkin: return raw <= 20 ? 1 : raw <= 40 ? 2 : raw <= 60 ? 3 : raw <= 80 ? 4 : 5;
        case kHairColour:
        case kBeardColour: return table_bucket(kHairColourBucket, raw, 5);
        case kEyes: return table_bucket(kEyeBucket, raw, 5);
        case kHair: return std::min<int64_t>(raw / 1000, 4) + 1;
        case kBeard: {
            if (raw == 0) return 0;
            auto it = beard_looks().find(raw);
            return it != beard_looks().end() ? it->second : kSomeBeard;
        }
        default: return raw;
    }
}

template <size_t N>
static std::string bucket_name(const char* const (&names)[N], int64_t key) {
    return key >= 1 && key < int64_t(N) ? std::string(names[key]) : "Group " + std::to_string(key);
}

std::string key_label(Facet f, int64_t key) {
    if (key == kAny) return "Any";
    if (key == kNoValue) return "not in this table";
    switch (f) {
        case kEthnicity: return bucket_name(kGroupNames, key);
        case kSkin: return bucket_name(kSkinBuckets, key);
        case kHairColour:
        case kBeardColour: return bucket_name(kHairColourBuckets, key);
        case kHair: return bucket_name(kHairBuckets, key);
        case kBeard:
            if (key == kSomeBeard) return "Any facial hair";
            return key >= 0 && key <= kFullBeard ? std::string(kBeardBuckets[key]) : "Group " + std::to_string(key);
        case kEyes: return bucket_name(kEyeBuckets, key);
        case kGender: return key == 0 ? "Male" : key == 1 ? "Female" : "Gender " + std::to_string(key);
        default: return std::to_string(key);
    }
}

std::string trait_label(Facet f, int64_t raw) {
    if (raw < 0) return key_label(f, kNoValue);
    const int64_t key = facet_key(f, raw);
    const std::string n = std::to_string(raw);
    switch (f) {
        case kSkin:
            if (raw >= 10 && raw <= 100 && raw % 10 == 0) return key_label(f, key) + " (" + kSkinNames[raw / 10] + ")";
            return key_label(f, key) + " (" + n + ")";
        case kHairColour:
        case kBeardColour:
            if (raw < int64_t(sizeof(kHairColours) / sizeof(kHairColours[0]))) return key_label(f, key) + " (" + kHairColours[raw] + ")";
            return key_label(f, key) + " (colour " + n + ")";
        case kEyes:
            if (raw >= 1 && raw < int64_t(sizeof(kEyeColours) / sizeof(kEyeColours[0]))) return key_label(f, key) + " (" + kEyeColours[raw] + ")";
            return key_label(f, key) + " (eyes " + n + ")";
        case kHair: return "style " + n;
        case kBeard:
            if (raw == 0) return key_label(f, key);
            return (key == kSomeBeard ? std::string("Facial hair") : key_label(f, key)) + " (style " + n + ")";
        default: return key_label(f, key);
    }
}

bool facet_has_pictures(Facet f) { return f == kHair || f == kBeard; }

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
    if (f == kBeard && sel == kSomeBeard) return raw > 0;
    return facet_key(f, raw) == sel;
}

bool matches(const Face& f, const Filter& flt, int skip) {
    for (int i = 0; i < kFacetCount; ++i)
        if (i != skip && !facet_ok(f, static_cast<Facet>(i), flt.sel[i])) return false;
    return true;
}

std::vector<Count> facet_counts(const std::vector<Face>& faces, const std::vector<size_t>& pool, const Filter& flt, Facet fc) {
    std::map<int64_t, Count> by_key;
    Count some;
    some.key = kSomeBeard;
    for (size_t i : pool) {
        if (i >= faces.size()) continue;
        const Face& f = faces[i];
        if (f.raw[fc] < 0 || !matches(f, flt, fc)) continue;
        const int64_t k = facet_key(fc, f.raw[fc]);
        auto it = by_key.find(k);
        if (k == kSomeBeard) {
            // a facial hair style whose look is not known: counted below under "Any facial hair" only
        } else if (it == by_key.end()) {
            Count c;
            c.key = k;
            c.n = 1;
            c.sample = i;
            by_key.emplace(k, c);
        } else {
            ++it->second.n;
        }
        if (fc == kBeard && f.raw[fc] > 0) {
            if (some.n == 0) some.sample = i;
            ++some.n;
        }
    }
    std::vector<Count> out;
    if (fc == kBeard && some.n > 0) out.push_back(some);
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
