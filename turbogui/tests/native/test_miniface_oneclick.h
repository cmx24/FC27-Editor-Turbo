// 5.0b-C (Cassio 10-29 23:45: "assigning of minifaces should be a one-click, remove plain background always unchecked,
// preview auto drawing with every change"): a click on a source writes the miniface at once, the background removal is
// never switched on by code, and a change of the 3D model fields redraws the preview.
// Included by test_main.cpp after the Ui driver; the UI cases run from test_ui() (mo_ui_cases), the pure ones from the run list.
// The probe API of ui_images.h (TURBO_MINIFACE_PROBE) does not exist on the old tree: the parts that need it say so with a FAIL
// instead of failing to compile, so this file can be run on the base (fail-old proof).
#pragma once
#include <algorithm>
#include <cstring>

// ---- helpers: widgets of the miniface editor
static const ItemRec* mo_item(Ui& ui, const std::string& label, const std::string& win = "") {
    const ItemRec* r = ui.find(label, win);
    if (!r) r = ui.find_scrolling(label, win, "");
    return r;
}
// -1 = the box is not on screen, 0 = unticked, 1 = ticked
static int mo_ticked(Ui& ui, const std::string& label) {
    const ItemRec* r = mo_item(ui, label);
    if (!r) return -1;
    return (r->flags & ImGuiItemStatusFlags_Checked) ? 1 : 0;
}
// The editor of a subject, fresh: a neighbour first (the editor resets when the subject changes), then the subject, on its 3D tab
static bool mo_open(Ui& ui, App& app, bool manager) {
    app.request_tab = manager ? 2 : 0;
    ui.frames(2);
    if (manager) {
        if (!ui.click("502##m1", "##mlist") || !ui.click("Miniface", "##medit")) return false;
        ui.frames(2);
        if (!ui.click("501##m0", "##mlist") || !ui.click("Miniface", "##medit")) return false;
        ui.frames(2);
        return ui.click("The 3D model", "##mmface");
    }
    if (!ui.click("1003", "##plist") || !ui.click("Miniface", "##pedit")) return false;
    ui.frames(2);
    if (!ui.click("1002", "##plist") || !ui.click("Miniface", "##pedit")) return false;
    ui.frames(2);
    return ui.click("The 3D model", "##mface");
}
// One "Generate from 3D model" answered with a plain picture of this colour
static bool mo_capture(Ui& ui, const std::shared_ptr<FakeCapture>& fake, int w, uint8_t r, uint8_t g, uint8_t b) {
    if (!ui.click("Generate from 3D model")) return false;
    ui.frames(2);
    fake->next = capture::Result();
    fake->next.ok = true;
    fake->next.format = "raw RGBA";
    fake->next.image = solid(w, w, r, g, b);
    fake->deliver = true;
    ui.frames(4);
    return !fake->pending;
}
static std::shared_ptr<FakeCapture> mo_fake(App& app) {
    auto fake = std::make_shared<FakeCapture>();
    fake->st.installed = true;
    fake->st.available = true;
    fake->st.reason = "ready";
    app.capture = fake;
    return fake;
}
static std::string mo_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "<absent>";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---- pure part: where a miniface goes, and the setter of the capture fields
static void test_miniface_oneclick() {
    run_case("test_miniface_oneclick (c): miniface_write_plan puts a player's and a manager's miniface under the career's own folder", [&] {
#ifdef TURBO_MINIFACE_PROBE
        const std::string A = "0123456789abcdef";
        const career_faces::Paths p{g_out / "mo_plan" / "turbo_output", g_out / "mo_plan" / "mods" / "legacy"};
        const MinifaceWritePlan pl = miniface_write_plan(p, false, 1002, 55, A);
        CHECK(pl.career_scoped && pl.path == p.turbo_output / "careers" / A / "heads" / "p1002.dds", "player: " + pl.path.u8string());
        CHECK(pl.rel == "data/ui/imgAssets/heads/p1002.dds" && pl.size == 256 && pl.undo_dir == p.turbo_output / "careers" / A / "_undo",
              "player rel / size / undo dir: " + pl.rel + " " + std::to_string(pl.size) + " " + pl.undo_dir.u8string());
        const MinifaceWritePlan mg = miniface_write_plan(p, true, 501, 7501, A);
        CHECK(mg.career_scoped && mg.path == p.turbo_output / "careers" / A / "heads_staff" / "heads_staff_501.dds", "manager: " + mg.path.u8string());
        CHECK(mg.rel == "data/ui/imgAssets/heads_staff/heads_staff_501.dds" && mg.size == 512, "manager rel / size: " + mg.rel);
        // no career (or an unidentified one): the shared file, nothing career-scoped, no undo folder
        const MinifaceWritePlan none = miniface_write_plan(p, false, 1002, 55, "");
        CHECK(!none.career_scoped && none.path == p.mods_legacy / "data" / "ui" / "imgAssets" / "heads" / "p1002.dds" && none.undo_dir.empty(),
              "no career: " + none.path.u8string());
        const MinifaceWritePlan bad = miniface_write_plan(p, true, 501, 7501, "not-a-career");
        CHECK(!bad.career_scoped && bad.path.u8string().find("careers") == std::string::npos, "invalid career id is no career: " + bad.path.u8string());
#else
        CHECK(false, "miniface_write_plan does not exist on this tree (ui_images.h has no TURBO_MINIFACE_PROBE): no plan of where a miniface goes");
#endif
    });

    run_case("test_miniface_oneclick (b0): the capture fields have a setter that bumps the editor's version on a real change only", [&] {
#ifdef TURBO_MINIFACE_PROBE
        // the editors are static: this needs no UI, only the setter and the probe
        const MinifaceEditorProbe p0 = miniface_editor_probe(false);
        miniface_set_capture_param(false, MinifaceCaptureParam::GameId, p0.capture_id + 1);
        const MinifaceEditorProbe p1 = miniface_editor_probe(false);
        CHECK(p1.version > p0.version && p1.capture_id == p0.capture_id + 1,
              fmt("Game id changed: version %llu -> %llu", (unsigned long long)p0.version, (unsigned long long)p1.version));
        miniface_set_capture_param(false, MinifaceCaptureParam::GameId, p1.capture_id);
        CHECK(miniface_editor_probe(false).version == p1.version, "the same value again: no change");
#else
        CHECK(false, "no miniface_set_capture_param on this tree: a change of the 3D fields cannot bump the editor's version");
#endif
    });
}

