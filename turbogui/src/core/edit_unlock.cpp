#include "edit_unlock.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>

namespace turbo {
namespace eu {

namespace fs = std::filesystem;
using nlohmann::json;

const char* const kCareerSettingsPath = "data/gamesettings/gamesettings_context_Career.json";
const char* const kCareerSetupPath = "data/gamesettings/gamesettings_context_Career_Setup.json";
static const char* const kAvatarPrefix = "data/avatar/avatarcustomizationcfg_";

std::string avatar_path(const std::string& screen) { return kAvatarPrefix + screen + ".json"; }

const std::vector<FileSpec>& files() {
    static const std::vector<FileSpec> v = {
        {avatar_path("managercareer_editplayers"), "Career > Squad > Edit Player", Group::CareerPlayers},
        {avatar_path("managercareer_edit_custom_player"), "Create a Club > squad > edit player", Group::CreatedPlayers},
        {avatar_path("managercareer_edit"), "Edit Manager (your created manager)", Group::Manager},
        {avatar_path("managercareer_edit_retiredreal"), "Edit Manager (a real manager)", Group::Manager},
        {avatar_path("managercareer_create_real"), "New career with a real manager", Group::Manager},
        {avatar_path("mainmenu_edit_real"), "Main menu > Edit Players (real players)", Group::MainMenu},
        {avatar_path("mainmenu_edit_created"), "Main menu > Edit Players (created players)", Group::MainMenu},
        {kCareerSettingsPath, "Career hub > Settings", Group::CareerSettings},
        // read only
        {avatar_path("managercareer_create"), "read: manager name lengths", Group::Source},
        {avatar_path("managercareer_edit_online"), "read: manager outfit picker (experimental)", Group::Source},
        {avatar_path("playercareer_edit_position"), "read: Composure / Defensive awareness (experimental)", Group::Source},
        {kCareerSetupPath, "read: squad settings for the hub (experimental)", Group::Source},
    };
    return v;
}

const FileSpec* find_file(const std::string& path) {
    for (const auto& f : files())
        if (f.path == path) return &f;
    return nullptr;
}

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// "managercareer_editplayers" for an avatar path, "" otherwise
static std::string screen_of(const std::string& path) {
    if (!starts_with(path, kAvatarPrefix) || path.size() < 5 || path.compare(path.size() - 5, 5, ".json") != 0) return "";
    return path.substr(std::string(kAvatarPrefix).size(), path.size() - std::string(kAvatarPrefix).size() - 5);
}

bool write_allowed(const std::string& path) {
    const FileSpec* f = find_file(path);
    if (!f || f->group == Group::Source || !legacy_path::valid(path)) return false;
    std::string base = lower(path.substr(path.rfind('/') + 1));
    for (const char* p : {"avatarcustomizationcfg_", "gamesettings_context_"})
        if (starts_with(base, p)) base = base.substr(std::string(p).size());
    if (base.find("_online") != std::string::npos || base.find("online") == 0) return false;
    for (const char* p : {"managerlive_", "playercareer_", "clubs", "tournament_"})
        if (starts_with(base, p)) return false;
    return true;
}

// ---------------------------------------------------------------- options
bool Options::group_on(Group g) const {
    switch (g) {
        case Group::CareerPlayers: return career_players;
        case Group::CreatedPlayers: return created_players;
        case Group::Manager: return manager;
        case Group::MainMenu: return main_menu;
        case Group::CareerSettings: return career_settings;
        case Group::Source: return false;
    }
    return false;
}

bool Options::file_on(const std::string& path) const {
    const FileSpec* f = find_file(path);
    return f && f->group != Group::Source && group_on(f->group) && !files_off.count(path);
}

std::string Options::signature() const {
    std::string s = "r" + std::to_string(kRecipeVersion) + ";cp" + std::to_string(career_players) + ";cr" +
                    std::to_string(created_players) + ";mg" + std::to_string(manager) + ";mm" + std::to_string(main_menu) +
                    ";cs" + std::to_string(career_settings) + ";x" + std::to_string(experimental) + ";off";
    for (const auto& p : files_off) s += "," + p;
    return s;
}

json Options::to_json() const {
    json j = {{"enabled", enabled}, {"hook", hook},
              {"career_players", career_players}, {"created_players", created_players}, {"manager", manager},
              {"main_menu", main_menu},           {"career_settings", career_settings}, {"experimental", experimental}};
    j["files_off"] = json::array();
    for (const auto& p : files_off) j["files_off"].push_back(p);
    return j;
}

Options Options::from_json(const json& j) {
    Options o;
    if (!j.is_object()) return o;
    auto b = [&](const char* k, bool d) { return j.contains(k) && j[k].is_boolean() ? j[k].get<bool>() : d; };
    o.enabled = b("enabled", o.enabled);
    o.hook = b("hook", o.hook);
    o.career_players = b("career_players", o.career_players);
    o.created_players = b("created_players", o.created_players);
    o.manager = b("manager", o.manager);
    o.main_menu = b("main_menu", o.main_menu);
    o.career_settings = b("career_settings", o.career_settings);
    o.experimental = b("experimental", o.experimental);
    if (j.contains("files_off") && j["files_off"].is_array())
        for (const auto& p : j["files_off"])
            if (p.is_string()) o.files_off.insert(p.get<std::string>());
    return o;
}

Options Options::load(const json& gui_settings) {
    if (!gui_settings.is_object()) return Options();
    auto it = gui_settings.find(kSettingsKey);
    return it != gui_settings.end() ? from_json(*it) : Options();
}

void Options::save(json& gui_settings) const {
    if (!gui_settings.is_object()) gui_settings = json::object();
    json& e = gui_settings[kSettingsKey];
    if (!e.is_object()) e = json::object();
    const json mine = to_json();
    for (const auto& kv : mine.items()) e[kv.key()] = kv.value();
}

edit_unlock::Settings Options::hook_settings() const {
    edit_unlock::Settings h;
    h.enabled = enabled;
    h.experimental = experimental;
    h.hook = hook;
    for (int i = 0; i < edit_unlock::kContextCount; ++i)
        if (!file_on(avatar_path(edit_unlock::context_at(i).name))) h.context_off |= 1u << i;
    return h;
}

std::vector<std::string> sources_for(const std::string& target, const Options& opt) {
    std::vector<std::string> v;
    const std::string s = screen_of(target);
    const bool x = opt.experimental;
    if (s == "managercareer_editplayers") {
        v.push_back(avatar_path("managercareer_edit_custom_player"));
        if (x) v.push_back(avatar_path("managercareer_edit"));
    } else if (s == "managercareer_edit_custom_player") {
        v.push_back(avatar_path("managercareer_editplayers"));
    } else if (s == "managercareer_edit" || s == "managercareer_edit_retiredreal" || s == "managercareer_create_real") {
        v.push_back(avatar_path("managercareer_create"));
        if (x && s != "managercareer_create_real") v.push_back(avatar_path("managercareer_edit_online"));
        if (x && s == "managercareer_edit_retiredreal") v.push_back(avatar_path("managercareer_edit"));
    } else if (target == kCareerSettingsPath) {
        if (x) v.push_back(kCareerSetupPath);
    }
    if (x && (s == "managercareer_editplayers" || s == "managercareer_edit_custom_player" || s == "mainmenu_edit_real" ||
              s == "mainmenu_edit_created"))
        v.push_back(avatar_path("playercareer_edit_position"));
    return v;
}

// ---------------------------------------------------------------- SHA-256 (FIPS 180-4)
namespace {
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t n = 0;
    uint64_t bits = 0;
    static uint32_t rotr(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
            0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
            0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
            0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
            0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
            0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t len) {
        bits += uint64_t(len) * 8;
        while (len > 0) {
            size_t take = std::min(len, sizeof(buf) - n);
            std::copy(p, p + take, buf + n);
            n += take; p += take; len -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    std::string hex() {
        uint64_t total = bits;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (n != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(total >> (56 - 8 * i));
        update(len, 8);
        static const char* x = "0123456789abcdef";
        std::string s;
        for (uint32_t v : h)
            for (int i = 28; i >= 0; i -= 4) s += x[(v >> i) & 15];
        return s;
    }
};
}  // namespace

std::string sha256_hex(const std::string& bytes) {
    Sha256 s;
    s.update(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    return s.hex();
}

// ---------------------------------------------------------------- parsing
std::string repair_missing_brace(const std::string& text) {
    // a dependency-tree entry written as  },<ws>"parents": [...]  (its "{" missing): put the brace back
    std::string out;
    out.reserve(text.size() + 8);
    size_t pos = 0;
    const std::string key = "\"parents\"";
    for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + key.size())) {
        size_t i = at;
        while (i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) --i;
        if (i == 0 || text[i - 1] != ',') continue;
        size_t j = i - 1;
        while (j > 0 && std::isspace(static_cast<unsigned char>(text[j - 1]))) --j;
        if (j == 0 || text[j - 1] != '}') continue;
        out.append(text, pos, at - pos);
        out += '{';
        pos = at;
    }
    out.append(text, pos, std::string::npos);
    return out;
}

bool parse(const std::string& text, ojson& out, bool* repaired, std::string* err) {
    if (repaired) *repaired = false;
    try {
        out = ojson::parse(text);
        return true;
    } catch (const std::exception& e) {
        std::string fixed = repair_missing_brace(text);
        if (fixed != text) {
            try {
                out = ojson::parse(fixed);
                if (repaired) *repaired = true;
                return true;
            } catch (const std::exception&) {
            }
        }
        if (err) *err = e.what();
        return false;
    }
}

// ---------------------------------------------------------------- vocabulary and keep-list
bool known_name(const std::string& n) {
    static const std::set<std::string> k = {
        "ABOUT_ME", "ACCELERATION", "AGGRESSION", "AGILITY", "ANKLE_TAPE", "APPELLATIVE", "ARCHETYPE", "ARCHETYPE_BODY",
        "ARM_SLEEVES", "ATHLETIC", "ATTACK", "ATTRIBUTES", "BALANCE", "BALL_CONTROL", "BIRTH_DAY", "BIRTH_MONTH",
        "BIRTH_YEAR", "BODY", "BODY_TYPE", "BOOT", "BOTTOMS", "BRAND_ANIMATIONS", "COMMENTARY_NAME", "COMPOSURE",
        "CRANIUM_HEAD", "CROSSING", "CURVE", "DEFENDING", "DEFENSIVE_AWARENESS", "DRIBBLING", "FACIALACCESSORY",
        "FINISHING", "FIRST_NAME", "FIXEDOUTFIT", "FIXED_OUTFITS", "FREEKICKSTYLE", "FREE_KICK_ACCURACY", "GAMEPLAY",
        "GEAR", "GENDER", "GK_DIVING", "GK_GLOVES", "GK_HANDLING", "GK_KICKING", "GK_PANTS", "GK_POSITIONING",
        "GK_REFLEXES", "GLOVES", "GLOVES_AND_WRIST", "GOALCELEBRATION", "GOALKEEPER", "HEAD", "HEADING_ACCURACY",
        "HEADWEAR", "HEIGHT", "INFO", "INNER_TOPS", "INTERCEPTIONS", "JERSEY_FIT", "JUMPING", "KIT_FIT", "KIT_NAME",
        "KIT_NUMBER", "KIT_SLEEVES", "KIT_SOCK", "KNOWN_AS", "LEFT_ARM_SLEEVE", "LEFT_WRIST", "LONG_PASSING", "LONG_SHOTS",
        "MATCHDAY", "MENTALITY", "MERGED_TOPS", "MOVEMENT", "NATIONALITY", "OUTER_TOPS", "OUTFITACCESSORY2",
        "OUTFITACCESSORY3", "OUTFITBOTTOMLAYER2", "OUTFITBOTTOMMULTILAYER", "OUTFITSHOE", "OUTFITSOCK", "OUTFITTOPLAYER1",
        "OUTFITTOPLAYER2", "OUTFITTOPMULTILAYER", "PENALTIES", "PENALTYKICKSTYLE", "PLAYING_STYLE", "POSITION",
        "POSITIONING", "POWER", "PREFERRED_FOOT", "PREFERRED_POSITION", "REACTIONS", "RIGHT_ARM_SLEEVE", "RIGHT_WRIST",
        "ROLE", "RUNNINGSTYLE", "SHOE", "SHORT_PASSING", "SHOT_POWER", "SKILL", "SLIDING_TACKLE", "SOCK", "SPRINT_SPEED",
        "STAMINA", "STANDING_TACKLE", "STREET", "STRENGTH", "SURNAME", "TATTOO", "TEAM", "TIES_OR_SCARVES", "TOPS",
        "VISION", "VOLLEYS", "WAIST_FIT", "WEAK_FOOT_ABILITY", "WEIGHT", "WRIST"};
    return k.count(n) > 0;
}

static bool manager_screen(const std::string& s) {
    return s == "managercareer_edit" || s == "managercareer_edit_retiredreal" || s == "managercareer_create_real";
}

std::set<std::string> keep_list(const std::string& target, const Options& opt) {
    std::set<std::string> k;
    const int ci = edit_unlock::context_index(target);
    for (int i = 0; i < edit_unlock::kKeptCount; ++i) {
        const char* n = edit_unlock::kept_at(i).name;
        // the shared table; a file that is not an editor context keeps all four
        if (ci < 0 || edit_unlock::keep_name(n, edit_unlock::context_at(ci), opt.experimental)) k.insert(n);
    }
    return k;
}

static const std::set<std::string> kSettingsKeepLocked = {"CAREER_COMPETITION", "CAREER_CURRENCY", "CAREER_DEEPER_SIMULATION",
                                                          "CAREER_FINANCIAL_TAKEOVER", "CAREER_YOUTH_ACADEMY"};

// ---------------------------------------------------------------- tree helpers (attributeCategories -> filters.data)
static ojson* kids(ojson& node) {
    if (!node.is_object()) return nullptr;
    auto f = node.find("filters");
    if (f == node.end() || !f->is_object()) return nullptr;
    auto d = f->find("data");
    if (d == f->end() || !d->is_array()) return nullptr;
    return &*d;
}

static std::string name_of(const ojson& n) {
    if (!n.is_object()) return "";
    auto it = n.find("name");
    return it != n.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static ojson* top_list(ojson& doc) {
    if (!doc.is_object()) return nullptr;
    auto it = doc.find("attributeCategories");
    return it != doc.end() && it->is_array() ? &*it : nullptr;
}

static ojson* find_in(ojson* list, const std::string& name) {
    if (!list) return nullptr;
    for (auto& c : *list)
        if (name_of(c) == name) return &c;
    return nullptr;
}

static ojson* find_path(ojson& doc, const std::vector<std::string>& path) {
    ojson* list = top_list(doc);
    ojson* node = nullptr;
    for (const auto& p : path) {
        node = find_in(list, p);
        if (!node) return nullptr;
        list = kids(*node);
    }
    return node;
}

// node, path "A/B/C" of names
static void each_node(ojson& list, const std::string& prefix, const std::function<void(ojson&, const std::string&)>& fn) {
    for (auto& n : list) {
        std::string p = prefix.empty() ? name_of(n) : prefix + "/" + name_of(n);
        fn(n, p);
        if (ojson* k = kids(n)) each_node(*k, p, fn);
    }
}
static void each_node(ojson& doc, const std::function<void(ojson&, const std::string&)>& fn) {
    if (ojson* l = top_list(doc)) each_node(*l, "", fn);
}

static const ojson* find_by_name(ojson& doc, const std::string& name) {
    const ojson* hit = nullptr;
    each_node(doc, [&](ojson& n, const std::string&) {
        if (!hit && name_of(n) == name && !kids(n)) hit = &n;
    });
    return hit;
}

static std::set<std::string> node_names(ojson& doc) {
    std::set<std::string> s;
    each_node(doc, [&](ojson& n, const std::string&) { s.insert(name_of(n)); });
    return s;
}

// every string under a "name" key, anywhere (the names an original file uses)
static void collect_names(const ojson& j, std::set<std::string>& out) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "name" && it->is_string()) out.insert(it->get<std::string>());
            collect_names(*it, out);
        }
    } else if (j.is_array()) {
        for (const auto& e : j) {
            if (e.is_string()) out.insert(e.get<std::string>());  // dependency trees list names
            else collect_names(e, out);
        }
    }
}

