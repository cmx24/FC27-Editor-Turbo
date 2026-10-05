// FC 27 LE Turbo GUI - native tests of the game-editor unlock (core/edit_unlock.h, ui/ui_edit_unlock.cpp).
// Included by test_main.cpp after its mini framework (CHECK, run_case, read_file, read_json, Ui). Every fixture below is
// SYNTHETIC: small JSON written for these tests in the shape of FC 27's avatar / gamesettings configs (attributeCategories
// -> filters.data, flags, dependency trees). No EA file is used or shipped.
#pragma once
#include <algorithm>

#include "core/edit_unlock.h"
#include "ui/ui_edit_unlock.h"

namespace eut {

using turbo::eu::ojson;
namespace eu = turbo::eu;

static std::string crlf(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '\n') o += '\r';
        o += c;
    }
    return o;
}

// Career squad Edit Player: names, kit number, birth date, height, position, role locked; TEAM / GENDER / PREFERRED_POSITION kept
static const char* kEditPlayers = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "FIRST_NAME", "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "KNOWN_AS", "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "COMMENTARY_NAME", "isEditable": false, "componentType": "LIST"},
				{"name": "GENDER", "isEditable": false, "isVisible": false, "componentType": "TOGGLE"},
				{"name": "KIT_NUMBER", "isEditable": false, "minValue": 1, "maxValue": 99, "componentType": "TOGGLE"},
				{"name": "TEAM", "isEditable": false, "componentType": "TOGGLE", "dataType": "INT", "dataPolicyType": "OPTIONS"},
				{"name": "BIRTH_YEAR", "isEditable": false, "componentType": "TOGGLE"},
				{"name": "BIRTH_MONTH", "isEditable": false, "minValue": 1, "maxValue": 12, "componentType": "TOGGLE"},
				{"name": "BIRTH_DAY", "isEditable": false, "minValue": 1, "maxValue": 31, "componentType": "TOGGLE"}
			]}}
		]}},
		{"name": "GEAR", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "MATCHDAY", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "SOCK", "filters": {"areFiltersAttributes": true, "data": [{"name": "KIT_SOCK"}]}}
			]}}
		]}},
		{"name": "ATHLETIC", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "BODY", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "HEIGHT", "isEditable": false, "minValue": 150, "maxValue": 210, "componentType": "SLIDER"},
				{"name": "GENDER", "isVisible": false}
			]}},
			{"name": "GAMEPLAY", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "POSITION", "isEditable": false, "componentType": "TOGGLE"},
				{"name": "ROLE", "isEditable": false, "componentType": "TOGGLE", "disabledOptions": [7, 9]},
				{"name": "PREFERRED_POSITION", "isVisible": false}
			]}}
		]}}
	],
	"globalAttributeDependencyTree": [
		{"parents": ["GENDER"], "children": ["FIRST_NAME"]},
		{"parents": ["POSITION"], "children": ["HEIGHT"]}
	]
}
)";

// Create-a-Club player: everything editable, an Attributes section and Brand animations
static const char* kCustomPlayer = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "FIRST_NAME", "minLength": 2, "maxLength": 12, "componentType": "INPUT_TEXT"},
				{"name": "KNOWN_AS", "minLength": 0, "maxLength": 12, "componentType": "INPUT_TEXT"},
				{"name": "GENDER", "isEditable": false, "isVisible": false, "componentType": "TOGGLE"},
				{"name": "BIRTH_YEAR", "minValue": 1990, "maxValue": 2008, "componentType": "TOGGLE"}
			]}}
		]}},
		{"name": "ATHLETIC", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "BODY", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "HEIGHT", "minValue": 150, "maxValue": 200, "componentType": "SLIDER"},
				{"name": "BODY_TYPE", "isVisible": false}
			]}},
			{"name": "GAMEPLAY", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "POSITION", "componentType": "TOGGLE"},
				{"name": "ROLE", "componentType": "TOGGLE", "disabledOptions": [7, 9]},
				{"name": "PREFERRED_POSITION", "isVisible": false}
			]}},
			{"name": "ATTRIBUTES", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "ATTACK", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "FINISHING", "minValue": 1, "maxValue": 99, "componentType": "BAR", "isEditable": true}
				]}},
				{"name": "DEFENDING", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "STANDING_TACKLE", "minValue": 1, "maxValue": 99, "componentType": "BAR"}
				]}},
				{"name": "MENTALITY", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "VISION", "minValue": 1, "maxValue": 99, "componentType": "BAR"}
				]}}
			]}}
		]}},
		{"name": "BRAND_ANIMATIONS", "filters": {"areFiltersAttributes": true, "data": [
			{"name": "GOALCELEBRATION", "disabledOptions": [101, 102]},
			{"name": "RUNNINGSTYLE"}
		]}}
	],
	"globalAttributeDependencyTree": [
		{"parents": ["GENDER"], "children": ["FIRST_NAME", "KNOWN_AS"]},
		{"parents": ["POSITION"], "children": ["HEIGHT", "ROLE"]},
		{"parents": ["ROLE"], "children": ["PREFERRED_POSITION"]},
		{"parents": ["BIRTH_MONTH", "BIRTH_YEAR"], "children": ["BIRTH_DAY"]},
		{"parents": ["HEIGHT", "WEIGHT"], "children": ["BODY_TYPE"]}
	]
}
)";

// Edit Manager (created manager): names and birth date locked, height hidden, a head editor
static const char* kMgrEdit = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "HEIGHT", "isVisible": false, "minValue": 160, "maxValue": 200, "componentType": "SLIDER"},
				{"name": "FIRST_NAME", "isMandatory": true, "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "APPELLATIVE", "isMandatory": true, "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "GENDER", "isVisible": false, "isMandatory": true, "isEditable": false, "componentType": "TOGGLE"},
				{"name": "BIRTH_YEAR", "isEditable": false, "componentType": "TOGGLE"},
				{"name": "BIRTH_MONTH", "isEditable": false, "minValue": 1, "maxValue": 12, "componentType": "TOGGLE"},
				{"name": "BIRTH_DAY", "isEditable": false, "minValue": 1, "maxValue": 31, "componentType": "TOGGLE"}
			]}}
		]}},
		{"name": "GEAR", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "MATCHDAY", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "TIES_OR_SCARVES", "filters": {"areFiltersAttributes": true, "data": [{"name": "OUTFITACCESSORY3"}]}}
			]}}
		]}},
		{"name": "CRANIUM_HEAD", "filters": {"areFiltersAttributes": true, "data": [{"name": "HEAD", "isCranium": true}]}}
	],
	"globalAttributeDependencyTree": [
		{"parents": ["OUTFITTOPLAYER1"], "children": ["MERGED_TOPS"]}
	]
}
)";

