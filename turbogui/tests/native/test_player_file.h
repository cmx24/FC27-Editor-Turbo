// Native tests: ".player" files (core/player_file.h) and accent-agnostic search (core/text_fold.h). Synthetic data only: the .player
// fixture is written here from code (UTF-16LE with a BOM, tabs, CRLF) for a made-up player, never a real one. Included by
// test_main.cpp; player_file_test::run().
#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/cmtracker.h"
#include "core/player_file.h"
#include "core/text_fold.h"

namespace player_file_test {

using namespace turbo;
using nlohmann::json;
namespace fs = std::filesystem;

// UTF-8 text -> UTF-16LE bytes (BMP and surrogate pairs), with or without the BOM
static std::string utf16le(const std::string& s, bool bom) {
    std::string out;
    if (bom) out += "\xFF\xFE";
    auto unit = [&](uint32_t u) {
        out += static_cast<char>(u & 0xFF);
        out += static_cast<char>(u >> 8);
    };
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            unit(0xD800 + (cp >> 10));
            unit(0xDC00 + (cp & 0x3FF));
        } else unit(cp);
    }
    return out;
}

// The made-up player: FC Editor's layout (gameversion first, names as text in the *nameid columns, "number" last), plus a quoted
// cell with a tab inside, a column FC 27 does not have, values out of range and one that is no number
static std::string fixture_text(const std::string& gameversion = "27") {
    const std::vector<std::pair<std::string, std::string>> cols = {
        {"gameversion", gameversion},       {"firstnameid", "Zéphyrin"},  {"lastnameid", "Øvrebø-Çakır"}, {"playerjerseynameid", "Øvrebø"},
        {"commonnameid", ""},               {"overallrating", "140"},      {"potential", "88"},           {"preferredposition1", "25"},
        {"preferredposition2", "-1"},       {"sprintspeed", "0"},          {"acceleration", "91"},        {"height", "\"183\""},
        {"birthdate", "150000"},            {"modifier", "12"},            {"trait1", "4325376"},         {"headassetid", "499001"},
        {"playerid", "499001"},             {"shoetypecode", "abc"},       {"finishing", "77.0"},         {"futurefield", "\"a\tb\""},
        {"growthprofile", "2"},             {"number", "77"}};
    std::string h, v;
    for (size_t i = 0; i < cols.size(); ++i) {
        h += (i ? "\t" : "") + cols[i].first;
        v += (i ? "\t" : "") + cols[i].second;
    }
    return h + "\r\n" + v + "\r\n";
}

static fs::path tmp_file(const char* name) {
    std::error_code ec;
    fs::path d = fs::temp_directory_path(ec) / "player_file_test";
    fs::create_directories(d, ec);
    return d / name;
}

