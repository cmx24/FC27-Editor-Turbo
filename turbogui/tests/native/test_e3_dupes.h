// 5.0c E3 (D-1, reviews\5.0b-review.md:167-170; brief briefs\5.0c\E3-E4-dupes.md with Verifier V1-V10). STATEMENTS, not functions:
// test_main.cpp includes this once, inside test_ui(), after test_e3_e4_dupes.h (app / ui / le / g_lua in scope). No #pragma once.
//
// On the base tree the manager Appearance tab edited the hair attribute in THREE places: the Look card rows (##mlook), the "Hair & beard
// (3D)" section and the raw "Appearance fields" grid (##mappgrid), both under ##mapp (ui_faces.cpp manager_appearance). After the first
// 5.0c E3 round a real-face manager STILL had two hair pickers and two beard pickers (the Look card's generic Hair / Beard rows plus the
// 3D section, moved into a closed fold). Now a real-face manager (headclasscode 0) has ONE editor per attribute: the 3D section, drawn
// open in the Look card (no fold); the generic Hair / Beard rows (hairtypecode / facialhairtypecode) are not drawn for him
// (look::band_owned); the typed entry stays in "All fields". A generic-head manager keeps the generic rows. The faces3d writer, Apply and
// His own hair & beard are unchanged. The grid's columns stay typed in the card's "All fields" fold.
//   E3a  BEHAVIOURAL: with a real-face manager on the Appearance tab, "Apply hair & beard" is inside ##mlook (no fold to open) and not
//        inside ##mapp; the Look card draws exactly two rows fewer for him than for a generic-head manager (no second hair / beard picker).
//        Fails on the base tree (button in ##mapp) and on the first round (two rows too many).
//   E3b  source: ui_teams.cpp tells manager_appearance the Look card is above; manager_appearance draws neither the 3D section nor the
//        grid when it is; look_card_body has no 3D fold and skips the band-owned rows.
//   E3c  no capability lost: every column of the removed grid is a registry picker or a typed-only column of the Look card's "All fields".
{
    namespace e3fs = std::filesystem;
    const e3fs::path e3_ui = e3fs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui";
    auto e3_slurp = [](const e3fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };

    run_case("5.0c E3a: a real-face manager's Appearance tab edits hair and beard in ONE place, the Look card's 3D section", [&] {
        const int was_tab = shell::player_edit_tab();
        const int was_mtab = shell::manager_edit_tab();
        const int was_sel = app.sel_manager;
        shell::PageFn saved[shell::kPageCount] = {};
        for (int i = 1; i < shell::kPageCount; ++i) saved[i] = shell::registered_page(static_cast<shell::Page>(i));
        shell::clear_registry_for_tests();
        shell::register_page(shell::Page::Home, draw_home);
        shell::register_page(shell::Page::LooksStudio, turbo::looks::draw_looks_studio);
        ui.big_window();
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
            // 2026-10-31 (5.0c menus step 6): the 4.0 tree's manager card is gone; the manager tabs (##mtabs) live on Players > Staff and the
            // Appearance tab is asked for through manager_edit_tab(), as Home's tiles do
            app.go(shell::Page::Home);
            ui.frames(5);
            shell::manager_edit_tab() = 1;
            app.go(shell::Page::Staff);
            ui.frames(6);
            CHECK(ui.find("Choose a real face...", "##medit") != nullptr, "the manager's Appearance tab is open");
            ui.frames(3);
            // the real-face Look card draws two rows fewer than a generic-head one: no second hair / beard picker (the 3D section is the editor)
            auto rows_of_frame = [&]() {
                const int before = turbo::looks::studio_state().look_rows;
                ui.frames(1);
                return turbo::looks::studio_state().look_rows - before;
            };
            const int rows_real = rows_of_frame();
            CHECK(app.db.set_int(*mt, m0.rec, "headclasscode", 1), "manager 0 as a generic head, for the row count");
            ui.frames(2);
            const int rows_generic = rows_of_frame();
            CHECK(app.db.set_int(*mt, m0.rec, "headclasscode", 0), "manager 0 a real face again");
            ui.frames(3);
            CHECK(rows_generic > 0 && rows_real == rows_generic - 2,
                  "real-face Look card has exactly the hair and the beard row fewer: real " + std::to_string(rows_real) + " vs generic " + std::to_string(rows_generic));
            // ##mlook is half the pane's height: the button may sit below its visible part (a user scrolls to it)
            CHECK(ui.find("Apply hair & beard", "##mlook") != nullptr || ui.find_scrolling("Apply hair & beard", "##mlook", "") != nullptr,
                  "Apply hair & beard is in the Look card (open, no fold)");
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
        shell::player_edit_tab() = was_tab;
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
        const std::string studio = e3_slurp(e3_ui / "ui_looks_studio.cpp");
        const size_t lc = studio.find("void look_card_body(");
        CHECK(!studio.empty() && lc != std::string::npos, "read ui_looks_studio.cpp look_card_body");
        // the span from the card's start to the registry rows: the 3D section is there, open (no fold), and look_rows is told to skip the band-owned rows
        const size_t s0 = lc == std::string::npos ? std::string::npos : studio.find("card_begin(", lc);
        const size_t s1 = s0 == std::string::npos ? std::string::npos : studio.find("vp::look_rows(", s0);
        const std::string span = s1 == std::string::npos ? std::string() : studio.substr(s0, s1 - s0);
        CHECK(!span.empty(), "found the span between card_begin and vp::look_rows in look_card_body");
        CHECK(span.find("faces3d_manager_section(") != std::string::npos, "the Look card body draws the 3D section before the rows");
        CHECK(span.find("TreeNode") == std::string::npos && span.find("CollapsingHeader") == std::string::npos, "the 3D section is not behind a fold in the Look card body");
        CHECK(s1 != std::string::npos && studio.find(", band)", s1) == s1 + std::string("vp::look_rows(app, t, rec, who.manager").size(), "look_rows is told to skip the band-owned rows (', band)')");
        CHECK(body.find("vp::rows(") != std::string::npos && body.find("item_galleries(") == std::string::npos, "step 7: the Outfit rows are the one picker's (vp::rows), item_galleries is no longer drawn here");
        CHECK(body.find("Choose a real face...") != std::string::npos, "the real-face chooser button stays");
    });

    // RR11 (trial 2): the Looks studio page's hair_card returns at once for a manager, so it holds no manager branch (dead code on the base: the
    // base tree drew faces3d_manager_section there, the second editor of F5/R4). Fails on d197d851 and on 256a7da2; passes after the removal.
    run_case("5.0c E3d: the Looks page hair_card has no manager branch (the Look card carries the manager's 3D section)", [&] {
        const std::string studio = e3_slurp(e3_ui / "ui_looks_studio.cpp");
        const size_t fn = studio.find("void hair_card(");
        CHECK(!studio.empty() && fn != std::string::npos, "hair_card is defined in ui_looks_studio.cpp");
        const size_t fn_end = fn == std::string::npos ? std::string::npos : studio.find("\n}", fn);
        const std::string body = fn_end == std::string::npos ? std::string() : studio.substr(fn, fn_end - fn);
        CHECK(!body.empty(), "found the body of hair_card");
        CHECK(body.find("if (who.manager) return;") != std::string::npos, "hair_card returns at once for a manager");
        CHECK(body.find("faces3d_manager_section(") == std::string::npos, "hair_card does not draw the manager 3D section (dead branch removed; the Look card draws it)");
        CHECK(body.find("sel_manager") == std::string::npos, "hair_card has no manager lookup left");
        CHECK(body.find("faces3d_gate_section(") != std::string::npos, "the player branch is untouched");
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
