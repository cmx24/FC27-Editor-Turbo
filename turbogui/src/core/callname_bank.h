// FC 27 LE Turbo GUI - the callname bank of the game asset catalog (docs/asset-catalog.md, family "callnames") for one
// commentary language and team: which commentary ids have a generic recording and which player ids have their own,
// read from the commentary banks of the game files by the offline builder. Includes players who are no longer in the
// database: the bank keeps their recordings.
//
// Header-only and platform independent (no ImGui, no Windows calls): core/callnames.h holds one (Callnames::bank),
// core/callnames_catalog.h builds the "All callnames" rows, the voice-source gate and the count line from it.
//
// Team rule: eng_us has two commentary teams (dr, gm) with their own player recordings; the generic surnames exist
// once per language (eng_us: only in the dr set), so `generic` is always the union over the language's teams. Turbo
// does not know which team the game uses: team "" = every team of the language (a single-team pack is also "").
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset_catalog.h"

namespace turbo {

// CatalogBank::own bits: which recording kinds the bank holds for a player
constexpr int kBankSimple = 1;  // PLAYER_NAMES_SIMPLE
constexpr int kBankLink = 2;    // PLAYER_NAMES_LINK
constexpr int kBankHigh = 4;    // high-intensity set (informational: the kick-off name check never asks it)
// What makes a player "spoken by his own recording": the game's kick-off GetCallname check asks only
// PLAYER_LOW_SIMPLE / _LINK (the 2.0 SIMPLE|LINK rule, build_callname_master.py), so a HIGH-only id is not
constexpr int kBankSpoken = kBankSimple | kBankLink;

// One commentary team's lists as the catalog stores them (callnames.<lang>.teams.<team>)
struct BankTeamLists {
    std::string team;                                  // "dr", "gm", "" (single-team pack)
    std::array<std::vector<int64_t>, 4> kinds;         // assets::CallKind order: generic, simple, link, high
};

struct CatalogBank {
    std::string lang;                  // "ita_it"; "" = no bank
    std::string team;                  // the team asked for; "" = every team of the language
    std::vector<std::string> teams;    // the language's teams in the catalog, sorted ("" alone for a single-team pack)
    std::string build_key;             // the catalog's build key
    bool current = false;              // catalog built for the running game (false: stale, hidden sources refused)
    bool hidden_off = false;           // asset_hidden_off.txt: ids no row of the save uses are not listed
    std::unordered_set<int64_t> generic;           // commentary ids with a generic recording (every team)
    std::unordered_map<int64_t, int> own;          // player id -> kBank* bits, in `team` (every team when "")
    std::unordered_set<int64_t> own_other_team;    // own recordings only in another team than `team`
    std::unordered_map<int64_t, assets::Name> player_names;      // catalog names.player of own / own_other_team ids
    std::unordered_map<int64_t, assets::Name> commentary_names;  // catalog names.commentary of the generic ids

