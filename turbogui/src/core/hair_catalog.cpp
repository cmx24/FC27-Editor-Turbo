// FC 27 LE Turbo GUI - hair catalog (see hair_catalog.h)
#include "core/hair_catalog.h"

#include <algorithm>
#include <cctype>

#include "core/hair_styles_data.h"

namespace turbo::hair {

static const char* kLengths[kLengthCount] = {"Bald", "Buzz cut", "Short", "Medium", "Long", "Unknown (no preview)"};
static const char* kTypes[kTypeCount] = {"Straight", "Wavy", "Curly", "Afro / coily", "Braids / cornrows", "Dreads / locs",
                                         "Twists", "Ponytail / bun / top knot", "Mohawk / fade / undercut", "Slicked / quiff",
                                         "Spiky", "Other"};
static const char* kAccessories[kAccessoryCount] = {"None", "Headband", "Alice band / hairband", "Hair tie / clips", "Hat / cap / durag"};

const Style* all() { return data::kStyles; }
size_t count() { return sizeof(data::kStyles) / sizeof(data::kStyles[0]); }

const Style* find(int64_t id) {
    const Style* b = all();
    const Style* e = b + count();
    const Style* it = std::lower_bound(b, e, id, [](const Style& s, int64_t v) { return s.id < v; });
    return it != e && it->id == id ? it : nullptr;
}

Style lookup(int64_t id) {
    if (const Style* s = find(id)) return *s;
    return Style{static_cast<int16_t>(id), kShort, kOther, kNoAccessory, 0};
}

const char* length_name(int v) { return v >= 0 && v < kLengthCount ? kLengths[v] : "?"; }
const char* type_name(int v) { return v >= 0 && v < kTypeCount ? kTypes[v] : "?"; }
const char* accessory_name(int v) { return v >= 0 && v < kAccessoryCount ? kAccessories[v] : "?"; }

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string describe(const Style& s) {
    std::string out = length_name(s.length);
    if (s.type != kOther) out += ", " + lower(type_name(s.type));
    if (s.accessory != kNoAccessory) out += ", " + lower(accessory_name(s.accessory));
    return out;
}

}  // namespace turbo::hair
