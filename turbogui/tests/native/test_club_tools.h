// Native tests (Turbo 1.1.1): reopen the club customisation hub (core/hub_customise.h) and the career settings unlock
// (core/career_settings.h). Included by test_main.cpp after its framework (CHECK, run_case, SimMemory, g_out,
// read_file). Every game file here is SYNTHETIC: it mimics the schema of gamesettings_context_Career.json (contexts ->
// categories -> settings with "alwaysLocked"), never EA's file.
#pragma once
#include "core/career_settings.h"
#include "core/hub_customise.h"

namespace club_tools_test {

using namespace turbo;
using ojs = nlohmann::ordered_json;  // the fixtures keep the game's key order (name first)

// ---------------------------------------------------------------- synthetic MainHubManager in a career manager table
struct HubWorld {
    SimMemory mem;
    static constexpr uint64_t kManagers = 0x31000000ULL, kObj = 0x31010000ULL, kVt = 0x14B016730ULL;
    explicit HubWorld(uint8_t licensed = 0, uint8_t customised = 1, uint8_t new_season = 0) {
        mem.map(kManagers, 0x2000);
        mem.map(kObj, 0x1000);
        const uint64_t slot = kManagers + svm::kSlotSize * static_cast<uint64_t>(mhm::kTypeId), type = kManagers + 0x1800,
                       holder = kManagers + 0x1900;
        mem.wr(slot + svm::kSlotCount, static_cast<int32_t>(1));
        mem.wr(slot + svm::kSlotType, type);
        mem.wr(type + svm::kTypeFlag, static_cast<int32_t>(1));
        mem.wr(slot + svm::kSlotHolder, holder);
        mem.wr(holder, kObj);
        mem.wr(kObj, kVt);
        mem.wr(kObj + mhm::kTable, kManagers);
        mem.wr(kObj + mhm::kLicensed, licensed);
        mem.wr(kObj + mhm::kCustomised, customised);
        mem.wr(kObj + mhm::kNewSeason, new_season);
    }
    uint8_t byte(uint64_t off) {
        uint8_t b = 0xEE;
        mem.rd(kObj + off, b);
        return b;
    }
};

// ---------------------------------------------------------------- synthetic career settings files
// The hub context with the 32 locked settings of the plan (names only; values are the game's own and never shipped)
static std::string career_fixture(bool bom_crlf = true) {
    auto set = [](const char* n, bool locked) {
        ojs s = ojs::object();
        s["name"] = n;
        if (locked) s["alwaysLocked"] = true;
        return s;
    };
    auto cat = [](const char* n, std::vector<ojs> settings) {
        ojs c = ojs::object();
        c["name"] = n;
        c["settings"] = settings;
        return c;
    };
    std::vector<ojs> match = {set("HALF_LENGTH", false), set("DIFFICULTY_LEVEL", false)};
    for (const char* n : {"CAREER_QUICK_SIM", "CAREER_SIM_MATCH", "CAREER_TACTICAL_VIEW", "CAREER_PLAY_HIGHLIGHTS",
                          "CAREER_ATTACKING_HIGHLIGHTS", "CAREER_MATCH_RESTART"})
        match.push_back(set(n, true));
    std::vector<ojs> training, transfers, general;
    for (const char* n : {"CAREER_ENERGY_RECOVERY", "CAREER_TRAINING_PLAN_TRAINING_DRILLS", "CAREER_DEVELOPMENT_RATE_SENIORS",
                          "CAREER_DEVELOPMENT_RATE_YOUTH"})
        training.push_back(set(n, true));
    for (const char* n : {"CAREER_TRANSFER", "CAREER_TRANSFER_EMBARGO_DURATION", "CAREER_TRANSFER_NEGOTIATION",
                          "CAREER_NEGOTIATION_STRICTNESS", "CAREER_CONTRACT_EXTENSION", "CAREER_SCOUTING", "CAREER_SEARCH_PLAYERS",
                          "CAREER_SCOUT_SPEED_STAR_PLAYERS", "CAREER_SCOUT_SPEED_GENERAL_PLAYERS"})
        transfers.push_back(set(n, true));
    for (const char* n : {"CAREER_BOARD_EXPECTATIONS", "CAREER_INTERNATIONAL_JOB_OFFERS", "CAREER_MANAGER_MARKET",
                          "CAREER_MANAGER_MARKET_REALISM", "CAREER_COMPETITION", "CAREER_DEEPER_SIMULATION", "CAREER_FINANCIAL_TAKEOVER",
                          "CAREER_CURRENCY", "CAREER_YOUTH_ACADEMY", "CAREER_UNEXPECTED_EVENTS", "CAREER_UNEXPECTED_EVENTS_FREQUENCY",
                          "CAREER_PITCH_WEAR", "CAREER_POINTS_DEDUCTION"})
        general.push_back(set(n, true));
    general[4]["checker"] = ojs{{"op", "and"}, {"parts", {"IsValidCareerCompetition", "IsNotCareerLSP"}}};
    ojs sim = ojs::object();
    sim["name"] = "GAMEPLAY_SIMULATION_CATEGORY";
    sim["categories"] = ojs::array({ojs{{"name", "SIMULATION_PRESET"}}, ojs{{"name", "SIMULATION_SPEED"}}});
    ojs hub = ojs::object();
    hub["name"] = "FROM_CAREER_MANAGER_HUB";
    hub["categories"] = ojs::array({cat("CAREER_MATCH_SETUP", match), sim, cat("CAREER_TRAINING", training),
                                     cat("CAREER_TRANSFERS_SCOUTING", transfers), cat("CAREER_GENERAL", general)});
    ojs player_general = cat("CAREER_GENERAL", {set("CAREER_LOANOUT", true), set("CAREER_RETIREMENT", true)});
    player_general["settings"][1]["checker"] = "IsRealOrLegendaryPlayer";
    ojs player = ojs::object();
    player["name"] = "FROM_CAREER_PLAYER_HUB";
    player["categories"] = ojs::array({player_general});
    ojs root = ojs::object();
    root["contexts"] = ojs::array({hub, player});
    std::string text = root.dump(4);
    if (!bom_crlf) return text;
    std::string out = "\xEF\xBB\xBF";
    for (char c : text) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

static std::string setup_fixture() {
    ojs squad = {{"name", "CAREER_SQUAD"},
                  {"settings", ojs::array({ojs{{"name", "CAREER_EDIT_INJURIES"}}, ojs{{"name", "CAREER_EDIT_SUSPENSIONS"}},
                                            ojs{{"name", "CAREER_RELEASE_PLAYERS"}, {"checker", "IsNotTeamWithLegends"}}})}};
    ojs ctx = {{"name", "FROM_CAREER_SETUP"},
                {"categories", ojs::array({ojs{{"name", "CAREER_TRAINING"}, {"settings", ojs::array()}}, squad,
                                            ojs{{"name", "CAREER_TRANSFERS_SCOUTING"}, {"settings", ojs::array()}}})}};
    return ojs{{"contexts", ojs::array({ctx})}}.dump(4);
}

// the object (anywhere below n) whose "name" is `name`
static const json* find_named(const json& n, const std::string& name) {
    if (n.is_object()) {
        if (n.value("name", std::string()) == name) return &n;
        for (const auto& kv : n.items())
            if (kv.value().is_structured())
                if (const json* f = find_named(kv.value(), name)) return f;
    } else if (n.is_array()) {
        for (const auto& x : n)
            if (const json* f = find_named(x, name)) return f;
    }
    return nullptr;
}

static void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static void run() {
    // ------------------------------------------------------------ club customisation hub
    run_case("club customisation: created club, designer used this season -> +0x512 = 0, +0x511 untouched", [&] {
        HubWorld w(0, 1, 1);
        mhm::State s;
        CHECK(mhm::locate(w.mem, HubWorld::kManagers, HubWorld::kVt, mhm::kCreatedClubTeam, s).empty() && s.obj == HubWorld::kObj, "located");
        CHECK(s.created_club && s.customised == 1 && !s.tile_shown(), "hidden this season: " + mhm::describe(s));
        mhm::Result r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, mhm::kCreatedClubTeam, false);
        CHECK(r.ok && r.changed, "reopened: " + r.message);
        CHECK(w.byte(mhm::kCustomised) == 0 && w.byte(mhm::kLicensed) == 0 && w.byte(mhm::kNewSeason) == 1, "only +0x512 written");
        CHECK(r.after.tile_shown() && r.message.find("leave the hub") != std::string::npos && r.message.find("Kits, crest") != std::string::npos,
              "message: " + r.message);
        r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, mhm::kCreatedClubTeam, false);
        CHECK(r.ok && !r.changed && r.message.find("Nothing to change") != std::string::npos, "second press: " + r.message);
        CHECK(w.mem.failed_reads == 0, "no read outside the synthetic memory");
    });

