// Native tests: archetype with a target overall (2.3, core/archetypes.h apply_target; the Attributes tab's archetype row
// and Create player's archetype, ui_players.cpp / ui_presets.cpp). Included by test_main.cpp: run_core() with the core
// cases, run_ui() from test_ui after the Names cases. Players are referred to by id only.
#pragma once
#include "core/archetypes.h"
#include "core/overall.h"
#include "ui/ui_archetype.h"

namespace archetype_target_test {

using turbo::overall::Attrs;
using turbo::overall::kAttrCount;
namespace ar = turbo::archetypes;
namespace ov = turbo::overall;

inline size_t ix(int i) { return static_cast<size_t>(i); }

// Synthetic players: a spread, a weak one, a strong one, a flat 1 and a flat 99, a keeper-like one
static std::vector<Attrs> bases() {
    std::vector<Attrs> v;
    Attrs a{};
    for (int i = 0; i < kAttrCount; ++i) a[ix(i)] = 40 + (i * 7 + 13) % 55;
    v.push_back(a);
    for (int i = 0; i < kAttrCount; ++i) a[ix(i)] = 25 + (i * 5 + 3) % 20;
    v.push_back(a);
    for (int i = 0; i < kAttrCount; ++i) a[ix(i)] = 78 + (i * 3 + 1) % 20;
    v.push_back(a);
    a.fill(1);
    v.push_back(a);
    a.fill(99);
    v.push_back(a);
    for (int i = 0; i < kAttrCount; ++i) a[ix(i)] = (i >= ov::GKDiving && i <= ov::GKPositioning) ? 80 : 20 + i;
    v.push_back(a);
    return v;
}

static int index_of(const char* name) {
    const auto& all = ar::all();
    for (size_t i = 0; i < all.size(); ++i)
        if (std::string(all[i].name) == name) return static_cast<int>(i);
    return -1;
}

static void run_core() {
    run_case("archetype target: every position, archetype and target 40/60/75/90/99 is hit exactly or refused with the closest", [&] {
        long hits = 0, refused = 0, outside = 0, checked = 0;
        bool all_exact = true, all_range = true, all_shape = true, all_outside_ok = true, all_refusals_ok = true, all_centered = true;
        std::string first_bad;
        auto bad = [&](const std::string& why) {
            if (first_bad.empty()) first_bad = why;
        };
        const std::vector<Attrs> bs = bases();
        for (int pos = 0; pos < 28; ++pos) {
            for (size_t ai = 0; ai < ar::all().size(); ++ai) {
                const ar::Archetype& a = ar::all()[ai];
                for (size_t b = 0; b < bs.size(); ++b) {
                    for (int target : {40, 60, 75, 90, 99}) {
                        ++checked;
                        ar::TargetResult r;
                        const bool ok = ar::apply_target(a, pos, bs[b], target, r);
                        const std::string what = fmt("pos %d %s base %zu target %d", pos, a.name, b, target);
                        if (!ok) {
                            ++refused;
                            // refused only when no attribute set reaches it: the position's range, the closest at its edge
                            if (r.ok || r.attrs != bs[b] || r.why.empty() || (target >= r.lo && target <= r.hi) ||
                                r.closest != std::max(r.lo, std::min(r.hi, target))) {
                                all_refusals_ok = false;
                                bad("refusal " + what + ": " + r.why);
                            }
                            continue;
                        }
                        ++hits;
                        if (ov::compute_overall(pos, r.attrs) != target || r.overall_after != target || !r.ok || r.closest != target) {
                            all_exact = false;
                            bad("not exact " + what);
                        }
                        if (std::fabs(r.raw_after - target) > 0.5) {
                            all_centered = false;
                            bad(fmt("unrounded %.3f ", r.raw_after) + what);
                        }
                        for (int i = 0; i < kAttrCount; ++i)
                            if (r.attrs[ix(i)] < 1 || r.attrs[ix(i)] > 99) {
                                all_range = false;
                                bad("range " + what);
                            }
                        if (!ar::keeps_shape(a, r.attrs)) {
                            all_shape = false;
                            bad("shape " + what);
                        }
                        // outside the profile: kept, unless the walk could not reach the target (then listed, and the profile clamped)
                        const bool beyond = target > r.profile_hi || target < r.profile_lo;
                        std::vector<int> moved;
                        for (int i = 0; i < kAttrCount; ++i)
                            if (!ar::in_profile(a, i) && r.attrs[ix(i)] != bs[b][ix(i)]) moved.push_back(i);
                        bool profile_at_limit = true;
                        for (int i = 0; i < kAttrCount; ++i)
                            if (ar::in_profile(a, i) && r.attrs[ix(i)] != (target > r.profile_hi ? 99 : 1)) profile_at_limit = false;
                        if (moved != r.outside_moved || (!moved.empty() && (!beyond || !profile_at_limit)) || (beyond && moved.empty())) {
                            all_outside_ok = false;
                            bad("outside " + what);
                        }
                        outside += !moved.empty();
                    }
                }
            }
        }
        CHECK(checked == 28L * static_cast<long>(ar::all().size()) * static_cast<long>(bs.size()) * 5, fmt("checked %ld", checked));
        CHECK(all_exact, "every reachable target hit exactly: " + first_bad);
        CHECK(all_centered, "the unrounded formula within 0.5 of the target: " + first_bad);
        CHECK(all_range, "every attribute 1..99: " + first_bad);
        CHECK(all_shape, "the profile's shape kept (higher profile value never lower; equal values within 1): " + first_bad);
        CHECK(all_outside_ok, "attributes outside the profile kept, moved (and listed) only beyond the profile's reach: " + first_bad);
        CHECK(all_refusals_ok, "refused only outside the position's range, with the closest reachable overall: " + first_bad);
        CHECK(hits > 0 && outside > 0, fmt("%ld hits, %ld refused, %ld with attributes outside the profile moved", hits, refused, outside));
        // 40..90 are inside every position's range: never refused
        bool mid_ok = true;
        for (int pos = 0; pos < 28; ++pos)
            for (const ar::Archetype& a : ar::all())
                for (const Attrs& b : bs)
                    for (int target : {40, 60, 75, 90}) {
                        ar::TargetResult r;
                        mid_ok = mid_ok && ar::apply_target(a, pos, b, target, r);
                    }
        CHECK(mid_ok, "targets 40, 60, 75 and 90 are reached for every position, archetype and player");
    });

    run_case("archetype target: an own-group archetype reaches 40..90 with the attributes outside its profile untouched", [&] {
        Attrs b{};
        for (int i = 0; i < kAttrCount; ++i) b[ix(i)] = 40 + (i * 7 + 13) % 55;
        bool ok = true;
        std::string why;
        for (int pos : {0, 3, 5, 7, 10, 12, 14, 16, 18, 23, 25, 27})
            for (const ar::Archetype* a : ar::of_group(ov::group_of_position(pos)))
                for (int target : {40, 60, 75, 90}) {
                    ar::TargetResult r;
                    if (!ar::apply_target(*a, pos, b, target, r) || !r.outside_moved.empty()) {
                        ok = false;
                        if (why.empty()) why = fmt("pos %d %s %d", pos, a->name, target);
                    }
                }
        CHECK(ok, "own group, 40..90: the profile alone gives it " + why);
    });

    run_case("archetype target: edges - targets near 1 and 99, unreachable positions, clamping, goalkeepers, bad input", [&] {
        Attrs b{};
        for (int i = 0; i < kAttrCount; ++i) b[ix(i)] = 40 + (i * 7 + 13) % 55;
        const ar::Archetype& poacher = ar::all()[ix(index_of("Poacher"))];
        const ar::Archetype& shot = ar::all()[ix(index_of("Shot Stopper"))];
        const ar::Archetype& defensive_fb = ar::all()[ix(index_of("Defensive Full-back"))];
        CHECK(index_of("Poacher") >= 0 && index_of("Shot Stopper") >= 0 && index_of("Defensive Full-back") >= 0, "archetypes found");
        // the position ranges: every attribute at 1 / 99 (a full-back's formula has a bonus near 2: overall 1 and 2 cannot exist)
        Attrs lo{}, hi{};
        lo.fill(1);
        hi.fill(99);
        for (int pos = 0; pos < 28; ++pos) {
            const int l = ov::compute_overall(pos, lo), h = ov::compute_overall(pos, hi);
            for (int target : {1, 2, 3, 97, 98, 99}) {
                ar::TargetResult r;
                const bool ok = ar::apply_target(poacher, pos, b, target, r);
                CHECK(ok == (target >= l && target <= h) && r.lo == l && r.hi == h &&
                          (ok ? ov::compute_overall(pos, r.attrs) == target : r.closest == std::max(l, std::min(h, target))),
                      fmt("pos %d target %d: %s (range %d..%d, closest %d)", pos, target, ok ? "hit" : "refused", l, h, r.closest));
            }
        }
        {
            ar::TargetResult r;
            CHECK(!ar::apply_target(defensive_fb, 3, b, 1, r) && r.closest == 3 && r.lo == 3 && !r.why.empty() && r.attrs == b,
                  "RB overall 1: refused, the closest is 3, attributes untouched: " + r.why);
            CHECK(ar::apply_target(defensive_fb, 3, b, r.closest, r) && ov::compute_overall(3, r.attrs) == 3, "and 3 is reached");
            CHECK(!ar::apply_target(shot, 0, b, 1, r) && r.closest == 2, fmt("GK overall 1: refused, closest %d", r.closest));
        }
        // clamping at 99: the profile alone tops out below 99, so it sits at 99 and the others move (listed)
        {
            ar::TargetResult r;
            CHECK(ar::apply_target(poacher, 0, b, 99, r) && r.profile_hi < 99 && !r.outside_moved.empty(),
                  fmt("a Poacher as a keeper at 99: the profile tops out at %d, %zu attributes outside it moved", r.profile_hi, r.outside_moved.size()));
            bool at99 = true;
            for (int i = 0; i < kAttrCount; ++i)
                if (ar::in_profile(poacher, i)) at99 = at99 && r.attrs[ix(i)] == 99;
            CHECK(at99 && r.at_limit > 0 && ov::compute_overall(0, r.attrs) == 99, "every profile attribute at 99, the overall 99");
            bool gk_moved = false;
            for (int i : r.outside_moved) gk_moved = gk_moved || (i >= ov::GKDiving && i <= ov::GKPositioning);
            CHECK(gk_moved, "the goalkeeping attributes moved for it");
        }
        // clamping at 1: a Pressing Forward leaves heading, shot power, long shots and volleys out of his profile, so a
        // strong striker's overall 3 needs the profile at 1 and those outside attributes down too
        {
            Attrs strong{};
            strong.fill(90);
            const ar::Archetype& pressing = ar::all()[ix(index_of("Pressing Forward"))];
            ar::TargetResult r;
            CHECK(ar::apply_target(pressing, 25, strong, 3, r) && ov::compute_overall(25, r.attrs) == 3 && !r.outside_moved.empty() &&
                      r.profile_lo > 3,
                  fmt("ST overall 3 from a strong player: reached, the profile alone stops at %d, %zu outside attributes moved", r.profile_lo,
                      r.outside_moved.size()));
            bool at1 = true;
            for (int i = 0; i < kAttrCount; ++i)
                if (ar::in_profile(pressing, i)) at1 = at1 && r.attrs[ix(i)] == 1;
            CHECK(at1, "every profile attribute at 1");
        }
        // goalkeepers: a keeper archetype moves the goalkeeping attributes (and reactions) only
        {
            ar::TargetResult r;
            CHECK(ar::apply_target(shot, 0, b, 80, r) && ov::compute_overall(0, r.attrs) == 80 && r.outside_moved.empty(),
                  "Shot Stopper at 80 for a GK: exact, nothing outside the profile");
            bool only_gk = true;
            for (int i = 0; i < kAttrCount; ++i)
                if (r.attrs[ix(i)] != b[ix(i)] && !(i >= ov::GKDiving && i <= ov::GKPositioning) && i != ov::Reactions) only_gk = false;
            CHECK(only_gk && r.attrs[ix(ov::GKDiving)] >= r.attrs[ix(ov::GKKicking)] && r.attrs[ix(ov::GKReflexes)] >= r.attrs[ix(ov::GKHandling)],
                  "only the goalkeeping attributes and reactions moved, diving / reflexes above kicking / handling");
        }
        // out of 1..99 and an empty profile
        {
            ar::TargetResult r;
            CHECK(!ar::apply_target(poacher, 25, b, 0, r) && r.closest == 1 && r.attrs == b, "target 0 refused, closest 1");
            CHECK(!ar::apply_target(poacher, 25, b, 100, r) && r.closest == 99 && r.attrs == b, "target 100 refused, closest 99");
            ar::Archetype empty = poacher;
            empty.profile.fill(0);
            ar::Result kept;
            CHECK(!ar::apply_target(empty, 25, b, 70, r) && !r.why.empty() && r.attrs == b && !ar::apply(empty, 25, b, kept),
                  "an archetype with no profile is refused");
        }
    });

    run_case("archetype target: a higher target never lowers a profile attribute; the shape check; the default target", [&] {
        const std::vector<Attrs> bs = bases();
        bool mono = true;
        std::string why;
        for (int pos : {0, 3, 5, 10, 14, 18, 23, 25})
            for (const ar::Archetype& a : ar::all()) {
                Attrs prev{};
                bool have = false;
                for (int target = 1; target <= 99; ++target) {
                    ar::TargetResult r;
                    if (!ar::apply_target(a, pos, bs[0], target, r)) continue;
                    if (have)
                        for (int i = 0; i < kAttrCount; ++i)
                            if (ar::in_profile(a, i) && r.attrs[ix(i)] < prev[ix(i)]) {
                                mono = false;
                                if (why.empty()) why = fmt("pos %d %s target %d attr %d", pos, a.name, target, i);
                            }
                    prev = r.attrs;
                    have = true;
                }
            }
        CHECK(mono, "profile attributes never fall when the target rises " + why);
        const ar::Archetype& poacher = ar::all()[ix(index_of("Poacher"))];
        Attrs s{};
        s.fill(50);
        CHECK(ar::keeps_shape(poacher, s), "flat attributes keep any shape");
        s[ix(ov::Finishing)] = 40;  // Finishing is the Poacher's highest value: below the others breaks the shape
        CHECK(!ar::keeps_shape(poacher, s), "a top attribute below a lower one breaks the shape");
        CHECK(ar::default_target(80, 79) == 80 && ar::default_target(79, 80) == 79, "default: the stored overall when it is sane");
        CHECK(ar::default_target(1, 75) == 75 && ar::default_target(5, 60) == 60, "broken overall (OVR-1): the formula's");
        CHECK(ar::default_target(4, 6) == 4, "a low overall near the formula stays");
        CHECK(ar::default_target(0, 60) == 60 && ar::default_target(120, 60) == 60 && ar::default_target(0, 120) == 99, "outside 1..99: the formula, clamped");
        // the archetype keeping the overall (apply) still keeps it exactly, now through apply_target
        bool kept = true;
        for (int pos = 0; pos < 28; ++pos)
            for (const ar::Archetype& a : ar::all())
                for (const Attrs& b : bs) {
                    ar::Result r;
                    kept = kept && ar::apply(a, pos, b, r) && r.exact && ov::compute_overall(pos, r.attrs) == ov::compute_overall(pos, b);
                }
        CHECK(kept, "apply(): the overall kept exactly for every position, archetype and player");
    });
}

// ---------------------------------------------------------------- UI
static Attrs db_attrs(App& app, const Table& t, uint64_t rec) {
    Attrs a{};
    for (int i = 0; i < kAttrCount; ++i) a[ix(i)] = static_cast<int>(app.db.get_int(t, rec, ov::kAttrNames[i], 0));
    return a;
}

static void run_ui(App& app, Ui& ui, SimMemory& mem, uint64_t kMb) {
    run_case("UI: archetype with a target overall - role, position, target, preview, Apply (read back), Undo; refusal and Use N", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.request_tab = 0;
        ui.frames(3);
        const Table* pt = app.db.table("players");
        CHECK(pt != nullptr, "players table");
        if (!pt) return;
        const int64_t pid = 2002;
        const uint64_t rec = app.db.find(*pt, "playerid", pid);
        CHECK(rec != 0, "player 2002");
        if (!rec) return;
        const Attrs orig = db_attrs(app, *pt, rec);
        const int64_t pos_orig = app.db.get_int(*pt, rec, "preferredposition1"), ovr_orig = app.db.get_int(*pt, rec, "overallrating");
        // a striker whose stored overall is one above the formula (both are shown; the target is the formula's result)
        std::string err;
        const int st_pos = 25;
        const int formula = ov::compute_overall(st_pos, orig);
        CHECK(app.db.set_int(*pt, rec, "preferredposition1", st_pos, &err) && app.db.set_int(*pt, rec, "overallrating", formula + 1, &err),
              "2002 as a striker, stored overall = formula + 1: " + err);
        app.model.refresh_player(pid, app.today());
        // 2026-10-31 (5.0c menus step 6): the classic tab (##plist / ##pedit / ##attr) is gone; the bar is the Player type & tier card of
        // Players > Player > Attributes & roles (ui_player_profile.cpp: g_ability_archetype_bar), the list is the ##select pane, the editor ##editor
        CHECK(ui.click("Players"), "Players tab");
        CHECK(ui.click("###pe_ability"), "Attributes & roles tab");
        ui.frames(2);
        CHECK(ui.type_into(ui.find("##psearch", "##select"), "2002"), "search 2002");
        CHECK(ui.click("2002", "##select") && app.sel_player == pid, "row 2002");
        ui.frames(2);
        const turbo::ArchetypeUiState& st = turbo::archetype_ui_state();
        CHECK(st.playerid == pid && st.position == st_pos && st.target == formula + 1 && !st.target_typed,
              fmt("defaults: his position, his stored overall (%d, position %d)", st.target, st.position));
        CHECK(ui.find("##archtarget", "##editor") && ui.find("##archpos", "##editor") && ui.find("###archpreview", "##editor"),
              "role, position, target box and the preview button");

        // Poacher at 80
        CHECK(ui.click("##arch", "##editor") && ui.click("Poacher", "##Combo"), "pick Poacher");
        CHECK(st.arch == index_of("Poacher"), "Poacher chosen");
        CHECK(ui.type_into(ui.find("##archtarget", "##editor"), "80") && st.target == 80 && st.target_typed, "target 80 typed");
        CHECK(ui.click("###archpreview", "##editor"), "preview");
        ui.frames(2);
        CHECK(st.pend_playerid == pid && st.pend.ok && st.pend.target == 80 && ov::compute_overall(st_pos, st.pend.attrs) == 80 &&
                  ar::keeps_shape(ar::all()[ix(st.pend_arch)], st.pend.attrs) && st.pend_stored == formula + 1 && st.pend.overall_before == formula,
              fmt("preview: exact 80 for ST, stored %lld and formula %d both known", static_cast<long long>(st.pend_stored), st.pend.overall_before));
        {
            ar::TargetResult want;
            ar::apply_target(ar::all()[ix(index_of("Poacher"))], st_pos, db_attrs(app, *pt, rec), 80, want);
            CHECK(want.attrs == st.pend.attrs, "the preview is the core's result");
        }
        CHECK(ui.find("Apply##arch", "Apply archetype") != nullptr && ui.find("Cancel##arch", "Apply archetype") != nullptr, "preview window with Apply");
        CHECK(db_attrs(app, *pt, rec) == orig, "nothing written by the preview");
        const Attrs want80 = st.pend.attrs;
        const size_t bulk0 = app.bulk_undo.size();
        CHECK(ui.click("Apply##arch", "Apply archetype"), "Apply");
        ui.frames(2);
        CHECK(db_attrs(app, *pt, rec) == want80 && app.db.get_int(*pt, rec, "overallrating") == 80 && app.db.get_int(*pt, rec, "preferredposition1") == st_pos,
              fmt("written: the attributes, the stored overall 80 (%lld), the position unchanged", static_cast<long long>(app.db.get_int(*pt, rec, "overallrating"))));
        CHECK(ov::compute_overall(st_pos, db_attrs(app, *pt, rec)) == 80, "the formula gives 80 from what is in the database");
        CHECK(app.model.player(pid) && app.model.player(pid)->overall == 80, "the list row shows 80");
        CHECK(ui.toast_contains("Player type Poacher at overall 80") && ui.toast_contains("read back"), "toast: applied and read back");
        CHECK(app.bulk_undo.size() == bulk0 + 1 && app.bulk_undo.back().playerid == pid && app.bulk_undo.back().label == st.applied_label,
              "one bulk undo entry: " + st.applied_label);
        CHECK(ui.find("###archundo", "##editor") != nullptr, "Undo shown after Apply");
        CHECK(ui.click("###archundo", "##editor"), "Undo");
        ui.frames(2);
        CHECK(db_attrs(app, *pt, rec) == orig && app.db.get_int(*pt, rec, "overallrating") == formula + 1, "Undo: attributes and overall back");
        CHECK(ui.find("###archundo", "##editor") == nullptr, "Undo gone once used");

        // unreachable: a full-back's overall cannot be 1 (his formula's bonus): refused, the closest offered
        CHECK(ui.click("##archpos", "##editor") && ui.click("RB", "##Combo") && st.position == 3 && st.position_picked, "position RB");
        CHECK(ui.click("##arch", "##editor") && ui.click("Defensive Full-back", "##Combo"), "pick Defensive Full-back");
        CHECK(ui.type_into(ui.find("##archtarget", "##editor"), "1") && st.target == 1, "target 1");
        CHECK(ui.click("###archpreview", "##editor"), "preview");
        ui.frames(2);
        CHECK(!st.pend.ok && st.pend.closest == 3 && !st.pend.why.empty(), fmt("refused, closest %d: %s", st.pend.closest, st.pend.why.c_str()));
        CHECK(ui.click("Apply##arch", "Apply archetype"), "click the disabled Apply");
        CHECK(db_attrs(app, *pt, rec) == orig && app.bulk_undo.size() == bulk0, "refused: nothing written");
        CHECK(ui.click("Use 3##archclosest", "Apply archetype"), "Use 3");
        ui.frames(1);
        CHECK(st.pend.ok && st.pend.target == 3 && st.target == 3 && ov::compute_overall(3, st.pend.attrs) == 3, "the preview now reaches 3");
        CHECK(ui.click("Cancel##arch", "Apply archetype"), "Cancel");
        CHECK(db_attrs(app, *pt, rec) == orig, "Cancel: nothing written");

        // another position, made his: Stopper as a CB at 70 writes preferredposition1 too
        CHECK(ui.click("##archpos", "##editor") && ui.click("CB", "##Combo") && st.position == 5, "position CB");
        CHECK(ui.click("##arch", "##editor") && ui.click("Stopper", "##Combo"), "pick Stopper");
        CHECK(ui.type_into(ui.find("##archtarget", "##editor"), "70"), "target 70");
        CHECK(ui.click("###archpreview", "##editor"), "preview");
        ui.frames(2);
        CHECK(st.pend.ok && st.pend_position == 5 && st.pend_his_position == st_pos && st.adopt_position, "preview for CB, adopt on by default");
        CHECK(ui.find("Make CB his main position (now ST)##archadopt", "Apply archetype") != nullptr, "the adopt checkbox");
        CHECK(ui.click("Apply##arch", "Apply archetype"), "Apply");
        ui.frames(2);
        CHECK(app.db.get_int(*pt, rec, "preferredposition1") == 5 && app.db.get_int(*pt, rec, "overallrating") == 70 &&
                  ov::compute_overall(5, db_attrs(app, *pt, rec)) == 70,
              "now a CB with overall 70 by the CB formula");
        CHECK(ui.click("###archundo", "##editor"), "Undo");
        CHECK(db_attrs(app, *pt, rec) == orig && app.db.get_int(*pt, rec, "preferredposition1") == st_pos &&
                  app.db.get_int(*pt, rec, "overallrating") == formula + 1,
              "Undo: attributes, position and overall back");

        // the same, his position kept: his stored overall follows what the ST formula moved (the 2.1 follow rule)
        CHECK(ui.click("###archpreview", "##editor"), "preview again");
        ui.frames(2);
        CHECK(ui.click("Make CB his main position (now ST)##archadopt", "Apply archetype") && !st.adopt_position, "adopt off");
        const int follow_want = ov::follow(st_pos, orig, st_pos, st.pend.attrs, formula + 1);
        CHECK(ui.click("Apply##arch", "Apply archetype"), "Apply");
        ui.frames(2);
        CHECK(app.db.get_int(*pt, rec, "preferredposition1") == st_pos && app.db.get_int(*pt, rec, "overallrating") == follow_want &&
                  ov::compute_overall(5, db_attrs(app, *pt, rec)) == 70,
              fmt("still a ST, stored overall %lld (follow %d), the CB formula 70", static_cast<long long>(app.db.get_int(*pt, rec, "overallrating")), follow_want));
        CHECK(ui.click("###archundo", "##editor") && db_attrs(app, *pt, rec) == orig &&
                  app.db.get_int(*pt, rec, "overallrating") == formula + 1, "Undo");
        // a broken stored overall (OVR-1): the default target is the formula's value
        CHECK(ui.click("##archpos", "##editor") && ui.click("ST", "##Combo") && st.position == st_pos && !st.position_picked, "his position again");
        CHECK(app.db.set_int(*pt, rec, "overallrating", 1, &err), "stored overall 1");
        app.model.refresh_player(pid, app.today());
        CHECK(ui.type_into(ui.find("##psearch", "##select"), "1002") && ui.click("1002", "##select") && app.sel_player == 1002, "another player");
        ui.frames(2);
        CHECK(ui.type_into(ui.find("##psearch", "##select"), "2002") && ui.click("2002", "##select") && app.sel_player == pid, "back to 2002");
        ui.frames(2);
        CHECK(!st.target_typed && st.target == formula, fmt("broken stored overall: the default target is the formula's %d (%d)", formula, st.target));
        CHECK(app.db.set_int(*pt, rec, "preferredposition1", pos_orig, &err) && app.db.set_int(*pt, rec, "overallrating", ovr_orig, &err),
              "2002 as he was: " + err);
        app.model.refresh_player(pid, app.today());
        CHECK(ui.type_into(ui.find("##psearch", "##select"), ""), "search cleared");
        (void)mem;
        (void)kMb;
    });

