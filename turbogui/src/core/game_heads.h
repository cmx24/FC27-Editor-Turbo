// FC 27 LE Turbo GUI - "Game heads" of the real-face chooser (docs/asset-catalog.md, design 3.7 / 4.1): the 3D heads the
// game ships that no row of the loaded save uses, with readable names, and the write gate that decides whether one may be
// given to a player or a manager (headassetid = id, headclasscode 0, hashighqualityhead 1, headvariation = the first
// shipped variation; headtypecode is kept).
//
// Pure and header-only (no ImGui, no Windows calls, no build-list entry): ui_faces.cpp draws it, the standalone runner
// tests/catalog/run_game_heads.sh checks it in seconds. HeadCatalog is a plain copy of what the chooser needs from an
// assets::Catalog (from_catalog), so the tests fill it from the synthetic fixture without the real loader.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/asset_catalog.h"

namespace turbo::game_heads {

struct HeadInfo {
    int64_t id = 0;          // headassetid
    std::vector<int> vars;   // shipped variations, sorted
    std::string toc;         // "L", "S", "LS" ...
    bool generic = false;    // a generic_* head: never offered as a real face
    bool miniface = false;   // the game ships the miniface p<id> (the tile shows it; "His miniface too" copies it)
    std::string name;        // catalog name ("" = none)
    std::string source;      // assets::Name::source
};

struct HeadCatalog {
    assets::State state = assets::State::NotLoaded;
    bool hidden_off = false;   // asset_hidden_off.txt: no hidden head is listed or offered
    std::string build_key;     // the catalog's build key (write log)
    std::string status_line;   // assets::Catalog::status_line() (the reason in the chooser)
    std::vector<HeadInfo> heads;  // sorted by id

    bool loaded() const { return state == assets::State::Current || state == assets::State::Stale; }
    bool current() const { return state == assets::State::Current; }
    const HeadInfo* find(int64_t id) const {
        auto it = std::lower_bound(heads.begin(), heads.end(), id, [](const HeadInfo& h, int64_t v) { return h.id < v; });
        return it != heads.end() && it->id == id ? &*it : nullptr;
    }
};

// Copy of the head3d family (and its names, the miniface flags) of a loaded catalog; an empty HeadCatalog with the
// catalog's state otherwise
inline HeadCatalog from_catalog(const assets::Catalog& c) {
    HeadCatalog h;
    h.state = c.state();
    h.hidden_off = c.hidden_off();
    h.build_key = c.status().build_key;
    h.status_line = c.status_line();
    if (!c.loaded()) return h;
    for (int64_t id : c.ids(assets::Family::Head3d)) {
        HeadInfo i;
        i.id = id;
        i.vars = c.head_vars(id);
        i.toc = c.head_toc(id);
        i.generic = c.generic_head(id);
        i.miniface = c.has(assets::Family::Miniface, id);
        const assets::Name n = c.name(assets::NameKind::Player, id);
        i.name = n.text;
        i.source = n.source;
        h.heads.push_back(std::move(i));
    }
    std::sort(h.heads.begin(), h.heads.end(), [](const HeadInfo& a, const HeadInfo& b) { return a.id < b.id; });
    return h;
}

// The name a game head shows: the catalog name, else "Head #id". A name taken from a game file name (EBX slug) is
// ambiguous (5 hidden heads share a slug with another head), so it always carries the id: "Some Player #90003".
inline std::string display_name(const HeadInfo& h) {
    if (h.name.empty()) return assets::fallback_name(assets::Family::Head3d, h.id);
    if (h.source == "slug") return h.name + " #" + std::to_string(h.id);
    return h.name;
}

// Heads used by the save (design 3.6): players.headassetid of any class + manager.headassetid
using UsedHeads = std::unordered_set<int64_t>;

// Hidden heads to list: in the catalog, not generic, used by no row; none when the catalog is not loaded or
// asset_hidden_off.txt is there. Sorted by id.
inline std::vector<const HeadInfo*> hidden_heads(const HeadCatalog& c, const UsedHeads& used) {
    std::vector<const HeadInfo*> out;
    if (!c.loaded() || c.hidden_off) return out;
    for (const HeadInfo& h : c.heads)
        if (!h.generic && h.id > 0 && used.count(h.id) == 0) out.push_back(&h);
    return out;
}

// Write gate (4.1) for giving game head `id` as a real face. A head some row already uses is proven by the database and
// takes the normal path (copy from its owner), so the gate is only asked for hidden heads. False + why when:
// the catalog is off / not loaded / stale, hidden heads are switched off, the id is not a 3D head of the catalog, or it
// is a generic head.
inline bool may_give(const HeadCatalog& c, int64_t id, std::string* why) {
    auto no = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (c.state == assets::State::Off) return no("the asset catalog is switched off (asset_catalog_off.txt)");
    if (!c.loaded()) return no("no asset catalog is loaded: " + (c.status_line.empty() ? std::string("not loaded") : c.status_line));
    if (!c.current())
        return no("the asset catalog was built for another game build (" + c.build_key + "): rebuild it with " + assets::rebuild_command());
    if (c.hidden_off) return no("hidden assets are switched off (asset_hidden_off.txt)");
    const HeadInfo* h = c.find(id);
    if (!h) return no("head " + std::to_string(id) + " is not a 3D head of the game files (catalog " + c.build_key + ")");
    if (h->generic) return no("head " + std::to_string(id) + " is a generic head, not a real face");
    if (why) why->clear();
    return true;
}

// The fields a game head writes (only those the target table has are written)
struct FieldWrite {
    const char* field;
    int64_t value;
};
inline std::vector<FieldWrite> head_writes(const HeadInfo& h) {
    return {{"headassetid", h.id},
            {"headclasscode", 0},
            {"hashighqualityhead", 1},
            {"headvariation", h.vars.empty() ? 0 : int64_t(h.vars.front())}};
}

// Is a head's gender known? Schema v1 has no gender per head, and a hidden head has no row to tell: always unknown, so
// the chooser asks for a second click (critic S1: a cross-gender head is unchecked in game).
inline const char* gender_note() {
    return "Its gender is unknown (no player of this save has it): check that it suits the player before you apply it.";
}

}  // namespace turbo::game_heads
