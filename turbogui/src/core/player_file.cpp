// FC 27 LE Turbo GUI - ".player" files (see player_file.h)
#include "core/player_file.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "core/model.h"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

const std::vector<PlayersFieldRange>& fc27_players_fields() {
    static const std::vector<PlayersFieldRange> k = {
    {"acceleration", 1, 128}, {"accessorycode1", 0, 2047}, {"accessorycode2", 0, 2047}, {"accessorycode3", 0, 2047},
    {"accessorycode4", 0, 2047}, {"accessorycolourcode1", 0, 255}, {"accessorycolourcode2", 0, 255}, {"accessorycolourcode3", 0, 255},
    {"accessorycolourcode4", 0, 255}, {"aggression", 1, 128}, {"agility", 1, 128}, {"animfreekickstartposcode", 0, 31},
    {"animpenaltiesstartposcode", 0, 31}, {"avatarpomid", 0, 511}, {"balance", 1, 128}, {"ballcontrol", 1, 128}, {"birthdate", 0, 1048575},
    {"bodytypecode", 1, 512}, {"commonnameid", 0, 65535}, {"composure", 1, 128}, {"contractvaliduntil", 2000, 2127}, {"crossing", 1, 128},
    {"curve", 1, 128}, {"defensiveawareness", 1, 128}, {"defspe", 0, 127}, {"dribbling", 1, 128}, {"driref", 0, 127}, {"emotion", 1, 8},
    {"eyebrowcode", 0, 16777215}, {"eyecolorcode", 1, 64}, {"eyedetail", 0, 31}, {"faceposerpreset", 0, 15}, {"facepsdlayer0", 0, 15},
    {"facepsdlayer1", 0, 15}, {"facialhaircolorcode", 0, 63}, {"facialhairtypecode", 0, 1023}, {"finishing", 1, 128},
    {"finishingcode1", 0, 131071}, {"finishingcode2", 0, 131071}, {"firstnameid", 0, 65535}, {"freekickaccuracy", 1, 128},
    {"gender", 0, 1}, {"gkdiving", 1, 128}, {"gkglovetypecode", 0, 1023}, {"gkhandling", 1, 128}, {"gkkicking", 1, 128},
    {"gkkickstyle", 0, 3}, {"gkpositioning", 1, 128}, {"gkreflexes", 1, 128}, {"gksavetype", 0, 1}, {"growthprofile", 0, 15},
    {"haircolorcode", 0, 63}, {"hairstylecode", 0, 31}, {"hairtypecode", 0, 16383}, {"hashighqualityhead", 0, 1},
    {"hasseasonaljersey", 0, 7}, {"headassetid", 0, 524287}, {"headclasscode", 0, 3}, {"headingaccuracy", 1, 128},
    {"headtypecode", 0, 16383}, {"headvariation", 0, 31}, {"height", 130, 257}, {"icontrait1", 0, 1073741823}, {"icontrait2", 0, 131071},
    {"interceptions", 1, 128}, {"internationalrep", 1, 8}, {"iscustomized", 0, 1}, {"isretiring", 0, 1}, {"jerseyfit", 0, 3},
    {"jerseysleevelengthcode", 0, 7}, {"jerseystylecode", 0, 1}, {"jumping", 1, 128}, {"lastnameid", 0, 65535}, {"lipcolor", 0, 127},
    {"longpassing", 1, 128}, {"longshots", 1, 128}, {"modifier", -5, 10}, {"muscularitycode", 0, 1}, {"nationality", 0, 4095},
    {"overallrating", 1, 128}, {"pacdiv", 0, 127}, {"paskic", 0, 127}, {"penalties", 1, 128}, {"personality", 1, 8}, {"phypos", 0, 127},
    {"playerid", -1, 524286}, {"playerjerseynameid", 0, 65535}, {"playerjointeamdate", 0, 1048575}, {"positioning", 1, 128},
    {"potential", 1, 128}, {"preferredfoot", 1, 2}, {"preferredposition1", 0, 31}, {"preferredposition2", -1, 62},
    {"preferredposition3", -1, 62}, {"preferredposition4", -1, 62}, {"preferredposition5", -1, 62}, {"preferredposition6", -1, 62},
    {"preferredposition7", -1, 62}, {"reactions", 1, 128}, {"releaseclause", 0, 1073741823}, {"role1", 0, 255}, {"role2", 0, 255},
    {"role3", 0, 255}, {"role4", 0, 255}, {"role5", 0, 255}, {"role6", 0, 255}, {"role7", 0, 255}, {"role8", 0, 255}, {"role9", 0, 255},
    {"runningcode1", 0, 131071}, {"runningcode2", 0, 131071}, {"runstylecode", 0, 63}, {"shoecolorcode1", 0, 31},
    {"shoecolorcode2", 0, 31}, {"shoedesigncode", 0, 3}, {"shoetypecode", 0, 4095}, {"shohan", 0, 127}, {"shortpassing", 1, 128},
    {"shortstyle", 0, 1}, {"shotpower", 1, 128}, {"sideburnscode", 0, 3}, {"skillmoves", 0, 7}, {"skillmoveslikelihood", 0, 7},
    {"skincomplexion", 0, 127}, {"skinmakeup", 0, 127}, {"skinsurfacepack", 100000, 362143}, {"skintonecode", 0, 127},
    {"skintypecode", 0, 127}, {"slidingtackle", 1, 128}, {"smallsidedshoetypecode", 0, 2047}, {"socklengthcode", 0, 7},
    {"sockstylecode", 0, 15}, {"sprintspeed", 1, 128}, {"stamina", 1, 128}, {"standingtackle", 1, 128}, {"strength", 1, 128},
    {"tattooback", 0, 1023}, {"tattoofront", 0, 1023}, {"tattoohead", 0, 1023}, {"tattooleftarm", 0, 1023}, {"tattooleftleg", 0, 1023},
    {"tattoorightarm", 0, 1023}, {"tattoorightleg", 0, 1023}, {"trait1", 0, 1073741823}, {"trait2", 0, 131071}, {"undershortstyle", 0, 1},
    {"usercaneditname", 0, 1}, {"vision", 1, 128}, {"volleys", 1, 128}, {"wage", 0, 134217727}, {"weakfootabilitytypecode", 1, 8},
    {"weight", 30, 157},
    };
    return k;
}

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return s;
}

