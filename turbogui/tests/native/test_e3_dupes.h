// 5.0c E3 (D-1, reviews\5.0b-review.md:167-170; brief briefs\5.0c\E3-E4-dupes.md with Verifier V1-V10). STATEMENTS, not functions:
// test_main.cpp includes this once, inside test_ui(), after test_e3_e4_dupes.h (app / ui / le / g_lua in scope). No #pragma once.
//
// On the base tree the manager Appearance tab edited the hair attribute in THREE places: the Look card rows (##mlook), the "Hair & beard
// (3D)" section and the raw "Appearance fields" grid (##mappgrid), both under ##mapp (ui_faces.cpp manager_appearance). The new tree
// has ONE editor surface, the Look card: its rows (the game's hairstyles, any head) and a closed fold "3D hair and beard" (real faces
// only; the faces3d writer, Apply and His own hair & beard are unchanged). The grid's columns stay typed in the card's "All fields" fold.
//   E3a  BEHAVIOURAL: with a real-face manager on the Appearance tab, "Apply hair & beard" is inside ##mlook (after opening the fold)
//        and not inside ##mapp. Fails on the base tree (the button was in ##mapp, ##mlook had no 3D fold).
//   E3b  source: ui_teams.cpp tells manager_appearance the Look card is above; manager_appearance draws neither the 3D section nor the
//        grid when it is; the grid literal stays inside the guard (test_one_picker.h holds it as a raw-entry site).
//   E3c  no capability lost: every column of the removed grid is a registry picker or a typed-only column of the Look card's "All fields".
{
    namespace e3fs = std::filesystem;
    const e3fs::path e3_ui = e3fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui";
    auto e3_slurp = [](const e3fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };

    run_case("5.0c E3a: a real-face manager's Appearance tab edits hair in ONE place, the Look card's 3D fold", [&] {
        const bool was_tree = shell::tree40();
        const bool was_legacy = app.legacy_pages;
        const bool was_adv = app.prefs.advanced_mode;
        const int was_tab = shell::player_edit_tab();
        const int was_who = shell::player_edit_who();
        const int was_mtab = shell::manager_edit_tab();
        const int was_sel = app.sel_manager;
        shell::PageFn saved[shell::kPageCount] = {};
        for (int i = 1; i < shell::kPageCount; ++i) saved[i] = shell::registered_page(static_cast<shell::Page>(i));
        shell::clear_registry_for_tests();
        shell::register_page(shell::Page::Home, draw_home);
        shell::register_page(shell::Page::LooksStudio, turbo::looks::draw_looks_studio);
        app.legacy_pages = false;
        app.prefs.advanced_mode = false;
        ui.big_window();
        shell::tree40() = true;
        const Table* mt = app.db.table("manager");
        CHECK(mt && mt->has("headclasscode"), "the test world has the manager table with headclasscode");
        if (mt && mt->has("headclasscode") && !app.model.managers().empty()) {
            app.sel_manager = 0;
            const ManagerRow& m0 = app.model.managers()[0];
            const int64_t hc0 = app.db.get_int(*mt, m0.rec, "headclasscode", 0);
            CHECK(app.db.set_int(*mt, m0.rec, "headclasscode", 0), "manager 0 is a real face (headclasscode 0)");
            lualoader::Faces3dStatus st;
            st.opted_in = st.installed = st.substitute = st.in_memory = st.built = true;
            st.header_ok = st.build_key_ok = st.stock_matches_header = st.live_looks = true;
            st.substituted = 18;
            st.running_build = "6AC07E31-2145C000";
            app.faces3d_report = [st]() { return st; };
            shell::player_edit_tab() = 0;
            shell::player_edit_who() = 0;
            shell::manager_edit_tab() = 0;
            app.go(shell::Page::Home);
            ui.frames(5);
            CHECK(ui.click("###home_manager_looks"), "click Home's Manager looks tile");
            ui.frames(6);
            CHECK(ui.find("Choose a real face...", "##medit") != nullptr, "the manager's Appearance tab is open");
            // the fold is closed until opened (the faces card reads a file every frame it is drawn): open it
            CHECK(ui.find("###look3dnode", "##mlook") != nullptr, "the Look card has the 3D hair and beard fold");
            CHECK(ui.click("###look3dnode", "##mlook"), "open the 3D fold");
            ui.frames(3);
            // ##mlook is half the pane's height: the button may sit below its visible part (a user scrolls to it)
            CHECK(ui.find("Apply hair & beard", "##mlook") != nullptr || ui.find_scrolling("Apply hair & beard", "##mlook", "") != nullptr,
                  "Apply hair & beard is in the Look card");
            CHECK(ui.find("Apply hair & beard", "##mapp") == nullptr, "the real-face pane (##mapp) has no second Apply hair & beard");
            CHECK(ui.find("His own hair & beard", "##mapp") == nullptr, "the real-face pane has no second His own hair & beard");
            CHECK(app.db.set_int(*mt, m0.rec, "headclasscode", hc0), "restore headclasscode");
            app.faces3d_report = nullptr;
            ui.click("Details", "##medit");
            ui.frames(3);
        }
        shell::clear_registry_for_tests();
        for (int i = 1; i < shell::kPageCount; ++i)
            if (saved[i]) shell::register_page(static_cast<shell::Page>(i), saved[i]);
        shell::tree40() = was_tree;
        app.legacy_pages = was_legacy;
        app.prefs.advanced_mode = was_adv;
        shell::player_edit_tab() = was_tab;
        shell::player_edit_who() = was_who;
        shell::manager_edit_tab() = was_mtab;
        app.sel_manager = was_sel;
        turbo::looks::looks_studio_show_managers(false);
        app.go(shell::Page::Home);
        ui.frames(3);
    });

    run_case("5.0c E3b: manager_appearance draws neither the 3D section nor the raw grid when the Look card is above it", [&] {
        const std::string teams = e3_slurp(e3_ui / "ui_teams.cpp");
        const std::string faces = e3_slurp(e3_ui / "ui_faces.cpp");
        CHECK(!teams.empty() && !faces.empty(), "read ui_teams.cpp and ui_faces.cpp");
        CHECK(teams.find("manager_appearance(app, *t, m, true)") != std::string::npos, "ui_teams.cpp tells manager_appearance the Look card is above");
        const size_t fn = faces.find("void manager_appearance(");
        CHECK(fn != std::string::npos, "manager_appearance is defined");
        const size_t fn_end = faces.find("\n}", fn == std::string::npos ? 0 : fn);  // the sources are CRLF: the first closing brace at column 0
        const std::string body = fn == std::string::npos || fn_end == std::string::npos ? std::string() : faces.substr(fn, fn_end - fn);
        const size_t guard = body.find("if (!look_card_above)");
        CHECK(guard != std::string::npos, "the function has a look_card_above guard");
        const size_t sec = body.find("faces3d_manager_section(");
        const size_t grid = body.find("field_grid(");
        const size_t lit = body.find("\"##mappgrid\"");
        CHECK(guard != std::string::npos && sec != std::string::npos && sec > guard, "the 3D section is inside the guard");
        CHECK(guard != std::string::npos && grid != std::string::npos && grid > guard, "the raw grid is inside the guard");
        CHECK(guard != std::string::npos && lit != std::string::npos && lit > guard, "the grid's id literal stays inside the guard (test_one_picker.h holds it)");
        CHECK(body.find("item_galleries(") != std::string::npos, "the Outfit galleries stay (their pictures are the only outfit picker of this pane)");
        CHECK(body.find("Choose a real face...") != std::string::npos, "the real-face chooser button stays");
    });

    run_case("5.0c E3c: every column of the removed manager grid is still reachable in the Look card (a row or the All fields fold)", [&] {
        const char* const grid[] = {"headassetid", "headclasscode", "hashighqualityhead", "headtypecode", "headvariation", "skintonecode", "skintypecode",
                                    "skincomplexion", "haircolorcode", "hairtypecode", "hairstylecode", "facialhairtypecode", "facialhaircolorcode",
                                    "eyecolorcode", "eyebrowcode", "bodytypecode", "height", "weight", "outfitid"};
        for (const char* c : grid) {
            bool found = false;
            for (const turbo::look::LookPicker* p : turbo::look::pickers_for(turbo::look::kManager))
                if (std::string(p->column) == c) found = true;
            for (const char* tc : turbo::look::typed_only(turbo::look::kManager))
                if (std::string(tc) == c) found = true;
            CHECK(found, std::string("grid column still in the Look card: ") + c);
        }
    });
}
