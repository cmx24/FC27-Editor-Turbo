// Native tests: which commentary language the game LOADED (core/callnames.h detect_commentary_language,
// match_loaded_language, Callnames::detect_runtime_language) and the callnames per source (callname_population).
// Included by test_main.cpp; every bank, list and id is synthetic.
#pragma once
#include "core/callnames.h"
#include "core/game_settings.h"  // 4.0 lang-settings: the synthetic Settings file is built with gsettings::kFileSize / fix_crc

static void test_lang_detect() {
    using namespace turbo;
    const fs::path le = g_out / "lang_detect_world";
    fs::remove_all(le);
    fs::create_directories(le / "turbo" / "callnames");
    fs::create_directories(le / "turbo_output");
    const fs::path game = le / "game";
    make_live_pack(game, "ita_it");
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

    run_case("Language: detection order override > runtime > registry Locale > eng_us, each with a reason", [&] {
        const RegistryLocale reg = registry_locale_from("it_IT", "test", game);
        std::string why;
        CHECK(detect_commentary_language("SPA_ES", "eng_us", "x", "", "", reg, &why) == "spa_es" && why.find("MANUAL OVERRIDE") != std::string::npos,
              "override wins and says so: " + why);
        CHECK(detect_commentary_language("", "eng_us", "match 0.9", "", "", reg, &why) == "eng_us" && why.find("running game") != std::string::npos,
              "runtime evidence beats the registry: " + why);
        CHECK(detect_commentary_language("", "", "", "", "", reg, &why) == "ita_it" && why.find("registry Locale it_IT, pack present") == 0 &&
                  why.find("settings file: not read (no settings folder given)") != std::string::npos,
              "no runtime evidence: the registry Locale with its pack: " + why);
        CHECK(detect_commentary_language("", "", "", "", "", RegistryLocale{}, &why) == "eng_us" && why.find("base game") != std::string::npos, "nothing: eng_us: " + why);
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
        // kill switch: no runtime match, the watcher does not wait for one (the saved answer is not part of this check)
        fs::remove(loaded_language_path(le));
        { std::ofstream(lang_detect_off_path(le)) << "off"; }
        Callnames k;
        k.refresh(le, game, "");
        CHECK(k.runtime_off && k.settled(), "kill switch: settled without asking the game");
        CHECK(!k.detect_runtime_language(eng, le) && k.lang == "ita_it" && k.runtime_why.find("off") != std::string::npos, "kill switch: " + k.runtime_why);
        fs::remove(lang_detect_off_path(le));
    });

    run_case("Language review: thresholds (a 0.71 cross-language score never wins) and the asked-ids restriction", [&] {
        // synthetic overlap ratios measured on the shipped lists: answer A (1000 ids); ger_de 0.708, ita_it 0.52,
        // a weak eng_us 0.55. With the old 0.60 / 0.15 thresholds ger_de would have won with a 0.158 lead.
        std::unordered_set<int64_t> a, ger, ita2, weak;
        for (int64_t i = 0; i < 1000; ++i) a.insert(910001 + i);
        for (int64_t i = 0; i < 850; ++i) ger.insert(910001 + i);
        for (int64_t i = 0; i < 200; ++i) ger.insert(920001 + i);
        for (int64_t i = 0; i < 600; ++i) ita2.insert(910001 + i), weak.insert(910001 + i);
        for (int64_t i = 0; i < 150; ++i) ita2.insert(930001 + i);
        for (int64_t i = 0; i < 90; ++i) weak.insert(940001 + i);
        std::vector<LanguageCandidate> c = {{"eng_us", "spoken list", weak}, {"ger_de", "spoken list", ger}, {"ita_it", "spoken list", ita2}};
        LanguageMatch m = match_loaded_language(a, c);
        CHECK(m.lang.empty() && m.score > 0.70 && m.score < 0.72, fmt("ger_de at %.3f does not win: ", m.score) + m.why);
        CHECK(kLangMatchMin >= 0.85 && kLangMatchMargin >= 0.2, "floor 0.85, margin 0.2");
        // a smaller asked list: only 100 ids asked (910801..910900, all spoken). Unrestricted the true language scores 0.1;
        // restricted to the asked ids it scores 1.0 and ger_de (50 of them) 0.5
        std::unordered_set<int64_t> asked, answer;
        for (int64_t i = 0; i < 100; ++i) asked.insert(910801 + i), answer.insert(910801 + i);
        std::vector<LanguageCandidate> c2 = {{"eng_us", "spoken list", a}, {"ger_de", "spoken list", ger}, {"ita_it", "spoken list", ita2}};
        CHECK(match_loaded_language(answer, c2).lang.empty(), "unrestricted: the small answer matches nothing");
        m = match_loaded_language(answer, c2, &asked);
        CHECK(m.lang == "eng_us" && m.score > 0.99 && m.second < 0.51 && m.why.find("of 100 asked") != std::string::npos, "restricted: " + m.why);
    });

    run_case("Language review: the capture is cached under the evidence (each path)", [&] {
        // the four paths of capture_target
        CaptureTarget t = capture_target(true, "", "", "", "ita_it");
        CHECK(t.cache_lang == "ita_it" && t.adopt, "kill switch: cached under the language in use, as before");
        t = capture_target(false, "", "no language matches (unknown)", "", "ita_it");
        CHECK(t.cache_lang.empty() && !t.adopt && t.note.find("not cached") != std::string::npos, "unknown: not cached anywhere: " + t.note);
        t = capture_target(false, "ita_it", "x", "eng_us", "eng_us");
        CHECK(t.cache_lang == "ita_it" && !t.adopt && t.note.find("CONFLICT") != std::string::npos, "override differs: cached as the runtime language: " + t.note);
        t = capture_target(false, "eng_us", "x", "", "eng_us");
        CHECK(t.cache_lang == "eng_us" && t.adopt && t.note.empty(), "known: cached and used");
        // through Callnames: an Italian game with override eng_us never writes spoken_eng_us.json
        fs::remove(loaded_language_path(le));
        const fs::path ce = spoken_cache_path(le, "eng_us"), ci = spoken_cache_path(le, "ita_it");
        fs::remove(ce);
        fs::remove(ci);
        BankCapture cap;
        cap.ok = true;
        cap.surnames = ita;
        cap.players = {{70001, 1}};
        Callnames o;
        o.refresh(le, game, "eng_us");
        if (o.detect_runtime_language(ita, le)) o.refresh(le, game, "eng_us");
        std::string err, note;
        CHECK(o.lang == "eng_us" && o.runtime_lang == "ita_it", "override eng_us, the game speaks ita_it");
        CHECK(o.apply_runtime_capture(cap, le, "w", "B1", &err, &note) && fs::exists(ci) && !fs::exists(ce) && note.find("CONFLICT") != std::string::npos,
              "cached as spoken_ita_it.json only: " + note + err);
        // an unknown answer: the existing (valid) cache stays as it is, nothing new is written
        { std::ofstream(ci, std::ios::trunc) << "KEEP"; }
        fs::remove(loaded_language_path(le));
        Callnames u;
        u.refresh(le, game, "");
        std::unordered_set<int64_t> other;
        for (int64_t i = 960001; i <= 960300; ++i) other.insert(i);
        u.detect_runtime_language(other, le);
        BankCapture capu = cap;
        capu.surnames = other;
        err.clear();
        CHECK(u.runtime_lang.empty() && !u.apply_runtime_capture(capu, le, "w", "B1", &err, &note) && !fs::exists(ce) &&
                  err.find("not cached") != std::string::npos,
              "unknown: refused: " + err);
        std::string kept;
        { std::ifstream f(ci); std::getline(f, kept); }
        CHECK(kept == "KEEP", "the earlier spoken_ita_it.json is untouched");
        // a known answer without an override: cached under it
        fs::remove(ci);
        Callnames k;
        k.refresh(le, game, "");
        if (k.detect_runtime_language(eng, le)) k.refresh(le, game, "");
        CHECK(k.lang == "eng_us" && k.apply_runtime_capture(cap, le, "w", "B1", &err, &note) && fs::exists(ce) && !fs::exists(ci), "known: spoken_eng_us.json");
        fs::remove(ce);
        fs::remove(loaded_language_path(le));
    });

    run_case("Language review: the runtime answer is saved (step a2) and the language is unproven until then", [&] {
        fs::remove(loaded_language_path(le));
        Callnames a;
        a.game_build = "B1";
        a.refresh(le, game, "");
        CHECK(a.lang == "ita_it" && !a.lang_proven && a.lang_why.find("NOT PROVEN") != std::string::npos, "packs only: unproven: " + a.lang_why);
        if (a.detect_runtime_language(eng, le)) a.refresh(le, game, "");
        CHECK(a.lang == "eng_us" && a.lang_proven && fs::exists(loaded_language_path(le)), "runtime: proven, saved");
        Callnames b;  // the next session
        b.game_build = "B1";
        b.refresh(le, game, "");
        CHECK(b.lang == "eng_us" && b.lang_proven && b.lang_why.find("earlier session") != std::string::npos && !b.settled(),
              "saved answer used, still asked again this session: " + b.lang_why);
        Callnames c;  // another build
        c.game_build = "B2";
        c.refresh(le, game, "");
        CHECK(c.lang == "ita_it" && !c.lang_proven && c.lang_why.find("build B1") != std::string::npos, "other build: not used: " + c.lang_why);
        make_live_pack(game, "spa_es");
        Callnames d;  // other packs
        d.game_build = "B1";
        d.refresh(le, game, "");
        CHECK(!d.lang_proven && d.lang_why.find("other commentary packs") != std::string::npos, "other packs: not used: " + d.lang_why);
        remove_live_pack(game, "spa_es");
        Callnames o;
        o.refresh(le, game, "spa_es");
        CHECK(o.lang_proven, "a manual override counts as proven (the user's choice)");
        { std::ofstream(lang_detect_off_path(le)) << "off"; }
        fs::remove(loaded_language_path(le));
        Callnames k;
        k.refresh(le, game, "");
        CHECK(k.lang_proven && k.lang_why.find("runtime detection off") != std::string::npos, "kill switch: the packs act as before: " + k.lang_why);
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

    // ---- 4.0 lang-settings, step b: the commentary language the game saved in its Settings file (synthetic files: the layout's structure
    // and the one field, file offset 0x21AE). The suite's registry Locale is it_IT (test_locale_it) and the fake game has the ita_it and
    // fre_fr packs live, so the old code answers ita_it whatever the file says (Cassio 2026-10-29: his loaded language is French = value 3).
    const fs::path le2 = g_out / "lang_settings_world";
    fs::remove_all(le2);
    fs::create_directories(le2 / "turbo" / "callnames");
    fs::create_directories(le2 / "turbo_output");
    const fs::path game2 = le2 / "game";
    make_live_pack(game2, "ita_it");
    make_live_pack(game2, "fre_fr");
    const fs::path sdir = le2 / "settings";
    // the file as the game saves it, as far as gsettings::validate looks: header, tag, CRC'd length, constant words, CRC
    auto settings_bytes = [](uint32_t value) {
        namespace gs = turbo::gsettings;
        std::vector<uint8_t> b(gs::kFileSize, 0);
        std::memcpy(b.data(), "FBCHUNKS\x01\x00", 10);
        auto put = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(v >> (8 * k)); };
        put(10, uint32_t(gs::kMetaSize));
        put(14, uint32_t(gs::kPayloadSize));
        std::memcpy(b.data() + gs::kHeaderSize, "Personal Settings 1", 19);
        std::memcpy(b.data() + gs::kPayloadOff, "SaveType_Settin", 15);
        put(gs::kPayloadOff + gs::kCrcLenAt, uint32_t(gs::kPayloadSize - gs::kCrcFrom));
        for (int i = 0; i < 4; ++i) put(gs::kPayloadOff + gs::kConstAt + 4u * size_t(i), gs::kConstWords[i]);
        put(gs::kPayloadOff + kSettingsLangPayloadOff, value);
        gs::fix_crc(b);
        return b;
    };
    auto write_bytes = [](const fs::path& p, const std::vector<uint8_t>& b, int age_s) {
        { std::ofstream f(p, std::ios::binary | std::ios::trunc); f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size())); }
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::seconds(age_s));
    };
    auto put_settings = [&](const std::string& name, uint32_t value, int age_s) { write_bytes(sdir / name, settings_bytes(value), age_s); };
    auto fresh_settings_dir = [&] { fs::remove_all(sdir); fs::create_directories(sdir); };
    auto contains = [](const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; };

    run_case("Language settings: the value table (2 eng_us, 1 dut_nl, 3 fre_fr; anything else unknown, 5 and 10 included)", [&] {
        CHECK(settings_lang_for_value(2) == "eng_us" && settings_lang_for_value(1) == "dut_nl", "2 and 1: the proven values");
        CHECK(settings_lang_for_value(3) == "fre_fr", "3: French (Cassio's statement + the live file)");
        CHECK(settings_lang_for_value(5).empty() && settings_lang_for_value(10).empty() && settings_lang_for_value(0).empty() &&
                  settings_lang_for_value(4).empty() && settings_lang_for_value(0xFFFFFFFFu).empty(),
              "5, 10, 0, 4 and the rest are not known: never guessed");
        CHECK(!settings_value_proof(1).empty() && !settings_value_proof(2).empty() && !settings_value_proof(3).empty() && settings_value_proof(5).empty(),
              "every table value says what proves it, an unknown one nothing");
        CHECK(kSettingsLangPayloadOff + turbo::gsettings::kPayloadOff == 0x21AE, "payload 0x1D48 is file 0x21AE");
    });

    run_case("Language settings: the reader takes the newest Settings file, validated, and only that (read only)", [&] {
        fresh_settings_dir();
        SettingsLang none = read_settings_language(sdir);
        CHECK(!none.read && contains(none.why, "no Settings file"), "empty folder: " + none.why);
        CHECK(!read_settings_language(le2 / "nowhere").read && !read_settings_language(fs::path()).read, "no folder: nothing read");
        put_settings("Settings20261029100000000", 2, 600);
        put_settings("Settings20261029212252375", 3, 10);
        // files the game keeps beside it, newer than both: never the language source
        write_bytes(sdir / "Settings20261029212252375.bak", settings_bytes(2), 0);
        write_bytes(sdir / "CmMgrC20261029", settings_bytes(2), 0);
        write_bytes(sdir / "ProfileOptions", settings_bytes(2), 0);
        const std::string md5 = turbo::gsettings::md5_hex(settings_bytes(3));
        const auto stamp = fs::last_write_time(sdir / "Settings20261029212252375");
        SettingsLang r = read_settings_language(sdir);
        CHECK(r.read && r.value == 3 && r.lang == "fre_fr" && r.file == "Settings20261029212252375", "newest by time: " + r.why);
        std::vector<uint8_t> after;
        CHECK(turbo::gsettings::read_bytes(sdir / "Settings20261029212252375", after) && turbo::gsettings::md5_hex(after) == md5 &&
                  fs::last_write_time(sdir / "Settings20261029212252375") == stamp,
              "the file is byte-for-byte and time-for-time what it was");
        std::string f;
        int64_t t = 0;
        newest_settings_stamp(sdir, &f, &t);
        CHECK(f == r.file && t == r.mtime && t != 0, "the stat gives the same key as the read");
    });

    run_case("Language settings: value 3 + registry it_IT + both packs live -> fre_fr (FAIL-OLD: the old code says ita_it)", [&] {
        fresh_settings_dir();
        put_settings("Settings20261029212252375", 3, 0);
        Callnames old;  // no settings folder given: the old order
        old.refresh(le2, game2, "");
        CHECK(old.lang == "ita_it" && contains(old.lang_why, "registry Locale it_IT, pack present"), "without the file: the registry: " + old.lang_why);
        Callnames cn;
        cn.settings_dir = sdir;
        cn.refresh(le2, game2, "");
        CHECK(cn.lang == "fre_fr", "the Settings file decides: " + cn.log_line());
        CHECK(cn.settings_lang.read && cn.settings_lang.value == 3 && cn.settings_lang.pack_live, "read, value 3, pack live");
        CHECK(contains(cn.lang_why, "Settings20261029212252375") && contains(cn.lang_why, "commentary language field = 3 -> fre_fr"), "the reason names the file and the value: " + cn.lang_why);
        CHECK(cn.lang_proven && contains(cn.lang_why, "proven by Cassio's statement"), "3 is proven: " + cn.lang_why);
        CHECK(!contains(cn.lang_why, "not read (field not proven)"), "the old 'not read' text is gone");
        // the kill switch (runtime detection off) does not change the Settings answer
        { std::ofstream(lang_detect_off_path(le2)) << "off"; }
        Callnames k;
        k.settings_dir = sdir;
        k.refresh(le2, game2, "");
        CHECK(k.lang == "fre_fr" && k.lang_proven && !contains(k.lang_why, "runtime detection off"), "kill switch: still fre_fr: " + k.lang_why);
        fs::remove(lang_detect_off_path(le2));
    });

    run_case("Language settings: value 3 but the fre_fr pack is not live -> falls through, the reason names the pack", [&] {
        fresh_settings_dir();
        put_settings("Settings20261029212252375", 3, 0);
        remove_live_pack(game2, "fre_fr");
        Callnames cn;
        cn.settings_dir = sdir;
        cn.refresh(le2, game2, "");
        CHECK(cn.lang == "ita_it" && contains(cn.lang_why, "registry Locale it_IT"), "the registry answers: " + cn.lang_why);
        CHECK(contains(cn.lang_why, "fre_fr") && contains(cn.lang_why, "not live"), "and says why the file was not used: " + cn.lang_why);
        make_live_pack(game2, "fre_fr");
    });

    run_case("Language settings: value 2 -> eng_us (no pack to find), value 1 -> dut_nl when its pack is live; both proven", [&] {
        fresh_settings_dir();
        put_settings("Settings20261029212252375", 2, 0);
        Callnames e;
        e.settings_dir = sdir;
        e.refresh(le2, game2, "");
        CHECK(e.lang == "eng_us" && e.lang_proven && contains(e.lang_why, "field = 2 -> eng_us") && contains(e.lang_why, "proven by controlled in-game switches"),
              "value 2: " + e.lang_why);
        fresh_settings_dir();
        put_settings("Settings20261029212252376", 1, 0);
        Callnames d;
        d.settings_dir = sdir;
        d.refresh(le2, game2, "");
        CHECK(d.lang == "ita_it" && contains(d.lang_why, "dut_nl") && contains(d.lang_why, "not live"), "value 1 without a dut_nl pack falls through: " + d.lang_why);
        make_live_pack(game2, "dut_nl");
        Callnames d2;
        d2.settings_dir = sdir;
        d2.refresh(le2, game2, "");
        CHECK(d2.lang == "dut_nl" && d2.lang_proven, "value 1 with the pack: dut_nl, proven: " + d2.lang_why);
        remove_live_pack(game2, "dut_nl");
    });

    run_case("Language settings: a value outside the table (5, 7, 10, 0) is not guessed: the log says so and names the fallback", [&] {
        for (uint32_t v : {5u, 7u, 10u, 0u}) {
            fresh_settings_dir();
            put_settings("Settings20261029212252375", v, 0);
            Callnames cn;
            cn.settings_dir = sdir;
            cn.refresh(le2, game2, "");
            const std::string vs = std::to_string(v);
            CHECK(cn.lang == "ita_it" && !cn.lang_proven, "value " + vs + ": the registry, unproven: " + cn.lang_why);
            CHECK(contains(cn.lang_why, "Settings file value " + vs + ": language not known yet") && contains(cn.lang_why, "registry Locale it_IT"),
                  "value " + vs + " is named as unknown and the fallback is named: " + cn.lang_why);
        }
    });

    run_case("Language settings: the Settings file beats the running game's answer, the reason says they disagree", [&] {
        fresh_settings_dir();
        put_settings("Settings20261029212252375", 3, 0);
        const SettingsLang sl = settings_language_from(sdir, game2);
        const RegistryLocale reg = registry_locale_from("it_IT", "test", game2);
        std::string why;
        bool proven = false;
        CHECK(detect_commentary_language("", "eng_us", "match 0.97", "", "", reg, &why, &proven, nullptr, &sl) == "fre_fr", "settings wins: " + why);
        CHECK(contains(why, "the running game answered eng_us") && contains(why, "disagree") && proven, "the disagreement is named: " + why);
        CHECK(detect_commentary_language("", "fre_fr", "match 0.97", "", "", reg, &why, &proven, nullptr, &sl) == "fre_fr" && proven && contains(why, "the running game agrees"),
              "the running game agrees: proven: " + why);
        // a launcher hint that differs does not change the answer
        LauncherHint h;
        h.lang = "ita_it";
        CHECK(detect_commentary_language("", "", "", "", "", reg, &why, &proven, &h, &sl) == "fre_fr" && contains(why, "launcher hint ita_it differs"), "hint: " + why);
        // an unknown value hands the decision to the running game
        put_settings("Settings20261029212252376", 5, 0);
        const SettingsLang unk = settings_language_from(sdir, game2);
        CHECK(!unk.usable() && detect_commentary_language("", "eng_us", "match 0.97", "", "", reg, &why, &proven, nullptr, &unk) == "eng_us" &&
                  contains(why, "language not known yet"),
              "value 5: the running game decides, the log says the value was not known: " + why);
        fs::remove(sdir / "Settings20261029212252376");
        // the manual override still wins over everything
        CHECK(detect_commentary_language("ENG_US", "fre_fr", "x", "", "", reg, &why, &proven, nullptr, &sl) == "eng_us" && contains(why, "MANUAL OVERRIDE") && proven, "override: " + why);
        Callnames o;
        o.settings_dir = sdir;
        o.refresh(le2, game2, "ita_it");
        CHECK(o.lang == "ita_it" && contains(o.lang_why, "MANUAL OVERRIDE") && o.lang_proven, "override through refresh: " + o.lang_why);
    });

    run_case("Language settings: no Settings file, a missing folder or an invalid file leaves the old order, with the reason", [&] {
        fresh_settings_dir();
        Callnames none;
        none.settings_dir = sdir;
        none.refresh(le2, game2, "");
        CHECK(none.lang == "ita_it" && contains(none.lang_why, "registry Locale it_IT, pack present") && contains(none.lang_why, "no Settings file"), "empty folder: " + none.lang_why);
        Callnames gone;
        gone.settings_dir = le2 / "nowhere";
        gone.refresh(le2, game2, "");
        CHECK(gone.lang == "ita_it" && contains(gone.lang_why, "not read (the settings folder is not there"), "missing folder: " + gone.lang_why);
        // wrong size, and a flipped byte behind a good CRC: neither is read
        write_bytes(sdir / "Settings20261029212252375", std::vector<uint8_t>(1000, 0), 0);
        Callnames small;
        small.settings_dir = sdir;
        small.refresh(le2, game2, "");
        CHECK(small.lang == "ita_it" && contains(small.lang_why, "is not a valid Settings file"), "short file: " + small.lang_why);
        auto bad = settings_bytes(3);
        bad[0x3000] ^= 0xFF;
        write_bytes(sdir / "Settings20261029212252375", bad, 0);
        Callnames crc;
        crc.settings_dir = sdir;
        crc.refresh(le2, game2, "");
        CHECK(crc.lang == "ita_it" && contains(crc.lang_why, "CRC check failed"), "CRC: " + crc.lang_why);
        // the newest file is the one that counts: an invalid newest is not replaced by an older good one
        put_settings("Settings20261029100000000", 3, 600);
        Callnames newest;
        newest.settings_dir = sdir;
        newest.refresh(le2, game2, "");
        CHECK(newest.lang == "ita_it" && !newest.settings_lang.read, "newest invalid: older good file not used: " + newest.lang_why);
    });

    run_case("Language settings: two files - the newest by time wins, equal times fall back to the larger name", [&] {
        fresh_settings_dir();
        put_settings("Settings20261029100000000", 3, 600);
        put_settings("Settings20261029110000000", 2, 5);
        SettingsLang r = read_settings_language(sdir);
        CHECK(r.read && r.value == 2 && r.file == "Settings20261029110000000", "newer time wins: " + r.why);
        fresh_settings_dir();
        put_settings("Settings20261029110000000", 2, 0);
        put_settings("Settings20261029100000000", 3, 0);
        const auto same = fs::file_time_type::clock::now() - std::chrono::seconds(30);
        fs::last_write_time(sdir / "Settings20261029110000000", same);
        fs::last_write_time(sdir / "Settings20261029100000000", same);
        r = read_settings_language(sdir);
        CHECK(r.read && r.value == 2 && r.file == "Settings20261029110000000", "equal time: the later name wins: " + r.why);
    });

    run_case("Language settings: a new file or a new time asks for a refresh (stat only), a language switch in game is picked up", [&] {
        fs::remove_all(sdir);  // the folder does not exist yet
        Callnames cn;
        CHECK(!cn.settings_changed(), "no folder given: never changed");
        cn.settings_dir = sdir;
        cn.refresh(le2, game2, "");
        CHECK(cn.lang == "ita_it" && !cn.settings_changed(), "no folder: nothing to read, nothing changed");
        fs::create_directories(sdir);
        put_settings("Settings20261029100000000", 2, 60);
        CHECK(cn.settings_changed(), "a first Settings file appeared");
        cn.refresh(le2, game2, "");
        CHECK(cn.lang == "eng_us" && !cn.settings_changed(), "read: " + cn.lang_why);
        put_settings("Settings20261029110000000", 3, 0);  // the player switched to French; the game saved a new file
        CHECK(cn.settings_changed(), "a new file name");
        cn.refresh(le2, game2, "");
        CHECK(cn.lang == "fre_fr" && cn.prev_lang == "eng_us" && !cn.settings_changed(), "the switch is followed: " + cn.lang_why);
        fs::last_write_time(sdir / "Settings20261029110000000", fs::file_time_type::clock::now() + std::chrono::seconds(20));
        CHECK(cn.settings_changed(), "the same name with a new time");
        cn.refresh(le2, game2, "");
        CHECK(!cn.settings_changed(), "re-read");
    });
}
