// FC 27 LE Turbo GUI - the tactic profile store (see tactic_profiles.h; modelled on reapply.cpp)
#include "tactic_profiles.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace turbo {

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

// ------------------------------------------------------------------------------------------------ categories
const char* category_name(ProfileCategory c) {
    switch (c) {
        case ProfileCategory::Match: return "match";
        case ProfileCategory::Team: return "team";
        case ProfileCategory::Position: return "position";
        case ProfileCategory::Opposition: return "opposition";
        case ProfileCategory::Bundle: return "bundle";
    }
    return "team";
}

bool category_from_name(const std::string& s, ProfileCategory& out) {
    for (ProfileCategory c : {ProfileCategory::Match, ProfileCategory::Team, ProfileCategory::Position, ProfileCategory::Opposition,
                              ProfileCategory::Bundle}) {
        if (s == category_name(c)) {
            out = c;
            return true;
        }
    }
    return false;
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ------------------------------------------------------------------------------------------------ built-in profiles
namespace {

struct Seed {
    const char* key;
    int value;
};

Profile mk(ProfileCategory c, const char* slug, const char* name, const char* note, const char* formation, std::initializer_list<Seed> vals) {
    Profile p;
    p.id = std::string("builtin.") + category_name(c) + "." + slug;
    p.category = c;
    p.name = name;
    p.note = note;
    p.formation = formation;
    p.builtin = true;
    for (const Seed& s : vals) p.sliders.set(s.key, s.value, true);
    return p;
}

std::vector<Profile> make_builtins() {
    using C = ProfileCategory;
    std::vector<Profile> v;
    // match: only the Live values reach the game today; the rest is kept so the preset is complete once RE promotes them
    v.push_back(mk(C::Match, "authentic", "Authentic baseline", "Nothing switched on: the game decides everything.", "", {}));
    v.push_back(mk(C::Match, "tough_referee", "Tough referee", "A strict referee. Applied once the game's referee setting is found.", "",
                   {{"match.card_strictness", 80}, {"match.foul_frequency", 65}}));
    v.push_back(mk(C::Match, "physical", "Physical", "More fouls and more injuries.", "",
                   {{"match.foul_frequency", 70}, {"match.injury_frequency_user", 65}, {"match.injury_frequency_cpu", 65}}));
    v.push_back(mk(C::Match, "fewer_injuries", "Fewer injuries", "Rarer and lighter injuries for both teams.", "",
                   {{"match.injury_frequency_user", 20}, {"match.injury_frequency_cpu", 20}, {"match.injury_severity_user", 20},
                    {"match.injury_severity_cpu", 20}}));
    v.push_back(mk(C::Match, "high_scoring", "High scoring", "More shots, weaker goalkeepers. Applied once the game's shooting settings are found.", "",
                   {{"match.shot_frequency", 70}, {"match.shot_error", 40}, {"match.gk_ability", 35}}));
    // team archetypes, seeded from the familiar roles and duties of football management games
    v.push_back(mk(C::Team, "gegenpress", "Gegenpress", "Win it back at once and attack fast.", "4-3-3",
                   {{"team.mentality", 5}, {"team.line_depth", 75}, {"team.engagement_height", 80}, {"team.press_intensity", 85},
                    {"team.press_trigger", 75}, {"team.counter_press", 85}, {"team.tempo", 75}, {"team.line_length", 75}}));
    v.push_back(mk(C::Team, "low_block", "Low block counter", "Sit deep, then break quickly.", "4-4-2",
                   {{"team.mentality", 1}, {"team.line_depth", 25}, {"team.engagement_height", 25}, {"team.press_intensity", 35},
                    {"team.counter_attack", 80}, {"team.regroup_depth", 80}, {"team.directness", 70}, {"team.line_length", 80}}));
    v.push_back(mk(C::Team, "possession", "Possession", "Keep the ball, move it patiently.", "4-3-3",
                   {{"team.mentality", 4}, {"team.tempo", 40}, {"team.directness", 25}, {"team.buildup_short", 80}, {"team.shot_patience", 70},
                    {"team.fluidity", 65}, {"team.width_att", 60}}));
    v.push_back(mk(C::Team, "wing_play", "Wing play", "Stretch the pitch and cross.", "4-4-2",
                   {{"team.mentality", 4}, {"team.width_att", 85}, {"team.width_def", 60}, {"team.cross_freq", 80}, {"team.forward_runs", 65},
                    {"team.players_in_box", 7}}));
    v.push_back(mk(C::Team, "direct", "Direct", "Go long and go forward.", "4-4-2",
                   {{"team.mentality", 4}, {"team.directness", 80}, {"team.tempo", 70}, {"team.buildup_short", 25}, {"team.forward_runs", 70}}));
    v.push_back(mk(C::Team, "balanced", "Balanced", "Everything in the middle.", "4-2-3-1",
                   {{"team.mentality", 3}, {"team.line_depth", 50}, {"team.press_intensity", 50}, {"team.tempo", 50}, {"team.directness", 50},
                    {"team.width_att", 50}}));
    // position archetypes
    v.push_back(mk(C::Position, "playmaker", "Playmaker", "Dictates the play from deep.", "",
                   {{"pos.attack_bias", 45}, {"pos.risk_passing", 70}, {"pos.roam", 55}, {"pos.close_down", 40}}));
    v.push_back(mk(C::Position, "box_to_box", "Box to box", "Covers both boxes.", "",
                   {{"pos.attack_bias", 55}, {"pos.forward_runs", 65}, {"pos.close_down", 65}, {"pos.tackle_aggr", 60}}));
    v.push_back(mk(C::Position, "poacher", "Poacher", "Lives in the box.", "",
                   {{"pos.attack_bias", 90}, {"pos.shot_freq", 80}, {"pos.dribble_freq", 30}, {"pos.close_down", 30}, {"pos.hold_pos", 70}}));
    v.push_back(mk(C::Position, "anchor", "Anchor", "Shields the back line.", "",
                   {{"pos.attack_bias", 15}, {"pos.hold_pos", 85}, {"pos.roam", 15}, {"pos.mark_tight", 70}, {"pos.tackle_aggr", 60}}));
    v.push_back(mk(C::Position, "winger", "Winger", "Hugs the touchline and delivers.", "",
                   {{"pos.attack_bias", 70}, {"pos.width_bias", 85}, {"pos.dribble_freq", 70}, {"pos.cross_depth", 75}}));
    // opposition: solver presets
    v.push_back(mk(C::Opposition, "off", "No variety", "The opposition is left as the game made it.", "",
                   {{"opp.variety_strength", 0}}));
    v.push_back(mk(C::Opposition, "subtle", "Subtle variety", "Small changes, recognisable teams.", "",
                   {{"opp.variety_strength", 30}, {"opp.jitter", 10}}));
    v.push_back(mk(C::Opposition, "strong", "Strong variety", "Bigger changes, more jitter.", "",
                   {{"opp.variety_strength", 80}, {"opp.jitter", 40}, {"opp.quality_scaling", 60}}));
    return v;
}

// ------------------------------------------------------------------------------------------------ JSON
const std::set<std::string>& profile_known_fields() {
    static const std::set<std::string> k = {"id",     "category",       "name",   "tags",    "note",    "created",
                                            "schema_version", "game_build", "formation", "values", "enabled", "rules"};
    return k;
}

std::string raw_dump(const ojson& j) { return j.dump(-1, ' ', false, ojson::error_handler_t::replace); }

ojson raw_parse(const std::string& s) {
    ojson j = ojson::parse(s, nullptr, false);
    if (j.is_discarded()) return ojson(s);
    return j;
}

ojson profile_to_json(const Profile& p) {
    ojson j = ojson::object();
    j["id"] = p.id;
    j["category"] = category_name(p.category);
    j["name"] = p.name;
    j["tags"] = p.tags;
    j["note"] = p.note;
    j["created"] = p.created;
    j["schema_version"] = p.schema_version;
    j["game_build"] = p.game_build;
    if (!p.formation.empty() || p.category == ProfileCategory::Team || p.category == ProfileCategory::Bundle) j["formation"] = p.formation;
    ojson values = ojson::object();
    for (const auto& kv : p.sliders.values) values[kv.first] = kv.second;
    for (const auto& kv : p.sliders.unknown_values) values[kv.first] = raw_parse(kv.second);
    j["values"] = values;
    ojson en = ojson::array();
    for (const auto& k : p.sliders.enabled) en.push_back(k);
    for (const auto& k : p.sliders.unknown_enabled) en.push_back(k);
    j["enabled"] = en;
    if (!p.rules_json.empty()) j["rules"] = raw_parse(p.rules_json);
    for (const auto& kv : p.extra) j[kv.first] = raw_parse(kv.second);
    return j;
}

std::string str_of(const ojson& o, const char* key) {
    auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// version steps: kMigrations[i] upgrades a profile object from schema i+1 to i+2. Empty while 1 is the only version.
using MigrateFn = void (*)(ojson&);
const std::vector<MigrateFn>& migrations() {
    static const std::vector<MigrateFn> m;
    return m;
}

void migrate_profile(ojson& e, int from) {
    const auto& m = migrations();
    for (int v = from; v < kTacticProfilesVersion; ++v)
        if (v >= 1 && static_cast<size_t>(v - 1) < m.size()) m[static_cast<size_t>(v - 1)](e);
}

bool parse_profile(const ojson& e0, Profile& p, ProfileLoadReport& rep) {
    if (!e0.is_object()) return false;
    ojson e = e0;
    int schema = kTacticProfilesVersion;
    if (auto it = e.find("schema_version"); it != e.end() && it->is_number_integer()) schema = static_cast<int>(it->get<int64_t>());
    if (schema < 1) schema = 1;
    if (schema < kTacticProfilesVersion) {
        migrate_profile(e, schema);
        schema = kTacticProfilesVersion;
    }
    p = Profile{};
    p.id = str_of(e, "id");
    p.name = str_of(e, "name");
    if (p.id.empty() || p.name.empty() || !category_from_name(str_of(e, "category"), p.category)) return false;
    p.note = str_of(e, "note");
    p.created = str_of(e, "created");
    p.game_build = str_of(e, "game_build");
    p.formation = str_of(e, "formation");
    p.schema_version = schema;
    p.locked = schema > kTacticProfilesVersion;
    if (auto it = e.find("tags"); it != e.end() && it->is_array())
        for (const ojson& t : *it)
            if (t.is_string()) p.tags.push_back(t.get<std::string>());
    if (auto it = e.find("values"); it != e.end() && it->is_object()) {
        for (auto v = it->begin(); v != it->end(); ++v) {
            const SliderDef* d = find_slider(v.key());
            if (!d) {
                p.sliders.unknown_values[v.key()] = raw_dump(v.value());
                ++rep.unknown_keys;
                continue;
            }
            if (!v.value().is_number_integer()) {
                ++rep.bad_values;
                continue;
            }
            const int64_t raw = v.value().get<int64_t>();
            const int64_t cl = std::max<int64_t>(-1000000, std::min<int64_t>(1000000, raw));
            const int fixed = clamp_slider(*d, static_cast<int>(cl));
            if (fixed != raw) ++rep.clamped;
            p.sliders.values[v.key()] = fixed;
        }
    }
    if (auto it = e.find("enabled"); it != e.end() && it->is_array()) {
        for (const ojson& k : *it) {
            if (!k.is_string()) continue;
            const std::string key = k.get<std::string>();
            if (const SliderDef* d = find_slider(key)) {
                p.sliders.values.emplace(key, d->def);  // switched on without a stored value: its default
                p.sliders.enabled.insert(key);
            } else {
                p.sliders.unknown_enabled.insert(key);
            }
        }
    }
    if (auto it = e.find("rules"); it != e.end() && !it->is_null()) p.rules_json = raw_dump(*it);
    for (auto f = e.begin(); f != e.end(); ++f)
        if (!profile_known_fields().count(f.key())) p.extra[f.key()] = raw_dump(f.value());
    return true;
}

ojson file_to_json(const std::vector<const Profile*>& which, const std::string& game, const std::string& build,
                   const std::map<std::string, std::string>* active, const std::map<std::string, std::string>* extra) {
    ojson j = ojson::object();
    j["turbo_tactic_profiles"] = kTacticProfilesVersion;
    j["game"] = game;
    j["build"] = build;
    ojson arr = ojson::array();
    for (const Profile* p : which) arr.push_back(profile_to_json(*p));
    j["profiles"] = arr;
    if (active) {
        ojson a = ojson::object();
        for (const auto& kv : *active) a[kv.first] = kv.second;
        j["active"] = a;
    }
    if (extra)
        for (const auto& kv : *extra) j[kv.first] = raw_parse(kv.second);
    return j;
}

}  // namespace

const std::vector<Profile>& builtin_profiles() {
    static const std::vector<Profile> b = make_builtins();
    return b;
}

// ------------------------------------------------------------------------------------------------ the store
const Profile* TacticProfileStore::find(const std::string& id) const {
    for (const Profile& p : profiles)
        if (p.id == id) return &p;
    for (const Profile& p : builtin_profiles())
        if (p.id == id) return &p;
    return nullptr;
}

const Profile* TacticProfileStore::find_by_name(ProfileCategory c, const std::string& name) const {
    const std::string n = lower(name);
    for (const Profile& p : builtin_profiles())
        if (p.category == c && lower(p.name) == n) return &p;
    for (const Profile& p : profiles)
        if (p.category == c && lower(p.name) == n) return &p;
    return nullptr;
}

std::vector<const Profile*> TacticProfileStore::list(ProfileCategory c) const {
    std::vector<const Profile*> out;
    for (const Profile& p : builtin_profiles())
        if (p.category == c) out.push_back(&p);
    for (const Profile& p : profiles)
        if (p.category == c) out.push_back(&p);
    return out;
}

std::string TacticProfileStore::unique_name(ProfileCategory c, const std::string& name, const std::string& except_id) const {
    std::string base = name;
    while (!base.empty() && std::isspace(static_cast<unsigned char>(base.back()))) base.pop_back();
    if (base.empty()) base = "Profile";
    auto taken = [&](const std::string& n) {
        const Profile* p = find_by_name(c, n);
        return p && p->id != except_id;
    };
    if (!taken(base)) return base;
    for (int i = 2; i < 100000; ++i) {
        const std::string cand = base + " (" + std::to_string(i) + ")";
        if (!taken(cand)) return cand;
    }
    return base;
}

std::string TacticProfileStore::make_id(const std::string& seed_text) const {
    uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char ch : seed_text) {
        h ^= ch;
        h *= 1099511628211ULL;
    }
    for (uint64_t n = 0;; ++n) {
        uint64_t x = h + n * 0x9E3779B97F4A7C15ULL;
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ULL;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBULL;
        x ^= x >> 31;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "p%016llx", static_cast<unsigned long long>(x));
        if (!find(buf)) return buf;
    }
}

