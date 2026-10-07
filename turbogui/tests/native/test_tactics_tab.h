// Native tests: Teams > Tactics (ui/ui_tactics.cpp, ui/pitch_view.cpp, Turbo 2.0). Included by test_main.cpp (after FakeMatchSetup),
// run from test_ui; the cases drive the real panel through the mouse and read what it drew from tactics_tab_state().
#pragma once
#include "core/sliders.h"
#include "ui/pitch_view.h"
#include "ui/ui_tactics.h"

static void ui_cases_tactics_tab(App& app, Ui& ui, const fs::path& le) {
    // pure checks of the honesty helpers (no UI)
    run_case("pitch view: unit strings are recognised, the derived label never carries one", [&] {
        CHECK(pitch_label_has_unit("19 m"), "19 m");
        CHECK(pitch_label_has_unit("48m deep"), "48m");
        CHECK(pitch_label_has_unit("about 20 metres"), "metres");
        CHECK(pitch_label_has_unit("30 yd"), "yd");
        CHECK(pitch_label_has_unit("2 km"), "km");
        CHECK(!pitch_label_has_unit("62/100 (schematic)"), "62/100 (schematic)");
        CHECK(!pitch_label_has_unit("relative intensity (arbitrary)"), "relative intensity");
        CHECK(!pitch_label_has_unit("Defensive line 7/100 (schematic)"), "defensive line");
        PitchLayer l;
        l.value = 62;
        l.label = "Defensive line";
        CHECK(pitch_derived_label(l) == "Defensive line 62/100 (schematic)", "label: " + pitch_derived_label(l));
        CHECK(std::string(pitch_chip_text()) == "Model view: Turbo's estimate, not game output", "the chip text");
    });

    run_case("registry: every slider has a status, only Live and DB are ever written, Preview and RE are not", [&] {
        CHECK(validate_registry().empty(), "registry validates: " + validate_registry());
        for (const SliderDef& d : slider_registry()) {
            const bool game = d.status == SliderStatus::Live || d.status == SliderStatus::DB;
            CHECK(writes_game(d) == game, "writes_game agrees with the status: " + d.key);
            CHECK(preview_only(d) == (d.status == SliderStatus::Preview || d.status == SliderStatus::RE), "preview_only: " + d.key);
        }
        SliderSet s;
        for (const SliderDef& d : slider_registry()) s.set(d.key, d.def, true);  // everything switched on
        for (const SliderWrite& w : game_writes(s)) {
            const SliderDef* d = find_slider(w.key);
            CHECK(d && writes_game(*d), "a write is only listed for a Live / DB slider: " + w.key);
        }
    });

    run_case("UI: Teams > Tactics: Match scope, a Live slider, Apply sends the game variable, preview-only rows never", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        std::error_code ec;
        fs::remove(le / "turbo_output" / "tactics_off.txt", ec);
        app.request_tab = 1;
        ui.frames(3);
        CHECK(ui.click("7", "##tlist"), "Everton row");
        CHECK(app.sel_team == 7, "team selected");
        CHECK(ui.click("Tactics", "##tedit"), "Tactics tab");
        ui.frames(3);
        const TacticsTabState& st = tactics_tab_state();
        CHECK(st.error.empty(), "no inline error: " + st.error);
        CHECK(st.scope == 0, "Match scope first");
        CHECK(!st.rows.empty(), "slider rows drawn");
        CHECK(st.chip == "Model view: Turbo's estimate, not game output", "the honesty chip: " + st.chip);
        CHECK(st.receive_title == "What the game will receive", "the receive panel");
        CHECK(st.receive.empty(), "nothing switched on: nothing to receive");
        CHECK(st.preview_strip.find("preview-only") != std::string::npos, "preview-only strip: " + st.preview_strip);
        // every drawn row: status registry, tag text, dimming of the preview-only rows
        bool saw_live = false, saw_preview = false;
        for (const auto& r : st.rows) {
            const SliderDef* d = find_slider(r.key);
            CHECK(d != nullptr, "row of a known slider: " + r.key);
            if (!d) continue;
            CHECK(r.status == status_name(d->status), "badge = status: " + r.key);
            CHECK(!r.enabled, "default is the game decides: " + r.key);
            if (preview_only(*d)) {
                saw_preview = true;
                CHECK(r.dimmed && r.tag == "not applied to game", "preview-only row dimmed and tagged: " + r.key);
            }
            if (d->status == SliderStatus::Live) saw_live = true;
            if (d->status == SliderStatus::DB) CHECK(r.tag == "saved data, in-game effect unverified", "DB tag: " + r.key);
        }
        CHECK(saw_live && saw_preview, "both a Live and a preview-only row in the Match scope");
        // move a slider by typing: injury frequency (your team)
        const ItemRec* typed = ui.find("##typed", "##tacsliders", "match.injury_frequency_user");
        if (!typed) ui.dump("typed box of injury frequency");
        CHECK(typed != nullptr, "typed value box");
        CHECK(ui.type_into(typed, "25"), "type 25");
        ui.frames(2);
        bool found = false;
        for (const auto& r : tactics_tab_state().rows)
            if (r.key == "match.injury_frequency_user") found = r.enabled && r.value == 25;
        CHECK(found, "the slider is on and holds 25");
        CHECK(tactics_tab_state().changed == 1, fmt("1 changed, got %d", tactics_tab_state().changed));
        bool listed = false;
        for (const auto& l : tactics_tab_state().receive)
            listed = listed || l == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER = 25  [Live]";
        CHECK(listed, "the write is listed in What the game will receive");
        CHECK(tactics_tab_state().receive.size() == 1, "only the Live write is listed");
        // a preview-only value (RE row) can be moved but never reaches the receive list
        const ItemRec* re_typed = ui.find("##typed", "##tacsliders", "match.pass_error");
        if (re_typed) {
            CHECK(ui.type_into(re_typed, "40"), "type into a preview-only row");
            ui.frames(2);
            CHECK(tactics_tab_state().receive.size() == 1, "an RE slider is never in the receive list");
        }
        CHECK(fake->sets.empty(), "nothing is written before Apply");
        // dry run: shows, writes nothing
        CHECK(ui.click("Dry run##tacdry"), "dry run on");
        CHECK(ui.click("Apply##tac"), "Apply (dry run)");
        ui.frames(2);
        CHECK(fake->sets.empty(), "dry run sends nothing");
        CHECK(ui.toast_contains("dry run"), "dry-run toast");
        CHECK(ui.click("Dry run##tacdry"), "dry run off");
        CHECK(ui.click("Apply##tac"), "Apply");
        ui.frames(2);
        CHECK(fake->sets.size() == 1 && fake->sets[0].first == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER" && fake->sets[0].second == 25,
              "the game variable is set to 25");
        CHECK(ui.toast_contains("Tactics: 1 write(s) sent to the game"), "toast after Apply");
        CHECK(tactics_tab_state().changed == 0, "nothing changed after Apply");
        // the overrides banner follows the host's active variables
        msetup::VarResult r;
        r.ok = true;
        r.name = "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER";
        r.value = 25;
        fake->results.push_back(r);
        ui.frames(3);
        CHECK(tactics_tab_state().banner.find("Game overrides are active (1)") != std::string::npos, "overrides banner: " + tactics_tab_state().banner);
        // leaving: the variables this panel set are cleared
        fake->clears.clear();
        tactics_clear_on_exit(app);
        CHECK(fake->clears.size() == 1 && fake->clears[0] == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", "auto-clear on exit");
        app.match_setup = nullptr;
    });

    run_case("UI: Teams > Tactics: tactics_off.txt blocks every write and greys the effect layers", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        app.request_tab = 1;
        ui.frames(2);
        CHECK(ui.click("7", "##tlist"), "Everton row");
        CHECK(ui.click("Tactics", "##tedit"), "Tactics tab");
        ui.frames(2);
        const ItemRec* typed = ui.find("##typed", "##tacsliders", "match.injury_severity_user");
        CHECK(typed != nullptr, "typed box");
        CHECK(ui.type_into(typed, "60"), "type 60");
        fs::create_directories(le / "turbo_output");
        {
            std::ofstream f((le / "turbo_output" / "tactics_off.txt").string(), std::ios::binary);
            f << "off";
        }
        CHECK(ui.click("Apply##tac"), "Apply with the switch present");
        ui.frames(2);
        CHECK(fake->sets.empty(), "no write when tactics_off.txt exists");
        bool err_toast = false;
        for (const auto& tt : app.toasts) err_toast = err_toast || (tt.error && tt.text.find("tactics_off.txt") != std::string::npos);
        CHECK(err_toast, "an error toast names the switch");
        ui.frames(130);  // the panel looks at the switch every 2 s
        CHECK(tactics_tab_state().off && !tactics_tab_state().off_line.empty(), "the panel shows the switch");
        // the effect layers turn grey: a derived slider on the My team scope
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        const ItemRec* depth = ui.find("##typed", "##tacsliders", "team.line_depth");
        if (depth) {
            CHECK(ui.type_into(depth, "62"), "type the defensive line");
            ui.frames(2);
        }
        fs::remove(le / "turbo_output" / "tactics_off.txt");
        ui.frames(130);
        CHECK(!tactics_tab_state().off, "the switch is gone: the panel writes again");
        CHECK(ui.click("Apply##tac"), "Apply without the switch");
        ui.frames(2);
        bool severity = false;  // the earlier case's frequency (25) is still on and is sent again: the writes are idempotent
        for (const auto& kv : fake->sets) severity = severity || (kv.first == "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER" && kv.second == 60);
        CHECK(severity && !fake->sets.empty(), "the held write goes through once the switch is removed");
        app.match_setup = nullptr;
    });

    run_case("UI: Teams > Tactics: the pitch (105:68, capped, dashed schematic labels without unit strings)", [&] {
        app.request_tab = 1;
        ui.frames(2);
        CHECK(ui.click("7", "##tlist"), "Everton row");
        CHECK(ui.click("Tactics", "##tedit"), "Tactics tab");
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        for (const char* key : {"team.line_depth", "team.engagement_height", "team.width_def"}) {
            const ItemRec* typed = ui.find("##typed", "##tacsliders", key);
            if (!typed) {
                ui.dump(std::string("typed box of ") + key);
                continue;
            }
            CHECK(ui.type_into(typed, "70"), std::string("type into ") + key);
        }
        ui.frames(3);
        const PitchViewResult& pv = pitch_view_last();
        CHECK(pv.primitives > 0 && pv.primitives <= kPitchPrimitiveCap, fmt("primitives within the cap: %d", pv.primitives));
        CHECK(!pv.capped, "the cap was not hit");
        CHECK(std::fabs(pv.height / pv.width - 68.0f / 105.0f) < 0.001f, "105:68 aspect");
        CHECK(!pv.labels.empty(), "derived labels drawn for the sliders that are on");
        for (const std::string& l : pv.labels) {
            CHECK(!pitch_label_has_unit(l), "no unit string on a derived or modelled label: " + l);
            CHECK(l.find("/100 (schematic)") != std::string::npos || l == "relative intensity (arbitrary)", "label wording: " + l);
        }
        CHECK(pv.exact_labels.size() == 11, fmt("eleven formation dots with their position names: %zu", pv.exact_labels.size()));
        // the modelled layer is off by default; switching it on adds the hatched grid, still under the cap and without units
        const int before = pv.primitives;
        CHECK(ui.click("Modelled layer (hatched, off by default)##tach"), "Modelled layer on");
        ui.frames(3);
        const PitchViewResult& pv2 = pitch_view_last();
        CHECK(pv2.primitives > before && pv2.primitives <= kPitchPrimitiveCap, fmt("modelled layer adds primitives: %d -> %d", before, pv2.primitives));
        for (const std::string& l : pv2.labels) CHECK(!pitch_label_has_unit(l), "no unit on the modelled label: " + l);
        CHECK(ui.click("Modelled layer (hatched, off by default)##tach"), "Modelled layer off again");
        // a Position/Role DB write that this save's schema cannot take is refused, never silent
        CHECK(ui.click("Position/Role##scope"), "Position scope");
        ui.frames(2);
        app.sel_player = 2001;
        ui.frames(2);
        for (const auto& r : tactics_tab_state().rows)
            if (r.status == "DB") CHECK(r.tag == "saved data, in-game effect unverified", "DB tag on " + r.key);
        CHECK(ui.click("Match (both teams)##scope"), "back to the Match scope");
    });
}