// isEditable / isVisible false -> true outside the keep-list (only keys already there change). Returns fields changed.
static int unlock(ojson& doc, const std::set<std::string>& keep) {
    int n = 0;
    each_node(doc, [&](ojson& node, const std::string&) {
        if (keep.count(name_of(node))) return;
        bool changed = false;
        for (const char* k : {"isEditable", "isVisible"}) {
            auto it = node.find(k);
            if (it != node.end() && it->is_boolean() && !it->get<bool>()) {
                *it = true;
                changed = true;
            }
        }
        if (changed) ++n;
    });
    return n;
}

// Copy src's node at `path` under the same parent in dst when dst lacks it: 1 copied, 0 already there, -1 not possible
static int graft(ojson& dst, ojson& src, const std::vector<std::string>& path) {
    if (find_path(dst, path)) return 0;
    ojson* s = find_path(src, path);
    if (!s) return -1;
    ojson* parent_list = nullptr;
    if (path.size() == 1) {
        parent_list = top_list(dst);
    } else {
        ojson* parent = find_path(dst, std::vector<std::string>(path.begin(), path.end() - 1));
        if (parent) parent_list = kids(*parent);
    }
    if (!parent_list) return -1;
    parent_list->push_back(*s);
    return 1;
}

// Add `leaf` to the group at `group` (after the sibling named `after`, else last) when the group lacks it
static bool add_leaf(ojson& dst, const std::vector<std::string>& group, const ojson* leaf, const std::string& after) {
    if (!leaf) return false;
    ojson* g = find_path(dst, group);
    ojson* list = g ? kids(*g) : nullptr;
    if (!list) return false;
    const std::string name = name_of(*leaf);
    for (const auto& c : *list)
        if (name_of(c) == name) return false;
    size_t at = list->size();
    for (size_t i = 0; i < list->size(); ++i)
        if (name_of((*list)[i]) == after) at = i + 1;
    list->insert(list->begin() + static_cast<std::ptrdiff_t>(at), *leaf);
    return true;
}

