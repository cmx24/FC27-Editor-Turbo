// FC 27 LE Turbo GUI - the store of edits Turbo writes again at every career load (see reapply.h)
#include "reapply.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "callnames.h"
#include "nlohmann/json.hpp"
#include "overall.h"

namespace turbo {

namespace fs = std::filesystem;
using nlohmann::json;

const std::vector<std::string>& kit_colour_prefixes() {
    // ui_identity.cpp team_colours_editor: the kit colour pickers
    static const std::vector<std::string> p = {"teamcolorprim",         "teamcolorsec",          "teamcolortert",
                                               "jerseynamecolor",       "jerseynameoutlinecolor", "jerseynumbercolorprim",
                                               "jerseynumbercolorsec",  "jerseynumbercolorter",  "shortsnumbercolorprim",
                                               "shortsnumbercolorsec",  "shortsnumbercolorter"};
    return p;
}

bool kit_colour_field(const std::string& field) {
    if (field.size() < 2) return false;
    const char ch = field.back();
    if (ch != 'r' && ch != 'g' && ch != 'b') return false;
    const std::string prefix = field.substr(0, field.size() - 1);
    for (const auto& p : kit_colour_prefixes())
        if (p == prefix) return true;
    return false;
}

const std::vector<std::string>& kit_setting_fields() {
    // ui_kits.cpp: the settings.json keys of a FIFA Kit Creator zip that are not colours (core/kit_import.h)
    static const std::vector<std::string> f = {"teamcolorprimpercent", "teamcolorsecpercent", "teamcolortertpercent",
                                               "jerseycollargeometrytype", "jerseynamelayouttype", "shortstyle"};
    return f;
}

const std::vector<std::string>& kit_font_fields() {
    // 2.11 K1 (ui_identity.cpp, core/kit_fonts.h): the name font, number fonts and outline width Teams > Colours writes in
    // teamkits. NOT in kit_setting_fields(): that list is the FIFA Kit Creator settings.json keys the importer reads
    static const std::vector<std::string> f = {"jerseynamefonttype", "numberfonttype", "shortsnumberfonttype", "jerseynameoutlinewidth"};
    return f;
}

bool kit_font_field(const std::string& field) {
    for (const auto& f : kit_font_fields())
        if (f == field) return true;
    return false;
}

bool kit_setting_field(const std::string& field) {
    for (const auto& f : kit_setting_fields())
        if (f == field) return true;
    return kit_font_field(field);
}

bool kit_kept_field(const std::string& field) { return kit_colour_field(field) || kit_setting_field(field); }

// ---------------------------------------------------------------- 2.15 scopes and the career identity
const char* const kScopeGlobal = "global";
static const std::string kCareerPrefix = "career:";

std::string career_scope(const std::string& career_id) { return career_id.empty() ? std::string() : kCareerPrefix + career_id; }

std::string scope_career(const std::string& scope) {
    return scope.size() > kCareerPrefix.size() && scope.compare(0, kCareerPrefix.size(), kCareerPrefix) == 0 ? scope.substr(kCareerPrefix.size())
                                                                                                             : std::string();
}

ScopeKind scope_kind(const std::string& scope) {
    if (scope.empty()) return ScopeKind::NotAssigned;
    if (scope == kScopeGlobal) return ScopeKind::Global;
    if (!scope_career(scope).empty()) return ScopeKind::Career;
    return ScopeKind::Unknown;
}

int scope_rank(const std::string& scope, const std::string& career_id) {
    switch (scope_kind(scope)) {
        case ScopeKind::NotAssigned: return 1;
        case ScopeKind::Global: return 2;
        case ScopeKind::Career: return !career_id.empty() && scope_career(scope) == career_id ? 3 : 0;
        default: return 0;
    }
}

uint64_t fnv1a64(const std::string& bytes) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

std::string career_identity_text(const CareerManagerFacts& m, bool has_start, int64_t startdate) {
    return "turbo-career-1|m=" + std::to_string(m.managerid) + "|f=" + m.firstname + "|s=" + m.surname + "|c=" + m.commonname +
           "|b=" + std::to_string(m.birthdate) + "|n=" + std::to_string(m.nationality) + "|start=" + (has_start ? std::to_string(startdate) : "-");
}

std::string career_identity_id(const CareerManagerFacts& m, bool has_start, int64_t startdate) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(fnv1a64(career_identity_text(m, has_start, startdate))));
    return buf;
}

