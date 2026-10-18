// Native tests (2026-10-17, GUI revamp step 9, brief 2.16-gui-pages track "changes"): Career > Changes and undo (ui/ui_changes.cpp).
// Included by test_main.cpp after test_club_page.h; changes_page_test::ui_cases runs inside the UI block, a real App, before
// test_shell_app clears the page registry. The page is drawn through the Ui hook g_ui_extra (a separate window "##changes_test").
//   - the list: a player edit is one row, the global Undo takes it back through App::undo; an older change of the same player waits for
//     his newer one; an undo done elsewhere drops the row; the bulk entry of a change goes with it
//   - "Can't be undone" rows (note_irreversible) are listed and never offered to the global Undo
//   - the kit-import look (a record file of the club's slot): a row for an import seen to appear, none for a record that was already there
//   - Remembered for every career load: "Stop doing this" and the scope buttons ask first; cancel changes nothing; confirm does the work
//   - the page: the one primary button reads "Undo: <what>" and works; the other pages' undo buttons open their page
// Every case runs on an emptied undo stack / bulk list / kept-edit store (put back afterwards) so the cases do not depend on what the
// cases before them left behind.
#pragma once
#include "ui/ui_changes.h"
#include "ui/ui_shell.h"

namespace changes_page_test {

using namespace turbo;

static void draw_extra(App& app) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // focusing this window closes every open popup (ImGui::FocusWindow -> ClosePopupsOverWindow), so not while a confirm is open
    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##changes_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) changes::draw_changes_page(app);
    ImGui::End();
}
static void show(Ui& ui) {
    g_ui_extra = draw_extra;
    ui.frames(3);
}
static void hide(Ui& ui) {
    g_ui_extra = nullptr;
    ui.frames(2);
}

// The state a case may change, emptied for the case and put back after it
struct Sandbox {
    App& app;
    Ui& ui;
    std::map<int64_t, std::deque<App::UndoStep>> undo;
    std::vector<App::BulkUndo> bulk;
    ReapplyStore reapply;
    shell::Place place;
    int64_t sel_team;
    Sandbox(App& a, Ui& u) : app(a), ui(u), undo(a.undo_), bulk(a.bulk_undo), reapply(a.reapply), place(a.place()), sel_team(a.sel_team) {
        app.undo_.clear();
        app.bulk_undo.clear();
        app.reapply = ReapplyStore();
        app.sel_team = 0;
        changes::reset_for_tests();
    }
    ~Sandbox() {
        g_ui_extra = nullptr;
        app.undo_ = undo;
        app.bulk_undo = bulk;
        app.reapply = reapply;
        app.sel_team = sel_team;
        app.save_kept_edits();
        if (place.valid()) app.go(place.area, place.page);
        ui.frames(3);
        changes::reset_for_tests();
    }
};

