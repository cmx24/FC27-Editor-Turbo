// Callnames from the game asset catalog (core/callname_bank.h, core/callnames_catalog.h, the catalog parts of
// core/callnames.cpp): bank per language and team, All callnames rows (hidden ids named and badged, master-only ids
// flagged), the voice-source gate, the count line, the bank as the spoken set. Synthetic data only: the schema v1
// fixture (tests/fixtures/asset_catalog_v1_small.json) plus a few hand-made master / database rows.
// Run: bash turbogui/tests/catalog/run_callnames.sh
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "core/asset_catalog.h"
#include "core/callname_bank.h"
#include "core/callnames.h"
#include "core/callnames_catalog.h"
#include "nlohmann/json.hpp"

using namespace turbo;
using json = nlohmann::json;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, what)                                                       \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_fail;                                                           \
            std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, std::string(what).c_str()); \
        }                                                                       \
    } while (0)

// Test-only reader of the fixture's callnames section and name tables (the real loader is core/asset_catalog, WP-05)
struct FixtureBanks {
    json j;
    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        j = json::parse(ss.str(), nullptr, false);
        return !j.is_discarded();
    }
    std::vector<BankTeamLists> lists(const std::string& lang) const {
        std::vector<BankTeamLists> out;
        const json& banks = j["families"]["callnames"];
        if (!banks.contains(lang)) return out;
        const json& teams = banks[lang]["teams"];
        for (auto it = teams.begin(); it != teams.end(); ++it) {
            BankTeamLists l;
            l.team = it.key();
            const char* keys[4] = {"generic", "simple", "link", "high"};
            for (size_t k = 0; k < 4; ++k)
                if (it.value().contains(keys[k])) l.kinds[k] = it.value()[keys[k]].get<std::vector<int64_t>>();
            out.push_back(std::move(l));
        }
        return out;
    }
    assets::Name name(assets::NameKind k, int64_t id) const {
        const char* table = k == assets::NameKind::Player ? "player" : k == assets::NameKind::Team ? "team" : "commentary";
        const json& t = j["names"][table];
        auto it = t.find(std::to_string(id));
        if (it == t.end()) return {};
        return {(*it)[0].get<std::string>(), (*it)[1].get<std::string>()};
    }
    CatalogBank bank(const std::string& lang, const std::string& team, bool current = true, bool hidden_off = false) const {
        return make_catalog_bank(lang, team, lists(lang), [this](assets::NameKind k, int64_t id) { return name(k, id); },
                                 j["build_key"].get<std::string>(), current, hidden_off);
    }
};

static const AllCallnameRow* find_row(const std::vector<AllCallnameRow>& rows, bool own, int64_t id) {
    for (const auto& r : rows)
        if (r.own() == own && (own ? r.playerid : r.commentaryid) == id) return &r;
    return nullptr;
}

