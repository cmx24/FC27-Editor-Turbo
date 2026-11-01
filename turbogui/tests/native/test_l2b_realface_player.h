// Turbo X 1.0.1 L2b (owner's screenshots 2026-11-01, 08_FABLE_REVIEW_AND_NOON_PLAN.md section 8): on a REAL-FACE PLAYER (headclasscode 0)
// the Appearance Editor showed TWO hair pickers and TWO beard pickers: the faces gate's 3D section (Hair & beard card, hair_card) AND the Look
// card's generic Hair / Beard rows. Now a real-face player behaves like a real-face manager (test_e3_dupes.h): the 3D section owns hair and
// beard type, the Look card draws exactly two rows fewer for him than for a generic head; Hair colour, Beard colour, Eyebrows stay.
// STATEMENTS, not functions: test_main.cpp includes this once inside test_ui(), right after test_e3_dupes.h (app / ui / le in scope).
// No #pragma once. Fails on the base f8ac4cb (band_owned is manager-only: rows_real == rows_generic).
{
    run_case("1.0.1 L2b: look::band_owned covers a real-face player's hair and beard TYPE only; band codes, Bald and No beard unchanged", [&] {
        namespace lpk = turbo::look;
        CHECK(lpk::band_owned(lpk::kPlayer, "hairtypecode", 0) && lpk::band_owned(lpk::kPlayer, "facialhairtypecode", 0),
              "real-face player: the 3D section owns hairtypecode and facialhairtypecode");
        CHECK(!lpk::band_owned(lpk::kPlayer, "hairtypecode", 1) && !lpk::band_owned(lpk::kPlayer, "facialhairtypecode", 1) &&
                  !lpk::band_owned(lpk::kPlayer, "hairtypecode", -1),
              "generic or unknown head: the generic rows stay");
        for (const char* c : {"haircolorcode", "facialhaircolorcode", "eyebrowcode", "hairstylecode", "eyecolorcode", "skintonecode"})
            CHECK(!lpk::band_owned(lpk::kPlayer, c, 0), std::string("real-face player keeps the row: ") + c);
        CHECK(lpk::band_owned(lpk::kManager, "hairtypecode", 0) && !lpk::band_owned(lpk::kManager, "haircolorcode", 0), "manager rule unchanged");
        CHECK(turbo::faces3d::kHairBaldCode == 9998 && turbo::faces3d::kBeardNoneCode == 510, "Bald 9998 / No beard 510 unchanged");
        CHECK(turbo::faces3d::kHairBandBase == 8039 && turbo::faces3d::kBeardBandBase == 335, "band bases 8039 / 335 unchanged");
    });

    run_case("1.0.1 L2b: real-face player's Appearance Editor has ONE hair / beard editor (the faces gate), a generic head keeps the rows", [&] {
        const Table* pt = app.db.table("players");
        const uint64_t rec = pt ? app.db.find(*pt, "playerid", 1001) : 0;
        CHECK(pt && rec != 0 && pt->has("headclasscode"), "the test world has player 1001 with headclasscode");
        const PlayerRow* pr = app.model.player(1001);
        CHECK(pr != nullptr, "the model has player 1001");
        if (!pt || !rec || !pt->has("headclasscode") || !pr) return;
        const int64_t hc0 = app.db.get_int(*pt, rec, "headclasscode", 1);
        const std::string name = pr->name;
        const auto cap0 = app.capture;
        app.capture = nullptr;
        turbo::looks::looks_studio_reset_for_tests();
        turbo::looks::looks_studio_show_managers(false);
        turbo::looks::looks_studio_open_all_fields_for_tests(false);
        app.sel_player = 0;
        page_looks_test::show(ui);
        CHECK(ui.type_into(ui.find("##lsq", "##looksstudio_test"), name), "search box");
        CHECK(ui.click(name + "##lsp1001", "##looksstudio_test"), "picked player 1001 from the list");
        ui.frames(4);
        CHECK(turbo::looks::studio_state().subject.valid() && turbo::looks::studio_state().subject.id == 1001 && !turbo::looks::studio_state().subject.manager,
              "player 1001 is the person shown");

        CHECK(app.db.set_int(*pt, rec, "headclasscode", 0), "player 1001 is a real face (headclasscode 0)");
        ui.frames(4);
        const int rows_real = turbo::looks::studio_state().look_rows;
        CHECK(turbo::looks::studio_state().look_cards == 1, "one Look card is drawn");
        // the faces-gate section is present: the Hair & beard card's fold, and inside it the gate's Apply
        const ItemRec* node = ui.find("###lshairnode", "##looksstudio_test");
        if (!node) node = ui.find_scrolling("###lshairnode", "##looksstudio_test", "");
        CHECK(node != nullptr, "real face: the Hair & beard card (faces gate) is on the page");
        if (node) {
            ui.click(node);
            ui.frames(3);
            CHECK(ui.find("Apply hair & beard", "##looksstudio_test") != nullptr || ui.find_scrolling("Apply hair & beard", "##looksstudio_test", "") != nullptr,
                  "real face: the faces gate's Apply hair & beard is drawn in the Hair & beard card");
            const ItemRec* again = ui.find("###lshairnode", "##looksstudio_test");
            if (!again) again = ui.find_scrolling("###lshairnode", "##looksstudio_test", "");
            if (again) ui.click(again);  // closed again: later cases see the card as a user first does
            ui.frames(2);
        }

        CHECK(app.db.set_int(*pt, rec, "headclasscode", 1), "player 1001 as a generic head, for the row count");
        ui.frames(4);
        const int rows_generic = turbo::looks::studio_state().look_rows;
        CHECK(rows_generic > 0 && rows_real == rows_generic - 2,
              "real-face Look card has exactly the Hair and the Beard row fewer: real " + std::to_string(rows_real) + " vs generic " + std::to_string(rows_generic));

        CHECK(app.db.set_int(*pt, rec, "headclasscode", hc0), "restore headclasscode");
        ui.frames(2);
        page_looks_test::hide(ui);
        app.capture = cap0;
        turbo::looks::looks_studio_reset_for_tests();
    });
}
