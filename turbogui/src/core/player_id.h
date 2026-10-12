// FC 27 LE Turbo GUI - the "Player ID" box of Players > Create player (ui_presets.cpp): which id a new player may get, and
// what the game files hold for that id (docs/asset-catalog.md "Player ID of a new player").
//
// Empty box = automatic, as before (Lua's create_player takes the first free id above every existing one and passes over
// ids that carry a game head). A typed id must be a whole number from 1 to 459,999 (460,000 and up is the game's own range
// for the players it generates; the database field allows 500,000), not the player-career player's id 31399, and absent
// from every per-player table below: a row there means a player of this career has or had the id. The asset catalog
// (core/asset_catalog.h) says what the game files ship for the id: a 3D head (headassetid = the id), an own callname
// recording (the commentary finds it by player id), a miniface. A new player with that id uses them; "hidden" ids are the
// ones no player of this save has, typically players who left the database whose assets still ship with the game.
//
// Pure and header-only (no ImGui, no Windows calls, no build-list entry), like core/game_heads.h: the dialog draws it and
// tests/native/test_player_id.h checks it. Nothing here writes. Lua's create_player (M.check_typed_id) checks the typed id
// again right before the first write (the same tables plus the game's squad status table, PlayerStatusManager), and
// Turbo.dll's player_create once more through the game's own SELECT (players, teamplayerlinks, editedplayernames).
// Kill switch: turbo_output\create_player_typed_id_off.txt (typed ids refused here and in Lua; automatic ids as before).
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/asset_catalog.h"
#include "core/player_create.h"
#include "core/t3db.h"