// Manager creation: the name lengths
static const char* kMgrCreate = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "GENDER", "isMandatory": true, "componentType": "TOGGLE"},
				{"name": "FIRST_NAME", "isMandatory": true, "minLength": 2, "maxLength": 12, "componentType": "INPUT_TEXT"},
				{"name": "APPELLATIVE", "isMandatory": true, "minLength": 2, "maxLength": 17, "componentType": "INPUT_TEXT"},
				{"name": "BIRTH_YEAR", "minValue": 1960, "maxValue": 2005, "componentType": "TOGGLE"}
			]}}
		]}}
	],
	"globalAttributeDependencyTree": [
		{"parents": ["GENDER"], "children": ["HEAD", "HEIGHT", "OUTFITSHOE"]},
		{"parents": ["BIRTH_MONTH", "BIRTH_YEAR"], "children": ["BIRTH_DAY"]}
	]
}
)";

// Edit Manager for a real manager, with EA's missing "{" before the last dependency entry (LF line ends)
static const char* kMgrRetired = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "FIRST_NAME", "isMandatory": true, "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "GENDER", "isVisible": false, "isMandatory": true, "isEditable": false, "componentType": "TOGGLE"},
				{"name": "BIRTH_YEAR", "isEditable": false, "componentType": "TOGGLE"}
			]}}
		]}},
		{"name": "GEAR", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "MATCHDAY", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "TIES_OR_SCARVES", "filters": {"areFiltersAttributes": true, "data": [{"name": "OUTFITACCESSORY3"}]}}
			]}}
		]}}
	],
	"globalAttributeDependencyTree": [
		{
			"parents": [
				"OUTFITTOPLAYER1"
			],
			"children": [
				"MERGED_TOPS"
			]
		},

			"parents": [
				"OUTFITTOPLAYER1",
				"GENDER"
			],
			"children": [
				"OUTFITACCESSORY3"
			]
		}
	]
}
)";

// Career start with a real manager
static const char* kMgrCreateReal = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "FIRST_NAME", "isMandatory": true, "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "GENDER", "isVisible": false, "isMandatory": true, "isEditable": false, "componentType": "TOGGLE"}
			]}}
		]}}
	]
}
)";

// The online manager editor: the outfit picker
static const char* kMgrOnline = R"({
	"attributeCategories": [
		{"name": "GEAR", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "MATCHDAY", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "INNER_TOPS", "filters": {"areFiltersAttributes": true, "data": [{"name": "OUTFITTOPLAYER1"}]}},
				{"name": "SHOE", "filters": {"areFiltersAttributes": true, "data": [{"name": "OUTFITSHOE"}]}}
			]}}
		]}}
	],
	"globalAttributeDependencyTree": [
		{"parents": ["OUTFITTOPLAYER1"], "children": ["OUTFITSHOE"]}
	]
}
)";

// Player Career position editor: the two hidden bars
static const char* kPosition = R"({
	"attributeCategories": [
		{"name": "GAMEPLAY", "filters": {"areFiltersAttributes": true, "data": [
			{"name": "COMPOSURE", "minValue": 1, "maxValue": 99, "componentType": "BAR", "isVisible": false},
			{"name": "DEFENSIVE_AWARENESS", "minValue": 1, "maxValue": 99, "componentType": "BAR", "isVisible": false}
		]}}
	]
}
)";

// Main menu Create Player (offline): tattoos, sleeves, sock styles, plus what Turbo must never graft (head editor, store
// outfits, the hidden bars)
static const char* kCreateOffline = R"({
	"attributeCategories": [
		{"name": "GEAR", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "MATCHDAY", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "TATTOO", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "TATTOOLEFTARM", "componentType": "LIST"}, {"name": "TATTOOBACK", "componentType": "LIST"}]}},
				{"name": "ARM_SLEEVES", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "LEFT_ARM_SLEEVE"}, {"name": "RIGHT_ARM_SLEEVE"}, {"name": "OUTFITTOPLAYER1"}]}},
				{"name": "SOCK", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "KIT_SOCK"}, {"name": "FASHION_SOCKS", "componentType": "LIST"}, {"name": "OUTFITSOCK"}]}},
				{"name": "INNER_TOPS", "filters": {"areFiltersAttributes": true, "data": [{"name": "OUTFITTOPLAYER1"}]}}
			]}}
		]}},
		{"name": "CRANIUM_HEAD", "filters": {"areFiltersAttributes": true, "data": [{"name": "HEAD", "isCranium": true}]}},
		{"name": "ATHLETIC", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ATTRIBUTES", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "MENTALITY", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "COMPOSURE", "minValue": 1, "maxValue": 99, "componentType": "BAR", "isVisible": false}]}}
			]}}
		]}}
	]
}
)";

// Main menu Edit Players
static const char* kMainMenu = R"({
	"attributeCategories": [
		{"name": "INFO", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "ABOUT_ME", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "FIRST_NAME", "isEditable": false, "componentType": "INPUT_TEXT"},
				{"name": "TEAM", "isEditable": false, "componentType": "TOGGLE"},
				{"name": "GENDER", "isEditable": false, "isVisible": false, "componentType": "TOGGLE"}
			]}}
		]}},
		{"name": "ATHLETIC", "filters": {"areFiltersAttributes": false, "data": [
			{"name": "BODY", "filters": {"areFiltersAttributes": true, "data": [
				{"name": "HEIGHT", "componentType": "SLIDER", "isEditable": false, "minValue": 143, "maxValue": 200}
			]}},
			{"name": "ATTRIBUTES", "filters": {"areFiltersAttributes": false, "data": [
				{"name": "DEFENDING", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "STANDING_TACKLE", "minValue": 1, "maxValue": 99, "componentType": "BAR"}
				]}},
				{"name": "MENTALITY", "filters": {"areFiltersAttributes": true, "data": [
					{"name": "VISION", "minValue": 1, "maxValue": 99, "componentType": "BAR"}
				]}}
			]}}
		]}}
	]
}
)";

static const char* kSettings = R"({"contexts": [
	{"name": "FROM_CAREER_MANAGER_HUB", "categories": [
		{"name": "CAREER_TRAINING", "settings": [{"name": "CAREER_DEVELOPMENT_RATE_SENIORS", "alwaysLocked": true}, {"name": "HALF_LENGTH"}]},
		{"name": "CAREER_TRANSFERS_SCOUTING", "settings": [{"name": "CAREER_TRANSFER", "alwaysLocked": true}]},
		{"name": "CAREER_GENERAL", "settings": [
			{"name": "CAREER_COMPETITION", "checker": {"op": "and", "parts": ["IsValidCareerCompetition"]}, "alwaysLocked": true},
			{"name": "CAREER_YOUTH_ACADEMY", "alwaysLocked": true}]}
	]},
	{"name": "FROM_CAREER_PLAYER_HUB", "categories": [
		{"name": "CAREER_TRAINING", "settings": [{"name": "CAREER_DEVELOPMENT_RATE_SENIORS", "alwaysLocked": true}]}
	]}
]}
)";

static const char* kSetup = R"({"contexts": [
	{"name": "FROM_CAREER_MANAGER_SETUP", "categories": [
		{"name": "CAREER_SQUAD", "settings": [{"name": "CAREER_EDIT_INJURIES"}, {"name": "CAREER_RELEASE_PLAYERS"}]}
	]}
]}
)";

