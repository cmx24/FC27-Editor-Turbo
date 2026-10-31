// Native tests (5.0c E8-body, brief PITWALL\briefs\5.0c\E8-body.md, Cassio 10-30: "Body type list: text only, no visual; duplicate 'Unique
// body' rows; 'Manager body 81-89' raw names"). Included by test_main.cpp after test_page_looks.h (the same Studio and Ui helpers);
// body_picker_test::core_cases() runs with the core tests, body_picker_test::ui_cases() inside the UI block. Cases are named
// "Body picker: ..." so TURBO_TEST_ONLY="body picker" runs them alone (a fail-old proof only, never the gate).
//
// What is proven here:
//   T1  every name the body grid shows (managers, players of every gender, with and without the probe catalogue) is unique, plain (no
//       "Unique body", "Manager body N", "Style N", "Specific body", no leading number), and no manager name repeats a generic
//       name once its "Manager, " prefix and "(n of m)" suffix are taken off; codes 12 / 131 / 381 are offered only as the current
//       value, and then as exactly one "His own body".  FAILS on 5.0b/integration d197d851 (Manager body 81..89 x9, Unique body x3).
//   T6  RV4 (Cassio 5.0c): the manager grid shows ONE tile per distinct figure (no two manager-set tiles with the same height word and
//       build word, no "(n of m)" name); "Show all body types" (looks::body_show_all) lists all of 81-89 again; a manager holding a hidden
//       look-alike (83) sees his own code in place of the group's lead. Fail-old: the same checker is run on the show-all list, which
//       IS the old list (E8 9cb9000d: 9 codes, "(n of 3)" / "(n of 2)" names), and must report the look-alikes.
//   V1  apply_bodytype writes the manager set 81-89 on a manager with a generic head (the game itself pairs them: the census
//       generic_head column), and still refuses a player-specific body (code 20) there.  FAILS on d197d851 (it was refused).
#pragma once
#include <regex>

#include "core/bodytype_catalog.h"
#include "ui/ui_bodytypes.h"
#include "ui/ui_body_figure.h"
#include "core/body_shape.h"
#include "ui/ui_looks_studio.h"
#include "ui/ui_visual_picker.h"
#include "ui/ui_style.h"