namespace turbo::player_id {

constexpr int64_t kMinId = 1;
constexpr int64_t kMaxId = pc::kMaxPlayerId - 1;            // 459999
constexpr int64_t kPlayerCareerId = pm::kPlayerCareer;     // 31399
inline const char* typed_off_name() { return "create_player_typed_id_off.txt"; }

// Tables a new player's id must not be in (the same list as create_player.lua M.ID_TABLES; absent tables are skipped)
inline const std::vector<const char*>& owner_tables() {
    static const std::vector<const char*> v = {"players",
                                               "editedplayernames",
                                               "teamplayerlinks",
                                               "playerloans",
                                               "transfers",
                                               "career_youthplayers",
                                               "career_youthplayerhistory",
                                               "career_playergrowthuserseason",
                                               "career_playerawards",
                                               "career_playermatchratinghistory",
                                               "career_teamofweek",
                                               "career_playercontract"};
    return v;
}
// Tables whose row for the id is reported but does not refuse it: playernamemap keeps rows of players who left the
// database, and the commentary uses such a row's name for a new player with that id who has no recording of his own
inline const std::vector<const char*>& note_tables() {
    static const std::vector<const char*> v = {"playernamemap"};
    return v;
}

// What the loaded database uses (scan(); tests fill it by hand)
struct Use {
    std::map<std::string, std::unordered_set<int64_t>> ids;  // table -> playerid of every valid row (owner and note tables)
    std::unordered_set<int64_t> heads;                       // players.headassetid + manager.headassetid
    std::vector<std::string> absent;                         // owner tables this database does not have (not checked)
    bool read = false;
    // The owner tables with a row for `id`, in owner_tables() order
    std::vector<std::string> owners_of(int64_t id) const {
        std::vector<std::string> out;
        for (const char* t : owner_tables()) {
            auto it = ids.find(t);
            if (it != ids.end() && it->second.count(id)) out.push_back(t);
        }
        return out;
    }
    bool has(const char* table, int64_t id) const {
        auto it = ids.find(table);
        return it != ids.end() && it->second.count(id) != 0;
    }
};

// One pass over the tables (a bulk copy each, as the lists do). A table that cannot be read counts as absent.
inline Use scan(Database& db) {
    Use u;
    auto read_ids = [&](const char* table, const char* field, std::unordered_set<int64_t>& out) {
        const Table* t = db.table(table);
        const Field* f = t ? t->field(field) : nullptr;
        if (!f) return false;
        Snapshot s;
        if (!s.load(db.memory(), *t)) return false;
        for (uint32_t i : s.valid) out.insert(s.get_int(i, *f));
        return true;
    };
    for (const char* t : owner_tables())
        if (!read_ids(t, "playerid", u.ids[t])) {
            u.ids.erase(t);
            u.absent.push_back(t);
        }
    for (const char* t : note_tables())
        if (!read_ids(t, "playerid", u.ids[t])) u.ids.erase(t);
    read_ids("players", "headassetid", u.heads);
    read_ids("manager", "headassetid", u.heads);
    u.read = db.table("players") != nullptr;
    return u;
}

// What the game files hold for an id (assets_of)
struct Assets {
    bool loaded = false;     // a catalog is loaded (current or stale) and not switched off
    bool current = false;    // ... built for the running game: its head may be given to the new player
    bool hidden_off = false; // asset_hidden_off.txt
    std::string why_not;     // why the catalog does not answer ("" when loaded)
    bool head = false;       // a 3D head of the game files with this id (headassetid), not a generic one
    bool head_generic = false;
    int head_var = 0;        // its first shipped variation
    bool head_worn = false;  // a row of the save already wears that head (players / manager headassetid)
    bool miniface = false;   // data/ui/imgAssets/heads/p<id>.dds
    std::vector<std::string> call_langs;  // languages with an own recording of the id (simple / link / high)
    bool call_loaded = false;             // the loaded commentary language has one (true when the language is unknown)
    bool call_high_only = false;          // only high-intensity recordings
    std::string name, name_source;        // the catalog's name for the id ("" = none)
    bool callname() const { return !call_langs.empty(); }
    bool any() const { return head || callname(); }
};

inline Assets assets_of(const assets::Catalog* c, const Use& use, int64_t id, const std::string& lang) {
    Assets a;
    if (!c || !c->loaded()) {
        a.why_not = !c ? std::string("no asset catalog") : c->status_line();
        return a;
    }
    a.loaded = true;
    a.current = c->current();
    a.hidden_off = c->hidden_off();
    if (c->has(assets::Family::Head3d, id)) {
        a.head_generic = c->generic_head(id);
        a.head = !a.head_generic && c->has_model(assets::Family::Head3d, id);
        const std::vector<int> vars = c->head_vars(id);
        a.head_var = vars.empty() ? 0 : vars.front();
        a.head_worn = use.heads.count(id) != 0;
    }
    a.miniface = c->has(assets::Family::Miniface, id);
    // own recordings per language (simple / link: the name calls; high: high-intensity only); the loaded language decides
    // when the catalog has it, else every language counts
    bool low_any = false, low_lang = false, lang_known = false;
    for (const std::string& l : c->callname_languages()) {
        bool rec = false, low = false;
        for (const std::string& t : c->callname_teams(l))
            for (assets::CallKind k : {assets::CallKind::Simple, assets::CallKind::Link, assets::CallKind::High}) {
                const std::vector<int64_t> v = c->callname_ids(l, t, k);
                if (std::binary_search(v.begin(), v.end(), id)) {
                    rec = true;
                    low = low || k != assets::CallKind::High;
                }
            }
        if (l == lang) lang_known = true;
        if (!rec) continue;
        a.call_langs.push_back(l);
        low_any = low_any || low;
        if (l == lang) {
            a.call_loaded = true;
            low_lang = low;
        }
    }
    if (!lang_known && a.callname()) a.call_loaded = true;  // the loaded language is unknown or not in the catalog
    a.call_high_only = a.callname() && (lang_known && a.call_loaded ? !low_lang : !low_any);
    const assets::Name n = c->name(assets::NameKind::Player, id);
    a.name = n.text;
    a.name_source = n.source;
    return a;
}

// The answer for one text of the box
struct Verdict {
    bool ok = false;
    bool automatic = false;               // empty box: the first free id (as before)
    int64_t id = 0;                       // the typed id (0 = automatic)
    std::string reason;                   // a refusal ("" when ok)
    std::vector<std::string> owners;      // owner tables with a row for the id
    Assets assets;
    std::string assets_line;              // what the new player picks up from the game files ("" = nothing to say)
    std::vector<std::string> notes;       // other remarks (playernamemap row, gender, language, worn head)
    bool give_head = false;               // send "id_assets": the head is given (headassetid = id, headclasscode 0)
};

inline std::string with_commas(int64_t v) {
    std::string s = std::to_string(v < 0 ? -v : v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
    return v < 0 ? "-" + s : s;
}

inline std::string range_text() {
    return with_commas(kMinId) + " to " + with_commas(kMaxId) + " (" + with_commas(pc::kMaxPlayerId) +
           " and up is the game's own range for the players it generates)";
}

inline std::string join(const std::vector<std::string>& v, const char* sep = ", ") {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : sep) + x;
    return s;
}

// text: the box; use: scan(); cat: the app's catalog (may be nullptr); lang: the loaded commentary language ("" unknown);
// typed_off: the kill switch file exists
inline Verdict check(const std::string& text, const Use& use, const assets::Catalog* cat, const std::string& lang, bool typed_off) {
    Verdict v;
    size_t b = 0, e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
    const std::string t = text.substr(b, e - b);
    auto no = [&](std::string why) {
        v.ok = false;
        v.reason = std::move(why);
        return v;
    };
    if (t.empty()) {
        v.ok = true;
        v.automatic = true;
        return v;
    }
    if (typed_off) return no(std::string("typed player ids are switched off (turbo_output\\") + typed_off_name() + "): leave the box empty for an automatic id");
    for (char c : t)
        if (c < '0' || c > '9') return no("\"" + t.substr(0, 20) + "\" is not a whole number: type a player id from " + range_text() + ", or leave the box empty");
    if (t.size() > 9) return no("player id " + t.substr(0, 12) + " is out of range: " + range_text());
    const int64_t id = std::stoll(t);
    v.id = id;
    if (id < kMinId || id > kMaxId) return no("player id " + with_commas(id) + " is out of range: " + range_text());
    if (id == kPlayerCareerId) return no("player id " + std::to_string(id) + " is the player-career player's id: not used for a new player");
    if (!use.read) return no("the database is not read yet: connect to the career first");
    v.owners = use.owners_of(id);
    if (!v.owners.empty())
        return no("player id " + std::to_string(id) + " is in use: " + join(v.owners) + (v.owners.size() == 1 ? " has a row" : " have rows") +
                  " for it (a player of this career has or had this id)");
    for (const char* nt : note_tables())
        if (use.has(nt, id))
            v.notes.push_back(std::string(nt) + " has a row for this id (a player who left the database): without a recording of his own the commentary uses that row's name");
    v.assets = assets_of(cat, use, id, lang);
    const Assets& a = v.assets;
    if (!a.loaded) {
        v.ok = true;
        v.assets_line = "What the game files hold for this id is not checked (" + a.why_not + ").";
        return v;
    }
    if (a.head_generic) return no("player id " + std::to_string(id) + " is a generic head of the game files, not a player's face: choose another id");
    if (a.hidden_off && a.head && !a.head_worn)
        return no("player id " + std::to_string(id) + " carries a 3D head of the game files and hidden assets are switched off (turbo_output\\asset_hidden_off.txt): choose another id");
    const std::string what = a.head && a.callname() ? "a 3D head and a callname" : a.head ? "a 3D head" : a.callname() ? "a callname" : "";
    if (!a.current) {
        v.ok = true;
        v.assets_line = "The asset catalog was built for another game build: what the game files hold for this id is not checked" +
                        (what.empty() ? std::string(".") : " (it lists " + what + "; the new player keeps the template's head).");
        return v;
    }
    v.ok = true;
    v.give_head = a.head;
    if (what.empty()) {
        v.assets_line = std::string("The game files hold no 3D head or callname for this id: he gets the template's head") +
                        (a.miniface ? " (the game files have a miniface p" + std::to_string(id) + ")." : ".");
    } else {
        v.assets_line = "This id has " + what + (a.miniface ? " (and a miniface)" : "") + " in the game files: the new player will " +
                        (a.head && a.callname() ? "use them." : "use it.");
        if (!a.name.empty()) v.assets_line += " Name in the catalog: " + a.name + ".";
    }
    if (a.head && a.head_worn) v.notes.push_back("that head is also worn by a player or manager of this save");
    if (a.head) v.notes.push_back("the head's gender is unknown (no player of this save has it): check that it suits the new player");
    if (a.callname() && !a.call_loaded)
        v.notes.push_back("the callname is recorded in " + join(a.call_langs) + " only; the loaded commentary is " + lang);
    if (a.call_high_only) v.notes.push_back("only a high-intensity recording (the kick-off name call does not use it)");
    return v;
}

// ---------------------------------------------------------------- the "Use a hidden player's id" picker
struct Hidden {
    int64_t id = 0;
    bool head = false, callname = false, miniface = false;
    std::string name, source;  // catalog name ("" = none)
};

// Ids with a hidden 3D head (no row wears it, not generic) or an own callname recording in the loaded language (any
// language when it is unknown), in range, and in no per-player table of the save. Sorted by id. Empty when no catalog is
// loaded or asset_hidden_off.txt exists.
inline std::vector<Hidden> hidden_ids(const assets::Catalog* c, const Use& use, const std::string& lang) {
    std::vector<Hidden> out;
    if (!c || !c->loaded() || c->hidden_off() || !use.read) return out;
    std::map<int64_t, Hidden> m;
    auto ok_id = [&](int64_t id) { return id >= kMinId && id <= kMaxId && id != kPlayerCareerId && use.owners_of(id).empty(); };
    for (int64_t id : c->ids(assets::Family::Head3d))
        if (!c->generic_head(id) && !use.heads.count(id) && ok_id(id)) m[id].head = true;
    const std::vector<std::string> langs = c->callname_languages();
    const bool lang_in = std::find(langs.begin(), langs.end(), lang) != langs.end();
    for (const std::string& l : langs) {
        if (lang_in && l != lang) continue;
        for (const std::string& t : c->callname_teams(l))
            for (assets::CallKind k : {assets::CallKind::Simple, assets::CallKind::Link, assets::CallKind::High})
                for (int64_t id : c->callname_ids(l, t, k))
                    if (ok_id(id)) m[id].callname = true;
    }
    out.reserve(m.size());
    for (auto& [id, h] : m) {
        // an own recording of a generic head's id: the head is not offered (generic), the callname is
        if (!h.head && !h.callname) continue;
        h.id = id;
        h.miniface = c->has(assets::Family::Miniface, id);
        const assets::Name n = c->name(assets::NameKind::Player, id);
        h.name = n.text;
        h.source = n.source;
        out.push_back(std::move(h));
    }
    return out;
}

enum class Want : uint8_t { Any, Head, Callname, Both };
inline const char* want_label(Want w) {
    switch (w) {
        case Want::Head: return "3D head";
        case Want::Callname: return "Callname";
        case Want::Both: return "3D head and callname";
        default: return "3D head or callname";
    }
}

inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The rows the picker shows: the filter, then the search (part of the name or of the id, any letter case)
inline std::vector<const Hidden*> pick(const std::vector<Hidden>& all, Want w, const std::string& search) {
    std::vector<const Hidden*> out;
    const std::string q = lower(search);
    for (const Hidden& h : all) {
        if (w == Want::Head && !h.head) continue;
        if (w == Want::Callname && !h.callname) continue;
        if (w == Want::Both && !(h.head && h.callname)) continue;
        if (!q.empty() && lower(h.name).find(q) == std::string::npos && std::to_string(h.id).find(q) == std::string::npos) continue;
        out.push_back(&h);
    }
    return out;
}

}  // namespace turbo::player_id
