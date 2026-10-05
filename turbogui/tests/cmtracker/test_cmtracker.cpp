// Standalone test of core/cmtracker (no Lua, no game): g++ only. Run: bash tests/cmtracker/run.sh
// Synthetic players only (the CSV layout is CMTracker's players export; the values are made up).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "core/cmtracker.h"
#include "core/image.h"

using namespace turbo;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

static const char* kHeader =
    "\"attributes.acceleration\",\"attributes.sprintspeed\",\"attributes.marking\",\"attributes.gkdiving\",\"attributes.gkhandling\","
    "\"attributes.gkkicking\",\"attributes.gkreflexes\",\"attributes.gkpositioning\",\"info.contract.jointeamdate\","
    "\"info.contract.enddate\",\"info.nation.id\",\"info.nation.name\",\"info.name.firstname\",\"info.name.lastname\","
    "\"info.name.playerjerseyname\",\"info.name.knownas\",\"info.playerid\",\"info.overallrating\",\"info.potential\","
    "\"info.birthdate\",\"info.age\",\"info.teams.club_team.id\",\"info.teams.club_team.name\",\"info.headshot\","
    "\"info.haircolor\",\"info.eyecolor\",\"info.skintone\",\"info.headtype\",\"info.bodytype\",\"info.preferredfoot\","
    "\"info.skillmoves\",\"info.weafoot\",\"info.internationalrep\",\"info.ovrmodifier\",\"info.height\",\"info.weight\","
    "\"info.gender\",\"info.real_face\",\"info.isretiring\",\"info.traits.trait1\",\"info.traits.trait2\","
    "\"card_attrs.pac\",\"card_attrs.sho\",\"card_attrs.pas\",\"card_attrs.dri\",\"card_attrs.def\",\"card_attrs.phy\","
    "\"primary_position\",\"other_positions\"\n";
//                    acc spr mrk div han kic ref pos  join         end          nat  nation  first    last      jersey   known       id     ovr pot birth                     age club  clubname  headshot                                                     hair eye skin head body foot   sm wf rep mod hgt wgt gender face retire trait1                       trait2             pac sho pas dri def phy pos other
static const char* kRows =
    "50,60,70,10,11,12,13,14,\"2020-07-01\",\"2028-06-01\",18,\"France\",\"Jean\",\"Dupont\",\"Dupont\",\"Jean Dupont\",900001,80,85,\"1999-03-04T00:00:00.000Z\",27,243,\"Test FC\",\"https://x/DB/27/heads/p900001.png\",2,3,40,100,5,\"Left\",4,3,2,1,181,75,\"Male\",\"Yes\",\"No\",\"Rapid, Gamechanger, Nonsense Style\",\"\",70,71,72,73,74,75,\"ST\",\"RW | CAM | -\"\n"
    "20,30,40,80,81,82,83,84,\"2021-01-15\",\"2026-06-30\",7,\"Belgium\",\"Alexis José\",\"Núñez Álvarez\",\"Núñez\",\"Alexis Nunito\",900002,70,70,\"2001-12-31T00:00:00.000Z\",25,0,\"\",\"\",1,1,20,30,3,\"Right\",1,2,1,0,190,85,\"Female\",\"No\",\"Yes\",\"\",\"GK Far Throw, GK Far Reach\",47,24,30,50,16,50,\"GK\",\"-\"\n";