std::string TacticProfileStore::add(Profile p) {
    if (read_only) return std::string();
    p.builtin = false;
    p.locked = false;
    p.schema_version = kTacticProfilesVersion;
    if (p.id.empty() || p.id.rfind("builtin.", 0) == 0 || find(p.id))
        p.id = make_id(p.name + "|" + p.created + "|" + std::to_string(profiles.size()));
    p.name = unique_name(p.category, p.name);
    profiles.push_back(std::move(p));
    return profiles.back().id;
}

bool TacticProfileStore::overwrite(const std::string& id, const SliderSet& sliders, const std::string& formation) {
    if (read_only) return false;
    for (Profile& p : profiles) {
        if (p.id != id) continue;
        if (p.locked) return false;
        p.sliders = sliders;
        p.formation = formation;
        return true;
    }
    return false;
}

std::string TacticProfileStore::duplicate(const std::string& id, const std::string& new_name, const std::string& created) {
    if (read_only) return std::string();
    const Profile* src = find(id);
    if (!src) return std::string();
    Profile p = *src;
    p.id.clear();
    p.builtin = false;
    p.locked = false;
    p.created = created;
    p.name = new_name.empty() ? src->name + " copy" : new_name;
    return add(std::move(p));
}

bool TacticProfileStore::remove(const std::string& id) {
    if (read_only) return false;
    for (size_t i = 0; i < profiles.size(); ++i) {
        if (profiles[i].id != id) continue;
        if (profiles[i].locked) return false;
        profiles.erase(profiles.begin() + static_cast<std::ptrdiff_t>(i));
        for (auto it = active.begin(); it != active.end();) it = it->second == id ? active.erase(it) : std::next(it);
        return true;
    }
    return false;
}

