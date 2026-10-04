// FC 27 LE Turbo GUI - real-face chooser filters (see face_filter.h)
#include "core/face_filter.h"

#include <algorithm>
#include <map>

namespace turbo::faces {

static const char* kTitles[kFacetCount] = {"Ethnicity", "Skin tone", "Hair colour", "Hair", "Facial hair", "Facial hair colour", "Eyes"};
static const char* kFields[kFacetCount] = {"headtypecode", "skintonecode", "haircolorcode", "hairtypecode",
                                           "facialhairtypecode", "facialhaircolorcode", "eyecolorcode"};

const char* facet_title(Facet f) { return int(f) >= 0 && int(f) < kFacetCount ? kTitles[f] : "?"; }
const char* facet_field(Facet f) { return int(f) >= 0 && int(f) < kFacetCount ? kFields[f] : ""; }

// Ethnicity groups of the head type ranges (headtypecode / 500). FC 27 has no ethnicity field for players: the groups
// come from the game's own head types, named after the skin tones and nations of the players using each range in the
// FC 27 database (2847 real faces, 21340 players, 2026-10-04): 0-499 light skin, European nations; 500-999 Korea,
// China, Japan; 1000-1499 dark skin (skin tone 70-100), France, England, Nigeria; 1500-1999 Argentina, Spain, Brazil...
enum Group { kEuropean = 1, kMediterranean, kLatin, kAfrican, kAsian, kMixed, kOther };
static const char* kGroupNames[] = {"", "European", "Mediterranean", "Latin", "African", "Asian", "Mixed", "Other"};
static const int kEthnicGroups[] = {
    kEuropean, kAsian,   kAfrican, kLatin,         kEuropean, kLatin,    kAfrican,  // 0 .. 3499
    kEuropean, kEuropean, kAfrican, kAfrican,      kEuropean, kAsian,    kAfrican,  // 3500 .. 6999
    kMediterranean, kEuropean, kOther, kAfrican,   kEuropean, kEuropean, kMixed,    // 7000 .. 10499
    kEuropean,                                                                      // 10500 .. 10999
};

int64_t facet_key(Facet f, int64_t raw) {
    if (raw < 0) return kNoValue;
    if (f == kEthnicity) {
        int64_t b = raw / 500;
        return b < int64_t(sizeof(kEthnicGroups) / sizeof(kEthnicGroups[0])) ? kEthnicGroups[b] : kOther;
    }
    return raw;
}

// Live Editor's labels (loc/eng_us/localize.json). FC 27 stores the skin tone as 10, 20 ... 100 (Live Editor's 1 .. 10).
static const char* kSkinNames[] = {"", "Caucasian 1", "Caucasian 2", "Caucasian 3", "Latin Asian 1", "Latin Asian 2", "Latin Asian 3",
                                   "Latin Asian 4", "African 1", "African 2", "African 3"};
// Hair colours; facial hair uses the same codes (0 .. 27 in FC 27, same spread as the hair)
static const char* kHairColours[] = {"Black", "Blonde", "Dirty Blonde", "Dark Brown", "Light Blonde", "Light Brown", "Medium Brown", "Red",
                                     "White", "Silver", "Green", "Blue", "Ginger", "Dark Red", "Pink"};
static const char* kEyeColours[] = {"", "Blue", "Light Blue", "Brown", "Light Brown", "Hazel", "Green", "Light Green", "Medium Blue",
                                    "Dark Brown", "Saturated Green"};

std::string key_label(Facet f, int64_t key) {
    if (key == kAny) return "Any";
    if (key == kNoValue) return "not in this table";
    const std::string n = std::to_string(key);
    switch (f) {
        case kEthnicity: return key >= kEuropean && key <= kOther ? kGroupNames[key] : "Group " + n;
        case kSkin:
            if (key >= 10 && key <= 100 && key % 10 == 0) return kSkinNames[key / 10];
            return "Skin tone " + n;
        case kHairColour:
        case kBeardColour:
            if (key >= 0 && key < int64_t(sizeof(kHairColours) / sizeof(kHairColours[0]))) return kHairColours[key];
            return "Colour " + n;
        case kHair: return key == 0 ? "Hair 0 (none)" : "Hair " + n;
        case kBeard:
            if (key == kSomeBeard) return "Any facial hair";
            return key == 0 ? "Clean-shaven" : "Style " + n;
        case kEyes:
            if (key >= 1 && key < int64_t(sizeof(kEyeColours) / sizeof(kEyeColours[0]))) return kEyeColours[key];
            return "Eyes " + n;
        default: return n;
    }
}

bool facet_has_pictures(Facet f) { return f == kHair || f == kBeard; }

std::string facet_picture(Facet f, int64_t key) {
    if (key <= 0) return "";
    if (f == kHair) return "data/ui/imgAssets/hairstyle/item_" + std::to_string(key) + "_0.dds";
    if (f == kBeard) return "data/ui/imgAssets/facialhairstyle/item_" + std::to_string(key) + "_0.dds";
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
        if (it == by_key.end()) {
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
