// 5.0b-C (Cassio 10-29 23:00, "fix cluttered layouts with a better UX/UI", screenshot of Choose a real face): the chooser's first row
// is a search box, a Filters (n) button and the scope, nothing else; every filter of the old header is inside the Filters popover;
// the search box has the keyboard focus when the chooser opens; a click on a tile selects it, and the bar under the grid holds
// What to copy, Apply, His own head and Close, all on screen. Behavioural: the cases drive the real chooser, so the old tree (a
// header of 6 controls, tile click applies at once) FAILS them by assertion, not by a compile error.
// Included by test_main.cpp after the Ui driver; the UI case runs from test_ui() (fcl_ui_cases), the pure one from the run list.
#pragma once
#if __has_include("ui/ui_faces_layout.h")
#include "ui/ui_faces_layout.h"
#define TURBO_HAS_CHOOSER_LAYOUT 1
#endif

// Every filter widget of the old header, by the label it is drawn with (the facet buttons are the ones the existing filter tests click)
static const char* const kFclOldFilters[] = {"Real faces only", "High-quality heads only", "Sort##facesort", "Hair colour: Any##facet2",
                                              "Gender: Any##facet7", "Facial hair: Any##facet4", "Skin tone: Any##facet1", "Hair: Any##facet3"};

// Items of one window whose box overlaps the box of `row` (same row of the layout)
static int fcl_count_in_row(const std::string& window, const ItemRec& row, std::string* names = nullptr) {
    int n = 0;
    for (const auto& kv : g_items) {
        const ItemRec& r = kv.second;
        if (r.frame != g_frame || r.window != window) continue;
        if (r.rect.GetWidth() <= 0 || r.rect.GetHeight() <= 0) continue;
        if (r.rect.GetHeight() > 100.0f) continue;  // the window itself and its two resize borders are not controls (a control is <= 60 px tall)
        if (r.rect.Max.y <= row.rect.Min.y + 1.0f || r.rect.Min.y >= row.rect.Max.y - 1.0f) continue;
        ++n;
        if (names) *names += " [" + r.label + " @" + std::to_string(int(r.rect.Min.x)) + "," + std::to_string(int(r.rect.Min.y)) + " " +
                             std::to_string(int(r.rect.GetWidth())) + "x" + std::to_string(int(r.rect.GetHeight())) + "]";
    }
    return n;
}

// The Filters popover open: click the button when a filter of the popover is not on screen
static bool fcl_open_filters(Ui& ui, const std::string& win = "Choose a real face") {
    if (ui.find("Real faces only") || ui.find("Player heads") || ui.find("Manager heads")) return true;
    const bool ok = ui.click("###facefilters", win);
    ui.frames(3);
    return ok;
}

// The Filters popover closed with its Done button (a click on the chooser's own buttons while it is open may only close the popover)
static void fcl_close_filters(Ui& ui) {
    if (ui.find("Done##facefilters")) {
        ui.click("Done##facefilters");
        ui.frames(2);
    }
}

// 5.0b-C: a click on a tile only SELECTS it; Apply writes. `apply` = "Apply##faces" (player chooser) or "Apply##mfaces" (manager chooser).
static bool fcl_pick(Ui& ui, const std::string& tile, const std::string& win, const char* apply = "Apply##faces") {
    fcl_close_filters(ui);
    if (!ui.click(tile, win)) return false;
    ui.frames(2);
    return ui.click(apply);
}

// Close the chooser (the first click may only close the popover that is open over it)
static void fcl_close_chooser(Ui& ui, const char* close_label) {
    for (int k = 0; k < 3; ++k) {
        if (!ui.find(close_label)) break;
        ui.click(close_label);
        ui.frames(3);
    }
}

