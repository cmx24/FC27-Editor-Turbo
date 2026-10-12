// Native tests: which commentary language the game LOADED (core/callnames.h detect_commentary_language,
// match_loaded_language, Callnames::detect_runtime_language) and the callnames per source (callname_population).
// Included by test_main.cpp; every bank, list and id is synthetic.
#pragma once
#include "core/callnames.h"

static void test_lang_detect() {
    using namespace turbo;
    const fs::path le = g_out / "lang_detect_world";
    fs::remove_all(le);
    fs::create_directories(le / "turbo" / "callnames");
    fs::create_directories(le / "turbo_output");
    const fs::path game = le / "game";
    fs::create_directories(game / "commentary" / "commentaryfull_ita_it");
    fs::create_directories(game / "Data" / "Win32");
    { std::ofstream(game / "Data" / "Win32" / "commentaryfull_eng_us.toc") << "x"; }

    // two synthetic banks: ita 900001..900100, eng 900051..900250 (50 ids shared)
    std::unordered_set<int64_t> ita, eng;
    for (int64_t i = 900001; i <= 900100; ++i) ita.insert(i);
    for (int64_t i = 900051; i <= 900250; ++i) eng.insert(i);
    auto write_list = [&](const std::string& lang, const std::unordered_set<int64_t>& ids) {
        std::ofstream f(le / "turbo" / "callnames" / ("spoken_" + lang + ".txt"));
        f << "#turbo-spoken " << lang << " " << ids.size() << "\n";
        for (int64_t id : ids) f << id << "\n";
    };
    write_list("ita_it", ita);
    write_list("eng_us", eng);

    run_case("Language: detection order override > runtime > packs > eng_us, each with a reason", [&] {
        std::vector<CommentaryPack> packs = {{"eng_us", false}, {"ita_it", true}};
        std::string why;
        CHECK(detect_commentary_language("SPA_ES", "eng_us", "x", packs, &why) == "spa_es" && why.find("MANUAL OVERRIDE") != std::string::npos,
              "override wins and says so: " + why);
        CHECK(detect_commentary_language("", "eng_us", "match 0.9", packs, &why) == "eng_us" && why.find("running game") != std::string::npos,
              "runtime evidence beats the packs: " + why);
        CHECK(detect_commentary_language("", "", "", packs, &why) == "ita_it" && why.find("installed packs") != std::string::npos &&
                  why.find("settings file: not read") != std::string::npos,
              "no runtime evidence: the downloaded pack: " + why);
        CHECK(detect_commentary_language("", "", "", {}, &why) == "eng_us" && why.find("base game") != std::string::npos, "nothing: eng_us: " + why);
    });

    run_case("Language: runtime match picks the clear winner, else unknown", [&] {
        std::vector<LanguageCandidate> c = {{"eng_us", "spoken list", eng}, {"ita_it", "spoken list", ita}};
        LanguageMatch m = match_loaded_language(ita, c);
        CHECK(m.lang == "ita_it" && m.score > 0.99, "the Italian answer -> ita_it: " + m.why);
        m = match_loaded_language(eng, c);
        CHECK(m.lang == "eng_us", "the English answer -> eng_us: " + m.why);
        std::unordered_set<int64_t> mixed;
        for (int64_t i = 900051; i <= 900100; ++i) mixed.insert(i);  // only the shared ids
        m = match_loaded_language(mixed, c);
        CHECK(m.lang.empty() && m.why.find("unknown") != std::string::npos, "ambiguous -> unknown: " + m.why);
        std::unordered_set<int64_t> other;
        for (int64_t i = 960001; i <= 960300; ++i) other.insert(i);
        m = match_loaded_language(other, c);
        CHECK(m.lang.empty() && m.why.find("no language matches") != std::string::npos, "another bank -> unknown: " + m.why);
        CHECK(match_loaded_language({}, c).lang.empty(), "empty answer -> unknown");
        CHECK(match_loaded_language(ita, {}).lang.empty(), "no candidates -> unknown");
    });

    run_case("Language: override key, refresh on a language change, kill switch", [&] {
        Callnames cn;
        cn.refresh(le, game, "");
        CHECK(cn.lang == "ita_it" && cn.log_line().find("callnames: language ita_it (") == 0, "packs: " + cn.log_line());
        CHECK(cn.spoken.from == SpokenSet::From::ListFile && cn.spoken.ids.size() == 100, "the ita_it list is loaded");
        CHECK(!cn.settled(), "runtime language not asked yet: the watcher keeps going");
        const auto cands = cn.language_candidates(le);
        CHECK(cands.size() == 2, fmt("two candidates (%zu)", cands.size()));
        // the game answers with the English bank: everything follows
        CHECK(cn.detect_runtime_language(eng, le), "a changed language asks for a refresh");
        CHECK(!cn.refreshed, "marked for refresh");
        cn.refresh(le, game, "");
        CHECK(cn.lang == "eng_us" && cn.prev_lang == "ita_it" && cn.lang_why.find("running game") != std::string::npos, "runtime: " + cn.lang_why);
        CHECK(cn.spoken.ids.size() == 200 && cn.spoken.spoken(900250) && !cn.spoken.spoken(900001), "the eng_us list replaced the Italian one");
        CHECK(cn.settled(), "settled after the runtime check");
        CHECK(!cn.detect_runtime_language(eng, le), "same answer: no refresh");
        // the manual override wins and is labelled
        cn.refresh(le, game, "ita_it");
        CHECK(cn.lang == "ita_it" && cn.lang_why.find("MANUAL OVERRIDE") != std::string::npos, "override: " + cn.lang_why);
        // kill switch: no runtime match, the watcher does not wait for one
        { std::ofstream(lang_detect_off_path(le)) << "off"; }
        Callnames k;
        k.refresh(le, game, "");
        CHECK(k.runtime_off && k.settled(), "kill switch: settled without asking the game");
        CHECK(!k.detect_runtime_language(eng, le) && k.lang == "ita_it" && k.runtime_why.find("off") != std::string::npos, "kill switch: " + k.runtime_why);
        fs::remove(lang_detect_off_path(le));
    });

    run_case("Language: callnames per source and their union", [&] {
        CatalogBank b;
        b.lang = "ita_it";
        b.generic = {900001, 900002, 900003};
        b.own = {{70001, 1}, {70002, 1}};
        b.own_other_team = {70003};
        MasterList m;
        m.generic_ids = {900003, 900004};
        m.real_players = {70002, 70009};
        CallnameIndex ix;
        ix.used_ids = {900000, 900002, 900005, 900006};
        ix.playernamemap = {{1, 900007}, {2, 900000}, {3, 900001}};
        const CallnamePopulation p = callname_population(b, m, ix);
        CHECK(p.bank_generic == 3 && p.bank_own == 2, "bank");
        CHECK(p.db_generic == 3 && p.db_specific == 2, fmt("db (%zu, %zu): 900000 is no callname", p.db_generic, p.db_specific));
        CHECK(p.master_generic == 2 && p.master_own == 2, "masters");
        CHECK(p.union_generic == 7, fmt("union generic 900001..900007 (%zu)", p.union_generic));
        CHECK(p.union_own == 4, fmt("union own 70001 70002 70003 70009 (%zu)", p.union_own));
        CHECK(callname_population_line(p).find("union 7 generic / 4 own") != std::string::npos, callname_population_line(p));
    });
}
