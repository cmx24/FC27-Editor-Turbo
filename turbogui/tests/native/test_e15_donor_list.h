// Native tests (5.0c E15, Cassio 10-30: "team audio ... selection seems short - also dig for hidden and custom team names to pick from",
// INSTRUCTIONS.md:341; briefs\5.0c\E15-teamaudio.md Part A + Verifier). The Club & crest > Identity > "Commentary name" card lists the clubs
// the loaded commentary language has a recording for. Behavioural: the cases draw the real page (club_40_test::show) with a clubs file
// written under the test LE root, so the old card (a 180 px list, a donor with no teams row shown as "team N", one "clubs listed" line)
// FAILS them by assertion, not by a compile error. The pure helpers are behind TURBO_E15_DONOR_LABEL (core/club_voice.h) so this file builds
// on the old tree too.
//   a  the list is a fixed number of rows tall (>= 13), the same on every frame (never GetContentRegionAvail: the card is AutoResizeY)
//   b  a recorded club with no row in the teams table shows the readable name of the fallback map (Live Editor custom_team_names.csv),
//      else "Club <id> (not in this database)", never "team <id>"; the search finds it by that name
//   c  the count line says how many clubs the language has a recording for and how many are shown; the two special ids 999997 and
//      9999990 are listed only behind "Show special ids"; the 800001-800112 block stays listed
//   d  Save keeps the same readable name in the store (Entry::from)
#pragma once
#include <cstring>