std::string ReapplyStore::scope_label(const std::string& scope) const {
    switch (scope_kind(scope)) {
        case ScopeKind::NotAssigned: return "not assigned yet (every career)";
        case ScopeKind::Global: return "global (every career)";
        case ScopeKind::Career: {
            const std::string id = scope_career(scope);
            auto it = careers.find(id);
            const std::string name = it != careers.end() && !it->second.label.empty() ? it->second.label : "id " + id;
            return id == career ? "this career (" + name + ")" : "only career " + name;
        }
        default: return "unknown scope '" + scope + "' (not applied)";
    }
}

static bool movable_to(const std::string& to) { return scope_kind(to) != ScopeKind::Unknown; }

void ReapplyStore::set_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& team, const std::string& when,
                                 const std::string& field, int64_t value) {
    set_kit_field_in(edit_scope(), teamtechid, kittype, teamkitid, team, when, field, value);
}

void ReapplyStore::set_kit_field_in(const std::string& scope, int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& team,
                                    const std::string& when, const std::string& field, int64_t value) {
    if (teamkitid < 0) teamkitid = -1;
    KitEdit& k = kits[KitKey{teamtechid, kittype, teamkitid, scope}];
    k.teamtechid = teamtechid;
    k.kittype = kittype;
    k.teamkitid = teamkitid;
    k.scope = scope;
    if (!team.empty()) k.team = team;
    if (!when.empty()) k.when = when;
    k.fields[field] = value;
}

bool ReapplyStore::forget_kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) {
    const KitEdit* k = kit(teamtechid, kittype, teamkitid);
    if (!k) return false;
    return kits.erase(KitKey{k->teamtechid, k->kittype, k->teamkitid, k->scope}) > 0;
}

bool ReapplyStore::forget_kit_field(int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field) {
    return forget_kit_field_in(edit_scope(), teamtechid, kittype, teamkitid, field);
}

bool ReapplyStore::forget_kit_field_in(const std::string& scope, int64_t teamtechid, int64_t kittype, int64_t teamkitid, const std::string& field) {
    auto it = kits.find(KitKey{teamtechid, kittype, teamkitid < 0 ? -1 : teamkitid, scope});
    if (it == kits.end() || !it->second.fields.erase(field)) return false;
    if (it->second.fields.empty()) kits.erase(it);
    return true;
}

const KitEdit* ReapplyStore::kit(int64_t teamtechid, int64_t kittype, int64_t teamkitid) const {
    const int64_t want = teamkitid < 0 ? -1 : teamkitid;
    const KitEdit* best = nullptr;
    int best_rank = 0;
    auto consider = [&](int64_t id) {
        for (const auto& kv : kits) {
            const KitEdit& k = kv.second;
            if (k.teamtechid != teamtechid || k.kittype != kittype || k.teamkitid != id) continue;
            const int r = rank(k.scope);
            if (r > best_rank) {
                best_rank = r;
                best = &k;
            }
        }
    };
    consider(want);
    if (!best && want >= 0) consider(-1);
    return best;
}

const KitEdit* ReapplyStore::kit_in(const std::string& scope, int64_t teamtechid, int64_t kittype, int64_t teamkitid) const {
    auto it = kits.find(KitKey{teamtechid, kittype, teamkitid < 0 ? -1 : teamkitid, scope});
    return it == kits.end() ? nullptr : &it->second;
}

size_t ReapplyStore::kits_of_type(int64_t teamtechid, int64_t kittype) const {
    std::set<int64_t> ids;
    for (const auto& kv : kits)
        if (kv.second.teamtechid == teamtechid && kv.second.kittype == kittype && rank(kv.second.scope) > 0) ids.insert(kv.second.teamkitid);
    return ids.size();
}

