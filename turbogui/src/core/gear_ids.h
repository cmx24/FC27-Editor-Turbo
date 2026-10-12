// FC 27 LE Turbo GUI - gear galleries from the game asset catalog (docs/asset-catalog.md, WP-08): which ids a hair,
// facial hair, boots, GK gloves or tattoo gallery lists, which picture each one shows, which are "Hidden" (in the game
// files, no row of the save uses them) and which may be written.
//
// Rules (design 3.7 / 4.1):
//  - ids = 3D models + manifest-confirmed previews of the catalog; Live Editor's csv names are not used while a catalog
//    is active, so an id the csv lists and the game does not ship is gone;
//  - a model-only id is listed with a "3D only" placeholder (no picture asked from the game);
//  - a preview-only id is listed greyed and is never writable (a picture alone does not prove a 3D-backed field);
//  - 0 = none is always writable; an id the save already uses is proven by the database (stale catalog included);
//    any other id needs a current catalog (built for the running game) and a 3D model;
//  - asset_hidden_off.txt: hidden ids are neither listed nor offered;
//  - no catalog, catalog off, or a family the catalog has nothing for: the gallery keeps its 2.0 source (FamilyView
//    stays inactive).
// Header only and pure (no ImGui, no Windows calls): tests/catalog/run_gear.sh tests it with g++ alone.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/asset_catalog.h"

namespace turbo::gear {

// The catalog family behind a players field (Family::Count: none, the gallery keeps its 2.0 source). Accessories and
// managers' outfits stay on their 2.0 source (no gate yet, design 3.7).
inline assets::Family family_of_field(const std::string& field) {
    if (field == "hairtypecode") return assets::Family::Hair;
    if (field == "facialhairtypecode") return assets::Family::Facialhair;
    if (field == "shoetypecode" || field == "smallsidedshoetypecode") return assets::Family::Boots;
    if (field == "gkglovetypecode") return assets::Family::Gkglove;
    static const char* const kTattooFields[] = {"tattoohead",    "tattoofront",    "tattooback",    "tattooleftarm",
                                                "tattoorightarm", "tattooleftleg", "tattoorightleg"};
    for (const char* t : kTattooFields)
        if (field == t) return assets::Family::Tattoo;
    return assets::Family::Count;
}

// What a gallery reads from the catalog for one family (from_catalog(), or filled directly by tests)
struct FamilyView {
    bool active = false;      // a catalog is loaded (current or stale), not switched off, and lists this family
    bool current = false;     // built for the running game: hidden ids may be written
    bool hidden_off = false;  // asset_hidden_off.txt: hidden ids neither listed nor offered
    std::string build_key;    // the catalog's build key (for the stale banner and the write log)
    std::vector<int64_t> models;              // ids with a 3D model, sorted, unique
    std::map<int64_t, std::string> previews;  // id -> exact preview path as listed in the game's manifests