namespace body_picker_test {

using namespace turbo;

static std::string lower_copy_bp(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// the generic names (codes 1-9 and 11) a manager name must not repeat
static std::set<std::string> generic_names() {
    std::set<std::string> out;
    for (int64_t c : {1, 2, 3, 4, 5, 6, 7, 8, 9, 11}) out.insert(lower_copy_bp(looks::body_caption(c, nullptr)));
    return out;
}

// "Manager, medium height, regular build (2 of 3)" -> "medium height, regular build"
static std::string strip_manager(std::string n) {
    const std::string pre = "Manager, ";
    if (n.compare(0, pre.size(), pre) == 0) n.erase(0, pre.size());
    static const std::regex suffix(R"( \(\d+ of \d+\)$)");
    return lower_copy_bp(std::regex_replace(n, suffix, ""));
}

// the problems of one grid: what T1 forbids, as text ("" = clean)
static std::string grid_problems(const char* what, const bodytype::Catalog* cat, int64_t current, int gender, bool manager) {
    std::string bad;
    const std::vector<int64_t> codes = looks::body_grid_codes(cat, current, gender, manager);
    std::map<std::string, int64_t> seen;
    static const std::regex banned(R"(Unique body|Manager body|Style \d|Specific body|^\d)");
    const std::set<std::string> generic = generic_names();
    int own = 0;
    for (int64_t c : codes) {
        const std::string cap = looks::body_caption(c, cat, manager);
        if (std::regex_search(cap, banned)) bad += " [" + std::to_string(c) + " reads '" + cap + "']";
        if (cap == "His own body") ++own;
        auto it = seen.find(cap);
        if (it != seen.end()) bad += " [" + std::to_string(c) + " repeats '" + cap + "' of " + std::to_string(it->second) + "]";
        else seen[cap] = c;
        const bool is_generic = c == 1 || c == 2 || c == 3 || c == 4 || c == 5 || c == 6 || c == 7 || c == 8 || c == 9 || c == 11;
        if (manager && !is_generic && cap != "His own body" && generic.count(strip_manager(cap)))
            bad += " [" + std::to_string(c) + " '" + cap + "' repeats a generic name]";
        if ((c == 12 || c == 131 || c == 381) && c != current) bad += " [" + std::to_string(c) + " is offered but is not his]";
    }
    const bool own_code = current == 12 || current == 131 || current == 381;
    if (own_code && own != 1) bad += " [his own body shows " + std::to_string(own) + " times, not once]";
    if (!own_code && own != 0) bad += " ['His own body' shown for a named current value]";
    return bad.empty() ? std::string() : std::string(what) + ":" + bad;
}

// T6: the look-alike problems of one manager grid ("" = one tile per distinct figure, plain names)
static std::string look_alike_problems(const bodytype::Catalog* cat, int64_t current) {
    std::string bad;
    std::map<std::string, int64_t> figure;  // "height word, build word" (the plain name) -> the first code drawn that way
    static const std::regex nofm(R"(\(\d+ of \d+\))");
    for (int64_t c : looks::body_grid_codes(cat, current, -1, true)) {
        if (!bodyshape::manager_set(c)) continue;
        const std::string cap = looks::body_caption(c, cat, true);
        if (std::regex_search(cap, nofm)) bad += " [" + std::to_string(c) + " reads '" + cap + "']";
        const std::string key = bodyshape::manager_set_plain_name(c);
        auto it = figure.find(key);
        if (it != figure.end()) bad += " [" + std::to_string(c) + " draws the same figure as " + std::to_string(it->second) + ": " + key + "]";
        else figure[key] = c;
    }
    return bad;
}

inline void core_cases() {
    run_case("Body picker: T1 the grid shows unique plain names for managers and players (no Unique body, no Manager body N)", [&] {
        bodytype::Catalog cat;
        std::string err;
        CHECK(cat.load_probe(bt_fixture(), &err), "probe fixture loads: " + err);
        std::string bad;
        const bodytype::Catalog* const cats[2] = {nullptr, &cat};
        for (const bodytype::Catalog* c : cats) {
            const std::string tag = c ? "with the probe" : "without the probe";
            bad += grid_problems((tag + ", manager").c_str(), c, 0, -1, true);
            bad += grid_problems((tag + ", manager holding 82").c_str(), c, 82, -1, true);
            for (int64_t own : {12, 131, 381}) bad += grid_problems((tag + ", manager holding " + std::to_string(own)).c_str(), c, own, -1, true);
            for (int g : {-1, 0, 1}) {
                bad += grid_problems((tag + ", player gender " + std::to_string(g)).c_str(), c, 0, g, false);
                bad += grid_problems((tag + ", player gender " + std::to_string(g) + " holding 10").c_str(), c, 10, g, false);
            }
        }
        CHECK(bad.empty(), "the grid reads plain and unique:" + bad);
    });

    run_case("Body picker: T6 RV4 one manager tile per distinct figure, Show all body types lists every code, his own look-alike stays his", [&] {
        bodytype::Catalog cat;
        std::string err;
        CHECK(cat.load_probe(bt_fixture(), &err), "probe fixture loads: " + err);
        const bodytype::Catalog* const cats[2] = {nullptr, &cat};
        std::string bad;
        looks::body_show_all(false);
        for (const bodytype::Catalog* c : cats)
            for (int64_t cur : {int64_t{0}, int64_t{82}, int64_t{83}, int64_t{86}, int64_t{81}, int64_t{89}, int64_t{84}, int64_t{12}}) {
                const std::string p = look_alike_problems(c, cur);
                if (!p.empty()) bad += std::string(c ? " with" : " without") + " the probe, holding " + std::to_string(cur) + ":" + p;
            }
        CHECK(bad.empty(), "default manager grid has one tile per distinct figure:" + bad);
        // holding 83 (a hidden look-alike of the lead): his own code is the group's one tile
        const std::vector<int64_t> g83 = looks::body_grid_codes(nullptr, 83, -1, true);
        auto has = [](const std::vector<int64_t>& v, int64_t c) { return std::find(v.begin(), v.end(), c) != v.end(); };
        int group83 = 0;
        for (int64_t c : g83) group83 += bodyshape::manager_set(c) && bodyshape::manager_set_lead(c) == bodyshape::manager_set_lead(83);
        CHECK(has(g83, 83) && group83 == 1, fmt("holding 83: his code is listed and is the only tile of its group (%d)", group83));
        const size_t n_default = looks::body_grid_codes(nullptr, 0, -1, true).size();
        // recoverable: ticked, all nine codes of the set are listed again, with the "(n of m)" names that tell them apart
        looks::body_show_all(true);
        const std::vector<int64_t> all = looks::body_grid_codes(nullptr, 0, -1, true);
        int set_codes = 0;
        for (int64_t c = 81; c <= 89; ++c) set_codes += has(all, c);
        CHECK(set_codes == 9 && all.size() > n_default, fmt("Show all body types lists 81-89 (%d of 9), %zu tiles vs %zu", set_codes, all.size(), n_default));
        // fail-old: the show-all list is the old list, and the checker must find the look-alikes there (82/83/86, 81/89)
        const std::string old_list = look_alike_problems(nullptr, 0);
        CHECK(!old_list.empty() && old_list.find("draws the same figure") != std::string::npos,
              "the checker reports the old list's look-alikes: " + old_list);
        looks::body_show_all(false);
        CHECK(!looks::body_show_all(), "the box is off again for the next case");
    });

    run_case("Body picker: T2 the figure follows the size - taller bodies are drawn taller, every point is inside the tile, names are the plain words", [&] {
        const bodytype::Catalog* none = nullptr;
        // the game's own names: 4 Tall lean, 1 Average lean, 7 Short lean (same build, three heights); 3 stocky vs 1 lean (same height)
        const auto s4 = bodyshape::body_shape(false, 4, none), s1 = bodyshape::body_shape(false, 1, none), s7 = bodyshape::body_shape(false, 7, none);
        const auto s3 = bodyshape::body_shape(false, 3, none), s11 = bodyshape::body_shape(false, 11, none);
        CHECK(s4.height_cm > s1.height_cm && s1.height_cm > s7.height_cm, "tall > average > short: " + std::to_string(s4.height_cm) + " " + std::to_string(s1.height_cm) + " " + std::to_string(s7.height_cm));
        CHECK(s3.build > s1.build, "stocky is broader than lean");
        CHECK(s11.height_cm > s4.height_cm, "very tall is taller than tall");
        auto top_y = [](const bodyshape::BodyShape& s) {
            float top = 2.0f;
            for (const bodyshape::Polygon& poly : bodyshape::silhouette(s))
                for (const bodyshape::Pt& p : poly) top = std::min(top, p.y);
            return top;
        };
        CHECK(top_y(s4) < top_y(s1) && top_y(s1) < top_y(s7), "a taller figure reaches higher in the same tile");
        bool inside = true, convex_ok = true;
        for (int64_t c = 1; c <= 400; ++c) {
            for (bool mgr : {false, true}) {
                const auto polys = bodyshape::silhouette(bodyshape::body_shape(mgr, c, none));
                inside = inside && polys.size() == 7;
                for (const bodyshape::Polygon& poly : polys) {
                    convex_ok = convex_ok && poly.size() >= 3;
                    for (const bodyshape::Pt& p : poly) inside = inside && p.x >= 0.0f && p.x <= 1.0f && p.y >= 0.0f && p.y <= 1.0f;
                }
            }
        }
        CHECK(inside && convex_ok, "every silhouette point of codes 1..400 lies in the unit tile");
        CHECK(bodyshape::body_shape(true, 81, none).height_cm != bodyshape::body_shape(true, 89, none).height_cm, "the manager set differs in height");
    });

    run_case("Body picker: names - the game's words for 1-11, 'Like <player>' for a player's own body, manager sizes for 81-89, risky_for per subject", [&] {
        CHECK(bodyshape::body_name(false, 4, nullptr) == "Tall, lean", "code 4: " + bodyshape::body_name(false, 4, nullptr));
        CHECK(bodyshape::body_name(false, 10, nullptr) == "Like Lionel Messi", "code 10: " + bodyshape::body_name(false, 10, nullptr));
        for (int64_t c = 81; c <= 89; ++c) {
            const std::string n = bodyshape::body_name(true, c, nullptr);
            CHECK(n.rfind("Manager, ", 0) == 0, "manager " + std::to_string(c) + " reads a size: " + n);
        }
        CHECK(bodyshape::body_name(true, 12, nullptr).empty(), "a manager's own style (12) has no name: the grid says His own body");
        bodytype::Catalog cat;
        std::string err;
        CHECK(cat.load_probe(bt_fixture(), &err), "probe fixture loads: " + err);
        CHECK(!bodyshape::risky_for(true, cat, 82, 1) && bodytype::risky_pairing(cat, 82, 1), "the manager set on a generic head is not risky for a manager, the plain rule says it is");
        CHECK(bodyshape::risky_for(false, cat, 82, 1) == bodytype::risky_pairing(cat, 82, 1), "for a player nothing changes");
    });

    run_case("Body picker: T5 contrast - the figure against its tile and the selection ring against the tile are 3:1 or better in every state", [&] {
        std::string bad;
        for (bool sel : {false, true})
            for (bool hov : {false, true}) {
                const float f = bodyfig::contrast_ratio(bodyfig::figure_fill(sel), bodyfig::tile_ground(sel, hov));
                if (f < 3.0f) bad += " figure sel=" + std::to_string(sel) + " hov=" + std::to_string(hov) + " " + std::to_string(f);
            }
        const float ring = bodyfig::contrast_ratio(bodyfig::selected_ring(), bodyfig::tile_ground(false, false));
        if (ring < 3.0f) bad += " ring " + std::to_string(ring);
        // a selected tile also differs from an unselected one by more than colour: the ring (shape) and a different fill
        CHECK(bodyfig::tile_ground(true, false) != bodyfig::tile_ground(false, false), "selected differs from normal");
        CHECK(bad.empty(), "contrast below 3:1:" + bad);
    });
}

inline void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    run_case("Body picker: V1 apply_bodytype writes the manager set 81-89 on a generic head and still refuses a player-specific body there", [&] {
        const Table* m = app.db.table("manager");
        CHECK(m && m->has("bodytypecode"), "manager table has bodytypecode");
        if (!m || !m->has("bodytypecode")) return;
        const uint64_t m503 = app.db.find(*m, "managerid", 503);
        CHECK(m503 != 0, "manager 503 exists");
        if (m503 == 0) return;
        CHECK(m->has("headclasscode") && app.db.get_int(*m, m503, "headclasscode") == 1, "manager 503 has a generic head (headclasscode 1)");
        const int64_t before = app.db.get_int(*m, m503, "bodytypecode");
        std::string msg;
        CHECK(!apply_bodytype(app, *m, m503, 20, false, &msg) && app.db.get_int(*m, m503, "bodytypecode") == before,
              "a player-specific body (20) on a generic head stays refused: " + msg);
        for (int64_t code : {81, 82, 89}) {
            const bool ok = apply_bodytype(app, *m, m503, code, false, &msg);
            CHECK(ok && app.db.get_int(*m, m503, "bodytypecode") == code, "manager set " + std::to_string(code) + " written on a generic head: " + msg);
            app.db.set_int(*m, m503, "bodytypecode", before);
        }
    });

    // RV6: the caption text (the picker's own Text colour) against every ground a tile name sits on, 4.5:1 (WCAG 1.4.3)
    run_case("Body picker: RV6 caption contrast - the name text against the window and the selected / hovered / pressed row is 4.5:1 or better", [&] {
        // the real Turbo theme (not whatever the harness left in the style), and the Header washes are translucent: composite them over the window
        ImGuiStyle st;
        turbo::theme::apply_turbo_theme(st);
        auto opaque = [&](ImGuiCol col) {
            const ImVec4 bg = st.Colors[ImGuiCol_WindowBg];
            ImVec4 c = st.Colors[col];
            const float a = col == ImGuiCol_WindowBg ? 1.0f : c.w;
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * a + bg.x * (1.0f - a), c.y * a + bg.y * (1.0f - a), c.z * a + bg.z * (1.0f - a), 1.0f));
        };
        ImVec4 tv = st.Colors[ImGuiCol_Text];
        tv.w = 1.0f;
        const ImU32 text = ImGui::ColorConvertFloat4ToU32(tv);
        std::string bad;
        const struct {
            ImGuiCol col;
            const char* what;
        } grounds[] = {{ImGuiCol_WindowBg, "WindowBg"}, {ImGuiCol_Header, "Header (selected)"}, {ImGuiCol_HeaderHovered, "HeaderHovered"}, {ImGuiCol_HeaderActive, "HeaderActive"}};
        for (const auto& g : grounds) {
            const float r = bodyfig::contrast_ratio(text, opaque(g.col));
            if (r < 4.5f) bad += std::string(" ") + g.what + " " + std::to_string(r);
        }
        CHECK(bad.empty(), "caption contrast below 4.5:1:" + bad);
    });

    // T4 / RV1: ONE entry point per page. The button every page calls is drawn here in a window of its own, for a player and for a
    // manager; each opens the same visual picker, every tile a figure, and the old modal is nowhere.
    run_case("Body picker: T4 the Body type button (player and manager) opens the one visual picker of figures, no modal gallery", [&] {
        for (bool manager : {false, true}) {
            const Table* t = app.db.table(manager ? "manager" : "players");
            CHECK(t && t->has("bodytypecode"), "table with bodytypecode");
            if (!t || !t->has("bodytypecode")) return;
            const uint64_t rec = manager ? app.db.find(*t, "managerid", 503) : app.db.find(*t, "playerid", 2002);
            CHECK(rec != 0, "the record exists");
            if (rec == 0) return;
            const std::string who = manager ? "manager 503" : "player 2002";
            const int64_t before = app.db.get_int(*t, rec, "bodytypecode");
            ui.extra_draw = [&app, t, rec, manager]() {
                ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(620.0f, 120.0f), ImGuiCond_Always);
                ImGui::Begin("##bpt4", nullptr, ImGuiWindowFlags_NoSavedSettings);
                bodytype_gallery_button(app, *t, rec, manager);
                ImGui::End();
            };
            ui.frames(3);
            CHECK(ui.find("Body types...##btopen", "##bpt4") == nullptr, who + ": no 'Body types...' modal button");
            CHECK(ui.find("Body type...##btopen", "##bpt4") != nullptr && ui.find("Copy body type from player...##btcopybtn", "##bpt4") != nullptr,
                  who + ": one 'Body type...' button and the copy button");
            CHECK(ui.click("Body type...##btopen", "##bpt4"), who + ": click Body type...");
            ui.frames(4);
            CHECK(visual_picker_ui().open && visual_picker_ui().field == "bodytypecode", who + ": the visual picker opened for bodytypecode");
            CHECK(!bodytype_allow_risky(), who + ": the allow-risky box starts unticked (no tick carried over from an earlier record)");
            CHECK(ui.find("Reload##btreload") == nullptr && ui.find("Use##bt4") == nullptr, who + ": the 7-column modal gallery is not on screen");
            const size_t n = visual_picker_list_size();
            CHECK(n >= 10 && static_cast<size_t>(visual_picker_ui().source_figure) == n && visual_picker_ui().source_rdbm + visual_picker_ui().source_game == 0,
                  fmt("%s: all %zu tiles are figure tiles (figure %d)", who.c_str(), n, visual_picker_ui().source_figure));
            size_t fig = 0;
            std::set<std::string> names;
            for (int64_t c : visual_picker_codes()) {
                if (visual_picker_tile_source(c) == TileSource::Figure) ++fig;
                names.insert(looks::body_caption(c, &bodytype_catalog(app), manager));
            }
            CHECK(fig == n && names.size() == n, fmt("%s: figure tiles %zu, distinct plain names %zu of %zu", who.c_str(), fig, names.size(), n));
            if (manager) {
                // the manager set writes through the picker onto a generic head (V1 through the UI path); a player-specific body does not
                visual_picker_select(82);
                CHECK(visual_picker_apply(app) && app.db.get_int(*t, rec, "bodytypecode") == 82, who + ": 82 written through the picker: " + visual_picker_ui().last_result);
                visual_picker_select(20);
                CHECK(!visual_picker_apply(app) && app.db.get_int(*t, rec, "bodytypecode") == 82, who + ": a player-specific body stays refused: " + visual_picker_ui().last_result);
            }
            CHECK(ui.click("Cancel##vpcancel", "###vispicker"), who + ": close the window");
            ui.frames(3);
            CHECK(!visual_picker_ui().open, who + ": picker closed");
            app.db.set_int(*t, rec, "bodytypecode", before);
        }
        ui.extra_draw = nullptr;
        ui.frames(2);
    });

    // 5.0c E8 (Cassio): the old modal's Generic / Specific filter lives in the visual picker (same groups, default All); and the
    // "Show all body types" tick is kept with the layout (gui_settings.json gui.body_show_all), not reset at every open.
    run_case("Body picker: E8 the Generic / Specific group filter (default All) and the saved Show all body types tick", [&] {
        auto open_picker = [&](const Table* t, uint64_t rec, bool manager) {
            ui.extra_draw = [&app, t, rec, manager]() {
                ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(620.0f, 120.0f), ImGuiCond_Always);
                ImGui::Begin("##bpe8", nullptr, ImGuiWindowFlags_NoSavedSettings);
                bodytype_gallery_button(app, *t, rec, manager);
                ImGui::End();
            };
            ui.frames(3);
            ui.click("Body type...##btopen", "##bpe8");
            ui.frames(4);
        };
        auto close_picker = [&] {
            ui.click("Cancel##vpcancel", "###vispicker");
            ui.frames(3);
        };
        auto has = [](const std::vector<int64_t>& v, int64_t c) { return std::find(v.begin(), v.end(), c) != v.end(); };
        auto choose = [&](const char* name) {  // open the group combo, click the entry
            const bool ok = ui.click("##vpgroup", "###vispicker");
            ui.frames(3);
            const bool ok2 = ui.click(name);
            ui.frames(4);
            return ok && ok2;
        };
        for (bool manager : {false, true}) {
            const Table* t = app.db.table(manager ? "manager" : "players");
            if (!t || !t->has("bodytypecode")) { CHECK(false, "table with bodytypecode"); return; }
            const uint64_t rec = manager ? app.db.find(*t, "managerid", 503) : app.db.find(*t, "playerid", 2002);
            CHECK(rec != 0, "the record exists");
            if (rec == 0) return;
            const std::string who = manager ? "manager 503" : "player 2002";
            open_picker(t, rec, manager);
            CHECK(visual_picker_ui().open && visual_picker_ui().field == "bodytypecode", who + ": the visual picker opened for bodytypecode");
            CHECK(ui.find("##vpgroup", "###vispicker") != nullptr, who + ": the Generic / Specific filter is drawn in the picker");
            const std::vector<int64_t> all = visual_picker_shown_codes();
            CHECK(all.size() == visual_picker_list_size() && all.size() >= 10, fmt("%s: the default is All (%zu of %zu shown)", who.c_str(), all.size(), visual_picker_list_size()));
            CHECK(choose("Generic only"), who + ": choose 'Generic only'");
            const std::vector<int64_t> gen = visual_picker_shown_codes();
            CHECK(choose("Player-specific only"), who + ": choose 'Player-specific only'");
            const std::vector<int64_t> spec = visual_picker_shown_codes();
            std::set<int64_t> both(gen.begin(), gen.end());
            both.insert(spec.begin(), spec.end());
            CHECK(!gen.empty() && gen.size() < all.size(), fmt("%s: 'Generic only' narrows the list (%zu of %zu)", who.c_str(), gen.size(), all.size()));
            CHECK(gen.size() + spec.size() == all.size() && both.size() == all.size(),
                  fmt("%s: generic %zu + specific %zu = all %zu, no tile in both", who.c_str(), gen.size(), spec.size(), all.size()));
            if (!manager) {
                CHECK((has(spec, 20) && !has(gen, 20)) || !has(all, 20), who + ": code 20 (player-specific) is in the Specific group only");
                // code 10 is "Style 10" (nothing names it; field_labels lists it among the player-specific ones), code 5 is a named size
                CHECK((has(spec, 10) && !has(gen, 10)) || !has(all, 10), who + ": code 10 (unnamed style) is in the Specific group only");
                CHECK((has(gen, 5) && !has(spec, 5)) || !has(all, 5), who + ": code 5 (named) is in the Generic group only");
            } else {
                CHECK(has(gen, 82) || !has(all, 82), who + ": the manager set (82) is in the Generic group");
            }
            CHECK(choose("All body types"), who + ": choose 'All body types'");
            CHECK(visual_picker_shown_codes().size() == all.size(), who + ": 'All body types' lists everything again");
            close_picker();
            open_picker(t, rec, manager);
            CHECK(visual_picker_shown_codes().size() == all.size(), who + ": a fresh open starts at All");
            close_picker();
        }

        // the saved tick: set in the picker, kept over a close / reopen and over a settings reload, then put back
        const Table* mt = app.db.table("manager");
        const uint64_t mrec = mt ? app.db.find(*mt, "managerid", 503) : 0;
        CHECK(mrec != 0, "manager 503 exists");
        if (mrec == 0) return;
        looks::body_show_all(false);
        app.prefs.body_show_all = false;
        open_picker(mt, mrec, true);
        const size_t n_default = visual_picker_list_size();
        CHECK(ui.click("Show all body types##vpallbodies", "###vispicker"), "tick Show all body types");
        ui.frames(4);
        const size_t n_all = visual_picker_list_size();
        CHECK(n_all > n_default && looks::body_show_all() && app.prefs.body_show_all, fmt("ticked: %zu tiles (was %zu), kept in the prefs", n_all, n_default));
        close_picker();
        open_picker(mt, mrec, true);
        CHECK(visual_picker_list_size() == n_all && looks::body_show_all(), fmt("reopened: the tick is still on (%zu tiles)", visual_picker_list_size()));
        close_picker();
        looks::body_show_all(false);  // a fresh process starts from the saved layout, not from the in-memory switch
        app.load_gui_settings();
        CHECK(looks::body_show_all() && app.prefs.body_show_all, "after reloading the layout file the tick is back on");
        open_picker(mt, mrec, true);
        CHECK(ui.click("Show all body types##vpallbodies", "###vispicker"), "untick Show all body types");
        ui.frames(4);
        CHECK(visual_picker_list_size() == n_default && !looks::body_show_all() && !app.prefs.body_show_all, fmt("unticked: back to %zu tiles", visual_picker_list_size()));
        close_picker();
        app.load_gui_settings();
        CHECK(!looks::body_show_all(), "the unticked state is saved too");
        ui.extra_draw = nullptr;
        ui.frames(2);
    });

    // T4 on the pages this harness cannot open: every caller of the entry point still calls it, and nothing in src/ui can open the old modal
    run_case("Body picker: T4 source scan - the five pages call the one entry point; the 'Body types##bt' modal and its 7-column rows are gone", [&] {
        namespace sfs = std::filesystem;
        const sfs::path ui_dir = sfs::path(g_lua).parent_path().parent_path().parent_path() / "src" / "ui";
        auto read = [&](const sfs::path& f) {
            std::ifstream in(f, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
        CHECK(sfs::exists(ui_dir / "ui_bodytypes.cpp"), "source folder found: " + ui_dir.string());
        for (const char* f : {"ui_player_page.cpp", "ui_players.cpp", "ui_career_page.cpp", "ui_teams.cpp", "ui_images.cpp"})
            CHECK(read(ui_dir / f).find("bodytype_gallery_button(") != std::string::npos, std::string(f) + " calls bodytype_gallery_button");
        std::string leaks;
        for (const auto& e : sfs::directory_iterator(ui_dir)) {
            if (e.path().extension() != ".cpp") continue;
            const std::string s = read(e.path());
            for (const char* needle : {"Body types##bt", "Body types...##btopen", "##btlist", "Used by players only##btused"})
                if (s.find(needle) != std::string::npos) leaks += " " + e.path().filename().string() + ":" + needle;
        }
        CHECK(leaks.empty(), "the old modal gallery survives in:" + leaks);
        const std::string b = read(ui_dir / "ui_bodytypes.cpp");
        CHECK(b.find("vp::open(app, t, rec, \"bodytypecode\", manager)") != std::string::npos && b.find("vp::window(app, t, rec)") != std::string::npos,
              "the entry point opens and draws the visual picker");
    });
}

}  // namespace body_picker_test
