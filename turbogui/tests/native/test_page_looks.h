// Native tests (2.17 looks/studio-ui, brief 2.17-looks-studio track U; route FULLBODY_FAST_ROUTE.md point 6): Faces & looks > Looks
// studio (ui/ui_looks_studio.cpp). Included by test_main.cpp after test_page_advanced.h (FakeCapture is defined before it);
// page_looks_test::ui_cases runs inside the UI block (a real App on the simulated game). The page is drawn by function in a window of
// its own ("##looksstudio_test", tall enough for the whole page), and once through the shell (Faces & looks > Looks studio).
//
// What is proven here: the body names carry no numbers and the grid offers the generic bodies first, the players' own bodies and his
// current one, leaving out a probed body of the other gender; the Accessories and Kit fit fields exist in the FC 27 schema; a player's
// picture is asked from the game's capture service with the full-body camera and the learned descriptor off, never while the service
// is busy, again after a change of his look, and given up (cancelled) after the timeout; a body made for another head asks nothing by
// itself; a body-type tile writes through the edit path and the page's Undo puts it back (App's Undo for players, the page's own for
// managers); a manager gets no picture request by himself and his real face opens the chooser in one click, with the warning in it.
// 3.1 (brief 3.1-gui, track render): picking a player asks his picture by itself (R3); a failed full body at mode 1 is asked once
// more at mode 0 and both lines reach turbo_gui.log (R1); the caption names the size the game drew (R2). Cases are named
// "Looks studio: ..." so TURBO_TEST_ONLY="looks studio" runs them alone.
#pragma once
#include "core/bodytype_catalog.h"
#include "core/bodytype_thumbs.h"
#include "ui/ui_bodytypes.h"
#include "ui/ui_looks_studio.h"
#include "ui/ui_shell.h"

namespace page_looks_test {

using namespace turbo;

static bool g_show = false;
static void draw_extra(App& app) {
    if (!g_show) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // focusing the page window every frame would close the question (a modal popup) the page has just opened
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##looksstudio_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        looks::draw_looks_studio(app);
    ImGui::End();
}
static void show(Ui& ui) {
    g_show = true;
    g_ui_extra = draw_extra;
    ui.frames(4);
}
static void hide(Ui& ui) {
    g_show = false;
    g_ui_extra = nullptr;
    ui.frames(2);
}

static bool has_digit(const std::string& s) { return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }); }

// The tile of `code` in the body-type grid
static std::string tile_label(App& app, int64_t code, bool manager = false) {
    return looks::body_caption(code, &bodytype_catalog(app), manager) + "##bt" + std::to_string(code);
}