static std::map<std::string, std::string> originals() {
    return {
        {eu::avatar_path("managercareer_editplayers"), crlf(kEditPlayers)},
        {eu::avatar_path("managercareer_edit_custom_player"), crlf(kCustomPlayer)},
        {eu::avatar_path("managercareer_edit"), crlf(kMgrEdit)},
        {eu::avatar_path("managercareer_create"), crlf(kMgrCreate)},
        {eu::avatar_path("managercareer_edit_retiredreal"), kMgrRetired},
        {eu::avatar_path("managercareer_create_real"), kMgrCreateReal},
        {eu::avatar_path("managercareer_edit_online"), kMgrOnline},
        {eu::avatar_path("playercareer_edit_position"), kPosition},
        {eu::avatar_path("mainmenu_create_offline"), kCreateOffline},
        {eu::avatar_path("mainmenu_edit_real"), kMainMenu},
        {eu::avatar_path("mainmenu_edit_created"), kMainMenu},
        {eu::kCareerSettingsPath, kSettings},
        {eu::kCareerSetupPath, kSetup},
    };
}

// node at "A/B/C" (names along attributeCategories -> filters.data), nullptr if absent
static const ojson* at(const ojson& doc, const std::string& path) {
    const ojson* list = doc.contains("attributeCategories") ? &doc["attributeCategories"] : nullptr;
    const ojson* node = nullptr;
    size_t s = 0;
    while (list && s <= path.size()) {
        size_t e = path.find('/', s);
        if (e == std::string::npos) e = path.size();
        std::string name = path.substr(s, e - s);
        node = nullptr;
        for (const auto& c : *list)
            if (c.is_object() && c.value("name", "") == name) { node = &c; break; }
        if (!node) return nullptr;
        list = node->contains("filters") ? &(*node)["filters"]["data"] : nullptr;
        s = e + 1;
        if (e == path.size()) return node;
    }
    return nullptr;
}
static bool flag(const ojson* n, const char* k, bool dflt = true) {
    return n && n->contains(k) ? (*n)[k].get<bool>() : dflt;
}
static bool has_dep(const ojson& doc, std::vector<std::string> parents, const std::string& child) {
    std::sort(parents.begin(), parents.end());
    for (const auto& e : doc["globalAttributeDependencyTree"]) {
        std::vector<std::string> p = e["parents"].get<std::vector<std::string>>();
        std::sort(p.begin(), p.end());
        if (p != parents) continue;
        for (const auto& c : e["children"])
            if (c.get<std::string>() == child) return true;
    }
    return false;
}

static void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
static fs::path rel(const fs::path& base, const std::string& legacy) {
    fs::path p = base;
    std::stringstream ss(legacy);
    std::string part;
    while (std::getline(ss, part, '/')) p /= part;
    return p;
}

// Every file now under <root>\mods\legacy\data (relative, '/'-separated)
static std::set<std::string> mods_files(const fs::path& root) {
    std::set<std::string> s;
    fs::path base = root / "mods" / "legacy";
    std::error_code ec;
    if (!fs::exists(base / "data", ec)) return s;
    for (fs::recursive_directory_iterator it(base / "data", ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) s.insert(fs::relative(it->path(), base).generic_string());
    return s;
}

}  // namespace eut

