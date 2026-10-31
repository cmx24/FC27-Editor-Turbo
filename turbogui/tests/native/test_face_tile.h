// 5.0c E7-realface (brief 5.0c-FIX, E7): the real-face chooser's hidden game heads. Included by test_main.cpp after test_asset_app.h.
//   T1  a hidden head with no miniface and no legend picture is RENDERED: a request with {head id, manager = true} goes to the capture
//       service and the picture lands in the cache as m_<headid>.dds (5.0b: queue_head3d returned early for a game head, so the cell
//       showed "no picture" for ever).                                      [behavioural: fails on the 5.0b code]
//   T2  the render breaker: after 12 failures with no success the chooser stops asking (the game is not stalled by a loop on heads that
//       will not render), says why on the tile, and a new catalog re-arms it.                [behavioural: fails on the 5.0b code]
//   T3/T4  (needs the 5.0c debug hook face_tile_debug) the source a tile shows is never "unavailable" while a render is possible, and the
//       chips of a tile (Hidden, name match, 3D pending) do not overlap, lie inside the cell and are drawn with the real font.
// The pure ladder and layout are also proven by tests/catalog/run_face_tile.sh. Part B is compiled only when ui/ui_face_tile.h exists.
#pragma once
#include <chrono>
#include <functional>
#include <thread>

#include "core/asset_catalog.h"
#include "ui/ui_asset_browser.h"
#include "ui/ui_faces.h"
#include "ui/ui_images.h"

#if __has_include("ui/ui_face_tile.h")
#define TURBO_FACE_TILE_DEBUG_API 1
#endif