bool TacticProfileStore::rename(const std::string& id, const std::string& new_name) {
    if (read_only || new_name.empty()) return false;
    for (Profile& p : profiles) {
        if (p.id != id) continue;
        if (p.locked) return false;
        const Profile* clash = find_by_name(p.category, new_name);
        if (clash && clash->id != id) return false;
        p.name = new_name;
        return true;
    }
    return false;
}

bool TacticProfileStore::set_active(ProfileCategory c, const std::string& id) {
    if (read_only) return false;
    const Profile* p = find(id);
    if (!p || p->category != c) return false;
    active[category_name(c)] = id;
    return true;
}

void TacticProfileStore::clear_active(ProfileCategory c) {
    if (!read_only) active.erase(category_name(c));
}

const Profile* TacticProfileStore::active_profile(ProfileCategory c) const {
    auto it = active.find(category_name(c));
    return it == active.end() ? nullptr : find(it->second);
}

// ------------------------------------------------------------------------------------------------ JSON in and out
std::string tactic_profiles_json(const TacticProfileStore& s) {
    std::vector<const Profile*> all;
    for (const Profile& p : s.profiles) all.push_back(&p);
    ojson j = file_to_json(all, s.game, s.build, &s.active, &s.extra);
    return j.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
}

std::string export_profiles_json(const std::vector<const Profile*>& which, const std::string& build) {
    ojson j = file_to_json(which, "fc27", build, nullptr, nullptr);
    return j.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
}

