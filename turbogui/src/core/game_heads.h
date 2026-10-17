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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/asset_catalog.h"

namespace turbo::game_heads {

struct HeadInfo {
    int64_t id = 0;          // headassetid
    std::vector<int> vars;   // shipped variations, sorted
    std::string toc;         // "L", "S", "LS" ...
    bool generic = false;    // a generic_* head: never offered as a real face
    bool miniface = false;   // the game ships the miniface p<id> (the tile shows it; giving the head copies it)
    bool legend = false;     // the game ships a legend picture legend_<id> (shown and copied when there is no miniface)
    std::string name;        // catalog name ("" = none)
    std::string source;      // assets::Name::source
    std::string slug;        // EBX folder slug ("doe_john"; "" with a catalog of builder 1)
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
        i.legend = c.has(assets::Family::Legendface, id);
        const assets::Name n = c.name(assets::NameKind::Player, id);
        i.name = n.text;
        i.source = n.source;
        i.slug = c.head_slug(id);
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

// ---- managers' own heads shipped under another id (a former player's or a legend's head: HEADS3D_CENSUS.md 4.B)
// Nothing in the game data links a manager to those heads, so the chooser matches names and marks the result "name
// match": listed for the user to check, never applied without a click and the usual confirmation.

// Name key for the match: letters only, lower case, the accents of Latin-1 / Latin Extended-A folded ("Röhl" and
// "rohl" give "rohl", "Þorsteinn" gives "thorsteinn"); "doe_john" and "Doe John" give "doejohn". Other characters drop.
inline std::string name_key(const std::string& utf8) {
    // U+00C0 .. U+017F -> base letters ("" = drop); index = code point - 0xC0
    static const char* const kFold[] = {
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",          // C0..CF
        "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "ss",          // D0..DF
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",          // E0..EF
        "d", "n", "o", "o", "o", "o", "o", "", "o", "u", "u", "u", "u", "y", "th", "y",           // F0..FF
        "a", "a", "a", "a", "a", "a", "c", "c", "c", "c", "c", "c", "c", "c", "d", "d",          // 100..10F
        "d", "d", "e", "e", "e", "e", "e", "e", "e", "e", "e", "e", "g", "g", "g", "g",          // 110..11F
        "g", "g", "g", "g", "h", "h", "h", "h", "i", "i", "i", "i", "i", "i", "i", "i",          // 120..12F
        "i", "i", "ij", "ij", "j", "j", "k", "k", "k", "l", "l", "l", "l", "l", "l", "l",        // 130..13F
        "l", "l", "l", "n", "n", "n", "n", "n", "n", "n", "n", "n", "o", "o", "o", "o",          // 140..14F
        "o", "o", "oe", "oe", "r", "r", "r", "r", "r", "r", "s", "s", "s", "s", "s", "s",        // 150..15F
        "s", "s", "t", "t", "t", "t", "t", "t", "u", "u", "u", "u", "u", "u", "u", "u",          // 160..16F
        "u", "u", "u", "u", "w", "w", "y", "y", "y", "z", "z", "z", "z", "z", "z", "s"};         // 170..17F
    static_assert(sizeof(kFold) / sizeof(kFold[0]) == 0x180 - 0xC0, "one entry per code point U+00C0 .. U+017F");
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
            else if (c >= 'a' && c <= 'z') out += static_cast<char>(c);
            ++i;
            continue;
        }
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (i + size_t(len) > utf8.size()) break;
        if (len == 2) {
            const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3Fu);
            if (cp >= 0xC0 && cp <= 0x17F) out += kFold[cp - 0xC0];
        }
        i += size_t(len);
    }
    return out;
}

// A manager of the save, as the name match needs it
struct ManagerName {
    int64_t managerid = 0;
    int64_t headassetid = 0;  // his head now (never offered back to him)
    std::string first, last, common;  // firstname, surname, commonname ("" when the table has none)
    std::string label;        // what the chooser shows ("Doe John")
};

struct NameMatch {
    const HeadInfo* head = nullptr;
    const ManagerName* manager = nullptr;
};

// The name keys of a manager: first + last, the common name, first + the first word of last ("Doe" of "Doe Smith")
inline std::vector<std::string> manager_keys(const ManagerName& m) {
    std::vector<std::string> k;
    auto add = [&](const std::string& s) {
        const std::string n = name_key(s);
        if (n.size() >= 4 && std::find(k.begin(), k.end(), n) == k.end()) k.push_back(n);
    };
    add(m.first + m.last);
    add(m.common);
    const size_t sp = m.last.find(' ');
    if (sp != std::string::npos) add(m.first + m.last.substr(0, sp));
    return k;
}