int main(int argc, char** argv) {
    CmtLibrary lib;
    std::string err;
    CHECK(lib.add_csv(std::string(kHeader) + kRows, "synthetic.csv", &err) == 2);
    CHECK(lib.size() == 2);
    CHECK(lib.add_csv("a,b\n1,2\n", "bad.csv", &err) == 0 && !err.empty());

    // search: name, accent folding, id, club, no match; a later file replaces the same id
    CHECK(lib.search("dupont").size() == 1);
    CHECK(lib.search("nunez alvarez").size() == 1);
    CHECK(lib.search("900002").size() == 1 && lib.search("900002")[0]->id == 900002);
    CHECK(lib.search("test fc").size() == 1);
    CHECK(lib.search("zzz").empty());
    CHECK(lib.search("").size() == 2 && lib.search("")[0]->overall == 80);
    CHECK(lib.add_csv(std::string(kHeader) + kRows, "again.csv") == 2 && lib.size() == 2);

    // outfield player
    CmtPresetOptions o;
    o.teamid = 4; o.jersey = 9;
    auto j = cmt_to_preset(*lib.find(900001), o);
    auto& p = j["players"];
    CHECK(j["format"] == "turbo-player-preset" && j["playerid"] == 900001);
    CHECK(p["acceleration"] == 50 && p["defensiveawareness"] == 70);
    CHECK(p["pacdiv"] == 70 && p["shohan"] == 71 && p["paskic"] == 72 && p["driref"] == 73 && p["defspe"] == 74 && p["phypos"] == 75);
    CHECK(p["skillmoves"] == 3 && p["weakfootabilitytypecode"] == 3 && p["preferredfoot"] == 2 && p["gender"] == 0);
    CHECK(p["birthdate"] == 150056 - 0 || p["birthdate"].is_number());
    CHECK(p["contractvaliduntil"] == 2028 && p["nationality"] == 18 && p["skintonecode"] == 40 && p["headtypecode"] == 100);
    CHECK(p["preferredposition1"] == 25 && p["preferredposition2"] == 23 && p["preferredposition3"] == 18 && p["preferredposition4"] == -1);
    CHECK(p["trait1"] == ((1 << 21) | (1 << 7)) && p["trait2"] == 0);   // Rapid, Game Changer
    CHECK(j["cmtracker"]["left_out"].size() == 1 && j["cmtracker"]["real_face_id"] == 900001);
    CHECK(j["names"]["commonname"] == "" && j["names"]["surname"] == "Dupont" && j["links"][0]["teamid"] == 4);
    CHECK(cmt_head_url(900001, 27) == "https://cmtracker.fra1.cdn.digitaloceanspaces.com/DB/27/heads/p900001.png");
    CHECK(cmt_head_url(1, 5).empty() && cmt_head_url(0, 27).empty() && cmt_head_url(-3, 27).empty());
    // goalkeeper: card values are the GK ones, speed from the card's PAC; common name when "known as" differs
    auto g = cmt_to_preset(*lib.find(900002), CmtPresetOptions{});
    auto& q = g["players"];
    CHECK(q["pacdiv"] == 80 && q["shohan"] == 81 && q["paskic"] == 82 && q["driref"] == 83 && q["phypos"] == 84 && q["defspe"] == 47);
    CHECK(q["trait2"] == 17 && q["gender"] == 1 && q["isretiring"] == 1 && q["skillmoves"] == 0);
    CHECK(g["names"]["commonname"] == "Alexis Nunito" && !g["cmtracker"].contains("real_face_id"));
    CHECK(g["links"].empty());
    int which = 0;
    CHECK(cmt_playstyle_bit("Game Changer", &which) == 7 && which == 1);
    CHECK(cmt_playstyle_bit("GK Far Reach", &which) == 4 && which == 2);
    CHECK(cmt_playstyle_bit("Nope", &which) == -1);

    // the miniface path: a downloaded PNG becomes a 256 x 256 DXT5 DDS the game reads (optional file argument)
    if (argc > 1) {
        Rgba img;
        std::string e;
        CHECK(load_image_file(argv[1], img, &e));
        auto dds = encode_dds_dxt5(frame_image(img, 256, Framing{}));
        CHECK(dds.size() == 128 + 256 * 256);
        Rgba back;
        CHECK(decode_dds(dds, back, &e) && back.w == 256 && back.h == 256);
        std::printf("miniface: %dx%d png -> %zu byte DDS\n", img.w, img.h, dds.size());
    }
    std::printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
