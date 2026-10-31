// 3.1.1 brief m3 (defect D3: GUI frames of the same manager draw differently): ONE resolver decides which id the game is asked to draw.
//   user manager -> 9999 (the game's created avatar), other managers -> their headassetid, players -> their playerid.
// Included by test_main.cpp after test_page_looks.h (FakeCapture and page_looks_test's show / hide are defined before it);
// capture_subject_test::ui_cases is called next to page_looks_test::ui_cases. Cases are named "Capture subject (3.1.1 M3): ..." so
// TURBO_TEST_ONLY="capture subject" runs them alone.
// The UI cases drive both frames by clicks (the Looks studio's "Draw him", the Miniface editor's "The 3D model" > "Generate") against
// FakeCapture and read the id each one asked the game for: they use no new API, so they compile on the tree before the resolver
// (9a71c2f) and FAIL there at run time (the user manager is not 9999 on either frame). The pure resolver cases need the new API
// (core/capture_subject.h, header-only) and are compiled only when that header exists (__has_include).
#pragma once
#include <array>
#include "core/legacy.h"
#include "core/player_capture.h"
#if __has_include("core/capture_subject.h")
#include "core/capture_subject.h"
#define CAPSUBJ_RESOLVER 1
#endif
#include "ui/ui_images.h"
#include "ui/ui_looks_studio.h"