// One Int field of a player written through the same path as the sliders (an undo step, a kept edit)
static bool write_int(App& app, int64_t pid, const char* field, int64_t v) {
    const Table* p = app.db.table("players");
    if (!p) return false;
    const uint64_t rec = app.db.find(*p, "playerid", pid);
    const Field* f = p->field(field);
    return rec && f && app.edit_field(*p, rec, *f, Value::of_int(v));
}
static int64_t read_int(App& app, int64_t pid, const char* field) {
    const Table* p = app.db.table("players");
    return p ? app.db.get_int(*p, app.db.find(*p, "playerid", pid), field) : -1;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    // outside run_case, so it also runs when TURBO_TEST_ONLY skips the case that connects the app ("UI: connects and every main tab renders")
    if (!app.connected()) ui.frames(5);
    run_case("Changes and undo: a player edit is one row; the global Undo takes it back, the older change of the same player waits", [&] {
        Sandbox sb(app, ui);
        changes::poll(app, true);
        CHECK(changes::rows().empty() && changes::last_undoable(app) == nullptr && changes::undo_label(app) == "Nothing to undo", "nothing to undo to begin with");
        const int64_t pot0 = read_int(app, 1002, "potential");
        CHECK(write_int(app, 1002, "potential", pot0 == 70 ? 71 : 70), "write the potential");
        changes::poll(app, true);
        CHECK(changes::rows().size() == 1, fmt("one row (%zu)", changes::rows().size()));
        const changes::Change& c = changes::rows().front();
        CHECK(c.source == changes::Source::PlayerEdit && c.how == changes::How::Undo && c.id == 1002 && c.count == 1, "a player edit, undoable, one step");
        CHECK(c.what.find(" for ") != std::string::npos, "plain words: " + c.what);
        CHECK(changes::undo_label(app).find("Undo: ") == 0 && changes::undo_label(app).size() <= 28, "the global label is short: " + changes::undo_label(app));
        // a second change of the same player: the older row waits for it
        const uint64_t first_seq = c.seq;
        const int64_t pot1 = read_int(app, 1002, "potential");
        CHECK(write_int(app, 1002, "potential", pot1 == 80 ? 81 : 80), "write it again");
        changes::poll(app, true);
        CHECK(changes::rows().size() == 2, "two rows");
        const changes::Change* older = nullptr;
        for (const auto& r : changes::rows())
            if (r.seq == first_seq) older = &r;
        CHECK(older != nullptr && !changes::can_undo_row(app, *older), "the older change cannot be undone before the newer one");
        std::string msg;
        CHECK(!changes::undo_row(app, first_seq, &msg) && !msg.empty(), "refused with a reason: " + msg);
        CHECK(app.undo_count(1002) == 2, "nothing was undone by the refusal");
        CHECK(changes::undo_last(app, &msg) && read_int(app, 1002, "potential") == pot1, "the global Undo takes the newer one back: " + msg);
        CHECK(changes::rows().size() == 1, "its row is gone");
        CHECK(changes::undo_last(app, &msg) && read_int(app, 1002, "potential") == pot0, "then the older one: " + msg);
        CHECK(changes::rows().empty() && app.undo_count(1002) == 0, "the list and the player's stack are empty again");
    });

    run_case("Changes and undo: an undo done on the player page drops its row; a bulk entry goes with the change", [&] {
        Sandbox sb(app, ui);
        changes::poll(app, true);
        const int64_t pot0 = read_int(app, 1002, "potential");
        CHECK(write_int(app, 1002, "potential", pot0 == 60 ? 61 : 60), "write");
        changes::poll(app, true);
        CHECK(changes::rows().size() == 1, "a row");
        CHECK(app.undo(1002), "the player page's Undo");
        changes::poll(app, true);
        CHECK(changes::rows().empty(), "the row left with it");
        // a change that also keeps a bulk entry (overall +/-1, an archetype): the entry belongs to the row
        CHECK(write_int(app, 1002, "potential", pot0 == 62 ? 63 : 62), "write again");
        App::BulkUndo b;
        b.playerid = 1002;
        b.label = "test";
        b.before.push_back({"potential", pot0});
        app.push_bulk_undo(b);
        changes::poll(app, true);
        CHECK(changes::rows().size() == 1 && changes::rows().front().bulk == 1 && app.bulk_undo.size() == 1, "the row counts the bulk entry");
        std::string msg;
        CHECK(changes::undo_last(app, &msg), "undo: " + msg);
        CHECK(app.bulk_undo.empty() && read_int(app, 1002, "potential") == pot0, "the bulk entry went with it, the value is back");
    });

    run_case("Changes and undo: Can't be undone rows are listed and never offered to the global Undo", [&] {
        Sandbox sb(app, ui);
        changes::poll(app, true);
        changes::note_irreversible(app, "Released Jamie Test", "He is a free agent now.");
        CHECK(changes::rows().size() == 1 && changes::rows().front().how == changes::How::Never, "a Never row");
        CHECK(!changes::can_undo_row(app, changes::rows().front()), "not undoable");
        CHECK(changes::last_undoable(app) == nullptr && changes::undo_label(app) == "Nothing to undo", "the global Undo has nothing to offer");
        std::string msg;
        CHECK(!changes::undo_row(app, changes::rows().front().seq, &msg) && msg.find("cannot be undone") != std::string::npos, "refused: " + msg);
    });

    run_case("Changes and undo: a kit imported while Turbo is open is a row; a record that was already there never is", [&] {
        Sandbox sb(app, ui);
        const fs::path rec = kits::slot_dir(le, 7, kits::Slot::Away) / "import.json";
        std::error_code ec;
        fs::remove(rec, ec);
        // 1. an import from an earlier session: the club comes into view (the Kits page opens on it) after the record is there
        changes::poll(app, true);
        fs::create_directories(rec.parent_path(), ec);
        { std::ofstream f(rec, std::ios::binary); f << "{}\n"; }
        app.sel_team = 7;
        changes::poll(app, true);
        CHECK(changes::rows().empty() && changes::last_undoable(app) == nullptr, "an old import is not a change: no row, and the global Undo has nothing");
        fs::remove(rec, ec);
        changes::poll(app, true);
        CHECK(changes::rows().empty(), "the record going away adds nothing");
        // 2. an import seen to appear
        fs::create_directories(rec.parent_path(), ec);
        { std::ofstream f(rec, std::ios::binary); f << "{}\n"; }
        changes::poll(app, true);
        CHECK(changes::rows().size() == 1, "a kit row appeared");
        const changes::Change& c = changes::rows().front();
        CHECK(c.source == changes::Source::KitImport && c.how == changes::How::Undo && c.id == 7 && c.what.find("Away kit for") == 0, "Away kit row: " + c.what);
        fs::remove(rec, ec);
        changes::poll(app, true);
        CHECK(changes::rows().empty(), "the row went when the import record did (restored on the Kits page)");
    });

    run_case("Changes and undo: Remembered for every career load asks first (Stop doing this, the scope buttons)", [&] {
        Sandbox sb(app, ui);
        changes::poll(app, true);
        // a fixture callname of a player id nobody has
        app.reapply.set_callname_in(kScopeGlobal, 991001, 900050, "Fixture Player", "test", "2026-10-17 10:00");
        const std::string before = reapply_json(app.reapply);
        show(ui);
        const ItemRec* stop = ui.find("###chg_stop_callname", "##changes_test");
        CHECK(stop != nullptr, "the row has Stop doing this");
        CHECK(ui.click(stop), "click Stop doing this");
        ui.frames(3);
        CHECK(reapply_json(app.reapply) == before, "nothing is forgotten by the click alone");
        const ItemRec* no = ui.find("###askno");
        CHECK(no != nullptr && ui.find("###askyes") != nullptr, "the confirm is on screen with its two buttons");
        CHECK(ui.click(no), "Cancel");
        ui.frames(3);
        CHECK(reapply_json(app.reapply) == before && app.reapply.callname(991001) != nullptr, "cancel changes nothing");
        CHECK(ui.find("###askyes") == nullptr, "the confirm closed");
        // the scope buttons ask first as well: the row is global, so "This career" is the one that can be clicked once a career is known
        app.reapply.career = "00112233445566ff";
        ui.frames(3);
        const ItemRec* here = ui.find("###chg_scope_here", "##changes_test");
        CHECK(here != nullptr && ui.click(here), "click This career");
        ui.frames(3);
        CHECK(reapply_json(app.reapply) == before, "nothing moved by the click alone");
        CHECK(ui.find("###askyes") != nullptr, "the move asks first");
        CHECK(ui.click(ui.find("###askno")), "Cancel the move");
        ui.frames(3);
        CHECK(app.reapply.callname(991001) && app.reapply.callname(991001)->scope == kScopeGlobal, "still global");
        // confirm Stop doing this
        stop = ui.find("###chg_stop_callname", "##changes_test");
        CHECK(ui.click(stop), "click Stop doing this again");
        ui.frames(3);
        const ItemRec* yes = ui.find("###askyes");
        CHECK(yes != nullptr && ui.click(yes), "confirm");
        ui.frames(3);
        CHECK(app.reapply.callname(991001) == nullptr, "now the callname is no longer kept");
        hide(ui);
    });

    run_case("Changes and undo: the page, its one primary button, and the links to the other pages' undo buttons", [&] {
        Sandbox sb(app, ui);
        changes::poll(app, true);
        show(ui);
        const ItemRec* big = ui.find("###changes_undo", "##changes_test");
        CHECK(big != nullptr, "the primary button is there");
        CHECK(ui.find("Open page###chg_open_page", "##changes_test") != nullptr, "the list of other pages' undo buttons has links");
        // an edit: the button names it and works
        const int64_t pot0 = read_int(app, 1002, "potential");
        CHECK(write_int(app, 1002, "potential", pot0 == 64 ? 65 : 64), "write");
        ui.frames(3);
        CHECK(changes::rows().size() == 1 && changes::undo_label(app).find("Undo: ") == 0, "the label names the change: " + changes::undo_label(app));
        big = ui.find("###changes_undo", "##changes_test");
        CHECK(big != nullptr && ui.click(big), "click the primary button");
        ui.frames(3);
        CHECK(read_int(app, 1002, "potential") == pot0, "the value is back");
        CHECK(ui.toast_contains("Undone:"), "the status line says what was undone");
        // a link opens the page that holds the Undo
        const ItemRec* link = ui.find("Open page###chg_open_page", "##changes_test");
        CHECK(link != nullptr && ui.click(link), "click the first link");
        ui.frames(3);
        CHECK(app.place() == shell::place_of(shell::Page::TacticPresets), "the Tactic presets page opened");
        hide(ui);
    });
}

}  // namespace changes_page_test