static void test_face_chooser_layout() {
    run_case("test_face_chooser_layout (e0): the ChooserLayout table has 3 controls in row 1 and lists every old filter", [] {
#ifdef TURBO_HAS_CHOOSER_LAYOUT
        using namespace chooser_layout;
        CHECK(zone_count(Zone::Row1) == 3, fmt("row 1 has %d controls", zone_count(Zone::Row1)));
        for (const char* l : kFclOldFilters) CHECK(popover_lists(l), std::string("the Filters popover lists ") + l);
        for (const char* l : {"Only heads with a 3D look", "Clear filters", "Player heads", "Manager heads"})
            CHECK(popover_lists(l), std::string("the Filters popover lists ") + l);
        const std::vector<Control> want = {Control::Search,     Control::FiltersButton, Control::Scope,      Control::Tiles,
                                           Control::WhatToCopy, Control::Apply,         Control::HisOwnHead, Control::Close};
        CHECK(tab_order() == want, "Tab order: search, Filters, scope, tiles, What to copy, Apply, His own head, Close");
#else
        CHECK(false, "no ChooserLayout descriptor (ui/ui_faces_layout.h) in this tree: the chooser header is 6+ loose controls");
#endif
    });
}

static void fcl_ui_cases(App& app, Ui& ui) {
    run_case("test_face_chooser_layout (e): row 1 = search + Filters + scope; the old filters inside the popover; focus on open; a click selects", [&] {
        const std::string W = "Choose a real face";
        app.request_tab = 0;
        ui.frames(2);
        CHECK(ui.click("3001", "##plist"), "player 3001");
        CHECK(ui.click("Appearance", "##pedit"), "Appearance tab");
        CHECK(ui.click("Choose a real face..."), "open the chooser");
        ui.frames(3);
        const ItemRec* search = ui.find("##facesearch", W);
        CHECK(search != nullptr, "the search box is on screen");
        if (!search) {
            fcl_close_chooser(ui, "Close##faces");
            return;
        }
        // focus on open: the keyboard is in the search box, no click needed
        CHECK(GImGui->ActiveId == search->id, "the search box has the keyboard focus when the chooser opens");
        // row 1: exactly three controls (search, Filters, scope)
        std::string row_names;
        const int n = fcl_count_in_row(W, *search, &row_names);
        CHECK(n == 3, fmt("row 1 holds %d controls (want 3: search, Filters, scope):", n) + row_names);
        const ItemRec* fb = ui.find("###facefilters", W);
        CHECK(fb != nullptr && fb->label.compare(0, 9, "Filters (") == 0, "a Filters (n) button");
        CHECK(fb && fb->rect.Min.x > search->rect.Max.x - 1.0f, "Filters sits after the search box on the same row");
        // the old filters are not on the chooser itself ...
        for (const char* l : kFclOldFilters) CHECK(!ui.find(l), std::string("not on the chooser before Filters is opened: ") + l);
        // ... they are inside the popover, all of them
        CHECK(fcl_open_filters(ui), "open the Filters popover");
        for (const char* l : kFclOldFilters) CHECK(ui.find(l) != nullptr, std::string("inside the Filters popover: ") + l);
        CHECK(ui.find("Clear filters") != nullptr, "Clear filters inside the Filters popover");
        // a click on a tile selects it (the old chooser applied it at once); the bar under the grid has the four controls on screen
        const Table* t = app.db.table("players");
        const uint64_t rec = app.db.find(*t, "playerid", 3001);
        const int64_t head0 = app.db.get_int(*t, rec, "headassetid");
        const ItemRec* tile = ui.find("face1003", W);
        CHECK(tile != nullptr, "tile face1003");
        if (tile) ui.click("face1003", W);
        ui.frames(3);
        CHECK(app.db.get_int(*t, rec, "headassetid") == head0, "a tile click only selects: nothing is written until Apply");
        CHECK(ui.find("##whatcopy", W) != nullptr, "What to copy is on screen under the selected tile");
        CHECK(ui.find("Apply##faces", W) != nullptr, "Apply is on screen");
        CHECK(ui.find("His own head##faces", W) != nullptr, "His own head is on screen, next to Apply");
        CHECK(ui.find("Close##faces", W) != nullptr, "Close is on screen");
        const ItemRec* ap = ui.find("Apply##faces", W);
        const ItemRec* own = ui.find("His own head##faces", W);
        CHECK(ap && own && std::fabs(ap->rect.GetCenter().y - own->rect.GetCenter().y) < ap->rect.GetHeight(), "His own head is on Apply's row");
        CHECK(ap && ap->rect.GetHeight() >= 40.0f, fmt("Apply is a big target (%.0f px)", ap ? ap->rect.GetHeight() : 0.0f));
        fcl_close_chooser(ui, "Close##faces");
        CHECK(app.db.get_int(*t, rec, "headassetid") == head0, "closing writes nothing");
    });

    // 5.0b-C: the one "What to copy" control decides what Apply writes (it replaced the Eyes / Skin boxes): Head only keeps his eye colour, Head + eyes + skin
    // takes the giver's. Behavioural: the old tree has no such control (its boxes were ticked by hand), so this fails there by assertion too.
    run_case("test_face_chooser_layout (f): What to copy: Head only keeps his eyes, Head + eyes + skin copies the giver's, the third entry adds the body type", [&] {
        const std::string W = "Choose a real face";
        const Table* pt = app.db.table("players");
        const PlayerRow* y = app.model.player(3001);
        const PlayerRow* x = app.model.player(1003);
        CHECK(pt && y && x, "players 3001 (target) and 1003 (giver)");
        if (!pt || !y || !x) return;
        auto get = [&](const PlayerRow* r, const char* f) { return app.db.get_int(*pt, r->rec, f, -1); };
        auto set = [&](const PlayerRow* r, const char* f, int64_t v) {
            const Field* fd = pt->field(f);
            return fd && app.edit_field(*pt, r->rec, *fd, Value::of_int(v));
        };
        const size_t undo_y0 = app.undo_count(3001), undo_x0 = app.undo_count(1003);
        CHECK(set(y, "eyecolorcode", 1) && set(x, "eyecolorcode", 3), "the two differ on the eye colour (3001: 1, 1003: 3)");
        const size_t undo_y1 = app.undo_count(3001);
        app.request_tab = 0;
        ui.frames(2);
        CHECK(ui.click("3001", "##plist"), "player 3001");
        CHECK(ui.click("Appearance", "##pedit"), "Appearance tab");
        auto open_chooser = [&] {
            CHECK(ui.click("Choose a real face..."), "open the chooser");
            ui.frames(3);
            return ui.find("##whatcopy", W) != nullptr;
        };
        // 1. Head only (the default): the head moves, his eye colour stays
        CHECK(open_chooser(), "the chooser is open");
        fcl_close_filters(ui);
        CHECK(ui.click("face1003", W), "select 1003's head");
        ui.frames(2);
        CHECK(ui.click("Apply##faces"), "Apply (What to copy = Head only)");
        ui.frames(3);
        CHECK(get(y, "eyecolorcode") == 1, "Head only: his own eye colour stays (is " + std::to_string(get(y, "eyecolorcode")) + ")");
        while (app.undo_count(3001) > undo_y1) app.undo(3001);
        // 2. Head + eyes + skin: the giver's eye colour comes with the head
        CHECK(open_chooser(), "the chooser is open again");
        fcl_close_filters(ui);
        CHECK(ui.click("##whatcopy", W), "open What to copy");
        CHECK(ui.find("Head only", "##Combo") && ui.find("Head + eyes + skin", "##Combo") && ui.find("Head + eyes + skin + body type", "##Combo"),
              "three entries: Head only / Head + eyes + skin / Head + eyes + skin + body type");
        CHECK(ui.click("Head + eyes + skin", "##Combo"), "pick Head + eyes + skin");
        ui.frames(2);
        CHECK(ui.click("face1003", W), "select 1003's head");
        ui.frames(2);
        CHECK(ui.click("Apply##faces"), "Apply (Head + eyes + skin)");
        ui.frames(3);
        CHECK(get(y, "eyecolorcode") == 3, "Head + eyes + skin: the giver's eye colour came along (is " + std::to_string(get(y, "eyecolorcode")) + ")");
        while (app.undo_count(3001) > undo_y0) app.undo(3001);
        while (app.undo_count(1003) > undo_x0) app.undo(1003);
        // 3. leave What to copy at its default for the cases that follow (the choice is kept for the session)
        CHECK(open_chooser(), "the chooser is open a third time");
        fcl_close_filters(ui);
        CHECK(ui.click("##whatcopy", W) && ui.click("Head only", "##Combo"), "back to Head only");
        ui.frames(2);
        fcl_close_chooser(ui, "Close##faces");
    });
}
