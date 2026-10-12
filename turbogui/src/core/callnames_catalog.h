// FC 27 LE Turbo GUI - "All callnames" from the game asset catalog (docs/asset-catalog.md): every recording of the
// commentary bank (core/callname_bank.h), including players who are no longer in the database, with readable names,
// the Hidden / No recording flags, the voice-source gate and the count line (bank vs master list vs database).
//
// Without a bank (catalog off, missing, refused, or no bank for the language) the picker keeps the 2.0 behaviour:
// core/callnames.h all_callnames() over the master list.
//
// Header-only and pure (no ImGui, no Windows calls, no database access): the UI passes what the save uses.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "asset_catalog.h"
#include "callname_bank.h"
#include "callnames.h"

namespace turbo {

namespace callnames_catalog_detail {
inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
inline bool by_text(const AllCallnameRow& a, const AllCallnameRow& b) {
    const std::string la = lower(a.text), lb = lower(b.text);
    if (la != lb) return la < lb;
    return a.own() ? a.playerid < b.playerid : a.commentaryid < b.commentaryid;
}
}  // namespace callnames_catalog_detail

// What the loaded save uses, for the Hidden flag (docs/asset-catalog.md section 4: callnames own = players.playerid,
// generic = commentarynames.commentaryid + playernamemap)
struct CallnameDbUse {
    std::function<bool(int64_t)> player_in_db;        // is this player id in the database?
    std::function<std::string(int64_t)> player_name;  // his name in the database ("" = not there)
    std::unordered_set<int64_t> commentary_ids;       // commentarynames ids (else playernames' ids) + playernamemap's
};

// Rows of "All callnames" from the bank: the generic ids (bank and master list) by text, then the own recordings (bank,
// the other commentary team's and master list) by name. Names: database > catalog > master list > "Callname #id" (name_source says which: "db",
// "master:<lang>" / "slug" / ... from the catalog, "master" for the master list, "" for the fallback).
// Flags: recorded = the bank has the recording (false: only the master list names it, so it is not a voice source);
// hidden = recorded and no row of the save uses the id; in_master = the master list has it.
inline std::vector<AllCallnameRow> all_callnames_bank(const CatalogBank& b, const MasterList& m, const CallnameIndex& ix,
                                                      const CallnameDbUse& use) {
    using callnames_catalog_detail::by_text;
    std::vector<AllCallnameRow> gen, own;
    // ---- generic recordings
    std::unordered_set<int64_t> gids(b.generic.begin(), b.generic.end());
    gids.insert(m.generic_ids.begin(), m.generic_ids.end());
    std::unordered_map<int64_t, size_t> at;
    gen.reserve(gids.size());
    for (int64_t cid : gids) {
        AllCallnameRow r;
        r.commentaryid = cid;
        r.recorded = b.has_generic(cid);
        r.in_master = m.generic_ids.count(cid) > 0;
        at[cid] = gen.size();
        gen.push_back(std::move(r));
    }
    // the playernames rows using each id (the one most players use carries a name assignment), as all_callnames()
    for (const auto& [nameid, cid] : ix.name_commentary) {
        auto it = at.find(cid);
        if (it == at.end()) continue;
        AllCallnameRow& r = gen[it->second];
        auto u = ix.name_users.find(nameid);
        const int users = u != ix.name_users.end() ? u->second : 0;
        ++r.name_rows;
        r.users += users;
        if (!r.nameid) {
            r.nameid = nameid;
        } else {
            auto bu = ix.name_users.find(r.nameid);
            const int best = bu != ix.name_users.end() ? bu->second : 0;
            if (users > best || (users == best && nameid < r.nameid)) r.nameid = nameid;
        }
    }
    std::unordered_map<int64_t, const std::string*> db_name_text;  // nameid -> the database's text
    for (const auto& c : ix.names) db_name_text[c.nameid] = &c.name;
    for (auto& r : gen) {
        const int64_t cid = r.commentaryid;
        if (r.nameid) {
            auto d = db_name_text.find(r.nameid);
            if (d != db_name_text.end() && !d->second->empty()) {
                r.text = *d->second;
                r.name_source = "db";
            }
        }
        if (r.text.empty()) {
            auto c = b.commentary_names.find(cid);
            if (c != b.commentary_names.end()) {
                r.text = c->second.text;
                r.name_source = c->second.source;
            }
        }
        if (r.text.empty()) {
            auto t = m.generic_names.find(cid);
            if (t != m.generic_names.end() && !t->second.empty()) {
                r.text = t->second;
                r.name_source = "master";
            }
        }
        if (r.text.empty()) r.text = assets::fallback_name(assets::Family::Callnames, cid);
        r.hidden = r.recorded && r.name_rows == 0 && !use.commentary_ids.count(cid);
    }
    std::sort(gen.begin(), gen.end(), by_text);
    // ---- own recordings
    std::unordered_set<int64_t> pids;
    for (const auto& kv : b.own) pids.insert(kv.first);
    pids.insert(b.own_other_team.begin(), b.own_other_team.end());  // listed, flagged No recording for this team
    pids.insert(m.real_players.begin(), m.real_players.end());
    own.reserve(pids.size());
    for (int64_t pid : pids) {
        AllCallnameRow r;
        r.playerid = pid;
        r.recorded = b.has_own(pid);
        r.in_master = m.real_players.count(pid) > 0;
        const bool in_db = use.player_in_db ? use.player_in_db(pid) : false;
        if (in_db && use.player_name) {
            r.text = use.player_name(pid);
            if (!r.text.empty()) r.name_source = "db";
        }
        if (r.text.empty()) {
            auto c = b.player_names.find(pid);
            if (c != b.player_names.end()) {
                r.text = c->second.text;
                r.name_source = c->second.source;
            }
        }
        if (r.text.empty()) {
            auto n = m.names.find(pid);
            if (n != m.names.end() && !n->second.empty()) {
                r.text = n->second;
                r.name_source = "master";
            }
        }
        if (r.text.empty()) r.text = assets::fallback_name(assets::Family::Callnames, pid);
        r.hidden = r.recorded && !in_db;
        own.push_back(std::move(r));
    }
    std::sort(own.begin(), own.end(), by_text);
    gen.insert(gen.end(), std::make_move_iterator(own.begin()), std::make_move_iterator(own.end()));
    return gen;
}

// Does a row pass the All / In this save / Hidden filter? Hidden rows are never listed with asset_hidden_off.txt. A
// row the bank lacks (master list only) is listed under All only.
inline bool callname_row_shown(const CatalogBank& b, assets::Scope scope, const AllCallnameRow& r) {
    if (r.hidden && b.hidden_off) return false;
    if (!r.recorded) return scope == assets::Scope::All;
    return assets::scope_shows(scope, r.hidden);
}

// "name from: the database" / "name from: ita_it master" / ... for the row's tooltip ("" for the fallback)
inline std::string callname_name_source_label(const std::string& source, const MasterList& m) {
    if (source.empty()) return "";
    if (source == "db") return "name from: the database";
    if (source == "master") return "name from: " + m.label();
    return assets::name_source_label(source);
}

// The voice-source gate (docs/asset-catalog.md section 5): a voice swap source / a kick-off generic id needs a
// recording in the bank of the active language (and team). No bank = the 2.0 rules (always ok).
struct VoiceGate {
    bool ok = true;
    std::string why;  // when refused: one line for the UI
};
inline VoiceGate voice_source_gate(const CatalogBank& b, const AllCallnameRow& r) {
    VoiceGate g;
    if (!b.loaded()) return g;
    const bool recorded = r.own() ? b.has_own(r.playerid) : b.has_generic(r.commentaryid);
    if (!recorded) {
        g.ok = false;
        if (r.own() && b.own_other_team.count(r.playerid))
            g.why = "recorded only by the other " + b.lang + " commentary team, not " + b.team;
        else
            g.why = std::string("no recording of this ") + (r.own() ? "player" : "callname") + " in the " + b.where() +
                    " commentary files (asset catalog " + b.build_key + ")";
        return g;
    }
    if (r.hidden && !b.current) {
        g.ok = false;
        g.why = "the asset catalog was built for another game build (" + b.build_key +
                "): rebuild it to use recordings of players who are not in this save (" + assets::rebuild_command() + ")";
    }
    return g;
}

// Counts behind the reconciliation line: the bank against the master list against the database
struct CallnameCounts {
    size_t bank_generic = 0, bank_own = 0;            // recordings in the bank (this language / team)
    size_t master_generic = 0, master_own = 0;        // ids the master list names
    size_t master_generic_unrecorded = 0, master_own_unrecorded = 0;  // master ids the bank lacks
    size_t bank_generic_unlisted = 0, bank_own_unlisted = 0;          // bank ids the master list lacks
    size_t own_in_db = 0, own_hidden = 0;             // own recordings of players in / not in the save
    size_t generic_in_db = 0, generic_hidden = 0;     // generic recordings the save uses / does not use
};
inline CallnameCounts callname_counts(const std::vector<AllCallnameRow>& rows) {
    CallnameCounts c;
    for (const AllCallnameRow& r : rows) {
        const bool own = r.own();
        if (r.recorded) {
            ++(own ? c.bank_own : c.bank_generic);
            ++(r.hidden ? (own ? c.own_hidden : c.generic_hidden) : (own ? c.own_in_db : c.generic_in_db));
            if (!r.in_master) ++(own ? c.bank_own_unlisted : c.bank_generic_unlisted);
        }
        if (r.in_master) {
            ++(own ? c.master_own : c.master_generic);
            if (!r.recorded) ++(own ? c.master_own_unrecorded : c.master_generic_unrecorded);
        }
    }
    return c;
}
// "Bank ita_it: 2533 generic, 4046 own recordings | FC 27 master: 2533 generic, 4048 own (2 without a recording) |
//  this save: 2869 players with their own recording, 1177 not in it (Hidden); 2512 generic ids used, 21 not"
inline std::string callname_count_line(const CallnameCounts& c, const CatalogBank& b, const MasterList& m) {
    auto n = [](size_t v) { return std::to_string(v); };
    std::string s = "Bank " + b.where() + ": " + n(c.bank_generic) + " generic, " + n(c.bank_own) + " own recordings";
    if (c.bank_generic_unlisted || c.bank_own_unlisted)
        s += " (" + n(c.bank_generic_unlisted + c.bank_own_unlisted) + " not in the master list)";
    if (m.loaded()) {
        s += " | " + m.label(true) + ": " + n(c.master_generic) + " generic, " + n(c.master_own) + " own";
        const size_t un = c.master_generic_unrecorded + c.master_own_unrecorded;
        if (un) s += " (" + n(un) + " without a recording)";
    } else {
        s += " | no master list";
    }
    s += " | this save: " + n(c.own_in_db) + " players with their own recording, " + n(c.own_hidden) + " not in it (Hidden); " +
         n(c.generic_in_db) + " generic ids used, " + n(c.generic_hidden) + " not";
    if (!b.current) s += " | catalog built for another game build";
    return s;
}

}  // namespace turbo
