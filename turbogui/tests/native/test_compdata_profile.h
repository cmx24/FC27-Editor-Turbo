// Native tests (2.21 compdata guard, core/compdata_profile.h, and the launcher-hint rank, core/callnames.h): the launcher's
// record (missing -> UNKNOWN, never MISMATCH; BOM; bad values), the disk state of the 11 files, record vs disk, the seeded career
// list (shipped block, built-in fallback, bad entries), the decision table and its fixed log heads; the hint rank (hint + live pack,
// hint + absent pack, hint older / newer than a saved answer, manual override wins, a hint answer is never proven), the
// Data\Win32-only pack test and (CHANGE 8) the registry Locale step: the exe's locale table, a missing pack, override precedence. Synthetic folders only. Included by test_main.cpp; compdata_profile_test::run() is called from main
// next to crest_serve_test::run().
#pragma once
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/callnames.h"
#include "core/compdata_profile.h"

namespace compdata_profile_test {

namespace cd = turbo::compdata;

inline bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
inline bool starts(const std::string& s, const std::string& head) { return s.rfind(head, 0) == 0; }

inline void run() {
    std::printf("native compdata guard + launcher hint (2.21)\n");

    run_case("compdata guard: the launcher's record (missing, BOM, bad JSON, bad profile, set_at)", [] {
        cd::Record r = cd::parse_record("", false);
        CHECK(!r.present && r.profile == cd::Profile::Unknown && has(r.why, "has not run"), "missing file: unknown: " + r.why);
        r = cd::parse_record("{\"profile\": \"stock\", \"set_at\": \"2026-10-19 16:20:00\", \"files\": []}", true);
        CHECK(r.profile == cd::Profile::Stock && r.set_at == "2026-10-19 16:20:00" && r.why.empty(), "stock record");
        r = cd::parse_record("\xEF\xBB\xBF{\"profile\": \"EERSTE\"}", true);
        CHECK(r.profile == cd::Profile::Eerste, "a UTF-8 BOM (PowerShell 5.1) and upper case are read: " + r.why);
        r = cd::parse_record("not json", true);
        CHECK(r.profile == cd::Profile::Unknown && has(r.why, "not readable JSON"), "bad JSON: unknown");
        r = cd::parse_record("{\"profile\": \"netherlands\"}", true);
        CHECK(r.profile == cd::Profile::Unknown && has(r.why, "neither stock nor eerste"), "unknown value: unknown");
        r = cd::parse_record("{\"set_at\": \"x\"}", true);
        CHECK(r.profile == cd::Profile::Unknown && has(r.why, "no \"profile\""), "no profile key: unknown");
        r = cd::parse_record("{\"profile\": 1}", true);
        CHECK(r.profile == cd::Profile::Unknown, "profile not text: unknown");
        CHECK(cd::strip_bom("\xEF\xBB\xBFx") == "x" && cd::strip_bom("x") == "x" && cd::strip_bom("") == "", "strip_bom");
    });

    run_case("compdata guard: the 11 files on disk (all, none, partial; stat only)", [] {
        CHECK(cd::eerste_files().size() == 11, "the install record lists 11 files");
        const fs::path le = g_out / "compdata_guard_world";
        fs::remove_all(le);
        cd::DiskState d = cd::read_disk_state(le);
        CHECK(d.profile == cd::Profile::Stock && d.live == 0 && d.total == 11, "no folder: none live = stock: " + d.why);
        const fs::path dir = cd::compdata_dir(le);
        fs::create_directories(dir / "schedules");
        for (const auto& rel : cd::eerste_files()) std::ofstream(dir / fs::path(rel)) << "x";
        d = cd::read_disk_state(le);
        CHECK(d.profile == cd::Profile::Eerste && d.live == 11, "all 11 live = eerste: " + d.why);
        fs::remove(dir / "LIVE_EDITOR_LIMITS.json");
        d = cd::read_disk_state(le);
        CHECK(d.profile == cd::Profile::Unknown && d.live == 10 && has(d.why, "partial"), "10 of 11 = partial, unknown: " + d.why);
        CHECK(has(cd::compdata_dir(le).generic_string(), "mods/legacy/dlc/dlc_FootballCompEng/dlc/FootballCompEng/data/compdata"), "the LE compdata folder");
        CHECK(cd::record_path(le).filename() == "compdata_profile.json" && cd::record_path(le).parent_path().filename() == "turbo_output", "record path");
        fs::remove_all(le);
    });

    run_case("compdata guard: record and disk must agree", [] {
        const cd::Record stock = cd::parse_record("{\"profile\": \"stock\"}", true);
        const cd::Record eerste = cd::parse_record("{\"profile\": \"eerste\"}", true);
        const cd::Record none = cd::parse_record("", false);
        const cd::DiskState d0 = cd::disk_state_from_count(0, 11), d11 = cd::disk_state_from_count(11, 11), d5 = cd::disk_state_from_count(5, 11);
        CHECK(cd::installed_profile(stock, d0).profile == cd::Profile::Stock, "stock + none live = stock");
        CHECK(cd::installed_profile(eerste, d11).profile == cd::Profile::Eerste, "eerste + all live = eerste");
        cd::Installed i = cd::installed_profile(stock, d11);
        CHECK(i.profile == cd::Profile::Unknown && has(i.why, "disagree"), "stock record, files live (moved without the launcher): unknown: " + i.why);
        i = cd::installed_profile(eerste, d0);
        CHECK(i.profile == cd::Profile::Unknown && has(i.why, "disagree"), "eerste record, files aside: unknown");
        CHECK(cd::installed_profile(eerste, d5).profile == cd::Profile::Unknown, "partial set: unknown");
        i = cd::installed_profile(none, d11);
        CHECK(i.profile == cd::Profile::Unknown && has(i.why, "has not run"), "no record: unknown even with a clear disk");
    });

    run_case("compdata guard: seeded careers (shipped block, built-in fallback, bad entries)", [] {
        cd::Careers c = cd::parse_careers("");
        CHECK(cd::career_need(c, "d91d1b4e90a27eda") == cd::Profile::Eerste && has(c.source, "built-in"), "no file: built-in SPARTA = eerste");
        c = cd::parse_careers("{\"gui\": {\"autoload\": true}}");
        CHECK(cd::career_need(c, "D91D1B4E90A27EDA") == cd::Profile::Eerste && has(c.source, "built-in"),
              "an older turbo_config.json without the block: the built-in list (no false banner on SPARTA)");
        CHECK(cd::career_need(c, "0123456789abcdef") == cd::Profile::Stock, "any other identity: stock");
        CHECK(cd::career_need(c, "") == cd::Profile::Unknown, "not identified: unknown");
        c = cd::parse_careers("{\"compdata_profile\": {\"careers\": {\"_help\": \"x\", \"0123456789abcdef\": \"eerste\", \"zz\": \"eerste\", "
                              "\"fedcba9876543210\": \"other\"}}}");
        CHECK(cd::career_need(c, "0123456789abcdef") == cd::Profile::Eerste && cd::career_need(c, "d91d1b4e90a27eda") == cd::Profile::Stock &&
                  c.map.size() == 1 && has(c.note, "zz") && has(c.note, "fedcba9876543210") && !has(c.note, "_help"),
              "the block replaces the built-in list; bad entries named, _help skipped: " + c.note);
        c = cd::parse_careers("{\"compdata_profile\": {\"careers\": 5}}");
        CHECK(has(c.source, "built-in") && has(c.note, "not an object"), "bad block: built-in + note");
        c = cd::parse_careers("\xEF\xBB\xBF{bad");
        CHECK(has(c.source, "built-in") && has(c.note, "not readable"), "unreadable config: built-in + note");
        // the shipped turbo_config.json carries the seeded list
        std::ifstream f("../turbo/package/turbo_config.json", std::ios::binary);
        const std::string shipped((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        c = cd::parse_careers(shipped);
        CHECK(!shipped.empty() && has(c.source, "turbo_config.json compdata_profile.careers") && c.note.empty() &&
                  cd::career_need(c, cd::kSpartaIdentity) == cd::Profile::Eerste,
              "shipped turbo_config.json: SPARTA = eerste from the block: " + c.source + " " + c.note);
    });

    run_case("compdata guard: decision table and the fixed log heads (OK, MISMATCH, UNKNOWN; banner only on MISMATCH)", [] {
        cd::Installed stock{cd::Profile::Stock, "s"}, eerste{cd::Profile::Eerste, "e"}, unknown{cd::Profile::Unknown, "u"};
        const std::string id = "0123456789abcdef";
        cd::Decision d = cd::decide(stock, cd::Profile::Stock, id, "A B, Leicester City", "src");
        CHECK(d.verdict == cd::Verdict::Ok && starts(d.line, "compdata profile: stock installed, career needs stock, OK") && d.banner.empty(), d.line);
        d = cd::decide(eerste, cd::Profile::Eerste, cd::kSpartaIdentity, "", "src");
        CHECK(d.verdict == cd::Verdict::Ok && starts(d.line, "compdata profile: eerste installed, career needs eerste, OK") && d.banner.empty(), d.line);
        d = cd::decide(eerste, cd::Profile::Stock, id, "", "src");
        CHECK(d.verdict == cd::Verdict::Mismatch && starts(d.line, "compdata profile: eerste installed, career needs stock, MISMATCH") &&
                  has(d.banner, "DO NOT SAVE") && has(d.banner, "other than Netherlands"),
              d.line + " | " + d.banner);
        d = cd::decide(stock, cd::Profile::Eerste, cd::kSpartaIdentity, "", "src");
        CHECK(d.verdict == cd::Verdict::Mismatch && starts(d.line, "compdata profile: stock installed, career needs eerste, MISMATCH") &&
                  has(d.banner, "DO NOT SAVE") && has(d.banner, "pick Netherlands"),
              d.line + " | " + d.banner);
        d = cd::decide(unknown, cd::Profile::Stock, id, "", "src");
        CHECK(d.verdict == cd::Verdict::Unknown && starts(d.line, "compdata profile: unknown installed, career needs stock, UNKNOWN") && d.banner.empty(),
              "unknown installed: UNKNOWN, never MISMATCH: " + d.line);
        d = cd::decide(eerste, cd::Profile::Unknown, "", "", "src");
        CHECK(d.verdict == cd::Verdict::Unknown && starts(d.line, "compdata profile: eerste installed, career not identified, UNKNOWN") && d.banner.empty(),
              "not identified: UNKNOWN, no banner: " + d.line);
        // the missing record end to end: UNKNOWN, never MISMATCH, whatever the disk and the career
        const cd::Installed miss = cd::installed_profile(cd::parse_record("", false), cd::disk_state_from_count(11, 11));
        d = cd::decide(miss, cd::Profile::Stock, id, "", "src");
        CHECK(d.verdict == cd::Verdict::Unknown && d.banner.empty(), "missing record -> UNKNOWN: " + d.line);
    });

    // ---- the launcher hint (core/callnames.h)
    using turbo::LauncherHint;
    using turbo::RegistryLocale;
    using turbo::detect_commentary_language;
    const RegistryLocale packs;  // no registry Locale: the hint cases below fall back to eng_us, never to a pack guess

    run_case("launcher hint: rank (hint + live pack, hint + absent pack, manual override wins, runtime wins, never proven)", [&] {
        std::string why;
        bool proven = true;
        LauncherHint h;
        h.lang = "eng_us";
        h.stamp = "2026-10-19 16:20:00";
        h.pack_live = true;
        h.pack_why = "toc + cas";
        // no runtime answer, no saved answer: the hint decides, NOT proven
        std::string l = detect_commentary_language("", "", "", "", "", packs, &why, &proven, &h);
        CHECK(l == "eng_us" && has(why, "launcher hint eng_us") && has(why, "NOT PROVEN") && !proven, "hint + live pack: " + why);
        // without the hint and without a registry Locale: eng_us (CHANGE 8: no alphabetical pack guess)
        l = detect_commentary_language("", "", "", "", "", packs, &why, &proven);
        CHECK(l == "eng_us" && has(why, "registry Locale not read") && !proven, "no hint, no Locale: eng_us: " + why);
        // the pack is not live in Data\Win32: ignored, said so
        h.pack_live = false;
        h.pack_why = "no Data\\Win32\\commentaryfull_eng_us.toc";
        l = detect_commentary_language("", "", "", "", "", packs, &why, &proven, &h);
        CHECK(l == "eng_us" && has(why, "launcher hint eng_us ignored: its pack is not live") && !proven, "hint + absent pack: " + why);
        h.pack_live = true;
        // the manual override wins (CHANGE 7)
        l = detect_commentary_language("ita_it", "", "", "", "", packs, &why, &proven, &h);
        CHECK(l == "ita_it" && has(why, "MANUAL OVERRIDE") && has(why, "launcher hint eng_us not used") && proven, "override wins: " + why);
        // this session's runtime answer wins; a different hint is named
        l = detect_commentary_language("", "ita_it", "match 0.98", "", "", packs, &why, &proven, &h);
        CHECK(l == "ita_it" && has(why, "running game") && has(why, "launcher hint eng_us differs") && proven, "runtime wins: " + why);
        // a malformed code is ignored
        LauncherHint bad = h;
        bad.lang = "english";
        l = detect_commentary_language("", "", "", "", "", packs, &why, &proven, &bad);
        CHECK(l == "eng_us" && has(why, "not a language code"), "malformed hint: " + why);
    });

    run_case("launcher hint: against an earlier session's saved answer (newer hint wins, older hint loses, undated loses, agreeing keeps proof)", [&] {
        std::string why;
        bool proven = false;
        LauncherHint h;
        h.lang = "eng_us";
        h.pack_live = true;
        // saved ita_it heard 09:48, hint set 16:20: the hint (newer pick) wins, not proven
        h.stamp = "2026-10-19T16:20:00";
        h.saved_when = "2026-10-19 09:48:35";
        std::string l = detect_commentary_language("", "", "", "ita_it", "heard", packs, &why, &proven, &h);
        CHECK(l == "eng_us" && !proven && has(why, "launcher hint eng_us") && has(why, "earlier answer ita_it"), "hint newer than the saved answer: " + why);
        // the saved answer was heard after the hint: it wins, proven
        h.stamp = "2026-10-19 08:00";
        l = detect_commentary_language("", "", "", "ita_it", "heard", packs, &why, &proven, &h);
        CHECK(l == "ita_it" && proven && has(why, "launcher hint eng_us ignored") && has(why, "before the answer heard"), "hint older than the saved answer: " + why);
        // an undated hint ranks below the saved answer
        h.stamp = "yesterday";
        l = detect_commentary_language("", "", "", "ita_it", "heard", packs, &why, &proven, &h);
        CHECK(l == "ita_it" && proven && has(why, "is not a date"), "undated hint: " + why);
        // a saved answer that agrees with the hint keeps its proof
        h.stamp = "2026-10-19 16:20:00";
        l = detect_commentary_language("", "", "", "eng_us", "heard", packs, &why, &proven, &h);
        CHECK(l == "eng_us" && proven && has(why, "agrees"), "agreeing saved answer stays proven: " + why);
        CHECK(turbo::normalize_stamp("2026-10-19T16:20") == "2026-10-19 16:20:00" && turbo::normalize_stamp("2026-10-19 16:20:05.123+02:00") == "2026-10-19 16:20:05" &&
                  turbo::normalize_stamp("19/10/2026") == "" && turbo::normalize_stamp("") == "",
              "stamp shapes");
        CHECK(turbo::language_code_ok("dut_nl") && !turbo::language_code_ok("DUT_NL") && !turbo::language_code_ok("dut-nl") && !turbo::language_code_ok("dutch"),
              "language codes");
    });

    run_case("launcher hint: the pack test reads Data\\Win32 only (toc above stub size + a non-empty full cas; never <game>\\commentary, never launch)", [] {
        const fs::path game = g_out / "hint_game_world";
        fs::remove_all(game);
        const fs::path w32 = game / "Data" / "Win32";
        fs::create_directories(w32 / "superbundlelayout" / "commentaryfull_dut_nl");
        fs::create_directories(w32 / "superbundlelayout" / "commentarylaunch_spa_es");
        fs::create_directories(game / "commentary" / "commentaryfull_spa_es");
        std::string why;
        CHECK(!turbo::launcher_pack_live(game, "dut_nl", &why) && has(why, "no Data\\Win32"), "no toc: " + why);
        { std::ofstream(w32 / "commentaryfull_dut_nl.toc") << std::string(923, 'x'); }
        CHECK(!turbo::launcher_pack_live(game, "dut_nl", &why) && has(why, "stub"), "923 B stub toc: not live: " + why);
        { std::ofstream(w32 / "commentaryfull_dut_nl.toc") << std::string(20000, 'x'); }
        CHECK(!turbo::launcher_pack_live(game, "dut_nl", &why) && has(why, "no non-empty cas"), "toc without cas: " + why);
        { std::ofstream(w32 / "superbundlelayout" / "commentaryfull_dut_nl" / "cas_01.cas"); }
        CHECK(!turbo::launcher_pack_live(game, "dut_nl", &why), "a 0 B cas is not a pack: " + why);
        { std::ofstream(w32 / "superbundlelayout" / "commentaryfull_dut_nl" / "cas_02.cas") << "data"; }
        CHECK(turbo::launcher_pack_live(game, "dut_nl", &why) && has(why, "1 cas"), "toc + non-empty cas: live: " + why);
        // spa_es: a full pack only in <game>\commentary (the backup store) and a launch subset in Data\Win32: not live
        { std::ofstream(game / "commentary" / "commentaryfull_spa_es.toc") << std::string(20000, 'x'); }
        { std::ofstream(game / "commentary" / "commentaryfull_spa_es" / "cas_01.cas") << "data"; }
        { std::ofstream(w32 / "commentarylaunch_spa_es.toc") << std::string(20000, 'x'); }
        { std::ofstream(w32 / "superbundlelayout" / "commentarylaunch_spa_es" / "cas_01.cas") << "data"; }
        CHECK(!turbo::launcher_pack_live(game, "spa_es", &why), "backup store + launch subset do not count: " + why);
        CHECK(!turbo::launcher_pack_live(fs::path(), "dut_nl", &why) && has(why, "unknown"), "no game folder");
        CHECK(!turbo::launcher_pack_live(game, "../x", &why), "a bad code never builds a path");
        // Callnames::refresh with the hint end to end: hint live -> used, not proven
        turbo::Callnames cn;
        cn.launcher_hint.lang = "dut_nl";
        cn.launcher_hint.stamp = "2026-10-19 16:20:00";
        cn.refresh(g_out / "hint_le_world", game, "");
        CHECK(cn.lang == "dut_nl" && !cn.lang_proven && cn.launcher_hint.pack_live && has(cn.log_line(), "launcher hint dut_nl"),
              "refresh: " + cn.log_line());
        // with the kill switch the hint answer still is not proven (intent, not evidence)
        fs::create_directories(g_out / "hint_le_world" / "turbo_output");
        { std::ofstream(turbo::lang_detect_off_path(g_out / "hint_le_world")) << "off"; }
        cn.refresh(g_out / "hint_le_world", game, "");
        CHECK(cn.lang == "dut_nl" && !cn.lang_proven, "kill switch does not prove a hint answer: " + cn.log_line());
        // the manual override still wins inside refresh
        cn.refresh(g_out / "hint_le_world", game, "ita_it");
        CHECK(cn.lang == "ita_it" && cn.lang_proven, "override in refresh: " + cn.log_line());
        fs::remove_all(g_out / "hint_le_world");
        fs::remove_all(game);
    });
    // ---- 2.21 CHANGE 8: the registry Locale step (replaces <game>\commentary + the alphabetical pick)
    run_case("registry Locale: the exe's locale table (nl_NL dut, it_IT ita, fr_FR fre, de_DE ger, pt_BR por, es_ES spa, en_* eng_us)", [] {
        using turbo::commentary_lang_for_locale;
        CHECK(commentary_lang_for_locale("nl_NL") == "dut_nl" && commentary_lang_for_locale("it_IT") == "ita_it" &&
                  commentary_lang_for_locale("fr_FR") == "fre_fr" && commentary_lang_for_locale("de_DE") == "ger_de" &&
                  commentary_lang_for_locale("pt_BR") == "por_br" && commentary_lang_for_locale("es_ES") == "spa_es",
              "the six languages");
        CHECK(commentary_lang_for_locale("en_US") == "eng_us" && commentary_lang_for_locale("en_GB") == "eng_us", "English: the base");
        CHECK(commentary_lang_for_locale("FR-fr") == "fre_fr" && commentary_lang_for_locale("pt-BR") == "por_br", "any case, '-' or '_'");
        CHECK(commentary_lang_for_locale("ja_JP").empty() && commentary_lang_for_locale("es_MX").empty() && commentary_lang_for_locale("").empty() &&
                  commentary_lang_for_locale("fr_FR ").empty() && commentary_lang_for_locale("french").empty(),
              "not in the table: no language");
        // the real registry, read-only (this PC: whatever the EA app set; the value is printed, not judged)
        std::string rw;
        const std::string real = turbo::read_registry_locale(&rw);
        std::printf("    real registry Locale (read-only): '%s' -> '%s' (%s)\n", real.c_str(), commentary_lang_for_locale(real).c_str(), rw.c_str());
        CHECK(!rw.empty(), "the read always says what it found");
    });

    run_case("registry Locale: rank (pack present -> its language, NOT proven; missing pack / unknown Locale -> eng_us; override, runtime and hint win; a2 outside English + Locale dropped)", [] {
        const fs::path game = g_out / "locale_game_world";
        fs::remove_all(game);
        make_live_pack(game, "fre_fr");
        make_live_pack(game, "dut_nl");
        std::string why;
        bool proven = true;
        const RegistryLocale fr = turbo::registry_locale_from("fr_FR", "test", game);
        CHECK(fr.usable() && fr.lang == "fre_fr", "fr_FR -> fre_fr, pack live: " + fr.pack_why);
        std::string l = detect_commentary_language("", "", "", "", "", fr, &why, &proven);
        CHECK(l == "fre_fr" && !proven && why.rfind("registry Locale fr_FR, pack present", 0) == 0 && has(why, "offers English + fre_fr"),
              "pack present: " + why);
        CHECK(turbo::commentary_language_log_line(l, why).rfind("callnames: language fre_fr (registry Locale fr_FR, pack present", 0) == 0, "the log line names the source");
        // missing pack: de_DE -> ger_de, nothing in Data\Win32 -> eng_us, said so
        const RegistryLocale de = turbo::registry_locale_from("de_DE", "test", game);
        l = detect_commentary_language("", "", "", "", "", de, &why, &proven);
        CHECK(l == "eng_us" && !proven && has(why, "registry Locale de_DE -> ger_de, its pack is not live in Data\\Win32"), "missing pack: " + why);
        // the pack only in the backup store <game>\commentary does not count
        fs::create_directories(game / "commentary" / "commentaryfull_ita_it");
        { std::ofstream(game / "commentary" / "commentaryfull_ita_it.toc") << std::string(20000, 'x'); }
        { std::ofstream(game / "commentary" / "commentaryfull_ita_it" / "cas_01.cas") << "data"; }
        const RegistryLocale it = turbo::registry_locale_from("it_IT", "test", game);
        CHECK(!it.usable() && detect_commentary_language("", "", "", "", "", it, &why) == "eng_us", "backup store only: not live: " + why);
        // a Locale outside the exe's table
        l = detect_commentary_language("", "", "", "", "", turbo::registry_locale_from("ja_JP", "test", game), &why, &proven);
        CHECK(l == "eng_us" && has(why, "not in the game's locale table"), "unknown Locale: " + why);
        // the manual override wins over the registry and the hint
        LauncherHint h;
        h.lang = "dut_nl";
        h.stamp = "2026-10-19 16:20:00";
        h.pack_live = true;
        l = detect_commentary_language("ita_it", "", "", "", "", fr, &why, &proven, &h);
        CHECK(l == "ita_it" && proven && has(why, "MANUAL OVERRIDE"), "override wins: " + why);
        // this session's runtime answer wins over the registry
        l = detect_commentary_language("", "eng_us", "match 0.99", "", "", fr, &why, &proven);
        CHECK(l == "eng_us" && proven && has(why, "running game"), "runtime wins: " + why);
        // the launcher hint ranks above the registry; a hint the Locale cannot offer is named
        l = detect_commentary_language("", "", "", "", "", fr, &why, &proven, &h);
        CHECK(l == "dut_nl" && !proven && has(why, "launcher hint dut_nl") && has(why, "offers English + fre_fr only"), "hint above the registry: " + why);
        // an earlier answer outside English + the Locale's language is dropped; English stays (the game offers it)
        l = detect_commentary_language("", "", "", "dut_nl", "heard", fr, &why, &proven);
        CHECK(l == "fre_fr" && !proven && has(why, "earlier answer dut_nl not used"), "stale a2 dropped: " + why);
        l = detect_commentary_language("", "", "", "eng_us", "heard", fr, &why, &proven);
        CHECK(l == "eng_us" && proven && has(why, "earlier session"), "a2 English kept: " + why);
        l = detect_commentary_language("", "", "", "fre_fr", "heard", fr, &why, &proven);
        CHECK(l == "fre_fr" && proven, "a2 = the Locale's language kept: " + why);
        // no usable Locale: a2 is used as before
        l = detect_commentary_language("", "", "", "dut_nl", "heard", de, &why, &proven);
        CHECK(l == "dut_nl" && proven, "no usable Locale: a2 as before: " + why);
        fs::remove_all(game);
    });

    run_case("registry Locale: Callnames::refresh end to end (packs from Data\\Win32 only, Locale with and without its pack, override)", [] {
        const fs::path game = g_out / "locale_refresh_game";
        const fs::path le = g_out / "locale_refresh_le";
        fs::remove_all(game);
        fs::remove_all(le);
        fs::create_directories(le / "turbo_output");
        make_live_pack(game, "fre_fr");
        // the backup store holds dut_nl: never read, never listed
        fs::create_directories(game / "commentary" / "commentaryfull_dut_nl");
        { std::ofstream(game / "commentary" / "commentaryfull_dut_nl.toc") << std::string(20000, 'x'); }
        { std::ofstream(game / "commentary" / "commentaryfull_dut_nl" / "cas_01.cas") << "data"; }
        std::string loc = "fr_FR";
        turbo::locale_reader_for_tests() = [&loc](std::string* w) {
            if (w) *w = "test registry " + loc;
            return loc;
        };
        turbo::Callnames cn;
        cn.refresh(le, game, "");
        CHECK(cn.packs.size() == 1 && cn.packs[0].code == "fre_fr", "packs: Data\\Win32 only");
        CHECK(cn.pack_data == "Data\\Win32 commentaryfull_fre_fr" && has(cn.log_line(), "[pack data: Data\\Win32 commentaryfull_fre_fr]"),
              "data source: no backup copy -> Data\\Win32: " + cn.log_line());
        CHECK(cn.lang == "fre_fr" && !cn.lang_proven && cn.log_line().rfind("callnames: language fre_fr (registry Locale fr_FR, pack present", 0) == 0,
              "Locale fr_FR with its pack: " + cn.log_line());
        loc = "nl_NL";  // dut_nl only in the backup store
        cn.refresh(le, game, "");
        CHECK(cn.lang == "eng_us" && !cn.lang_proven && has(cn.lang_why, "nl_NL -> dut_nl, its pack is not live"), "Locale without its pack: " + cn.log_line());
        cn.refresh(le, game, "dut_nl");
        CHECK(cn.lang == "dut_nl" && cn.lang_proven && has(cn.lang_why, "MANUAL OVERRIDE"), "override wins: " + cn.log_line());
        CHECK(cn.pack_data == "backup store <game>\\commentary\\commentaryfull_dut_nl", "data source: the backup store: " + cn.pack_data);
        // fre_fr in both places: the decision is the Locale's, the data comes from the backup store first
        loc = "fr_FR";
        fs::create_directories(game / "commentary" / "commentaryfull_fre_fr");
        { std::ofstream(game / "commentary" / "commentaryfull_fre_fr.toc") << std::string(20000, 'x'); }
        { std::ofstream(game / "commentary" / "commentaryfull_fre_fr" / "cas_01.cas") << "data"; }
        cn.refresh(le, game, "");
        CHECK(cn.lang == "fre_fr" && cn.log_line().rfind("callnames: language fre_fr (registry Locale fr_FR, pack present", 0) == 0 &&
                  has(cn.log_line(), "[pack data: backup store <game>\\commentary\\commentaryfull_fre_fr]"),
              "decision source + data source both named: " + cn.log_line());
        // a stub toc or an empty cas in the backup store is not a data source
        { std::ofstream(game / "commentary" / "commentaryfull_fre_fr" / "cas_01.cas", std::ios::trunc); }
        CHECK(turbo::pack_data_source(game, "fre_fr") == "Data\\Win32 commentaryfull_fre_fr", "empty backup cas: Data\\Win32");
        CHECK(turbo::pack_data_source(game, "ger_de").rfind("none", 0) == 0 && turbo::pack_data_source(fs::path(), "fre_fr").rfind("none", 0) == 0 &&
                  turbo::pack_data_source(game, "../x").rfind("none", 0) == 0,
              "no pack / no game / bad code: none");
        turbo::locale_reader_for_tests() = test_locale_it;
        fs::remove_all(game);
        fs::remove_all(le);
    });
}

}  // namespace compdata_profile_test