// Entries of src's globalAttributeDependencyTree whose parents all exist in dst; children dst lacks are dropped (an
// entry left without children is skipped); an entry with the same parents gets the missing children. Returns changes.
static int merge_deps(ojson& dst, ojson& src) {
    auto s = src.find("globalAttributeDependencyTree");
    if (s == src.end() || !s->is_array()) return 0;
    std::set<std::string> have = node_names(dst);
    if (!dst.contains("globalAttributeDependencyTree") || !dst["globalAttributeDependencyTree"].is_array())
        dst["globalAttributeDependencyTree"] = ojson::array();
    ojson& d = dst["globalAttributeDependencyTree"];
    auto names = [](const ojson& e, const char* k) {
        std::vector<std::string> v;
        if (e.is_object() && e.contains(k) && e[k].is_array())
            for (const auto& x : e[k])
                if (x.is_string()) v.push_back(x.get<std::string>());
        return v;
    };
    int changes = 0;
    for (const auto& e : *s) {
        std::vector<std::string> parents = names(e, "parents"), children;
        if (parents.empty()) continue;
        bool all = true;
        for (const auto& p : parents) all = all && have.count(p);
        if (!all) continue;
        for (const auto& c : names(e, "children"))
            if (have.count(c)) children.push_back(c);
        if (children.empty()) continue;
        std::set<std::string> pset(parents.begin(), parents.end());
        ojson* same = nullptr;
        for (auto& x : d) {
            std::vector<std::string> xp = names(x, "parents");
            if (std::set<std::string>(xp.begin(), xp.end()) == pset) same = &x;
        }
        if (same) {
            if (!(*same).contains("children") || !(*same)["children"].is_array()) continue;
            std::vector<std::string> xc = names(*same, "children");
            for (const auto& c : children)
                if (std::find(xc.begin(), xc.end(), c) == xc.end()) {
                    (*same)["children"].push_back(c);
                    ++changes;
                }
        } else {
            ojson ne = ojson::object();
            ne["parents"] = parents;
            ne["children"] = children;
            d.push_back(ne);
            ++changes;
        }
    }
    return changes;
}