bool ReapplyStore::move_kit(const KitKey& key, const std::string& to) {
    auto it = kits.find(key);
    if (it == kits.end() || std::get<3>(key) == to || !movable_to(to)) return false;
    KitEdit moved = it->second;
    kits.erase(it);
    moved.scope = to;
    KitEdit& dst = kits[KitKey{std::get<0>(key), std::get<1>(key), std::get<2>(key), to}];
    if (dst.fields.empty()) {
        dst = moved;
    } else {
        for (const auto& f : moved.fields) dst.fields[f.first] = f.second;
        if (!moved.team.empty()) dst.team = moved.team;
        if (!moved.when.empty()) dst.when = moved.when;
    }
    return true;
}

void ReapplyStore::set_callname(int64_t playerid, int64_t commentaryid, const std::string& player, const std::string& from, const std::string& when) {
    set_callname_in(edit_scope(), playerid, commentaryid, player, from, when);
}

void ReapplyStore::set_callname_in(const std::string& scope, int64_t playerid, int64_t commentaryid, const std::string& player,
                                   const std::string& from, const std::string& when) {
    CallnameEdit& c = callnames[RowKey{playerid, scope}];
    c.playerid = playerid;
    c.commentaryid = commentaryid;
    c.player = player;
    c.from = from;
    c.when = when;
    c.scope = scope;
}

bool ReapplyStore::forget_callname(int64_t playerid) {
    const CallnameEdit* c = callname(playerid);
    return c && callnames.erase(RowKey{c->playerid, c->scope}) > 0;
}

bool ReapplyStore::move_callname(const RowKey& key, const std::string& to) {
    auto it = callnames.find(key);
    if (it == callnames.end() || key.second == to || !movable_to(to)) return false;
    CallnameEdit moved = it->second;
    callnames.erase(it);
    moved.scope = to;
    callnames[RowKey{key.first, to}] = moved;
    return true;
}

bool plan_field(const std::string& field) {
    if (overall::attr_index(field) >= 0) return true;
    return field == "attackingworkrate" || field == "defensiveworkrate" || field == "skillmoves" || field == "weakfootabilitytypecode";
}

bool kept_player_field(const std::string& field) { return plan_field(field) || field == "overallrating" || field == "potential"; }

KeptField kept_field_state(const PlayerFieldEdit& e, int64_t loaded) {
    if (loaded == e.value) return KeptField::InPlace;
    if (loaded == e.was) return KeptField::Write;
    return KeptField::Moved;
}

bool ReapplyStore::note_player_edit(int64_t playerid, const std::string& player, const std::string& when, const std::string& field,
                                    int64_t before, int64_t after) {
    if (playerid <= 0 || before == after || !kept_player_field(field)) return false;
    const std::string scope = edit_scope();
    auto pit = players.find(RowKey{playerid, scope});
    if (pit != players.end()) {
        auto fit = pit->second.fields.find(field);
        // the edit continues from the kept value: the value the game might put back is still the first one replaced
        if (fit != pit->second.fields.end() && fit->second.value == before) {
            if (after == fit->second.was) {
                // back where it was: nothing left to keep for this field
                pit->second.fields.erase(fit);
                if (pit->second.fields.empty()) players.erase(pit);
                return true;
            }
            fit->second.value = after;
            if (!player.empty()) pit->second.player = player;
            if (!when.empty()) pit->second.when = when;
            return true;
        }
    }
    // another row in effect here holds the field at `before` (an unassigned or global row): the edit continues from it, so
    // the value the game might put back is that row's replaced value. That row itself is never changed from here.
    int64_t was = before;
    {
        const PlayerEdit merged = player_in_effect(playerid);
        auto f = merged.fields.find(field);
        if (f != merged.fields.end() && f->second.value == before) was = f->second.was;
    }
    if (after == was) return false;  // back to the value the other row replaced: nothing to keep in this scope
    PlayerEdit& e = players[RowKey{playerid, scope}];
    e.playerid = playerid;
    e.scope = scope;
    if (!player.empty()) e.player = player;
    if (!when.empty()) e.when = when;
    e.fields[field] = PlayerFieldEdit{after, was};
    return true;
}

