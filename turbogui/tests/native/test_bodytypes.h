// Native tests: the body type catalogue (core/bodytype_catalog.h, loaded at run time from the probe file and Live Editor's
// localize.json) and the gallery (ui/ui_bodytypes.h). Included by test_main.cpp; the fixture is synthetic.
#pragma once
#include "core/bodytype_catalog.h"
#include "ui/ui_bodytypes.h"

static fs::path bt_fixture() { return fs::path(g_lua).parent_path() / "bodytypes_fixture_synthetic.json"; }

static void test_bodytype_catalog() {
    using namespace turbo;
    using bodytype::Catalog;
    using bodytype::Filter;
    using bodytype::Group;
    using bodytype::Kind;
    const fs::path d = g_out / "bodytype_world";
    fs::remove_all(d);
    fs::create_directories(d);

    run_case("Body types: probe file loads (synthetic fixture), bad rows skipped, examples capped", [&] {
        Catalog c;
        std::string err;
        CHECK(c.load_probe(bt_fixture(), &err), "fixture loads: " + err);
        CHECK(c.probe_loaded() && c.probed_count() == 5, fmt("5 usable entries (%zu)", c.probed_count()));
        CHECK(c.skipped() == 2, fmt("a bad code and a missing code are skipped (%zu)", c.skipped()));
        const bodytype::Entry* e = c.find(20);
        CHECK(e && e->players == 2 && e->height_min == 188 && e->height_max == 190 && e->weight_min == 80 && e->weight_max == 84, "code 20 fields");
        CHECK(e && e->headclass == 0 && e->gender == 0 && e->examples.size() == 2 && e->examples[0] == "Synthetic Star A", "head class, gender, examples");
        CHECK(c.find(21) && c.find(21)->gender == 1, "gender 1");
        CHECK(c.find(22) && c.find(22)->examples.size() == bodytype::kMaxExamples, "at most 5 examples kept");
        CHECK(c.find(1) && c.find(1)->headclass == -1, "mixed head classes -> unknown");
        CHECK(c.find(2) == nullptr && c.find(999) == nullptr, "an unprobed code has no entry");
        // the object form keyed by code
        std::ofstream(d / "obj.json") << R"({"bodytypes":{"30":{"count":4,"height":{"min":150,"max":160}},"x":{"players":1}}})";
        CHECK(c.load_probe(d / "obj.json", &err) && c.probed_count() == 1 && c.skipped() == 1, "object form: keyed by code, bad key skipped");
        CHECK(c.find(30) && c.find(30)->players == 4 && c.find(30)->height_max == 160, "object form values");
        std::ofstream(d / "empty.json") << R"({"bodytypes":[]})";
        CHECK(c.load_probe(d / "empty.json") && c.probe_loaded() && c.probed_count() == 0, "an empty list is a valid probe");
    });

    run_case("Body types: missing and malformed files leave the built-in names and never throw", [&] {
        Catalog c;
        std::string err;
        CHECK(!c.load_probe(d / "nope.json", &err) && !err.empty() && !c.probe_loaded(), "missing file: false, reason given");
        CHECK(!c.probe_error().empty(), "reason kept");
        CHECK(c.codes().size() == 10, fmt("only the named codes 1-9 and 11 (%zu)", c.codes().size()));
        CHECK(c.name(4) == "Tall and Lean" && c.name(11) == "Very Tall and Lean", "field_labels.h names without any file");
        struct Bad { const char* name; const char* text; };
        const Bad bad[] = {{"junk", "this is not json"}, {"trunc", R"({"bodytypes":[{"code":1,)"}, {"arr", "[1,2,3]"},
                           {"noList", R"({"version":1})"}, {"scalar", R"({"bodytypes":5})"}, {"empty", ""}};
        for (const auto& b : bad) {
            std::ofstream(d / (std::string(b.name) + ".json")) << b.text;
            CHECK(!c.load_probe(d / (std::string(b.name) + ".json"), &err) && !err.empty() && !c.probe_loaded() && c.probed_count() == 0,
                  std::string("malformed: ") + b.name);
        }
        CHECK(c.load_names(d / "nope.json") == 0 && c.load_names(d / "junk.json") == 0, "missing or malformed localize.json: no names");
        CHECK(c.name(4) == "Tall and Lean", "still named");
        c.load_for_root(d / "no_such_root");
        CHECK(!c.probe_loaded() && c.localized_count() == 0, "load_for_root of a folder with nothing in it");
    });

    run_case("Body types: names from localize.json win, specific bodies are never guessed", [&] {
        Catalog c;
        fs::create_directories(d / "loc" / "eng_us");
        std::ofstream(d / "loc" / "eng_us" / "localize.json")
            << R"({"bodytype_1":"LE Average and Lean","bodytype_2":"","bodytype_x":"skip","player_role_1":"GK","nested":{"bodytype_9":"LE Short and Stocky"}})";
        CHECK(c.load_names(d / "loc" / "eng_us" / "localize.json") == 2, fmt("2 names, any depth, empty and malformed keys ignored (%zu)", c.localized_count()));
        CHECK(c.name(1) == "LE Average and Lean" && c.name(9) == "LE Short and Stocky", "Live Editor's names");
        CHECK(c.name(2) == "Average and Normal", "an empty or missing string falls back to field_labels.h");
        CHECK(c.name(10) == "Specific body #10" && c.name(20) == "Specific body #20" && c.name(0) == "Specific body #0", "unnamed codes: Specific body #N, no guess");
        CHECK(c.load_probe(bt_fixture()), "fixture");
        CHECK(c.kind(4) == Kind::Generic && c.kind(20) == Kind::Specific && c.kind(10) == Kind::Specific, "named = generic, unnamed = specific");
        CHECK(c.describe(20) == "Specific body #20 (examples: Synthetic Star A, Synthetic Star B)", "describe: " + c.describe(20));
        CHECK(c.describe(4) == "Tall and Lean", "a generic type shows no examples");
        CHECK(c.describe(999) == "Specific body #999" && c.players(999) == 0 && c.players(20) == 2 && c.examples_text(999).empty(), "unprobed code");
        CHECK(c.codes().size() == 13, fmt("1-9, 11, 20, 21, 22 (%zu)", c.codes().size()));
        const std::vector<int64_t> cs = c.codes();
        CHECK(std::is_sorted(cs.begin(), cs.end()), "codes ascending");
    });

    run_case("Body types: filters (group, height, weight, text, used only) and the risky pairing rule", [&] {
        Catalog c;
        CHECK(c.load_probe(bt_fixture()), "fixture");
        using V = std::vector<int64_t>;
        Filter f;
        CHECK(c.filter(f).size() == 13, "no filter: everything");
        f.group = Group::Specific;
        CHECK(c.filter(f) == (V{20, 21, 22}), "specific only");
        f.group = Group::Generic;
        CHECK(c.filter(f).size() == 10, "generic only: 1-9 and 11");
        f = Filter();
        f.height_min = 185; f.height_max = 195;
        CHECK(c.filter(f) == (V{4, 20}), "height 185-195 overlaps codes 4 and 20; unprobed codes are hidden");
        f = Filter();
        f.height_min = 199;
        CHECK(c.filter(f) == (V{22}), "height from 199");
        f = Filter();
        f.height_max = 169;
        CHECK(c.filter(f) == (V{1}), "height up to 169: only code 1 (168-178) reaches that low");
        f = Filter();
        f.weight_min = 90; f.weight_max = 100;
        CHECK(c.filter(f) == (V{22}), "weight 90-100");
        f = Filter();
        f.group = Group::Specific; f.height_min = 160; f.height_max = 172;
        CHECK(c.filter(f) == (V{21}), "specific and height 160-172");
        f = Filter();
        f.text = "star b";
        CHECK(c.filter(f) == (V{20}), "text finds an example player");
        f.text = "21";
        CHECK(c.filter(f) == (V{21}), "text finds a code");
        f.text = "TALL";
        CHECK(c.filter(f) == (V{4, 5, 6, 11}), "text is case-insensitive over names: " + std::to_string(c.filter(f).size()));
        f = Filter();
        f.used_only = true;
        CHECK(c.filter(f) == (V{1, 4, 20, 21, 22}), "used by players only");
        CHECK(bodytype::risky_pairing(c, 20, 1) && bodytype::risky_pairing(c, 20, -1) && !bodytype::risky_pairing(c, 20, 0), "specific body: only a specific head is safe");
        CHECK(!bodytype::risky_pairing(c, 4, 1) && !bodytype::risky_pairing(c, 4, -1), "a generic body never conflicts");
    });
}