namespace face_tile_test {

static const char* kKey = "0000AAAA-0000BBBB";  // the fixture's build

// The chooser remembers which renders exist (g_r3d.cached) and how often a head failed: after the files are deleted by the test,
// forget those too, or a later case sees a render that is gone.
static void forget_renders(App& app) {
    for (int64_t id = 90001; id <= 90040; ++id) {
        faces3d_forget_render(app, true, id);
        faces3d_forget_render(app, false, id);
    }
}

// The fixture's catalog, with `extra` more hidden heads that have no miniface and no legend picture (ids 90010...), the name of 90003 a
// manager's (name match for manager 504) when `tuchel`, and a new built_at (a reload under the same key is a change only then).
static void write_catalog(const fs::path& dir, int extra, bool tuchel, const char* built_at) {
    std::ifstream in("tests/fixtures/asset_catalog_v1_small.json", std::ios::binary);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    j["built_at"] = built_at;
    auto& h = j["families"]["head3d"];
    for (int i = 0; i < extra; ++i) {
        const int id = 90010 + i;
        h["ids"].push_back(id);
        h["vars"][std::to_string(id)] = nlohmann::json::array({0});
        h["toc"][std::to_string(id)] = "S";
        h["slug"][std::to_string(id)] = "extra_head_" + std::to_string(i);
    }
    if (tuchel) h["slug"]["90003"] = "thomas_tuchel";
    std::ofstream out(dir / (std::string("catalog_") + kKey + ".json"), std::ios::binary | std::ios::trunc);
    out << j.dump();
}

static void load_catalog(App& app, Ui& ui, const char* why) {
    for (int i = 0; i < 400 && !app.asset_reload(why); ++i) {
        ui.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 400 && app.asset_loading(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ui.frames(2);
}

// The game answers the request in flight: every head with `ok_for(id)`, or an error
static void answer(FakeCapture& fk, const std::function<bool(int32_t)>& ok_for) {
    if (!fk.pending || !fk.answers.empty()) return;
    std::vector<capture::Result> rs;
    for (const auto& e : capture::request_entries(fk.current)) {
        capture::Result a;
        a.id = e.id;
        a.label = e.label;
        a.ok = ok_for(e.id);
        if (a.ok) {
            a.format = "DDS DXT5 256x256";
            a.image = solid(256, 256, 40, 70, 110);
        } else {
            a.error = "fake: the game did not render it";
        }
        rs.push_back(a);
    }
    if (rs.size() == 1) {  // the fake's single-answer path
        fk.next = rs.front();
        fk.deliver = true;
    } else {
        for (auto& a : rs) fk.answers.push_back(a);
    }
}

// Every head id (>= 90000: hidden game heads) the requests so far asked for, with the key space it asked in
struct Asked {
    std::vector<int32_t> ids;
    bool all_manager_space = true;
    bool any_player_space = false;
};
static Asked asked(const FakeCapture& fk) {
    Asked a;
    for (const auto& r : fk.requests)
        for (const auto& e : capture::request_entries(r))
            if (e.id >= 90000) {
                a.ids.push_back(e.id);
                a.all_manager_space = a.all_manager_space && e.manager;
                a.any_player_space = a.any_player_space || !e.manager;
            }
    return a;
}

// The end of a case that leaves a request in flight: close the chooser (no more on-screen heads), empty the queues, let the game answer
// what is in flight, so the next case starts with nothing in flight (re-review 10-31: T2 left its re-arm request in flight, and the next
// case's fake never answered it, so nothing more was sent until the 20 s timeout)
static void drain(Ui& ui, FakeCapture& fk) {
    if (ui.find("Close##faces")) ui.click("Close##faces");
    ui.frames(2);
    faces3d_stop();
    for (int i = 0; i < 4 && fk.pending; ++i) {
        answer(fk, [](int32_t) { return false; });
        ui.frames(6);
        faces3d_stop();
    }
    fk.pending = false;
    fk.answers.clear();
}

static bool open_hidden_scope(Ui& ui) {
    if (!ui.click("3001", "##plist") || !ui.click("Appearance", "##pedit") || !ui.click("Choose a real face...")) return false;
    ui.frames(3);
    if (!ui.click("##facescope", "Choose a real face")) return false;
    ui.frames(2);
    if (!ui.click("Hidden", "Combo")) return false;
    ui.frames(3);
    return true;
}

static void ui_cases(App& app, Ui& ui, const fs::path& le) {
    const fs::path out = le / "turbo_output";
    const fs::path cdir = assets::catalog_dir(out);
    const fs::path r3d = out / "cache" / "faces3d";
    std::error_code ec;

    // ---- T1
    run_case("5.0c E7 T1: a hidden head with no picture is rendered through the manager route (m_<headid>.dds), not left at 'no picture'", [&] {
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::create_directories(cdir, ec);
        write_catalog(cdir, 0, false, "2026-10-30T10:00:00-04:00");
        auto saved_hook = app.hook_report;
        app.hook_report = [&]() {
            HookReport r;
            r.build = kKey;
            return r;
        };
        load_catalog(app, ui, "test: E7 catalog");
        CHECK(app.asset_catalog()->loaded(), "catalog loaded: " + app.asset_load_note);
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = true;
        fake->st.available = true;
        fake->st.reason = "ready";
        fake->st.max_batch = 6;
        app.capture = fake;
        app.request_tab = 0;
        ui.frames(2);
#ifdef TURBO_FACE_TILE_DEBUG_API
        face_tile_debug_enable(true);
#endif
        CHECK(open_hidden_scope(ui), "open the chooser on the Hidden scope (player 3001, Appearance, Choose a real face..., scope Hidden)");
        ui.frames(4);
        CHECK(!fake->requests.empty(), fmt("a render request went out for the hidden heads on screen (%zu)", fake->requests.size()));
        // the game renders every head it is asked for; the loop answers whatever is in flight, a few rounds
        for (int round = 0; round < 8; ++round) {
            answer(*fake, [](int32_t) { return true; });
            ui.frames(6);
        }
        const Asked a = asked(*fake);
        const bool has3 = std::find(a.ids.begin(), a.ids.end(), 90003) != a.ids.end();
        const bool has4 = std::find(a.ids.begin(), a.ids.end(), 90004) != a.ids.end();
        CHECK(has3 && has4, "heads 90003 and 90004 (no miniface, no legend picture) were asked for");
        CHECK(a.all_manager_space && !a.any_player_space, "every hidden head is asked in the manager key space (head id, manager = true), never as a player id");
        CHECK(std::count(a.ids.begin(), a.ids.end(), 90003) == 1, "a head that rendered is not asked again");
        CHECK(fs::exists(r3d / "m_90003.dds") && fs::exists(r3d / "m_90004.dds"), "the pictures are in the cache as m_<headid>.dds");
        CHECK(!fs::exists(r3d / "p_90003.dds"), "and not as p_<id> (a hidden head has no player)");
#ifdef TURBO_FACE_TILE_DEBUG_API
        bool shown = false;
        for (const auto& d : face_tile_debug())
            if (d.id == 90003) shown = d.game_head && d.source == static_cast<int>(face_tile::Source::Render3dM);
        CHECK(shown, "the tile of head 90003 now shows its 3D render (Render3dM)");
        face_tile_debug_enable(false);
#endif
        if (ui.find("Close##faces")) ui.click("Close##faces");
        ui.frames(2);
        app.capture.reset();
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::remove(cdir / (std::string("catalog_") + kKey + ".json"), ec);
        load_catalog(app, ui, "test: E7 catalog removed");
        app.hook_report = saved_hook;
    });

    // ---- T2
    run_case("5.0c E7 T2: 12 failed hidden-head renders switch the route off for the session (no loop on the game), a new catalog re-arms it", [&] {
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::create_directories(cdir, ec);
        write_catalog(cdir, 20, false, "2026-10-30T11:00:00-04:00");
        auto saved_hook = app.hook_report;
        app.hook_report = [&]() {
            HookReport r;
            r.build = kKey;
            return r;
        };
        load_catalog(app, ui, "test: E7 catalog with 20 more heads");
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = true;
        fake->st.available = true;
        fake->st.reason = "ready";
        fake->st.max_batch = 6;
        app.capture = fake;
        app.request_tab = 0;
        ui.frames(2);
#ifdef TURBO_FACE_TILE_DEBUG_API
        face_tile_debug_enable(true);
#endif
        CHECK(open_hidden_scope(ui), "open the chooser on the Hidden scope");
        ui.frames(4);
        for (int round = 0; round < 24; ++round) {
            answer(*fake, [](int32_t) { return false; });  // the game never renders a head id no row uses
            ui.frames(6);
        }
        const size_t n_asked = asked(*fake).ids.size();
        CHECK(n_asked >= 12, fmt("the heads were asked for until 12 had failed (%zu asked)", n_asked));
        CHECK(n_asked <= 12 + 6, fmt("... and no further: at most the 12 failures plus the batch in flight (%zu asked)", n_asked));
        const size_t reqs = fake->requests.size();
        for (int i = 0; i < 4; ++i) {
            answer(*fake, [](int32_t) { return false; });
            ui.frames(10);
        }
        CHECK(fake->requests.size() == reqs, fmt("nothing more is asked once the breaker tripped (%zu requests)", fake->requests.size()));
#ifdef TURBO_FACE_TILE_DEBUG_API
        int off_tiles = 0;
        for (const auto& d : face_tile_debug())
            if (d.game_head && d.source == static_cast<int>(face_tile::Source::Unavailable) && d.why.find("stopped asking") != std::string::npos) ++off_tiles;
        CHECK(off_tiles > 0, fmt("tiles of heads never tried say why they have no picture: Turbo stopped asking (%d tiles)", off_tiles));
#endif
        // a new catalog (the folder poll / Reload catalog) re-arms the route: the heads never tried are asked again
        write_catalog(cdir, 20, false, "2026-10-30T11:30:00-04:00");
        load_catalog(app, ui, "test: E7 catalog rebuilt");
        ui.frames(4);
        CHECK(fake->requests.size() > reqs, fmt("a new catalog re-arms the route (%zu -> %zu requests)", reqs, fake->requests.size()));
#ifdef TURBO_FACE_TILE_DEBUG_API
        face_tile_debug_enable(false);
#endif
        drain(ui, *fake);
        app.capture.reset();
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::remove(cdir / (std::string("catalog_") + kKey + ".json"), ec);
        load_catalog(app, ui, "test: E7 catalog removed");
        app.hook_report = saved_hook;
    });

    // ---- T2b (re-review 2026-10-31): the breaker holds in "Render all" too. With 5 heads per request the 12th failure is the 2nd answer
    // of the 3rd request; the 3 heads after it failed once only, and eb603573 queued them again (failure() is false after the trip), so a
    // 4th request went out after "Turbo stops asking".                                  [behavioural: fails on eb603573]
    run_case("5.0c E7 T2b: Render all: once 12 hidden heads failed, the heads still in flight are not asked again (no request after the trip)", [&] {
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::create_directories(cdir, ec);
        write_catalog(cdir, 20, false, "2026-10-30T12:00:00-04:00");
        auto saved_hook = app.hook_report;
        app.hook_report = [&]() {
            HookReport r;
            r.build = kKey;
            return r;
        };
        load_catalog(app, ui, "test: E7 catalog for Render all");
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = true;
        fake->st.available = false;  // nothing goes out before Render all is pressed
        fake->st.reason = "ready";
        fake->st.max_batch = 5;
        app.capture = fake;
        app.request_tab = 0;
        ui.frames(2);
        CHECK(open_hidden_scope(ui), "open the chooser on the Hidden scope");
        ui.frames(3);
        std::string all;
        for (const auto& kv : g_items) {
            const std::string& l = kv.second.label;
            if (kv.second.frame == g_frame && l.size() > 8 && l.compare(l.size() - 8, 8, "##r3dall") == 0) all = l;
        }
        CHECK(!all.empty(), "the Render all button is shown for the hidden heads with no picture");
        if (all.empty()) return;
        CHECK(ui.click(all, "Choose a real face"), all);
        CHECK(fake->requests.empty(), "nothing sent while the game is unavailable");
        fake->st.available = true;
        for (int round = 0; round < 30; ++round) {
            answer(*fake, [](int32_t) { return false; });  // the game never renders a head id no row uses
            ui.frames(6);
        }
        // the request that carried the 12th failure
        size_t cum = 0, trip_req = fake->requests.size();
        for (size_t i = 0; i < fake->requests.size() && trip_req == fake->requests.size(); ++i) {
            for (const auto& e : capture::request_entries(fake->requests[i]))
                if (e.id >= 90000) ++cum;
            if (cum >= 12) trip_req = i;  // face_tile::kBreakerFails (a literal: the 5.0b tree has no ui_face_tile.h)
        }
        CHECK(trip_req < fake->requests.size(), fmt("12 hidden heads failed (%zu asked in %zu requests)", cum, fake->requests.size()));
        CHECK(fake->requests.size() == trip_req + 1,
              fmt("no request after the one that tripped the breaker (tripped in request %zu, %zu requests sent)", trip_req + 1, fake->requests.size()));
        drain(ui, *fake);
        app.capture.reset();
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::remove(cdir / (std::string("catalog_") + kKey + ".json"), ec);
        load_catalog(app, ui, "test: E7 catalog removed");
        app.hook_report = saved_hook;
    });

#ifdef TURBO_FACE_TILE_DEBUG_API
    // ---- T3 / T4
    run_case("5.0c E7 T3/T4: a name-match head's tile: a picture source (never 'unavailable' while a render is possible) and chips that do not overlap", [&] {
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::create_directories(cdir, ec);
        write_catalog(cdir, 0, true, "2026-10-30T12:00:00-04:00");
        auto saved_hook = app.hook_report;
        app.hook_report = [&]() {
            HookReport r;
            r.build = kKey;
            return r;
        };
        load_catalog(app, ui, "test: E7 catalog with a name match");
        CHECK(app.model.managers().size() > 0, "the test world has a manager");
        if (app.model.managers().empty()) {
            app.hook_report = saved_hook;
            return;
        }
        const int64_t mid = app.model.managers()[0].managerid;
        bool open_it = false;
        ui.extra_draw = [&] {
            if (open_it) {
                ImGui::OpenPopup(kManagerFacePopup);
                open_it = false;
            }
            manager_real_face_picker(app, mid);
        };
        face_tile_debug_enable(true);
        auto tile = [&](int64_t id) -> const FaceTileDebug* {
            for (const auto& d : face_tile_debug())
                if (d.id == id) return &d;
            return nullptr;
        };
        auto chips_ok = [&](const FaceTileDebug& d, const char* when) {
            bool inside = !d.rects.empty(), apart = true, real_font = d.drawn_w.size() == d.rects.size();
            for (size_t i = 0; i < d.rects.size(); ++i) {
                const auto& r = d.rects[i];
                inside = inside && r.x >= 0 && r.y >= 0 && r.x + r.w <= d.cell + 0.01f && r.y + r.h <= d.cell + 0.01f;
                if (i < d.drawn_w.size()) real_font = real_font && d.drawn_w[i] <= r.text_max_w + 0.5f;
                for (size_t k = i + 1; k < d.rects.size(); ++k) apart = apart && !face_tile::rects_intersect(r, d.rects[k]);
            }
            CHECK(inside, std::string(when) + ": every chip lies inside the cell");
            CHECK(apart, std::string(when) + ": no two chips overlap (5.0b: 'Hidden' and 'name match' were drawn on top of each other)");
            CHECK(real_font, std::string(when) + ": every label, measured with the real font, fits its chip");
        };

        // 3D heads off: the head has no miniface in the game; the tile says why instead of "no picture"
        app.capture.reset();
        open_it = true;
        ui.frames(4);
        const FaceTileDebug* d0 = tile(90003);
        CHECK(d0 != nullptr, "the manager chooser lists the name-match head 90003");
        if (d0) {
            CHECK(d0->chips.size() == 2 && d0->chips[0] == face_tile::BadgeKind::Hidden && d0->chips[1] == face_tile::BadgeKind::NameMatch,
                  "chips: Hidden, name match");
            CHECK(d0->source == static_cast<int>(face_tile::Source::Unavailable) && d0->why.find("switched off") != std::string::npos,
                  "3D heads off: the tile says so (" + d0->why + ")");
            chips_ok(*d0, "3D off");
        }
        // 3D heads on: a render is on its way, and a third chip joins the first two
        auto fake = std::make_shared<FakeCapture>();
        fake->st.installed = true;
        fake->st.available = true;
        fake->st.reason = "ready";
        fake->st.max_batch = 6;
        app.capture = fake;
        ui.frames(3);
        const FaceTileDebug* d1 = tile(90003);
        CHECK(d1 != nullptr, "the head is still listed");
        if (d1) {
            CHECK(d1->source == static_cast<int>(face_tile::Source::Rendering), fmt("3D heads on: the tile shows a render in progress (source %d)", d1->source));
            CHECK(d1->chips.size() == 3 && d1->chips[2] == face_tile::BadgeKind::RenderPending, "chips: Hidden, name match, 3D pending");
            chips_ok(*d1, "3D on");
        }
        if (ui.find("Close##mfaces")) ui.click("Close##mfaces");
        ui.frames(2);
        ui.extra_draw = nullptr;
        face_tile_debug_enable(false);
        fake->pending = false;
        app.capture.reset();
        fs::remove_all(r3d, ec);
        forget_renders(app);
        fs::remove(cdir / (std::string("catalog_") + kKey + ".json"), ec);
        load_catalog(app, ui, "test: E7 catalog removed");
        app.hook_report = saved_hook;
    });
#endif
}

}  // namespace face_tile_test
