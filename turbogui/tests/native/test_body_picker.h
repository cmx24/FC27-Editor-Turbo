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
//   V1  apply_bodytype writes the manager set 81-89 on a manager with a generic head (the game itself pairs them: the census
//       generic_head column), and still refuses a player-specific body (code 20) there.  FAILS on d197d851 (it was refused).
#pragma once
#include <regex>

#include "core/bodytype_catalog.h"
#include "ui/ui_bodytypes.h"
#include "ui/ui_body_figure.h"
#include "core/body_shape.h"
#include "ui/ui_looks_studio.h"

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
        const ImGuiStyle& st = ImGui::GetStyle();
        ImVec4 tv = st.Colors[ImGuiCol_Text];
        tv.w = 1.0f;
        const ImU32 text = ImGui::ColorConvertFloat4ToU32(tv);
        std::string bad;
        const struct {
            ImGuiCol col;
            const char* what;
        } grounds[] = {{ImGuiCol_WindowBg, "WindowBg"}, {ImGuiCol_Header, "Header (selected)"}, {ImGuiCol_HeaderHovered, "HeaderHovered"}, {ImGuiCol_HeaderActive, "HeaderActive"}};
        for (const auto& g : grounds) {
            ImVec4 c = st.Colors[g.col];
            c.w = 1.0f;
            const float r = bodyfig::contrast_ratio(text, ImGui::ColorConvertFloat4ToU32(c));
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