// minLength / maxLength of text fields dst lacks, from the same field in src (manager names from managercareer_create)
static int copy_name_lengths(ojson& dst, ojson& src) {
    int n = 0;
    each_node(dst, [&](ojson& node, const std::string&) {
        if (kids(node) || node.contains("minLength") || node.contains("maxLength")) return;
        const ojson* s = find_by_name(src, name_of(node));
        if (!s || !s->contains("maxLength")) return;
        if (s->contains("minLength")) node["minLength"] = (*s)["minLength"];
        node["maxLength"] = (*s)["maxLength"];
        ++n;
    });
    return n;
}

static std::string dump(const ojson& j, bool crlf) {
    std::string s = j.dump(1, '\t');
    s += "\n";
    if (!crlf) return s;
    std::string o;
    o.reserve(s.size() + s.size() / 16);
    for (char c : s) {
        if (c == '\n') o += '\r';
        o += c;
    }
    return o;
}

// ---------------------------------------------------------------- the recipe
RecipeResult build(const std::string& target, const std::map<std::string, std::string>& originals, const Options& opt) {
    RecipeResult r;
    if (!write_allowed(target)) {
        r.error = "Turbo never writes this file";
        return r;
    }
    auto it = originals.find(target);
    if (it == originals.end()) {
        r.error = "the game's original is not exported yet";
        return r;
    }
    ojson doc;
    std::string err;
    if (!parse(it->second, doc, &r.repaired, &err)) {
        r.error = "the game's file does not parse (" + err + ")";
        return r;
    }
    if (r.repaired) r.notes.push_back("EA's missing brace repaired");
    auto source = [&](const std::string& p, ojson& out) {
        auto s = originals.find(p);
        bool rep = false;
        return s != originals.end() && parse(s->second, out, &rep, nullptr);
    };
    const bool crlf = it->second.find("\r\n") != std::string::npos;
    const std::set<std::string> keep = keep_list(target, opt);
    char buf[160];

    if (target == kCareerSettingsPath) {
        if (!doc.contains("contexts") || !doc["contexts"].is_array()) {
            r.error = "no contexts list";
            return r;
        }
        ojson* hub = nullptr;
        for (auto& c : doc["contexts"])
            if (name_of(c) == "FROM_CAREER_MANAGER_HUB") hub = &c;
        if (!hub) {
            r.error = "no FROM_CAREER_MANAGER_HUB context";
            return r;
        }
        int n = 0;
        std::function<void(ojson&)> walk = [&](ojson& node) {
            if (node.contains("categories") && node["categories"].is_array())
                for (auto& c : node["categories"]) walk(c);
            if (node.contains("settings") && node["settings"].is_array())
                for (auto& s : node["settings"]) {
                    if (!s.is_object() || kSettingsKeepLocked.count(name_of(s))) continue;
                    auto a = s.find("alwaysLocked");
                    if (a != s.end() && a->is_boolean() && a->get<bool>()) {
                        s.erase("alwaysLocked");
                        ++n;
                    }
                }
        };
        walk(*hub);
        if (n == 0) {
            r.error = "no locked setting to unlock in FROM_CAREER_MANAGER_HUB (not the game's file)";
            return r;
        }
        std::snprintf(buf, sizeof(buf), "%d locked settings unlocked", n);
        r.notes.push_back(buf);
        ojson setup;
        if (opt.experimental && source(kCareerSetupPath, setup) && setup.contains("contexts") && setup["contexts"].is_array() &&
            hub->contains("categories") && (*hub)["categories"].is_array()) {
            // CAREER_SQUAD (edit injuries, edit suspensions, release players) from any context of the setup file
            const ojson* squad = nullptr;
            for (auto& c : setup["contexts"])
                if (!squad && c.contains("categories") && c["categories"].is_array())
                    for (auto& cat : c["categories"])
                        if (!squad && name_of(cat) == "CAREER_SQUAD") squad = &cat;
            ojson& cats = (*hub)["categories"];
            bool there = false;
            size_t at = cats.size();
            for (size_t i = 0; i < cats.size(); ++i) {
                const std::string cn = name_of(cats[i]);
                if (cn == "CAREER_SQUAD") there = true;
                if (cn == "CAREER_TRANSFERS_SCOUTING" && at == cats.size()) at = i;
            }
            if (at == cats.size())  // no transfers and scouting: after training, else last
                for (size_t i = 0; i < cats.size(); ++i)
                    if (name_of(cats[i]) == "CAREER_TRAINING") at = i + 1;
            if (squad && !there) {
                ojson copy = *squad;
                walk(copy);  // the copy's own locks go too
                cats.insert(cats.begin() + static_cast<std::ptrdiff_t>(at), copy);
                r.notes.push_back("squad settings added (experimental)");
            } else if (!squad) {
                r.notes.push_back("squad settings: Career_Setup.json has no CAREER_SQUAD");
            }
        }
    } else {
        ojson* top = top_list(doc);
        if (!top || top->empty()) {
            r.error = "no attributeCategories";
            return r;
        }
        const std::string s = screen_of(target);
        if (s == "managercareer_editplayers") {
            ojson custom;
            if (source(avatar_path("managercareer_edit_custom_player"), custom)) {
                if (graft(doc, custom, {"ATHLETIC", "ATTRIBUTES"}) == 1) r.notes.push_back("Attributes section added");
                if (graft(doc, custom, {"BRAND_ANIMATIONS"}) == 1) r.notes.push_back("Brand animations section added");
                if (int d = merge_deps(doc, custom)) {
                    std::snprintf(buf, sizeof(buf), "%d dependency links merged", d);
                    r.notes.push_back(buf);
                }
            } else {
                r.notes.push_back("Attributes and Brand animations not added (the Create-a-Club player file is not exported)");
            }
            ojson mgr;
            if (opt.experimental && source(avatar_path("managercareer_edit"), mgr) && graft(doc, mgr, {"CRANIUM_HEAD"}) == 1)
                r.notes.push_back("head editor added (experimental)");
        } else if (s == "managercareer_edit_custom_player") {
            ojson ep;
            if (source(avatar_path("managercareer_editplayers"), ep) &&
                add_leaf(doc, {"INFO", "ABOUT_ME"}, find_by_name(ep, "COMMENTARY_NAME"), "KNOWN_AS"))
                r.notes.push_back("commentary name added");
        } else if (manager_screen(s)) {
            // sections first, so the dependency links below can name what they added
            ojson online;
            bool outfits = false;
            if (opt.experimental && s != "managercareer_create_real") {
                if (source(avatar_path("managercareer_edit_online"), online)) {
                    int added = 0;
                    for (const char* g : {"INNER_TOPS", "OUTER_TOPS", "MERGED_TOPS", "BOTTOMS", "SOCK", "SHOE"})
                        if (graft(doc, online, {"GEAR", "MATCHDAY", g}) == 1) ++added;
                    if (added) {
                        outfits = true;
                        std::snprintf(buf, sizeof(buf), "outfit picker added: %d groups (experimental)", added);
                        r.notes.push_back(buf);
                    }
                }
                ojson mgr;
                if (s == "managercareer_edit_retiredreal" && source(avatar_path("managercareer_edit"), mgr) &&
                    graft(doc, mgr, {"CRANIUM_HEAD"}) == 1)
                    r.notes.push_back("head editor added (experimental)");
                if (!keep.count("GENDER")) r.notes.push_back("gender editable (experimental)");
            }
            ojson create;
            int links = 0;
            if (source(avatar_path("managercareer_create"), create)) {
                // the creation limits (2-12, known-as 2-17) fit a created manager only: real names can be longer
                if (int n = s == "managercareer_edit" ? copy_name_lengths(doc, create) : 0) {
                    std::snprintf(buf, sizeof(buf), "%d name lengths set", n);
                    r.notes.push_back(buf);
                }
                links += merge_deps(doc, create);
            }
            if (outfits) links += merge_deps(doc, online);
            if (links) {
                std::snprintf(buf, sizeof(buf), "%d dependency links merged", links);
                r.notes.push_back(buf);
            }
        }
        if (opt.experimental && !manager_screen(s)) {
            ojson pos;
            if (source(avatar_path("playercareer_edit_position"), pos)) {
                int added = 0;
                if (add_leaf(doc, {"ATHLETIC", "ATTRIBUTES", "MENTALITY"}, find_by_name(pos, "COMPOSURE"), "")) ++added;
                if (add_leaf(doc, {"ATHLETIC", "ATTRIBUTES", "DEFENDING"}, find_by_name(pos, "DEFENSIVE_AWARENESS"), "")) ++added;
                if (added) r.notes.push_back("Composure / Defensive awareness added (experimental)");
            }
            int cel = 0;
            each_node(doc, [&](ojson& n, const std::string&) {
                if (name_of(n) == "GOALCELEBRATION" && n.contains("disabledOptions")) {
                    n.erase("disabledOptions");
                    ++cel;
                }
            });
            if (cel) r.notes.push_back("every celebration offered (experimental)");
        }
        int n = unlock(doc, keep);
        std::snprintf(buf, sizeof(buf), "%d fields unlocked", n);
        r.notes.insert(r.notes.begin(), buf);
        // EA bounds every editable BIRTH_YEAR; without minValue / maxValue the game's item init leaves -1..-1
        const bool mgr = manager_screen(s);
        int ranged = 0;
        each_node(doc, [&](ojson& node, const std::string&) {
            if (kids(node) || name_of(node) != "BIRTH_YEAR") return;
            auto ed = node.find("isEditable");
            if (ed != node.end() && !(ed->is_boolean() && ed->get<bool>())) return;
            if (node.contains("minValue") && node.contains("maxValue")) return;
            if (!node.contains("minValue")) node["minValue"] = mgr ? 1930 : 1960;
            if (!node.contains("maxValue")) node["maxValue"] = mgr ? 2010 : 2040;
            ++ranged;
        });
        if (ranged) r.notes.push_back(mgr ? "birth year range 1930-2010 set" : "birth year range 1960-2040 set");
    }

    r.text = dump(doc, crlf);
    std::set<std::string> known;
    for (const auto& kv : originals) {
        ojson o;
        bool rep = false;
        if (parse(kv.second, o, &rep, nullptr)) collect_names(o, known);
    }
    ojson original;
    bool rep = false;
    parse(it->second, original, &rep, nullptr);
    std::string why = validate(target, r.text, original, known, keep);
    if (!why.empty()) {
        r.error = "validation failed: " + why;
        r.text.clear();
        return r;
    }
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------- validation
static std::string check_int(const ojson& n, const char* k, long long lo, long long hi) {
    auto it = n.find(k);
    if (it == n.end()) return "";
    if (!it->is_number_integer()) return std::string(k) + " of " + name_of(n) + " is not a whole number";
    long long v = it->get<long long>();
    if (v < lo || v > hi) return std::string(k) + " of " + name_of(n) + " out of range: " + std::to_string(v);
    return "";
}

std::string validate(const std::string& target, const std::string& out_text, const ojson& original,
                     const std::set<std::string>& extra_known, const std::set<std::string>& keep) {
    if (out_text.size() > kMaxOutputBytes) return "larger than 256 KB";
    ojson out;
    try {
        out = ojson::parse(out_text);
    } catch (const std::exception& e) {
        return std::string("does not parse back: ") + e.what();
    }
    try {
        if (dump(out, false) != dump(ojson::parse(dump(out, false)), false)) return "does not round-trip";
    } catch (const std::exception& e) {
        return std::string("does not round-trip: ") + e.what();
    }
    if (target == kCareerSettingsPath) {
        if (!out.contains("contexts") || !out["contexts"].is_array() || out["contexts"].empty()) return "no contexts";
        bool hub = false;
        for (const auto& c : out["contexts"]) hub = hub || name_of(c) == "FROM_CAREER_MANAGER_HUB";
        if (!hub) return "no FROM_CAREER_MANAGER_HUB context";
        std::set<std::string> names;
        collect_names(out, names);
        for (const auto& n : names)
            if (!extra_known.count(n)) return "unknown name " + n;
        // settings that stay locked: still locked
        std::function<void(const ojson&, std::set<std::string>&)> locked = [&](const ojson& j, std::set<std::string>& s) {
            if (j.is_object()) {
                if (j.contains("alwaysLocked") && j["alwaysLocked"].is_boolean() && j["alwaysLocked"].get<bool>()) s.insert(name_of(j));
                for (const auto& v : j) locked(v, s);
            } else if (j.is_array()) {
                for (const auto& v : j) locked(v, s);
            }
        };
        std::set<std::string> was, now;
        locked(original, was);
        locked(out, now);
        for (const auto& k : kSettingsKeepLocked)
            if (was.count(k) && !now.count(k)) return k + " must stay locked";
        return "";
    }
    ojson* top = top_list(out);
    if (!top || top->empty()) return "attributeCategories is not a non-empty list";
    std::string why;
    std::map<std::string, ojson> keep_flags;  // "<path>#<n>" -> flags of keep-listed fields in the original
    auto flags = [](const ojson& n) {
        ojson f = ojson::object();
        for (const char* k : {"isEditable", "isVisible"})
            if (n.contains(k)) f[k] = n[k];
        return f;
    };
    {
        ojson orig = original;
        std::map<std::string, int> seen;
        each_node(orig, [&](ojson& n, const std::string& p) {
            if (keep.count(name_of(n))) keep_flags[p + "#" + std::to_string(seen[p]++)] = flags(n);
        });
    }
    std::map<std::string, int> seen;
    each_node(out, [&](ojson& n, const std::string& p) {
        if (!why.empty()) return;
        if (!n.is_object()) { why = "a field that is not an object under " + p; return; }
        const std::string name = name_of(n);
        if (name.empty()) { why = "a field without a name under " + p; return; }
        if (!known_name(name) && !extra_known.count(name)) { why = "unknown field " + name; return; }
        if (n.contains("filters") && !kids(n)) { why = "filters of " + name + " has no data list"; return; }
        for (const char* k : {"isEditable", "isVisible", "isMandatory"})
            if (n.contains(k) && !n[k].is_boolean()) { why = std::string(k) + " of " + name + " is not true/false"; return; }
        for (const char* k : {"options", "disabledOptions"})
            if (n.contains(k) && !n[k].is_array()) { why = std::string(k) + " of " + name + " is not a list"; return; }
        for (auto e : {check_int(n, "minValue", -10000000, 10000000), check_int(n, "maxValue", -10000000, 10000000),
                       check_int(n, "minLength", 0, 255), check_int(n, "maxLength", 0, 255)})
            if (!e.empty()) { why = e; return; }
        if (n.contains("minValue") && n.contains("maxValue") && n["minValue"].get<long long>() > n["maxValue"].get<long long>()) {
            why = "minValue above maxValue for " + name;
            return;
        }
        if (n.contains("minLength") && n.contains("maxLength") && n["minLength"].get<long long>() > n["maxLength"].get<long long>()) {
            why = "minLength above maxLength for " + name;
            return;
        }
        if (keep.count(name)) {
            auto o = keep_flags.find(p + "#" + std::to_string(seen[p]++));
            if (o != keep_flags.end() && o->second != flags(n)) why = name + " must stay as the game has it";
        }
    });
    if (!why.empty()) return why;
    for (auto it = out.begin(); it != out.end(); ++it) {
        const std::string& k = it.key();
        if (k.size() < 14 || k.compare(k.size() - 14, 14, "DependencyTree") != 0) continue;
        if (!it->is_array()) return k + " is not a list";
        for (const auto& e : *it) {
            if (!e.is_object()) return k + ": an entry is not an object";
            for (const char* part : {"parents", "children"}) {
                if (!e.contains(part) || !e[part].is_array()) return k + ": an entry has no " + part + " list";
                for (const auto& x : e[part]) {
                    if (!x.is_string()) return k + ": a name is not text";
                    if (!known_name(x.get<std::string>()) && !extra_known.count(x.get<std::string>()))
                        return k + ": unknown name " + x.get<std::string>();
                }
            }
        }
    }
    return "";
}

// ---------------------------------------------------------------- service
static fs::path under(const fs::path& base, const std::string& rel) {
    fs::path p = base;
    size_t start = 0;
    while (start < rel.size()) {
        size_t e = rel.find('/', start);
        if (e == std::string::npos) e = rel.size();
        if (e > start) p /= rel.substr(start, e - start);
        start = e + 1;
    }
    return p;
}

static bool read_all(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool write_atomic(const fs::path& p, const std::string& data, std::string* err) {
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
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) {
            if (err) *err = "writing " + tmp.string() + " failed";
            return false;
        }
    }
    fs::rename(tmp, p, ec);
    if (ec) {
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

static std::string stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tmv);
    return b;
}

