// FC 27 LE Turbo GUI - CMTracker player library (see cmtracker.h)
#include "core/cmtracker.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/model.h"
#include "ui/playstyles.h"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- text helpers
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string lower_ascii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Accent folding for the Latin letters of player names (U+00C0..U+017F), everything else is lower-cased ASCII or kept
static const char* kFold00C0 =  // U+00C0..U+00FF
    "aaaaaaaceeeeiiii" "dnooooo*ouuuuyts" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
static const char* kFold0100 =  // U+0100..U+017F
    "aaaaaaccccccccdd" "ddeeeeeeeeeegggg" "gggghhhhiiiiiiii" "iiiijjkkklllllll"
    "lllnnnnnnnnnoooo" "oooorrrrrrssssss" "ssttttttuuuuuuuu" "uuuuwwyyyzzzzzzs";

static std::string fold(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out += static_cast<char>(std::tolower(c)); ++i; continue; }
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            if (cp >= 0xC0 && cp <= 0xFF) { out += kFold00C0[cp - 0xC0]; i += 2; continue; }
            if (cp >= 0x100 && cp <= 0x17F) { out += kFold0100[cp - 0x100]; i += 2; continue; }
            out += s.substr(i, 2);
            i += 2;
            continue;
        }
        out += static_cast<char>(c);
        ++i;
    }
    return out;
}

static bool to_int(const std::string& s, int64_t& v) {
    std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    double d = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    v = static_cast<int64_t>(d);
    return true;
}

// ---------------------------------------------------------------- CSV
std::vector<std::vector<std::string>> parse_csv_text(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false;
    size_t i = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    auto push_cell = [&] { row.push_back(cell); cell.clear(); };
    auto push_row = [&] {
        push_cell();
        if (row.size() > 1 || !row[0].empty()) rows.push_back(row);
        row.clear();
    };
    for (; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; ++i; }
                else quoted = false;
            } else cell += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') push_cell();
        else if (c == '\r') {}
        else if (c == '\n') push_row();
        else cell += c;
    }
    if (!cell.empty() || !row.empty()) push_row();
    return rows;
}

// ---------------------------------------------------------------- library
static const std::string& col(const CmtPlayer& p, const char* k) {
    static const std::string none;
    auto it = p.cols.find(k);
    return it == p.cols.end() ? none : it->second;
}

size_t CmtLibrary::add_csv(const std::string& text, const std::string& source, std::string* err) {
    auto rows = parse_csv_text(text);
    if (rows.size() < 2) { if (err) *err = "no player rows"; return 0; }
    std::vector<std::string> header;
    for (auto& h : rows[0]) header.push_back(lower_ascii(trim(h)));
    if (std::find(header.begin(), header.end(), "info.playerid") == header.end() ||
        std::find(header.begin(), header.end(), "info.overallrating") == header.end()) {
        if (err) *err = "not a CMTracker players CSV (no info.playerid / info.overallrating columns)";
        return 0;
    }
    size_t added = 0;
    for (size_t r = 1; r < rows.size(); ++r) {
        CmtPlayer p;
        for (size_t c = 0; c < header.size() && c < rows[r].size(); ++c) p.cols[header[c]] = trim(rows[r][c]);
        if (!to_int(col(p, "info.playerid"), p.id) || p.id <= 0) continue;
        std::string first = col(p, "info.name.firstname"), last = col(p, "info.name.lastname");
        p.name = col(p, "info.name.knownas");
        if (p.name.empty()) p.name = trim(first + " " + last);
        if (p.name.empty()) p.name = "player " + std::to_string(p.id);
        p.club = col(p, "info.teams.club_team.name");
        int64_t v = 0;
        if (to_int(col(p, "info.teams.club_team.id"), v)) p.club_id = v;
        if (to_int(col(p, "info.overallrating"), v)) p.overall = static_cast<int>(v);
        if (to_int(col(p, "info.potential"), v)) p.potential = static_cast<int>(v);
        if (to_int(col(p, "info.age"), v)) p.age = static_cast<int>(v);
        p.position = col(p, "primary_position");
        p.nation = col(p, "info.nation.name");
        // game of the miniface URL: ".../DB/27/heads/p<id>.png"
        const std::string& hs = col(p, "info.headshot");
        size_t k = hs.find("/DB/");
        if (k != std::string::npos && to_int(hs.substr(k + 4, 3), v) && v > 0 && v < 100) p.db_version = static_cast<int>(v);
        p.source = source;
        auto it = by_id_.find(p.id);
        if (it != by_id_.end()) players_[it->second] = std::move(p);
        else { by_id_[p.id] = players_.size(); players_.push_back(std::move(p)); }
        ++added;
    }
    return added;
}