static void test_edit_unlock() {
    using namespace eut;
    const std::string EP = eu::avatar_path("managercareer_editplayers"), CP = eu::avatar_path("managercareer_edit_custom_player"),
                      ME = eu::avatar_path("managercareer_edit"), MR = eu::avatar_path("managercareer_edit_retiredreal"),
                      MCR = eu::avatar_path("managercareer_create_real"), MM = eu::avatar_path("mainmenu_edit_real"),
                      MC = eu::avatar_path("mainmenu_edit_created");

    run_case("edit unlock: SHA-256 test vectors", [&] {
        CHECK(eu::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
        CHECK(eu::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
        CHECK(eu::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              "two blocks");
        CHECK(eu::sha256_hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "a million a");
    });

    run_case("edit unlock: only the eight targets are ever written", [&] {
        int targets = 0;
        for (const auto& f : eu::files()) {
            bool w = eu::write_allowed(f.path);
            CHECK(w == (f.group != eu::Group::Source), "write_allowed matches the file list: " + f.path);
            targets += w;
        }
        CHECK(targets == 8, fmt("8 targets (%d)", targets));
        for (const char* p : {"data/avatar/avatarcustomizationcfg_managercareer_edit_online.json",
                              "data/avatar/avatarcustomizationcfg_managerlive_editplayers.json",
                              "data/avatar/avatarcustomizationcfg_playercareer_editplayers.json",
                              "data/avatar/avatarcustomizationcfg_clubs.json",
                              "data/avatar/avatarcustomizationcfg_tournament_editplayers.json",
                              "data/gamesettings/gamesettings_context_Online.json", "data/ui/imgAssets/heads/p1.dds",
                              "data/avatar/../avatar/avatarcustomizationcfg_managercareer_edit.json"}) {
            CHECK(!eu::write_allowed(p), std::string("refused: ") + p);
            eu::RecipeResult r = eu::build(p, {{p, kMainMenu}}, eu::Options());
            CHECK(!r.ok && r.text.empty(), std::string("build refuses ") + p);
        }
    });

    run_case("edit unlock: career Edit Player gets every field, Attributes and Brand animations; TEAM / GENDER stay", [&] {
        eu::RecipeResult r = eu::build(EP, originals(), eu::Options());
        CHECK(r.ok, "built: " + r.error);
        ojson d = ojson::parse(r.text);
        for (const char* p : {"INFO/ABOUT_ME/FIRST_NAME", "INFO/ABOUT_ME/KNOWN_AS", "INFO/ABOUT_ME/COMMENTARY_NAME",
                              "INFO/ABOUT_ME/KIT_NUMBER", "INFO/ABOUT_ME/BIRTH_YEAR", "INFO/ABOUT_ME/BIRTH_DAY",
                              "ATHLETIC/BODY/HEIGHT", "ATHLETIC/GAMEPLAY/POSITION", "ATHLETIC/GAMEPLAY/ROLE"})
            CHECK(flag(at(d, p), "isEditable", false), std::string("editable: ") + p);
        CHECK(!flag(at(d, "INFO/ABOUT_ME/TEAM"), "isEditable"), "TEAM stays locked");
        CHECK(!flag(at(d, "INFO/ABOUT_ME/GENDER"), "isEditable") && !flag(at(d, "INFO/ABOUT_ME/GENDER"), "isVisible"), "GENDER stays hidden");
        CHECK(!flag(at(d, "ATHLETIC/BODY/GENDER"), "isVisible"), "body GENDER stays hidden");
        CHECK(!flag(at(d, "ATHLETIC/GAMEPLAY/PREFERRED_POSITION"), "isVisible"), "PREFERRED_POSITION stays (set by ROLE)");
        const ojson* team = at(d, "INFO/ABOUT_ME/TEAM");
        CHECK(team && (*team)["dataType"] == "INT" && (*team)["dataPolicyType"] == "OPTIONS", "other keys kept");
        CHECK((*at(d, "ATHLETIC/GAMEPLAY/ROLE"))["disabledOptions"] == ojson::parse("[7, 9]"), "ROLE blacklist kept");
        CHECK(at(d, "ATHLETIC/ATTRIBUTES/ATTACK/FINISHING") && at(d, "ATHLETIC/ATTRIBUTES/MENTALITY/VISION"), "Attributes copied");
        CHECK(at(d, "BRAND_ANIMATIONS/GOALCELEBRATION") && at(d, "BRAND_ANIMATIONS/RUNNINGSTYLE"), "Brand animations copied");
        CHECK((*at(d, "BRAND_ANIMATIONS/GOALCELEBRATION"))["disabledOptions"].size() == 2, "celebration blacklist kept (stage 1)");
        CHECK(!at(d, "ATHLETIC/ATTRIBUTES/MENTALITY/COMPOSURE") && !at(d, "CRANIUM_HEAD"), "no experiments in stage 1");
        CHECK((*at(d, "ATHLETIC"))["filters"]["areFiltersAttributes"] == false, "filter flags kept");
        CHECK(has_dep(d, {"ROLE"}, "PREFERRED_POSITION") && has_dep(d, {"BIRTH_MONTH", "BIRTH_YEAR"}, "BIRTH_DAY"), "dependencies merged");
        CHECK(has_dep(d, {"POSITION"}, "ROLE") && has_dep(d, {"POSITION"}, "HEIGHT"), "same parents: children added to the entry");
        CHECK(!has_dep(d, {"GENDER"}, "KNOWN_AS") || at(d, "INFO/ABOUT_ME/KNOWN_AS"), "only names the target has");
        CHECK(!has_dep(d, {"HEIGHT", "WEIGHT"}, "BODY_TYPE"), "an entry whose parent the target lacks is skipped");
        CHECK(d["globalAttributeDependencyTree"].size() == 4, fmt("4 entries (%zu)", d["globalAttributeDependencyTree"].size()));
        bool all_crlf = r.text.find("\r\n") != std::string::npos;
        for (size_t i = 0; i < r.text.size(); ++i)
            if (r.text[i] == '\n' && (i == 0 || r.text[i - 1] != '\r')) all_crlf = false;
        CHECK(all_crlf, "CRLF like the original");
        CHECK(r.notes.size() >= 3 && r.notes[0] == "10 fields unlocked", "notes: " + (r.notes.empty() ? std::string() : r.notes[0]));
    });

    run_case("edit unlock: Unlock everything adds the game's gear to career Edit Player, nothing from the deny-list", [&] {
        eu::Options o;
        o.experimental = true;
        eu::RecipeResult r = eu::build(EP, originals(), o);
        CHECK(r.ok, r.error);
        ojson d = ojson::parse(r.text);
        CHECK(at(d, "GEAR/MATCHDAY/TATTOO/TATTOOLEFTARM") && at(d, "GEAR/MATCHDAY/TATTOO/TATTOOBACK"), "tattoos grafted");
        CHECK(at(d, "GEAR/MATCHDAY/ARM_SLEEVES/LEFT_ARM_SLEEVE") && !at(d, "GEAR/MATCHDAY/ARM_SLEEVES/OUTFITTOPLAYER1"),
              "sleeves grafted without the store outfit");
        CHECK(at(d, "GEAR/MATCHDAY/SOCK/FASHION_SOCKS") && !at(d, "GEAR/MATCHDAY/SOCK/OUTFITSOCK"), "sock style added to SOCK");
        CHECK((*at(d, "GEAR/MATCHDAY/SOCK"))["filters"]["data"].size() == 2, "KIT_SOCK not doubled");
        CHECK(!at(d, "GEAR/MATCHDAY/INNER_TOPS") && !at(d, "CRANIUM_HEAD") && !at(d, "ATHLETIC/ATTRIBUTES/MENTALITY/COMPOSURE") &&
                  !at(d, "ATHLETIC/ATTRIBUTES/DEFENDING/DEFENSIVE_AWARENESS"),
              "no head editor, store outfit, Composure or Defensive awareness");
        CHECK((*at(d, "BRAND_ANIMATIONS/GOALCELEBRATION"))["disabledOptions"].size() == 2, "celebration blacklist kept");
        CHECK(!flag(at(d, "INFO/ABOUT_ME/GENDER"), "isVisible") && !flag(at(d, "INFO/ABOUT_ME/TEAM"), "isEditable"),
              "GENDER hidden, TEAM locked");
        bool noted = false;
        for (const auto& n : r.notes) noted = noted || n.find("TATTOO, ARM_SLEEVES, SOCK") != std::string::npos;
        CHECK(noted, "the note lists the gear");
        // the gear needs GEAR/MATCHDAY on the screen: the Create-a-Club fixture has none, nothing is invented
        ojson cp = ojson::parse(eu::build(CP, originals(), o).text);
        CHECK(!at(cp, "GEAR"), "no gear section invented");
        // without the source file: no gear, still valid
        auto without = originals();
        without.erase(eu::avatar_path("mainmenu_create_offline"));
        eu::RecipeResult w = eu::build(EP, without, o);
        CHECK(w.ok && !at(ojson::parse(w.text), "GEAR/MATCHDAY/TATTOO"), "no source, no gear: " + w.error);
        // the second source is used when the first is missing
        without[eu::avatar_path("playercareer_edit_vpro")] = kCreateOffline;
        CHECK(at(ojson::parse(eu::build(EP, without, o).text), "GEAR/MATCHDAY/TATTOO"), "Player Career pro editor as the source");
        // every target, experimental: no name of the deny-list the game's own file lacks, and the output validates
        std::function<void(const ojson&, std::set<std::string>&)> names = [&](const ojson& j, std::set<std::string>& out) {
            if (j.is_object()) {
                if (j.contains("name") && j["name"].is_string()) out.insert(j["name"].get<std::string>());
                for (const char* k : {"attributeCategories", "filters", "data"})
                    if (j.contains(k)) names(j[k], out);
            } else if (j.is_array()) {
                for (const auto& v : j) names(v, out);
            }
        };
        int targets = 0;
        for (const auto& f : eu::files()) {
            if (f.group == eu::Group::Source || f.group == eu::Group::CareerSettings) continue;
            eu::RecipeResult x = eu::build(f.path, originals(), o);
            CHECK(x.ok, f.path + ": " + x.error);
            if (!x.ok) continue;
            ++targets;
            std::set<std::string> was, now;
            ojson orig;
            bool rep = false;
            eu::parse(originals()[f.path], orig, &rep, nullptr);
            names(orig, was);
            names(ojson::parse(x.text), now);
            for (const auto& n : now) CHECK(!eu::denied_name(n) || was.count(n), f.path + ": denied name added: " + n);
        }
        CHECK(targets == 7, fmt("the 7 editor targets built (%d)", targets));
        // validation itself refuses a denied name the original lacks
        ojson orig = ojson::parse(kEditPlayers), bad = orig;
        bad["attributeCategories"].push_back(ojson::parse(R"({"name": "CRANIUM_HEAD", "filters": {"data": [{"name": "HEAD"}]}})"));
        CHECK(eu::validate(EP, bad.dump(), orig, {}, eu::keep_list(EP, o)).find("CRANIUM_HEAD") == 0, "validate: head editor refused");
    });

    run_case("edit unlock: Create-a-Club player gets the commentary name from the career file", [&] {
        eu::RecipeResult r = eu::build(CP, originals(), eu::Options());
        CHECK(r.ok, r.error);
        ojson d = ojson::parse(r.text);
        const ojson& list = (*at(d, "INFO/ABOUT_ME"))["filters"]["data"];
        CHECK(list.size() == 5 && list[2]["name"] == "COMMENTARY_NAME", "inserted after KNOWN_AS");
        CHECK(list[2]["componentType"] == "LIST" && flag(&list[2], "isEditable", false), "copied and editable");
        CHECK(!flag(at(d, "ATHLETIC/BODY/BODY_TYPE"), "isVisible"), "BODY_TYPE stays hidden");
    });

    run_case("edit unlock: manager files: names, birth date, height; lengths from creation; GENDER always kept", [&] {
        eu::RecipeResult r = eu::build(ME, originals(), eu::Options());
        CHECK(r.ok, r.error);
        ojson d = ojson::parse(r.text);
        CHECK(flag(at(d, "INFO/ABOUT_ME/FIRST_NAME"), "isEditable", false) && flag(at(d, "INFO/ABOUT_ME/BIRTH_YEAR"), "isEditable", false),
              "names and birth date editable");
        CHECK(flag(at(d, "INFO/ABOUT_ME/HEIGHT"), "isVisible", false), "height shown");
        CHECK((*at(d, "INFO/ABOUT_ME/FIRST_NAME"))["minLength"] == 2 && (*at(d, "INFO/ABOUT_ME/FIRST_NAME"))["maxLength"] == 12, "first name 2..12");
        CHECK((*at(d, "INFO/ABOUT_ME/APPELLATIVE"))["maxLength"] == 17, "appellative 2..17");
        CHECK((*at(d, "INFO/ABOUT_ME/BIRTH_YEAR"))["minValue"] == 1930 && (*at(d, "INFO/ABOUT_ME/BIRTH_YEAR"))["maxValue"] == 2010,
              "editable birth year bounded 1930..2010 (not copied from creation)");
        CHECK(!flag(at(d, "INFO/ABOUT_ME/GENDER"), "isEditable"), "GENDER locked");
        CHECK(has_dep(d, {"BIRTH_MONTH", "BIRTH_YEAR"}, "BIRTH_DAY") && has_dep(d, {"GENDER"}, "HEAD"), "dependencies merged");
        CHECK(!has_dep(d, {"GENDER"}, "OUTFITSHOE"), "no outfit dependency without the outfit picker");
        CHECK(!at(d, "GEAR/MATCHDAY/INNER_TOPS"), "no outfit picker in stage 1");
        eu::Options o;
        o.experimental = true;
        ojson x = ojson::parse(eu::build(ME, originals(), o).text);
        CHECK(!flag(at(x, "INFO/ABOUT_ME/GENDER"), "isEditable") && !flag(at(x, "INFO/ABOUT_ME/GENDER"), "isVisible"),
              "manager gender stays locked even with experiments (closing Edit Manager could close the game)");
        CHECK(!at(x, "GEAR/MATCHDAY/INNER_TOPS") && !at(x, "GEAR/MATCHDAY/SHOE") && !has_dep(x, {"OUTFITTOPLAYER1"}, "OUTFITSHOE"),
              "no outfit picker with experiments either");
        CHECK(ojson::parse(eu::build(ME, originals(), eu::Options()).text) == x, "experimental Edit Manager = stage 1");
        ojson cr = ojson::parse(eu::build(MCR, originals(), o).text);
        CHECK(!flag(at(cr, "INFO/ABOUT_ME/GENDER"), "isEditable") && flag(at(cr, "INFO/ABOUT_ME/FIRST_NAME"), "isEditable", false),
              "career start: names, never the gender");
        // a real manager's name can be longer than the creation limits: no lengths on the real-manager files
        for (const char* screen : {"managercareer_edit_retiredreal", "managercareer_create_real"}) {
            eu::RecipeResult rr = eu::build(eu::avatar_path(screen), originals(), eu::Options());
            CHECK(rr.ok, rr.error);
            const ojson* fn = rr.ok ? at(ojson::parse(rr.text), "INFO/ABOUT_ME/FIRST_NAME") : nullptr;
            CHECK(rr.ok && rr.text.find("maxLength") == std::string::npos && (!fn || !fn->contains("minLength")),
                  std::string("no name lengths: ") + screen);
        }
    });

    run_case("edit unlock: every editable BIRTH_YEAR has a range (players 1960..2040, managers 1930..2010)", [&] {
        int checked = 0;
        for (const auto& f : eu::files()) {
            if (f.group == eu::Group::Source || f.group == eu::Group::CareerSettings) continue;
            eu::RecipeResult r = eu::build(f.path, originals(), eu::Options());
            CHECK(r.ok, f.path + ": " + r.error);
            if (!r.ok) continue;
            const ojson d = ojson::parse(r.text);
            std::function<void(const ojson&)> walk = [&](const ojson& j) {
                if (j.is_object()) {
                    if (j.value("name", std::string()) == "BIRTH_YEAR" && !(j.contains("isEditable") && j["isEditable"] == false)) {
                        ++checked;
                        CHECK(j.contains("minValue") && j.contains("maxValue"), f.path + ": BIRTH_YEAR without a range");
                        if (j.contains("minValue") && j.contains("maxValue") && !j.contains("filters"))
                            CHECK(j["minValue"].get<int>() < j["maxValue"].get<int>(), f.path + ": BIRTH_YEAR range order");
                    }
                    for (const auto& kv : j.items()) walk(kv.value());
                } else if (j.is_array()) {
                    for (const auto& v : j) walk(v);
                }
            };
            walk(d);
            const ojson* by = at(d, "INFO/ABOUT_ME/BIRTH_YEAR");
            if (by && f.path == eu::avatar_path("managercareer_editplayers"))
                CHECK((*by)["minValue"] == 1960 && (*by)["maxValue"] == 2040, "career Edit Player: 1960..2040");
        }
        CHECK(checked == 4, fmt("the 4 editable birth years of the fixtures checked (%d)", checked));
        // a range EA set is kept
        const ojson c = ojson::parse(eu::build(eu::avatar_path("managercareer_edit_custom_player"), originals(), eu::Options()).text);
        CHECK((*at(c, "INFO/ABOUT_ME/BIRTH_YEAR"))["minValue"] == 1990 && (*at(c, "INFO/ABOUT_ME/BIRTH_YEAR"))["maxValue"] == 2008,
              "EA's own range kept");
    });

    run_case("edit unlock: Re-read the game's files drops the cached exports and the originals", [&] {
        const fs::path le = g_out / "eu_reread";
        fs::remove_all(le);
        LegacyImages L(le);
        eu::EditUnlock s(L, le);
        const std::string EP = eu::avatar_path("managercareer_editplayers");
        for (const auto& kv : originals()) put(rel(L.cache_dir(), kv.first), kv.second);
        eu::Options o;
        s.collect(o);
        s.apply(o);
        CHECK(s.has_original(EP) && s.wrote(EP) && fs::exists(rel(L.mods_dir(), EP)), "unlocked");
        const std::string r = s.reread(o);
        CHECK(r.find("re-reading") != std::string::npos, r);
        CHECK(!fs::exists(rel(L.mods_dir(), EP)) && s.written_count() == 0, "Turbo's files removed");
        CHECK(!s.has_original(EP) && !fs::exists(rel(L.cache_dir(), EP)), "original and cached export dropped");
        CHECK(fs::exists(rel(L.cache_dir(), eu::kCareerSettingsPath)), "a file the options do not need is left alone");
        put(rel(L.cache_dir(), EP), crlf(kEditPlayers));  // the game exports it again
        s.collect(o);
        CHECK(s.has_original(EP), "the fresh export is the new original");
    });

    run_case("edit unlock: EA's missing brace in the real-manager file is repaired", [&] {
        ojson j;
        bool rep = false;
        std::string err;
        CHECK(!eu::parse(std::string(kMgrRetired).substr(0, 40), j, &rep, &err) && !err.empty(), "garbage still fails");
        CHECK(!ojson::accept(kMgrRetired), "the fixture is broken like EA's");
        CHECK(eu::parse(kMgrRetired, j, &rep, &err) && rep, "parsed after the repair");
        CHECK(j["globalAttributeDependencyTree"].size() == 2 && j["globalAttributeDependencyTree"][1]["children"][0] == "OUTFITACCESSORY3",
              "the entry is whole");
        CHECK(eu::repair_missing_brace(kMainMenu) == kMainMenu, "a valid file is left alone");
        eu::Options o;
        o.experimental = true;
        eu::RecipeResult r = eu::build(MR, originals(), o);
        CHECK(r.ok && r.repaired, "built: " + r.error);
        ojson d = ojson::parse(r.text);
        CHECK(!at(d, "CRANIUM_HEAD") && flag(at(d, "INFO/ABOUT_ME/FIRST_NAME"), "isEditable", false),
              "names; no head editor grafted onto a real manager");
        CHECK(r.text.find('\r') == std::string::npos, "LF like the original");
    });

    run_case("edit unlock: career hub settings: unlocked except the five that stay; squad settings with experiments", [&] {
        eu::RecipeResult r = eu::build(eu::kCareerSettingsPath, originals(), eu::Options());
        CHECK(r.ok, r.error);
        ojson d = ojson::parse(r.text);
        const ojson& hub = d["contexts"][0]["categories"];
        CHECK(!hub[0]["settings"][0].contains("alwaysLocked") && !hub[1]["settings"][0].contains("alwaysLocked"), "development, transfers");
        CHECK(hub[2]["settings"][0]["alwaysLocked"] == true && hub[2]["settings"][1]["alwaysLocked"] == true, "competition, academy stay");
        CHECK(hub[2]["settings"][0].contains("checker"), "checker kept");
        CHECK(d["contexts"][1]["categories"][0]["settings"][0]["alwaysLocked"] == true, "Player Career hub untouched");
        CHECK(hub.size() == 3, "no squad settings in stage 1");
        eu::Options o;
        o.experimental = true;
        ojson x = ojson::parse(eu::build(eu::kCareerSettingsPath, originals(), o).text);
        CHECK(x["contexts"][0]["categories"][1]["name"] == "CAREER_SQUAD", "squad settings before transfers");
        // fed its own output: nothing locked is left, so it is not the game's file
        auto again = originals();
        again[eu::kCareerSettingsPath] = r.text;
        eu::RecipeResult a = eu::build(eu::kCareerSettingsPath, again, eu::Options());
        CHECK(!a.ok && a.error.find("no locked setting") != std::string::npos, "already unlocked: " + a.error);
    });

    run_case("edit unlock: validation refuses bad output", [&] {
        ojson orig = ojson::parse(kEditPlayers);
        std::set<std::string> keep = eu::keep_list(EP, eu::Options()), none;
        auto with = [&](const std::function<void(ojson&)>& f) {
            ojson j = orig;
            f(j);
            return j.dump(1, '\t');
        };
        CHECK(eu::validate(EP, orig.dump(1, '\t'), orig, none, keep).empty(), "the original passes");
        CHECK(!eu::validate(EP, "{\"attributeCategories\": [", orig, none, keep).empty(), "not JSON");
        CHECK(!eu::validate(EP, "{\"attributeCategories\": []}", orig, none, keep).empty(), "no categories");
        CHECK(eu::validate(EP, with([](ojson& j) { j["attributeCategories"][0]["name"] = "HACKED"; }), orig, none, keep).find("unknown") == 0,
              "unknown name");
        CHECK(eu::validate(EP, with([](ojson& j) { j["attributeCategories"][0]["name"] = "HACKED"; }), orig, {"HACKED"}, keep).empty(),
              "a name an original uses is known");
        CHECK(!eu::validate(EP, with([](ojson& j) { j["attributeCategories"][2]["filters"]["data"][0]["filters"]["data"][0]["minValue"] = 300; }),
                            orig, none, keep).empty(),
              "minValue above maxValue");
        CHECK(!eu::validate(EP, with([](ojson& j) { j["attributeCategories"][0]["filters"]["data"][0]["filters"]["data"][0]["maxLength"] = 999; }),
                            orig, none, keep).empty(),
              "length out of range");
        CHECK(!eu::validate(EP, with([](ojson& j) { j["attributeCategories"][0]["filters"]["data"][0]["filters"]["data"][0]["isEditable"] = "yes"; }),
                            orig, none, keep).empty(),
              "flag not true/false");
        CHECK(eu::validate(EP, with([](ojson& j) { j["attributeCategories"][0]["filters"]["data"][0]["filters"]["data"][5]["isEditable"] = true; }),
                           orig, none, keep).find("TEAM") == 0,
              "TEAM unlocked by mistake is caught");
        CHECK(!eu::validate(EP, with([](ojson& j) { j["globalAttributeDependencyTree"][0]["children"].push_back("NOPE"); }), orig, none, keep).empty(),
              "unknown dependency name");
        CHECK(!eu::validate(EP, with([](ojson& j) { j["pad"] = std::string(300 * 1024, 'x'); }), orig, none, keep).empty(), "over 256 KB");
        std::map<std::string, std::string> bad = originals();
        bad[EP] = "{ not json";
        eu::RecipeResult r = eu::build(EP, bad, eu::Options());
        CHECK(!r.ok && r.error.find("does not parse") != std::string::npos, "a broken original is skipped: " + r.error);
    });

    run_case("edit unlock: originals first, manifest, apply, restore exactly Turbo's files", [&] {
        fs::path root = g_out / "eu_le";
        fs::remove_all(root);
        LegacyImages L(root);
        fs::path cache = L.cache_dir();
        std::map<std::string, std::string> orig = originals();
        for (const auto& kv : orig)
            if (kv.first != eu::kCareerSettingsPath && kv.first != eu::kCareerSetupPath) put(rel(cache, kv.first), kv.second);
        // another tool's file that Turbo must never touch, and a forbidden name next to the targets
        put(rel(L.mods_dir(), "data/avatar/avatarcustomizationcfg_managercareer_create.json"), "{\"other\": 1}");
        eu::EditUnlock s(L, root);
        eu::Options o;
        auto lines = s.collect(o);
        CHECK(lines.size() == 8, fmt("8 originals saved: 7 targets + manager creation (%zu)", lines.size()));
        CHECK(s.has_original(EP) && fs::exists(rel(s.originals_dir(), EP)), "original copied");
        json m = read_json(s.manifest_path());
        CHECK(m["originals"][EP]["sha256"] == eu::sha256_hex(orig[EP]), "hash in the manifest");
        CHECK(m["written"].empty(), "nothing written yet");
        CHECK(s.status(EP).state == eu::EditUnlock::State::Original, "status: original exported");
        CHECK(s.has_original(eu::avatar_path("managercareer_create")) &&
                  read_file(rel(s.originals_dir(), eu::avatar_path("managercareer_create"))) == orig[eu::avatar_path("managercareer_create")],
              "the game's create file, not the other tool's override");
        CHECK(!s.has_original(eu::avatar_path("playercareer_edit_position")), "experiment sources not asked for by default");
        CHECK(!s.has_original(eu::kCareerSettingsPath), "career settings not needed by default");
        std::string sum = s.apply(o);
        CHECK(sum.find("7 written") != std::string::npos, "summary: " + sum);
        std::set<std::string> mods = mods_files(root);
        for (const auto& p : mods) CHECK(p == "data/avatar/avatarcustomizationcfg_managercareer_create.json" || eu::write_allowed(p), "only targets: " + p);
        CHECK(mods.size() == 8, fmt("7 written + the other tool's file (%zu)", mods.size()));
        CHECK(read_file(rel(L.mods_dir(), "data/avatar/avatarcustomizationcfg_managercareer_create.json")) == "{\"other\": 1}",
              "the other tool's file is untouched");
        CHECK(s.status(EP).state == eu::EditUnlock::State::Unlocked, "status unlocked: " + s.status(EP).note);
        m = read_json(s.manifest_path());
        CHECK(m["written"].size() == 7 && m["written"][EP]["sha256"] == eu::sha256_hex(read_file(rel(L.mods_dir(), EP))), "manifest lists them");
        CHECK(m["written"][EP]["original_sha256"] == m["originals"][EP]["sha256"], "with the original's hash");
        CHECK(s.apply(o).find("0 written, 7 up to date") != std::string::npos, "second apply writes nothing");
        // Live Editor may now export the override: never taken as the original (even after Restore)
        put(rel(cache, EP), read_file(rel(L.mods_dir(), EP)));
        s.collect(o);
        CHECK(read_json(s.manifest_path())["originals"][EP]["sha256"] == eu::sha256_hex(orig[EP]), "override export refused");
        // someone edits one of Turbo's files: Restore keeps it
        put(rel(L.mods_dir(), MM), read_file(rel(L.mods_dir(), MM)) + " ");
        std::string rs = s.restore();
        CHECK(rs.find("6 files restored") != std::string::npos, "restore: " + rs);
        CHECK(s.status(MM).state == eu::EditUnlock::State::Kept && fs::exists(rel(L.mods_dir(), MM)), "changed file left alone");
        CHECK(!fs::exists(rel(L.mods_dir(), EP)) && s.status(EP).state == eu::EditUnlock::State::Restored, "Turbo's file removed");
        CHECK(fs::exists(rel(L.mods_dir(), "data/avatar/avatarcustomizationcfg_managercareer_create.json")), "other tool's file still there");
        CHECK(read_json(s.manifest_path())["written"].empty() && s.written_count() == 0, "manifest empty");
        CHECK(fs::exists(root / "turbo_output" / "edit_unlock" / "backups"), "removed files backed up to edit_unlock\\backups");
        s.collect(o);
        CHECK(read_json(s.manifest_path())["originals"][EP]["sha256"] == eu::sha256_hex(orig[EP]),
              "after Restore the old override export is still refused");
    });

    run_case("edit unlock: switches, waiting and missing files, title update", [&] {
        fs::path root = g_out / "eu_le2";
        fs::remove_all(root);
        LegacyImages L(root);
        std::map<std::string, std::string> orig = originals();
        put(rel(L.cache_dir(), EP), orig[EP]);
        eu::EditUnlock s(L, root);
        eu::Options o;
        o.manager = o.main_menu = o.created_players = false;
        s.collect(o);
        CHECK(!s.ready(o), "waiting for the Create-a-Club file");
        s.apply(o);
        CHECK(s.status(EP).state == eu::EditUnlock::State::Waiting && s.status(EP).note.find("custom_player") != std::string::npos,
              "Edit Player waits for its source: " + s.status(EP).note);
        L.flush();
        CHECK(read_file(L.cache_dir() / "want.txt").find(CP) != std::string::npos, "the source is asked from the game");
        // the game has no such file: go on without it
        put(L.cache_dir() / "missing.txt", CP + "\n");
        L.tick(100.0);
        s.collect(o);
        CHECK(s.ready(o), "ready once the source is known to be missing");
        s.apply(o);
        CHECK(s.status(EP).state == eu::EditUnlock::State::Unlocked, "written without the sections: " + s.status(EP).note);
        ojson d = ojson::parse(read_file(rel(L.mods_dir(), EP)));
        CHECK(!at(d, "ATHLETIC/ATTRIBUTES") && flag(at(d, "INFO/ABOUT_ME/FIRST_NAME"), "isEditable", false), "flags only");
        // per-file switch off: Turbo's file goes
        o.files_off.insert(EP);
        CHECK(s.apply(o).find("1 restored") != std::string::npos && !fs::exists(rel(L.mods_dir(), EP)), "switched off: removed");
        o.files_off.clear();
        s.apply(o);
        CHECK(fs::exists(rel(L.mods_dir(), EP)), "on again: written");
        // title update: the game's own file changed while no custom file is there (cache emptied, re-exported)
        s.restore();
        std::string updated = orig[EP];
        updated.replace(updated.find("\"maxValue\": 210"), 15, "\"maxValue\": 212");
        put(rel(L.cache_dir(), EP), updated);
        auto lines = s.collect(o);
        CHECK(lines.size() == 1 && lines[0].find("changed") != std::string::npos, "title update noticed");
        CHECK(read_json(s.manifest_path())["originals"][EP]["sha256"] == eu::sha256_hex(updated), "new original");
        s.apply(o);
        CHECK(read_file(rel(L.mods_dir(), EP)).find("212") != std::string::npos, "rebuilt from the new original");
        // an unreadable manifest is never overwritten and blocks writes
        s.restore();
        put(s.manifest_path(), "{ broken");
        eu::EditUnlock s2(L, root);
        CHECK(!s2.manifest_error().empty() && s2.apply(o) == s2.manifest_error(), "blocked: " + s2.manifest_error());
        CHECK(read_file(s2.manifest_path()) == "{ broken", "kept as it is");
        CHECK(!fs::exists(rel(L.mods_dir(), EP)), "nothing written");
    });

    run_case("edit unlock: options in gui_settings, defaults", [&] {
        eu::Options d;
        CHECK(d.enabled && d.hook && d.career_players && d.created_players && d.manager && d.main_menu && !d.career_settings &&
                  !d.experimental,
              "stage 1 and the in-memory fallback on, advanced and experiments off");
        CHECK(eu::Options::load(json::object()).signature() == d.signature() && eu::Options::load(json{{"edit_unlock", 3}}).enabled,
              "load: no or malformed object = defaults");
        eu::Options o = eu::Options::from_json(json{{"experimental", true}, {"files_off", json::array({MM})}});
        CHECK(o.experimental && o.career_players && o.files_off.count(MM) && !o.file_on(MM) && o.file_on(MC), "from json");
        CHECK(eu::Options::from_json(o.to_json()).signature() == o.signature(), "round trip");
        CHECK(!o.file_on(eu::kCareerSettingsPath) && !o.file_on(eu::avatar_path("managercareer_create")), "advanced off, sources never");
    });
}

#ifndef TURBO_EU_CORE_ONLY  // (a scratch driver can build the core cases alone)
// Turbo Tools > Game editors, driven through the UI (called from test_ui with its App)
static void eu_scroll_to(Ui& ui, const ItemRec* r) {
    if (!r) return;
    ImGuiWindow* w = nullptr;
    for (ImGuiWindow* x : GImGui->Windows)
        if (r->window == x->Name) w = x;
    if (!w) return;
    ImGui::SetScrollY(w, w->Scroll.y + (r->rect.Min.y - w->InnerClipRect.Min.y) - 2.0f);
    ui.frames(2);
}
static void eu_scroll_bottom(Ui& ui, const std::string& part) {
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find(part) != std::string::npos) ImGui::SetScrollY(w, w->ScrollMax.y);
    ui.frames(2);
    for (ImGuiWindow* w : GImGui->Windows)
        if (std::string(w->Name).find(part) != std::string::npos) ImGui::SetScrollY(w, w->ScrollMax.y);
    ui.frames(2);
}

static void test_edit_unlock_ui(App& app, Ui& ui, const fs::path& le) {
    using namespace eut;
    run_case("UI: Game editors: write the unlocked files, restore, switch", [&] {
        const std::string EP = eu::avatar_path("managercareer_editplayers");
        for (const auto& kv : originals()) put(rel(app.legacy.cache_dir(), kv.first), kv.second);
        put(app.legacy.cache_dir() / "missing.txt", eu::avatar_path("playercareer_edit_vpro") + "\n");  // the second gear source
        CHECK(ui.click("Turbo Tools"), "Tools tab");
        ui.frames(2);
        eu_scroll_bottom(ui, "##tools");
        const char* header = "Game editors (unlock FC 27's own edit screens)";
        CHECK(ui.click(header), "Game editors header");
        eu_scroll_to(ui, ui.find(header));
        CHECK(ui.click("Write the unlocked files now"), "write now");
        fs::path out = rel(le / "mods" / "legacy", EP);
        CHECK(fs::exists(out), "career Edit Player file written");
        CHECK(ui.toast_contains("Game editors: "), "summary toast");
        eu::EditUnlock& s = edit_unlock_service(app);
        CHECK(s.written_count() == 7, fmt("7 files (%zu)", s.written_count()));
        CHECK(ui.click("Unlock everything (experimental)"), "experiments on");
        json gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["edit_unlock"]["experimental"] == true && gs["edit_unlock"]["enabled"] == true, "saved");
        ui.frames(70);  // the tick applies within a second
        CHECK(read_file(out).find("TATTOO") != std::string::npos, "rewritten with the experiments (gear)");
        CHECK(ui.click("Unlock everything (experimental)"), "experiments off");
        ui.frames(70);
        CHECK(read_file(out).find("TATTOO") == std::string::npos, "back to stage 1");
        // one section: the in-memory fallback's switch lives inside it and saves into the same object
        CHECK(!ui.find("Game editors (unlock the game's own Edit Player / Edit Manager)"), "one Game editors section");
        const char* fallback = "Also patch the editors in memory (fallback when Live Editor ignores the files)";
        std::vector<std::pair<ImGuiWindow*, float>> scroll;  // put back afterwards: the buttons above are clicked next
        for (ImGuiWindow* w : GImGui->Windows)
            if (std::string(w->Name).find("##tools") != std::string::npos) scroll.push_back({w, w->Scroll.y});
        eu_scroll_bottom(ui, "##tools");  // the fallback sits at the end of the section, the last one of the tab
        CHECK(ui.click(fallback), "in-memory fallback off");
        gs = read_json(le / "turbo_output" / "gui_settings.json");
        CHECK(gs["edit_unlock"]["hook"] == false && gs["edit_unlock"]["enabled"] == true && gs["edit_unlock"]["career_players"] == true &&
                  gs["edit_unlock"]["files_off"].is_array(),
              "one settings object: " + gs["edit_unlock"].dump());
        CHECK(ui.click(fallback), "in-memory fallback on");
        for (const auto& ws : scroll) ImGui::SetScrollY(ws.first, ws.second);
        ui.frames(2);
        CHECK(ui.click("Restore the game's originals"), "restore");
        CHECK(!fs::exists(out) && s.written_count() == 0, "removed");
        CHECK(read_json(le / "turbo_output" / "gui_settings.json")["edit_unlock"]["enabled"] == false, "switch off after Restore");
        ui.frames(70);
        CHECK(!fs::exists(out), "not written again while off");
        CHECK(ui.click("Unlock the game's editors"), "switch on");
        ui.frames(70);
        CHECK(fs::exists(out), "written again by the tick");
        CHECK(ui.click("Unlock the game's editors"), "switch off");
        CHECK(!fs::exists(out) && s.written_count() == 0, "switching off restores");
        CHECK(ui.click(header), "collapse");
        // leave the default and the scroll position for the rest of the suite
        for (ImGuiWindow* w : GImGui->Windows)
            if (std::string(w->Name).find("##tools") != std::string::npos) ImGui::SetScrollY(w, 0.0f);
        app.gui_settings.erase("edit_unlock");
        app.save_gui_settings();
        fs::remove_all(app.legacy.cache_dir() / "data" / "avatar");
        fs::remove_all(app.legacy.cache_dir() / "data" / "gamesettings");
        fs::remove_all(le / "turbo_output" / "edit_unlock");
    });
}
#endif