static std::string file_name(const std::string& path) { return path.substr(path.rfind('/') + 1); }

EditUnlock::EditUnlock(LegacyImages& legacy, fs::path le_root) : legacy_(legacy), root_(std::move(le_root)) { load_manifest(); }

fs::path EditUnlock::dir() const { return root_ / "turbo_output" / "edit_unlock"; }
fs::path EditUnlock::originals_dir() const { return dir() / "originals"; }
fs::path EditUnlock::manifest_path() const { return dir() / "manifest.json"; }

bool EditUnlock::load_manifest() {
    manifest_ = {{"recipe", kRecipeVersion}, {"originals", json::object()}, {"written", json::object()}, {"turbo_hashes", json::array()}};
    std::error_code ec;
    if (!fs::exists(manifest_path(), ec)) return true;
    std::string text;
    try {
        if (!read_all(manifest_path(), text)) throw std::runtime_error("cannot read it");
        json j = json::parse(text);
        if (!j.is_object()) throw std::runtime_error("not an object");
        for (const char* k : {"originals", "written"})
            if (j.contains(k) && j[k].is_object()) manifest_[k] = j[k];
        if (j.contains("turbo_hashes") && j["turbo_hashes"].is_array()) manifest_["turbo_hashes"] = j["turbo_hashes"];
    } catch (const std::exception& e) {
        // never overwritten: it is the only record of which files Turbo wrote
        manifest_unreadable_ = true;
        manifest_error_ = "turbo_output\\edit_unlock\\manifest.json is unreadable (" + std::string(e.what()) +
                          "): nothing is written or removed until it is fixed or deleted";
        return false;
    }
    return true;
}

