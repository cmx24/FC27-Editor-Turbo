// 5.0c E9: the miniface follows the look. When a look attribute that changes the 3D render (core/look_pickers.h render_changes)
// changes, the Miniface editor asks the game for a new picture by itself (0.6 s after the last change, queued while the service is busy,
// never when nothing changed, paused after a failure, one undo step per session).
// Included by test_main.cpp after test_miniface_oneclick.h (it uses that file's helpers); ui_cases() runs from the UI world.
// Every case names itself "e9-auto ..." so TURBO_TEST_ONLY=e9- runs these with the id cases of test_capture_subject.h.
// The probe API (miniface_forget_looks, miniface_set_capture_timeout, the auto fields of MinifaceEditorProbe) does not exist on the old
// tree: the parts that need it say so with a FAIL instead of failing to compile (fail-old proof).
#pragma once

namespace miniface_auto_test {

static const char* kTick = "Update the picture when the look changes";

struct World {
    App& app;
    Ui& ui;
    std::shared_ptr<FakeCapture> fake;
    const Table* mt = nullptr;
    uint64_t mrec = 0;
    std::vector<std::pair<std::string, int64_t>> keep;  // look columns of manager 501, put back at the end
    fs::path m501;
    explicit World(App& a, Ui& u) : app(a), ui(u) {}
    int64_t get(const char* f) const { return app.db.get_int(*mt, mrec, f, 0); }
    bool set(const char* f, int64_t v, std::string* msg = nullptr) { return visual_apply(app, *mt, mrec, f, v, msg); }
    void restore() {
        for (const auto& kv : keep) app.db.set_int(*mt, mrec, kv.first, kv.second);
        app.capture.reset();
        wipe_saved_looks();
    }
    // the saved "rendered look" record of the career (5.0c E9 R5) lives on disk and outlives a case: wipe it so no later case sees it
    void wipe_saved_looks() {
#ifdef TURBO_MINIFACE_AUTO
        std::error_code ec;
        fs::remove(miniface_looks_file(app), ec);
        miniface_forget_looks();
#endif
    }
    // "the program was restarted": the memory of what was rendered is gone, the career's files stay; the editor is rebuilt by showing another
    // manager and coming back to this one on the 3D tab
    bool restart_and_return() {
#ifdef TURBO_MINIFACE_AUTO
        miniface_forget_looks();
#endif
        if (!ui.click("502##m1", "##mlist") || !ui.click("Miniface", "##medit")) return false;
        ui.frames(4);
        if (!ui.click("501##m0", "##mlist") || !ui.click("Miniface", "##medit")) return false;
        ui.frames(4);
        return ui.click("The 3D model", "##mmface");
    }
    size_t reqs() const { return fake->requests.size(); }
    // answer the request in flight with a plain picture of this colour
    void deliver(uint8_t r, uint8_t g, uint8_t b) {
        fake->next = capture::Result();
        fake->next.ok = true;
        fake->next.format = "raw RGBA";
        fake->next.image = solid(540, 540, r, g, b);
        fake->deliver = true;
        ui.frames(4);
        fake->deliver = false;
    }
};

// The user's manager (501, club 1 = the fixture's user club) open on his 3D tab, one picture rendered and written by the button
static bool open_rendered(World& w, bool render = true) {
    career_faces_test::reset(w.app, w.ui);
#ifdef TURBO_MINIFACE_AUTO
    miniface_forget_looks();
#endif
    w.fake = mo_fake(w.app);
    w.mt = w.app.db.table("manager");
    if (!w.mt) return false;
    w.mrec = w.app.db.find(*w.mt, "managerid", 501);
    if (!w.mrec) return false;
    w.keep.clear();
    for (const Field& f : w.mt->fields)
        if (look::render_changes(f.name.c_str())) w.keep.emplace_back(f.name, w.app.db.get_int(*w.mt, w.mrec, f.name, 0));
    w.m501 = career_faces::overlay_file(w.app.career_faces_paths(), w.app.career_faces_wanted(), legacy_path::staff_miniface_for(501, 7501));
    if (!mo_open(w.ui, w.app, true)) return false;
    if (!render) return true;
    if (!mo_capture(w.ui, w.fake, 540, 90, 90, 200)) return false;
    w.ui.frames(2);
    return w.reqs() == 1 && fs::exists(w.m501);
}

static int toasts_with(App& app, const std::string& s) {
    int n = 0;
    for (const auto& t : app.toasts)
        if (t.text.find(s) != std::string::npos) ++n;
    return n;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;

    run_case("e9-auto (E): a hair colour change after a render asks the game again after 0.6 s and rewrites the career's picture", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "manager 501 open on the 3D tab, one picture rendered and written");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        const std::string a = mo_bytes(w.m501);
        CHECK(w.reqs() == 1 && w.fake->requests[0].manager && w.fake->requests[0].id == 9999, "the first request asked for the created avatar (9999)");
        const int64_t was = w.get("haircolorcode");
        std::string msg;
        CHECK(w.set("haircolorcode", was == 5 ? 6 : 5, &msg), "haircolorcode written through visual_apply: " + msg);
        w.ui.frames(20);
        CHECK(w.reqs() == 1, fmt("0.33 s after the change: no request yet (debounce), have %zu", w.reqs()));
        w.ui.frames(40);
        CHECK(w.reqs() == 2, fmt("1 s after the change: one more request (old code: still 1), have %zu", w.reqs()));
        if (w.reqs() >= 2) CHECK(w.fake->requests[1].id == 9999 && w.fake->requests[1].manager && w.fake->requests[1].second_id == w.fake->requests[0].second_id,
                                 "the auto request is the Generate request (same id, manager flag, club)");
        w.deliver(200, 60, 60);
        CHECK(mo_bytes(w.m501) != a && mo_bytes(w.m501) != "<absent>", "heads_staff_501.dds was rewritten with the new picture");
#ifdef TURBO_MINIFACE_AUTO
        const MinifaceEditorProbe p = miniface_editor_probe(true);
        CHECK(p.auto_requests == 1, fmt("auto_requests %d", p.auto_requests));
        CHECK(p.undo_steps == 1, fmt("one undo step for the whole session, not one per auto render (V4): %zu", p.undo_steps));
        CHECK(!p.remove_bg, "Remove plain background stayed unticked");
#else
        CHECK(false, "no editor probe on this tree");
#endif
        w.ui.frames(120);
        CHECK(w.reqs() == 2, "nothing changed since: no further request");
        w.restore();
    });

    run_case("e9-auto (F1): a body type change asks again; a change back to what was rendered does not", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        const int64_t body = w.get("bodytypecode");
        std::string msg;
        CHECK(w.set("bodytypecode", body == 4 ? 3 : 4, &msg), "bodytypecode written: " + msg);
        w.ui.frames(60);
        CHECK(w.reqs() == 2, fmt("a body type change gives a request, have %zu", w.reqs()));
        w.deliver(10, 120, 60);
        // change to something and back inside the debounce: the look equals the rendered one again, so no request
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 7 ? 8 : 7), "hair colour changed");
        w.ui.frames(10);
        CHECK(w.set("haircolorcode", hair), "hair colour changed back");
        w.ui.frames(80);
        CHECK(w.reqs() == 2, fmt("changed and changed back before the render: no request, have %zu", w.reqs()));
        w.restore();
    });

    run_case("e9-auto (F2): a change while the service is busy is queued (not dropped, not doubled) and sent once when it is free", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        w.fake->pending = true;  // someone else (the face chooser, the looks studio) has a request in the service
        const size_t before = w.reqs();
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(120);
        CHECK(w.reqs() == before && w.fake->cancels == 0, fmt("busy: nothing sent, nothing cancelled (requests %zu, cancels %d)", w.reqs(), w.fake->cancels));
        w.fake->pending = false;
        w.ui.frames(10);
        CHECK(w.reqs() == before + 1, fmt("free again: exactly one request, have %zu (+%zu)", w.reqs(), w.reqs() - before));
        w.deliver(30, 30, 160);
        w.ui.frames(60);
        CHECK(w.reqs() == before + 1, "and no more");
        w.restore();
    });

    run_case("e9-auto (F3): two changes inside 0.6 s of each other give one request", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        const int64_t hair = w.get("haircolorcode"), skin = w.get("skintonecode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(20);
        CHECK(w.set("skintonecode", skin == 30 ? 40 : 30), "skin tone changed 0.33 s later");
        w.ui.frames(20);
        CHECK(w.reqs() == 1, fmt("0.33 s after the second change: still waiting (re-armed), have %zu", w.reqs()));
        w.ui.frames(40);
        CHECK(w.reqs() == 2, fmt("one request for both changes, have %zu", w.reqs()));
        w.deliver(80, 80, 80);
        w.restore();
    });

    run_case("e9-auto (F4): a failed auto render pauses the auto update (no loop); Generate lifts the pause", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(60);
        CHECK(w.reqs() == 2, "an auto request went out");
        w.fake->next = capture::Result();
        w.fake->next.ok = false;
        w.fake->next.error = "the game did not finish the capture";
        w.fake->deliver = true;
        w.ui.frames(4);
        w.fake->deliver = false;
#ifdef TURBO_MINIFACE_AUTO
        CHECK(miniface_editor_probe(true).auto_paused, "auto_paused after the failure");
#else
        CHECK(false, "no editor probe on this tree");
#endif
        CHECK(w.set("haircolorcode", hair == 9 ? 10 : 9), "hair colour changed again");
        w.ui.frames(200);
        CHECK(w.reqs() == 2, fmt("paused: no further auto request, have %zu", w.reqs()));
        // the person presses Generate: that render goes out and the pause is lifted
        CHECK(ui.click("Generate from 3D model"), "Generate");
        w.ui.frames(2);
        CHECK(w.reqs() == 3, "Generate sent a request");
        w.deliver(60, 160, 60);
#ifdef TURBO_MINIFACE_AUTO
        CHECK(!miniface_editor_probe(true).auto_paused, "Generate lifted the pause");
#endif
        CHECK(w.set("haircolorcode", hair == 11 ? 12 : 11), "hair colour changed once more");
        w.ui.frames(60);
        CHECK(w.reqs() == 4, fmt("auto update works again, have %zu", w.reqs()));
        w.deliver(60, 60, 160);
        w.restore();
    });

    run_case("e9-auto (F5): with the tick box off a look change asks nothing; on again it does", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        CHECK(mo_ticked(ui, kTick) == 1, "the tick box is on by default");
        CHECK(ui.click(kTick), "untick");
        w.ui.frames(2);
        CHECK(mo_ticked(ui, kTick) == 0, "unticked");
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(120);
        CHECK(w.reqs() == 1, fmt("tick box off: no request, have %zu", w.reqs()));
        CHECK(ui.click(kTick), "tick again");
        w.ui.frames(60);
        CHECK(w.reqs() == 2, fmt("on again: the pending change is rendered, have %zu", w.reqs()));
        w.deliver(90, 20, 20);
        w.restore();
    });

    run_case("e9-auto (F6): an auto render keeps the person's framing, leaves 'Remove plain background' alone and toasts nothing", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        w.ui.frames(2);
        const ItemRec* drag = mo_item(ui, "##mfdrag");
        CHECK(drag != nullptr, "the preview");
        if (!drag) {
            w.restore();
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 c = drag->rect.GetCenter();
        io.AddMousePosEvent(c.x, c.y);
        w.ui.frame();
        io.AddMouseWheelEvent(0.0f, 3.0f);  // zoom in
        w.ui.frames(4);
#ifdef TURBO_MINIFACE_AUTO
        const float zoom = miniface_editor_probe(true).fr_zoom;
        CHECK(zoom > 1.2f, fmt("the wheel zoomed the picture (%.2f)", zoom));
#endif
        w.ui.frames(60);  // the framing re-write settles
        const int set0 = toasts_with(app, "Miniface set"), rendered0 = toasts_with(app, "3D model rendered");
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(60);
        CHECK(w.reqs() == 2, "an auto request went out");
        w.deliver(200, 200, 40);
#ifdef TURBO_MINIFACE_AUTO
        const MinifaceEditorProbe p = miniface_editor_probe(true);
        CHECK(std::fabs(p.fr_zoom - zoom) < 0.001f, fmt("the framing was kept: zoom %.3f before, %.3f after", zoom, p.fr_zoom));
        CHECK(!p.remove_bg, "Remove plain background still unticked");
        CHECK(p.undo_steps == 1, fmt("still one undo step (%zu)", p.undo_steps));
#endif
        CHECK(toasts_with(app, "Miniface set") == set0 && toasts_with(app, "3D model rendered") == rendered0,
              "no toast for an auto render (every hair change would toast)");
        w.restore();
    });

    run_case("e9-auto (F7): an auto render that never comes back ends (GUI-side deadline) and pauses the auto update", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
#ifdef TURBO_MINIFACE_AUTO
        miniface_set_capture_timeout(3.0);
#endif
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(60);
        CHECK(w.reqs() == 2 && w.fake->pending, "an auto request is in flight and the game never answers");
        w.ui.frames(60 * 2);
        CHECK(w.fake->pending, "2 s: still waiting");
        w.ui.frames(60 * 3);
#ifdef TURBO_MINIFACE_AUTO
        CHECK(w.fake->cancels >= 1 && !w.fake->pending, fmt("after the deadline the request was cancelled (cancels %d)", w.fake->cancels));
        CHECK(miniface_editor_probe(true).auto_paused, "and the auto update is paused (old code: pending forever, nothing said)");
        miniface_set_capture_timeout(0.0);  // default again
#else
        CHECK(false, "no deadline on this tree");
#endif
        w.restore();
    });

    run_case("e9-auto (F8): a look changed while another manager was shown is rendered when his editor comes back", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        CHECK(ui.click("502##m1", "##mlist") && ui.click("Miniface", "##medit"), "another manager (502) is shown");
        w.ui.frames(4);
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "501's hair colour changed while 502 is shown");
        w.ui.frames(80);
        CHECK(w.reqs() == 1, fmt("nothing is drawn for 502's editor on 501's behalf, have %zu", w.reqs()));
        CHECK(ui.click("501##m0", "##mlist") && ui.click("Miniface", "##medit"), "back to 501");
        w.ui.frames(4);
        CHECK(ui.click("The 3D model", "##mmface"), "his 3D tab");
        w.ui.frames(60);
        CHECK(w.reqs() == 2, fmt("501 comes back and is rendered with his new look (old code: no code path), have %zu", w.reqs()));
        w.deliver(110, 110, 30);
        w.restore();
    });

    run_case("e9-auto (F9): a manager never rendered is not rendered by a look change (the first draw only records the look)", [&] {
        World w(app, ui);
        CHECK(open_rendered(w, false), "manager 501 open, nothing rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed");
        w.ui.frames(120);
        CHECK(w.reqs() == 0, fmt("no render yet, so nothing to update: no request, have %zu", w.reqs()));
        w.restore();
    });

    // R6 (verifier): a manager who is not the user's is asked by his head id; when his head id changes the Game id must follow it, unless the
    // person typed an id
    run_case("e9-auto (R6): another manager's Game id follows his head id after it changes (a typed id is kept)", [&] {
        World w(app, ui);
        career_faces_test::reset(app, ui);
#ifdef TURBO_MINIFACE_AUTO
        miniface_forget_looks();
#endif
        w.fake = mo_fake(app);
        w.mt = app.db.table("manager");
        if (!w.mt || !w.mt->has("headassetid")) {
            CHECK(false, "the test world has managers with a head asset id");
            return;
        }
        const uint64_t rec = app.db.find(*w.mt, "managerid", 502);
        const BridgeState& bs = app.bridge.state();
        const int64_t team = rec ? app.db.get_int(*w.mt, rec, "teamid", 0) : 0;
        CHECK(rec != 0 && team != bs.user_team, "manager 502 exists and is not the user's manager");
        if (!rec) return;
        const int64_t head0 = app.db.get_int(*w.mt, rec, "headassetid", 0);
        CHECK(head0 > 0 && head0 != 9999, fmt("502 has a head id of his own (%lld)", (long long)head0));
        app.request_tab = 2;
        w.ui.frames(2);
        CHECK(ui.click("502##m1", "##mlist") && ui.click("Miniface", "##medit"), "manager 502, Miniface");
        w.ui.frames(2);
        CHECK(ui.click("The 3D model", "##mmface"), "his 3D tab");
        CHECK(mo_capture(ui, w.fake, 540, 90, 90, 200), "Generate, answered");
        w.ui.frames(2);
        CHECK(w.reqs() == 1 && w.fake->requests[0].id == head0, fmt("Generate asked his head id %lld (asked %lld)", (long long)head0, w.reqs() ? (long long)w.fake->requests[0].id : -1LL));
        // his head changes (the picker, the real-face chooser, an import): the look changed, so a new picture is asked, for the NEW head
        const int64_t head1 = head0 + 11;
        app.db.set_int(*w.mt, rec, "headassetid", head1);
        w.ui.frames(70);
        CHECK(w.reqs() == 2, fmt("a changed head asks for a new picture (old code: no code path), have %zu", w.reqs()));
        if (w.reqs() >= 2)
            CHECK(w.fake->requests[1].id == head1,
                  fmt("the new picture is asked for his NEW head %lld (the stale id %lld is the R6 defect), asked %lld", (long long)head1, (long long)head0,
                      (long long)w.fake->requests[1].id));
        w.deliver(70, 70, 150);
#ifdef TURBO_MINIFACE_AUTO
        CHECK(!miniface_editor_probe(true).capture_id_typed, "the id was never typed");
        CHECK(miniface_editor_probe(true).capture_id == head1, fmt("and the Game id box reads the new head, read %d", miniface_editor_probe(true).capture_id));
        // a typed id is the person's: it stays when the head changes again
        miniface_set_capture_param(true, MinifaceCaptureParam::GameId, 4242);
        CHECK(miniface_editor_probe(true).capture_id_typed, "the id was typed");
        w.ui.frames(2);
        app.db.set_int(*w.mt, rec, "headassetid", head1 + 5);
        w.ui.frames(70);
        CHECK(w.reqs() == 3 && w.fake->requests.back().id == 4242,
              fmt("a typed id is kept: the third request asks 4242 (asked %lld, %zu requests)", w.reqs() ? (long long)w.fake->requests.back().id : -1LL, w.reqs()));
        w.deliver(150, 70, 70);
#endif
        app.db.set_int(*w.mt, rec, "headassetid", head0);
        w.restore();
    });

    // R5 (verifier): the look a miniface was rendered for is saved with the career, so a look changed after a restart is still rendered
    run_case("e9-auto (R5): a render from an earlier session is updated after a restart (saved with the career); a face written since is left alone", [&] {
        World w(app, ui);
        CHECK(open_rendered(w), "open + rendered");
        if (!w.mt || !w.mrec || w.fake == nullptr) return;
#ifdef TURBO_MINIFACE_AUTO
        CHECK(fs::exists(miniface_looks_file(app)), "the look the picture was rendered for was saved under the career's folder");
#else
        CHECK(false, "no saved rendered look on this tree");
#endif
        CHECK(w.restart_and_return(), "restart: the program forgot what it rendered; 501's 3D tab is open again");
        w.ui.frames(40);
        CHECK(w.reqs() == 1, fmt("nothing changed since the render: no request after the restart, have %zu", w.reqs()));
        const int64_t hair = w.get("haircolorcode");
        CHECK(w.set("haircolorcode", hair == 5 ? 6 : 5), "hair colour changed after the restart");
        w.ui.frames(70);
        CHECK(w.reqs() == 2, fmt("the picture follows the look although it was rendered in an earlier session (old code: no request), have %zu", w.reqs()));
        w.deliver(30, 160, 160);
        w.restore();

        // a face written into the career's folder after the render (the chooser, another tool) is not replaced by the next look change
        World v(app, ui);
        CHECK(open_rendered(v), "open + rendered again");
        if (!v.mt || !v.mrec || v.fake == nullptr) return;
        {
            std::ofstream out(v.m501, std::ios::binary | std::ios::trunc);
            out << "a real face the person chose afterwards";
        }
        CHECK(v.restart_and_return(), "restart again");
        v.ui.frames(10);
        const int64_t h2 = v.get("haircolorcode");
        CHECK(v.set("haircolorcode", h2 == 5 ? 6 : 5), "hair colour changed");
        v.ui.frames(120);
        CHECK(v.reqs() == 1, fmt("the career's copy is no longer the render: no request, have %zu", v.reqs()));
        CHECK(mo_bytes(v.m501) == "a real face the person chose afterwards", "and the file is untouched");
        v.restore();
    });
}

}  // namespace miniface_auto_test