namespace capture_subject_test {

using namespace turbo;

static MinifaceTarget g_target;
static bool g_draw = false;
static ImVec2 g_studio_display, g_miniface_display;
static void draw_extra(App& app) {
    if (!g_draw) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    if (ImGui::Begin("##capsubj_test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        miniface_editor(app, g_target);
    ImGui::End();
}

#ifdef CAPSUBJ_RESOLVER
static void pure_cases() {
    run_case("e9-capture subject (3.1.1 M3): the resolver (user manager 9999, other managers headassetid, players playerid) and its cache key", [&] {
        using capture::CaptureSubject;
        auto mgr = [](int64_t id, int64_t head, int64_t club, bool in_cm, int64_t user_team) {
            CaptureSubject s;
            s.manager = true;
            s.id = id;
            s.head = head;
            s.club = club;
            s.in_career = in_cm;
            s.user_team = user_team;
            return s;
        };
        CaptureSubject user = mgr(501, 7501, 1, true, 1);
        CHECK(capture::is_user_manager(user) && capture::capture_subject_id(user) == capture::kUserAvatarHeadId, "the manager of the user's club is 9999");
        CaptureSubject other = mgr(502, 7502, 7, true, 1);
        CHECK(!capture::is_user_manager(other) && capture::capture_subject_id(other) == 7502, "another club's manager is his head asset id");
        CHECK(capture::capture_subject_id(mgr(501, 7501, 1, false, 1)) == 7501, "outside a career nobody is the user's manager");
        CHECK(capture::capture_subject_id(mgr(501, 7501, 1, true, 0)) == 7501, "no user club known: nobody is the user's manager");
        CHECK(capture::capture_subject_id(mgr(505, 7505, 0, true, 0)) == 7505, "a free agent (club 0) is not the user's manager when the user's club is 0 too");
        CHECK(capture::capture_subject_id(mgr(505, 7505, 0, true, 1)) == 7505, "a free agent is not the user's manager");
        CHECK(capture::capture_subject_id(mgr(501, 0, 1, true, 1)) == capture::kUserAvatarHeadId, "the user's manager needs no head id");
        CHECK(capture::capture_subject_id(mgr(502, 0, 7, true, 1)) == 0, "another manager without a head: nothing the game can draw (0)");
        // 5.0c E9 (D): the manager row whose own id is 9999 is the created avatar, whatever the bridge says (no career, no user club, another club)
        CHECK(capture::is_user_manager(mgr(9999, 51, 95, false, 0)) && capture::capture_subject_id(mgr(9999, 51, 95, false, 0)) == capture::kUserAvatarHeadId,
              "managerid 9999 outside a career (bridge not in_cm, no user club): 9999, not his head 51");
        CHECK(capture::capture_subject_id(mgr(9999, 51, 7, true, 1)) == capture::kUserAvatarHeadId, "managerid 9999 with a club that is not the bridge's: still 9999");
        CHECK(capture::capture_subject_id(mgr(9999, 0, 0, false, 0)) == capture::kUserAvatarHeadId, "managerid 9999 needs no head and no club");
        CaptureSubject pl;
        pl.id = 3001;
        pl.head = 8001;
        pl.club = 1;
        pl.in_career = true;
        pl.user_team = 1;
        CHECK(!capture::is_user_manager(pl) && capture::capture_subject_id(pl) == 3001, "a player of the user's club is his playerid, never 9999");
        pl.id = 0;
        CHECK(capture::capture_subject_id(pl) == 0, "a player with no id: 0");
        // cache key = subject + mode: the same subject in the same mode is one key for every frame; another mode / camera / subject is another
        const std::string a = capture::capture_cache_key(user, capture::kCameraBodyFull, -1);
        CHECK(a == capture::capture_cache_key(user, capture::kCameraBodyFull, -1), "same subject, same mode: same key");
        CHECK(a != capture::capture_cache_key(user, capture::kCameraBody, -1), "another camera: another key");
        CHECK(a != capture::capture_cache_key(user, capture::kCameraBodyFull, 0), "the mode-0 retry: another key");
        CHECK(a != capture::capture_cache_key(other, capture::kCameraBodyFull, -1), "another manager: another key");
        CaptureSubject pl9999;
        pl9999.id = 9999;
        CHECK(a != capture::capture_cache_key(pl9999, capture::kCameraBodyFull, -1), "a player numbered 9999 never shares the user manager's picture");
    });
}
#else
static void pure_cases() {}
#endif

// What the two frames ask the game for, one person at a time (both through the clicks a user makes)
struct Asked {
    int64_t studio = -1, miniface = -1;
    int box = -1;  // the Game id box of the 3D tab after the click (miniface_editor_probe)
    bool studio_manager = false, miniface_manager = false;
};

static void deliver(Ui& ui, std::shared_ptr<FakeCapture>& fake) {
    fake->next = capture::Result();
    fake->next.ok = true;
    fake->next.format = "PNG 1024x1024";
    fake->next.image = solid(64, 64, 120, 90, 60);
    fake->deliver = true;
    ui.frames(4);
}

static Asked ask_manager(App& app, Ui& ui, std::shared_ptr<FakeCapture>& fake, int index) {
    Asked out;
    const auto& mgrs = app.model.managers();
    const ManagerRow& m = mgrs[static_cast<size_t>(index)];
    const Table* t = app.db.table("manager");
    const int64_t head = t ? app.db.get_int(*t, m.rec, "headassetid", 0) : 0;
    // frame 1: the Looks studio's picture of him ("Draw him")
    fake->requests.clear();
    looks::looks_studio_reset_for_tests();
    looks::looks_studio_show_managers(true);
    app.sel_manager = index;
    fake->pending = false;
    ImGui::GetIO().DisplaySize = g_studio_display;  // the Looks studio is drawn at the size its own test uses; the miniface frame below needs room
    page_looks_test::show(ui);
    ui.frames(30);
    CHECK(ui.click("Draw him###lsdraw", "##looksstudio_test"), "Draw him");
    ui.frames(2);
    std::string seen;
    for (const capture::Request& r : fake->requests) {
        seen += "[" + r.label + " id " + std::to_string(r.id) + "] ";
        if (r.label.rfind("Looks studio", 0) == 0) {
            out.studio = r.id;
            out.studio_manager = r.manager;
        }
    }
    CHECK(out.studio != -1, "the Looks studio asked the game (requests seen: " + (seen.empty() ? std::string("none") : seen) + ", studio says: " + "line: " + looks::studio_state().preview_line + ", studio requests " + std::to_string(looks::studio_state().requests) + ")");
    ImGui::GetIO().DisplaySize = g_miniface_display;
    deliver(ui, fake);
    page_looks_test::hide(ui);
    looks::looks_studio_show_managers(false);
    // frame 2: the Miniface editor's "The 3D model" tab ("Generate from 3D model"), the same manager as Career > Managers & staff shows him
    fake->requests.clear();
    fake->pending = false;
    // the editor keeps its Game id per target: another target first, so this manager's editor starts from its default id
    g_target = MinifaceTarget();
    g_target.manager = true;
    g_target.path = "capsubj_reset";
    g_draw = true;
    g_ui_extra = draw_extra;
    ui.frames(3);
    g_target = MinifaceTarget();
    g_target.manager = true;
    g_target.headassetid = head;
    g_target.id = m.managerid;
    g_target.teamid = m.teamid;
    g_target.path = legacy_path::staff_miniface_for(m.managerid, head);
    ui.frames(4);
    CHECK(ui.click("The 3D model", "##capsubj_test"), "the 3D model tab");
    ui.frames(2);
    CHECK(ui.click("Generate from 3D model", "##capsubj_test"), "Generate from 3D model");
    ui.frames(2);
    if (!fake->requests.empty()) {
        out.miniface = fake->requests.back().id;
        out.miniface_manager = fake->requests.back().manager;
    }
    out.box = miniface_editor_probe(true).capture_id;
    deliver(ui, fake);
    g_draw = false;
    g_ui_extra = nullptr;
    ui.frames(2);
    return out;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    (void)le;
    pure_cases();
    const ImVec2 old_display = ImGui::GetIO().DisplaySize;
    const auto old_capture = app.capture;
    const int old_manager = app.sel_manager;
    g_studio_display = old_display;
    g_miniface_display = ImVec2(1280.0f, 3400.0f);
    ImGui::GetIO().DisplaySize = g_miniface_display;
    ui.frames(3);

    run_case("e9-capture subject (3.1.1 M3): the Looks studio and the Miniface editor ask the game for the SAME id for the user's manager (9999) and for another manager (his head)", [&] {
        const Table* t = app.db.table("manager");
        const auto& mgrs = app.model.managers();
        const BridgeState& bs = app.bridge.state();
        CHECK(bs.in_cm && bs.user_team > 0, "the test world is in a career with a user club");
        int user = -1, other = -1;
        for (size_t i = 0; i < mgrs.size(); ++i) {
            if (mgrs[i].teamid == bs.user_team && user < 0) user = static_cast<int>(i);
            if (mgrs[i].teamid != bs.user_team && mgrs[i].teamid > 0 && other < 0) other = static_cast<int>(i);
        }
        if (!t || user < 0 || other < 0 || !t->has("headassetid")) {
            CHECK(false, "the test world has the user's manager and another club's manager, with a head asset id");
            return;
        }
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = fake->st.available = true;
        fake->st.reason = "ready";
        app.capture = fake;
        const int64_t uhead = app.db.get_int(*t, mgrs[static_cast<size_t>(user)].rec, "headassetid", 0);
        const int64_t ohead = app.db.get_int(*t, mgrs[static_cast<size_t>(other)].rec, "headassetid", 0);
        CHECK(uhead > 0 && ohead > 0 && uhead != capture::kUserAvatarHeadId, "both managers have a head asset id of their own");

        const Asked u = ask_manager(app, ui, fake, user);
        CHECK(u.studio == capture::kUserAvatarHeadId,
              "Looks studio, the user's manager: the game is asked for 9999, not his head " + std::to_string(uhead) + " (asked " + std::to_string(u.studio) + ")");
        CHECK(u.miniface == capture::kUserAvatarHeadId,
              "Miniface editor, the user's manager: the game is asked for 9999, not his head " + std::to_string(uhead) + " (asked " + std::to_string(u.miniface) + ")");
        CHECK(u.studio == u.miniface, "the two frames ask the same id for him: studio " + std::to_string(u.studio) + ", miniface " + std::to_string(u.miniface));
        CHECK(u.studio_manager && u.miniface_manager, "both ask for a manager's head");
        CHECK(u.box == capture::kUserAvatarHeadId, "Miniface editor: the Game id box reads 9999 for him (read " + std::to_string(u.box) + ")");

        const Asked o = ask_manager(app, ui, fake, other);
        CHECK(o.studio == ohead && o.miniface == ohead,
              "another club's manager: both frames ask his head " + std::to_string(ohead) + " (studio " + std::to_string(o.studio) + ", miniface " + std::to_string(o.miniface) + ")");
        CHECK(o.studio_manager && o.miniface_manager, "both ask for a manager's head (other)");
        CHECK(o.box == ohead, "Miniface editor: the Game id box reads his head " + std::to_string(ohead) + " for another club's manager (read " + std::to_string(o.box) + ")");
        app.capture = old_capture;
    });

    // 5.0c E9 (D): a manager whose managerid is 9999 is the created avatar even when his club is not the bridge's user club (the id rule, alone)
    run_case("e9-capture subject (5.0c E9): a manager with managerid 9999 asks the game for 9999 on the Miniface editor even when his club is not the user's", [&] {
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = fake->st.available = true;
        fake->st.reason = "ready";
        app.capture = fake;
        const BridgeState& bs = app.bridge.state();
        auto open_editor = [&](int64_t managerid, int64_t head, int64_t team) {
            fake->requests.clear();
            fake->pending = false;
            g_target = MinifaceTarget();
            g_target.manager = true;
            g_target.path = "capsubj_reset_d";
            g_draw = true;
            g_ui_extra = draw_extra;
            ui.frames(3);
            g_target = MinifaceTarget();
            g_target.manager = true;
            g_target.headassetid = head;
            g_target.id = managerid;
            g_target.teamid = team;
            g_target.path = legacy_path::staff_miniface_for(managerid, head);
            ui.frames(4);
            const bool ok = ui.click("The 3D model", "##capsubj_test");
            ui.frames(2);
            const bool ok2 = ok && ui.click("Generate from 3D model", "##capsubj_test");
            ui.frames(2);
            const int64_t asked = fake->requests.empty() ? -1 : fake->requests.back().id;
            const int box = miniface_editor_probe(true).capture_id;
            g_draw = false;
            g_ui_extra = nullptr;
            ui.frames(2);
            fake->pending = false;
            return std::array<int64_t, 3>{ok2 ? 1 : 0, asked, box};
        };
        const int64_t other_club = bs.user_team + 1000;
        const auto d = open_editor(9999, 51, other_club);
        CHECK(d[0] == 1, "the 3D tab and Generate are on screen");
        CHECK(d[1] == capture::kUserAvatarHeadId, "managerid 9999 (club " + std::to_string(other_club) + ", head 51): the game is asked for 9999 (asked " + std::to_string(d[1]) + ")");
        CHECK(d[2] == capture::kUserAvatarHeadId, "and the Game id box reads 9999 (read " + std::to_string(d[2]) + ")");
        // no over-reach (B): a manager of another club with head 7501 is still his head
        const auto b = open_editor(777, 7501, other_club);
        CHECK(b[0] == 1 && b[1] == 7501 && b[2] == 7501, "another club's manager 777 (head 7501): 7501 (asked " + std::to_string(b[1]) + ", box " + std::to_string(b[2]) + ")");
        app.capture = old_capture;
    });

    app.sel_manager = old_manager;
    app.capture = old_capture;
    ImGui::GetIO().DisplaySize = old_display;
    ui.frames(2);
}

}  // namespace capture_subject_test