bool EditUnlock::save_manifest() {
    if (manifest_unreadable_) return false;
    manifest_["recipe"] = kRecipeVersion;
    std::string err;
    if (!write_atomic(manifest_path(), manifest_.dump(2) + "\n", &err)) {
        manifest_error_ = "manifest not saved: " + err;
        return false;
    }
    manifest_error_.clear();
    return true;
}

std::string EditUnlock::file_sha(const fs::path& p) {
    std::error_code ec;
    uintmax_t size = fs::file_size(p, ec);
    if (ec) return "";
    auto mtime = fs::last_write_time(p, ec);
    if (ec) return "";
    auto& c = sha_cache_[p.string()];
    // the write time may not change between two quick writes (timer granularity): a file written in the last few
    // seconds is always hashed again
    const bool settled = fs::file_time_type::clock::now() - mtime > std::chrono::seconds(3);
    if (!c.sha.empty() && c.size == size && c.mtime == mtime && settled) return c.sha;
    std::string bytes;
    if (!read_all(p, bytes)) return "";
    c.size = size;
    c.mtime = mtime;
    c.sha = sha256_hex(bytes);
    return c.sha;
}

bool EditUnlock::has_original(const std::string& path) const {
    if (!manifest_["originals"].contains(path)) return false;
    std::error_code ec;
    return fs::is_regular_file(under(originals_dir(), path), ec);
}