bool ReapplyStore::forget_player_field(int64_t playerid, const std::string& field) {
    auto best = players.end();
    int best_rank = 0;
    for (auto it = players.lower_bound(RowKey{playerid, std::string()}); it != players.end() && it->first.first == playerid; ++it) {
        const int r = rank(it->first.second);
        if (r > best_rank && it->second.fields.count(field)) {
            best_rank = r;
            best = it;
        }
    }
    if (best == players.end()) return false;
    best->second.fields.erase(field);
    if (best->second.fields.empty()) players.erase(best);
    return true;
}

bool ReapplyStore::forget_player(int64_t playerid) {
    bool any = false;
    for (auto it = players.lower_bound(RowKey{playerid, std::string()}); it != players.end() && it->first.first == playerid;) {
        if (rank(it->first.second) > 0) {
            it = players.erase(it);
            any = true;
        } else {
            ++it;
        }
    }
    return any;
}

const PlayerEdit* ReapplyStore::player(int64_t playerid) const {
    const PlayerEdit* best = nullptr;
    int best_rank = 0;
    for (auto it = players.lower_bound(RowKey{playerid, std::string()}); it != players.end() && it->first.first == playerid; ++it) {
        const int r = rank(it->first.second);
        if (r > best_rank) {
            best_rank = r;
            best = &it->second;
        }
    }
    return best;
}

PlayerEdit ReapplyStore::player_in_effect(int64_t playerid) const {
    PlayerEdit out;
    out.playerid = playerid;
    // rank 1, 2, 3 in turn: a higher rank overwrites a field a lower one holds
    for (int want = 1; want <= 3; ++want) {
        for (auto it = players.lower_bound(RowKey{playerid, std::string()}); it != players.end() && it->first.first == playerid; ++it) {
            if (rank(it->first.second) != want) continue;
            for (const auto& f : it->second.fields) out.fields[f.first] = f.second;
            if (!it->second.player.empty()) out.player = it->second.player;
            if (!it->second.when.empty()) out.when = it->second.when;
            out.scope = it->second.scope;
        }
    }
    return out;
}

size_t ReapplyStore::player_fields() const {
    size_t n = 0;
    for (const auto& kv : players) n += kv.second.fields.size();
    return n;
}

const CallnameEdit* ReapplyStore::callname(int64_t playerid) const {
    const CallnameEdit* best = nullptr;
    int best_rank = 0;
    for (auto it = callnames.lower_bound(RowKey{playerid, std::string()}); it != callnames.end() && it->first.first == playerid; ++it) {
        const int r = rank(it->first.second);
        if (r > best_rank) {
            best_rank = r;
            best = &it->second;
        }
    }
    return best;
}

bool ReapplyStore::move_player(const RowKey& key, const std::string& to) {
    auto it = players.find(key);
    if (it == players.end() || key.second == to || !movable_to(to)) return false;
    PlayerEdit moved = it->second;
    players.erase(it);
    moved.scope = to;
    PlayerEdit& dst = players[RowKey{key.first, to}];
    if (dst.fields.empty()) {
        dst = moved;
    } else {
        for (const auto& f : moved.fields) dst.fields[f.first] = f.second;
        if (!moved.player.empty()) dst.player = moved.player;
        if (!moved.when.empty()) dst.when = moved.when;
    }
    return true;
}