bool parse_tactic_profiles_json(const std::string& text, TacticProfileStore& out, std::string* err, ProfileLoadReport* report) {
    out = TacticProfileStore{};
    ProfileLoadReport rep;
    if (report) *report = rep;
    try {
        ojson j = ojson::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            if (err) *err = "not valid JSON";
            return false;
        }
        auto vit = j.find("turbo_tactic_profiles");
        if (vit == j.end() || !vit->is_number_integer() || vit->get<int64_t>() < 1) {
            if (err) *err = "not a Turbo tactic profile file (no \"turbo_tactic_profiles\": 1)";
            return false;
        }
        const int64_t ver = vit->get<int64_t>();
        out.file_version = static_cast<int>(std::min<int64_t>(ver, 1000000));
        out.read_only = ver > kTacticProfilesVersion;
        if (!str_of(j, "game").empty()) out.game = str_of(j, "game");
        out.build = str_of(j, "build");
        std::set<std::string> ids;
        if (auto it = j.find("profiles"); it != j.end() && it->is_array()) {
            for (const ojson& e : *it) {
                Profile p;
                if (!parse_profile(e, p, rep) || p.id.rfind("builtin.", 0) == 0 || !ids.insert(p.id).second) {
                    ++rep.dropped_profiles;
                    continue;
                }
                if (out.read_only) p.locked = true;
                out.profiles.push_back(std::move(p));
            }
        }
        if (auto it = j.find("active"); it != j.end() && it->is_object())
            for (auto a = it->begin(); a != it->end(); ++a)
                if (a.value().is_string()) out.active[a.key()] = a.value().get<std::string>();
        static const std::set<std::string> known = {"turbo_tactic_profiles", "game", "build", "profiles", "active"};
        for (auto f = j.begin(); f != j.end(); ++f)
            if (!known.count(f.key())) out.extra[f.key()] = raw_dump(f.value());
    } catch (const std::exception& e) {
        out = TacticProfileStore{};
        if (err) *err = std::string("cannot read the profiles: ") + e.what();
        return false;
    }
    if (report) *report = rep;
    return true;
}

