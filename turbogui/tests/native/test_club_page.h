// Native tests (2026-10-16, GUI revamp track T3, GUI_REVAMP_SPEC.md 3.3): Club & Kits pages (ui/ui_club_page.cpp, the Kits page of
// ui/ui_kits.cpp). Included by test_main.cpp after test_kit_import.h (it reuses the kit fixtures); club_page_test::ui_cases runs
// inside the UI block, a real App, before test_shell_app clears the page registry. The pages are drawn through the Ui hook
// g_ui_extra (a separate window "##clubpage_test" drawn after the app's own frame).
#pragma once
#include "ui/ui_club_page.h"
#include "ui/ui_club_tools.h"
#include "ui/ui_shell.h"

namespace club_page_test {

using namespace turbo;
using namespace kit_test;

static void (*g_page_fn)(App&) = nullptr;
static void draw_extra(App& app) {
    if (!g_page_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##clubpage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_page_fn(app);
    ImGui::End();
}
static void show(Ui& ui, App& app, shell::Page p) {
    g_page_fn = shell::registered_page(p);
    g_ui_extra = draw_extra;
    (void)app;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_page_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}
// the kept kit rows as text: key + fields
static std::string kept_rows(const ReapplyStore& r) {
    std::string out;
    for (const auto& kv : r.kits) {
        out += std::to_string(kv.second.teamtechid) + "/" + std::to_string(kv.second.kittype) + "/" + std::to_string(kv.second.teamkitid) + ":";
        for (const auto& f : kv.second.fields) out += f.first + "=" + std::to_string(f.second) + ",";
        out += ";";
    }
    return out;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("Club & Kits: the three pages are registered, the slot list, the other pages' parts", [&] {
        using shell::Page;
        for (Page p : {Page::Kits, Page::Identity, Page::StadiumExtras}) CHECK(shell::registered_page(p) != nullptr, std::string("registered: ") + shell::page_label(p));
        CHECK(shell::area_of(Page::Kits) == shell::Area::Club && shell::default_page(shell::Area::Club) == Page::Kits, "Kits is the default page of Club & Kits");
        CHECK(ImGui::GetIO().DisplaySize.x >= 900.0f, "the test window is wide enough for the select panel");
        app.sel_team = 7;
        // slot list: five slots in the page's order, none imported yet
        const auto rows = kit_slot_rows(app, 7);
        CHECK(rows.size() == 5 && rows[0].slot == kits::Slot::Home && rows[1].slot == kits::Slot::Away && rows[2].slot == kits::Slot::Third &&
                  rows[3].slot == kits::Slot::Goalkeeper && rows[4].slot == kits::Slot::Fourth,
              "Home, Away, Third, Goalkeeper, Fourth");
        for (const auto& r : rows) CHECK(!r.custom && r.state == "EA", std::string("EA kit: ") + r.label);
        show(ui, app, Page::Kits);
        for (const char* l : {"Home##kslot", "Away##kslot", "Third##kslot", "Goalkeeper##kslot", "Fourth##kslot"})
            CHECK(ui.find(l, "##select") != nullptr, std::string("slot in the select panel: ") + l);
        CHECK(ui.find("Import kit zip...", "##editor") && ui.find("Import team folder...", "##editor"), "the two import buttons sit in the editor");
        CHECK(ui.find("Apply kit", "##clubpage_test") != nullptr, "the one primary action");
        CHECK(ui.find("Apply##kit0", "##clubpage_test") == nullptr, "no per-kit Apply button on the page");
        CHECK(ui.find("My club##clubmine", "##clubpage_test") != nullptr || app.bridge.state().user_team <= 0, "club row: My club");
        // Identity: name + colours (club colours only), side by side in the wide window
        show(ui, app, Page::Identity);
        CHECK(club_page_state().wide && club_page_state().teamid == 7, "Identity: wide, Everton");
        CHECK(ui.find("##nfull", "##tname") && ui.find("##nshort", "##tname") && ui.find("Save", "##tname"), "Identity: the name form with its Save");
        CHECK(ui.find("##X", "##tcolours", "##c") != nullptr, "Identity: club colour pickers");
        CHECK(ui.find("Home kit (type 0, kit id 70)", "##tcolours") == nullptr, "Identity: the kit colours live on the Kits page");
        // Stadium & extras: stadium colours, the hub with its link to Kits
        show(ui, app, Page::StadiumExtras);
        CHECK(club_take_page_request() == shell::Page::None, "no page asked yet");
        CHECK(ui.find("Open Kits##clubtoolskits", "##stadium") != nullptr, "the club tools carry a link to Kits, not a second kit entry");
        CHECK(ui.click("Open Kits##clubtoolskits", "##stadium"), "click the link");
        CHECK(club_take_page_request() == shell::Page::Kits && club_take_page_request() == shell::Page::None, "the link asks for Club & Kits > Kits once");
        CHECK(ui.find("Reopen club customisation", "##stadium") != nullptr, "the hub is still there");
        hide(ui);
    });

    run_case("Club & Kits > Kits: Apply kit, slot list, Restore EA kit acts on one club + slot and never touches other kept rows", [&] {
        const auto& spec = small_spec();
        kit_panel_spec(&spec);
        // the game's minikit for Everton's home kit (as the Lua side exports it)
        DdsFormat mf;
        mf.pixel = DdsFormat::Pixel::DXT5;
        mf.w = mf.h = 64;
        mf.mips = 7;
        write(le / "turbo_output" / "cache" / "legacy" / "data" / "ui" / "imgAssets" / "kits" / "j0_7_0.dds", encode_dds(solid(64, 64, 9, 9, 9), mf));
        const fs::path zip = g_out / "kits_src" / "club_page" / "h.zip";
        make_kit_zip(zip, spec);
        // Cassio's rows: another club's home kit and another slot of Everton (fixture values, removed at the end)
        app.reapply.set_kit_field(9999, 0, 9990, "Fixture FC", "2026-10-16 10:00", "teamcolorprimr", 77);
        app.reapply.set_kit_field(7, 1, 71, "Everton", "2026-10-16 10:00", "teamcolorsecr", 33);
        const std::string before = kept_rows(app.reapply);
        const size_t n_before = app.reapply.kits.size();
        app.sel_team = 7;
        show(ui, app, shell::Page::Kits);
        ui.frames(80);  // missing.txt / the exported minikit is read once a second
        CHECK(kit_panel_open(app, zip) && kit_wait(ui), "the zip is read");
        ui.frames(3);
        CHECK(kit_page_slot() == kits::Slot::Home, "the page shows the slot of the zip's kit");
        CHECK(kit_panel_count() == 1 && kit_panel_status(0) == "ready", "ready: " + kit_panel_status(0));
        CHECK(!has_kit_import(app, 7, kits::Slot::Home) && ui.find("Restore EA kit##kitundo", "##editor") == nullptr, "nothing to restore before the apply");
        CHECK(ui.click("Apply kit", "##clubpage_test"), "Apply kit");
        CHECK(kit_wait(ui), "textures converted");
        ui.frames(3);
        CHECK(has_kit_import(app, 7, kits::Slot::Home) && app.reapply.kit(7, 0, 70) != nullptr, "applied: the import record and the kept kit row exist");
        const auto rows = kit_slot_rows(app, 7);
        CHECK(rows[0].custom && rows[0].state == "custom *" && !rows[1].custom && !rows[3].custom, "slot list: Home custom *, the others EA");
        CHECK(app.reapply.kits.size() == n_before + 1, fmt("one new kept row (%zu -> %zu)", n_before, app.reapply.kits.size()));
        // Restore EA kit (the page's undo): this club + this slot only
        const ItemRec* restore = ui.find("Restore EA kit##kitundo", "##editor");
        CHECK(restore != nullptr, "Restore EA kit is offered for the imported slot");
        CHECK(ui.click(restore), "click Restore EA kit");
        ui.frames(3);
        CHECK(!has_kit_import(app, 7, kits::Slot::Home) && app.reapply.kit(7, 0, 70) == nullptr, "restored: the club's own home kit row is gone");
        CHECK(kept_rows(app.reapply) == before, "the other kept rows (another club, another slot) are exactly as before: " + kept_rows(app.reapply));
        CHECK(app.reapply.kit(9999, 0, 9990) && app.reapply.kit(9999, 0, 9990)->fields.at("teamcolorprimr") == 77 &&
                  app.reapply.kit(7, 1, 71) && app.reapply.kit(7, 1, 71)->fields.at("teamcolorsecr") == 33,
              "Cassio's fixture rows survive");
        CHECK(ui.toast_contains("import undone"), "toast");
        CHECK(kit_slot_rows(app, 7)[0].state == "EA", "slot list: Home is EA again");
        // another slot: nothing imported, nothing to restore, its kept row stays
        CHECK(ui.click("Away##kslot", "##select") && kit_page_slot() == kits::Slot::Away, "select Away");
        ui.frames(2);
        CHECK(ui.find("Restore EA kit##kitundo", "##editor") == nullptr, "Away: nothing to restore");
        std::string msg;
        CHECK(!undo_kit_import(app, 7, kits::Slot::Away, &msg) && kept_rows(app.reapply) == before, "restoring Away is refused and changes no row: " + msg);
        // cleanup
        CHECK(app.reapply.forget_kit(9999, 0, 9990) && app.reapply.forget_kit(7, 1, 71), "fixture rows removed");
        kit_page_select(kits::Slot::Home);
        hide(ui);
    });
}

}  // namespace club_page_test
