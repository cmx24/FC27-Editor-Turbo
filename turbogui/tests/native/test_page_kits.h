// Native tests (2026-10-17, GUI revamp step 7, brief 2.16-gui-pages track "kits"): the tidy Kits & club pages of ui/ui_kits_page.cpp
// (draw_kits_page, draw_club_identity_page, draw_kit_diagnostics). Included by test_main.cpp after test_club_page.h (it reuses the kit
// fixtures and the kept-rows helper); page_kits_test::ui_cases runs inside the UI block, a real App, before test_shell_app clears the page
// registry. The pages are not registered with the shell (the navigation re-cut wires them in), so they are drawn through the Ui hook
// g_ui_extra in a window of their own, "##kitspage_test", as test_club_page.h does for the pages it replaces.
#pragma once
#include "ui/ui_club_page.h"
#include "ui/ui_kits.h"
#include "ui/ui_kits_page.h"

namespace page_kits_test {

using namespace turbo;
using namespace kit_test;

static void (*g_fn)(App&) = nullptr;
static bool g_focus = false;  // focus the page window once, not every frame: a popup of the page closes when its window is focused again
static void draw_extra(App& app) {
    if (!g_fn) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (g_focus) ImGui::SetNextWindowFocus();
    g_focus = false;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##kitspage_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) g_fn(app);
    ImGui::End();
}
static void show(Ui& ui, void (*fn)(App&)) {
    g_fn = fn;
    g_focus = true;
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_fn = nullptr;
    g_ui_extra = nullptr;
    ui.frames(2);
}
// The text of the page: ImGui's text log of one frame with the page's window (`all`), and the lines of it that the app alone does not draw
// (`only`: the app's own window is drawn behind the page, so a word the page must not say is searched in `only`)
struct PT {
    std::string all, only;
};
static std::string logged_text(Ui& ui, bool with_page) {
    ImGui_ImplNull_NewFrame();
    ImGui::NewFrame();
    ++g_frame;
    ImGui::LogToBuffer();
    ui.app.tick(ui.t);
    ui.app.draw();
    if (with_page && g_ui_extra) g_ui_extra(ui.app);
    std::string text = GImGui->LogEnabled ? std::string(GImGui->LogBuffer.c_str()) : std::string();
    ImGui::LogFinish();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    ui.t += 1.0 / 60.0;
    return text;
}
static std::vector<std::string> lines_of(const std::string& t) {
    std::vector<std::string> v;
    size_t b = 0;
    while (b <= t.size()) {
        size_t e = t.find('\n', b);
        if (e == std::string::npos) e = t.size();
        v.push_back(t.substr(b, e - b));
        b = e + 1;
    }
    return v;
}
static PT page_text(Ui& ui) {
    const std::string base = logged_text(ui, false);
    PT pt;
    pt.all = logged_text(ui, true);
    std::set<std::string> in_base;
    for (const std::string& l : lines_of(base)) in_base.insert(l);
    for (const std::string& l : lines_of(pt.all))
        if (!in_base.count(l)) pt.only += l + "\n";
    return pt;
}
static bool has(const std::string& text, const char* s) { return text.find(s) != std::string::npos; }

// The game's own minikit for Everton's home kit, as the Lua side exports it
static void put_game_minikit(const fs::path& le) {
    DdsFormat mf;
    mf.pixel = DdsFormat::Pixel::DXT5;
    mf.w = mf.h = 64;
    mf.mips = 7;
    write(le / "turbo_output" / "cache" / "legacy" / "data" / "ui" / "imgAssets" / "kits" / "j0_7_0.dds", encode_dds(solid(64, 64, 9, 9, 9), mf));
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    run_case("Kits page: the plain words of a slot and of the 3D line (pure)", [&] {
        CHECK(std::string(kit_slot_state_words(true)) == "Custom" && std::string(kit_slot_state_words(false)) == "Game's own", "Custom / Game's own");
        // runtime, opted, pending, staged, active_swap, watch_only
        CHECK(kit_pitch_line(false, false, false, false, false, false) == "Not available in this session.", "no game hooks this session");
        CHECK(kit_pitch_line(true, false, false, false, false, false).rfind("Off:", 0) == 0, "not switched on: Off");
        CHECK(has(kit_pitch_line(true, false, true, false, false, false), "Needs a game restart") &&
                  has(kit_pitch_line(true, false, true, false, false, false), "Tested on a few kits so far"),
              "switched on, not restarted: the restart and the honest line");
        CHECK(has(kit_pitch_line(true, true, false, false, false, false), "Apply the kit first"), "switched on, nothing applied yet");
        CHECK(has(kit_pitch_line(true, true, false, true, false, true), "watch only") && has(kit_pitch_line(true, true, false, true, true, false), "Tested on a few kits"),
              "watch only and shown");
        for (bool a : {false, true})
            for (bool b : {false, true})
                for (bool c : {false, true}) {
                    const std::string s = kit_pitch_line(true, a, b, c, false, false);
                    for (const char* bad : {"turbo_output", ".txt", "hook", "Lua", "json", "\\"}) CHECK(!has(s, bad), std::string("no developer word in: ") + s);
                }
    });

    run_case("Kits page: slot list, one main button, no developer controls, no club number, the per-match option shown as planned", [&] {
        app.kit_swap = nullptr;
        app.sel_team = 7;
        show(ui, draw_kits_page);
        // the panel is shared with the earlier cases: read one file that is no kit, so the Home slot has no kit file of its own to show
        const fs::path junk = g_out / "kits_src" / "page_kits" / "not_a_kit.zip";
        write(junk, bytes_of("this is not a zip"));
        CHECK(kit_panel_open(app, junk) && kit_wait(ui), "a file that is no kit is read");
        ui.frames(3);
        for (const char* l : {"Home##kpslot", "Away##kpslot", "Third##kpslot", "Goalkeeper##kpslot", "Fourth##kpslot"})
            CHECK(ui.find(l, "##select") != nullptr, std::string("slot in the select panel: ") + l);
        CHECK(ui.find("###pg_apply", "##kitspage_test") != nullptr, "the one main button");
        CHECK(!kits_page_state().apply_enabled, "disabled until a kit file is read");
        CHECK(ui.find("Choose kit file...##kpfile", "##editor") != nullptr && ui.find("Restore the game's kit##kprestore", "##editor") != nullptr,
              "choose a file, restore");
        CHECK(ui.find("Change club...##kpchange", "##select") != nullptr, "the club is changed from a popup");
        for (const char* gone : {"Import kit zip...", "Import team folder...", "Turn on 3D kits (restart the game afterwards)##k3on", "Show it on the pitch##kswap",
                                 "Watch only (log what the game loads)##kswap", "Textures##kswap", "Apply##kit0", "##kitzip"})
            CHECK(ui.find(gone, "##kitspage_test") == nullptr, std::string("no old control: ") + gone);
        CHECK(ui.find("##kpclub") == nullptr, "no club number box on the page itself");
        const PT pt = page_text(ui);
        for (const char* bad : {"(ID ", "ID 7", "turbo_output", ".txt", ".json", "hotspot", "kit index", "Watch only", "hook", "Lua", "channel", "texture"})
            CHECK(!has(pt.only, bad), std::string("the page does not say: ") + bad);
        CHECK(has(pt.all, "Everton") && has(pt.all, "Game's own") && has(pt.all, "Home kit"), "club name, the plain slot state, the slot title");
        CHECK(has(pt.all, "planned") && has(pt.all, "Only in my matches, when no club sharing the kit plays"), "the per-match option is shown as planned");
        CHECK(kits_page_state().planned_shown, "state: the planned option was drawn");
        CHECK(has(pt.all, "Not tested yet"), "the sharing choices say they are not tested yet");
        const ItemRec* planned = ui.find("Only in my matches, when no club sharing the kit plays##kpplanned");
        CHECK(planned != nullptr && ui.click(planned), "the planned option can be clicked and does nothing");
        ui.frames(2);
        CHECK(kits_page_state().teamid == 7 && kits_page_state().slot == kits::Slot::Home && !kits_page_state().custom, "state: Everton, Home, not custom");
        CHECK(kits_page_state().status == "The game's own kit is in use.", "state: " + kits_page_state().status);
        CHECK(!kits_page_state().pitch_tick && !kits_page_state().pitch_enabled && kits_page_state().pitch_line == "Not available in this session.",
              "without game hooks the 3D tick is off and says so: " + kits_page_state().pitch_line);
        CHECK(ui.click("Away##kpslot", "##select") && kit_page_slot() == kits::Slot::Away, "select Away");
        ui.frames(2);
        CHECK(kits_page_state().slot == kits::Slot::Away && has(page_text(ui).all, "Away kit"), "the editor follows the slot");
        kit_page_select(kits::Slot::Home);
        ui.frames(2);
        // the club popup holds the box (name first, the number only inside the list); the next show() closes it with the focus change
        CHECK(ui.click("Change club...##kpchange", "##select"), "open the club popup");
        ui.frames(2);
        CHECK(ui.find("##kpclub") != nullptr, "the popup has the club box");
        hide(ui);
    });

    run_case("Kits page: Apply kit runs the proven apply path (database, minikit, staged textures); Restore the game's kit acts on one club and slot", [&] {
        const auto& spec = small_spec();
        kit_panel_spec(&spec);
        app.kit_swap = nullptr;
        put_game_minikit(le);
        const fs::path zip = g_out / "kits_src" / "page_kits" / "h.zip";
        make_kit_zip(zip, spec);
        // Cassio's rows: another club's home kit and another slot of Everton (fixture values, removed at the end)
        app.reapply.set_kit_field(9999, 0, 9990, "Fixture FC", "2026-10-16 10:00", "teamcolorprimr", 77);
        app.reapply.set_kit_field(7, 1, 71, "Everton", "2026-10-16 10:00", "teamcolorsecr", 33);
        const std::string before = club_page_test::kept_rows(app.reapply);
        const size_t n_before = app.reapply.kits.size();
        app.sel_team = 7;
        show(ui, draw_kits_page);
        ui.frames(80);  // the exported minikit is read once a second
        CHECK(kit_panel_open(app, zip) && kit_wait(ui), "the zip is read");
        ui.frames(3);
        CHECK(kit_page_slot() == kits::Slot::Home, "the page shows the slot of the zip's kit");
        CHECK(kit_panel_count() == 1 && kit_panel_status(0) == "ready", "ready: " + kit_panel_status(0));
        CHECK(kits_page_state().status.rfind("Ready to apply: h.zip", 0) == 0 && kits_page_state().apply_enabled, "state: " + kits_page_state().status);
        CHECK(!has_kit_import(app, 7, kits::Slot::Home), "nothing imported yet");
        CHECK(ui.click("###pg_apply", "##kitspage_test"), "Apply kit");
        CHECK(kit_wait(ui), "textures converted");
        ui.frames(3);
        CHECK(has_kit_import(app, 7, kits::Slot::Home) && app.reapply.kit(7, 0, 70) != nullptr, "applied: the import record and the kept kit row exist");
        CHECK(fs::exists(kits::slot_dir(le, 7, kits::Slot::Home) / "manifest.json"), "the textures were staged: the apply path of the old page ran, not a lesser one");
        CHECK(app.reapply.kits.size() == n_before + 1, "one new kept row");
        CHECK(page::status_text("kits").rfind("Done: ", 0) == 0, "the frame's one status line says it (4.0 S9): " + page::status_text("kits"));
        const std::string txt = page_text(ui).all;
        CHECK(has(txt, "Custom") && kits_page_state().custom, "the slot list says Custom");
        CHECK(kits_page_state().status.rfind("applied to Everton", 0) == 0, "state: " + kits_page_state().status);
        ui.frames(80);  // the page re-reads the record once a second
        CHECK(kits_page_state().status.rfind("Custom kit", 0) == 0 || kits_page_state().status.rfind("applied", 0) == 0, "later: " + kits_page_state().status);
        // Restore the game's kit: this club and this slot only
        const ItemRec* restore = ui.find("Restore the game's kit##kprestore", "##editor");
        CHECK(restore != nullptr && ui.click(restore), "click Restore the game's kit");
        ui.frames(3);
        CHECK(!has_kit_import(app, 7, kits::Slot::Home) && app.reapply.kit(7, 0, 70) == nullptr, "restored: the club's own home kit row is gone");
        CHECK(club_page_test::kept_rows(app.reapply) == before, "the other kept rows (another club, another slot) are exactly as before: " + club_page_test::kept_rows(app.reapply));
        CHECK(app.reapply.kit(9999, 0, 9990) && app.reapply.kit(9999, 0, 9990)->fields.at("teamcolorprimr") == 77 &&
                  app.reapply.kit(7, 1, 71) && app.reapply.kit(7, 1, 71)->fields.at("teamcolorsecr") == 33,
              "Cassio's fixture rows survive");
        CHECK(ui.toast_contains("import undone"), "toast");
        CHECK(!kits_page_state().custom, "the slot list says Game's own again");
        // another slot: nothing imported, restore refused, its kept row stays
        CHECK(ui.click("Away##kpslot", "##select") && kit_page_slot() == kits::Slot::Away, "select Away");
        ui.frames(2);
        CHECK(!kits_page_state().custom, "Away: nothing to restore");
        std::string msg;
        CHECK(!undo_kit_import(app, 7, kits::Slot::Away, &msg) && club_page_test::kept_rows(app.reapply) == before, "restoring Away is refused and changes no row: " + msg);
        CHECK(app.reapply.forget_kit(9999, 0, 9990) && app.reapply.forget_kit(7, 1, 71), "fixture rows removed");
        kit_page_select(kits::Slot::Home);
        hide(ui);
    });

    // 4.0 S9 fix round 1 (P1): the status line of the Kits page must not say "Done" when the apply did not hold. poll_jobs replaces the
    // panel line ("textures staged...") when the worker ends, so the page reads the outcome of the database part the moment apply_items returns.
    run_case("Kits page: an Apply whose import record could not be saved, or whose textures could not be staged, never says Done; a stale item of another slot does not turn a good Apply into Couldn't", [&] {
        const auto& spec = small_spec();
        kit_panel_spec(&spec);
        app.kit_swap = nullptr;
        put_game_minikit(le);
        const fs::path dir = g_out / "kits_src" / "page_kits_fail";
        const fs::path zip = dir / "h.zip";
        make_kit_zip(zip, spec);
        make_kit_zip(dir / "a.zip", spec);
        const std::string before = club_page_test::kept_rows(app.reapply);
        const fs::path rec = kits::slot_dir(le, 7, kits::Slot::Home) / "import.json";
        fs::path tmp = rec;
        tmp += ".tmp";
        std::error_code ec;
        app.sel_team = 7;
        show(ui, draw_kits_page);
        ui.frames(80);
        // a good Apply first: its record holds the game's own values, so Restore at the end puts everything back
        CHECK(kit_panel_open(app, zip) && kit_wait(ui), "the zip is read");
        ui.frames(3);
        CHECK(kit_page_slot() == kits::Slot::Home && kit_panel_status(0) == "ready", "ready: " + kit_panel_status(0));
        CHECK(ui.click("###pg_apply", "##kitspage_test"), "Apply kit");
        CHECK(kit_wait(ui), "textures converted");
        ui.frames(3);
        CHECK(page::status_text("kits").rfind("Done: ", 0) == 0, "a good Apply says Done: " + page::status_text("kits"));
        CHECK(fs::is_regular_file(rec, ec), "the import record is there");

        // (1) the record cannot be saved: a folder stands where write_text_atomic opens its temporary file (import.json.tmp), so the write fails
        // (a folder in import.json's own place would not do: write_text_atomic removes an empty one and renames over it)
        fs::create_directories(tmp, ec);
        CHECK(fs::is_directory(tmp, ec), "a folder stands in the temporary file's place");
        CHECK(kit_panel_open(app, zip) && kit_wait(ui), "the zip is read again");
        ui.frames(3);
        CHECK(kit_panel_status(0) == "ready", "ready again: " + kit_panel_status(0));
        CHECK(ui.click("###pg_apply", "##kitspage_test"), "Apply kit, the record cannot be saved");
        CHECK(kit_wait(ui), "textures converted");
        ui.frames(3);
        CHECK(page::status_text("kits").rfind("Done", 0) != 0, "the status line does not say Done when Undo cannot restore the kit: " + page::status_text("kits"));
        CHECK(page::status_text("kits").rfind("Couldn't apply the kit: ", 0) == 0 && has(page::status_text("kits"), "was not saved"),
              "it says Couldn't, with the reason: " + page::status_text("kits"));
        fs::remove_all(tmp, ec);
        CHECK(!fs::exists(tmp, ec) && fs::is_regular_file(rec), "the folder is gone and the first record is still there");

        // (2) the textures cannot be staged: the zip is gone when the worker opens it (the database part ran from the zip read earlier)
        const fs::path gone = dir / "h.zip.gone";
        CHECK(kit_panel_open(app, zip) && kit_wait(ui), "the zip is read once more");
        ui.frames(3);
        CHECK(kit_panel_status(0) == "ready", "ready: " + kit_panel_status(0));
        fs::rename(zip, gone, ec);
        CHECK(!ec, "the zip is set aside: " + ec.message());
        CHECK(ui.click("###pg_apply", "##kitspage_test"), "Apply kit, the textures cannot be staged");
        CHECK(kit_wait(ui), "the worker ended");
        ui.frames(3);
        CHECK(page::status_text("kits").rfind("Done", 0) != 0, "a staging failure is not Done: " + page::status_text("kits"));
        CHECK(page::status_text("kits").rfind("Couldn't apply the kit: the textures were not staged", 0) == 0, "it says so: " + page::status_text("kits"));
        fs::rename(gone, zip, ec);
        CHECK(!ec, "the zip is back");

        // (3) a stale "not applied" item of another slot (Away) is not this Apply's business: Home applies cleanly and says Done
        CHECK(kit_panel_open(app, dir) && kit_wait(ui), "both zips are read");
        ui.frames(3);
        size_t away = kit_panel_count(), home = kit_panel_count();
        for (size_t i = 0; i < kit_panel_count(); ++i) {
            if (kit_panel_slot(i) == kits::Slot::Away) away = i;
            if (kit_panel_slot(i) == kits::Slot::Home) home = i;
        }
        CHECK(kit_panel_count() == 2 && away < 2 && home < 2, "an Away item and a Home item");
        if (away < 2 && home < 2) {
            apply_items(app, 999999, {away});  // a club that is not in the database: the Away item is left "not applied"
            CHECK(kit_panel_status(away).rfind("not applied", 0) == 0, "the Away item is stale: " + kit_panel_status(away));
            kit_page_select(kits::Slot::Home);
            ui.frames(3);
            CHECK(kit_panel_status(home) == "ready", "the Home item is ready: " + kit_panel_status(home));
            CHECK(ui.click("###pg_apply", "##kitspage_test"), "Apply kit for Home");
            CHECK(kit_wait(ui), "textures converted");
            ui.frames(3);
            CHECK(page::status_text("kits").rfind("Done: ", 0) == 0, "a stale item of another slot does not make a good Apply fail: " + page::status_text("kits"));
        }

        // the first Apply's record puts the club back as it was
        std::string msg;
        CHECK(undo_kit_import(app, 7, kits::Slot::Home, &msg), "restored: " + msg);
        CHECK(club_page_test::kept_rows(app.reapply) == before, "the kept rows are as before: " + club_page_test::kept_rows(app.reapply));
        kit_page_select(kits::Slot::Home);
        hide(ui);
    });

    run_case("Kits page: the one 3D tick drives the public Runtime (switch, slot, share choices) and never moves a slot by drawing", [&] {
        const auto& spec = small_spec();
        kit_panel_spec(&spec);
        put_game_minikit(le);
        const fs::path zip = g_out / "kits_src" / "page_kits" / "h.zip";
        make_kit_zip(zip, spec);
        const fs::path on = le / "turbo_output" / "kits3d_on.txt";
        std::error_code ec;
        fs::remove(on, ec);
        app.sel_team = 7;
        show(ui, draw_kits_page);
        ui.frames(80);
        CHECK(!kits_page_state().pitch_enabled && kits_page_state().pitch_line == "Not available in this session.", "no Runtime: the tick is off and disabled");
        std::unique_ptr<kitswap::Runtime> rt(new kitswap::Runtime(le, le / "no_game_here", nullptr));
        app.kit_swap = rt.get();
        ui.frames(3);
        // not switched on: the tick makes (and removes) the one switch the old button made
        CHECK(kits_page_state().pitch_enabled && !kits_page_state().pitch_tick && kits_page_state().pitch_line.rfind("Off:", 0) == 0, "not switched on: " + kits_page_state().pitch_line);
        CHECK(ui.click("Show this kit on the pitch (3D)##kp3d", "##kpmatch") && fs::exists(on), "ticking switches 3D kits on from the next game start");
        ui.frames(2);
        CHECK(kits_page_state().pitch_tick && has(kits_page_state().pitch_line, "Needs a game restart"), "the restart is said: " + kits_page_state().pitch_line);
        CHECK(ui.toast_contains("restart the game"), "toast");
        CHECK(ui.click("Show this kit on the pitch (3D)##kp3d", "##kpmatch") && !fs::exists(on), "unticking takes the switch away again");
        ui.frames(2);
        CHECK(!kits_page_state().pitch_tick, "off again");
        // switched on (as after a restart), nothing applied yet: disabled, and it says what to do first
        std::ofstream(on) << "on";
        app.kit_swap = nullptr;  // a game restart: the page meets the Runtime again with the switch already there
        ui.frames(2);
        app.kit_swap = rt.get();
        ui.frames(3);
        CHECK(rt->opted_in() && !kits_page_state().pitch_enabled && has(kits_page_state().pitch_line, "Apply the kit first"), "nothing applied: " + kits_page_state().pitch_line);
        // apply a kit
        CHECK(kit_panel_open(app, zip) && kit_wait(ui), "the zip is read");
        ui.frames(3);
        CHECK(ui.click("###pg_apply", "##kitspage_test") && kit_wait(ui), "applied");
        ui.frames(80);
        CHECK(has_kit_import(app, 7, kits::Slot::Home) && kits_page_state().pitch_enabled && !kits_page_state().pitch_tick, "applied: the tick is enabled and off");
        // a slot set to watch only is not moved by drawing the page
        std::string err;
        CHECK(rt->activate(7, 0, kitswap::Mode::Observe, &err), "watch only set by hand: " + err);
        rt->wait_idle();
        ui.frames(90);
        kitswap::Mode md = kitswap::Mode::Off;
        CHECK(rt->is_active(7, 0, &md) && md == kitswap::Mode::Observe, "drawing the page left watch only alone");
        CHECK(!kits_page_state().pitch_tick && has(kits_page_state().pitch_line, "watch only"), "and says so: " + kits_page_state().pitch_line);
        // tick: show it on the pitch
        CHECK(ui.click("Show this kit on the pitch (3D)##kp3d", "##kpmatch"), "tick");
        rt->wait_idle();
        ui.frames(3);
        CHECK(rt->is_active(7, 0, &md) && md == kitswap::Mode::Swap && kits_page_state().pitch_tick, "the slot is served (swap)");
        CHECK(kits_page_state().pitch_line == "Tested on a few kits so far, so some kits may not show on the pitch.", "the honest line: " + kits_page_state().pitch_line);
        // share choices: only share_other changes, the other options go back unchanged
        bool d = false, k = false, sc = true, so = true;
        CHECK(rt->slot_options(7, 0, &d, &k, &sc, &so) && d && k && !sc && !so, "defaults: decals on, game's badge kept, no sharing");
        CHECK(ui.click("Change their kits too##kpshare", "##kpmatch"), "Change their kits too");
        rt->wait_idle();
        CHECK(rt->slot_options(7, 0, &d, &k, &sc, &so) && d && k && !sc && so, "share_other saved, the rest unchanged");
        CHECK(ui.click("Also change my club's other kits that use the same pictures##kpshareclub", "##kpmatch"), "Also change my club's other kits");
        rt->wait_idle();
        CHECK(rt->slot_options(7, 0, &d, &k, &sc, &so) && d && k && sc && so, "share_club saved, share_other kept");
        CHECK(ui.click("Leave them alone##kpshare", "##kpmatch"), "Leave them alone");
        rt->wait_idle();
        CHECK(rt->slot_options(7, 0, &d, &k, &sc, &so) && d && k && sc && !so, "back to leaving other clubs alone");
        // the kit maker's files, folded under Numbers & colours
        CHECK(ui.click("Numbers & colours##kpcolours", "##editor"), "open Numbers & colours");
        ui.frames(2);
        CHECK(ui.click("Ignore the crest in the kit file##kpcrest", "##editor"), "ignore the crest in the kit file");
        rt->wait_idle();
        CHECK(rt->slot_options(7, 0, &d, &k, &sc, &so) && d && !k && sc && !so, "keep_crest changed alone");
        CHECK(has(page_text(ui).all, "Names and numbers use the kit maker's position"), "the plain words for the placement option");
        // untick: off
        CHECK(ui.click("Show this kit on the pitch (3D)##kp3d", "##kpmatch"), "untick");
        rt->wait_idle();
        ui.frames(2);
        CHECK(!rt->is_active(7, 0) && !kits_page_state().pitch_tick, "off");
        // clean up (Restore switches the swap off with the Runtime still attached)
        CHECK(ui.click("Restore the game's kit##kprestore", "##editor"), "restore");
        ui.frames(3);
        rt->wait_idle();
        CHECK(!has_kit_import(app, 7, kits::Slot::Home) && app.reapply.kit(7, 0, 70) == nullptr, "restored");
        app.kit_swap = nullptr;
        rt.reset();
        fs::remove(on, ec);
        kit_page_select(kits::Slot::Home);
        hide(ui);
    });

    run_case("Kits page: Club name, colours & crest has its three cards; Kit diagnostics keeps every developer control", [&] {
        app.sel_team = 7;
        show(ui, draw_club_identity_page);
        CHECK(ui.find("##nfull", "##kpname") && ui.find("##nshort", "##kpname") && ui.find("Save", "##kpname"), "Name: the form with its Save");
        CHECK(ui.find("##X", "##kpcolours", "##c") != nullptr, "Colours: the club colour pickers");
        const PT pt = page_text(ui);
        CHECK(has(pt.all, "Name") && has(pt.all, "Colours") && has(pt.all, "Crest"), "the three cards");
        CHECK(has(pt.all, "have no Undo yet"), "the honest line: name and colour changes cannot be undone yet");
        CHECK(!has(pt.only, "Home kit") && !has(pt.only, "(ID "), "no kit colours, no club number on the page");
        CHECK(!has(pt.only, "Undo last"), "no promise of an Undo that does not exist");
        show(ui, draw_kit_diagnostics);
        CHECK(ui.find("Import kit zip...", "##kitspage_test") && ui.find("Import team folder...", "##kitspage_test"), "Kit diagnostics: the old Kits body, all of it");
        hide(ui);
    });
    club_40_test::ui_cases(app, ui, le);  // test_club_40.h (4.0 S9): both pages in the page template, the Staff tab, one descriptor
    // 4.0 S9 fix round 3 (P2, found by the round-2 reviewer): "Apply all N kits" (More) is an Apply like the bar's, so it goes through the same
    // apply_and_track path: a staging failure after it is not left under an earlier "Done".
    run_case("Kits page: Apply all N kits with a textures staging failure never leaves the line at an earlier Done (the one Apply path of the bar)", [&] {
        const auto& spec = small_spec();
        kit_panel_spec(&spec);
        app.kit_swap = nullptr;
        put_game_minikit(le);
        // "Apply all" is drawn only for two ready kits and waits for the game's own minikit of every slot: Away's is set here
        const fs::path away_mk = le / "turbo_output" / "cache" / "legacy" / kits::minikit_path(7, kits::Slot::Away);
        {
            DdsFormat mf;
            mf.pixel = DdsFormat::Pixel::DXT5;
            mf.w = mf.h = 64;
            mf.mips = 7;
            write(away_mk, encode_dds(solid(64, 64, 9, 9, 9), mf));
        }
        const std::string before = club_page_test::kept_rows(app.reapply);
        const fs::path dir = g_out / "kits_src" / "page_kits_all";
        make_kit_zip(dir / "h.zip", spec);
        make_kit_zip(dir / "a.zip", spec);
        std::error_code ec;
        app.sel_team = 7;
        show(ui, draw_kits_page);
        ui.frames(80);  // the exported minikits are read once a second
        page::done("kits", "an earlier action");  // what the line says before the click
        CHECK(kit_panel_open(app, dir) && kit_wait(ui), "both zips are read");
        ui.frames(3);
        CHECK(kit_panel_count() == 2 && kit_panel_status(0) == "ready" && kit_panel_status(1) == "ready", "two ready kits: " + kit_panel_status(0) + " / " + kit_panel_status(1));
        // the zip is gone when the worker opens it (the database part ran from the zip read earlier)
        fs::rename(dir / "h.zip", dir / "h.zip.gone", ec);
        CHECK(!ec, "the zip is set aside: " + ec.message());
        CHECK(ui.click("More##kpmore", "##editor"), "open More");
        ui.frames(3);
        CHECK(ui.click("Apply all 2 kits##kpall", "##editor"), "Apply all 2 kits");
        CHECK(kit_wait(ui), "the worker ended");
        ui.frames(3);
        CHECK(page::status_text("kits").rfind("Done", 0) != 0, "after a failed Apply all the line is not an old Done: " + page::status_text("kits"));
        CHECK(page::status_text("kits").rfind("Couldn't apply the kit: the textures were not staged", 0) == 0, "it says why: " + page::status_text("kits"));
        // Apply all applied both kits to the database part: put Everton back as it was
        fs::rename(dir / "h.zip.gone", dir / "h.zip", ec);
        std::string msg;
        CHECK(undo_kit_import(app, 7, kits::Slot::Home, &msg), "Home restored: " + msg);
        CHECK(undo_kit_import(app, 7, kits::Slot::Away, &msg), "Away restored: " + msg);
        CHECK(club_page_test::kept_rows(app.reapply) == before, "the kept rows are as before: " + club_page_test::kept_rows(app.reapply));
        fs::remove(away_mk, ec);
        kit_page_select(kits::Slot::Home);
        hide(ui);
    });
}

}  // namespace page_kits_test