// ------------------------------------------------------------------------------------------------ import
bool plan_import(const TacticProfileStore& store, const std::string& text, ImportPlan& plan, std::string* err) {
    plan = ImportPlan{};
    TacticProfileStore in;
    if (!parse_tactic_profiles_json(text, in, err, &plan.report)) return false;
    for (Profile& p : in.profiles) {
        ImportItem it;
        it.incoming = p;
        it.incoming.locked = false;  // a read-only source file still lets its profiles be copied in
        if (const Profile* e = store.find(p.id)) {
            it.clash = ClashKind::SameId;
            it.existing_id = e->id;
            it.existing_builtin = e->builtin;
            it.diff = diff_sliders(e->sliders, p.sliders);
        } else if (p.id.rfind("builtin.", 0) == 0) {
            it.clash = ClashKind::SameId;  // never reached through parse (built-in ids are dropped), kept for safety
        } else if (const Profile* e2 = store.find_by_name(p.category, p.name)) {
            it.clash = ClashKind::SameName;
            it.existing_id = e2->id;
            it.existing_builtin = e2->builtin;
            it.diff = diff_sliders(e2->sliders, p.sliders);
        }
        plan.items.push_back(std::move(it));
    }
    return true;
}

ImportResult apply_import(TacticProfileStore& store, const ImportPlan& plan, const std::vector<ClashAction>& actions) {
    ImportResult r;
    for (size_t i = 0; i < plan.items.size(); ++i) {
        const ImportItem& it = plan.items[i];
        ClashAction act = i < actions.size() ? actions[i] : ClashAction::Rename;
        if (store.read_only || (act == ClashAction::Skip && it.clash != ClashKind::None)) {
            ++r.skipped;
            continue;
        }
        Profile p = it.incoming;
        p.builtin = false;
        if (it.clash == ClashKind::None) {
            if (!store.add(std::move(p)).empty()) ++r.added;
            else ++r.skipped;
            continue;
        }
        if (act == ClashAction::Replace && !it.existing_builtin) {
            bool done = false;
            for (Profile& e : store.profiles) {
                if (e.id != it.existing_id || e.locked) continue;
                p.id = e.id;
                p.name = e.name == p.name ? p.name : store.unique_name(p.category, p.name, e.id);
                p.schema_version = kTacticProfilesVersion;
                e = std::move(p);
                done = true;
                break;
            }
            if (done) {
                ++r.replaced;
                continue;
            }
        }
        // Rename (also the fallback for a built-in or locked profile): a new id and a free name
        p.id.clear();
        if (!store.add(std::move(p)).empty()) ++r.renamed;
        else ++r.skipped;
    }
    return r;
}