static void run() {
    std::printf("native .player import and search folding\n");

    run_case("player file: UTF-16LE + BOM, tabs, CRLF, quoted cells, the made-up player read", [] {
        const PlayerFile f = parse_player_file(utf16le(fixture_text(), true));
        CHECK(f.ok, "parsed: " + f.error);
        CHECK(f.encoding == "UTF-16LE", "encoding " + f.encoding);
        CHECK(f.rows == 1 && f.header.size() == 22 && f.values.size() == 22, fmt("1 row, 22 columns (%d, %zu)", f.rows, f.header.size()));
        CHECK(f.get("lastnameid") == "Øvrebø-Çakır", "UTF-16 -> UTF-8 name: " + f.get("lastnameid"));
        CHECK(f.get("futurefield") == "a\tb", "a quoted cell keeps its tab");
        CHECK(f.get("height") == "183", "quotes around a number dropped");
        CHECK(f.get("nosuchcolumn").empty() && !f.has("nosuchcolumn"), "a missing column is empty");
    });

    run_case("player file: players fields mapped, clamped, left out with a note; names; jersey; team link", [] {
        const PlayerFile f = parse_player_file(utf16le(fixture_text(), true));
        PlayerFileOptions o;
        o.teamid = 1001;
        o.source = "made_up.player";
        const PlayerFilePreset r = player_file_to_preset(f, o);
        const json& p = r.preset["players"];
        CHECK(r.preset["format"] == "turbo-player-preset" && r.preset["turbo"] == "player_file", "a Turbo player preset");
        CHECK(p["overallrating"] == 128 && r.overall == 128, "overallrating 140 clamped to 128");
        CHECK(p["sprintspeed"] == 1, "sprintspeed 0 clamped to 1");
        CHECK(p["modifier"] == 10, "modifier 12 clamped to 10");
        CHECK(p["preferredposition2"] == -1 && p["preferredposition1"] == 25 && r.position == 25, "positions kept");
        CHECK(p["finishing"] == 77 && p["height"] == 183 && p["trait1"] == 4325376 && p["headassetid"] == 499001, "values kept");
        CHECK(!p.contains("shoetypecode"), "text that is no number left out");
        CHECK(!p.contains("futurefield") && r.preset["player_file"]["extra"]["futurefield"] == "a\tb", "an unknown column kept as extra only");
        CHECK(!p.contains("playerid") && r.playerid == 499001 && r.preset["playerid"] == 499001, "playerid is the preset's, not a field to write");
        CHECK(!p.contains("firstnameid") && !p.contains("lastnameid") && !p.contains("number") && !p.contains("gameversion"), "no name ids, number, gameversion");
        CHECK(p["growthprofile"] == 2, "a field the file has is not defaulted");
        CHECK(p.contains("headclasscode") && p["headclasscode"] == 1, "a field the file lacks gets the new-player default");
        auto has = [](const std::vector<std::string>& v, const std::string& s) {
            for (const auto& x : v)
                if (x.find(s) == 0) return true;
            return false;
        };
        CHECK(has(r.clamped, "overallrating: 140 -> 128") && has(r.clamped, "sprintspeed: 0 -> 1") && has(r.clamped, "modifier: 12 -> 10"),
              "clamped values noted");
        CHECK(has(r.left_out, "shoetypecode=abc (not a number)") && has(r.left_out, "futurefield="), "left-out columns noted");
        CHECK(has(r.defaulted, "headclasscode") && !has(r.defaulted, "growthprofile"), "defaulted fields noted");
        CHECK(r.filled == 13, fmt("13 players fields from the file (%d)", r.filled));
        const json& n = r.preset["names"];
        CHECK(n["firstname"] == "Zéphyrin" && n["surname"] == "Øvrebø-Çakır" && n["playerjerseyname"] == "Øvrebø" && n["commonname"] == "",
              "names for editedplayernames");
        CHECK(r.name == "Zéphyrin Øvrebø-Çakır", "display name: first + surname");
        CHECK(r.jersey == 77 && r.preset["player_file"]["number"] == 77, "number -> shirt number");
        CHECK(r.preset["links"].size() == 1 && r.preset["links"][0]["teamid"] == 1001 && r.preset["links"][0]["jerseynumber"] == 77,
              "team link with the file's number");
        CHECK(r.preset["player_file"]["gameversion"] == "27" && r.preset["source"] == ".player file (made_up.player)", "source and game noted");
        PlayerFileOptions none;
        CHECK(player_file_to_preset(f, none).preset["links"].empty(), "no club picked: no link");
    });

    run_case("player file: the live table's ranges win over FC 27's list", [] {
        const PlayerFile f = parse_player_file(utf16le(fixture_text(), true));
        std::vector<PlayersFieldRange> live = {{"overallrating", 1, 99}, {"potential", 1, 99}};
        PlayerFileOptions o;
        o.fields = &live;
        const PlayerFilePreset r = player_file_to_preset(f, o);
        CHECK(r.preset["players"]["overallrating"] == 99 && r.filled == 2, fmt("clamped to the live range, 2 fields (%d)", r.filled));
        CHECK(r.defaulted.empty(), "no defaults for fields the live table lacks");
    });

    run_case("player file: encodings (no BOM UTF-16LE, UTF-16BE, UTF-8 BOM, Windows-1252) and a surrogate pair", [] {
        std::string enc;
        CHECK(player_file_text(utf16le("playerid\tx", false), &enc) == "playerid\tx" && enc == "UTF-16LE", "UTF-16LE without a BOM: " + enc);
        std::string be = "\xFE\xFF";
        for (char c : std::string("ab")) { be += '\0'; be += c; }
        CHECK(player_file_text(be, &enc) == "ab" && enc == "UTF-16BE", "UTF-16BE");
        CHECK(player_file_text("\xEF\xBB\xBFplayerid", &enc) == "playerid" && enc == "UTF-8", "UTF-8 BOM dropped");
        CHECK(player_file_text("Jos\xE9", &enc) == "Jos\xC3\xA9" && enc == "ANSI", "Windows-1252 bytes -> UTF-8");
        CHECK(player_file_text(utf16le("\xF0\x9F\x98\x80", true), &enc) == "\xF0\x9F\x98\x80", "a surrogate pair joined");
        std::string lone = "\xFF\xFE";
        lone += '\x00';
        lone += '\xD8';  // a high surrogate with nothing after it
        CHECK(player_file_text(lone, &enc) == "\xEF\xBF\xBD", "a lone surrogate becomes U+FFFD");
        const PlayerFile f = parse_player_file("\xEF\xBB\xBFplayerid,overallrating,firstname\n7,70,\"Doe, J\"\n");
        CHECK(f.ok && f.get("firstname") == "Doe, J" && f.get("overallrating") == "70", "comma-separated UTF-8 read too");
    });

    run_case("player file: refusals and notes (empty, header only, not a player file, two rows, short row, duplicate column)", [] {
        CHECK(!parse_player_file("").ok, "empty file refused");
        CHECK(!parse_player_file(utf16le("playerid\toverallrating\r\n", true)).ok, "header only refused");
        CHECK(!parse_player_file(utf16le("teamid\tname\r\n1\tx\r\n", true)).ok, "not a .player file refused");
        const PlayerFile f = parse_player_file(utf16le("playerid\toverallrating\toverallrating\tpotential\r\n1\t60\t61\t70\r\n2\t65\r\n", true));
        CHECK(f.ok && f.rows == 2 && f.get("playerid") == "2" && f.get("overallrating") == "65" && f.get("potential").empty(), "the last row is used");
        CHECK(f.notes.size() == 3, fmt("three notes: duplicate, two rows, short row (%zu)", f.notes.size()));
        const PlayerFilePreset r = player_file_to_preset(f, PlayerFileOptions());
        CHECK(r.preset["players"]["overallrating"] == 65 && r.name == "player 2", "the first of two columns counts; a nameless player is 'player <id>'");
    });

    run_case("player file: read from disk, and the game's list covers every column of a .player file", [] {
        const fs::path p = tmp_file("made_up.player");
        {
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            const std::string b = utf16le(fixture_text("26"), true);
            out.write(b.data(), static_cast<std::streamsize>(b.size()));
        }
        const PlayerFile f = read_player_file(p);
        CHECK(f.ok && f.get("gameversion") == "26", "read from disk: " + f.error);
        CHECK(!read_player_file(tmp_file("missing.player")).ok, "a missing file refused");
        CHECK(fc27_players_fields().size() == 152, fmt("FC 27 has 152 players fields (%zu)", fc27_players_fields().size()));
        // the Player Creator's HEADER_ORDER (146 columns): every one is an FC 27 players field except "number"
        static const char* kPc[] = {"firstnameid", "lastnameid", "playerjerseynameid", "commonnameid", "role4", "role3", "gkglovetypecode",
            "role5", "role2", "role1", "eyebrowcode", "skintypecode", "haircolorcode", "facialhairtypecode", "curve", "jerseystylecode", "agility",
            "tattooback", "accessorycode4", "gksavetype", "positioning", "tattooleftarm", "hairtypecode", "facepsdlayer0", "standingtackle",
            "preferredposition3", "longpassing", "penalties", "animfreekickstartposcode", "lipcolor", "isretiring", "longshots", "gkdiving",
            "icontrait2", "interceptions", "shoecolorcode2", "crossing", "potential", "gkreflexes", "finishingcode1", "reactions", "composure",
            "skinsurfacepack", "vision", "contractvaliduntil", "finishing", "dribbling", "slidingtackle", "accessorycode3", "preferredposition5",
            "accessorycolourcode1", "headtypecode", "driref", "sprintspeed", "undershortstyle", "height", "hasseasonaljersey", "tattoohead",
            "preferredposition2", "strength", "shoetypecode", "birthdate", "preferredposition1", "tattooleftleg", "skinmakeup", "ballcontrol",
            "phypos", "shotpower", "trait1", "socklengthcode", "weight", "hashighqualityhead", "eyedetail", "tattoorightarm", "icontrait1",
            "balance", "gender", "headassetid", "gkkicking", "defspe", "internationalrep", "preferredposition6", "shortpassing",
            "freekickaccuracy", "skillmoves", "faceposerpreset", "usercaneditname", "avatarpomid", "finishingcode2", "aggression",
            "acceleration", "paskic", "headingaccuracy", "iscustomized", "preferredposition7", "runningcode2", "modifier", "gkhandling",
            "eyecolorcode", "jerseysleevelengthcode", "sockstylecode", "accessorycolourcode3", "accessorycode1", "playerjointeamdate",
            "headclasscode", "tattoofront", "nationality", "preferredfoot", "sideburnscode", "weakfootabilitytypecode", "jumping",
            "personality", "gkkickstyle", "stamina", "playerid", "accessorycolourcode4", "gkpositioning", "headvariation",
            "skillmoveslikelihood", "trait2", "shohan", "skintonecode", "shortstyle", "overallrating", "smallsidedshoetypecode", "emotion",
            "runstylecode", "facepsdlayer1", "muscularitycode", "skincomplexion", "jerseyfit", "accessorycode2", "shoedesigncode",
            "shoecolorcode1", "hairstylecode", "bodytypecode", "animpenaltiesstartposcode", "pacdiv", "defensiveawareness", "runningcode1",
            "preferredposition4", "volleys", "accessorycolourcode2", "tattoorightleg", "facialhaircolorcode", "number"};
        size_t known = 0;
        for (const char* c : kPc)
            for (const auto& fr : fc27_players_fields())
                if (fr.name == c) { ++known; break; }
        CHECK(sizeof(kPc) / sizeof(kPc[0]) == 146 && known == 145, fmt("145 of 146 columns are players fields (%zu)", known));
    });

    run_case("search fold: diacritics, multi-letter folds, case, combining marks, other scripts kept", [] {
        CHECK(fold_search("Müller") == "muller" && fold_search("SØRLOTH") == "sorloth" && fold_search("Çağrı") == "cagri", "Latin-1 / Ext-A");
        CHECK(fold_search("Straße") == "strasse" && fold_search("Æbleskiver") == "aebleskiver" && fold_search("Œil") == "oeil", "ß æ œ");
        CHECK(fold_search("Þór Ðan ð") == "thor dan d" && fold_search("Łukasz Đorđe") == "lukasz dorde", "þ ð ł đ");
        CHECK(fold_search("Ștefan Ţurcan Şahin ț") == "stefan turcan sahin t" && fold_search("Žižek Ő ğ ñ á") == "zizek o g n a", "ș ş ț ţ ž ő ğ ñ á");
        CHECK(fold_search("Nguyễn Văn") == "nguyen van", "Latin Extended Additional (Vietnamese)");
        CHECK(fold_search("Jose\xCC\x81") == "jose", "a combining accent (decomposed text) dropped");
        CHECK(fold_search("Иван 日本") == "Иван 日本", "other scripts kept byte for byte");
        CHECK(fold_search("a\xFFz") == "a\xFFz", "an invalid byte kept");
        CHECK(fold_search("") .empty() && fold_search("ID 158023") == "id 158023", "ASCII lower-cased only");
        CHECK(fold_contains("Martin Ødegaard", "odeg") && fold_contains("Ødegaard", "") && !fold_contains("Ødegaard", "x"), "fold_contains");
    });

    run_case("search fold: the CMTracker library finds accented names without the accents (and the other way round)", [] {
        CmtLibrary lib;
        lib.add_csv("info.playerid,info.overallrating,info.name.firstname,info.name.lastname,info.teams.club_team.name\n"
                    "900001,80,Zéphyrin,Øvrebø,Ålesund Made Up\n900002,70,Plain,Name,Elsewhere\n",
                    "made_up.csv");
        CHECK(lib.search("ovrebo").size() == 1 && lib.search("zephyrin ovrebo").size() == 1, "plain letters find the accented name");
        CHECK(lib.search("Øvrebø").size() == 1 && lib.search("alesund").size() == 1, "accented query and club fold too");
        CHECK(lib.search("").size() == 2 && lib.search("900002").size() == 1, "empty query and id search unchanged");
    });
}

}  // namespace player_file_test