namespace e15_test {

using namespace turbo;

static const char* kLang = "eng_e15";  // its own name: the picker caches the list per language

// The window of the donor list child
static ImGuiWindow* list_window() {
    for (ImGuiWindow* w : GImGui->Windows)
        if (w && w->Active && std::strstr(w->Name, "##cvlist")) return w;
    return nullptr;
}

// The rows of the list drawn in the current frame (labels ending "##cv<digits>")
static int rows_drawn() {
    int n = 0;
    for (const auto& kv : g_items) {
        const ItemRec& r = kv.second;
        if (r.frame != g_frame || r.window.find("##cvlist") == std::string::npos) continue;
        const size_t p = r.label.rfind("##cv");
        if (p == std::string::npos || p + 4 >= r.label.size()) continue;
        if (r.label.find_first_not_of("0123456789", p + 4) != std::string::npos) continue;
        ++n;
    }
    return n;
}

// Scroll the Identity tab with the mouse wheel until the item is on screen. The cards are AutoResizeY children: one scrolled out of view is not
// submitted at all (BeginChild false), so nothing of the Commentary name card exists until it has been scrolled to, as a user does.
static bool reveal(Ui& ui, const char* label) {
    for (int i = 0; i < 40; ++i) {
        if (ui.find(label)) return true;
        ImGuiWindow* idw = nullptr;
        for (ImGuiWindow* w : GImGui->Windows)
            if (w && w->Active && std::strstr(w->Name, "##identity") && (!idw || std::strlen(w->Name) < std::strlen(idw->Name))) idw = w;
        if (!idw) return false;
        ImGui::GetIO().AddMousePosEvent(idw->Pos.x + 3.0f, idw->Pos.y + idw->Size.y * 0.5f);
        ui.frame();
        ImGui::GetIO().AddMouseWheelEvent(0.0f, -3.0f);
        ui.frames(3);
    }
    return ui.find(label) != nullptr;
}

static void write_clubs_file(App& app, const std::vector<int>& ids) {
    const fs::path dir = app.bridge.root() / "turbo" / "callnames" / "clubs";
    std::error_code ec;
    fs::create_directories(dir, ec);
    json j;
    j["lang"] = kLang;
    j["ids"] = ids;
    std::ofstream((dir / (std::string(kLang) + ".json")).string()) << j.dump();
}

static void ui_cases(App& app, Ui& ui, const fs::path& /*le*/) {
    ui.frames(3);
    if (!app.connected()) app.refresh();
    ui.frames(3);

    // 20 clubs the test database does not hold, the 800xxx block, and the two special ids
    std::vector<int> ids;
    for (int i = 1001; i <= 1020; ++i) ids.push_back(i);
    ids.push_back(800001);
    ids.push_back(999997);
    ids.push_back(9999990);

    // Open the page for club 7 with the language and the clubs file; the card scrolled into view
    auto open_card = [&]() {
        app.sel_team = 7;
        write_clubs_file(app, ids);
        auto fb = std::make_shared<NameMap>();
        (*fb)[1001] = "Test Rovers";
        app.model.set_team_name_fallback(fb);
        app.callnames.lang = kLang;
        club_40_test::show(ui, draw_club_identity_page);
        for (int k = 0; k < 4; ++k) {
            app.callnames.lang = kLang;
            ui.frames(2);
        }
        reveal(ui, "Save##cvsave");
        ui.frames(2);
    };

    run_case("E15 donor list a: the list is a fixed >= 13 rows tall and the same on every frame (not the old 180 px, not growing with the card)", [&] {
        open_card();
        CHECK(app.model.team(1001) == nullptr, "the test database has no club 1001 (a donor without a teams row)");
        ImGuiWindow* w = list_window();
        CHECK(w != nullptr, "the donor list child is drawn");
        if (w) {
            const ImGuiStyle& st = ImGui::GetStyle();
            const float line = GImGui->FontSize + st.ItemSpacing.y;
            const float first = w->Size.y;
            const float rows = (first - 2.0f * st.WindowPadding.y) / line;
            CHECK(rows >= 13.0f, fmt("the list shows %.1f rows' height (old fixed 180 px: ~7)", rows));
            bool stable = true;
            for (int k = 0; k < 6; ++k) {
                ui.frame();
                ImGuiWindow* w2 = list_window();
                stable = stable && w2 && std::fabs(w2->Size.y - first) < 0.5f;
            }
            CHECK(stable, fmt("the height stays %.1f px over 6 more frames (a GetContentRegionAvail height grows each frame inside an AutoResizeY card)", first));
        }
        CHECK(rows_drawn() >= 12, fmt("at least 12 rows are drawn (%d)", rows_drawn()));
        club_40_test::hide(ui);
    });

    run_case("E15 donor list b: a recorded club with no teams row shows the fallback-map name, else 'Club <id> (not in this database)', never 'team N'", [&] {
        open_card();
        CHECK(ui.find("Test Rovers (1001)##cv1001") != nullptr, "club 1001 is listed by its readable name from the fallback map");
        CHECK(ui.find("Club 1002 (not in this database) (1002)##cv1002") != nullptr || ui.find("Club 1002 (not in this database)##cv1002") != nullptr,
              "club 1002 (no name anywhere) is listed as not in this database");
        const std::string txt = club_40_test::page_text(ui);
        CHECK(!club_40_test::has(txt, "team 1001") && !club_40_test::has(txt, "team 1002"), "no row reads 'team <id>'");
        // the search looks at the readable name too
        const ItemRec* box = ui.find("##cvsearch");
        CHECK(box != nullptr, "the search box is drawn");
        if (box) {
            ui.type_into(box, "rovers");
            ui.frames(2);
            CHECK(ui.find("Test Rovers (1001)##cv1001") != nullptr && ui.find("Club 1002 (not in this database)##cv1002") == nullptr &&
                      ui.find("Club 1002 (not in this database) (1002)##cv1002") == nullptr,
                  "searching 'rovers' finds club 1001 by its fallback name and drops the others");
            ui.type_into(ui.find("##cvsearch"), "");
            ui.frames(2);
        }
        club_40_test::hide(ui);
    });

    run_case("E15 donor list c: the count line, and the two special ids behind 'Show special ids' (800xxx block stays listed)", [&] {
        open_card();
        std::string txt = club_40_test::page_text(ui);
        CHECK(club_40_test::has(txt, "23 clubs have a recording in the eng_e15 commentary (showing 21)"), "count line, special ids hidden");
        const ItemRec* chk = ui.find("Show special ids##cvspecial");
        CHECK(chk != nullptr, "the 'Show special ids' checkbox is drawn (the language holds special ids)");
        if (chk) {
            ui.click(chk);
            ui.frames(2);
            txt = club_40_test::page_text(ui);
            CHECK(club_40_test::has(txt, "23 clubs have a recording in the eng_e15 commentary (showing 23)"), "count line after ticking the box");
            ui.click("Show special ids##cvspecial");
            ui.frames(2);
            txt = club_40_test::page_text(ui);
            CHECK(club_40_test::has(txt, "(showing 21)"), "unticked again: back to 21 (the cache key holds the box)");
        }
        club_40_test::hide(ui);
    });

    run_case("E15 donor list d: Save keeps the same readable name in the store", [&] {
        open_card();
        const ItemRec* row = ui.find("Test Rovers (1001)##cv1001");
        CHECK(row != nullptr, "the row is there");
        if (row) {
            ui.click(row);
            ui.frames(2);
            CHECK(ui.click("Save##cvsave"), "Save pressed");
            ui.frames(2);
            const clubvoice::Entry* e = app.club_voice.find_in_effect(7, app.reapply.career);
            const std::string scope = e ? e->scope : std::string();
            CHECK(e != nullptr && e->voice_of == 1001 && e->from == "Test Rovers", "the store says: from '" + (e ? e->from : std::string("-")) + "'");
            if (e) app.club_voice_forget(7, scope);
            CHECK(app.club_voice.find_in_effect(7, app.reapply.career) == nullptr, "the test entry is removed again");
        }
        club_40_test::hide(ui);
    });

#ifdef TURBO_E15_DONOR_LABEL
    run_case("E15 donor list e: donor_label and the list height, pure", [&] {
        using namespace clubvoice;
        CHECK(donor_label(123456, "", "Foo FC") == "Foo FC", "no row: the fallback name");
        CHECK(donor_label(123456, "", "") == "Club 123456 (not in this database)", "no name anywhere");
        CHECK(donor_label(1, "Arsenal", "Other") == "Arsenal", "a row wins");
        CHECK(donor_label(0, "x", "y") == "No club name", "id 0");
        CHECK(is_special_id(999997) && is_special_id(9999990) && !is_special_id(800001) && !is_special_id(1), "special ids");
        CHECK(donor_list_height(21.0f, 8.0f) >= 13.0f * 21.0f + 16.0f, "height = rows * line + padding");
    });
#else
    run_case("E15 donor list e: donor_label and the list height, pure", [&] { CHECK(false, "no clubvoice::donor_label in this tree"); });
#endif
}

}  // namespace e15_test