// ------------------------------------------------------------------------------------------------ files
fs::path tactic_profiles_path(const fs::path& le_root) { return le_root / "turbo_output" / "tactic_profiles.json"; }

fs::path tactic_profiles_unreadable_path(const fs::path& p) { return p.parent_path() / "tactic_profiles.unreadable.json"; }

bool read_text_file(const fs::path& p, std::string& out, std::string* err) {
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + p.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_text_atomic(const fs::path& p, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp.string();
            return false;
        }
        f << text;
        f.flush();
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    ec.clear();
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

bool load_tactic_profiles(const fs::path& p, TacticProfileStore& out, std::string* err, ProfileLoadReport* report) {
    out = TacticProfileStore{};
    if (report) *report = ProfileLoadReport{};
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::string text;
    if (!read_text_file(p, text, err)) return false;
    std::string why;
    if (!parse_tactic_profiles_json(text, out, &why, report)) {
        if (err) *err = p.filename().string() + ": " + why;
        return false;
    }
    return true;
}

bool save_tactic_profiles(const fs::path& p, const TacticProfileStore& s, std::string* err, bool set_aside) {
    if (s.read_only) {
        if (err) *err = "the profile file was written by a newer Turbo and is read-only here";
        return false;
    }
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (set_aside && fs::exists(p, ec)) {
        const fs::path keep = tactic_profiles_unreadable_path(p);
        fs::remove(keep, ec);
        ec.clear();
        fs::rename(p, keep, ec);
        if (ec) {
            if (err) *err = "cannot set the unreadable " + p.filename().string() + " aside: " + ec.message();
            return false;
        }
    }
    return write_text_atomic(p, tactic_profiles_json(s), err);
}

}  // namespace turbo