void put_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string utf16_to_utf8(const std::string& b, size_t start, bool big_endian) {
    std::string out;
    out.reserve((b.size() - std::min(start, b.size())) / 2);
    auto unit = [&](size_t i) -> uint32_t {
        const uint32_t lo = static_cast<unsigned char>(b[i]), hi = static_cast<unsigned char>(b[i + 1]);
        return big_endian ? (lo << 8) | hi : (hi << 8) | lo;
    };
    for (size_t i = start; i + 1 < b.size(); i += 2) {
        uint32_t u = unit(i);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 3 < b.size()) {
                const uint32_t v = unit(i + 2);
                if (v >= 0xDC00 && v <= 0xDFFF) {
                    put_utf8(out, 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00));
                    i += 2;
                    continue;
                }
            }
            put_utf8(out, 0xFFFD);
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            put_utf8(out, 0xFFFD);
        } else {
            put_utf8(out, u);
        }
    }
    return out;
}

bool valid_utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 && c >= 0xC2 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 && c <= 0xF4 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

// Windows-1252 bytes to UTF-8 (0x80..0x9F: the cp1252 letters; the five unused codes become U+FFFD)
std::string cp1252_to_utf8(const std::string& s) {
    static const uint16_t k80[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                     0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0xA0) put_utf8(out, k80[c - 0x80]);
        else put_utf8(out, c);
    }
    return out;
}

// A whole number in a cell: "12", "-1", "+3", "12.0", " 7 "; false for text, an empty cell, NaN or a number past int64
bool cell_int(const std::string& s, int64_t& v, bool* rounded) {
    const std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0') return false;
    if (!std::isfinite(d) || !(d > -9.0e18 && d < 9.0e18)) return false;
    const double r = std::nearbyint(d);
    if (rounded) *rounded = r != d;
    v = static_cast<int64_t>(r);
    return true;
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::string PlayerFile::get(const std::string& column) const {
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i] == column) return i < values.size() ? values[i] : std::string();
    return std::string();
}