    // 2.22 (G-ia): the dialog's own Archetype box (the attributes-only reshape, ##carch) is gone: its Player type & tier section (Populate) is the one
    // picker and fills the whole form, so this case now goes through that section. The core reshape stays proven by the cases above.
    run_case("UI: Create player: Player type & tier (Populate) with a target overall sends the attributes it previewed; one picker; the closest overall is offered", [&] {
        if (app.busy()) ui.click("Cancel");
        app.lua_queue.clear();
        app.request_tab = 0;
        ui.frames(3);
        const Table* pt = app.db.table("players");
        if (!pt) return;
        // 2026-10-31 (5.0c menus step 6): the classic tab (##plist / ##pedit) is gone; the template is picked on Players > Create player's own
        // list (rows "<name>###pc<id>", ui_player_creation.cpp) and the Create player... button is in that page's editor panel
        app.go(turbo::shell::Page::CreatePlayer);
        ui.frames(4);
        CHECK(ui.click("###pc1002") && app.sel_player == 1002, "template 1002");
        const uint64_t rec = app.db.find(*pt, "playerid", 1002);
        const Attrs tmpl = db_attrs(app, *pt, rec);
        CHECK(ui.click("Create player..."), "create dialog");
        ui.frames(2);
        CHECK(ui.find("##carch", "##pcreate") == nullptr, "the old Archetype box is gone: one Player type picker");
        CHECK(ui.type_into(ui.find("##first", "##pcreate"), "Arch"), "first name");
        CHECK(ui.type_into(ui.find("##last", "##pcreate"), "Target"), "surname");
        CHECK(ui.click("##cparch", "##pcreate") && ui.click("Poacher", "##Combo"), "player type Poacher");
        CHECK(ui.type_into(ui.find("##cptarget", "##pcreate"), "80"), "wanted overall 80");
        CHECK(ui.click("Review changes...###cpopreview", "##pcreate"), "review");
        ui.frames(2);
        CHECK(ui.click("Populate##cpop", "Fill the whole player"), "Populate");
        ui.frames(2);
        const nlohmann::json pending = create_prefill_state().pending;
        CHECK(ui.click("Create player", "##pcreate"), "Create player");
        CHECK(app.busy(), "command queued");
        json cmd = json::parse(mem.read_cstr(kMb + 0x20, 0x1000), nullptr, false);
        CHECK(!cmd.is_discarded() && cmd["module"] == "create_player", "create_player command");
        const json set = cmd.is_discarded() ? json::object() : cmd["overrides"]["set"];
        Attrs sent = tmpl;
        int n_attrs = 0;
        for (int i = 0; i < kAttrCount; ++i)
            if (set.contains(ov::kAttrNames[i])) {
                sent[ix(i)] = set[ov::kAttrNames[i]].get<int>();
                ++n_attrs;
            }
        CHECK(n_attrs > 0 && set.contains("overallrating") && set["overallrating"] == 80 && set.contains("preferredposition1"),
              fmt("set: %d attributes, overall 80, a position", n_attrs));
        if (set.contains("preferredposition1")) CHECK(ov::compute_overall(set["preferredposition1"].get<int>(), sent) == 80, "the sent attributes give 80 by his position's formula");
        (void)pending;
        CHECK(ui.click("Cancel"), "cancel the command");

        // unreachable in the dialog: a full-back at 1 - the review says so, Create stays on the form, "Use N" sets the closest
        CHECK(ui.click("Create player..."), "create dialog again");
        ui.frames(2);
        CHECK(ui.click("##cparch", "##pcreate") && ui.click("Defensive Full-back", "##Combo"), "player type Defensive Full-back");
        CHECK(ui.type_into(ui.find("##cptarget", "##pcreate"), "1"), "wanted overall 1");
        CHECK(ui.click("Review changes...###cpopreview", "##pcreate"), "review");
        ui.frames(2);
        const ItemRec* use = nullptr;
        for (const auto& kv : g_items)
            if (kv.second.frame == g_frame && kv.second.label.rfind("Use ", 0) == 0 && kv.second.label.find("##cpopclosest") != std::string::npos) use = &kv.second;
        CHECK(use != nullptr, "the closest overall is offered (Use N)");
        CHECK(ui.find("Populate##cpop", "Fill the whole player") != nullptr, "Populate is there, disabled while nothing can be filled");
        if (use) {
            CHECK(ui.click(use), "Use N");
            ui.frames(2);
        }
        if (ui.find("Cancel##cpop", "Fill the whole player")) CHECK(ui.click("Cancel##cpop", "Fill the whole player"), "close the review");
        CHECK(ui.click("Cancel##pcreate", "##pcreate"), "close the dialog");
    });
}

}  // namespace archetype_target_test