std::string reapply_json(const ReapplyStore& s) {
    json j = json::object();
    j["turbo_reapply"] = 1;
    j["note"] = "Edits FC 27 forgets when a career loads (it reloads teamkits and playernamemap from its base data; your players' "
                "development plans outrank the players table). Turbo writes them again when it connects to a newly loaded career, "
                "a player field only while it holds the value the edit replaced. Forget an entry in Teams > Colours, Players > "
                "Callname, Teams > Kits or Turbo Tools > Kept player edits.";
    json kits = json::array();
    for (const auto& kv : s.kits) {
        const KitEdit& k = kv.second;
        json fields = json::object();
        for (const auto& f : k.fields) fields[f.first] = f.second;
        json row = {{"teamtechid", k.teamtechid}, {"teamkittypetechid", k.kittype}, {"teamkitid", k.teamkitid},
                    {"team", k.team}, {"when", k.when}, {"fields", fields}};
        if (!k.scope.empty()) row["scope"] = k.scope;  // a row not assigned is written as an older Turbo wrote it
        kits.push_back(row);
    }
    j["kits"] = kits;
    json names = json::array();
    for (const auto& kv : s.callnames) {
        const CallnameEdit& c = kv.second;
        json row = {{"playerid", c.playerid}, {"commentaryid", c.commentaryid}, {"player", c.player}, {"from", c.from}, {"when", c.when}};
        if (!c.scope.empty()) row["scope"] = c.scope;
        names.push_back(row);
    }
    j["playernamemap"] = names;
    j["keep_players"] = s.keep_players;
    json players = json::array();
    for (const auto& kv : s.players) {
        const PlayerEdit& p = kv.second;
        json fields = json::object();
        for (const auto& f : p.fields) fields[f.first] = {{"value", f.second.value}, {"was", f.second.was}};
        json row = {{"playerid", p.playerid}, {"player", p.player}, {"when", p.when}, {"fields", fields}};
        if (!p.scope.empty()) row["scope"] = p.scope;
        players.push_back(row);
    }
    j["players"] = players;
    if (!s.careers.empty()) {
        json careers = json::object();
        for (const auto& kv : s.careers) careers[kv.first] = {{"label", kv.second.label}, {"seen", kv.second.seen}};
        j["careers"] = careers;
    }
    // error_handler::replace: a name with broken UTF-8 (a player name read from memory) must not throw
    return j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

static bool int_of(const json& o, const char* key, int64_t& out) {
    auto it = o.find(key);
    if (it == o.end() || !it->is_number_integer()) return false;
    out = it->get<int64_t>();
    return true;
}

static std::string str_of(const json& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// 2.15: absent / null / "" = not assigned; a string as it is; anything else as its JSON text (an unknown scope: kept, never
// applied, never dropped)
static std::string scope_of(const json& o) {
    auto it = o.find("scope");
    if (it == o.end() || it->is_null()) return std::string();
    if (it->is_string()) return it->get<std::string>();
    return it->dump(-1, ' ', false, json::error_handler_t::replace);
}

bool parse_reapply_json(const std::string& text, ReapplyStore& out, std::string* err, size_t* dropped) {
    const std::string career = out.career;  // runtime, never in the file: kept
    out = ReapplyStore{};
    out.career = career;
    if (dropped) *dropped = 0;
    size_t bad = 0;
    try {
        json j = json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        int64_t ver = 0;
        if (!int_of(j, "turbo_reapply", ver) || ver != 1) {
            if (err) *err = "not a Turbo re-apply store (no \"turbo_reapply\": 1)";
            return false;
        }
        if (auto it = j.find("kits"); it != j.end() && it->is_array()) {
            for (const json& e : *it) {
                int64_t tid = 0, type = 0, kitid = -1;
                if (!e.is_object() || !int_of(e, "teamtechid", tid) || !int_of(e, "teamkittypetechid", type) || tid <= 0 || type < 0) {
                    ++bad;
                    continue;
                }
                if (!int_of(e, "teamkitid", kitid)) kitid = -1;
                auto fi = e.find("fields");
                if (fi == e.end() || !fi->is_object() || fi->empty()) {
                    ++bad;
                    continue;
                }
                for (auto f = fi->begin(); f != fi->end(); ++f) {
                    // only the colour channels Teams > Colours writes and the settings Teams > Kits writes: a
                    // hand-edited key column (teamtechid, teamkitid, ...) or any other int is never written back at a
                    // career load
                    if (!kit_kept_field(f.key()) || !f.value().is_number_integer()) {
                        ++bad;
                        continue;
                    }
                    out.set_kit_field_in(scope_of(e), tid, type, kitid, str_of(e, "team"), str_of(e, "when"), f.key(), f.value().get<int64_t>());
                }
            }
        }
        if (auto it = j.find("playernamemap"); it != j.end() && it->is_array()) {
            for (const json& e : *it) {
                int64_t pid = 0, cid = 0;
                // the ids every commentary bank uses for names; 900000 is "no callname" and never a kept edit
                if (!e.is_object() || !int_of(e, "playerid", pid) || !int_of(e, "commentaryid", cid) || pid <= 0 || cid <= kNoCallname ||
                    cid > kCallnameMax) {
                    ++bad;
                    continue;
                }
                out.set_callname_in(scope_of(e), pid, cid, str_of(e, "player"), str_of(e, "from"), str_of(e, "when"));
            }
        }
        if (auto it = j.find("keep_players"); it != j.end() && it->is_boolean()) out.keep_players = it->get<bool>();
        if (auto it = j.find("players"); it != j.end() && it->is_array()) {
            for (const json& e : *it) {
                int64_t pid = 0;
                auto fi = e.is_object() ? e.find("fields") : e.end();
                if (!e.is_object() || !int_of(e, "playerid", pid) || pid <= 0 || fi == e.end() || !fi->is_object() || fi->empty()) {
                    ++bad;
                    continue;
                }
                for (auto f = fi->begin(); f != fi->end(); ++f) {
                    int64_t value = 0, was = 0;
                    // only the fields the Players editor keeps: a hand-edited key column (playerid, ...) or any other
                    // players field is never written back at a career load
                    if (!kept_player_field(f.key()) || !f.value().is_object() || !int_of(f.value(), "value", value) ||
                        !int_of(f.value(), "was", was) || value == was) {
                        ++bad;
                        continue;
                    }
                    const std::string scope = scope_of(e);
                    PlayerEdit& p = out.players[ReapplyStore::RowKey{pid, scope}];
                    p.playerid = pid;
                    p.scope = scope;
                    p.player = str_of(e, "player");
                    p.when = str_of(e, "when");
                    p.fields[f.key()] = PlayerFieldEdit{value, was};
                }
            }
        }
        if (auto it = j.find("careers"); it != j.end() && it->is_object()) {
            for (auto c = it->begin(); c != it->end(); ++c) {
                if (c.key().empty() || !c.value().is_object()) continue;
                out.careers[c.key()] = ReapplyStore::CareerInfo{str_of(c.value(), "label"), str_of(c.value(), "seen")};
            }
        }
    } catch (const std::exception& e) {
        out = ReapplyStore{};
        out.career = career;
        if (err) *err = std::string("cannot read the store: ") + e.what();
        return false;
    }
    if (dropped) *dropped = bad;
    return true;
}

fs::path reapply_store_path(const fs::path& le_root) { return le_root / "turbo_output" / "reapply_edits.json"; }

fs::path reapply_unreadable_path(const fs::path& p) { return p.parent_path() / "reapply_edits.unreadable.json"; }

bool load_reapply_store(const fs::path& p, ReapplyStore& out, std::string* err, size_t* dropped) {
    const std::string career = out.career;  // runtime, never in the file: kept
    out = ReapplyStore{};
    out.career = career;
    if (dropped) *dropped = 0;
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + p.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string why;
    if (!parse_reapply_json(ss.str(), out, &why, dropped)) {
        if (err) *err = p.filename().string() + ": " + why;
        return false;
    }
    return true;
}

bool save_reapply_store(const fs::path& p, const ReapplyStore& s, std::string* err, bool set_aside) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (set_aside && fs::exists(p, ec)) {
        const fs::path keep = reapply_unreadable_path(p);
        fs::remove(keep, ec);
        ec.clear();
        fs::rename(p, keep, ec);
        if (ec) {
            if (err) *err = "cannot set the unreadable " + p.filename().string() + " aside: " + ec.message();
            return false;
        }
    }
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << reapply_json(s);
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        // Windows: a rename over a file another program holds open can fail; replace in two steps
        ec.clear();
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (err) *err = "cannot replace " + p.string();
            return false;
        }
    }
    return true;
}

}  // namespace turbo