    bool has_model(int64_t id) const { return std::binary_search(models.begin(), models.end(), id); }
    const std::string& preview(int64_t id) const {
        static const std::string kNone;
        auto it = previews.find(id);
        return it == previews.end() ? kNone : it->second;
    }
};

inline FamilyView from_catalog(const assets::Catalog* cat, assets::Family f) {
    FamilyView v;
    if (!cat || f == assets::Family::Count || !cat->loaded()) return v;
    v.current = cat->current();
    v.hidden_off = cat->hidden_off();
    v.build_key = cat->status().build_key;
    for (int64_t id : cat->ids(f)) {
        if (cat->has_model(f, id)) v.models.push_back(id);
        std::string p = cat->preview_path(f, id);
        if (!p.empty()) v.previews.emplace(id, std::move(p));
    }
    std::sort(v.models.begin(), v.models.end());
    v.models.erase(std::unique(v.models.begin(), v.models.end()), v.models.end());
    v.active = !v.models.empty() || !v.previews.empty();  // an empty family: the builder found nothing, keep 2.0
    return v;
}

using IdSet = std::unordered_set<int64_t>;

struct Item {
    int64_t id = 0;
    std::string preview;   // exact path; "" = no picture ("3D only" placeholder when model)
    bool model = false;    // the game has the 3D model
    bool in_save = false;  // some row of the save uses it
    bool known = false;    // the database itself knows it (tattoos: the DB tattoo table)
    bool hidden = false;   // in the game files, no row of the save uses it
    bool writable = false;
    std::string why;       // why it cannot be picked ("" when writable)
};

// May `id` be written to a field of this family? `used`: ids rows of the save have in the field(s); `known`: ids the
// database itself knows (tattoo table), treated like used ones for the gate.
inline bool writable(const FamilyView& v, const IdSet& used, int64_t id, std::string* why = nullptr,
                     const IdSet* known = nullptr) {
    auto no = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (why) why->clear();
    if (id == 0 || used.count(id) || (known && known->count(id))) return true;
    if (!v.active) return true;  // no catalog: 2.0 rules (the gallery decides)
    const bool model = v.has_model(id);
    if (!model && v.previews.count(id)) return no("Only a preview picture: the game has no 3D model for it");
    if (!model) return no("Not in the game files (catalog " + v.build_key + ")");
    if (v.hidden_off) return no("Hidden items are switched off (asset_hidden_off.txt)");
    if (!v.current)
        return no("The asset catalog was built for another game build (" + v.build_key +
                  "): items no player uses cannot be applied until it is rebuilt");
    return true;
}

inline Item item(const FamilyView& v, const IdSet& used, int64_t id, const IdSet* known = nullptr) {
    Item it;
    it.id = id;
    it.preview = v.preview(id);
    it.model = v.has_model(id);
    it.in_save = used.count(id) != 0;
    it.known = known && known->count(id);
    it.hidden = id != 0 && !it.in_save && (it.model || !it.preview.empty());
    it.writable = writable(v, used, id, &it.why, known);
    return it;
}

// Gallery rows (0 = none is not among them): every id with a model or a preview, sorted; hidden ones are left out when
// hidden items are switched off. `extra` ids (the DB tattoo table, the current value) are listed too.
inline std::vector<Item> items(const FamilyView& v, const IdSet& used, const IdSet* known = nullptr,
                               const std::vector<int64_t>& extra = {}) {
    std::vector<int64_t> ids = v.models;
    for (const auto& kv : v.previews) ids.push_back(kv.first);
    ids.insert(ids.end(), extra.begin(), extra.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<Item> out;
    out.reserve(ids.size());
    for (int64_t id : ids) {
        if (id <= 0) continue;
        Item it = item(v, used, id, known);
        if (it.hidden && v.hidden_off && !it.known) continue;
        out.push_back(std::move(it));
    }
    return out;
}

// The codes a field's combo offers from the catalog: 0 and every writable listed id (values in use are added by the
// combo itself, widgets.cpp code_value_list)
inline std::vector<int64_t> combo_codes(const FamilyView& v, const IdSet& used, const IdSet* known = nullptr) {
    std::vector<int64_t> out{0};
    for (const Item& it : items(v, used, known))
        if (it.writable) out.push_back(it.id);
    return out;
}

// Pictures worth loading in the background: the previews of the listed ids (hidden ones only while they are offered)
inline std::vector<std::string> preload_paths(const FamilyView& v, const IdSet& used) {
    std::vector<std::string> out;
    for (const auto& kv : v.previews) {
        if (v.hidden_off && !used.count(kv.first)) continue;
        out.push_back(kv.second);
    }
    return out;
}

// Short marker after a code's name in a combo: "Hidden", "not in the game files", "preview only", or "" (no note)
inline std::string code_note(const FamilyView& v, const IdSet& used, int64_t id) {
    if (!v.active || id == 0 || used.count(id)) return "";
    const bool model = v.has_model(id);
    if (!model && v.previews.count(id)) return "preview only";
    if (!model) return "not in the game files";
    return "Hidden";
}

}  // namespace turbo::gear