static void pure_cases() {
    run_case("Looks studio: body names have no numbers; the grid offers generic bodies first, the players' own bodies and his own", [&] {
        CHECK(looks::body_caption(4, nullptr) == "Tall, lean", "a generic body by its name (track F): " + looks::body_caption(4, nullptr));
        CHECK(looks::body_caption(10, nullptr) == "Like Lionel Messi", "a player's own body by the player: " + looks::body_caption(10, nullptr));
        CHECK(looks::body_caption(81, nullptr) == "Like Kiernan Dewsbury-Hall", "the best-rated of several: " + looks::body_caption(81, nullptr));
        int64_t unknown = 0;
        for (int64_t c = 512; c > 400 && !unknown; --c)
            if (looks::body_caption(c, nullptr) == "Unique body") unknown = c;
        CHECK(unknown > 0, "a body nothing names reads Unique body");
        const std::vector<int64_t> g = looks::body_grid_codes(nullptr, 0, -1);
        CHECK(g.size() >= 10 + 100, "generic bodies and the players' own: " + std::to_string(g.size()));
        const int64_t generic[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11};
        bool first_generic = g.size() >= 10;
        for (size_t i = 0; i < 10 && i < g.size(); ++i) first_generic = first_generic && g[i] == generic[i];
        CHECK(first_generic, "the ten generic bodies come first, in order");
        CHECK(std::find(g.begin(), g.end(), 10) != g.end() && std::find(g.begin(), g.end(), 251) != g.end(), "Messi's and van de Beek's bodies are offered");
        std::string with_digits;
        for (int64_t c : g)
            if (has_digit(looks::body_caption(c, nullptr))) with_digits += " " + std::to_string(c);
        CHECK(with_digits.empty(), "no tile shows a number:" + with_digits);
        const std::vector<int64_t> h = looks::body_grid_codes(nullptr, unknown, -1);
        CHECK(!h.empty() && h.back() == unknown, "his own body is always offered, even one nothing names");

        // managers: their own set, named by track F
        const std::vector<int64_t> m = looks::body_grid_codes(nullptr, 0, -1, true);
        bool set = true;
        for (int64_t c = 81; c <= 89; ++c) set = set && std::find(m.begin(), m.end(), c) != m.end();
        CHECK(set && std::find(m.begin(), m.end(), 4) != m.end(), "managers: the generic bodies and the manager set 81-89");
        CHECK(looks::body_caption(82, nullptr, true) == "Manager body 82", "a manager body by track F's name: " + looks::body_caption(82, nullptr, true));
        // track F's census: a body only women have is not offered to a man
        const std::vector<int64_t> man = looks::body_grid_codes(nullptr, 0, 0), woman = looks::body_grid_codes(nullptr, 0, 1);
        CHECK(std::find(man.begin(), man.end(), 18) == man.end() && std::find(woman.begin(), woman.end(), 18) != woman.end(), "Sydney Leroux's body: women only");

        // a probed body of the other gender is left out, unless it is his own
        const fs::path probe = g_out / "tmp" / "looks_probe.json";
        fs::create_directories(probe.parent_path());
        {
            std::ofstream f(probe.string(), std::ios::binary);
            f << R"({"version": 1, "bodytypes": [{"code": 18, "players": 1, "gender": 1, "headclasscode": 0, "examples": ["Sydney Leroux"]},
                                                 {"code": 10, "players": 1, "gender": 0, "headclasscode": 0, "examples": ["Lionel Messi"]},
                                                 {"code": 470, "players": 1, "gender": 0, "headclasscode": 0, "examples": ["A. Probed"]}]})";
        }
        bodytype::Catalog cat;
        std::string err;
        CHECK(cat.load_probe(probe, &err), "probe fixture loads: " + err);
        const std::vector<int64_t> men = looks::body_grid_codes(&cat, 0, 0), women = looks::body_grid_codes(&cat, 0, 1);
        CHECK(std::find(men.begin(), men.end(), 18) == men.end() && std::find(men.begin(), men.end(), 10) != men.end(), "men: no women's body");
        CHECK(std::find(women.begin(), women.end(), 10) == women.end() && std::find(women.begin(), women.end(), 18) != women.end(), "women: no men's body");
        const std::vector<int64_t> own = looks::body_grid_codes(&cat, 18, 0);
        CHECK(std::find(own.begin(), own.end(), 18) != own.end(), "his own body stays offered whatever the gender");
        CHECK(std::find(men.begin(), men.end(), 470) != men.end() && looks::body_caption(470, &cat) == "Like A. Probed", "a probed body is named after its example");
    });

    run_case("Looks studio: the Accessories and Kit fit fields exist in the FC 27 schema; a manager's kit fit is his outfit", [&] {
        const std::vector<std::string> outfit = looks::card_fields(true, true);
        CHECK(std::find(outfit.begin(), outfit.end(), "outfitid") != outfit.end(), "a manager's kit fit card is his outfit");
        CHECK(!looks::card_fields(false, true).empty() && !looks::card_fields(false, false).empty() && !looks::card_fields(true, false).empty(),
              "players have both cards, managers both too");
        const std::vector<std::string> gear = looks::card_fields(false, false);
        CHECK(std::find(gear.begin(), gear.end(), "shoetypecode") != gear.end() && std::find(gear.begin(), gear.end(), "accessorycode1") != gear.end() &&
                  std::find(gear.begin(), gear.end(), "gkglovetypecode") != gear.end(),
              "players' accessories: the slots, boots and goalkeeper gloves");
        const fs::path schema = fs::path(g_lua).parent_path().parent_path().parent_path().parent_path() / "turbo" / "le27" / "fc27_db_schema.json";
        std::ifstream f(schema, std::ios::binary);
        if (!f) {
            std::printf("  note: %s is not here (a fixture copied with turbo\\le27\\libs): the schema check is skipped\n", schema.string().c_str());
            return;
        }
        const json j = json::parse(f, nullptr, false);
        CHECK(!j.is_discarded() && j.contains("tables"), "the schema reads");
        if (j.is_discarded() || !j.contains("tables")) return;
        auto has = [&](const char* table, const std::string& field) {
            if (!j["tables"].contains(table)) return false;
            for (const json& fd : j["tables"][table]["fields"])
                if (fd.value("name", "") == field) return true;
            return false;
        };
        for (bool kit : {false, true}) {
            for (const std::string& fd : looks::card_fields(false, kit)) CHECK(has("players", fd), "players." + fd + " is in the schema");
            for (const std::string& fd : looks::card_fields(true, kit)) CHECK(has("manager", fd), "manager." + fd + " is in the schema");
        }
    });
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    pure_cases();
    const ImVec2 old_display = ImGui::GetIO().DisplaySize;
    ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 3400.0f);  // a whole page fits: nothing is scrolled out of reach
    const auto old_capture = app.capture;
    const int64_t old_player = app.sel_player;
    const int old_manager = app.sel_manager;
    ui.frames(3);

    run_case("Looks studio: the Appearance Editor opens in the Simple view", [&] {
        app.prefs.advanced_mode = false;
        looks::looks_studio_reset_for_tests();
        app.capture = nullptr;
        // 2.20: the Appearance Editor is the only page of its area, so the area tab opens it (there is no page strip)
        CHECK(ui.click("Looks", "FC 27 LE Turbo", "##shell") && (ui.frames(2), app.place() == shell::place_of(shell::Page::LooksStudio)), "Appearance Editor");
        ui.frames(2);
        CHECK(ui.find("###lsplayers", "FC 27 LE Turbo") != nullptr && ui.find("###lsmanagers", "FC 27 LE Turbo") != nullptr, "the person list is drawn");
        CHECK(shell::default_page(shell::Area::Looks) == shell::Page::LooksStudio && shell::pages_of(shell::Area::Looks).size() == 1, "the Appearance Editor is the area's only page");
        app.go(shell::Page::PlayerEdit);
        ui.frames(3);
    });

    run_case("Looks studio: a player's full body (track C's camera, template off), never while busy, again after a change, the switch, timeout; a tile writes; Undo", [&] {
        const Table* t = app.db.table("players");
        const PlayerRow* p = app.model.player(3001);
        if (!t || !p || !t->has("bodytypecode")) {
            CHECK(false, "the test world has player 3001 with a body type");
            return;
        }
        const uint64_t rec = p->rec;
        const int64_t body0 = app.db.get_int(*t, rec, "bodytypecode", 0);
        const int64_t hc0 = t->has("headclasscode") ? app.db.get_int(*t, rec, "headclasscode", 0) : -1;
        if (t->has("headclasscode")) app.db.set_int(*t, rec, "headclasscode", 0);  // a real face: no body is risky on it
        looks::looks_studio_reset_for_tests();
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = fake->st.available = true;
        fake->st.reason = "ready";
        app.capture = fake;
        app.sel_player = 0;
        show(ui);
        // the list: search his name, click him
        const std::string name = p->name;
        CHECK(ui.type_into(ui.find("##lsq", "##looksstudio_test"), name), "search box");
        CHECK(ui.click(name + "##lsp3001", "##looksstudio_test") && app.sel_player == 3001, "picked from the list");
        CHECK(looks::studio_state().listed >= 1 && looks::studio_state().subject.id == 3001, "he is the person shown");
        ui.frames(30);
        // 3.1 R3: picking a player with no picture of him asks the idle service for one by itself (no "Draw him" click)
        CHECK(fake->requests.size() == 1, "picking him asks for his picture by itself (R3): " + std::to_string(fake->requests.size()));
        CHECK(ui.find("Draw again###lsdraw", "##looksstudio_test") != nullptr, "while it draws, the button says Draw again");
        if (!fake->requests.empty()) {
            const capture::Request& r = fake->requests.back();
            CHECK(r.id == 3001 && !r.manager && r.camera == capture::kCameraBodyFull && !r.use_template, "full-body camera (the default view), learned descriptor off");
            CHECK(r.second_id == (p->club > 0 ? p->club : -1), "his club as the second id");
            CHECK(r.mode_override == -1, "the camera's own mode (the built-in full body: mode 1)");
        }
        CHECK(looks::studio_state().waiting, "the pane says it is drawing");
        fake->next = capture::Result();
        fake->next.ok = true;
        fake->next.format = "PNG 1024x1024";
        fake->next.image = solid(64, 80, 30, 120, 200);
        fake->deliver = true;
        ui.frames(3);
        CHECK(looks::studio_state().preview_line.find("(64x80)") != std::string::npos, "the caption names the size the game drew: " + looks::studio_state().preview_line);
        CHECK(!fake->pending && looks::studio_state().picture && looks::studio_state().source == "Full body" && looks::studio_state().full_view,
              "the full-body picture is drawn in the pane");
        CHECK(ui.find("Draw again###lsdraw", "##looksstudio_test") != nullptr, "the button now says Draw again");
        CHECK(looks::studio_state().preview_line.find("Full body, drawn by the game at") == 0, "caption: " + looks::studio_state().preview_line);

        // a body-type tile: written through the edit path, one step of his Undo, and his picture asked again
        const int64_t b1 = body0 == 6 ? 4 : 6;
        CHECK(ui.click(tile_label(app, b1), "##looksstudio_test"), "tile " + tile_label(app, b1));
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == b1, "body type written");
        CHECK(looks::studio_state().undo_label == "Undo: Body type", "Undo names it: " + looks::studio_state().undo_label);
        CHECK(fake->requests.size() == 2 && fake->requests.back().camera == capture::kCameraBodyFull, "a new picture after the change");
        fake->deliver = true;
        ui.frames(3);

        // the service busy with another picture: nothing is sent; once it is free, the page asks
        fake->pending = true;
        fake->current = capture::Request();
        fake->current.label = "someone else";
        const int64_t b2 = body0 == 5 ? 7 : 5;
        CHECK(ui.click(tile_label(app, b2), "##looksstudio_test"), "tile " + tile_label(app, b2));
        ui.frames(20);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == b2 && fake->requests.size() == 2, "busy: written, nothing asked");
        CHECK(looks::studio_state().preview_line.find("Still drawing the last picture") != std::string::npos, "it says why: " + looks::studio_state().preview_line);
        fake->pending = false;
        ui.frames(130);  // tried again every 2 s
        CHECK(fake->requests.size() == 3, "asked once the service is free: " + std::to_string(fake->requests.size()));
        fake->deliver = true;
        ui.frames(3);

        // Undo twice: his body type is back
        CHECK(ui.click("###lsundo", "##looksstudio_test"), "Undo");
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == b1, "first Undo: the body before the last change");
        CHECK(ui.click("###lsundo", "##looksstudio_test"), "Undo again");
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == body0, "second Undo: his own body");
        ui.frames(40);
        if (fake->pending) {
            fake->deliver = true;
            ui.frames(3);
        }

        // the switch: Head and shoulders asks the big picture (none of him yet), Full body again asks nothing (his picture is current)
        size_t n = fake->requests.size();
        CHECK(ui.click("###lsviewhead", "##looksstudio_test"), "Head and shoulders");
        ui.frames(3);
        CHECK(fake->requests.size() == n + 1 && fake->requests.back().camera == looks::kCameraBody, "the head-and-shoulders picture is asked");
        fake->deliver = true;
        ui.frames(3);
        CHECK(!looks::studio_state().full_view && looks::studio_state().source == "Head and shoulders", "the head-and-shoulders picture is shown");
        n = fake->requests.size();
        CHECK(ui.click("###lsviewbody", "##looksstudio_test"), "Full body");
        ui.frames(3);
        if (fake->pending) {  // his full-body picture was older than his look (the second Undo): asked again
            fake->deliver = true;
            ui.frames(3);
        }
        CHECK(looks::studio_state().full_view && looks::studio_state().source == "Full body" && fake->requests.size() <= n + 1, "back to the full body");

        // no answer: given up after the timeout, the service is freed (cancel)
        looks::looks_studio_set_timeout_for_tests(0.5);
        const int cancels = fake->cancels;
        CHECK(ui.click("Draw again###lsdraw", "##looksstudio_test") && fake->pending, "Draw again asks");
        ui.frames(45);
        CHECK(fake->cancels == cancels + 1 && !fake->pending && !looks::studio_state().waiting, "timeout: cancelled");
        CHECK(looks::studio_state().preview_line.find("No picture after") != std::string::npos, "it says so: " + looks::studio_state().preview_line);
        looks::looks_studio_set_timeout_for_tests(25.0);

        // a body made for another player's head on a generic head: no picture by itself
        if (t->has("headclasscode")) {
            const size_t before = fake->requests.size();
            app.db.set_int(*t, rec, "headclasscode", 1);
            app.db.set_int(*t, rec, "bodytypecode", 10);
            ui.frames(45);
            CHECK(looks::studio_state().risky_body && fake->requests.size() == before, "risky body: nothing asked by itself");
            app.db.set_int(*t, rec, "bodytypecode", body0);
        }

        // the Accessories and Kit fit cards draw the fields the table has
        size_t want = 0;
        for (bool kit : {false, true})
            for (const std::string& fd : looks::card_fields(false, kit)) want += t->has(fd) ? 1 : 0;
        CHECK(looks::studio_state().fields == static_cast<int>(want), "field editors: " + std::to_string(looks::studio_state().fields) + " of " + std::to_string(want));
        CHECK(looks::studio_state().body_tiles >= 10, "body tiles drawn: " + std::to_string(looks::studio_state().body_tiles));
        hide(ui);
        if (t->has("headclasscode")) app.db.set_int(*t, rec, "headclasscode", hc0);
        app.db.set_int(*t, rec, "bodytypecode", body0);
    });

    run_case("Looks studio: no full-body picture at mode 1 -> ONE retry at mode 0, logged to turbo_gui.log (3.1 R1)", [&] {
        const PlayerRow* p = app.model.player(3001);
        if (!p) {
            CHECK(false, "the test world has player 3001");
            return;
        }
        const Table* t = app.db.table("players");
        const int64_t hc0 = t && t->has("headclasscode") ? app.db.get_int(*t, p->rec, "headclasscode", 0) : -1;
        if (t && t->has("headclasscode")) app.db.set_int(*t, p->rec, "headclasscode", 0);  // a real face: no body is risky on it
        looks::looks_studio_reset_for_tests();
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = fake->st.available = true;
        app.capture = fake;
        std::vector<std::string> host;
        const auto old_hook = app.log_hook;
        app.log_hook = [&host](const std::string& l) { host.push_back(l); };
        auto host_has = [&](const std::string& what) {
            return std::any_of(host.begin(), host.end(), [&](const std::string& l) { return l.find(what) != std::string::npos; });
        };
        app.sel_player = 0;
        show(ui);
        CHECK(ui.type_into(ui.find("##lsq", "##looksstudio_test"), p->name), "search box");
        CHECK(ui.click(p->name + "##lsp3001", "##looksstudio_test") && app.sel_player == 3001, "picked from the list");
        ui.frames(5);
        if (fake->requests.empty()) {  // before R3 the pick asks nothing: ask by hand, so the retry is still tested
            ui.click("Draw him###lsdraw", "##looksstudio_test");
            ui.frames(2);
        }
        CHECK(fake->requests.size() == 1 && fake->requests[0].mode_override == -1, "first ask: the camera's own mode (1)");
        fake->next = capture::Result();
        fake->next.ok = false;
        fake->next.error = "no picture within the Settle";
        fake->deliver = true;
        ui.frames(3);
        CHECK(fake->requests.size() == 2, "one retry: " + std::to_string(fake->requests.size()));
        if (fake->requests.size() >= 2) {
            const capture::Request& r = fake->requests[1];
            CHECK(r.mode_override == 0 && r.camera == capture::kCameraBodyFull && r.id == 3001 && !r.use_template, "the retry: full body at mode 0");
        }
        CHECK(looks::studio_state().waiting, "the pane is still drawing");
        fake->next = capture::Result();
        fake->next.ok = true;
        fake->next.format = "PNG 256x256";
        fake->next.image = solid(256, 256, 30, 120, 200);
        fake->deliver = true;
        ui.frames(3);
        CHECK(looks::studio_state().picture && looks::studio_state().source == "Full body", "the mode-0 picture is drawn");
        CHECK(host_has("full body: mode 1 gave nothing, drew at 256"), "turbo_gui.log: full body: mode 1 gave nothing, drew at 256");
        CHECK(host_has("picture of id 3001 (PNG 256x256)"), "turbo_gui.log names the size the game drew");
        // a manual ask starts over at the camera's own mode; a failed retry is not retried again
        fake->next = capture::Result();
        fake->next.ok = false;
        fake->next.error = "nothing";
        CHECK(ui.click("Draw again###lsdraw", "##looksstudio_test"), "Draw again");
        ui.frames(2);
        CHECK(fake->requests.size() == 3 && fake->requests[2].mode_override == -1, "Draw again: mode 1 again");
        fake->deliver = true;
        ui.frames(3);
        CHECK(fake->requests.size() == 4 && fake->requests[3].mode_override == 0, "its one retry at mode 0");
        fake->deliver = true;
        ui.frames(5);
        CHECK(fake->requests.size() == 4 && !fake->pending, "a failed retry is not retried: " + std::to_string(fake->requests.size()));
        CHECK(looks::studio_state().preview_line.find("did not draw him") != std::string::npos, "it says so: " + looks::studio_state().preview_line);
        hide(ui);
        app.log_hook = old_hook;
        if (t && t->has("headclasscode")) app.db.set_int(*t, p->rec, "headclasscode", hc0);
    });

    run_case("e9-Looks studio: a manager's picture (9999 for the user's manager, full body tried), the page's own Undo, and his real face in one click", [&] {
        const Table* t = app.db.table("manager");
        const auto& mgrs = app.model.managers();
        if (!t || mgrs.empty() || !t->has("bodytypecode")) {
            CHECK(false, "the test world has a manager with a body type");
            return;
        }
        const uint64_t rec = mgrs[0].rec;
        const int64_t body0 = app.db.get_int(*t, rec, "bodytypecode", 0);
        const int64_t head0 = app.db.get_int(*t, rec, "headassetid", 0);
        if (head0 <= 0 && t->has("headassetid")) app.db.set_int(*t, rec, "headassetid", 1016);  // a head the game can draw
        const int64_t head = app.db.get_int(*t, rec, "headassetid", 0);
        looks::looks_studio_reset_for_tests();
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = fake->st.available = true;
        app.capture = fake;
        looks::looks_studio_show_managers(true);
        app.sel_manager = 0;
        show(ui);
        ui.frames(30);
        CHECK(looks::studio_state().subject.manager && looks::studio_state().subject.id == mgrs[0].managerid, "the manager is shown");
        auto studio_requests = [&] {  // the page's own requests (the face chooser asks the same service for its 3D heads)
            size_t n = 0;
            for (const capture::Request& r : fake->requests) n += r.label.rfind("Looks studio", 0) == 0 ? 1 : 0;
            return n;
        };
        CHECK(studio_requests() == 0 && looks::studio_state().requests == 0, "picking him asks nothing");
        CHECK(looks::studio_state().source == "On the game's screen", "until then the pane shows the hint: " + looks::studio_state().source);
        CHECK(ui.click("Draw him###lsdraw", "##looksstudio_test"), "Draw him");
        ui.frames(2);
        CHECK(studio_requests() == 1, "one picture asked: " + std::to_string(studio_requests()));
        if (!fake->requests.empty()) {
            const capture::Request& r = fake->requests.back();
            // 5.0c E9 (3.1.1 M3): the first manager of the test world runs the user's club, so the ONE resolver (core/capture_subject.h) sends 9999, not his head id
            const BridgeState& bs = app.bridge.state();
            CHECK(bs.in_cm && bs.user_team > 0 && mgrs[0].teamid == bs.user_team, "the test world's first manager is the user's manager");
            CHECK(r.manager && r.id == capture::kUserAvatarHeadId && r.camera == capture::kCameraBodyFull && !r.use_template,
                  "by 9999 (the user's manager, not his head " + std::to_string(head) + "), full-body camera, learned descriptor off (asked " + std::to_string(r.id) + ")");
            CHECK(r.second_id == (mgrs[0].teamid > 0 ? mgrs[0].teamid : -1), "his club as the second id");
        }
        fake->next = capture::Result();
        fake->next.ok = true;
        fake->next.format = "PNG 1024x1024";
        fake->next.image = solid(64, 64, 200, 120, 30);
        fake->deliver = true;
        ui.frames(3);
        CHECK(looks::studio_state().picture && looks::studio_state().source == "Full body", "his picture is drawn in the pane");
        size_t want = 0;
        for (bool kit : {false, true})
            for (const std::string& fd : looks::card_fields(true, kit)) want += t->has(fd) ? 1 : 0;
        CHECK(looks::studio_state().fields == static_cast<int>(want), "manager fields: " + std::to_string(looks::studio_state().fields) + " of " + std::to_string(want));
        // a body-type tile and the page's own Undo
        const int64_t b1 = body0 == 3 ? 2 : 3;
        CHECK(ui.click(tile_label(app, b1, true), "##looksstudio_test"), "tile " + tile_label(app, b1, true));
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == b1, "manager's body type written");
        CHECK(looks::studio_state().undo_label == "Undo: Body type", "Undo names it: " + looks::studio_state().undo_label);
        CHECK(studio_requests() == 2, "his picture asked again after the change");
        fake->deliver = true;
        ui.frames(3);
        CHECK(ui.click("###lsundo", "##looksstudio_test"), "Undo");
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == body0 && looks::studio_state().undo_label.empty(), "his body type is back, nothing left to undo");
        // 3.1 R4: his real face in ONE click: the chooser opens with the "can't be undone" warning in it, no question first;
        // Close changes nothing
        const int64_t mhead = t->has("headassetid") ? app.db.get_int(*t, rec, "headassetid", 0) : 0;
        const int64_t mclass = t->has("headclasscode") ? app.db.get_int(*t, rec, "headclasscode", 0) : 0;
        CHECK(ui.click("###lsface", "##looksstudio_test"), "Choose a real face...");
        ui.frames(3);
        CHECK(ui.find("###lsaskok") == nullptr && ui.find("###lsaskno") == nullptr, "no question before the chooser");
        CHECK(ui.find("Close##mfaces") != nullptr, "one click: the manager chooser is open");
        CHECK(looks::studio_state().asking, "the chooser shows the warning line");
        if (ui.find("Close##mfaces")) ui.click("Close##mfaces");
        ui.frames(2);
        CHECK(ui.find("Close##mfaces") == nullptr && !looks::studio_state().asking, "Close: the chooser and its warning are gone");
        CHECK((!t->has("headassetid") || app.db.get_int(*t, rec, "headassetid", 0) == mhead) &&
                  (!t->has("headclasscode") || app.db.get_int(*t, rec, "headclasscode", 0) == mclass),
              "Close changes nothing (his head is the same)");
        CHECK(studio_requests() == 3 && looks::studio_state().requests == 3, "the page asked three pictures: Draw him, the change, the Undo");
        hide(ui);
        looks::looks_studio_show_managers(false);
        app.db.set_int(*t, rec, "bodytypecode", body0);
        if (t->has("headassetid")) app.db.set_int(*t, rec, "headassetid", head0);
    });

    run_case("Looks studio: the body-type grid shows the generator's pictures (a name where none), a new manifest refreshes it; edits and Undo reach the host log", [&] {
        const Table* t = app.db.table("players");
        const PlayerRow* p = app.model.player(3001);
        if (!t || !p || !t->has("bodytypecode")) {
            CHECK(false, "the test world has player 3001 with a body type");
            return;
        }
        const uint64_t rec = p->rec;
        const int64_t body0 = app.db.get_int(*t, rec, "bodytypecode", 0);
        looks::looks_studio_reset_for_tests();
        app.capture = nullptr;  // no picture service: this case is about the grid
        const fs::path out = app.turbo_output();
        const fs::path dir = bodytype_thumbs::folder(out);
        std::error_code ec;
        fs::remove_all(dir, ec);
        std::string file, err;
        const bool saved = bodytype_thumbs::save_pictures(out, 4, solid(64, 128, 200, 40, 40), &file, &err) &&
                           bodytype_thumbs::save_pictures(out, 6, solid(64, 128, 40, 200, 40), &file, &err);
        CHECK(saved && fs::exists(bodytype_thumbs::grid_file(out, 4)) && fs::exists(bodytype_thumbs::grid_file(out, 6)), "two generator pictures: " + err);
        bodytype_thumbs::invalidate();
        std::vector<std::string> host;
        const auto old_hook = app.log_hook;
        app.log_hook = [&host, old_hook](const std::string& l) {
            host.push_back(l);
            if (old_hook) old_hook(l);
        };
        app.sel_player = 3001;
        show(ui);
        ui.frames(12);
        CHECK(looks::studio_state().thumbs == 2, "the two pictures are drawn: " + std::to_string(looks::studio_state().thumbs));
        CHECK(looks::studio_state().body_tiles >= 10, "every body has its tile: " + std::to_string(looks::studio_state().body_tiles));
        CHECK(ui.find(tile_label(app, 5), "##looksstudio_test") != nullptr, "a body with no picture keeps its name: " + tile_label(app, 5));
        // another run: one picture gone, a new manifest.json -> the grid lists again (looked at every 2 s)
        fs::remove(bodytype_thumbs::grid_file(out, 6), ec);
        CHECK(bodytype_thumbs::write_file_atomic(bodytype_thumbs::manifest_file(out), "{\"version\": 1, \"entries\": []}", &err), "manifest written: " + err);
        ui.frames(130);
        CHECK(looks::studio_state().thumbs == 1, "after the new manifest one picture is left: " + std::to_string(looks::studio_state().thumbs));
        // a body-type edit and its Undo reach the host log (turbo_gui.log), in the live check's words
        const int64_t b1 = body0 == 4 ? 6 : 4;
        CHECK(ui.click(tile_label(app, b1), "##looksstudio_test"), "tile " + tile_label(app, b1));
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == b1, "body type written");
        auto logged = [&](const std::string& what) {
            for (const std::string& l : host)
                if (l.find(what) != std::string::npos) return true;
            return false;
        };
        CHECK(logged("[players.bodytypecode = " + std::to_string(b1) + "]"), "the edit is in the host log");
        CHECK(ui.click("###lsundo", "##looksstudio_test"), "Undo");
        ui.frames(2);
        CHECK(app.db.get_int(*t, rec, "bodytypecode", 0) == body0 && logged("undo players.bodytypecode = " + std::to_string(body0)),
              "the Undo is in the host log");
        hide(ui);
        app.log_hook = old_hook;
        fs::remove_all(dir, ec);
        bodytype_thumbs::invalidate();
        app.db.set_int(*t, rec, "bodytypecode", body0);
    });

    looks::looks_studio_reset_for_tests();
    app.capture = old_capture;
    app.sel_player = old_player;
    app.sel_manager = old_manager;
    ImGui::GetIO().DisplaySize = old_display;
    ui.frames(3);
}

}  // namespace page_looks_test