std::string EditUnlock::read_original(const std::string& path) const {
    std::string s;
    if (!has_original(path)) return s;
    read_all(under(originals_dir(), path), s);
    return s;
}

bool EditUnlock::wrote(const std::string& path) const { return manifest_["written"].contains(path); }
size_t EditUnlock::written_count() const { return manifest_["written"].size(); }

static std::set<std::string> needed(const Options& opt) {
    std::set<std::string> s;
    for (const auto& f : files()) {
        if (!opt.file_on(f.path)) continue;
        s.insert(f.path);
        for (const auto& src : sources_for(f.path, opt)) s.insert(src);
    }
    return s;
}

std::string EditUnlock::reread(const Options& opt) {
    if (manifest_unreadable_) return manifest_error_;
    const std::string restored = restore();
    if (written_count() > 0) return restored + " (the game's files are not re-read while one of Turbo's is still in place)";
    int dropped = 0;
    std::error_code ec;
    for (const auto& path : needed(opt)) {
        if (fs::remove(under(legacy_.cache_dir(), path), ec)) ++dropped;  // Lua exports it again
        fs::remove(under(originals_dir(), path), ec);
        manifest_["originals"].erase(path);
        status_.erase(path);
        source_state_.erase(path);
    }
    save_manifest();
    char b[200];
    std::snprintf(b, sizeof(b), "Game editors: re-reading the game's files (%d cached exports dropped); the unlocked files are "
                  "written again once they are exported", dropped);
    return b;
}

std::vector<std::string> EditUnlock::collect(const Options& opt) {
    std::vector<std::string> lines;
    if (manifest_unreadable_) return lines;
    std::set<std::string> ours;
    for (const auto& h : manifest_["turbo_hashes"])
        if (h.is_string()) ours.insert(h.get<std::string>());
    for (const auto& path : needed(opt)) {
        fs::path cache;
        LegacyImages::State st = legacy_.locate(path, &cache, false);
        const bool have = has_original(path);
        if (st == LegacyImages::State::Game) {
            std::string h = file_sha(cache);
            if (h.empty()) continue;
            const std::string orig = have ? manifest_["originals"][path].value("sha256", "") : "";
            source_state_.erase(path);
            if (h == orig) continue;
            fs::path cur = legacy_.custom_file(path);
            if (ours.count(h) || (!cur.empty() && file_sha(cur) == h)) {
                // the export is an override (Live Editor exports what the game loads): never an original
                if (!have) {
                    status_[path] = {State::Failed, "the game's export returned the custom file in mods\\legacy, not the "
                                                    "game's own: Restore the game's originals, then empty the picture cache (Status "
                                                    "tab) so the game exports it again"};
                    source_state_[path] = "waiting";
                }
                continue;
            }
            if (have && !cur.empty()) continue;  // a custom file is in place: the export cannot be told from it
            std::string bytes, err;
            if (!read_all(cache, bytes) || bytes.empty()) continue;
            if (!write_atomic(under(originals_dir(), path), bytes, &err)) {
                status_[path] = {State::Failed, "original not saved: " + err};
                continue;
            }
            manifest_["originals"][path] = {{"sha256", h}, {"bytes", bytes.size()}, {"saved", stamp()}};
            save_manifest();
            status_.erase(path);
            lines.push_back(have ? "Game editors: the game's " + file_name(path) +
                                       " changed (title update?): new original saved, the unlocked file is rebuilt"
                                 : "Game editors: original saved: " + file_name(path));
        } else if (!have) {
            source_state_[path] = st == LegacyImages::State::Missing ? "missing" : "waiting";
        }
    }
    return lines;
}

bool EditUnlock::ready(const Options& opt) const {
    for (const auto& p : needed(opt)) {
        if (has_original(p)) continue;
        auto it = source_state_.find(p);
        if (it == source_state_.end() || it->second != "missing") return false;
    }
    return true;
}

std::string EditUnlock::input_signature(const Options& opt) const {
    std::string s = opt.signature();
    for (const auto& p : needed(opt)) {
        s += "|" + p + "=";
        if (has_original(p)) s += manifest_["originals"][p].value("sha256", "");
        else {
            auto it = source_state_.find(p);
            s += it == source_state_.end() ? "?" : it->second;
        }
    }
    return s;
}