int main(int argc, char** argv) {
    const std::string fixture = argc > 1 ? argv[1] : "tests/fixtures/asset_catalog_v1_small.json";
    FixtureBanks fx;
    if (!fx.load(fixture)) {
        std::printf("cannot read %s\n", fixture.c_str());
        return 2;
    }

    // ---- banks per language and team
    {
        const CatalogBank it = fx.bank("ita_it", "");
        CHECK(it.loaded() && !it.multi_team() && it.where() == "ita_it", "ita_it: one team");
        CHECK(it.generic.size() == 1 && it.has_generic(990001), "ita_it generic");
        CHECK(it.own.size() == 3 && it.own.at(90001) == kBankSimple && it.own.at(90004) == kBankSimple && it.own.at(90002) == kBankHigh,
              "ita_it own recordings with their kinds");
        CHECK(it.player_names.at(90001).text == "Test Head 90001" && it.player_names.at(90001).source == "master:ita_it", "catalog name");
        CHECK(!it.player_names.count(90004), "an unnamed id has no catalog name");
        CHECK(it.commentary_names.at(990001).text == "Testname", "generic name");

        const CatalogBank all = fx.bank("eng_us", "");
        CHECK(all.multi_team() && all.teams.size() == 2 && all.where() == "eng_us (every team)", "eng_us: two teams, every team");
        CHECK(all.own.size() == 3 && all.own.at(90003) == kBankLink && all.own_other_team.empty(), "every team: union of own recordings");
        CHECK(all.generic.size() == 2, "eng_us generic");
        const CatalogBank gm = fx.bank("eng_us", "gm");
        CHECK(gm.team == "gm" && gm.where() == "eng_us gm", "team gm");
        CHECK(gm.own.size() == 1 && gm.has_own(90001) && gm.own_other_team.count(90002) && gm.own_other_team.count(90003),
              "gm: own recordings of gm only; dr-only ones noted");
        CHECK(gm.generic.size() == 2, "generic recordings are shared by the teams (eng_us: only in dr)");
        CHECK(fx.bank("eng_us", "zz").team.empty(), "an unknown team = every team");
        CHECK(!fx.bank("fre_fr", "").loaded(), "no bank for a language the catalog lacks");
    }

    // ---- All callnames rows: bank + master list, names DB > catalog > master > id, Hidden / No recording
    MasterList m;
    m.lang = "ita_it";
    m.game = "fc27";
    m.generic_ids = {990001, 990003};
    m.generic_names = {{990003, "Mastergen"}, {990001, "Mastertext"}};
    m.real_players = {90001, 90005};
    m.names = {{90005, "Master Only"}, {90001, "Master Name 90001"}};
    CallnameIndex ix;
    ix.name_commentary = {{7, 990001}};
    ix.name_users = {{7, 3}};
    NameChoice nc;
    nc.nameid = 7;
    nc.name = "Dbsurname";
    nc.commentaryid = 990001;
    nc.users = 3;
    ix.names.push_back(nc);
    CallnameDbUse use;
    use.player_in_db = [](int64_t pid) { return pid == 90001; };
    use.player_name = [](int64_t pid) { return pid == 90001 ? std::string("Db Player") : std::string(); };
    {
        const CatalogBank b = fx.bank("ita_it", "");
        const auto rows = all_callnames_bank(b, m, ix, use);
        CHECK(rows.size() == 6, "2 generic + 4 own rows (" + std::to_string(rows.size()) + ")");
        CHECK(!rows.empty() && !rows[0].own() && !rows[1].own() && rows[2].own(), "generic rows first");
        const AllCallnameRow* g1 = find_row(rows, false, 990001);
        CHECK(g1 && g1->text == "Dbsurname" && g1->name_source == "db" && g1->recorded && !g1->hidden && g1->name_rows == 1 &&
                  g1->users == 3 && g1->nameid == 7,
              "generic in the bank and the save: DB name first");
        const AllCallnameRow* g3 = find_row(rows, false, 990003);
        CHECK(g3 && g3->text == "Mastergen" && g3->name_source == "master" && !g3->recorded && !g3->hidden && g3->in_master,
              "master-only generic: No recording");
        const AllCallnameRow* p1 = find_row(rows, true, 90001);
        CHECK(p1 && p1->text == "Db Player" && p1->name_source == "db" && !p1->hidden && p1->recorded, "player in the save");
        const AllCallnameRow* p2 = find_row(rows, true, 90002);
        CHECK(p2 && p2->text == "Test Head 90002" && p2->name_source == "workbook" && p2->hidden && p2->recorded && !p2->in_master,
              "player no longer in the DB: named from the catalog, Hidden");
        const AllCallnameRow* p4 = find_row(rows, true, 90004);
        CHECK(p4 && p4->text == "Callname #90004" && p4->name_source.empty() && p4->hidden, "unnamed hidden id: fallback name");
        const AllCallnameRow* p5 = find_row(rows, true, 90005);
        CHECK(p5 && p5->text == "Master Only" && !p5->recorded && !p5->hidden, "master-only player: No recording, not Hidden");
        CHECK(callname_name_source_label("db", m) == "name from: the database" &&
                  callname_name_source_label("master", m) == "name from: the FC 27 master" && callname_name_source_label("", m).empty(),
              "name source labels");

        // the filter
        using assets::Scope;
        CHECK(callname_row_shown(b, Scope::All, *p2) && !callname_row_shown(b, Scope::InSave, *p2) && callname_row_shown(b, Scope::Hidden, *p2),
              "hidden row: All and Hidden");
        CHECK(callname_row_shown(b, Scope::InSave, *p1) && !callname_row_shown(b, Scope::Hidden, *p1), "row of the save: In this save");
        CHECK(callname_row_shown(b, Scope::All, *p5) && !callname_row_shown(b, Scope::InSave, *p5) && !callname_row_shown(b, Scope::Hidden, *p5),
              "No recording: All only");
        const CatalogBank off = fx.bank("ita_it", "", true, /*hidden_off*/ true);
        CHECK(!callname_row_shown(off, Scope::All, *p2) && callname_row_shown(off, Scope::All, *p1), "asset_hidden_off.txt: hidden rows never listed");

        // the voice-source gate
        CHECK(voice_source_gate(b, *p2).ok, "hidden source with a recording, current catalog: allowed");
        CHECK(voice_source_gate(b, *g1).ok, "generic with a recording: allowed");
        const VoiceGate r5 = voice_source_gate(b, *p5);
        CHECK(!r5.ok && r5.why.find("no recording of this player in the ita_it commentary files") != std::string::npos,
              "a master-only id without a recording is refused as a voice source: " + r5.why);
        CHECK(!voice_source_gate(b, *g3).ok, "a master-only generic id is refused as a kick-off callname");
        const CatalogBank stale = fx.bank("ita_it", "", /*current*/ false);
        CHECK(!voice_source_gate(stale, *p2).ok && voice_source_gate(stale, *p1).ok, "stale catalog: hidden sources refused, the save's kept");
        CHECK(voice_source_gate(CatalogBank{}, *p5).ok, "no catalog: the 2.0 rules (no gate)");

        // the count line
        const CallnameCounts c = callname_counts(rows);
        CHECK(c.bank_generic == 1 && c.bank_own == 3 && c.master_generic == 2 && c.master_own == 2, "bank and master counts");
        CHECK(c.master_generic_unrecorded == 1 && c.master_own_unrecorded == 1 && c.bank_own_unlisted == 2 && c.bank_generic_unlisted == 0,
              "differences");
        CHECK(c.own_in_db == 1 && c.own_hidden == 2 && c.generic_in_db == 1 && c.generic_hidden == 0, "save counts");
        const std::string line = callname_count_line(c, b, m);
        CHECK(line.find("Bank ita_it: 1 generic, 3 own recordings (2 not in the master list)") == 0, "line: " + line);
        CHECK(line.find("FC 27 master: 2 generic, 2 own (2 without a recording)") != std::string::npos, "line master: " + line);
        CHECK(line.find("this save: 1 players with their own recording, 2 not in it (Hidden); 1 generic ids used, 0 not") != std::string::npos,
              "line save: " + line);
        CHECK(callname_count_line(c, stale, MasterList{}).find("no master list") != std::string::npos &&
                  callname_count_line(c, stale, MasterList{}).find("another game build") != std::string::npos,
              "line without master / stale");
    }
    {
        // eng_us, team gm: a dr-only recording is refused with the team named; an unused generic is Hidden
        const CatalogBank gm = fx.bank("eng_us", "gm");
        MasterList none;
        const auto rows = all_callnames_bank(gm, none, CallnameIndex{}, use);
        const AllCallnameRow* p3 = find_row(rows, true, 90003);
        CHECK(p3 && !p3->recorded, "gm: 90003 (dr link) not recorded for gm");
        const VoiceGate g = p3 ? voice_source_gate(gm, *p3) : VoiceGate{};
        CHECK(!g.ok && g.why.find("other eng_us commentary team, not gm") != std::string::npos, "team named: " + g.why);
        const AllCallnameRow* g2 = find_row(rows, false, 990002);
        CHECK(g2 && g2->hidden && g2->text == "Testothername" && g2->name_source == "master:eng_us", "unused generic: Hidden, catalog name");
        CallnameDbUse used = use;
        used.commentary_ids = {990002};
        const auto rows2 = all_callnames_bank(gm, none, CallnameIndex{}, used);
        const AllCallnameRow* g2b = find_row(rows2, false, 990002);
        CHECK(g2b && !g2b->hidden, "a generic id in commentarynames / playernamemap is in the save");
    }

    // ---- the bank as the spoken set and the own-recording source (Callnames::use_bank)
    {
        Callnames cn;
        cn.lang = "ita_it";
        cn.masters = m;  // an FC 27 master: without a catalog it decides alone
        CHECK(cn.own_recording(90005) == kOwnFromMasters && cn.own_recording(90002) == 0, "2.0: the FC 27 master decides");
        cn.use_bank(fx.bank("ita_it", "", /*current*/ false));
        CHECK(cn.spoken.from != SpokenSet::From::Catalog && cn.bank.loaded(), "stale: rows only, the spoken set is kept");
        cn.use_bank(fx.bank("ita_it", ""));
        CHECK(cn.spoken.from == SpokenSet::From::Catalog && cn.spoken.verified && cn.spoken.players_from == SpokenSet::From::Catalog,
              "current: the bank is the spoken set");
        CHECK(cn.own_recording(90002) == kOwnFromGame && cn.own_recording(90005) == 0, "the bank beats the master list");
        CHECK(cn.own_source(kOwnFromGame) == "the game's commentary files", "source name: " + cn.own_source(kOwnFromGame));
        CHECK(cn.spoken_answer(990001) == SpokenAnswer::Spoken && cn.spoken_answer(990003) == SpokenAnswer::Silent &&
                  cn.spoken_answer(900017) == SpokenAnswer::Silent,
              "spoken answers from the bank (a master-only generic is silent)");
        CHECK(cn.silent_source(990003).find("commentary files") != std::string::npos, "silent source: " + cn.silent_source(990003));
        cn.spoken.from = SpokenSet::From::ListFile;  // as refresh() leaves it with a hand-made list
        cn.use_bank(fx.bank("ita_it", ""));
        CHECK(cn.spoken.from == SpokenSet::From::Catalog && cn.spoken.source.find("spoken-id list is not used") != std::string::npos,
              "the bank beats spoken_<lang>.txt");
    }

    // ---- the real catalog API: off / not loaded = no bank (2.0 behaviour); a loaded fixture = the same bank as above
    {
        assets::Catalog none;
        CHECK(!catalog_bank(none, "ita_it", "").loaded(), "catalog not loaded: no bank");
        assets::Catalog cat;
        std::string err;
        if (cat.load_file(fixture, "0000AAAA-0000BBBB", &err) && cat.loaded()) {
            const CatalogBank b = catalog_bank(cat, "ita_it", "");
            const CatalogBank ref = fx.bank("ita_it", "");
            CHECK(b.loaded() && b.own == ref.own && b.generic == ref.generic && b.current, "catalog_bank = fixture bank");
            CHECK(b.player_names.size() == ref.player_names.size(), "names through Catalog::name");
            const CatalogBank gm = catalog_bank(cat, "eng_us", "gm");
            CHECK(gm.own_other_team.size() == 2, "teams through the Catalog");
        } else {
            std::printf("  note: the catalog loader is not in this build yet (%s): Catalog-backed bank checks skipped\n", err.c_str());
        }
    }

    std::printf("callnames catalog: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
