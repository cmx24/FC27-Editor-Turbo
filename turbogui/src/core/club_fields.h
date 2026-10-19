// FC 27 LE Turbo GUI - 2.20 K3 (brief 2.20-K3-club-fields): the club, crowd, stadium and kit fields Turbo's Kits & Club pages did not draw.
//
// What is here: for every such field, its plain-English title, a sentence of help, how its values are NAMED, and the set of values the
// game's data really uses. Pure data and text: no ImGui, no game memory (the pickers are ui/ui_club_fields.cpp, the tests
// tests/native/test_club_fields.h). Ownership: S2 (2.20/team-defaults) owns core/field_labels.h and the descriptors of the codes the pages
// draw today; this header never includes it and never edits it. A table that belongs there goes through the Pit wall as a CHANGE line.
//
// THE VALUE SET OF A FIELD is the set of values the STOCK squad file holds in that column (invariant 4 of the fc27-modding skill: FC Editor's
// _temp export and FC 26 enums are not sources). tools/club_field_domains.py reads the file and writes the block below (it names its source
// file and md5). It is an observed set, not the schema range: crowdskintonecode is {20, 40, 70, 90} although the column takes 0..100, and
// shortstyle has no 1 because no stock kit uses it. The pickers offer only these values, the write refuses any other, and a value outside the
// set that is already in the database (another squad file) is shown as "Other (N)" and left alone until the user picks a listed value.
//
// HOW A VALUE IS NAMED (Look), strongest source first - an invented meaning is worse than no meaning:
//   Weather / Fit / ShortStyle / KitType  the names Live Editor ships for these columns (loc\eng_us\localize.json: weather_0..4, jerseyfit_0..1,
//                                         shortstyle_0..1, teamkittypetechid_0..3). Values of the set that LE names not are "Style N".
//   Switch   a field whose schema range is 0..1: 0 is "Off", 1 is "On" (a single-valued set shows the one value, locked).
//   Levels   a "has... / is...enabled / flame" field with more than two values: 0 is "Off" (read from the field's name; the game publishes no
//            list), every other value is "Style N".
//   Style    no source at all: every value is "Style N" (the noun can differ: "Ball 55", "Training ground 366"). N is the stored code, so the
//            number is stable between squad files; the hover says there is no name for it. The code is never the main text on its own.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace turbo {
namespace clubfields {

// ---- the generated value sets (tools/club_field_domains.py --write)
struct GenDomain {
    const char* table;
    const char* field;
    int n;
    const int* values;  // ascending
    const int* rows;    // rows of the stock file holding each value
};

// BEGIN GENERATED club_field_domains.py - do not edit by hand: py -3 tools/club_field_domains.py --squad <file> --write
// source: Squads20261004161718434  md5 e69a3495e99bbb48624f80412da86a7c  rows: stadiums 181, teamkits 4054, teams 870
// per field: the values the stock file holds (ascending) and how many rows hold each
static const int kDomV0[] = {0, 1, 2};  // teams.hastifo
static const int kDomN0[] = {746, 79, 45};
static const int kDomV1[] = {0, 1, 2};  // teams.isbannerenabled
static const int kDomN1[] = {64, 684, 122};
static const int kDomV2[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};  // teams.crowdregion
static const int kDomN2[] = {21, 128, 33, 154, 349, 50, 50, 59, 23, 3};
static const int kDomV3[] = {20, 40, 70, 90};  // teams.crowdskintonecode
static const int kDomN3[] = {682, 26, 107, 55};
static const int kDomV4[] = {0, 1, 2};  // teams.hasstandingcrowd
static const int kDomN4[] = {303, 483, 84};
static const int kDomV5[] = {0, 1};  // teams.hasvikingclap
static const int kDomN5[] = {853, 17};
static const int kDomV6[] = {0, 1, 2};  // teams.haslargeflag
static const int kDomN6[] = {844, 6, 20};
static const int kDomV7[] = {0, 1};  // teams.skinnyflags
static const int kDomN7[] = {788, 82};
static const int kDomV8[] = {0, 1, 2};  // teams.iscompetitionscarfenabled
static const int kDomN8[] = {185, 545, 140};
static const int kDomV9[] = {0, 1, 2};  // teams.iscompetitionpoleflagenabled
static const int kDomN9[] = {239, 483, 148};
static const int kDomV10[] = {0, 1, 2, 3};  // teams.iscompetitioncrowdcardsenabled
static const int kDomN10[] = {588, 112, 116, 54};
static const int kDomV11[] = {0, 1, 2};  // teams.stanchionflamethrower
static const int kDomN11[] = {847, 3, 20};
static const int kDomV12[] = {0, 1, 4};  // teams.flamethrowercannon
static const int kDomN12[] = {760, 11, 99};
static const int kDomV13[] = {0, 1};  // teams.hassubstitutionboard
static const int kDomN13[] = {853, 17};
static const int kDomV14[] = {0, 1};  // teams.pitchwear
static const int kDomN14[] = {856, 14};
static const int kDomV15[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 14, 15};  // teams.stadiummowpattern_code
static const int kDomN15[] = {82, 519, 2, 200, 1, 1, 1, 2, 1, 2, 1, 7, 1, 50};
static const int kDomV16[] = {0, 2};  // teams.playsurfacetype
static const int kDomN16[] = {823, 47};
static const int kDomV17[] = {0, 1};  // teams.presassetone
static const int kDomN17[] = {868, 2};
static const int kDomV18[] = {0, 1, 2};  // teams.presassettwo
static const int kDomN18[] = {758, 89, 23};
static const int kDomV19[] = {0, 22, 23, 25, 27, 29, 30, 31, 33, 36, 37, 41, 44, 45, 46, 51, 54, 55, 67, 71, 72, 76, 79, 82, 89, 90, 91, 92, 93, 96, 97, 100, 111, 122, 134, 137, 151, 173, 188, 196, 199};  // teams.ballid
static const int kDomN19[] = {59, 11, 10, 19, 18, 12, 18, 12, 12, 18, 34, 21, 10, 19, 40, 18, 12, 72, 18, 12, 16, 14, 16, 20, 18, 14, 20, 21, 18, 18, 16, 16, 52, 18, 31, 22, 21, 12, 30, 16, 16};
static const int kDomV20[] = {316, 366};  // teams.trainingstadium
static const int kDomN20[] = {738, 132};
static const int kDomV21[] = {0, 1};  // stadiums.tifo
static const int kDomN21[] = {15, 166};
static const int kDomV22[] = {1};  // stadiums.supportsunlicensedtifos
static const int kDomN22[] = {181};
static const int kDomV23[] = {1};  // stadiums.supportsunlicensedbanners
static const int kDomN23[] = {181};
static const int kDomV24[] = {1, 2, 3, 4, 5, 6, 7, 8};  // stadiums.seatcolor
static const int kDomN24[] = {162, 1, 2, 9, 1, 1, 1, 4};
static const int kDomV25[] = {0, 1, 2, 3, 4, 5, 6, 21};  // stadiums.colorregion
static const int kDomN25[] = {31, 6, 1, 44, 57, 31, 10, 1};
static const int kDomV26[] = {1, 2};  // stadiums.adboardtype
static const int kDomN26[] = {140, 41};
static const int kDomV27[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};  // stadiums.policetypecode
static const int kDomN27[] = {50, 8, 4, 32, 40, 15, 3, 2, 24, 3};
static const int kDomV28[] = {0, 1, 2, 3, 4, 5, 7, 8};  // stadiums.defaultweather
static const int kDomN28[] = {114, 16, 21, 4, 7, 1, 17, 1};
static const int kDomV29[] = {0, 1, 3};  // stadiums.tod1weather
static const int kDomN29[] = {15, 52, 114};
static const int kDomV30[] = {0, 1, 3};  // stadiums.tod2weather
static const int kDomN30[] = {18, 52, 111};
static const int kDomV31[] = {0, 1, 3};  // stadiums.tod3weather
static const int kDomN31[] = {139, 17, 25};
static const int kDomV32[] = {0, 1, 3};  // stadiums.tod4weather
static const int kDomN32[] = {139, 17, 25};
static const int kDomV33[] = {0, 1};  // stadiums.hasmexicanwave
static const int kDomN33[] = {149, 32};
static const int kDomV34[] = {0, 1};  // stadiums.pitchtarps
static const int kDomN34[] = {7, 174};
static const int kDomV35[] = {0, 1, 2, 3};  // stadiums.goalnetstanchiontype
static const int kDomN35[] = {106, 12, 55, 8};
static const int kDomV36[] = {0, 1};  // stadiums.stadiumgoalnettype
static const int kDomN36[] = {176, 5};
static const int kDomV37[] = {0, 1, 2};  // stadiums.stadiumgoalnettension
static const int kDomN37[] = {169, 2, 10};
static const int kDomV38[] = {0, 1};  // stadiums.stadiumgoalpoststyle
static const int kDomN38[] = {180, 1};
static const int kDomV39[] = {0, 1};  // teamkits.isgeneric
static const int kDomN39[] = {4053, 1};
static const int kDomV40[] = {0, 1};  // teamkits.jerseyfit
static const int kDomN40[] = {4050, 4};
static const int kDomV41[] = {0, 2, 3, 6, 7};  // teamkits.shortstyle
static const int kDomN41[] = {3920, 54, 57, 2, 21};
static const int kDomV42[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};  // teamkits.jerseycollargeometrytype
static const int kDomN42[] = {2188, 17, 83, 6, 17, 11, 10, 997, 179, 84, 7, 169, 3, 8, 18, 2, 3, 2, 6, 134, 15, 6, 37, 7, 10, 29, 6};
static const int kDomV43[] = {0, 1, 2};  // teamkits.jerseyrestriction
static const int kDomN43[] = {3706, 336, 12};
static const int kDomV44[] = {0, 1};  // teamkits.armbandtype
static const int kDomN44[] = {3658, 396};
static const int kDomV45[] = {0, 13, 14, 31, 65, 76, 78, 79, 84, 85, 86, 87, 88, 89, 100, 341, 351, 356};  // teamkits.captainarmband
static const int kDomN45[] = {1410, 91, 216, 73, 30, 857, 153, 60, 64, 8, 58, 37, 12, 6, 847, 54, 36, 42};
static const int kDomV46[] = {0, 1};  // teamkits.hasadvertisingkit
static const int kDomN46[] = {3456, 598};
static const int kDomV47[] = {0, 1};  // teamkits.jerseyfrontnumberplacementcode
static const int kDomN47[] = {3818, 236};
static const int kDomV48[] = {0, 1};  // teamkits.jerseybacknameplacementcode
static const int kDomN48[] = {236, 3818};
static const int kDomV49[] = {0, 1};  // teamkits.shortsnumberplacementcode
static const int kDomN49[] = {523, 3531};
static const int kDomV50[] = {0, 1};  // teamkits.jerseybacknamefontcase
static const int kDomN50[] = {4047, 7};
static const int kDomV51[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};  // teamkits.teamkittypetechid
static const int kDomN51[] = {1520, 964, 1400, 92, 11, 63, 1, 1, 1, 1};
static const GenDomain kGenDomains[] = {
    {"teams", "hastifo", 3, kDomV0, kDomN0},
    {"teams", "isbannerenabled", 3, kDomV1, kDomN1},
    {"teams", "crowdregion", 10, kDomV2, kDomN2},
    {"teams", "crowdskintonecode", 4, kDomV3, kDomN3},
    {"teams", "hasstandingcrowd", 3, kDomV4, kDomN4},
    {"teams", "hasvikingclap", 2, kDomV5, kDomN5},
    {"teams", "haslargeflag", 3, kDomV6, kDomN6},
    {"teams", "skinnyflags", 2, kDomV7, kDomN7},
    {"teams", "iscompetitionscarfenabled", 3, kDomV8, kDomN8},
    {"teams", "iscompetitionpoleflagenabled", 3, kDomV9, kDomN9},
    {"teams", "iscompetitioncrowdcardsenabled", 4, kDomV10, kDomN10},
    {"teams", "stanchionflamethrower", 3, kDomV11, kDomN11},
    {"teams", "flamethrowercannon", 3, kDomV12, kDomN12},
    {"teams", "hassubstitutionboard", 2, kDomV13, kDomN13},
    {"teams", "pitchwear", 2, kDomV14, kDomN14},
    {"teams", "stadiummowpattern_code", 14, kDomV15, kDomN15},
    {"teams", "playsurfacetype", 2, kDomV16, kDomN16},
    {"teams", "presassetone", 2, kDomV17, kDomN17},
    {"teams", "presassettwo", 3, kDomV18, kDomN18},
    {"teams", "ballid", 41, kDomV19, kDomN19},
    {"teams", "trainingstadium", 2, kDomV20, kDomN20},
    {"stadiums", "tifo", 2, kDomV21, kDomN21},
    {"stadiums", "supportsunlicensedtifos", 1, kDomV22, kDomN22},
    {"stadiums", "supportsunlicensedbanners", 1, kDomV23, kDomN23},
    {"stadiums", "seatcolor", 8, kDomV24, kDomN24},
    {"stadiums", "colorregion", 8, kDomV25, kDomN25},
    {"stadiums", "adboardtype", 2, kDomV26, kDomN26},
    {"stadiums", "policetypecode", 10, kDomV27, kDomN27},
    {"stadiums", "defaultweather", 8, kDomV28, kDomN28},
    {"stadiums", "tod1weather", 3, kDomV29, kDomN29},
    {"stadiums", "tod2weather", 3, kDomV30, kDomN30},
    {"stadiums", "tod3weather", 3, kDomV31, kDomN31},
    {"stadiums", "tod4weather", 3, kDomV32, kDomN32},
    {"stadiums", "hasmexicanwave", 2, kDomV33, kDomN33},
    {"stadiums", "pitchtarps", 2, kDomV34, kDomN34},
    {"stadiums", "goalnetstanchiontype", 4, kDomV35, kDomN35},
    {"stadiums", "stadiumgoalnettype", 2, kDomV36, kDomN36},
    {"stadiums", "stadiumgoalnettension", 3, kDomV37, kDomN37},
    {"stadiums", "stadiumgoalpoststyle", 2, kDomV38, kDomN38},
    {"teamkits", "isgeneric", 2, kDomV39, kDomN39},
    {"teamkits", "jerseyfit", 2, kDomV40, kDomN40},
    {"teamkits", "shortstyle", 5, kDomV41, kDomN41},
    {"teamkits", "jerseycollargeometrytype", 27, kDomV42, kDomN42},
    {"teamkits", "jerseyrestriction", 3, kDomV43, kDomN43},
    {"teamkits", "armbandtype", 2, kDomV44, kDomN44},
    {"teamkits", "captainarmband", 18, kDomV45, kDomN45},
    {"teamkits", "hasadvertisingkit", 2, kDomV46, kDomN46},
    {"teamkits", "jerseyfrontnumberplacementcode", 2, kDomV47, kDomN47},
    {"teamkits", "jerseybacknameplacementcode", 2, kDomV48, kDomN48},
    {"teamkits", "shortsnumberplacementcode", 2, kDomV49, kDomN49},
    {"teamkits", "jerseybacknamefontcase", 2, kDomV50, kDomN50},
    {"teamkits", "teamkittypetechid", 10, kDomV51, kDomN51},
};
// END GENERATED club_field_domains.py

// ---- the fields
enum class Section { Crowd, HomeGround, KitExtras };
enum class Look { Switch, Levels, Style, Weather, Fit, ShortStyle, KitType };
// Where the name of a value comes from (the hover says it)
enum class Src { Range, FieldName, LiveEditor, None };

struct Def {
    const char* table;  // "teams", "stadiums" or "teamkits" (never shown to the user)
    const char* field;
    Section section;
    const char* group;  // the sub-heading inside the section
    const char* title;
    const char* help;
    Look look;
    const char* noun;   // the word before the code of a "Style N" value (default "Style")
    bool editable;      // false: only used to name a value (teamkittypetechid)
};

inline const std::vector<Def>& defs() {
    static const std::vector<Def> d = {
        // ---- Crowd & presentation (the club row)
        {"teams", "hastifo", Section::Crowd, "Crowd", "Tifo display", "The big banner display the home fans hold up. The game gives its styles no names.", Look::Levels, "Style", true},
        {"teams", "isbannerenabled", Section::Crowd, "Crowd", "Crowd banners", "Banners hung in the stands.", Look::Levels, "Style", true},
        {"teams", "crowdregion", Section::Crowd, "Crowd", "Crowd region", "Which crowd set fills the stands. The game gives them no names.", Look::Style, "Style", true},
        {"teams", "crowdskintonecode", Section::Crowd, "Crowd", "Crowd skin tones", "The mix of skin tones among the fans. Only four mixes exist in the game's data.", Look::Style, "Style", true},
        {"teams", "hasstandingcrowd", Section::Crowd, "Crowd", "Standing sections", "Whether part of the crowd stands.", Look::Levels, "Style", true},
        {"teams", "hasvikingclap", Section::Crowd, "Crowd", "Viking clap", "The slow clap chant.", Look::Switch, "Style", true},
        {"teams", "haslargeflag", Section::Crowd, "Crowd", "Giant flag", "A giant flag across the stand.", Look::Levels, "Style", true},
        {"teams", "skinnyflags", Section::Crowd, "Crowd", "Thin flags", "Thin flags in the stands instead of the wide ones.", Look::Switch, "Style", true},
        {"teams", "iscompetitionscarfenabled", Section::Crowd, "Crowd", "Competition scarves", "Scarves of the competition in the crowd.", Look::Levels, "Style", true},
        {"teams", "iscompetitionpoleflagenabled", Section::Crowd, "Crowd", "Competition pole flags", "Pole flags of the competition in the crowd.", Look::Levels, "Style", true},
        {"teams", "iscompetitioncrowdcardsenabled", Section::Crowd, "Crowd", "Crowd cards", "Card displays held up by the crowd.", Look::Levels, "Style", true},
        {"teams", "stanchionflamethrower", Section::Crowd, "Effects", "Goal flame jets", "Flame jets at the goals.", Look::Levels, "Style", true},
        {"teams", "flamethrowercannon", Section::Crowd, "Effects", "Flame cannons", "Flame cannons at the walkout.", Look::Levels, "Style", true},
        {"teams", "hassubstitutionboard", Section::Crowd, "Effects", "Substitution board", "Show the electronic substitution board.", Look::Switch, "Style", true},
        {"teams", "presassetone", Section::Crowd, "Effects", "Presentation extra 1", "An extra piece of the match presentation. The game gives it no name.", Look::Switch, "Style", true},
        {"teams", "presassettwo", Section::Crowd, "Effects", "Presentation extra 2", "An extra piece of the match presentation. The game gives its styles no names.", Look::Style, "Style", true},
        {"teams", "pitchwear", Section::Crowd, "Pitch and ball", "Pitch wear", "How worn the pitch looks.", Look::Style, "Style", true},
        {"teams", "stadiummowpattern_code", Section::Crowd, "Pitch and ball", "Grass mowing pattern", "The stripes cut into the grass.", Look::Style, "Style", true},
        {"teams", "playsurfacetype", Section::Crowd, "Pitch and ball", "Playing surface", "The kind of surface the match is played on.", Look::Style, "Style", true},
        {"teams", "ballid", Section::Crowd, "Pitch and ball", "Match ball", "The ball used in this club's home matches.", Look::Style, "Ball", true},
        {"teams", "trainingstadium", Section::Crowd, "Pitch and ball", "Training ground", "The training ground scene of the club.", Look::Style, "Training ground", true},
        // ---- Home ground (the stadium row of the club's home ground)
        {"stadiums", "tifo", Section::HomeGround, "Stands", "Tifo allowed", "Whether this stadium can show a tifo.", Look::Switch, "Style", true},
        {"stadiums", "supportsunlicensedtifos", Section::HomeGround, "Stands", "Generic tifos allowed", "Whether the generic (unlicensed) tifos may be used here.", Look::Switch, "Style", true},
        {"stadiums", "supportsunlicensedbanners", Section::HomeGround, "Stands", "Generic banners allowed", "Whether the generic (unlicensed) banners may be used here.", Look::Switch, "Style", true},
        {"stadiums", "seatcolor", Section::HomeGround, "Stands", "Seat colour", "The colour of the seats. The game gives them no names.", Look::Style, "Style", true},
        {"stadiums", "colorregion", Section::HomeGround, "Stands", "Colour region", "The colour set of the stadium surroundings. The game gives it no names.", Look::Style, "Style", true},
        {"stadiums", "adboardtype", Section::HomeGround, "Stands", "Advertising boards", "The kind of boards around the pitch.", Look::Style, "Style", true},
        {"stadiums", "policetypecode", Section::HomeGround, "Stands", "Police set", "Which police figures stand around the pitch.", Look::Style, "Style", true},
        {"stadiums", "hasmexicanwave", Section::HomeGround, "Stands", "Mexican wave", "The crowd can do the wave.", Look::Switch, "Style", true},
        {"stadiums", "pitchtarps", Section::HomeGround, "Stands", "Pitch covers", "Covers on the pitch before the match.", Look::Style, "Style", true},
        {"stadiums", "defaultweather", Section::HomeGround, "Weather", "Default weather", "The weather this stadium starts with.", Look::Weather, "Style", true},
        {"stadiums", "tod1weather", Section::HomeGround, "Weather", "Weather, kick-off slot 1", "The weather for the first kick-off time slot.", Look::Weather, "Style", true},
        {"stadiums", "tod2weather", Section::HomeGround, "Weather", "Weather, kick-off slot 2", "The weather for the second kick-off time slot.", Look::Weather, "Style", true},
        {"stadiums", "tod3weather", Section::HomeGround, "Weather", "Weather, kick-off slot 3", "The weather for the third kick-off time slot.", Look::Weather, "Style", true},
        {"stadiums", "tod4weather", Section::HomeGround, "Weather", "Weather, kick-off slot 4", "The weather for the fourth kick-off time slot.", Look::Weather, "Style", true},
        {"stadiums", "goalnetstanchiontype", Section::HomeGround, "Goal frame", "Goal frame type", "The frame holding the goal net.", Look::Style, "Style", true},
        {"stadiums", "stadiumgoalnettype", Section::HomeGround, "Goal frame", "Goal net type", "The kind of goal net.", Look::Style, "Style", true},
        {"stadiums", "stadiumgoalnettension", Section::HomeGround, "Goal frame", "Goal net tension", "How tight the goal net is.", Look::Style, "Style", true},
        {"stadiums", "stadiumgoalpoststyle", Section::HomeGround, "Goal frame", "Goal post style", "The shape of the goal posts.", Look::Style, "Style", true},
        // ---- more kit options (one kit row)
        {"teamkits", "isgeneric", Section::KitExtras, "More kit options", "Generic kit design", "Use the game's generic shirt design instead of this club's own kit.", Look::Switch, "Style", true},
        {"teamkits", "jerseyfit", Section::KitExtras, "More kit options", "Jersey fit", "How closely the shirt fits.", Look::Fit, "Style", true},
        {"teamkits", "shortstyle", Section::KitExtras, "More kit options", "Shorts style", "The cut of the shorts.", Look::ShortStyle, "Style", true},
        {"teamkits", "jerseycollargeometrytype", Section::KitExtras, "More kit options", "Collar shape", "The collar of the shirt.", Look::Style, "Style", true},
        {"teamkits", "jerseyrestriction", Section::KitExtras, "More kit options", "Jersey restriction", "A restriction on when this kit may be chosen.", Look::Style, "Style", true},
        {"teamkits", "armbandtype", Section::KitExtras, "More kit options", "Armband type", "The kind of captain's armband.", Look::Style, "Style", true},
        {"teamkits", "captainarmband", Section::KitExtras, "More kit options", "Captain armband design", "The design of the captain's armband.", Look::Style, "Style", true},
        {"teamkits", "hasadvertisingkit", Section::KitExtras, "More kit options", "Sponsor shirt", "Use the shirt version with the sponsor.", Look::Switch, "Style", true},
        {"teamkits", "jerseyfrontnumberplacementcode", Section::KitExtras, "More kit options", "Front number placement", "Where the number sits on the front of the shirt.", Look::Style, "Style", true},
        {"teamkits", "jerseybacknameplacementcode", Section::KitExtras, "More kit options", "Back name placement", "Where the name sits on the back of the shirt.", Look::Style, "Style", true},
        {"teamkits", "shortsnumberplacementcode", Section::KitExtras, "More kit options", "Shorts number placement", "Where the number sits on the shorts.", Look::Style, "Style", true},
        {"teamkits", "jerseybacknamefontcase", Section::KitExtras, "More kit options", "Back name case", "How the name on the back is written.", Look::Style, "Style", true},
        // only names a kit (the header of the kit extras)
        {"teamkits", "teamkittypetechid", Section::KitExtras, "", "Kit", "Which kit this is.", Look::KitType, "Style", false},
    };
    return d;
}

inline const Def* find_def(const std::string& table, const std::string& field) {
    for (const Def& d : defs())
        if (table == d.table && field == d.field) return &d;
    return nullptr;
}

// ---- the value set of a field (ascending); empty when the header has none for it
inline const GenDomain* gen_domain(const Def& d) {
    for (const GenDomain& g : kGenDomains)
        if (std::strcmp(g.table, d.table) == 0 && std::strcmp(g.field, d.field) == 0) return &g;
    return nullptr;
}

inline std::vector<int> values_of(const Def& d) {
    std::vector<int> v;
    if (const GenDomain* g = gen_domain(d)) v.assign(g->values, g->values + g->n);
    return v;
}

inline bool in_domain(const Def& d, long long value) {
    const GenDomain* g = gen_domain(d);
    if (!g) return false;
    for (int i = 0; i < g->n; ++i)
        if (g->values[i] == value) return true;
    return false;
}

// ---- the names of values
struct ValueLabel {
    std::string text;  // empty when the value is not in the field's set
    Src src = Src::None;
};

inline std::string with_code(const char* noun, int v) { return std::string(noun) + " " + std::to_string(v); }

// The names Live Editor ships (loc\eng_us\localize.json), only for the values it names
inline const char* le_name(Look look, int v) {
    switch (look) {
        case Look::Weather: {
            static const char* const w[] = {"Clear", "Partly Clear", "Rain", "Snow", "Overcast"};
            return v >= 0 && v <= 4 ? w[v] : nullptr;
        }
        case Look::Fit: return v == 0 ? "Normal" : v == 1 ? "Tight" : nullptr;
        case Look::ShortStyle: return v == 0 ? "GK Shorts" : v == 1 ? "GK Pants" : nullptr;
        case Look::KitType: {
            static const char* const k[] = {"Home", "Away", "GK", "Third"};
            return v >= 0 && v <= 3 ? k[v] : nullptr;
        }
        default: return nullptr;
    }
}

// The name of `v` for this field. Empty text when `v` is not in the field's set - there is no name for a value the game's data never uses.
inline ValueLabel value_label(const Def& d, int v) {
    ValueLabel r;
    if (!in_domain(d, v)) return r;
    const std::vector<int> set = values_of(d);
    switch (d.look) {
        case Look::Switch:
            if (set.size() == 1 && set[0] == 1) { r.text = "On"; r.src = Src::Range; return r; }
            r.text = v == 0 ? "Off" : v == 1 ? "On" : with_code(d.noun, v);
            r.src = v <= 1 ? Src::Range : Src::None;
            return r;
        case Look::Levels:
            if (v == 0) { r.text = "Off"; r.src = Src::FieldName; return r; }
            r.text = with_code(d.noun, v);
            return r;
        case Look::Style:
            r.text = with_code(d.noun, v);
            return r;
        case Look::KitType:
        case Look::Weather:
        case Look::Fit:
        case Look::ShortStyle:
            if (const char* n = le_name(d.look, v)) { r.text = n; r.src = Src::LiveEditor; return r; }
            r.text = d.look == Look::KitType ? "Kit type " + std::to_string(v) + " (unknown)" : with_code(d.noun, v);
            return r;
    }
    return r;
}

// What a value that is stored but not in the set is called while it is shown
inline std::string other_label(int64_t v) { return "Other (" + std::to_string(v) + ")"; }

// The hover of a value: the stored number (never the word "code" before it), and where its name comes from
inline std::string value_hover(const Def& d, int v) {
    const ValueLabel l = value_label(d, v);
    std::string s = "Stored in the game's data as " + std::to_string(v) + " (" + d.field + ").\n";
    if (l.text.empty()) return s + "The game's data never uses this value.";
    switch (l.src) {
        case Src::Range: s += "On or off: this setting only takes 0 or 1."; break;
        case Src::FieldName: s += "Off is read from the setting's name; the game publishes no list for it."; break;
        case Src::LiveEditor: s += "The name is Live Editor's own."; break;
        case Src::None: s += "The game gives this value no name, so it is only numbered."; break;
    }
    return s;
}

// "" when `v` may be written to the field, else the sentence that says why not (it lists the choices by their names)
inline std::string refusal(const Def& d, long long v) {
    if (!d.editable) return std::string(d.title) + " cannot be changed here.";
    const std::vector<int> set = values_of(d);
    if (set.empty()) return std::string(d.title) + " has no list of values to choose from.";
    if (in_domain(d, v)) return std::string();
    std::string s = std::string(d.title) + ": the game's data never uses " + std::to_string(v) + ". Choose one of ";
    const size_t shown = std::min<size_t>(set.size(), 6);
    for (size_t i = 0; i < shown; ++i) {
        if (i) s += i + 1 == shown && set.size() == shown ? " or " : ", ";
        s += value_label(d, set[i]).text;
    }
    if (set.size() > shown) s += " ... (" + std::to_string(set.size()) + " in all)";
    return s + ".";
}

inline std::string title_of(const Def& d) { return d.title; }

// Every text the sections draw as a main text (titles and value names), for the label manifest of the tests
inline std::vector<std::string> main_texts() {
    std::vector<std::string> out;
    for (const Def& d : defs()) {
        if (!d.editable) continue;
        out.push_back(d.title);
        for (int v : values_of(d)) out.push_back(value_label(d, v).text);
    }
    return out;
}

}  // namespace clubfields
}  // namespace turbo