std::string EditUnlock::apply(const Options& opt) {
    if (manifest_unreadable_) return manifest_error_;
    std::map<std::string, std::string> originals;
    for (const auto& f : files())
        if (has_original(f.path)) originals[f.path] = read_original(f.path);
    int written = 0, current = 0, failed = 0, restored = 0, waiting = 0;
    for (const auto& f : files()) {
        if (f.group == Group::Source) continue;
        const std::string& path = f.path;
        if (!opt.file_on(path)) {
            if (wrote(path)) {
                if (restore_one(path)) ++restored;
            } else {
                status_[path] = {State::Off, "not selected"};
            }
            continue;
        }
        if (!write_allowed(path)) {
            status_[path] = {State::Failed, "Turbo never writes this file"};
            ++failed;
            continue;
        }
        if (!has_original(path)) {
            auto it = source_state_.find(path);
            if (it != source_state_.end() && it->second == "missing")
                status_[path] = {State::Missing, "the game has no such file: nothing to unlock"};
            else if (status_[path].state != State::Failed)
                status_[path] = {State::Waiting, "waiting for the game's export"};
            ++waiting;
            continue;
        }
        std::string wait_for;
        for (const auto& src : sources_for(path, opt)) {
            auto it = source_state_.find(src);
            if (!has_original(src) && !(it != source_state_.end() && it->second == "missing")) wait_for = src;
        }
        if (!wait_for.empty()) {
            status_[path] = {State::Waiting, "waiting for the game's export of " + file_name(wait_for)};
            ++waiting;
            continue;
        }
        RecipeResult r = build(path, originals, opt);
        if (!r.ok) {
            status_[path] = {State::Failed, r.error};
            ++failed;
            continue;
        }
        std::string notes;
        for (const auto& n : r.notes) notes += (notes.empty() ? "" : ", ") + n;
        const std::string h = sha256_hex(r.text);
        fs::path cur = legacy_.custom_file(path);
        std::string cur_sha = cur.empty() ? "" : file_sha(cur);
        const std::string orig_sha = manifest_["originals"][path].value("sha256", "");
        auto record = [&]() {
            manifest_["written"][path] = {{"sha256", h},
                                          {"original_sha256", orig_sha},
                                          {"options", opt.signature()},
                                          {"recipe", kRecipeVersion},
                                          {"written", stamp()}};
            json& th = manifest_["turbo_hashes"];
            bool known = false;
            for (const auto& x : th) known = known || (x.is_string() && x.get<std::string>() == h);
            if (!known) th.push_back(h);
            while (th.size() > 500) th.erase(th.begin());
            save_manifest();
        };
        if (!cur.empty() && cur_sha == h) {
            if (!wrote(path)) record();
            status_[path] = {State::Unlocked, "unlocked (up to date): " + notes};
            ++current;
            continue;
        }
        bool foreign = false;
        if (!cur.empty()) {
            foreign = true;
            for (const auto& x : manifest_["turbo_hashes"]) foreign = foreign && !(x.is_string() && x.get<std::string>() == cur_sha);
        }
        std::string err;
        fs::path bk;
        if (!legacy_.save_custom(path, std::vector<uint8_t>(r.text.begin(), r.text.end()), &err, &bk)) {
            status_[path] = {State::Failed, "not written: " + err};
            ++failed;
            continue;
        }
        sha_cache_.erase(legacy_.custom_file(path).string());
        record();
        status_[path] = {State::Unlocked, "unlocked: " + notes +
                                              (foreign ? "; another tool's custom file was replaced (backed up as " +
                                                             bk.filename().string() + ")"
                                                       : std::string())};
        ++written;
    }
    char buf[200];
    std::snprintf(buf, sizeof(buf), "Game editors: %d written, %d up to date, %d waiting for the game's files, %d restored, %d failed",
                  written, current, waiting, restored, failed);
    return buf;
}

bool EditUnlock::restore_one(const std::string& path) {
    if (!write_allowed(path)) {
        status_[path] = {State::Failed, "not a file Turbo writes: left alone"};
        manifest_["written"].erase(path);
        save_manifest();
        return false;
    }
    const std::string wsha = manifest_["written"][path].value("sha256", "");
    fs::path cur = legacy_.custom_file(path);
    if (cur.empty()) {
        manifest_["written"].erase(path);
        save_manifest();
        status_[path] = {State::Restored, "the game's own file is used (Turbo's file was already gone)"};
        return true;
    }
    if (file_sha(cur) != wsha) {
        manifest_["written"].erase(path);
        save_manifest();
        status_[path] = {State::Kept, "left alone: changed since Turbo wrote it (another tool's file?)"};
        return false;
    }
    std::string err;
    if (!legacy_.remove_custom(path, &err)) {
        status_[path] = {State::Failed, "not removed: " + err};
        return false;
    }
    sha_cache_.erase(cur.string());
    manifest_["written"].erase(path);
    save_manifest();
    status_[path] = {State::Restored, "restored: Turbo's file removed, the game's own is used"};
    return true;
}

std::string EditUnlock::restore() {
    if (manifest_unreadable_) return manifest_error_;
    std::vector<std::string> paths;
    for (auto it = manifest_["written"].begin(); it != manifest_["written"].end(); ++it) paths.push_back(it.key());
    int restored = 0, other = 0;
    for (const auto& p : paths) {
        if (restore_one(p)) ++restored;
        else ++other;
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Game editors: %d files restored to the game's own%s", restored,
                  other ? ", some left alone (see the file list)" : "");
    return buf;
}

EditUnlock::FileStatus EditUnlock::status(const std::string& path) const {
    auto it = status_.find(path);
    if (it != status_.end()) return it->second;
    if (has_original(path)) return {State::Original, "original exported"};
    auto s = source_state_.find(path);
    if (s != source_state_.end())
        return s->second == "missing" ? FileStatus{State::Missing, "the game has no such file"}
                                      : FileStatus{State::Waiting, "waiting for the game's export"};
    return {State::Off, "not needed with these settings"};
}

const char* EditUnlock::state_name(State s) {
    switch (s) {
        case State::Off: return "off";
        case State::Waiting: return "waiting";
        case State::Missing: return "missing";
        case State::Original: return "original exported";
        case State::Unlocked: return "unlocked";
        case State::Restored: return "restored";
        case State::Kept: return "kept";
        case State::Failed: return "failed";
    }
    return "?";
}

}  // namespace eu
}  // namespace turbo