// Game heads whose EBX slug or catalog name has a manager's name key. Only heads no row of the save uses (a used head is
// someone's: a player of the same name is not him), never a generic head, never the manager's own head; none when the
// catalog is not loaded or asset_hidden_off.txt is there. Sorted by the manager's label, then head id.
inline std::vector<NameMatch> manager_name_matches(const HeadCatalog& c, const std::vector<ManagerName>& managers,
                                                   const UsedHeads& used) {
    std::vector<NameMatch> out;
    if (!c.loaded() || c.hidden_off) return out;
    std::unordered_map<std::string, std::vector<const HeadInfo*>> by_key;
    for (const HeadInfo& h : c.heads) {
        if (h.generic || h.id <= 0 || used.count(h.id)) continue;
        std::vector<std::string> ks;
        if (!h.slug.empty()) ks.push_back(name_key(h.slug));
        if (!h.name.empty()) ks.push_back(name_key(h.name));
        for (const std::string& k : ks)
            if (!k.empty()) {
                auto& v = by_key[k];
                if (std::find(v.begin(), v.end(), &h) == v.end()) v.push_back(&h);
            }
    }
    for (const ManagerName& m : managers) {
        std::vector<const HeadInfo*> found;
        for (const std::string& k : manager_keys(m)) {
            auto it = by_key.find(k);
            if (it == by_key.end()) continue;
            for (const HeadInfo* h : it->second)
                if (h->id != m.headassetid && std::find(found.begin(), found.end(), h) == found.end()) found.push_back(h);
        }
        std::sort(found.begin(), found.end(), [](const HeadInfo* a, const HeadInfo* b) { return a->id < b->id; });
        for (const HeadInfo* h : found) out.push_back({h, &m});
    }
    std::stable_sort(out.begin(), out.end(), [](const NameMatch& a, const NameMatch& b) { return a.manager->label < b.manager->label; });
    return out;
}

// ---- the miniface that comes with a head: never a mixed result (G1 2026-10-17)
// A head given through the chooser always brings the matching picture into the target's miniface (players p<playerid>,
// managers heads_staff_<managerid>), or the message says plainly why his old picture stays. Where the picture comes
// from: a player head its owner's miniface p<owner>; a game head the game's p<head>, else its legend picture; a manager
// head the owner manager's own picture.
enum class PictureState {
    Ready,    // the source picture is at hand (custom or game file): copied with the head
    Loading,  // asked from the game, not there yet: nothing is written now (the user clicks again in a moment)
    None,     // the game has no picture for this head: the head is written, the old picture stays, and the message says so
    Stalled,  // asked, and nothing came for LegacyImages::kStallSeconds (no career loaded, Live Editor busy): as None, so
              // a click is never a dead end, with the reason in the message
};
// What the picture loader says, as a PictureState: at hand = Ready; asked and not there = Loading, or Stalled once it
// has waited `stall_s` seconds; neither = None
inline PictureState settle_picture(bool at_hand, bool waiting, double waited_s, double stall_s) {
    if (at_hand) return PictureState::Ready;
    if (waiting) return waited_s >= stall_s ? PictureState::Stalled : PictureState::Loading;
    return PictureState::None;
}
struct MinifacePlan {
    bool write_head = false;  // go ahead with the head fields
    bool copy = false;        // copy the source picture to the target's miniface
    bool in_step = false;     // head and miniface match afterwards
    std::string note;         // what the user is told about the miniface ("" when in step)
};
// `target` is how the message names his picture ("his miniface p123", "the manager's picture heads_staff_45")
inline MinifacePlan miniface_plan(PictureState s, const std::string& target) {
    MinifacePlan p;
    switch (s) {
        case PictureState::Ready:
            p.write_head = p.copy = p.in_step = true;
            break;
        case PictureState::Loading:
            p.note = "nothing written: the head's miniface is still loading from the game; click the head again in a moment, so "
                     "the head and " + target + " change together";
            break;
        case PictureState::None:
            p.write_head = true;
            p.note = "the game ships no miniface for this head, so " + target + " still shows the old face: make one in the "
                     "Miniface tab (Generate from 3D model)";
            break;
        case PictureState::Stalled:
            p.write_head = true;
            p.note = "the game has sent no pictures for a while (no career loaded, or Live Editor busy), so " + target +
                     " still shows the old face: give the head again once pictures load to bring its miniface";
            break;
    }
    return p;
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