bool PlayerFile::has(const std::string& column) const {
    return std::find(header.begin(), header.end(), column) != header.end();
}

std::string player_file_text(const std::string& b, std::string* encoding) {
    auto enc = [&](const char* e) { if (encoding) *encoding = e; };
    if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xFE) {
        enc("UTF-16LE");
        return utf16_to_utf8(b, 2, false);
    }
    if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xFE && static_cast<unsigned char>(b[1]) == 0xFF) {
        enc("UTF-16BE");
        return utf16_to_utf8(b, 2, true);
    }
    if (b.size() >= 3 && b.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        std::string t = b.substr(3);
        if (valid_utf8(t)) { enc("UTF-8"); return t; }
        enc("ANSI");
        return cp1252_to_utf8(t);
    }
    // no BOM: UTF-16 when the high (or low) bytes of the first characters are all 0
    if (b.size() >= 4) {
        size_t n = std::min<size_t>(b.size() / 2, 64), zero_odd = 0, zero_even = 0;
        for (size_t i = 0; i < n; ++i) {
            if (b[2 * i] == 0) ++zero_even;
            if (b[2 * i + 1] == 0) ++zero_odd;
        }
        if (zero_odd == n && zero_even == 0) { enc("UTF-16LE"); return utf16_to_utf8(b, 0, false); }
        if (zero_even == n && zero_odd == 0) { enc("UTF-16BE"); return utf16_to_utf8(b, 0, true); }
    }
    if (valid_utf8(b)) { enc("UTF-8"); return b; }
    enc("ANSI");
    return cp1252_to_utf8(b);
}

std::vector<std::vector<std::string>> parse_player_table(const std::string& text) {
    // the separator: a tab when the first line has one, else a comma
    const size_t eol = text.find_first_of("\r\n");
    const std::string first = text.substr(0, eol);
    const char sep = first.find('\t') != std::string::npos || first.find(',') == std::string::npos ? '\t' : ',';
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, was_quoted = false;
    auto push_cell = [&] {
        row.push_back(was_quoted ? cell : trim(cell));
        cell.clear();
        was_quoted = false;
    };
    auto push_row = [&] {
        push_cell();
        bool empty = std::all_of(row.begin(), row.end(), [](const std::string& c) { return c.empty(); });
        if (!empty) rows.push_back(row);
        row.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; ++i; }
                else quoted = false;
            } else cell += c;
        } else if (c == '"' && trim(cell).empty()) {
            cell.clear();
            quoted = was_quoted = true;
        } else if (c == sep) push_cell();
        else if (c == '\r') {}
        else if (c == '\n') push_row();
        else cell += c;
    }
    if (!cell.empty() || !row.empty()) push_row();
    return rows;
}

PlayerFile parse_player_file(const std::string& bytes) {
    PlayerFile f;
    if (bytes.empty()) {
        f.error = "the file is empty";
        return f;
    }
    const std::string text = player_file_text(bytes, &f.encoding);
    const auto rows = parse_player_table(text);
    if (rows.empty()) {
        f.error = "no header row";
        return f;
    }
    std::set<std::string> seen;
    for (const auto& h : rows[0]) {
        std::string k = lower_ascii(trim(h));
        if (!k.empty() && !seen.insert(k).second) f.notes.push_back("column " + k + " appears twice: the first one is used");
        f.header.push_back(k);
    }
    const bool players_like = std::find(f.header.begin(), f.header.end(), "overallrating") != f.header.end() &&
                              (std::find(f.header.begin(), f.header.end(), "playerid") != f.header.end() ||
                               std::find(f.header.begin(), f.header.end(), "firstnameid") != f.header.end());
    if (!players_like) {
        f.error = "not a .player file (the header row needs overallrating and playerid or firstnameid)";
        return f;
    }
    f.rows = static_cast<int>(rows.size()) - 1;
    if (f.rows < 1) {
        f.error = "the file has a header row but no player row";
        return f;
    }
    f.values = rows.back();
    if (f.rows > 1) f.notes.push_back("the file has " + std::to_string(f.rows) + " player rows: the last one is used");
    if (f.values.size() < f.header.size())
        f.notes.push_back("the player row has " + std::to_string(f.values.size()) + " cells for " + std::to_string(f.header.size()) +
                          " columns: the missing ones are empty");
    else if (f.values.size() > f.header.size())
        f.notes.push_back(std::to_string(f.values.size() - f.header.size()) + " cell(s) past the last column are ignored");
    f.values.resize(f.header.size());
    f.ok = true;
    return f;
}

