// Native tests: the 2.20 Callname page (brief 2.20-S1-callname, ui_callname_pick.h + ui_callnames.cpp callname_editor):
//  * the pure part: the ONE list (generic + real, the loaded language only, each row once), the search (text, never an id),
//    the Apply check (service, own recording, the catalog gate) and the Entry an Apply writes;
//  * the page through the UI: one click picks a row, Preview is always drawn, Apply writes exactly one swap row for the open
//    player and nothing else (his players / editedplayernames / playernamemap rows stay byte-equal), Undo gives back what
//    he had, a second language's callnames never show, no id and no tab of the older editor is drawn.
// Included by test_main.cpp; callname_pick_test::run() is the pure cases, ::ui_cases() the UI ones (called where the older
// Callname cases ended, with the older editor switched off again).
#pragma once
#include "ui/ui_callname_pick.h"

namespace callname_pick_test {

using namespace turbo;

static CatalogBank make_bank(const std::string& lang, const std::string& team, const std::vector<int64_t>& generic,
                             const std::vector<std::pair<int64_t, int>>& own) {
    CatalogBank b;
    b.lang = lang;
    b.team = team;
    b.teams = {team};
    b.build_key = "TESTKEY";
    b.current = true;
    for (int64_t g : generic) b.generic.insert(g);
    for (const auto& o : own) b.own[o.first] = o.second;
    return b;
}

static bool has_row(const std::vector<AllCallnameRow>& v, bool own, int64_t id) {
    for (const auto& r : v)
        if (r.own() == own && (own ? r.playerid : r.commentaryid) == id) return true;
    return false;
}

static void run() {
    run_case("callname page: the one list is generic + real of the loaded language, each row once, never another language", [&] {
        // ita_it: the commentary files record generic 900002 and 900004, real players 1001 and 1004 (simple / link) and 1010 (high only);
        // the master list adds 900002 again (a duplicate), 900017 (not recorded) and 1004 again
        MasterList m;
        m.lang = "ita_it";
        m.game = "fc27";
        m.generic_ids = {900002, 900017};
        m.real_players = {1001, 1004};
        m.generic_names = {{900002, "Saka"}, {900017, "Kane"}};
        m.names = {{1001, "Bukayo Saka"}, {1004, "Cristiano"}};
        CatalogBank ita = make_bank("ita_it", "", {900002, 900004}, {{1001, kBankSimple}, {1004, kBankLink}, {1010, kBankHigh}});
        ita.commentary_names[900004] = {"Havertz", "slug"};
        ita.player_names[1010] = {"Dennis", "slug"};
        CallnameIndex ix;
        CallnameDbUse use;
        use.player_in_db = [](int64_t pid) { return pid == 1001 || pid == 1004; };
        use.player_name = [](int64_t pid) { return pid == 1001 ? std::string("Bukayo Saka") : pid == 1004 ? std::string("Cristiano") : std::string(); };
        std::vector<AllCallnameRow> unsay;
        const auto rows = callpick::list_rows(ita, m, ix, use, &unsay);
        // union of the files and the master list: generic {900002, 900004, 900017} + real {1001, 1004, 1010}; the game can SAY
        // only what its files record as a simple / link recording: 900017 (master only) and 1010 (high only) are left out
        CHECK(rows.size() == 4, fmt("four rows the game can say (%zu)", rows.size()));
        CHECK(has_row(rows, false, 900002) && has_row(rows, false, 900004), "the generic callnames the files record");
        CHECK(has_row(rows, true, 1001) && has_row(rows, true, 1004), "the real players the files record");
        CHECK(!has_row(rows, false, 900017) && !has_row(rows, true, 1010), "the master-only name and the high-only player are not listed");
        CHECK(unsay.size() == 2 && has_row(unsay, false, 900017) && has_row(unsay, true, 1010), fmt("they are kept apart to be explained (%zu)", unsay.size()));
        CHECK(callpick::missing_line("kane", unsay, "ita_it") == "Kane has no recording in the game's ita_it commentary, so the game cannot say it." &&
                  callpick::missing_line("", unsay, "ita_it").empty() && callpick::missing_line("saka", unsay, "ita_it").empty(),
              "the line under a search: only for a typed name the files lack");
        std::set<std::string> keys;
        for (const auto& r : rows) keys.insert(callpick::row_key(r));
        CHECK(keys.size() == rows.size(), "no row twice (kind + id)");
        // the same name as a generic callname and as a real player stay two rows (two different recordings)
        int saka = 0;
        for (const auto& r : rows)
            if (r.text.find("Saka") != std::string::npos) ++saka;
        CHECK(saka == 2, fmt("Saka: his surname callname and his own recording, two rows (%d)", saka));
        // another language: its own bank and master; nothing of ita_it comes into its list, nothing of its own into ita_it's
        MasterList m2;
        m2.lang = "deu_de";
        m2.game = "fc27";
        m2.generic_ids = {900099};
        m2.real_players = {2001};
        CatalogBank deu = make_bank("deu_de", "", {900099}, {{2001, kBankSimple}});
        const auto rows2 = callpick::list_rows(deu, m2, ix, use);
        CHECK(rows2.size() == 2 && has_row(rows2, false, 900099) && has_row(rows2, true, 2001), "deu_de: its two rows");
        CHECK(!has_row(rows2, false, 900002) && !has_row(rows2, true, 1001) && !has_row(rows, false, 900099) && !has_row(rows, true, 2001),
              "no row of the other language in either list");
        // without the catalog's bank: the master list's rows
        const auto plain = callpick::list_rows(CatalogBank{}, m, ix, use);
        CHECK(plain.size() == 4 && has_row(plain, false, 900017) && has_row(plain, true, 1004), fmt("no bank: the master's four rows (%zu)", plain.size()));
        // the dut_nl case of 2026-10-19: the user's FC 26 Dutch list has El Ghazi (216433) and Cunningham (198190) with an own
        // recording, the FC 27 dut_nl files do not: neither is listed, a search for him says why
        MasterList nl;
        nl.lang = "dut_nl";
        nl.game = "fc26";
        nl.real_players = {216433, 198190, 299001};
        nl.names = {{216433, "Anwar El Ghazi"}, {198190, "Greg Cunningham"}, {299001, "Davy Klaassen"}};
        CatalogBank dut = make_bank("dut_nl", "", {900010}, {{299001, kBankSimple}});
        std::vector<AllCallnameRow> nl_unsay;
        const auto nl_rows = callpick::list_rows(dut, nl, ix, use, &nl_unsay);
        CHECK(!has_row(nl_rows, true, 216433) && !has_row(nl_rows, true, 198190) && has_row(nl_rows, true, 299001) && has_row(nl_rows, false, 900010),
              "dut_nl: El Ghazi and Cunningham are not offered, Klaassen (recorded) is");
        CHECK(has_row(nl_unsay, true, 216433) && has_row(nl_unsay, true, 198190), "dut_nl: both kept apart");
        CHECK(callpick::missing_line("ghazi", nl_unsay, "dut_nl") == "Anwar El Ghazi has no recording in the game's dut_nl commentary, so the game cannot say it." &&
                  !callpick::can_apply(true, "", dut, nl_unsay[0], 1002).ok,
              "dut_nl: the line names him; Apply would be refused anyway");
        // hidden_off drops a recorded row no save uses (a player who left)
        CatalogBank hoff = ita;
        hoff.hidden_off = true;
        const auto rows3 = callpick::list_rows(hoff, m, ix, use);
        CHECK(!has_row(rows3, true, 1010) && has_row(rows3, true, 1001), "asset_hidden_off: the player not in the save is not listed");
    });

    run_case("callname page: search (text only), the Apply check, the entry an Apply writes, the words of the page", [&] {
        AllCallnameRow kane;
        kane.commentaryid = 900017;
        kane.text = "Kane";
        AllCallnameRow lob;
        lob.playerid = 244263;
        lob.text = "Stanislav Lobotka";
        CHECK(callpick::matches("", kane) && callpick::matches("  ", kane) && callpick::matches("kAN", kane) && callpick::matches(" ane ", kane), "search: any case, trimmed");
        CHECK(!callpick::matches("900017", kane) && !callpick::matches("244263", lob) && callpick::matches("lobo", lob), "search: a number finds no callname (no ids)");
        CHECK(std::string(callpick::kind_label(kane)) == "Generic" && std::string(callpick::kind_label(lob)) == "Real player", "kind words");
        // the Apply check
        CatalogBank none;
        CatalogBank bank = make_bank("ita_it", "", {900017}, {{244263, kBankSimple}, {5, kBankHigh}});
        CHECK(callpick::can_apply(true, "", bank, kane, 1002).ok && callpick::can_apply(true, "", bank, lob, 1002).ok, "recorded rows: ok");
        callpick::Verdict self = callpick::can_apply(true, "", bank, lob, 244263);
        CHECK(!self.ok && self.why == "That is his own recording already.", "his own recording on himself: refused: " + self.why);
        callpick::Verdict off = callpick::can_apply(false, "game build", bank, kane, 1002);
        CHECK(!off.ok && off.why == "Callnames cannot be changed right now: this version of the game is not supported.", "service off: " + off.why);
        CHECK(callpick::can_apply(false, "kill switch", bank, kane, 1002).why.find("turned off") != std::string::npos &&
                  callpick::can_apply(false, "", bank, kane, 1002).why.find("not available") != std::string::npos,
              "service off: the other reasons in plain words");
        AllCallnameRow unrecorded;
        unrecorded.commentaryid = 900555;
        unrecorded.text = "Nobody";
        unrecorded.recorded = false;
        callpick::Verdict ur = callpick::can_apply(true, "", bank, unrecorded, 1002);
        CHECK(!ur.ok && ur.why.find("cannot be used in matches") != std::string::npos, "no recording in the files: refused: " + ur.why);
        AllCallnameRow high;
        high.playerid = 5;
        high.text = "High only";
        CHECK(!callpick::can_apply(true, "", bank, high, 1002).ok, "a high-intensity-only recording: refused");
        CHECK(callpick::can_apply(true, "", none, unrecorded, 1002).ok, "no catalog: the 2.0 rules, ok");
        // the entry
        voice::Entry g = callpick::make_entry(1002, "Martin Odegaard", kane, false);
        CHECK(g.playerid == 1002 && !g.voice_of && g.kickoff && *g.kickoff == 900017 && !g.names_only && g.from == "Kane (generic)" &&
                  callpick::entry_name(g) == "Kane",
              "generic row: kick-off id only");
        voice::Entry go = callpick::make_entry(1001, "Bukayo Saka", kane, true);
        CHECK(go.voice_of && *go.voice_of == 0 && go.kickoff && *go.kickoff == 900017, "generic row for a player with his own recording: his own off + the id, in one entry");
        voice::Entry r = callpick::make_entry(1002, "Martin Odegaard", lob, false);
        CHECK(r.voice_of && *r.voice_of == 244263 && r.kickoff && *r.kickoff == -1 && r.from == "Stanislav Lobotka" && callpick::entry_name(r) == "Stanislav Lobotka",
              "real row: his voice, the surname lines silent");
        voice::Entry off_only;
        off_only.voice_of = 0;
        CHECK(callpick::entry_name(off_only) == "his own recording is off" && callpick::entry_name(voice::Entry{}) == "no callname", "entry words");
        // Table: exactly what the hooks read
        voice::VoiceStore s;
        s.upsert(g);
        const voice::Table t = voice::build_table(s);
        CHECK(t.kickoff(1002) && t.kickoff(1002)->id == 900017 && !t.voice(1002), "table: a kick-off id, no voice row");
    });
}

static void ui_cases(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    (void)kMb;
    run();  // the pure cases first
    run_case("UI: Callname page: one list, search, one click picks, Preview, Apply, Undo; names and database untouched", [&] {
        const fs::path le = g_out / "LE";
        const auto npos = std::string::npos;
        struct FakeVoice : voice::Service {
            bool on = true;
            int publishes = 0;
            voice::Table last;
            bool available() const override { return on; }
            std::string why_off() const override { return on ? "" : "game build"; }
            void publish(const voice::Table& t) override {
                ++publishes;
                last = t;
            }
            voice::StatsSnapshot stats() const override { return {}; }
            bool observe_on() const override { return false; }
            void refresh_switches() override {}
        };
        FakeVoice fv;
        struct Unset {
            App& a;
            ~Unset() {
                a.voice_service = nullptr;
                callname_use_legacy_page(false);
            }
        } unset{app};
        callname_use_legacy_page(false);
        auto read_store = [&]() {
            std::ifstream f(voice::store_path(le).string());
            std::stringstream ss;
            ss << f.rdbuf();
            return json::parse(ss.str(), nullptr, false);
        };
        // ita_it: Saka (1001) and 1004 have their own recording; 900002 (Saka) and 900017 (Kane) are generic callnames
        const fs::path mp = master_list_path(le, "ita_it");
        fs::create_directories(mp.parent_path());
        std::ofstream(mp.string()) << R"({"language": "ita_it", "game": "fc27", "real_players": [1001, 1004], "generic_ids": [900002, 900017],
                                         "names": {"1001": "Bukayo Saka", "1004": "Cristiano Ronaldo"}})";
        // a second language's master that must never show while ita_it is loaded
        const fs::path mp2 = master_list_path(le, "deu_de");
        std::ofstream(mp2.string()) << R"({"language": "deu_de", "game": "fc27", "real_players": [2001], "generic_ids": [900099],
                                          "names": {"2001": "Nur Deutsch"}})";
        const fs::path game = g_out / "fakegame";
        fs::create_directories(game / "commentary" / "commentaryfull_ita_it");
        app.game_root = game;
        app.callnames.refreshed = false;
        app.voice_service = &fv;
        app.voice_store = voice::VoiceStore{};
        fs::remove(voice::store_path(le));
        app.request_tab = 0;
        ui.frames(3);
        CHECK(ui.type_into(ui.find("##psearch", "##plist"), ""), "search cleared");
        CHECK(ui.click("1002", "##plist"), "row 1002");
        CHECK(ui.click("Callname", "##pedit"), "Callname tab");
        ui.frames(3);
        const CallnamePageState& pg = callname_page_state();
        CHECK(pg.playerid == 1002 && pg.language == "ita_it", "page for 1002 in ita_it: " + pg.language);
        // ---- ONE list: 2 generic + 2 real, no other language, no old tab, no id
        CHECK(pg.rows_listed == 4 && pg.rows_shown == 4, fmt("four callnames of ita_it (%d listed, %d shown)", pg.rows_listed, pg.rows_shown));
        CHECK(ui.find("##cnpsearch") && ui.find("##cnpsearch") != nullptr, "the search box");
        CHECK(!ui.find("By name") && !ui.find("By player") && !ui.find("All callnames") && !ui.find("Players without own recording") &&
                  !ui.find("Voice swaps") && !ui.find("##cnsearch") && !ui.find("##cnallsearch") && !ui.find("Refresh##cn") &&
                  !ui.find("Assign callname") && !ui.find("Use in matches##all") && !ui.find("Remove player-specific callname..."),
              "none of the older editor's tabs, boxes or buttons");
        CHECK(ui.find("###g900017", "cnplist") && ui.find("###g900002", "cnplist") && ui.find("###p1001", "cnplist") &&
                  ui.find("###p1004", "cnplist"),
              "the four rows (generic and real together)");
        CHECK(!ui.find("###p2001", "cnplist"), "nothing of deu_de");
        CHECK(pg.state_line == "Not changed: the game says 'Saliba'" || pg.state_line.find("Not changed: the game says ") == 0, "state line: " + pg.state_line);
        CHECK(!pg.apply_enabled && !pg.undo_shown && ui.find("Preview###cnpreview") && ui.find("Apply###cnapply") && !ui.find("Undo###cnundo"),
              "nothing picked: Preview and Apply drawn and greyed, no Undo");
        // ---- the search: text only
        CHECK(ui.type_into(ui.find("##cnpsearch"), "kan"), "type kan");
        ui.frames(1);
        CHECK(pg.rows_shown == 1 && ui.find("###g900017", "cnplist"), "search: Kane only");
        CHECK(ui.type_into(ui.find("##cnpsearch"), "900017"), "type an id");
        ui.frames(1);
        CHECK(pg.rows_shown == 0, "search: an id finds nothing");
        CHECK(ui.type_into(ui.find("##cnpsearch"), ""), "search cleared");
        ui.frames(1);
        // ---- one click picks; Apply is the only thing that writes
        const Table* pt = app.db.table("players");
        const Table* et = app.db.table("editedplayernames");
        const Table* mt = app.db.table("playernamemap");
        CHECK(pt && et && mt, "players, editedplayernames, playernamemap");
        auto snap = [&](const Table* t) {
            Snapshot s;
            s.load(app.db.memory(), *t);
            return s.data;
        };
        const auto before_p = snap(pt), before_e = snap(et), before_m = snap(mt);
        CHECK(!before_p.empty() && !before_e.empty() && !before_m.empty(), "table snapshots read");
        const int pub0 = fv.publishes;  // the service may have been handed an empty table already
        CHECK(ui.click("###g900017", "cnplist"), "ONE click on Kane");
        ui.frames(1);
        CHECK(pg.selected == "Kane" && pg.apply_enabled && pg.why_line.empty(), "picked: Apply can be pressed");
        CHECK(app.voice_store.entries.empty() && fv.publishes == pub0 && read_store().is_discarded(), "a click only picks: nothing written");
        CHECK(!pg.preview_enabled && ui.find("Preview###cnpreview"), "Preview is drawn (greyed: this test has no recordings to play)");
        CHECK(ui.click("Apply###cnapply"), "Apply");
        ui.frames(2);
        const voice::Entry* e = app.voice_store.find(1002);
        CHECK(app.voice_store.entries.size() == 1 && e && !e->voice_of && e->kickoff && *e->kickoff == 900017 && !e->names_only,
              "exactly one swap row: 1002 gets the generic callname 900017 in matches");
        CHECK(fv.publishes == pub0 + 1 && fv.last.kickoff(1002) && fv.last.kickoff(1002)->id == 900017 && !fv.last.voice(1002), "published to the hooks once");
        {
            json j = read_store();
            CHECK(j.is_object() && j["entries"].size() == 1 && j["entries"][0].value("playerid", 0) == 1002 && j["entries"][0].value("kickoff", 0) == 900017 &&
                      j["entries"][0]["voice_of"].is_null(),
                  "voice_swaps.json: that one row: " + j.dump());
        }
        CHECK(pg.state_line == "Applied: Kane" && pg.undo_shown && ui.find("Undo###cnundo"), "state line: " + pg.state_line);
        CHECK(snap(pt) == before_p && snap(et) == before_e && snap(mt) == before_m, "his players, editedplayernames and playernamemap rows: byte-equal");
        CHECK(!app.busy() && !app.reapply.callname(1002), "nothing queued for the game, nothing kept for the next career load");
        // ---- Undo removes it (nothing before it)
        CHECK(ui.click("Undo###cnundo"), "Undo");
        ui.frames(2);
        CHECK(app.voice_store.entries.empty() && fv.last.empty() && read_store()["entries"].empty(), "Undo: the swap row is gone, published and saved");
        CHECK(pg.state_line.find("Not changed: ") == 0 && !pg.undo_shown, "state line: " + pg.state_line);
        CHECK(snap(pt) == before_p && snap(et) == before_e && snap(mt) == before_m, "still byte-equal");
        // ---- a real player's recording
        CHECK(ui.click("###p1001", "cnplist") && ui.click("Apply###cnapply"), "pick Saka's own recording, Apply");
        ui.frames(2);
        e = app.voice_store.find(1002);
        CHECK(e && e->voice_of && *e->voice_of == 1001 && e->kickoff && *e->kickoff == -1 && pg.state_line == "Applied: Bukayo Saka", "his voice: " + pg.state_line);
        // ---- Undo gives back what he had before the last Apply
        CHECK(ui.click("###g900017", "cnplist") && ui.click("Apply###cnapply"), "pick Kane, Apply over it");
        ui.frames(2);
        e = app.voice_store.find(1002);
        CHECK(app.voice_store.entries.size() == 1 && e && !e->voice_of && e->kickoff && *e->kickoff == 900017, "one row, now Kane");
        CHECK(ui.click("Undo###cnundo"), "Undo");
        ui.frames(2);
        e = app.voice_store.find(1002);
        CHECK(app.voice_store.entries.size() == 1 && e && e->voice_of && *e->voice_of == 1001 && pg.state_line == "Applied: Bukayo Saka", "Undo: Saka's voice is back");
        CHECK(ui.click("Undo###cnundo"), "Undo again");
        ui.frames(2);
        CHECK(app.voice_store.entries.empty(), "a second Undo removes it");
        // ---- a player with his own recording: a generic callname turns his own off, in the same single entry
        app.sel_player = 1001;
        ui.frames(3);
        CHECK(pg.playerid == 1001, "Saka opened");
        CHECK(ui.click("###p1001", "cnplist"), "pick his own recording");
        ui.frames(1);
        CHECK(!pg.apply_enabled && pg.why_line == "That is his own recording already.", "his own recording on himself: Apply greyed, with the reason: " + pg.why_line);
        CHECK(ui.click("###g900017", "cnplist") && ui.click("Apply###cnapply"), "pick Kane, Apply");
        ui.frames(2);
        e = app.voice_store.find(1001);
        CHECK(app.voice_store.entries.size() == 1 && e && e->voice_of && *e->voice_of == 0 && e->kickoff && *e->kickoff == 900017,
              "one row: his own recording off + the callname 900017");
        CHECK(ui.click("Undo###cnundo"), "Undo");
        ui.frames(2);
        CHECK(app.voice_store.entries.empty(), "his own recording is back");
        // ---- the service off: Apply greyed with a plain reason; an existing swap can still be undone
        app.sel_player = 1002;
        ui.frames(3);
        CHECK(ui.click("###g900017", "cnplist") && ui.click("Apply###cnapply"), "Kane applied to 1002");
        ui.frames(2);
        fv.on = false;
        ui.frames(2);
        CHECK(!pg.apply_enabled && pg.why_line == "Callnames cannot be changed right now: this version of the game is not supported." && pg.off_line == pg.why_line,
              "service off: " + pg.why_line);
        CHECK(pg.undo_shown && ui.click("Undo###cnundo"), "Undo still there");
        ui.frames(2);
        CHECK(app.voice_store.entries.empty(), "undone with the service off");
        fv.on = true;
        // ---- the loaded language decides the list
        app.gui_settings["callnames"]["override_language"] = "deu_de";
        fs::create_directories(game / "commentary" / "commentaryfull_deu_de");
        app.callnames.refreshed = false;
        ui.frames(3);
        CHECK(pg.language == "deu_de" && pg.rows_listed == 2 && ui.find("###p2001", "cnplist") && !ui.find("###g900017", "cnplist") &&
                  !ui.find("###p1001", "cnplist"),
              fmt("deu_de loaded: its two rows only (%s, %d)", pg.language.c_str(), pg.rows_listed));
        fs::remove_all(game / "commentary" / "commentaryfull_deu_de");  // only ita_it's folder is left
        if (app.gui_settings.contains("callnames")) app.gui_settings["callnames"].erase("override_language");
        app.callnames.refreshed = false;
        ui.frames(3);
        CHECK(pg.language == "ita_it" && pg.rows_listed == 4, "back to ita_it: " + pg.language);
        // ---- dut_nl (2026-10-19): the master list (FC 26) has El Ghazi, the game's files do not: he is not offered, a search says why;
        // a recorded player is one click, Preview, Apply
        const fs::path mp3 = master_list_path(le, "dut_nl");
        std::ofstream(mp3.string()) << R"({"language": "dut_nl", "game": "fc26", "real_players": [216433, 198190, 299001], "generic_ids": [900010],
                                          "names": {"216433": "Anwar El Ghazi", "198190": "Greg Cunningham", "299001": "Davy Klaassen"}})";
        app.gui_settings["callnames"]["override_language"] = "dut_nl";
        fs::create_directories(game / "commentary" / "commentaryfull_dut_nl");
        app.callnames.refreshed = false;
        ui.frames(3);
        app.callnames.use_bank(make_bank("dut_nl", "", {900010}, {{299001, kBankSimple}}));
        ui.frames(3);
        CHECK(pg.language == "dut_nl" && pg.rows_listed == 2 && pg.rows_unsayable == 2, fmt("dut_nl: two rows, two left out (%s, %d, %d)", pg.language.c_str(), pg.rows_listed, pg.rows_unsayable));
        CHECK(ui.find("###p299001", "cnplist") && !ui.find("###p216433", "cnplist") && !ui.find("###p198190", "cnplist"),
              "Klaassen is offered, El Ghazi and Cunningham are not");
        CHECK(ui.type_into(ui.find("##cnpsearch"), "ghazi"), "type ghazi");
        ui.frames(1);
        CHECK(pg.rows_shown == 0 && pg.missing_line == "Anwar El Ghazi has no recording in the game's dut_nl commentary, so the game cannot say it.",
              "search: no row, and the line says why: " + pg.missing_line);
        CHECK(ui.type_into(ui.find("##cnpsearch"), "klaas"), "type klaas");
        ui.frames(1);
        CHECK(pg.rows_shown == 1 && pg.missing_line.empty(), "a recorded name: its row, no line");
        CHECK(ui.click("###p299001", "cnplist"), "ONE click on Klaassen");
        ui.frames(1);
        CHECK(pg.selected == "Davy Klaassen" && pg.apply_enabled && pg.why_line.empty() && ui.find("Preview###cnpreview"), "picked: Preview drawn, Apply can be pressed");
        CHECK(ui.click("Apply###cnapply"), "Apply");
        ui.frames(2);
        e = app.voice_store.find(1002);
        CHECK(app.voice_store.entries.size() == 1 && e && e->voice_of && *e->voice_of == 299001 && pg.state_line == "Applied: Davy Klaassen", "applied: " + pg.state_line);
        CHECK(ui.click("Undo###cnundo"), "Undo");
        ui.frames(2);
        CHECK(app.voice_store.entries.empty(), "undone");
        CHECK(ui.type_into(ui.find("##cnpsearch"), ""), "search cleared");
        if (app.gui_settings.contains("callnames")) app.gui_settings["callnames"].erase("override_language");
        fs::remove(mp3);
        fs::remove_all(game / "commentary" / "commentaryfull_dut_nl");
        app.callnames.refreshed = false;
        ui.frames(3);
        // ---- clean up
        app.voice_service = nullptr;
        app.voice_store = voice::VoiceStore{};
        fs::remove(voice::store_path(le));
        fs::remove(mp);
        fs::remove(mp2);
        fs::remove_all(game / "commentary" / "commentaryfull_deu_de");
        app.game_root.clear();
        app.callnames.refreshed = false;
        ui.frames(2);
        (void)mem;
        (void)npos;
    });
}

}  // namespace callname_pick_test
