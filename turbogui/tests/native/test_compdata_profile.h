// Native tests (2.21 compdata guard, core/compdata_profile.h, and the launcher-hint rank, core/callnames.h): the launcher's
// record (missing -> UNKNOWN, never MISMATCH; BOM; bad values), the disk state of the 11 files, record vs disk, the seeded career
// list (shipped block, built-in fallback, bad entries), the decision table and its fixed log heads; the hint rank (hint + live pack,
// hint + absent pack, hint older / newer than a saved answer, manual override wins, a hint answer is never proven) and the
// Data\Win32-only pack test. Synthetic folders only. Included by test_main.cpp; compdata_profile_test::run() is called from main
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
    using turbo::CommentaryPack;
    using turbo::LauncherHint;
    using turbo::detect_commentary_language;
    const std::vector<CommentaryPack> packs = {{"dut_nl", true}, {"eng_us", false}, {"ita_it", true}, {"spa_es", true}};

    run_case("launcher hint: rank (hint + live pack, hint + absent pack, manual override wins, runtime wins, never proven)", [&] {
        std::string why;
        bool proven = true;
        LauncherHint h;
        h.lang = "eng_us";
        h.stamp = "2026-10-19 16:20:00";
        h.pack_live = true;
        h.pack_why = "toc + cas";
        // no runtime answer, no saved answer: the hint decides over the several downloaded packs, NOT proven
        std::string l = detect_commentary_language("", "", "", "", "", packs, &why, &proven, &h);
        CHECK(l == "eng_us" && has(why, "launcher hint eng_us") && has(why, "NOT PROVEN") && !proven, "hint + live pack: " + why);
        // without the hint the packs' alphabetical guess (unchanged step c)
        l = detect_commentary_language("", "", "", "", "", packs, &why, &proven);
        CHECK(l == "dut_nl" && has(why, "several downloaded"), "no hint: step c as before: " + why);
        // the pack is not live in Data\Win32: ignored, said so
        h.pack_live = false;
        h.pack_why = "no Data\\Win32\\commentaryfull_eng_us.toc";
        l = detect_commentary_language("", "", "", "", "", packs, &why, &proven, &h);
        CHECK(l == "dut_nl" && has(why, "launcher hint eng_us ignored: its pack is not live") && !proven, "hint + absent pack: " + why);
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
        CHECK(l == "dut_nl" && has(why, "not a language code"), "malformed hint: " + why);
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
}

}  // namespace compdata_profile_test
