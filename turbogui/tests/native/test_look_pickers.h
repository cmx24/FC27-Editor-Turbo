// 5.0b-A (brief 5.0b-A-pickers, Pit wall correction 00:47): ONE registry of look attributes (core/look_pickers.h) drives ONE Look card
// for players and for managers. This file holds STATEMENTS, not functions: test_main.cpp includes it once, inside test_ui(), right after
// test_one_picker.h, so app / ui / le / g_lua are in scope. No #pragma once on purpose. Every case name starts "test_look_pickers" (the
// filter `TURBO_TEST_ONLY=test_look_pickers` selects exactly these); every CHECK is inside a case.
//
//   (a) every column of the LITERAL player look-column list has exactly one registry entry, the typed-only columns are listed with their
//       reason, and the registry holds nothing else for players
//   (b) the same for managers, BEHAVIOURALLY: the registry's manager rows hold hair, beard, eye, skin ... (the old tree had none); a column
//       both tables have is ONE entry with ONE kind; no duplicate column, every entry has words and a section
//   (b2) no dead button: every picker the card would draw opens a non-empty list (the fixture's tables) or has a static catalogue
//   (d) parity: the hair and beard popup offers the union of what the three pickers it replaced offered (the Gallery list, the visual
//       picker's list, the 3D band), the band codes round-trip, the reason line of a greyed section is today's text
//   (c) is added with the duplicate removal (commit 3 of the track): the number of places that offer a hair or beard choice is 1
{
    namespace lpk = turbo::look;
    namespace lpfs = std::filesystem;
    const lpfs::path lp_ui = lpfs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui";  // g_lua = <root>/tests/native/gui_world.lua
    struct LpTyped {
        const char* column;
        const char* why;
    };
    // ---- the literal lists (read from the FC 27 schema of this tree: turbo/le27/fc27_db_schema.json, 152 players columns, 52 manager columns)
    const std::vector<std::string> lp_player_picked = {
        "headassetid", "headtypecode", "hairtypecode", "haircolorcode", "facialhairtypecode", "facialhaircolorcode", "eyebrowcode", "eyecolorcode",
        "skintonecode", "skincomplexion", "bodytypecode", "tattoohead", "tattoofront", "tattooback", "tattooleftarm", "tattoorightarm", "tattooleftleg",
        "tattoorightleg", "runstylecode", "animfreekickstartposcode", "animpenaltiesstartposcode", "shoetypecode", "shoecolorcode1", "shoecolorcode2",
        "shoedesigncode", "smallsidedshoetypecode", "gkglovetypecode", "jerseysleevelengthcode", "hasseasonaljersey", "jerseyfit", "jerseystylecode",
        "shortstyle", "undershortstyle", "socklengthcode", "sockstylecode", "accessorycode1", "accessorycode2", "accessorycode3", "accessorycode4",
        "accessorycolourcode1", "accessorycolourcode2", "accessorycolourcode3", "accessorycolourcode4"};
    const std::vector<LpTyped> lp_player_typed = {
        {"hairstylecode", "the 3D gate and the chooser write it with the head; no catalogue of its values exists"},
        {"headclasscode", "the real / generic head switch: the chooser and the 3D gate set it with the head"},
        {"eyedetail", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"faceposerpreset", "no catalogue of its values (field_labels words only)"},
        {"facepsdlayer0", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"facepsdlayer1", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"headvariation", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"hashighqualityhead", "a flag of the head asset: the chooser sets it with the head"},
        {"lipcolor", "no catalogue of its values (field_labels words only)"},
        {"sideburnscode", "no catalogue of its values (field_labels words only)"},
        {"skinmakeup", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"skinsurfacepack", "an asset pack id; the real-face chooser copies it with the face"},
        {"skintypecode", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"height", "a number (cm): Profile"},
        {"weight", "a number (kg): Profile"},
        {"muscularitycode", "out of scope in the brief (5.0b-A-pickers.md:17): two census values, no picture anywhere"},
    };
    const std::vector<std::string> lp_manager_picked = {
        "headassetid", "headtypecode", "hairtypecode", "haircolorcode", "facialhairtypecode", "facialhaircolorcode", "eyebrowcode", "eyecolorcode",
        "skintonecode", "skincomplexion", "bodytypecode", "outfitid", "seasonaloutfitid", "accessorycode1", "accessorycode2", "accessorycode3",
        "accessorycode4", "accessorycolourcode1", "accessorycolourcode2", "accessorycolourcode3", "accessorycolourcode4"};
    const std::vector<LpTyped> lp_manager_typed = {
        {"hairstylecode", "the 3D gate writes it with the head; no catalogue of its values exists"},
        {"headclasscode", "the real / generic head switch: the chooser and the 3D gate set it with the head"},
        {"eyedetail", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"faceposerpreset", "no catalogue of its values (field_labels words only)"},
        {"facepsdlayer0", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"facepsdlayer1", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"headvariation", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"hashighqualityhead", "a flag of the head asset: the chooser sets it with the head"},
        {"lipcolor", "no catalogue of its values (field_labels words only)"},
        {"sideburnscode", "no catalogue of its values (field_labels words only)"},
        {"skinmakeup", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"skinsurfacepack", "an asset pack id; the real-face chooser copies it with the face"},
        {"skintypecode", "no catalogue of its values; the real-face chooser copies it with the face"},
        {"height", "a number (cm): Details"},
        {"weight", "a number (kg): Details"},
        {"gender", "who he is (Details); not a look"},
    };

    auto lp_schema = [&]() {
        const lpfs::path schema = lpfs::path(g_lua).parent_path().parent_path().parent_path().parent_path() / "turbo" / "le27" / "fc27_db_schema.json";
        std::ifstream f(schema, std::ios::binary);
        return f ? nlohmann::json::parse(f, nullptr, false) : nlohmann::json();
    };
    auto lp_has = [](const nlohmann::json& j, const char* table, const std::string& field) {
        if (!j.is_object() || !j.contains("tables") || !j["tables"].contains(table)) return false;
        for (const nlohmann::json& fd : j["tables"][table]["fields"])
            if (fd.value("name", "") == field) return true;
        return false;
    };
    auto lp_entries = [](const std::string& column) {
        int n = 0;
        for (const lpk::LookPicker& p : lpk::look_pickers())
            if (column == p.column) ++n;
        return n;
    };
    // the picker's list of a field on a record of the fixture world: open it, read the codes, close it
    auto lp_open = [&](const Table& t, uint64_t r, const std::string& field, bool manager) {
        std::vector<int64_t> codes;
        visual_picker_close();
        ui.frames(2);
        vp::open(app, t, r, field, manager);
        ui.frames(3);
        if (visual_picker_ui().open && visual_picker_ui().field == field) codes = visual_picker_codes();
        visual_picker_close();
        ui.frames(2);
        return codes;
    };

    run_case("test_look_pickers (a): every player look column has ONE registry entry; the typed-only ones are listed with a reason; nothing else is registered for players", [&] {
        const std::set<std::string> picked(lp_player_picked.begin(), lp_player_picked.end());
        CHECK(picked.size() == lp_player_picked.size(), "the literal list of picked player columns has no duplicate");
        std::set<std::string> typed;
        for (const LpTyped& t : lp_player_typed) {
            typed.insert(t.column);
            CHECK(!picked.count(t.column), std::string(t.column) + " is both picked and typed-only");
            CHECK(t.why && t.why[0], std::string(t.column) + " is typed-only with a reason");
            const lpk::LookPicker* e = lpk::find(t.column);
            CHECK(!e || !(e->subjects & lpk::kPlayer), std::string(t.column) + " is typed-only, so it has no player picker");
        }
        CHECK(typed.size() == lp_player_typed.size(), "the typed-only list has no duplicate");
        for (const std::string& c : lp_player_picked) {
            const lpk::LookPicker* e = lpk::find(c.c_str());
            CHECK(lp_entries(c) == 1 && e && (e->subjects & lpk::kPlayer), c + " has exactly one registry entry, and players have it");
        }
        size_t entries = 0;
        for (const lpk::LookPicker& p : lpk::look_pickers()) {
            if (!(p.subjects & lpk::kPlayer)) continue;
            ++entries;
            CHECK(picked.count(p.column) != 0, std::string("registry entry ") + p.column + " is a player picker the literal list does not know");
        }
        CHECK(entries == picked.size(), "the registry holds exactly the player columns of the literal list (" + std::to_string(entries) + " vs " + std::to_string(picked.size()) + ")");
        // the lists are read from the schema: every column named exists in the players table (skipped when the schema file is not here)
        const nlohmann::json j = lp_schema();
        if (j.is_discarded() || !j.contains("tables")) {
            std::printf("  note: the FC 27 schema is not here (a fixture copied with turbo\\le27\\libs): the schema check of the player lists is skipped\n");
            return;
        }
        for (const std::string& c : picked) CHECK(lp_has(j, "players", c), "players." + c + " is in the schema");
        for (const std::string& c : typed) CHECK(lp_has(j, "players", c), "players." + c + " is in the schema");
    });

    run_case("test_look_pickers (b): a manager has the same picker for every look column its table has (hair, beard, eyes, skin ...), one entry and one kind per column", [&] {
        const std::set<std::string> picked(lp_manager_picked.begin(), lp_manager_picked.end());
        CHECK(picked.size() == lp_manager_picked.size(), "the literal list of picked manager columns has no duplicate");
        // BEHAVIOURAL: the manager rows of the registry (the old tree has none)
        const std::vector<const lpk::LookPicker*> rows = lpk::pickers_for(lpk::kManager);
        CHECK(!rows.empty(), "the registry has manager rows");
        std::set<std::string> have;
        for (const lpk::LookPicker* p : rows) have.insert(p->column);
        for (const char* must : {"hairtypecode", "facialhairtypecode", "haircolorcode", "facialhaircolorcode", "eyecolorcode", "eyebrowcode", "skintonecode", "skincomplexion",
                                 "bodytypecode", "headassetid", "headtypecode", "outfitid", "seasonaloutfitid", "accessorycode1", "accessorycolourcode4"})
            CHECK(have.count(must) != 0, std::string("pickers_for(manager) holds ") + must);
        for (const std::string& c : lp_manager_picked) {
            const lpk::LookPicker* e = lpk::find(c.c_str());
            CHECK(lp_entries(c) == 1 && e && (e->subjects & lpk::kManager), c + " has exactly one registry entry, and managers have it");
        }
        CHECK(have == picked, "the registry holds exactly the manager columns of the literal list (" + std::to_string(have.size()) + " vs " + std::to_string(picked.size()) + ")");
        // the SAME picker: a column both tables have is one entry (one kind, one route, one band), so the manager's Hair row is the player's Hair row
        for (const std::string& c : lp_manager_picked) {
            if (std::find(lp_player_picked.begin(), lp_player_picked.end(), c) == lp_player_picked.end()) continue;
            const lpk::LookPicker* e = lpk::find(c.c_str());
            CHECK(e && e->subjects == lpk::kBoth, c + " is in both tables: ONE entry shared by players and managers");
        }
        const lpk::LookPicker* hair = lpk::find("hairtypecode");
        const lpk::LookPicker* beard = lpk::find("facialhairtypecode");
        CHECK(hair && hair->kind == lpk::Kind::Band3D && hair->band && beard && beard->kind == lpk::Kind::Band3D && beard->band,
              "hair and beard are ONE entry each with their 3D band (the popup's two sections)");
        for (const LpTyped& t : lp_manager_typed) {
            CHECK(!picked.count(t.column), std::string(t.column) + " is both picked and typed-only (managers)");
            CHECK(t.why && t.why[0], std::string(t.column) + " is typed-only with a reason (managers)");
            const lpk::LookPicker* e = lpk::find(t.column);
            CHECK(!e || !(e->subjects & lpk::kManager), std::string(t.column) + " is typed-only, so it has no manager picker");
        }
        // the registry's own shape: one entry per column, words, a section of the card, a band exactly where the popup has two sections
        std::set<std::string> seen;
        for (const lpk::LookPicker& p : lpk::look_pickers()) {
            CHECK(seen.insert(p.column).second, std::string("no duplicate registry column: ") + p.column);
            CHECK(p.label && p.label[0] && (p.subjects == lpk::kPlayer || p.subjects == lpk::kManager || p.subjects == lpk::kBoth), std::string(p.column) + " has words and a subject");
            bool group_ok = false;
            for (const char* g : lpk::kGroups) group_ok = group_ok || std::strcmp(g, p.group) == 0;
            CHECK(group_ok, std::string(p.column) + " is in a section of the card");
            CHECK((p.kind == lpk::Kind::Band3D) == (p.band != nullptr), std::string(p.column) + ": a band catalogue exactly for the two-section pickers");
            CHECK((p.route == lpk::Route::BodyType) == (p.kind == lpk::Kind::BodyType) && (p.route == lpk::Route::RealFace) == (p.kind == lpk::Kind::RealFace) &&
                      (p.route == lpk::Route::TattooEditor) == (p.kind == lpk::Kind::Tattoo),
                  std::string(p.column) + ": the route fits the kind");
        }
        const nlohmann::json j = lp_schema();
        if (j.is_discarded() || !j.contains("tables")) {
            std::printf("  note: the FC 27 schema is not here: the schema check of the manager lists is skipped\n");
            return;
        }
        for (const std::string& c : picked) CHECK(lp_has(j, "manager", c), "manager." + c + " is in the schema");
        for (const LpTyped& t : lp_manager_typed) CHECK(lp_has(j, "manager", t.column), std::string("manager.") + t.column + " is in the schema");
    });

    run_case("test_look_pickers (b2): no dead button - every picker the card draws opens a list (the fixture's tables) or has a catalogue; the other routes have their opener", [&] {
        struct Opener {
            const char* column;
            const char* file;
            const char* needle;
        };
        // the routes that are not the vp grid: their opener must exist in the source (a renamed function must be updated here)
        const Opener openers[] = {{"headassetid", "ui_faces.cpp", "void real_face_picker("},  {"headassetid", "ui_faces.cpp", "void manager_real_face_picker("},
                                  {"bodytypecode", "ui_bodytypes.cpp", "bool apply_bodytype("},  {"tattoohead", "ui_images.cpp", "void tattoo_editor("}};
        for (const Opener& o : openers) {
            std::ifstream in(lp_ui / o.file, std::ios::binary);
            const std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            CHECK(txt.find(o.needle) != std::string::npos, std::string(o.column) + ": the opener '" + o.needle + "' is in " + o.file);
        }
        int opened = 0, static_only = 0;
        for (lpk::Subject s : {lpk::kPlayer, lpk::kManager}) {
            const bool mgr = s == lpk::kManager;
            const Table* t = app.db.table(mgr ? "manager" : "players");
            const uint64_t rec = t ? app.db.find(*t, mgr ? "managerid" : "playerid", mgr ? 503 : 2002) : 0;
            CHECK(t && rec != 0, mgr ? "the test world has manager 503" : "the test world has player 2002");
            if (!t || !rec) continue;
            for (const lpk::LookPicker* p : lpk::pickers_for(s)) {
                if (p->route != lpk::Route::AppEdit) continue;
                const turbo::looks::data::LookField* lf =
                    turbo::looks::data::find_look_field(mgr ? turbo::looks::data::LookSubject::Manager : turbo::looks::data::LookSubject::Player, p->column);
                const bool catalogue = (lf && lf->n_values > 0) || turbo::visual::set_for_field(p->column) != nullptr || gallery_has(p->column, mgr);
                if (t->has(p->column)) {
                    const std::vector<int64_t> codes = lp_open(*t, rec, p->column, mgr);
                    CHECK(!codes.empty(), std::string(mgr ? "manager " : "player ") + p->column + ": Change opens a list of choices (" + std::to_string(codes.size()) + ")");
                    ++opened;
                } else {
                    CHECK(catalogue, std::string(mgr ? "manager " : "player ") + p->column + ": the fixture table lacks the column, so a static catalogue must exist");
                    ++static_only;
                }
            }
        }
        std::printf("  test_look_pickers: %d pickers opened on fixture rows, %d proven by their static catalogue\n", opened, static_only);
        CHECK(opened > 0, "at least one picker was opened on a fixture row");
    });

    run_case("test_look_pickers (d): hair and beard offer in ONE popup the union of what the three old pickers offered (Gallery list, visual picker list, 3D band); band codes round-trip", [&] {
        const Table* p = app.db.table("players");
        const uint64_t rec = p ? app.db.find(*p, "playerid", 2002) : 0;
        CHECK(p && rec != 0, "the test world has player 2002");
        if (!p || !rec) return;
        for (const char* col : {"hairtypecode", "facialhairtypecode"}) {
            const lpk::LookPicker* e = lpk::find(col);
            CHECK(e && e->band, std::string(col) + " is a registry entry with a 3D band");
            if (!e || !e->band) continue;
            const bool hair = std::strcmp(col, "hairtypecode") == 0;
            const std::vector<int64_t> gallery = gallery_codes(app, col);  // old picker 2: Gallery rows (ui_images.cpp)
            const std::vector<int64_t> vpc = lp_open(*p, rec, col, false);  // old picker 1: visual "Hair and face" (the one catalogue of the field)
            const std::vector<int>& band = e->band();                       // old picker 3: "Hair & beard (3D)" (ui_faces3d_gate.cpp band grid)
            std::printf("  test_look_pickers (d) %s: gallery %zu ids, visual picker %zu ids, 3D band %zu ids\n", col, gallery.size(), vpc.size(), band.size());
            CHECK(!vpc.empty() && !band.empty(), std::string(col) + ": the visual picker and the band list are not empty");
            CHECK(band == (hair ? turbo::faces3d::band_hair_ids() : turbo::faces3d::band_beard_ids()), std::string(col) + ": the band is the 3D gate's list");
            std::set<lpk::TaggedId> want;
            for (int64_t c : gallery) want.insert({lpk::Section::Game, c});
            for (int64_t c : vpc) want.insert({lpk::Section::Game, c});
            for (int c : band) want.insert({lpk::Section::Band3D, c});
            const std::vector<lpk::TaggedId> tagged = lpk::popup_ids(*e, vpc);
            const std::set<lpk::TaggedId> got(tagged.begin(), tagged.end());
            CHECK(got == want, std::string(col) + ": the popup's ids are the union of the three old pickers' ids (" + std::to_string(got.size()) + " vs " + std::to_string(want.size()) + ")");
            const std::set<int64_t> game(vpc.begin(), vpc.end());
            int missing = 0;
            for (int64_t c : gallery) missing += game.count(c) ? 0 : 1;
            CHECK(missing == 0, std::string(col) + ": the Game section holds every code the Gallery rows offered");
            int bad = 0;
            for (int id : band) {
                const int64_t code = lpk::band_code(col, id);
                const int64_t back = hair ? turbo::faces3d::hair_from_code(code) : turbo::faces3d::beard_from_code(code);
                bad += (code == 0 || back != id) ? 1 : 0;
            }
            CHECK(bad == 0, std::string(col) + ": every band id round-trips through its band code (" + std::to_string(bad) + " do not)");
        }
        CHECK(std::strlen(lpk::section_reason(lpk::Section::Band3D, 0)) == 0, "a real face: the 3D section applies");
        CHECK(std::string(lpk::section_reason(lpk::Section::Band3D, 1)) == "3D hair and beard are for real faces. He has a generic head: use the game hairstyles.",
              "a generic head: the 3D section is greyed with today's words (ui_faces3d_gate.cpp)");
        CHECK(std::string(lpk::section_reason(lpk::Section::Band3D, -1)) == "3D hair and beard are for real faces.", "an unknown head: the shorter line");
        CHECK(std::strlen(lpk::section_reason(lpk::Section::Game, 1)) == 0, "the Game section is never greyed by the head");
        CHECK(lpk::render_changes("hairtypecode") && lpk::render_changes("skintonecode") && lpk::render_changes("bodytypecode") && !lpk::render_changes("shoetypecode"),
              "the columns that change the 3D head forget its render, the boots do not");
    });
}