    run_case("club customisation: refusals write nothing (no career, unknown vtable, wrong object, bad flags)", [&] {
        {
            HubWorld w;
            mhm::Result r = mhm::reopen(w.mem, 0, HubWorld::kVt, 48, false);
            CHECK(!r.ok && r.message.find("no career") != std::string::npos && w.byte(mhm::kCustomised) == 1, "no table: " + r.message);
            r = mhm::reopen(w.mem, HubWorld::kManagers, 0, 48, false);
            CHECK(!r.ok && r.message.find("not known") != std::string::npos && w.byte(mhm::kCustomised) == 1, "no vtable: " + r.message);
            r = mhm::reopen(w.mem, HubWorld::kManagers, 0x14B0160D8ULL, 48, false);
            CHECK(!r.ok && r.message.find("is not the MainHubManager") != std::string::npos && w.byte(mhm::kCustomised) == 1,
                  "another class's vtable: " + r.message);
        }
        {
            HubWorld w;
            w.mem.wr(HubWorld::kObj + mhm::kTable, HubWorld::kManagers + 0x40);
            mhm::Result r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, 48, false);
            CHECK(!r.ok && r.message.find("point back") != std::string::npos && w.byte(mhm::kCustomised) == 1, "table: " + r.message);
        }
        {
            HubWorld w;
            w.mem.wr(HubWorld::kManagers + svm::kSlotSize * 58 + svm::kSlotCount, static_cast<int32_t>(0));
            mhm::Result r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, 48, false);
            CHECK(!r.ok && r.message.find("MainHubManager: manager slot 58") != std::string::npos, "empty slot: " + r.message);
        }
        {
            HubWorld w(7, 1, 0);
            mhm::Result r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, 48, true);
            CHECK(!r.ok && r.message.find("expected 0 or 1") != std::string::npos && w.byte(mhm::kLicensed) == 7 &&
                      w.byte(mhm::kCustomised) == 1, "garbage flags: " + r.message);
        }
        {
            // the object sits at the end of its mapping: +0x511 is not readable
            SimMemory m;
            const uint64_t mgr = 0x32000000ULL, obj = 0x32010F80ULL;
            m.map(mgr, 0x2000);
            m.pages.erase(0x32010000ULL / SimMemory::kPage + 1);
            m.map(0x32010000ULL, 0xF00);
            const uint64_t slot = mgr + svm::kSlotSize * 58, type = mgr + 0x1800, holder = mgr + 0x1900;
            m.wr(slot + svm::kSlotCount, static_cast<int32_t>(1));
            m.wr(slot + svm::kSlotType, type);
            m.wr(type + svm::kTypeFlag, static_cast<int32_t>(1));
            m.wr(slot + svm::kSlotHolder, holder);
            m.wr(holder, obj);
            m.wr(obj, HubWorld::kVt);
            m.wr(obj + mhm::kTable, mgr);
            mhm::State s;
            std::string err = mhm::locate(m, mgr, HubWorld::kVt, 48, s);
            CHECK(err.find("not readable over its 0x8A8 bytes") != std::string::npos && s.obj == 0, "short object: " + err);
        }
    });

    run_case("club customisation: licensed stadium only with the opt-in", [&] {
        HubWorld w(1, 0, 0);
        mhm::State s;
        CHECK(mhm::locate(w.mem, HubWorld::kManagers, HubWorld::kVt, 48, s).empty() && !s.tile_shown() &&
                  mhm::describe(s).find("licensed stadium") != std::string::npos, "hidden: " + mhm::describe(s));
        mhm::Result r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, 48, false);
        CHECK(!r.ok && !r.changed && r.message.find("back up your save") != std::string::npos && w.byte(mhm::kLicensed) == 1,
              "refused without the opt-in: " + r.message);
        r = mhm::reopen(w.mem, HubWorld::kManagers, HubWorld::kVt, 48, true);
        CHECK(r.ok && r.changed && w.byte(mhm::kLicensed) == 0 && r.message.find("licensed stadium too") != std::string::npos &&
                  r.message.find("Stadium hub") != std::string::npos,
              "opt-in: " + r.message);
        // a club without a licensed stadium already has the stadium hub
        HubWorld v(0, 0, 1);
        r = mhm::reopen(v.mem, HubWorld::kManagers, HubWorld::kVt, 48, false);
        CHECK(r.ok && !r.changed && mhm::describe(r.after).find("stadium hub") != std::string::npos, "already there: " + r.message);
    });

    run_case("club customisation: the vtable comes from the signature, else the image base (this build)", [&] {
        mhm::set_signature_lookup(nullptr);
        CHECK(mhm::vtable(0x140000000ULL) == 0x14B016730ULL && mhm::vtable(0) == 0, "image base fallback");
        std::string asked;
        mhm::set_signature_lookup([&](const char* n) -> uint64_t {
            asked = n;
            return 0x150000000ULL;
        });
        CHECK(mhm::vtable(0x140000000ULL) == 0x150000000ULL && asked == "mhm_vtable", "signature first");
        mhm::set_signature_lookup([](const char*) -> uint64_t { return 0; });
        CHECK(mhm::vtable(0x140000000ULL) == 0x14B016730ULL, "unresolved signature: fallback");
        mhm::set_signature_lookup(nullptr);
        // the built-in signature resolves the constructor's lea on synthetic code
        const SignatureTable* t = builtin_signature_table("6AB9813C-211EF000");
        const Signature* s = t ? t->find("mhm_vtable") : nullptr;
        CHECK(s && s->resolve == "rip" && s->offset == 4, "built-in entry");
        if (s) {
            std::vector<uint8_t> code(0x40, 0xCC);
            const uint8_t ctor[] = {0x48, 0x89, 0x51, 0x08, 0x48, 0x8D, 0x05, 0x1D, 0xF8, 0x25, 0x03, 0x48, 0x89, 0x01,
                                    0x83, 0xCE, 0xFF, 0x89, 0x71, 0x10, 0x48, 0x8D, 0x05, 0x25, 0x69, 0xEF, 0x03};
            std::memcpy(code.data() + 0x14, ctor, sizeof(ctor));
            SigResult r = resolve_signature(*s, code.data(), code.size(), 0x147DB6EF4ULL);
            CHECK(r.state == SigState::Found && r.address == 0x14B016730ULL, fmt("resolved to 0x%llX (%s)", (unsigned long long)r.address, r.error.c_str()));
        }
    });

    run_case("club customisation: save backup copies the Manager Career saves only", [&] {
        const fs::path saves = g_out / "club_saves", out = g_out / "club_save_backups";
        fs::remove_all(saves);
        fs::remove_all(out);
        mhm::SaveBackup b = mhm::backup_saves(saves, out, "s1");
        CHECK(!b.ok && b.message.find("not found") != std::string::npos, "no folder: " + b.message);
        put(saves / "Settings.ini", "x");
        b = mhm::backup_saves(saves, out, "s1");
        CHECK(!b.ok && b.message.find("no Manager Career save") != std::string::npos, "no save: " + b.message);
        put(saves / "CmMgrC20261004120000000", "career one");
        put(saves / "CmMgrC20261004130000000", "career two");
        b = mhm::backup_saves(saves, out, "s2");
        CHECK(b.ok && b.files == 2 && read_file(out / "s2" / "CmMgrC20261004120000000") == "career one" &&
                  !fs::exists(out / "s2" / "Settings.ini"),
              "copied: " + b.message);
        CHECK(read_file(saves / "CmMgrC20261004130000000") == "career two", "the save itself untouched");
    });

    // ------------------------------------------------------------ career settings recipe
    run_case("career settings: the recipe unlocks 27 of the 32 hub settings and nothing else", [&] {
        std::string out;
        csu::RecipeReport rep;
        CHECK(csu::apply_recipe(career_fixture(), "", csu::RecipeOptions(), out, rep), "recipe: " + rep.error);
        CHECK(rep.unlocked.size() == 27 && rep.kept_locked.size() == 5, fmt("%zu unlocked, %zu kept", rep.unlocked.size(), rep.kept_locked.size()));
        json o = json::parse(out);
        const json& hub = o["contexts"][0];
        for (const char* n : {"CAREER_QUICK_SIM", "CAREER_DEVELOPMENT_RATE_SENIORS", "CAREER_TRANSFER", "CAREER_BOARD_EXPECTATIONS",
                              "CAREER_MANAGER_MARKET", "CAREER_PITCH_WEAR", "CAREER_POINTS_DEDUCTION"}) {
            const json* s = find_named(hub, n);
            CHECK(s && !s->contains("alwaysLocked"), std::string("unlocked: ") + n);
        }
        for (const std::string& n : csu::keep_locked()) {
            const json* s = find_named(hub, n);
            CHECK(s && s->value("alwaysLocked", false), "kept locked: " + n);
        }
        const json* comp = find_named(hub, "CAREER_COMPETITION");
        CHECK(comp && (*comp)["checker"]["parts"].size() == 2, "checker kept");
        const json* loan = find_named(o["contexts"][1], "CAREER_LOANOUT");
        const json* ret = find_named(o["contexts"][1], "CAREER_RETIREMENT");
        CHECK(loan && loan->value("alwaysLocked", false) && ret && ret->value("alwaysLocked", false) && ret->value("checker", "") == "IsRealOrLegendaryPlayer",
              "the player career context is left alone");
        CHECK(out.compare(0, 3, "\xEF\xBB\xBF") != 0 && out.find("{\n  \"contexts\"") == 0, "no BOM, plain JSON");
        // the order of keys and entries is the game's (name first)
        CHECK(out.find("\"name\": \"CAREER_COMPETITION\",\n") != std::string::npos, "name stays first");
        CHECK(hub["categories"][1]["categories"].size() == 2, "nested categories kept");
        // feeding the result back: nothing left to unlock
        std::string again;
        CHECK(!csu::apply_recipe(out, "", csu::RecipeOptions(), again, rep) && rep.error.find("no locked setting") != std::string::npos,
              "already unlocked: " + rep.error);
    });

    run_case("career settings: the Squad settings experiment and the refusals", [&] {
        std::string out;
        csu::RecipeReport rep;
        csu::RecipeOptions opt;
        opt.squad_settings = true;
        CHECK(csu::apply_recipe(career_fixture(false), setup_fixture(), opt, out, rep) && rep.squad_added, "with squad: " + rep.error);
        json o = json::parse(out);
        const json& cats = o["contexts"][0]["categories"];
        CHECK(cats.size() == 6 && cats[3]["name"] == "CAREER_SQUAD" && cats[4]["name"] == "CAREER_TRANSFERS_SCOUTING" &&
                  cats[3]["settings"].size() == 3 && cats[3]["settings"][2]["checker"] == "IsNotTeamWithLegends",
              "inserted before transfers and scouting");
        CHECK(csu::apply_recipe(career_fixture(), "", opt, out, rep) && !rep.squad_added && rep.squad_note.find("not available") != std::string::npos,
              "no setup file: " + rep.squad_note);
        CHECK(csu::apply_recipe(career_fixture(), "{\"contexts\": []}", opt, out, rep) && !rep.squad_added &&
                  rep.squad_note.find("no CAREER_SQUAD") != std::string::npos,
              "setup without the category: " + rep.squad_note);
        CHECK(!csu::apply_recipe("{\"contexts\": [", "", csu::RecipeOptions(), out, rep) && rep.error.find("does not parse") != std::string::npos && out.empty(),
              "broken file: " + rep.error);
        CHECK(!csu::apply_recipe("{\"contexts\": [{\"name\": \"FROM_CAREER_PLAYER_HUB\", \"categories\": []}]}", "", csu::RecipeOptions(), out, rep) &&
                  rep.error.find("FROM_CAREER_MANAGER_HUB") != std::string::npos,
              "no hub context: " + rep.error);
        CHECK(!csu::apply_recipe("[1, 2]", "", csu::RecipeOptions(), out, rep), "not an object");
        CHECK(csu::content_hash("abc") == csu::content_hash("abc") && csu::content_hash("abc") != csu::content_hash("abd") &&
                  csu::content_hash("").size() == 16,
              "hash");
    });

    run_case("career settings: export -> original kept -> override written -> restore (hash-guarded)", [&] {
        const fs::path le = g_out / "career_settings_le";
        fs::remove_all(le);
        LegacyImages L(le);
        csu::Store st(L, le);
        csu::Store::Status s = st.status();
        CHECK(!s.original && s.waiting && !s.written && s.line.find("waiting") != std::string::npos, "nothing exported yet: " + s.line);
        L.flush();
        const std::string want = read_file(L.cache_dir() / "want.txt");
        CHECK(want.find(csu::kPath) != std::string::npos && want.find(csu::kSetupPath) != std::string::npos, "asked the game: " + want);
        std::string msg;
        CHECK(!st.apply(csu::RecipeOptions(), msg) && msg.find("not exported yet") != std::string::npos, "apply before the export: " + msg);
        // Lua's LegacyFileExport delivered the game's file
        const std::string original = career_fixture();
        put(L.cache_dir() / "data" / "gamesettings" / "gamesettings_context_Career.json", original);
        s = st.status();
        CHECK(s.original && read_file(st.original_file(csu::kPath)) == original, "original kept: " + s.line);
        json man = json::parse(read_file(st.manifest_file()));
        CHECK(man["original_hash"] == csu::content_hash(original) && man["written_hash"] == "", "manifest: " + man.dump());
        // the squad experiment needs Career_Setup.json
        csu::RecipeOptions sq;
        sq.squad_settings = true;
        CHECK(!st.apply(sq, msg) && msg.find("Career_Setup.json") != std::string::npos, "squad without the setup file: " + msg);
        CHECK(st.apply(csu::RecipeOptions(), msg) && msg.find("27 settings") != std::string::npos, "apply: " + msg);
        const fs::path custom = L.mods_dir() / "data" / "gamesettings" / "gamesettings_context_Career.json";
        CHECK(fs::exists(custom) && find_named(json::parse(read_file(custom)), "CAREER_SCOUTING")->count("alwaysLocked") == 0, "override in mods\\legacy");
        s = st.status();
        CHECK(s.written && !s.foreign && !s.squad_settings && s.line.find("Unlocked") == 0, "status: " + s.line);
        // a fresh export that returns Turbo's own override is never taken as the original
        fs::remove(st.original_file(csu::kPath));
        put(L.cache_dir() / "data" / "gamesettings" / "gamesettings_context_Career.json", read_file(custom));
        s = st.status();
        CHECK(!s.original && !fs::exists(st.original_file(csu::kPath)), "override refused as original: " + s.line);
        put(L.cache_dir() / "data" / "gamesettings" / "gamesettings_context_Career.json", original);
        CHECK(st.status().original, "the real export is taken again");
        // the squad experiment once Career_Setup.json is exported
        put(L.cache_dir() / "data" / "gamesettings" / "gamesettings_context_Career_Setup.json", setup_fixture());
        CHECK(st.apply(sq, msg) && msg.find("Squad settings") != std::string::npos, "apply with squad: " + msg);
        CHECK(st.status().squad_settings, "status says squad");
        // restore: Turbo's file goes (backed up), the second restore has nothing to do
        CHECK(st.restore(msg) && !fs::exists(custom) && msg.find("backed up") != std::string::npos, "restore: " + msg);
        CHECK(!st.status().written && !st.restore(msg) && msg.find("nothing to restore") != std::string::npos, "nothing left: " + msg);
        // another mod's file: kept by Restore, Apply refused, never overwritten
        put(custom, "{\"contexts\": []}");
        s = st.status();
        CHECK(s.foreign && !s.written, "foreign file seen: " + s.line);
        CHECK(!st.apply(csu::RecipeOptions(), msg) && read_file(custom) == "{\"contexts\": []}", "apply refused: " + msg);
        CHECK(!st.restore(msg) && fs::exists(custom) && msg.find("not written by Turbo") != std::string::npos, "restore keeps it: " + msg);
        // a new Store (next game session) reads the manifest
        fs::remove(custom);
        CHECK(st.apply(csu::RecipeOptions(), msg), "apply again: " + msg);
        csu::Store st2(L, le);
        CHECK(st2.status().written && st2.restore(msg), "next session restores: " + msg);
    });
}

}  // namespace club_tools_test