// ---- UI part (called from test_ui, after the miniface UI cases)
static void mo_ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    run_case("test_miniface_oneclick (a): 'Remove plain background' stays unticked after a finished 3D capture (player and manager)", [&] {
        career_faces_test::reset(app, ui);
        auto fake = mo_fake(app);
        CHECK(mo_open(ui, app, false), "player 1002, 3D tab");
        CHECK(mo_ticked(ui, "Remove plain background") == 0, "player: unticked when the editor opens");
        CHECK(mo_capture(ui, fake, 256, 10, 200, 40), "a capture is delivered");
        CHECK(mo_ticked(ui, "Remove plain background") == 0, "player: still unticked after the capture (old code ticked it)");
        CHECK(mo_open(ui, app, true), "manager 501, 3D tab");
        CHECK(mo_ticked(ui, "Remove plain background") == 0, "manager: unticked when the editor opens");
        CHECK(mo_capture(ui, fake, 540, 90, 90, 200), "a manager capture is delivered");
        CHECK(mo_ticked(ui, "Remove plain background") == 0, "manager: still unticked after the capture (old code ticked it)");
        app.capture.reset();
    });

    run_case("test_miniface_oneclick (b): a change of the 3D model fields redraws the preview", [&] {
        career_faces_test::reset(app, ui);
        auto fake = mo_fake(app);
        CHECK(mo_open(ui, app, false), "player 1002, 3D tab");
        CHECK(mo_capture(ui, fake, 256, 10, 200, 40), "a capture is delivered");
        ui.frames(3);
#ifdef TURBO_MINIFACE_PROBE
        const MinifaceEditorProbe p0 = miniface_editor_probe(false);
        miniface_set_capture_param(false, MinifaceCaptureParam::Camera, 1);
        miniface_set_capture_param(false, MinifaceCaptureParam::Mode, 5);
        miniface_set_capture_param(false, MinifaceCaptureParam::Extra, 3);
        miniface_set_capture_param(false, MinifaceCaptureParam::UseTemplate, 0);
        miniface_set_capture_param(false, MinifaceCaptureParam::Advanced, 1);
        const MinifaceEditorProbe p1 = miniface_editor_probe(false);
        CHECK(p1.version >= p0.version + 5,
              fmt("five real changes: version %llu -> %llu", (unsigned long long)p0.version, (unsigned long long)p1.version));
        miniface_set_capture_param(false, MinifaceCaptureParam::Mode, 5);
        CHECK(miniface_editor_probe(false).version == p1.version, "the same value again: no change");
#else
        CHECK(false, "no miniface_set_capture_param on this tree: the capture fields never bumped the editor's version (preview stale)");
#endif
        app.capture.reset();
    });

    run_case("test_miniface_oneclick (d): a click on a source writes the career's miniface at once; Undo and Restore original bring the earlier ones back", [&] {
        career_faces_test::reset(app, ui);
        const std::string career = app.career_faces_wanted();
        const career_faces::Paths paths = app.career_faces_paths();
        const fs::path p1002 = career_faces::overlay_file(paths, career, legacy_path::player_miniface(1002));
        auto fake = mo_fake(app);
        CHECK(career_faces::is_career_id(career), "the UI world is an identified career");
        CHECK(mo_open(ui, app, false), "player 1002, 3D tab");
        CHECK(!fs::exists(p1002), "nothing written yet");
        // source A: one click on Generate and the miniface is there, nobody pressed Save
        CHECK(mo_capture(ui, fake, 256, 10, 200, 40), "capture A");
        CHECK(fs::exists(p1002), "A: the file is written at once, careers/<id>/heads/p1002.dds (old code: only 'Save as miniface' wrote it)");
        const std::string a = mo_bytes(p1002);
        CHECK(ui.toast_contains("Miniface set"), "A: the notification says it");
        CHECK(mo_item(ui, "Undo##mfundo") != nullptr, "A: Undo is offered");
        CHECK(mo_item(ui, "Save again") != nullptr, "A: 'Save as miniface' is now 'Save again'");
        Rgba img;
        std::string err;
        CHECK(load_image_file(p1002, img, &err) && img.w == 256 && img.h == 256, "A: 256 x 256 DDS: " + err);
#ifdef TURBO_MINIFACE_PROBE
        MinifaceEditorProbe pr = miniface_editor_probe(false);
        CHECK(pr.undo_steps == 1 && pr.auto_writes == 1 && fs::path(pr.written_path) == p1002 && pr.prev_path.empty(),
              fmt("A: probe steps %zu writes %d path %s prev '%s'", pr.undo_steps, pr.auto_writes, pr.written_path.c_str(), pr.prev_path.c_str()));
#else
        CHECK(false, "no editor probe on this tree: no plan {write path, prev path, undo available}");
#endif
        // source B: the file changes, the one before is kept for Undo
        CHECK(mo_capture(ui, fake, 256, 200, 30, 30), "capture B");
        const std::string b = mo_bytes(p1002);
        CHECK(b != "<absent>" && b != a, "B: the file is B's picture");
#ifdef TURBO_MINIFACE_PROBE
        pr = miniface_editor_probe(false);
        CHECK(pr.undo_steps == 2 && !pr.prev_path.empty() && mo_bytes(pr.prev_path) == a && pr.original_available,
              fmt("B: steps %zu, prev '%s' holds A, original offered %d", pr.undo_steps, pr.prev_path.c_str(), int(pr.original_available)));
#endif
        // source C, then Undo -> B, Undo -> A
        CHECK(mo_capture(ui, fake, 256, 30, 30, 220), "capture C");
        const std::string c = mo_bytes(p1002);
        CHECK(c != b && c != a, "C: the file is C's picture");
        CHECK(ui.click("Undo##mfundo"), "Undo");
        ui.frames(2);
        CHECK(mo_bytes(p1002) == b, "Undo: B is back");
        CHECK(ui.click("Undo##mfundo"), "Undo again");
        ui.frames(2);
        CHECK(mo_bytes(p1002) == a, "Undo again: A is back");
        // Restore original: every step of this session undone (A was the first: the career had no copy before it)
        CHECK(mo_capture(ui, fake, 256, 120, 120, 30), "capture D");
        CHECK(mo_capture(ui, fake, 256, 30, 120, 120), "capture E");
        CHECK(mo_item(ui, "Restore original##mforig") != nullptr, "Restore original is offered after two sources");
        CHECK(ui.click("Restore original##mforig"), "Restore original");
        ui.frames(2);
        CHECK(!fs::exists(p1002), "Restore original: the career has no copy again (it had none before the first click)");
        app.capture.reset();
    });

    run_case("test_miniface_oneclick (d1): a manager's source click writes heads_staff_501.dds (512 x 512) in the career at once", [&] {
        career_faces_test::reset(app, ui);
        const std::string career = app.career_faces_wanted();
        const career_faces::Paths paths = app.career_faces_paths();
        const fs::path m501 = career_faces::overlay_file(paths, career, legacy_path::staff_miniface_for(501, 7501));
        auto fake = mo_fake(app);
        CHECK(mo_open(ui, app, true), "manager 501, 3D tab");
        CHECK(mo_capture(ui, fake, 540, 90, 90, 200), "capture");
        CHECK(fs::exists(m501), "the file is written at once under careers/<id>/heads_staff/: " + m501.u8string());
        Rgba img;
        CHECK(load_image_file(m501, img) && img.w == 512 && img.h == 512, "512 x 512");
        CHECK(ui.toast_contains("Miniface set"), "notification");
        CHECK(mo_item(ui, "Undo##mfundo") != nullptr, "Undo is offered to managers too");
        CHECK(ui.click("Undo##mfundo"), "Undo");
        ui.frames(2);
        CHECK(!fs::exists(m501), "Undo: no copy again");
        app.capture.reset();
    });

    run_case("test_miniface_oneclick (d2): a framing change redraws at once and re-writes 0.6 s after the last change, without a new undo step", [&] {
        career_faces_test::reset(app, ui);
        const std::string career = app.career_faces_wanted();
        const fs::path p1002 = career_faces::overlay_file(app.career_faces_paths(), career, legacy_path::player_miniface(1002));
        auto fake = mo_fake(app);
        CHECK(mo_open(ui, app, false), "player 1002, 3D tab");
        CHECK(mo_capture(ui, fake, 256, 10, 200, 40), "capture");
        const std::string a = mo_bytes(p1002);
        ui.frames(2);
        const ItemRec* drag = mo_item(ui, "##mfdrag");
        CHECK(drag != nullptr, "the preview");
        if (!drag) {
            app.capture.reset();
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 c = drag->rect.GetCenter();
        io.AddMousePosEvent(c.x, c.y);
        ui.frame();
#ifdef TURBO_MINIFACE_PROBE
        const MinifaceEditorProbe p0 = miniface_editor_probe(false);
#endif
        io.AddMouseWheelEvent(0.0f, 2.0f);  // zoom the picture
        ui.frames(3);
#ifdef TURBO_MINIFACE_PROBE
        const MinifaceEditorProbe p1 = miniface_editor_probe(false);
        CHECK(p1.version > p0.version, "the preview follows the wheel at once (version bumped)");
        CHECK(p1.rewrites == p0.rewrites && mo_bytes(p1002) == a, "the file is not rewritten yet (debounce)");
        ui.frames(20);
        CHECK(miniface_editor_probe(false).rewrites == p0.rewrites && mo_bytes(p1002) == a, "0.3 s later: still not rewritten");
        ui.frames(40);
        const MinifaceEditorProbe p2 = miniface_editor_probe(false);
        CHECK(p2.rewrites == p0.rewrites + 1, fmt("1 s later: rewritten once (%d -> %d)", p0.rewrites, p2.rewrites));
        CHECK(p2.undo_steps == p0.undo_steps, "the rewrite is no new undo step");
#else
        CHECK(false, "no editor probe on this tree: nothing to tell when the file follows a framing change");
#endif
        app.capture.reset();
    });
}