size_t CmtLibrary::load(const fs::path& dir) {
    players_.clear();
    by_id_.clear();
    problems_.clear();
    files_ = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = lower_ascii(it->path().extension().string());
        if (ext != ".csv") continue;
        files.emplace_back(it->last_write_time(ec), it->path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : files) {
        if (fs::file_size(f.second, ec) > 256u * 1024u * 1024u) {
            problems_.push_back(f.second.filename().string() + ": file too large");
            continue;
        }
        std::ifstream in(f.second, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::string err;
        size_t n = add_csv(ss.str(), f.second.filename().string(), &err);
        if (n == 0) problems_.push_back(f.second.filename().string() + ": " + (err.empty() ? "no players" : err));
        else ++files_;
    }
    return players_.size();
}

const CmtPlayer* CmtLibrary::find(int64_t id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &players_[it->second];
}

std::vector<const CmtPlayer*> CmtLibrary::search(const std::string& query, size_t limit) const {
    std::vector<std::string> words;
    std::istringstream ws(fold(query));
    for (std::string w; ws >> w;) words.push_back(w);
    std::vector<std::pair<int, const CmtPlayer*>> hits;
    if (words.empty()) {
        for (auto& p : players_) hits.emplace_back(p.overall, &p);
    } else {
        for (auto& p : players_) {
            std::string hay = fold(p.name + " " + col(p, "info.name.firstname") + " " + col(p, "info.name.lastname") + " " +
                                   p.club + " " + p.nation + " " + p.position);
            bool ok = true;
            int score = p.overall;
            for (auto& w : words) {
                bool all_digits = !w.empty() && std::all_of(w.begin(), w.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
                if (all_digits && std::to_string(p.id) == w) { score += 1000; continue; }
                if (hay.find(w) == std::string::npos) { ok = false; break; }
            }
            if (ok) hits.emplace_back(score, &p);
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const CmtPlayer*> out;
    for (auto& h : hits) {
        if (out.size() >= limit) break;
        out.push_back(h.second);
    }
    return out;
}

// ---------------------------------------------------------------- mapping
std::string cmt_head_url(int64_t id, int db_version) {
    if (id <= 0 || id > 9999999 || db_version < 10 || db_version > 99) return "";
    return "https://cmtracker.fra1.cdn.digitaloceanspaces.com/DB/" + std::to_string(db_version) + "/heads/p" +
           std::to_string(id) + ".png";
}

static std::string squash(const std::string& s) {  // "Game Changer" == "Gamechanger" == "game-changer"
    std::string o;
    for (unsigned char c : s)
        if (std::isalnum(c)) o += static_cast<char>(std::tolower(c));
    return o;
}

int cmt_playstyle_bit(const std::string& name, int* which) {
    std::string key = squash(name);
    if (key.empty()) return -1;
    const auto& a = playstyle1_names();
    for (size_t i = 0; i < a.size(); ++i)
        if (squash(a[i]) == key) { if (which) *which = 1; return static_cast<int>(i); }
    const auto& b = playstyle2_names();
    for (size_t i = 0; i < b.size(); ++i)
        if (squash(b[i]) == key) { if (which) *which = 2; return static_cast<int>(i); }
    return -1;
}

int cmt_position_code(const std::string& name) {
    std::string n = trim(name);
    if (n.empty() || n == "-") return -1;
    for (int k = 0; k < position_count(); ++k)
        if (n == position_name(k)) return k;
    return -1;
}

static bool parse_iso_date(const std::string& s, GameDate& d) {
    if (s.size() < 10 || s[4] != '-' || s[7] != '-') return false;
    d.year = std::atoi(s.substr(0, 4).c_str());
    d.month = std::atoi(s.substr(5, 2).c_str());
    d.day = std::atoi(s.substr(8, 2).c_str());
    return is_real_date(d);
}

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

json cmt_to_preset(const CmtPlayer& p, const CmtPresetOptions& o) {
    json pl = json::object();
    json notes = json::object();
    json left_out = json::array();
    int64_t v = 0;
    auto num = [&](const char* k, int64_t& out) { return to_int(col(p, k), out); };
    auto set_num = [&](const char* field, const char* colname, int64_t add = 0) {
        if (num(colname, v)) pl[field] = v + add;
    };

    // attributes (same names in both; "marking" is defensiveawareness)
    static const char* kAttrs[] = {"acceleration", "sprintspeed", "agility", "balance", "jumping", "stamina", "strength",
        "reactions", "aggression", "composure", "interceptions", "positioning", "vision", "ballcontrol", "crossing", "dribbling",
        "finishing", "freekickaccuracy", "headingaccuracy", "longpassing", "shortpassing", "shotpower", "longshots",
        "standingtackle", "slidingtackle", "volleys", "curve", "penalties", "gkdiving", "gkhandling", "gkkicking",
        "gkreflexes", "gkpositioning"};
    for (const char* a : kAttrs) set_num(a, (std::string("attributes.") + a).c_str());
    {
        std::string c = "attributes.marking";
        if (num(c.c_str(), v)) pl["defensiveawareness"] = v;
    }
    // the six card values: a goalkeeper's are DIV HAN KIC REF SPD POS (speed = the card's PAC), the others PAC SHO PAS DRI DEF PHY
    const bool gk = col(p, "primary_position") == "GK";
    int64_t cpac = 0;
    if (gk) {
        for (auto& f : {std::make_pair("pacdiv", "gkdiving"), std::make_pair("shohan", "gkhandling"), std::make_pair("paskic", "gkkicking"),
                        std::make_pair("driref", "gkreflexes"), std::make_pair("phypos", "gkpositioning")})
            if (pl.contains(f.second)) pl[f.first] = pl[f.second];
        if (num("card_attrs.pac", cpac)) pl["defspe"] = cpac;
    } else {
        set_num("pacdiv", "card_attrs.pac");
        set_num("shohan", "card_attrs.sho");
        set_num("paskic", "card_attrs.pas");
        set_num("driref", "card_attrs.dri");
        set_num("defspe", "card_attrs.def");
        set_num("phypos", "card_attrs.phy");
    }

    // profile
    set_num("overallrating", "info.overallrating");
    set_num("potential", "info.potential");
    set_num("height", "info.height");
    set_num("weight", "info.weight");
    set_num("nationality", "info.nation.id");
    set_num("internationalrep", "info.internationalrep");
    set_num("weakfootabilitytypecode", "info.weafoot");
    set_num("modifier", "info.ovrmodifier");
    if (num("info.skillmoves", v)) pl["skillmoves"] = std::max<int64_t>(0, v - 1);   // CMTracker 1..5 stars, the game 0..4
    if (num("info.release_clause", v)) pl["releaseclause"] = std::min<int64_t>(v, 1000000000);
    pl["preferredfoot"] = col(p, "info.preferredfoot") == "Left" ? 2 : 1;
    pl["gender"] = col(p, "info.gender") == "Female" ? 1 : 0;
    pl["isretiring"] = col(p, "info.isretiring") == "Yes" ? 1 : 0;
    GameDate d;
    if (parse_iso_date(col(p, "info.birthdate"), d)) pl["birthdate"] = gregorian_days_from_date(d);
    if (parse_iso_date(col(p, "info.contract.jointeamdate"), d)) pl["playerjointeamdate"] = gregorian_days_from_date(d);
    if (parse_iso_date(col(p, "info.contract.enddate"), d)) pl["contractvaliduntil"] = d.year;

    // positions: the main one, then the others ("RW | CAM | RM"), the rest -1
    {
        std::vector<int> codes;
        int main_pos = cmt_position_code(col(p, "primary_position"));
        if (main_pos >= 0) codes.push_back(main_pos);
        for (auto& s : split(col(p, "other_positions"), '|')) {
            int c = cmt_position_code(s);
            if (c >= 0 && std::find(codes.begin(), codes.end(), c) == codes.end()) codes.push_back(c);
            else if (c < 0 && !s.empty() && s != "-") left_out.push_back("position " + s);
        }
        for (int i = 0; i < 7; ++i) pl["preferredposition" + std::to_string(i + 1)] = i < static_cast<int>(codes.size()) ? codes[i] : -1;
    }

    // PlayStyles: names in either trait column set the bit of the matching list
    {
        int64_t t1 = 0, t2 = 0;
        for (const char* k : {"info.traits.trait1", "info.traits.trait2"}) {
            for (auto& name : split(col(p, k), ',')) {
                if (name.empty()) continue;
                int which = 0;
                int bit = cmt_playstyle_bit(name, &which);
                if (bit < 0) left_out.push_back("playstyle " + name);
                else if (which == 1) t1 |= (int64_t(1) << bit);
                else t2 |= (int64_t(1) << bit);
            }
        }
        pl["trait1"] = t1;
        pl["trait2"] = t2;
        pl["icontrait1"] = 0;
        pl["icontrait2"] = 0;
    }

    // appearance CMTracker has
    set_num("haircolorcode", "info.haircolor");
    set_num("eyecolorcode", "info.eyecolor");
    set_num("skintonecode", "info.skintone");
    set_num("headtypecode", "info.headtype");
    set_num("bodytypecode", "info.bodytype");

    // what it has not: the defaults FC Editor's own new-player template uses (hair style, brows, kit fit, ...), so the
    // player does not start with zeros; the Players > Appearance tab changes them afterwards
    static const std::pair<const char*, int64_t> kDefaults[] = {
        {"headclasscode", 1}, {"hairtypecode", 16}, {"eyebrowcode", 30301}, {"jerseystylecode", 1}, {"facepsdlayer0", 1},
        {"skinsurfacepack", 223101}, {"hasseasonaljersey", 3}, {"sockstylecode", 1}, {"smallsidedshoetypecode", 500},
        {"emotion", 3}, {"personality", 2}, {"skincomplexion", 1}, {"jerseyfit", 2}, {"growthprofile", 1},
        {"skillmoveslikelihood", 1}, {"skinmakeup", 0}};
    for (auto& d2 : kDefaults)
        if (!pl.contains(d2.first)) pl[d2.first] = d2.second;
    if (!pl.contains("headtypecode")) pl["headtypecode"] = 1;

    // names: the text goes to editedplayernames; "known as" is a common name only when it is not just first + last
    json names = json::object();
    std::string first = col(p, "info.name.firstname"), last = col(p, "info.name.lastname");
    std::string known = col(p, "info.name.knownas");
    names["firstname"] = first;
    names["surname"] = last;
    names["playerjerseyname"] = col(p, "info.name.playerjerseyname");
    if (!known.empty() && lower_ascii(known) != lower_ascii(trim(first + " " + last))) names["commonname"] = known;
    else names["commonname"] = "";

    json preset = {{"format", "turbo-player-preset"}, {"version", 1}, {"name", p.name}, {"playerid", p.id},
                   {"names", names}, {"players", pl}, {"links", json::array()}, {"miniface", nullptr}, {"turbo", "cmtracker"},
                   {"source", "CMTracker CSV (" + p.source + ")"}};
    if (o.teamid > 0) preset["links"].push_back({{"teamid", o.teamid}, {"jerseynumber", o.jersey}, {"national", false}, {"position", 29}});
    notes["playerid"] = p.id;
    notes["db_version"] = p.db_version;
    if (o.real_face && col(p, "info.real_face") == "Yes") notes["real_face_id"] = p.id;
    notes["left_out"] = left_out;
    preset["cmtracker"] = notes;
    return preset;
}

}  // namespace turbo