PlayerFile read_player_file(const fs::path& file) {
    PlayerFile f;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        f.error = "no such file";
        return f;
    }
    if (fs::file_size(file, ec) > 4u * 1024u * 1024u) {
        f.error = "file too large for a .player file";
        return f;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        f.error = "cannot open the file";
        return f;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse_player_file(bytes);
}

PlayerFilePreset player_file_to_preset(const PlayerFile& f, const PlayerFileOptions& o) {
    PlayerFilePreset r;
    const std::vector<PlayersFieldRange>& fields = o.fields ? *o.fields : fc27_players_fields();
    std::map<std::string, const PlayersFieldRange*> by_name;
    for (const auto& fr : fields) by_name[fr.name] = &fr;

    // the name columns: FC Editor and the Player Creator put the TEXT in the *nameid columns; Live Editor's preset CSV names them
    // firstname / surname / playerjerseyname / commonname
    static const std::pair<const char*, const char*> kNames[] = {
        {"firstnameid", "firstname"}, {"lastnameid", "surname"}, {"playerjerseynameid", "playerjerseyname"}, {"commonnameid", "commonname"},
        {"firstname", "firstname"}, {"surname", "surname"}, {"lastname", "surname"}, {"playerjerseyname", "playerjerseyname"},
        {"jerseyname", "playerjerseyname"}, {"commonname", "commonname"}};
    json names = {{"firstname", ""}, {"surname", ""}, {"playerjerseyname", ""}, {"commonname", ""}};
    json players = json::object();
    json extra = json::object();
    std::set<std::string> done;

    for (size_t i = 0; i < f.header.size(); ++i) {
        const std::string& col = f.header[i];
        const std::string val = i < f.values.size() ? f.values[i] : std::string();
        if (col.empty() || !done.insert(col).second) continue;  // a nameless column, or the second copy of a column
        const char* name_key = nullptr;
        for (const auto& n : kNames)
            if (col == n.first) name_key = n.second;
        if (name_key) {
            if (val.empty()) continue;
            if (all_digits(val) && col.size() > 2 && col.compare(col.size() - 2, 2, "id") == 0)
                r.left_out.push_back(col + "=" + val + " (a name id, not a name: the new player's name ids are his own)");
            else if (names[name_key].get<std::string>().empty()) names[name_key] = val;
            continue;
        }
        if (col == "gameversion") continue;  // noted below
        if (col == "playerid") {
            int64_t v = 0;
            if (cell_int(val, v, nullptr)) r.playerid = v;
            continue;
        }
        if (col == "number" || col == "jerseynumber") {
            int64_t v = 0;
            if (cell_int(val, v, nullptr) && v >= 1 && v <= 99) r.jersey = static_cast<int>(v);
            else if (!val.empty()) r.left_out.push_back(col + "=" + val + " (shirt number outside 1..99)");
            continue;
        }
        if (col == "teamid") {
            int64_t v = 0;
            if (cell_int(val, v, nullptr) && v > 0) r.teamid = v;
            continue;
        }
        auto it = by_name.find(col);
        if (it == by_name.end()) {
            extra[col] = val;  // kept in the preset, never written
            r.left_out.push_back(col + "=" + val + " (not a players field of FC 27)");
            continue;
        }
        if (val.empty()) {
            r.left_out.push_back(col + " (empty)");
            continue;
        }
        int64_t v = 0;
        bool rounded = false;
        if (!cell_int(val, v, &rounded)) {
            r.left_out.push_back(col + "=" + val + " (not a number)");
            continue;
        }
        const PlayersFieldRange& fr = *it->second;
        const int64_t c = std::max(fr.min, std::min(fr.max, v));
        if (c != v || rounded)
            r.clamped.push_back(col + ": " + val + " -> " + std::to_string(c) + " (range " + std::to_string(fr.min) + ".." + std::to_string(fr.max) + ")");
        players[col] = c;
    }
    r.filled = static_cast<int>(players.size());

    // what the file lacks: the defaults of FC Editor's new-player template (the same list as cmtracker.cpp), so a short file does
    // not give a player zeros where the game expects a value; only fields this game has
    static const std::pair<const char*, int64_t> kDefaults[] = {
        {"headclasscode", 1}, {"headtypecode", 1}, {"hairtypecode", 16}, {"eyebrowcode", 30301}, {"jerseystylecode", 1}, {"facepsdlayer0", 1},
        {"skinsurfacepack", 223101}, {"hasseasonaljersey", 3}, {"sockstylecode", 1}, {"smallsidedshoetypecode", 500}, {"emotion", 3},
        {"personality", 2}, {"skincomplexion", 1}, {"jerseyfit", 2}, {"growthprofile", 1}, {"skillmoveslikelihood", 1}, {"skinmakeup", 0}};
    for (const auto& d : kDefaults) {
        auto it = by_name.find(d.first);
        if (it == by_name.end() || players.contains(d.first)) continue;
        players[d.first] = std::max(it->second->min, std::min(it->second->max, d.second));
        r.defaulted.push_back(d.first);
    }

    auto num = [&](const char* k) { return players.contains(k) ? players[k].get<int64_t>() : int64_t(0); };
    r.overall = static_cast<int>(num("overallrating"));
    r.potential = static_cast<int>(num("potential"));
    r.position = players.contains("preferredposition1") ? static_cast<int>(num("preferredposition1")) : -1;

    const std::string first = names["firstname"], last = names["surname"], common = names["commonname"];
    r.name = !common.empty() ? common : trim(first + " " + last);
    if (r.name.empty()) r.name = "player " + std::to_string(r.playerid);
    if (names["playerjerseyname"].get<std::string>().empty() && !last.empty()) names["playerjerseyname"] = last;

    std::sort(r.left_out.begin(), r.left_out.end());
    json notes = {{"columns", f.header.size()}, {"encoding", f.encoding}, {"fields_set", r.filled}, {"left_out", r.left_out},
                  {"clamped", r.clamped}, {"defaulted", r.defaulted}, {"file_notes", f.notes}, {"extra", extra}};
    const std::string gv = f.get("gameversion");
    if (!gv.empty()) notes["gameversion"] = gv;
    if (r.jersey > 0) notes["number"] = r.jersey;
    if (r.teamid > 0) notes["teamid"] = r.teamid;

    json preset = {{"format", "turbo-player-preset"}, {"version", 1}, {"name", r.name}, {"playerid", r.playerid},
                   {"names", names}, {"players", players}, {"links", json::array()}, {"miniface", nullptr}, {"turbo", "player_file"},
                   {"source", ".player file" + (o.source.empty() ? std::string() : " (" + o.source + ")")}, {"player_file", notes}};
    if (o.teamid > 0) {
        const int jersey = o.jersey >= 0 ? o.jersey : r.jersey;
        preset["links"].push_back({{"teamid", o.teamid}, {"jerseynumber", jersey}, {"national", false}, {"position", 29}});
    }
    r.preset = std::move(preset);
    return r;
}

void clamp_preset_ratings(nlohmann::json& preset) {
    if (!preset.is_object() || !preset.contains("players") || !preset["players"].is_object()) return;
    auto& pl = preset["players"];
    static const char* const kRatings[] = {
        "acceleration", "aggression", "agility", "balance", "ballcontrol", "composure", "crossing", "curve",
        "defensiveawareness", "dribbling", "finishing", "freekickaccuracy", "gkdiving", "gkhandling", "gkkicking",
        "gkpositioning", "gkreflexes", "headingaccuracy", "interceptions", "jumping", "longpassing", "longshots",
        "overallrating", "penalties", "positioning", "potential", "reactions", "shortpassing", "shotpower",
        "slidingtackle", "sprintspeed", "stamina", "standingtackle", "strength", "vision", "volleys"
    };
    for (const char* key : kRatings) {
        if (pl.contains(key) && pl[key].is_number()) {
            const int64_t v = pl[key].get<int64_t>();
            if (v < 1) pl[key] = 1;
            else if (v > 99) pl[key] = 99;
        }
    }
}

}  // namespace turbo
