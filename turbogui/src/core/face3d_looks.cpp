// FC 27 LE Turbo GUI - 3D looks of the real faces (see face3d_looks.h)
#include "core/face3d_looks.h"

#include <algorithm>
#include <cctype>

#include "core/face3d_looks_data.h"

namespace turbo::face3d {

namespace {
struct TraitInfo {
    const char* title;
    int n;
    const char* const* words;
    const char* const* names;
};
const char* const kSkinWords[] = {"very_light", "light", "medium", "olive", "brown", "dark"};
const char* const kSkinNames[] = {"Very light", "Light", "Medium", "Olive", "Brown", "Dark"};
const char* const kHairColourWords[] = {"black", "dark_brown", "brown", "light_brown", "blonde", "red", "grey", "white", "dyed", "none"};
const char* const kHairColourNames[] = {"Black", "Dark brown", "Brown", "Light brown", "Blonde", "Red", "Grey", "White", "Dyed", "None (no hair)"};
const char* const kLengthWords[] = {"bald", "buzz", "short", "medium", "long"};
const char* const kLengthNames[] = {"Bald", "Buzz cut", "Short", "Medium", "Long"};
const char* const kTypeWords[] = {"straight", "wavy", "curly", "afro", "braids", "dreads", "twists", "tied", "mohawk", "slicked", "spiky", "other"};
const char* const kTypeNames[] = {"Straight", "Wavy", "Curly", "Afro", "Braids / cornrows", "Dreads", "Twists",
                                  "Tied (ponytail / bun)", "Mohawk", "Slicked", "Spiky", "Other"};
const char* const kFacialWords[] = {"clean", "stubble", "moustache", "goatee", "short_beard", "full_beard", "long_beard"};
const char* const kFacialNames[] = {"Clean-shaven", "Stubble", "Moustache", "Goatee", "Short beard", "Full beard", "Long beard"};
const char* const kBeardColourWords[] = {"none", "black", "dark_brown", "brown", "blonde", "red", "grey"};
const char* const kBeardColourNames[] = {"None", "Black", "Dark brown", "Brown", "Blonde", "Red", "Grey"};
const char* const kHeadwearWords[] = {"none", "headband", "hairband", "hat"};
const char* const kHeadwearNames[] = {"None", "Headband", "Hairband", "Hat / cap"};

#define TURBO_TRAIT(title, w, n) {title, int(sizeof(w) / sizeof(w[0])), w, n}
const TraitInfo kTraits[kTraitCount] = {
    TURBO_TRAIT("Skin tone", kSkinWords, kSkinNames),          TURBO_TRAIT("Hair colour", kHairColourWords, kHairColourNames),
    TURBO_TRAIT("Hair length", kLengthWords, kLengthNames),    TURBO_TRAIT("Hair type", kTypeWords, kTypeNames),
    TURBO_TRAIT("Facial hair", kFacialWords, kFacialNames),    TURBO_TRAIT("Beard colour", kBeardColourWords, kBeardColourNames),
    TURBO_TRAIT("Headwear", kHeadwearWords, kHeadwearNames),
};
#undef TURBO_TRAIT
static_assert(sizeof(kSkinNames) / sizeof(kSkinNames[0]) == kSkinCount && sizeof(kHairColourNames) / sizeof(kHairColourNames[0]) == kHairColourCount &&
                  sizeof(kLengthNames) / sizeof(kLengthNames[0]) == kHairLengthCount && sizeof(kTypeNames) / sizeof(kTypeNames[0]) == kHairTypeCount &&
                  sizeof(kFacialNames) / sizeof(kFacialNames[0]) == kFacialHairCount &&
                  sizeof(kBeardColourNames) / sizeof(kBeardColourNames[0]) == kBeardColourCount &&
                  sizeof(kHeadwearNames) / sizeof(kHeadwearNames[0]) == kHeadwearCount && kHairTypeCount == kMaxValues,
              "face3d word lists and enums out of step");

const Look* g_table = data::kLooks;
size_t g_count = data::kCount;
uint64_t g_gen = 1;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace

const Look* all() { return g_table; }
size_t count() { return g_count; }

const Look* lookup(int64_t playerid) {
    if (playerid <= 0 || !g_table || g_count == 0) return nullptr;
    const Look* end = g_table + g_count;
    const Look* it = std::lower_bound(g_table, end, playerid, [](const Look& l, int64_t v) { return l.id < v; });
    return it != end && it->id == playerid ? it : nullptr;
}

void use_table(const Look* t, size_t n) {
    g_table = t ? t : data::kLooks;
    g_count = t ? n : data::kCount;
    ++g_gen;
}
uint64_t generation() { return g_gen; }

const char* trait_title(int trait) { return trait >= 0 && trait < kTraitCount ? kTraits[trait].title : "?"; }
int value_count(int trait) { return trait >= 0 && trait < kTraitCount ? kTraits[trait].n : 0; }
const char* value_name(int trait, int v) { return v >= 0 && v < value_count(trait) ? kTraits[trait].names[v] : "?"; }
const char* value_word(int trait, int v) { return v >= 0 && v < value_count(trait) ? kTraits[trait].words[v] : ""; }

int parse_value(int trait, const std::string& word) {
    const std::string w = lower(trim(word));
    for (int v = 0; v < value_count(trait); ++v)
        if (w == kTraits[trait].words[v]) return v;
    return -1;
}

bool parse_row(const std::string& line, Look* out) {
    std::string s = line;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    std::string cells[kTraitCount + 1];
    size_t n = 0, start = 0;
    for (;;) {
        const size_t comma = s.find(',', start);
        if (n > size_t(kTraitCount)) return false;
        cells[n++] = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (n != size_t(kTraitCount) + 1) return false;
    const std::string id = trim(cells[0]);
    if (id.empty() || id.size() > 10 || !std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    const long long pid = std::stoll(id);
    if (pid <= 0 || pid > 0x7FFFFFFFLL) return false;
    Look l{};
    l.id = static_cast<int32_t>(pid);
    for (int t = 0; t < kTraitCount; ++t) {
        const int v = parse_value(t, cells[t + 1]);
        if (v < 0) return false;
        l.v[t] = static_cast<uint8_t>(v);
    }
    if (out) *out = l;
    return true;
}

std::string describe(const Look& l) {
    // the CSV words read best in a sentence ("dark brown short wavy hair"); a few get their readable name
    auto word = [](int t, int v) {
        std::string w = value_word(t, v);
        std::replace(w.begin(), w.end(), '_', ' ');
        return w;
    };
    std::string out = word(kSkin, l.v[kSkin]) + " skin, ";
    if (l.v[kHairLength] == kBald) {
        out += "bald";
    } else {
        if (l.v[kHairColour] != kHairNone) out += word(kHairColour, l.v[kHairColour]) + " ";
        out += l.v[kHairLength] == kBuzz ? std::string("buzz-cut") : word(kHairLength, l.v[kHairLength]);
        if (l.v[kHairType] != kOtherType) out += " " + word(kHairType, l.v[kHairType]);
        out += " hair";
    }
    out += ", " + (l.v[kFacialHair] == kClean ? std::string("clean-shaven") : word(kFacialHair, l.v[kFacialHair]));
    if (l.v[kFacialHair] != kClean && l.v[kBeardColour] != kBeardNone) out += " (" + word(kBeardColour, l.v[kBeardColour]) + ")";
    out += l.v[kHeadwear] == kNoHeadwear ? std::string(", no headwear") : ", " + lower(value_name(kHeadwear, l.v[kHeadwear]));
    return out;
}

}  // namespace turbo::face3d