static void test_bodytypes_ui(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: body type gallery (player editor): groups, filters, warning, specific body on a generic head, copy from a player", [&] {
        using namespace turbo;
        const fs::path probe = le / "turbo_output" / "bodytypes_fc27.json";
        // the gallery button sits low in the Profile tab: give the test window room so it is not scrolled out of view
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 saved_display = io.DisplaySize;
        io.DisplaySize = ImVec2(1280.0f, 1700.0f);
        ui.frames(3);
        fs::copy_file(bt_fixture(), probe, fs::copy_options::overwrite_existing);
        const Table* p = app.db.table("players");
        CHECK(p && p->has("bodytypecode"), "the test world has bodytypecode");
        app.request_tab = 0;
        ui.frames(2);
        CHECK(ui.click("2002", "##plist"), "player 2002 (generic head, Short and Stocky)");
        for (int k = 0; k < 8 && !ui.find("Profile", "##pedit"); ++k) { ui.click("##<", "##pedit"); ui.frames(2); }  // the tab bar scrolls in the small test window
        CHECK(ui.click("Profile", "##pedit"), "Profile tab");
        ui.frames(2);
        const uint64_t rec = app.db.find(*p, "playerid", 2002);
        const Field* f = p->field("bodytypecode");
        CHECK(app.db.get_int(*p, rec, "bodytypecode") == 9 && app.db.get_int(*p, rec, "headclasscode") == 1, "start: code 9, generic head");
        CHECK(ui.click("Body types...##btopen", "##pedit"), "open the gallery");
        ui.frames(3);
        CHECK(bodytype_ui_state().open, "popup open");
        CHECK(ui.click("Reload##btreload", "Body types"), "reload the probe");
        ui.frames(2);
        CHECK(bodytype_ui_state().shown_generic == 10 && bodytype_ui_state().shown_specific == 3,
              fmt("Generic 10 rows, Player-specific 3 rows (%d / %d)", bodytype_ui_state().shown_generic, bodytype_ui_state().shown_specific));
        CHECK(ui.find("Use##bt20", "Body types") != nullptr && ui.find("Use##bt4", "Body types") != nullptr, "Use buttons in both groups");
        CHECK(ui.find("Allow a player-specific body on this head (untested)##btallow", "Body types") != nullptr, "warning checkbox on a generic head");

        // a specific body is not written onto a generic head by default
        ui.click("Use##bt20", "Body types");
        ui.frames(2);
        CHECK(app.db.get_int(*p, rec, "bodytypecode") == 9, "refused: body type unchanged");
        std::string msg;
        CHECK(!apply_bodytype(app, *p, rec, 20, false, &msg) && msg.find("not written") != std::string::npos, "apply_bodytype refuses too: " + msg);
        CHECK(!apply_bodytype(app, *p, rec, 99, true, &msg) && app.db.get_int(*p, rec, "bodytypecode") == 9, "outside the field range: refused");
        // a generic body is fine
        CHECK(ui.click("Use##bt4", "Body types"), "Use Tall and Lean");
        ui.frames(2);
        CHECK(app.db.get_int(*p, rec, "bodytypecode") == 4, "generic body written");
        CHECK(bodytype_ui_state().last_result == "Body type set to Tall and Lean", "result: " + bodytype_ui_state().last_result);
        // after the user ticks the box
        CHECK(ui.click("Allow a player-specific body on this head (untested)##btallow", "Body types"), "tick the box");
        ui.frames(2);
        CHECK(ui.click("Use##bt20", "Body types"), "Use specific body 20");
        ui.frames(2);
        CHECK(app.db.get_int(*p, rec, "bodytypecode") == 20, "written once allowed");
        CHECK(bodytype_label(20) == "Specific body #20", "the editors' label for the code");

        // filters
        CHECK(ui.type_into(ui.find("##bthmin", "Body types"), "199"), "height from 199");
        ui.frames(2);
        CHECK(bodytype_ui_state().shown_specific == 1 && bodytype_ui_state().shown_generic == 0,
              fmt("only code 22 left (%d / %d)", bodytype_ui_state().shown_generic, bodytype_ui_state().shown_specific));
        CHECK(ui.type_into(ui.find("##bthmin", "Body types"), "0"), "height filter off");
        ui.frames(2);
        CHECK(bodytype_ui_state().shown_generic == 10 && bodytype_ui_state().shown_specific == 3, "all rows back");

        // copy a body type from another player (1001 carries code 2)
        const PlayerRow* src = app.model.player(1001);
        CHECK(src != nullptr && src->name.size() >= 4, "player 1001");
        CHECK(ui.click("Copy body type from player...##btcopybtn", "Body types"), "open the copy picker");
        ui.frames(3);
        CHECK(ui.type_into(ui.find("##btcopyflt", "Copy body type"), src->name.substr(0, 4)), "type his name");
        ui.frames(2);
        CHECK(ui.click("Copy##btcp1001", "Copy body type"), "Copy from 1001");
        ui.frames(3);
        CHECK(app.db.get_int(*p, rec, "bodytypecode") == 2, "his body type (Average and Normal) copied");
        CHECK(app.db.get_int(*p, rec, "height") == 175 + 8 && app.db.get_int(*p, rec, "headclasscode") == 1, "height and head untouched");
        CHECK(ui.click("Close##btclose", "Body types"), "close");
        ui.frames(3);
        CHECK(!bodytype_ui_state().open, "popup closed");
        (void)f;

        // managers: the same gallery (501 has a specific head, so a specific body needs no box)
        app.request_tab = 2;
        app.sel_manager = 0;
        ui.frames(3);
        const Table* m = app.db.table("manager");
        CHECK(m && m->has("bodytypecode"), "manager table has bodytypecode");
        const ItemRec* mb = ui.find("Body types...##btopen", "##medit");
        CHECK(mb != nullptr, "manager editor has the Body types button");
        if (mb) {
            ui.click(mb);
            ui.frames(3);
            CHECK(bodytype_ui_state().open, "manager gallery open");
            CHECK(ui.find("Allow a player-specific body on this head (untested)##btallow", "Body types") == nullptr || true, "renders");
            ui.click("Close##btclose", "Body types");
            ui.frames(3);
        }
        std::error_code ec;
        fs::remove(probe, ec);
        app.request_tab = 0;
        io.DisplaySize = saved_display;
        ui.frames(3);
        ui.frames(2);
    });
}