    bool loaded() const { return !generic.empty() || !own.empty(); }
    bool has_generic(int64_t cid) const { return generic.count(cid) > 0; }
    bool has_own(int64_t playerid) const { return own.count(playerid) > 0; }  // any kind, HIGH included
    int own_bits(int64_t playerid) const {
        auto it = own.find(playerid);
        return it == own.end() ? 0 : it->second;
    }
    // a SIMPLE or LINK recording: what the game says at kick-off (the spoken set and the voice-source gate use this)
    bool has_spoken_own(int64_t playerid) const { return (own_bits(playerid) & kBankSpoken) != 0; }
    // only a high-intensity recording: listed (informational), never a voice source
    bool high_only(int64_t playerid) const {
        const int b = own_bits(playerid);
        return b != 0 && (b & kBankSpoken) == 0;
    }
    bool multi_team() const { return teams.size() > 1; }
    // "ita_it", "eng_us dr", "eng_us (every team)"
    std::string where() const {
        if (!multi_team()) return lang;
        return team.empty() ? lang + " (every team)" : lang + " " + team;
    }
};

// Builds the bank of `lang` / `team` from the catalog's lists. `name` gives the catalog name of an id (may be empty).
inline CatalogBank make_catalog_bank(const std::string& lang, const std::string& team, const std::vector<BankTeamLists>& lists,
                                     const std::function<assets::Name(assets::NameKind, int64_t)>& name,
                                     const std::string& build_key, bool current, bool hidden_off) {
    CatalogBank b;
    if (lang.empty() || lists.empty()) return b;
    b.lang = lang;
    b.build_key = build_key;
    b.current = current;
    b.hidden_off = hidden_off;
    bool team_known = team.empty();
    for (const BankTeamLists& l : lists) {
        b.teams.push_back(l.team);
        if (l.team == team) team_known = true;
    }
    std::sort(b.teams.begin(), b.teams.end());
    b.team = team_known ? team : std::string();  // an unknown team (another catalog) = every team
    std::unordered_set<int64_t> other;
    for (const BankTeamLists& l : lists) {
        for (int64_t id : l.kinds[static_cast<size_t>(assets::CallKind::Generic)]) b.generic.insert(id);
        const bool mine = b.team.empty() || l.team == b.team;
        const int bits[3] = {kBankSimple, kBankLink, kBankHigh};
        for (size_t k = 1; k < 4; ++k)
            for (int64_t id : l.kinds[k]) {
                if (mine)
                    b.own[id] |= bits[k - 1];
                else
                    other.insert(id);
            }
    }
    for (int64_t id : other)
        if (!b.own.count(id)) b.own_other_team.insert(id);
    if (name) {
        auto name_player = [&](int64_t id) {
            assets::Name n = name(assets::NameKind::Player, id);
            if (!n.text.empty()) b.player_names.emplace(id, std::move(n));
        };
        for (const auto& kv : b.own) name_player(kv.first);
        for (int64_t id : b.own_other_team) name_player(id);
        for (int64_t id : b.generic) {
            assets::Name n = name(assets::NameKind::Commentary, id);
            if (!n.text.empty()) b.commentary_names.emplace(id, std::move(n));
        }
    }
    return b;
}

// The generic commentary ids of EVERY language bank in a loaded catalog (all teams). The game's audio-service check asks
// these on top of the career's own ids, so a callname that only another language records is still probed (FC 27 spa_es has
// generic ids the English bank lacks). Empty when the catalog is not loaded.
inline std::unordered_set<int64_t> catalog_generic_union(const assets::Catalog& cat) {
    std::unordered_set<int64_t> out;
    if (!cat.loaded()) return out;
    for (const std::string& l : cat.callname_languages())
        for (const std::string& t : cat.callname_teams(l))
            for (int64_t id : cat.callname_ids(l, t, assets::CallKind::Generic)) out.insert(id);
    return out;
}

// The bank of a loaded catalog (current or stale); an empty bank when the catalog is not loaded (off, missing, refused)
// or has no bank for `lang`.
inline CatalogBank catalog_bank(const assets::Catalog& cat, const std::string& lang, const std::string& team) {
    if (!cat.loaded() || lang.empty()) return {};
    const auto langs = cat.callname_languages();
    if (std::find(langs.begin(), langs.end(), lang) == langs.end()) return {};
    std::vector<BankTeamLists> lists;
    for (const std::string& t : cat.callname_teams(lang)) {
        BankTeamLists l;
        l.team = t;
        for (size_t k = 0; k < 4; ++k) l.kinds[k] = cat.callname_ids(lang, t, static_cast<assets::CallKind>(k));
        lists.push_back(std::move(l));
    }
    return make_catalog_bank(lang, team, lists, [&cat](assets::NameKind k, int64_t id) { return cat.name(k, id); },
                             cat.status().build_key, cat.current(), cat.hidden_off());
}

}  // namespace turbo
