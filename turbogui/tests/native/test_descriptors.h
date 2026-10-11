// Native tests: 2.0/ux-descriptors ("NO MORE CODES instead of descriptors"). Every code-valued field the Turbo window shows
// has a descriptor for every value in its schema range (turbo/le27/fc27_db_schema.json, the game's own min / depth) or an
// explicit "Unnamed N" fallback: describe() never answers with a bare number, an empty text or "code N". Also the tables
// that came from Live Editor's localization (weather, morale, injuries: le_loc_labels_data.h), the ones the 0.4 label work
// added (boot colours, skin tone tens, hair colours 15..27, brands, who uses a body type) and the combo order helper.
// Included by test_main.cpp after test_describe.h; test_descriptors_core() runs from main with the core tests.
#pragma once
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/field_labels.h"
#include "fixture_player_codes.h"

// a text that is only a number (with an optional sign): the one thing a descriptor must never be
static bool bare_number(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// "code 3" as a word (a field name such as "bodytypecode 5" is not one)
static bool says_code_n(const std::string& text) {
    std::string low = text;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (size_t i = 0; (i = low.find("code ", i)) != std::string::npos; i += 5) {
        if (i > 0 && std::isalnum(static_cast<unsigned char>(low[i - 1]))) continue;
        if (i + 5 < low.size() && std::isdigit(static_cast<unsigned char>(low[i + 5]))) return true;
    }
    return false;
}

// The fields the audit found the window showing (UX_DESCRIPTORS_REPORT.txt), with the table they live in
struct AuditField {
    const char* table;
    const char* field;
};
static const AuditField kAuditFields[] = {
    {"players", "role1"}, {"players", "role5"}, {"players", "bodytypecode"}, {"players", "emotion"}, {"players", "skintonecode"},
    {"players", "haircolorcode"}, {"players", "facialhaircolorcode"}, {"players", "eyecolorcode"}, {"players", "accessorycode1"},
    {"players", "accessorycode4"}, {"players", "accessorycolourcode1"}, {"players", "shoecolorcode1"}, {"players", "shoecolorcode2"},
    {"players", "jerseysleevelengthcode"}, {"players", "hasseasonaljersey"}, {"players", "jerseyfit"}, {"players", "jerseystylecode"},
    {"players", "socklengthcode"}, {"players", "sockstylecode"}, {"players", "shortstyle"}, {"players", "preferredfoot"},
    {"players", "gender"}, {"players", "headclasscode"}, {"players", "weakfootabilitytypecode"}, {"players", "skillmoves"},
    {"players", "internationalrep"}, {"players", "gkkickstyle"}, {"players", "skillmoveslikelihood"}, {"players", "personality"},
    {"players", "undershortstyle"}, {"players", "shoedesigncode"}, {"players", "muscularitycode"}, {"players", "runstylecode"},
    {"players", "growthprofile"}, {"players", "shoetypecode"}, {"players", "gkglovetypecode"}, {"players", "headtypecode"},
    {"players", "eyebrowcode"}, {"players", "hairtypecode"}, {"players", "facialhairtypecode"}, {"players", "animfreekickstartposcode"},
    {"players", "animpenaltiesstartposcode"}, {"players", "runningcode1"}, {"players", "tattoohead"}, {"players", "tattooleftarm"},
    // fields FC 25+ dropped from the players table but Turbo still labels (enum_defs): checked over 0..64 when the schema lacks them
    {"players", "attackingworkrate"}, {"players", "defensiveworkrate"},
    // Live Editor's localization tables (the match setup view, morale and injury readers)
    {"match", "weather"}, {"players", "morale"}, {"players", "injury"},
};

// the schema range of a field: [min, min + 2^depth - 1]; false when the schema does not have it
static bool schema_range(const nlohmann::json& schema, const char* table, const char* field, int64_t& lo, int64_t& hi) {
    if (!schema.contains("tables") || !schema["tables"].contains(table)) return false;
    for (const auto& f : schema["tables"][table]["fields"]) {
        if (f.value("name", "") != field) continue;
        lo = f.value("min", 0LL);
        const int depth = f.value("depth", 0);
        hi = depth >= 62 ? INT64_MAX / 2 : lo + ((int64_t(1) << depth) - 1);
        if (f.contains("max")) hi = f["max"].get<int64_t>();
        return true;
    }
    return false;
}

static void test_descriptors_core() {
    using namespace turbo;
    run_case("descriptors: the data tables are in (loc names, brands, users, described hair colours)", [] {
        const size_t loc = sizeof(labels::data::kLocNamed) / sizeof(labels::data::kLocNamed[0]);
        CHECK(loc == 95, fmt("le_loc_labels_data.h: %zu entries (weather 5, morale 5, injury 85)", loc));
        CHECK(labels::describe("match", "weather", 0) == "Clear" && labels::describe("match", "weather", 4) == "Overcast" &&
                  labels::describe("match", "weather", 7) == "Unnamed 7",
              "weather: " + labels::describe("match", "weather", 7));
        CHECK(labels::describe("players", "morale", 4) == "Very Happy" && labels::describe("players", "morale", 0) == "Very Unhappy", "morale levels");
        CHECK(labels::describe("players", "injury", 12) == "Concussion" && labels::describe("players", "injury", 0) == "None" &&
                  labels::describe("players", "injury", 84) == "Unknown",
              "injury names: " + labels::describe("players", "injury", 12));
        CHECK(labels::find_users("bodytypecode", 16) != nullptr && std::string(labels::find_users("bodytypecode", 16)->names) == "Cristiano Ronaldo",
              "appearance_labels_data.h users");
        CHECK(labels::brand_of(labels::data::kGloves, labels::data::kGloveBrandNames, 0) == nullptr, "glove id 0 has no brand (it reads Default)");
        const std::vector<int64_t> hair = labels::offered_codes("haircolorcode", {}, 3);
        bool has27 = false, has0 = false;
        for (int64_t v : hair) {
            has27 = has27 || v == 27;
            has0 = has0 || v == 0;
        }
        CHECK(has0 && has27 && hair.size() == 28, fmt("a hair colour combo offers all 28 colours (%zu)", hair.size()));
        CHECK(labels::has_descriptors("role1") && labels::has_descriptors("weather") && labels::has_descriptors("skillmoves") &&
                  !labels::has_descriptors("shoetypecode") && !labels::has_descriptors("tattoohead"),
              "alphabetical combos for named fields, code order for item catalogues");
        CHECK(labels::code_label("role1", 14, "").kind == labels::Kind::Named &&
                  labels::code_label("haircolorcode", 21, "").kind == labels::Kind::Described &&
                  labels::code_label("role1", 9999, "").kind == labels::Kind::Unnamed,
              "a label says where its text came from");
    });

    run_case("descriptors: every audited field over its whole schema range: never a bare number, never empty, never 'code N'", [] {
        const char* tests_dir = std::getenv("TURBO_TESTS");
        const std::string schema_path = std::string(tests_dir ? tests_dir : "../turbo/tests") + "/../le27/fc27_db_schema.json";
        nlohmann::json schema;
        {
            std::ifstream in(schema_path);
            CHECK(in.good(), "the game DB schema is readable: " + schema_path);
            if (in.good()) in >> schema;
        }
        int in_schema = 0, checked = 0;
        for (const AuditField& af : kAuditFields) {
            int64_t lo = 0, hi = 64;
            const bool known = schema_range(schema, af.table, af.field, lo, hi);
            if (known) ++in_schema;
            // the whole range when it is small, else the ends plus a stride through the middle (eyebrowcode is 2^24)
            const int64_t span = hi - lo + 1;
            const int64_t step = span > 70000 ? span / 20000 : 1;
            int bad = 0;
            std::string first;
            for (int64_t v = lo; v <= hi; v += step) {
                const std::string d = labels::describe(af.table, af.field, v);
                ++checked;
                if (d.empty() || bare_number(d) || says_code_n(d)) {
                    if (!bad) first = std::string(af.field) + " " + std::to_string(v) + " -> '" + d + "'";
                    ++bad;
                }
                if (step > 1 && v + step > hi && v != hi) v = hi - step;  // always test the last value
            }
            CHECK(bad == 0, fmt("%s.%s [%lld..%lld]%s: %d bad, first %s", af.table, af.field, static_cast<long long>(lo), static_cast<long long>(hi),
                                known ? "" : " (not in the schema: 0..64)", bad, first.c_str()));
        }
        CHECK(in_schema >= 40, fmt("%d of %zu audited fields found in the schema, %d values checked", in_schema, sizeof(kAuditFields) / sizeof(kAuditFields[0]), checked));
    });

    run_case("descriptors: every value the real FC 27 players table holds (fixture_player_codes.h) has a text", [] {
        int fields = 0, values = 0, bad = 0, positions = 0, bad_pos = 0;
        std::string first, skipped;
        for (const FixtureCodes& fc : kPlayerCodes) {
            const std::string field = fc.field;
            if (field.rfind("preferredposition", 0) == 0) {  // positions read through core/model.h position_name()
                for (int64_t v : fc.values) {
                    ++positions;
                    if (v >= 0 && std::string(position_name(static_cast<int>(v))) == "?") ++bad_pos;
                }
                continue;
            }
            if (!labels::is_code_field(field) && !labels::enum_def(field)) {  // ids and flags (avatarpomid, iscustomized...) are numbers
                skipped += (skipped.empty() ? "" : ", ") + field;
                continue;
            }
            ++fields;
            for (int64_t v : fc.values) {
                ++values;
                const std::string d = labels::describe("players", fc.field, v);
                if (d.empty() || bare_number(d) || says_code_n(d)) {
                    if (!bad) first = std::string(fc.field) + " " + std::to_string(v) + " -> '" + d + "'";
                    ++bad;
                }
            }
        }
        CHECK(fields > 30 && values > 1000, fmt("fixture: %d code fields, %d values (not codes, skipped: %s)", fields, values, skipped.c_str()));
        CHECK(bad == 0, fmt("%d fixture values without a descriptor, first %s", bad, first.c_str()));
        CHECK(positions > 20 && bad_pos == 0, fmt("every preferredposition value of the database has a name (%d values, %d without)", positions, bad_pos));
    });
}
