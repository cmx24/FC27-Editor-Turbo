// Native tests: Teams > Tactics (ui/ui_tactics.cpp, ui/pitch_view.cpp, core/tactics_db.cpp, Turbo 2.0). Included by test_main.cpp (after
// FakeMatchSetup), run from test_ui; the cases drive the real panel through the mouse and read what it drew from tactics_tab_state().
// The simulated game (gui_world.lua) carries the real schema's formation tables: teamformationteamstylelinks, formations (+ formationoffsets)
// and the mentality tables cm_mentalities / mentalities, with a few teams that exercise every way the saved formation can be found.
#pragma once
#include "core/opp_rules.h"
#include "core/opposition.h"
#include "core/sliders.h"
#include "core/tactic_profiles.h"
#include "core/tactics.h"
#include "core/tactics_db.h"
#include "core/tactics_status.h"
#include "ui/file_picker.h"
#include "ui/pitch_view.h"
#include "ui/ui_tactics.h"

namespace tactics_tab_test {

static void click_at(Ui& ui, float x, float y) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    ui.frames(2);
    io.AddMouseButtonEvent(0, true);
    ui.frame();
    io.AddMouseButtonEvent(0, false);
    ui.frames(3);
}

static void select_team(App& app, Ui& ui, int id) {
    app.request_tab = 1;
    ui.frames(2);
    ui.click(std::to_string(id), "##tlist");
    ui.click("Tactics", "##tedit");
    ui.frames(3);
}

static void go_scope(Ui& ui, const char* label) {
    ui.click(std::string(label) + "##scope", "##tacleft");
    ui.frames(3);
}

static bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// "Show legacy fields" (the width / box / style sliders EA removed in FC 25 are hidden by default): tick or untick it
static void show_legacy(Ui& ui, bool on) {
    if (tactics_tab_state().show_legacy == on) return;
    ui.click("Show legacy fields##taclegacy", "##tacright");
    ui.frames(2);
}

// Apply: untested / legacy settings open "Apply untested settings?" first; confirm it when it is there
static bool apply_confirmed(Ui& ui) {
    if (!ui.click("Apply##tac")) return false;
    ui.frames(2);
    if (tactics_tab_state().confirm_open) {
        if (!ui.click("Apply anyway##tacconfirm", "Apply untested settings?")) return false;
        ui.frames(2);
    }
    return true;
}

static bool has_part(const std::vector<std::string>& v, const std::string& part) {
    for (const std::string& s : v)
        if (s.find(part) != std::string::npos) return true;
    return false;
}

static const TacticsTabState::Row* row_of(const char* key) {
    for (const auto& r : tactics_tab_state().rows)
        if (r.key == key) return &r;
    return nullptr;
}

// The slider cards scroll: bring the row of `key` into view (the card list's scroll is set from where the item was last laid out) and
// return its item. The item is looked up without the clip test, the way a person scrolls to it.
static ImGuiWindow* cards_window() {
    for (ImGuiWindow* w : GImGui->Windows)
        if (w->Active && std::string(w->Name).find("##tacsliders") != std::string::npos) return w;
    return nullptr;
}

static const ItemRec* slider_item(Ui& ui, const char* label, const char* key) {
    ImGuiWindow* w = cards_window();
    if (!w) return nullptr;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (const ItemRec* r = ui.find(label, "##tacsliders", key)) return r;
        const ItemRec* hit = nullptr;
        for (const auto& kv : g_items) {
            const ItemRec& r = kv.second;
            if (r.frame != g_frame || r.window.find("##tacsliders") == std::string::npos) continue;
            if (r.label != label && !(r.label.empty() && ImHashStr(label, 0, r.seed) == r.id)) continue;
            if (ImHashStr(key, 0, r.seed2) != r.seed) continue;
            hit = &r;
        }
        if (!hit) return nullptr;
        const float mid = hit->rect.GetCenter().y, view = w->InnerRect.Min.y + w->InnerRect.GetHeight() * 0.5f;
        ImGui::SetScrollY(w, std::max(0.0f, w->Scroll.y + (mid - view)));
        ui.frames(2);
    }
    return nullptr;
}

// The row's Reset button, found by its ID (the row's "##on" checkbox gives the ID stack of the row): "Reset##reset" under PushID(key)
static const ItemRec* reset_button(Ui& ui, const char* key) {
    const ItemRec* on = slider_item(ui, "##on", key);
    if (!on) {
        ui.dump(std::string("row of ") + key);
        return nullptr;
    }
    const ImGuiID id = ImHashStr("Reset##reset", 0, on->seed);
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto it = g_items.find(id);
        if (it != g_items.end() && it->second.frame == g_frame && it->second.rect.GetWidth() > 0) {
            const ItemRec& r = it->second;
            if (r.clip.Contains(r.rect.GetCenter())) return &r;
            // laid out but scrolled out of the card list's view: bring it to the middle, the way a person scrolls to it
            if (ImGuiWindow* w = cards_window()) {
                const float mid = r.rect.GetCenter().y, view = w->InnerRect.Min.y + w->InnerRect.GetHeight() * 0.5f;
                ImGui::SetScrollY(w, std::max(0.0f, w->Scroll.y + (mid - view)));
            }
        }
        ui.frames(2);
    }
    ui.dump(std::string("reset button of ") + key);
    return nullptr;
}

static bool type_slider(Ui& ui, const char* key, const char* value) {
    const ItemRec* typed = slider_item(ui, "##typed", key);
    if (!typed) {
        ui.dump(std::string("typed box of ") + key);
        return false;
    }
    return ui.type_into(typed, value);
}

static std::vector<std::string> labels_of(const Formation& f) {
    std::vector<std::string> v;
    for (const FormationSlot& s : f.slots) v.push_back(s.label);
    return v;
}

static const Profile* profile_named(App& app, const std::string& name, ProfileCategory c) {
    for (const Profile& p : app.tactic_profiles.profiles)
        if (p.category == c && p.name == name) return &p;
    return nullptr;
}

static fs::path real_schema_path() {
    fs::path schema;
    if (const char* t = std::getenv("TURBO_TESTS")) schema = fs::path(t) / ".." / "le27" / "fc27_db_schema.json";
    if (schema.empty() || !fs::exists(schema)) schema = fs::path(g_lua).parent_path() / ".." / ".." / ".." / "turbo" / "le27" / "fc27_db_schema.json";
    return schema;
}

// ---------------------------------------------------------------------------------------------------------------- core reads (no UI)
static void cases_db(App& app) {
    run_case("tactics db: position codes become pitch labels", [&] {
        CHECK(pitch_label_for_position(0) == "GK" && pitch_label_for_position(7) == "LB" && pitch_label_for_position(25) == "ST", "plain codes");
        CHECK(pitch_label_for_position(4) == "CB" && pitch_label_for_position(6) == "CB" && pitch_label_for_position(5) == "CB", "RCB, LCB, CB: CB");
        CHECK(pitch_label_for_position(9) == "CDM" && pitch_label_for_position(11) == "CDM", "RDM, LDM: CDM");
        CHECK(pitch_label_for_position(13) == "CM" && pitch_label_for_position(15) == "CM", "RCM, LCM: CM");
        CHECK(pitch_label_for_position(17) == "CAM" && pitch_label_for_position(19) == "CAM", "RAM, LAM: CAM");
        CHECK(pitch_label_for_position(24) == "ST" && pitch_label_for_position(26) == "ST", "RS, LS: ST");
        CHECK(pitch_label_for_position(30) == "?" && pitch_label_for_position(-1) == "?", "unknown codes are '?', never a crash");
    });

    run_case("tactics db: the saved formation of a team - style link, name fallback, formationoffsets, a flipped axis, the tactic route, nothing", [&] {
        // Arsenal: the style link names formation 10, a real 4-3-3
        const TeamFormation a = read_team_formation(app.db, 1);
        CHECK(a.saved && a.formation.from_db && a.note.empty(), "Arsenal: saved formation, no note: " + a.note);
        CHECK(a.formation.id == "10" && a.formation.name == "4-3-3" && a.formation.slots.size() == 11, "formation 10, 4-3-3, eleven slots");
        CHECK(labels_of(a.formation) == std::vector<std::string>({"GK", "LB", "CB", "CB", "RB", "CDM", "CM", "CM", "LW", "ST", "RW"}),
              "position codes became pitch labels");
        CHECK(a.source.find("saved formation 10") != std::string::npos && a.source.find("style link") != std::string::npos, "source: " + a.source);
        CHECK(a.formation.slots[0].group == PosGroup::GK && a.formation.slots[9].group == PosGroup::Att && a.formation.slots[5].group == PosGroup::Mid,
              "groups follow the labels");
        CHECK(std::fabs(a.formation.slots[9].x - 0.5f) < 1e-6f && std::fabs(a.formation.slots[9].y - 0.85f) < 1e-6f, "the striker's saved offsets");
        // Everton: the link names formation 2, a stub row without positions: the name picks Turbo's own 4-4-2
        const TeamFormation e = read_team_formation(app.db, 7);
        CHECK(!e.saved && !e.formation.from_db && e.formation.id == "4-4-2" && e.source.empty(), "Everton: Turbo's built-in 4-4-2");
        CHECK(e.note.find("built-in 4-4-2 shape") != std::string::npos && e.note.find("positions could not be read") != std::string::npos, "the reason: " + e.note);
        // England: positions in formations, offsets zero there, the real ones in formationoffsets
        const TeamFormation en = read_team_formation(app.db, 1318);
        CHECK(en.saved && en.formation.name == "3-5-2" && en.source.find("formationoffsets") != std::string::npos, "England: offsets from formationoffsets: " + en.source);
        CHECK(labels_of(en.formation) == std::vector<std::string>({"GK", "CB", "CB", "CB", "LWB", "CM", "CDM", "CM", "RWB", "ST", "ST"}), "3-5-2 labels");
        CHECK(std::fabs(en.formation.slots[4].x - 0.08f) < 1e-6f, "wing-back offset from the formationoffsets row");
        // Inter: a y axis that runs the other way (goalkeeper at 0.96) is turned round
        const TeamFormation in = read_team_formation(app.db, 241);
        CHECK(in.saved && in.formation.id == "12", "Inter: formation 12");
        CHECK(std::fabs(in.formation.slots[0].y - 0.04f) < 1e-5f && std::fabs(in.formation.slots[9].y - 0.82f) < 1e-5f, "y turned round: the goalkeeper at his own goal line");
        // Free Agents: no link; the team's active tactic in cm_mentalities names formation 10
        const TeamFormation fa = read_team_formation(app.db, 111592);
        CHECK(fa.saved && fa.formation.id == "10" && fa.source.find("cm_mentalities") != std::string::npos, "Free Agents: via the tactic row: " + fa.source);
        // a team the save knows nothing about: a built-in shape and a plain reason, never an error
        const TeamFormation nobody = read_team_formation(app.db, 999999);
        CHECK(!nobody.saved && nobody.formation.slots.size() == 11 && !nobody.note.empty(), "unknown team: built-in shape, reason: " + nobody.note);
        // every saved formation passes the core validation again
        for (const TeamFormation* t : {&a, &en, &in, &fa}) {
            Formation again;
            std::string err;
            CHECK(make_formation(t->formation.id, t->formation.name, t->formation.slots, again, &err), "make_formation accepts it: " + err);
        }
        // a formation id that is not there
        Formation f;
        std::string src, why;
        CHECK(!read_formation_by_id(app.db, 777, f, src, why) && why.find("777") != std::string::npos, "unknown id: " + why);
        CHECK(!read_formation_by_id(app.db, 1, f, src, why) && !why.empty(), "a stub row without a usable shape is refused: " + why);
    });

    run_case("tactics db: the rows of a team in the mentality tables (the active tactic first)", [&] {
        const Table* cm = app.db.table("cm_mentalities");
        const Table* m = app.db.table("mentalities");
        CHECK(cm && m, "both tables are in the simulated save");
        if (!cm || !m) return;
        const TeamRows a = team_rows(app.db, *cm, 1);
        CHECK(a.recs.size() == 2 && a.has_active && a.chosen == 1, fmt("Arsenal: 2 rows, the second is the active tactic (%zu rows, chosen %zu)", a.recs.size(), a.chosen));
        CHECK(app.db.get_int(*cm, a.recs[a.chosen], "artificialkey") == 2, "the chosen row is artificialkey 2");
        const TeamRows e = team_rows(app.db, *cm, 7);
        CHECK(e.recs.size() == 1 && !e.has_active && e.chosen == 0, "Everton: one row, no flag: the first");
        CHECK(team_rows(app.db, *cm, 241).recs.empty(), "Inter has no row");
        CHECK(team_rows(app.db, *m, 1).recs.size() == 1 && team_rows(app.db, *m, 1).has_active, "mentalities: one flagged row for Arsenal");
        const Table* teams = app.db.table("teams");
        CHECK(team_rows(app.db, *teams, 1).recs.size() == 1 && !team_rows(app.db, *teams, 1).has_active, "teams has teamid but no activetactic: its one row");
        const Table* version = app.db.table("version");
        CHECK(team_rows(app.db, *version, 1).key_missing, "a table without teamid is reported, not guessed");
    });

    run_case("tactics db: the real schema has every table and field the reader and the team sliders use (skipped without the schema file)", [&] {
        const fs::path schema = real_schema_path();
        if (!fs::exists(schema)) {
            std::printf("    (no fc27_db_schema.json: schema check skipped)\n");
            CHECK(true, "skipped");
            return;
        }
        const json js = read_json(schema);
        CHECK(js.contains("tables"), "schema has tables");
        const json& T = js["tables"];
        auto field = [&](const char* table, const std::string& name) -> const json* {
            if (!T.contains(table)) return nullptr;
            for (const json& f : T[table]["fields"])
                if (f.value("name", "") == name) return &f;
            return nullptr;
        };
        for (const char* tb : {"formations", "formationoffsets"}) {
            const json* id = field(tb, "formationid");
            CHECK(id && (*id)["pkey"].get<bool>(), std::string(tb) + ".formationid is the key");
            for (int i = 0; i < 11; ++i) {
                for (const char* axis : {"x", "y"}) {
                    const json* o = field(tb, "offset" + std::to_string(i) + axis);
                    CHECK(o && (*o)["type"].get<int>() == 4, std::string(tb) + ".offset" + std::to_string(i) + axis + " is a decimal number");
                }
                if (std::string(tb) == "formations") CHECK(field(tb, "position" + std::to_string(i)) != nullptr, "formations.position" + std::to_string(i));
            }
        }
        CHECK(field("formations", "formationname") != nullptr && field("formations", "teamid") != nullptr, "formations: name and teamid");
        const json* tl = field("teamformationteamstylelinks", "teamid");
        CHECK(tl && (*tl)["pkey"].get<bool>() && field("teamformationteamstylelinks", "formationid") != nullptr, "the style link: teamid -> formationid");
        for (const char* tb : {"cm_mentalities", "mentalities"}) {
            CHECK(field(tb, "teamid") != nullptr, std::string(tb) + " is keyed by teamid");
            CHECK(field(tb, "activetactic") != nullptr && field(tb, "sourceformationid") != nullptr, std::string(tb) + " flags the active tactic and names its formation");
        }
        // every DB slider that the team scope writes through a mentality table can find its row by teamid
        size_t seen = 0;
        for (const SliderDef& d : slider_registry()) {
            if (d.binding.kind != BindingKind::DbField) continue;
            for (const std::string& t : d.binding.targets) {
                const std::string table = t.substr(0, t.find('.'));
                if (table != "mentalities" && table != "cm_mentalities") continue;
                ++seen;
                CHECK(field(table.c_str(), "teamid") != nullptr, d.key + ": " + table + " can be found by teamid");
            }
        }
        CHECK(seen >= 8, fmt("mentality-bound targets checked: %zu", seen));
    });
}

// ---------------------------------------------------------------------------------------------------------------- the panel
static void cases_panel(App& app, Ui& ui, const fs::path& le) {
    run_case("pitch view: unit strings are recognised, the model's labels never carry one", [&] {
        CHECK(pitch_label_has_unit("19 m"), "19 m");
        CHECK(pitch_label_has_unit("48m deep"), "48m");
        CHECK(pitch_label_has_unit("about 20 metres"), "metres");
        CHECK(pitch_label_has_unit("30 yd"), "yd");
        CHECK(pitch_label_has_unit("2 km"), "km");
        CHECK(!pitch_label_has_unit("62/100 (schematic)"), "62/100 (schematic)");
        CHECK(!pitch_label_has_unit("Relative intensity (arbitrary)"), "relative intensity");
        CHECK(!pitch_label_has_unit("Defensive line 7/100 (schematic)"), "defensive line");
        CHECK(std::string(pitch_chip_text()) == "Model view: Turbo's estimate, not game output", "the chip text");
        // the labels the core model gives (everything on) are unit-free
        SliderSet team;
        PreviewOptions o;
        o.show_heat = o.show_roam = o.show_risk = true;
        const PreviewModel m = build_preview(*find_fallback_formation("4-3-3"), team, SliderSet{}, o);
        CHECK(!m.derived_modelled_labels().empty(), "the model has labels");
        for (const std::string& l : m.derived_modelled_labels()) CHECK(!pitch_label_has_unit(l), "no unit: " + l);
        // the primitive estimate stays within the cap with every layer on (the grid coarsens to fit)
        PitchDrawOptions po;
        CHECK(pitch_primitive_estimate(m, po) < kPitchPrimitiveCap, fmt("estimate without the grid: %d", pitch_primitive_estimate(m, po)));
    });

    run_case("registry: every slider has a status, only Live and DB are ever written, Preview and RE are not", [&] {
        CHECK(validate_registry().empty(), "registry validates: " + validate_registry());
        for (const SliderDef& d : slider_registry()) {
            const bool game = d.status == SliderStatus::Live || d.status == SliderStatus::DB;
            CHECK(writes_game(d) == game, "writes_game agrees with the status: " + d.key);
            CHECK(preview_only(d) == (d.status == SliderStatus::Preview || d.status == SliderStatus::RE), "preview_only: " + d.key);
        }
        SliderSet s;
        for (const SliderDef& d : slider_registry()) s.set(d.key, d.def, true);  // everything switched on
        for (const SliderWrite& w : game_writes(s)) {
            const SliderDef* d = find_slider(w.key);
            CHECK(d && writes_game(*d), "a write is only listed for a Live / DB slider: " + w.key);
        }
    });

    run_case("UI: Teams > Tactics: Match scope, a Live slider, Apply sends the game variable, preview-only rows never", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        std::error_code ec;
        fs::remove(le / "turbo_output" / "tactics_off.txt", ec);
        app.request_tab = 1;
        ui.frames(3);
        CHECK(ui.click("7", "##tlist"), "Everton row");
        CHECK(app.sel_team == 7, "team selected");
        CHECK(ui.click("Tactics", "##tedit"), "Tactics tab");
        ui.frames(3);
        const TacticsTabState& st = tactics_tab_state();
        CHECK(st.error.empty(), "no inline error: " + st.error);
        CHECK(st.scope == 0, "Match scope first");
        CHECK(!st.rows.empty(), "slider rows drawn");
        CHECK(st.chip == "Model view: Turbo's estimate, not game output", "the honesty chip: " + st.chip);
        CHECK(st.receive_title == "What the game will receive", "the receive panel");
        CHECK(st.receive.empty(), "nothing switched on: nothing to receive");
        CHECK(st.preview_strip.find("preview-only") != std::string::npos, "preview-only strip: " + st.preview_strip);
        CHECK(st.pitch_strip.find("preview-only") != std::string::npos, "the pitch carries the model's own strip: " + st.pitch_strip);
        // every drawn row: the registry status, the two badges (how written / what was seen), the read-back, dimming of the preview-only rows
        bool saw_live = false, saw_preview = false;
        for (const auto& r : st.rows) {
            const SliderDef* d = find_slider(r.key);
            CHECK(d != nullptr, "row of a known slider: " + r.key);
            if (!d) continue;
            const TacticStatus ts = tactic_status(*d);
            CHECK(r.status == status_name(d->status), "status = registry: " + r.key);
            CHECK(r.written == written_badge(ts.written) && r.seen == seen_badge(ts) && r.tag == status_tag(ts) && r.label == ts.label, "badges follow core/tactics_status.h: " + r.key);
            CHECK(r.written != "Live" && r.seen != "Live", "no 'Live' badge: " + r.key);
            CHECK(!r.enabled, "default is the game decides: " + r.key);
            CHECK(r.was.empty(), "nothing to say about a slider nobody touched: " + r.key);
            CHECK(r.channel_off.empty(), "no kill switch: nothing greyed by one: " + r.key);
            if (preview_only(*d)) {
                saw_preview = true;
                CHECK(r.dimmed && r.tag == "not applied to game", "preview-only row dimmed and tagged: " + r.key);
                CHECK(r.readback == "no read-back: nothing is written for this setting", "a preview row says it has no read-back: " + r.readback);
            }
            if (d->status == SliderStatus::Live) {
                saw_live = true;
                CHECK(r.written == "game variable" && r.readback == "game variable now: not set, the game decides", r.key + ": game variable, read-back: " + r.readback);
            }
            if (d->status == SliderStatus::DB) CHECK(r.written == "database row" && r.seen == "untested" && r.untested, "DB rows are untested database rows: " + r.key);
        }
        CHECK(saw_live && saw_preview, "both a game-variable and a preview-only row in the Match scope");
        CHECK(row_of("match.injury_frequency_user") && row_of("match.injury_frequency_user")->seen == "seen in a played match 2026-10-07", "injury frequency: seen in a played match");
        CHECK(row_of("match.difficulty") && row_of("match.difficulty")->seen == "no effect seen 2026-10-07" && row_of("match.difficulty")->untested, "difficulty: no effect seen, counts as untested");
        CHECK(row_of("match.time_of_day") && row_of("match.time_of_day")->seen == "no effect seen 2026-10-07", "time of day: no effect seen");
        CHECK(row_of("match.never_injure") && row_of("match.never_injure")->seen == "untested", "NEVER_INJURE: untested");
        CHECK(row_of("match.foul_strictness") && row_of("match.foul_strictness")->readback.find("no read-back:") == 0, "referee read-back in a save without the table: " + row_of("match.foul_strictness")->readback);
        // move a slider by typing: injury frequency (your team)
        const ItemRec* typed = ui.find("##typed", "##tacsliders", "match.injury_frequency_user");
        if (!typed) ui.dump("typed box of injury frequency");
        CHECK(typed != nullptr, "typed value box");
        CHECK(ui.type_into(typed, "25"), "type 25");
        ui.frames(2);
        bool found = false;
        for (const auto& r : tactics_tab_state().rows)
            if (r.key == "match.injury_frequency_user") found = r.enabled && r.value == 25 && r.was == "was: game decides";
        CHECK(found, "the slider is on, holds 25 and says what it was");
        CHECK(tactics_tab_state().changed == 1, fmt("1 changed, got %d", tactics_tab_state().changed));
        bool listed = false;
        for (const auto& l : tactics_tab_state().receive)
            listed = listed || l == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER = 25  [game variable]";
        CHECK(listed, "the write is listed in What the game will receive");
        CHECK(tactics_tab_state().receive.size() == 1, "only the game-variable write is listed");
        // a preview-only value (RE row) can be moved but never reaches the receive list
        const ItemRec* re_typed = ui.find("##typed", "##tacsliders", "match.pass_error");
        if (re_typed) {
            CHECK(ui.type_into(re_typed, "40"), "type into a preview-only row");
            ui.frames(2);
            CHECK(tactics_tab_state().receive.size() == 1, "an RE slider is never in the receive list");
        }
        CHECK(fake->sets.empty(), "nothing is written before Apply");
        // dry run: shows, writes nothing
        CHECK(ui.click("Dry run##tacdry"), "dry run on");
        CHECK(ui.click("Apply##tac"), "Apply (dry run)");
        ui.frames(2);
        CHECK(fake->sets.empty(), "dry run sends nothing");
        CHECK(ui.toast_contains("dry run"), "dry-run toast");
        CHECK(ui.click("Dry run##tacdry"), "dry run off");
        CHECK(ui.click("Apply##tac"), "Apply");
        ui.frames(2);
        CHECK(!tactics_tab_state().confirm_open, "a slider seen in a played match needs no confirmation");
        CHECK(fake->sets.size() == 1 && fake->sets[0].first == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER" && fake->sets[0].second == 25,
              "the game variable is set to 25");
        CHECK(ui.toast_contains("Tactics: 1 write(s) sent to the game"), "toast after Apply");
        CHECK(tactics_tab_state().changed == 0, "nothing changed after Apply");
        // Apply again: every switched-on value is sent again (the writes are idempotent; the row's read-back says what the game holds)
        CHECK(ui.click("Apply##tac"), "Apply again, nothing changed");
        ui.frames(2);
        CHECK(fake->sets.size() == 2 && fake->sets[1].second == 25, fmt("the same value is sent again (%zu sets)", fake->sets.size()));
        CHECK(ui.type_into(ui.find("##typed", "##tacsliders", "match.injury_frequency_user"), "30"), "change it to 30");
        ui.frames(2);
        CHECK(ui.click("Apply##tac"), "Apply the change");
        ui.frames(2);
        CHECK(fake->sets.size() == 3 && fake->sets[2].second == 30, "a changed value is sent");
        // the overrides banner and the read-back follow the host's active variables
        msetup::VarResult r;
        r.ok = true;
        r.name = "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER";
        r.value = 30;
        fake->results.push_back(r);
        ui.frames(3);
        CHECK(tactics_tab_state().banner.find("Game overrides are active (1)") != std::string::npos, "overrides banner: " + tactics_tab_state().banner);
        CHECK(row_of("match.injury_frequency_user") && row_of("match.injury_frequency_user")->readback == "game variable now: 30 (set by Turbo, read back after the write)",
              "the row reads the variable back: " + (row_of("match.injury_frequency_user") ? row_of("match.injury_frequency_user")->readback : std::string("no row")));
        // Reset to default: the override is cleared at once and the row is back to "the game decides"
        fake->clears.clear();
        CHECK(ui.click(reset_button(ui, "match.injury_frequency_user")), "Reset on the injury frequency row");
        ui.frames(2);
        CHECK(fake->clears.size() == 1 && fake->clears[0] == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", fmt("the variable was cleared (%zu clears)", fake->clears.size()));
        CHECK(row_of("match.injury_frequency_user") && !row_of("match.injury_frequency_user")->enabled, "the row is off again");
        CHECK(ui.toast_contains("override cleared"), "the toast says the override is gone");
        CHECK(tactics_tab_state().changed == 0, "nothing left to apply");
        CHECK(ui.click("Apply##tac"), "Apply after the reset");
        ui.frames(2);
        CHECK(fake->sets.size() == 3, "nothing is sent for a reset row");
        // a cleared variable is sent again when it is switched on with a value
        CHECK(ui.type_into(ui.find("##typed", "##tacsliders", "match.injury_frequency_user"), "30"), "30 again");
        ui.frames(2);
        CHECK(ui.click("Apply##tac"), "Apply");
        ui.frames(2);
        CHECK(fake->sets.size() == 4 && fake->sets[3].second == 30, "sent again after a reset, even with the old value");
        // leaving: the variables this panel set are cleared
        fake->clears.clear();
        tactics_clear_on_exit(app);
        CHECK(fake->clears.size() == 1 && fake->clears[0] == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER", "auto-clear on exit");
        app.match_setup = nullptr;
    });

    // Tactics channel A (core/gameplay_sliders.h) inside Phase 0's two badges: a per-side gameplay slider variable is "game variable" +
    // "untested", off by default, Apply asks first, tactics_gamevar_off.txt greys and blocks it, the host's call_gamevar_off.txt refuses it,
    // and the row's Reset clears the override at once (channel A's old "o" button only put the row back to "the game decides")
    run_case("UI: Teams > Tactics: a channel A per-side row - game variable + untested, off by default, asks first, both switches, Reset clears it", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        go_scope(ui, "Match (both teams)");
        const char* key = "match.sprint_speed_cpu";
        const std::string var = "GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU";
        auto sent = [&]() {
            for (const auto& kv : fake->sets)
                if (kv.first == var) return true;
            return false;
        };
        const TacticsTabState::Row* r = row_of(key);
        CHECK(r != nullptr, "the Match scope lists the channel A row");
        if (r) {
            CHECK(r->written == "game variable" && r->seen == "untested" && r->untested && !r->legacy, "badges: " + r->written + " / " + r->seen);
            CHECK(r->tag.find("game variable, untested") == 0 && r->tag.find("variable store") != std::string::npos, "tag: " + r->tag);
            CHECK(!r->enabled && r->channel_off.empty() && !r->dimmed, "off by default, not greyed");
            CHECK(r->readback == "game variable now: not set, the game decides", "read-back: " + r->readback);
        }
        size_t n = 0;
        for (const auto& row : tactics_tab_state().rows) {
            const SliderDef* d = find_slider(row.key);
            if (!d || !d->untested) continue;
            ++n;
            CHECK(row.written == "game variable" && row.seen == "untested" && row.untested && !row.enabled, "channel A row " + row.key + ": " + row.written + " / " + row.seen);
        }
        CHECK(n == 66, fmt("the 66 channel A rows are drawn with the two badges: %zu", n));
        CHECK(type_slider(ui, key, "0"), "CPU sprint speed 0");
        ui.frames(2);
        CHECK(row_of(key) && row_of(key)->enabled && row_of(key)->value == 0, "switched on at 0");
        CHECK(has(tactics_tab_state().receive, var + " = 0  [game variable]"), "listed in What the game will receive");
        // Phase 0's game-variable channel switch greys the row and blocks its write
        fs::create_directories(le / "turbo_output");
        {
            std::ofstream f((le / "turbo_output" / "tactics_gamevar_off.txt").string(), std::ios::binary);
            f << "off";
        }
        ui.frames(130);  // the panel looks at the switches every 2 s
        CHECK(row_of(key) && row_of(key)->channel_off == "tactics_gamevar_off.txt" && row_of(key)->dimmed, "greyed by tactics_gamevar_off.txt");
        app.toasts.clear();
        CHECK(apply_confirmed(ui), "Apply (confirmed: untested)");
        ui.frames(2);
        CHECK(!sent() && ui.toast_contains("blocked by a kill switch") && ui.toast_contains("tactics_gamevar_off.txt"), "blocked and named");
        fs::remove(le / "turbo_output" / "tactics_gamevar_off.txt");
        ui.frames(130);
        CHECK(row_of(key) && row_of(key)->channel_off.empty(), "the switch is gone");
        // channel A's own switch: the host refuses every game variable while call_gamevar_off.txt exists
        fake->vars_off = true;
        app.toasts.clear();
        CHECK(apply_confirmed(ui), "Apply with the host's switch");
        ui.frames(2);
        CHECK(!sent() && ui.toast_contains("refused") && ui.toast_contains("call_gamevar_off.txt"), "refused by call_gamevar_off.txt");
        fake->vars_off = false;
        // Apply asks first (untested), then sends the value
        CHECK(ui.click("Apply##tac"), "Apply");
        ui.frames(2);
        CHECK(tactics_tab_state().confirm_open && has_part(tactics_tab_state().confirm_lines, "(" + var + " = 0): untested"), "the confirmation names the channel A write");
        CHECK(!sent(), "nothing is sent while it asks");
        CHECK(ui.click("Apply anyway##tacconfirm", "Apply untested settings?"), "Apply anyway");
        ui.frames(2);
        CHECK(sent(), "the CPU sprint speed variable is set");
        bool zero = false;
        for (const auto& kv : fake->sets) zero = zero || (kv.first == var && kv.second == 0);
        CHECK(zero, "at 0 (off is never sent as 50)");
        // Reset: back to "the game decides" and the override cleared at once
        fake->clears.clear();
        app.toasts.clear();
        CHECK(ui.click(reset_button(ui, key)), "Reset on the channel A row");
        ui.frames(2);
        CHECK(fake->clears.size() == 1 && fake->clears[0] == var, fmt("the override was cleared (%zu clears)", fake->clears.size()));
        CHECK(row_of(key) && !row_of(key)->enabled && ui.toast_contains("override cleared"), "the row is off again");
        // the card list back at the top for the next case (it finds the injury rows without scrolling)
        if (ImGuiWindow* cw = cards_window()) ImGui::SetScrollY(cw, 0.0f);
        ui.frames(2);
        app.match_setup = nullptr;
    });

    run_case("UI: Teams > Tactics: tactics_off.txt blocks every write and greys the effect layers", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        app.request_tab = 1;
        ui.frames(2);
        CHECK(ui.click("7", "##tlist"), "Everton row");
        CHECK(ui.click("Tactics", "##tedit"), "Tactics tab");
        ui.frames(2);
        const ItemRec* typed = ui.find("##typed", "##tacsliders", "match.injury_severity_user");
        CHECK(typed != nullptr, "typed box");
        CHECK(ui.type_into(typed, "60"), "type 60");
        fs::create_directories(le / "turbo_output");
        {
            std::ofstream f((le / "turbo_output" / "tactics_off.txt").string(), std::ios::binary);
            f << "off";
        }
        CHECK(ui.click("Apply##tac"), "Apply with the switch present");
        ui.frames(2);
        CHECK(fake->sets.empty(), "no write when tactics_off.txt exists");
        bool err_toast = false;
        for (const auto& tt : app.toasts) err_toast = err_toast || (tt.error && tt.text.find("tactics_off.txt") != std::string::npos);
        CHECK(err_toast, "an error toast names the switch");
        ui.frames(130);  // the panel looks at the switch every 2 s
        CHECK(tactics_tab_state().off && !tactics_tab_state().off_line.empty(), "the panel shows the switch");
        // the effect layers turn grey: a derived slider on the My team scope
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        const ItemRec* depth = ui.find("##typed", "##tacsliders", "team.line_depth");
        if (depth) {
            CHECK(ui.type_into(depth, "62"), "type the defensive line");
            ui.frames(2);
        }
        fs::remove(le / "turbo_output" / "tactics_off.txt");
        ui.frames(130);
        CHECK(!tactics_tab_state().off, "the switch is gone: the panel writes again");
        CHECK(apply_confirmed(ui), "Apply without the switch (the defensive line is untested: confirmed)");
        ui.frames(2);
        bool severity = false;  // the earlier case's frequency (30) is still on and is sent again: the writes are idempotent
        for (const auto& kv : fake->sets) severity = severity || (kv.first == "GAMEPLAY_CUSTOMIZATION/INJURY_SEVERITY_USER" && kv.second == 60);
        CHECK(severity && !fake->sets.empty(), "the held write goes through once the switch is removed");
        app.match_setup = nullptr;
    });

    run_case("UI: Teams > Tactics: one kill switch per channel - tactics_db_off.txt greys the database rows, names itself, blocks only them", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        select_team(app, ui, 7);  // Everton
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        fs::create_directories(le / "turbo_output");
        {
            std::ofstream f((le / "turbo_output" / "tactics_db_off.txt").string(), std::ios::binary);
            f << "off";
        }
        ui.frames(130);  // the panel looks at the switches every 2 s
        const TacticsTabState& st = tactics_tab_state();
        CHECK(!st.off, "the master switch is not on");
        CHECK(st.switches == std::vector<std::string>({"tactics_db_off.txt"}), "the present switch is listed");
        CHECK(st.off_line.find("tactics_db_off.txt") != std::string::npos && st.off_line.find("career database rows") != std::string::npos, "the line names the file and the channel: " + st.off_line);
        const TacticsTabState::Row* depth = row_of("team.line_depth");
        CHECK(depth && depth->channel_off == "tactics_db_off.txt" && depth->dimmed, "the teams row is greyed and names the file");
        CHECK(ui.click("Match (both teams)##scope"), "Match scope");
        ui.frames(2);
        CHECK(row_of("match.injury_frequency_user") && row_of("match.injury_frequency_user")->channel_off.empty() && !row_of("match.injury_frequency_user")->dimmed,
              "a game-variable row is not greyed by the database switch");
        CHECK(row_of("match.foul_strictness") && row_of("match.foul_strictness")->channel_off == "tactics_db_off.txt", "the referee row (a database row) is greyed");
        // Apply with a teams write on: the database write is blocked and named, a game variable still goes
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        CHECK(type_slider(ui, "team.line_depth", "66"), "defensive line 66");
        const Table* teams = app.db.table("teams");
        const int64_t before = app.db.get_int(*teams, app.model.team(7)->rec, "defensivedepth");
        CHECK(apply_confirmed(ui), "Apply (confirmed: the teams field is untested)");
        ui.frames(2);
        CHECK(app.db.get_int(*teams, app.model.team(7)->rec, "defensivedepth") == before, "the teams row was not written");
        bool err_toast = false;
        for (const auto& tt : app.toasts) err_toast = err_toast || (tt.error && tt.text.find("blocked by a kill switch") != std::string::npos && tt.text.find("tactics_db_off.txt") != std::string::npos);
        CHECK(err_toast, "the toast says what was blocked and by which file");
        // Reset is blocked too (nothing restored, nothing written)
        CHECK(ui.click(reset_button(ui, "team.line_depth")), "Reset on the greyed row");
        ui.frames(2);
        CHECK(ui.toast_contains("nothing restored") && ui.toast_contains("tactics_db_off.txt"), "reset names the switch");
        fs::remove(le / "turbo_output" / "tactics_db_off.txt");
        ui.frames(130);
        CHECK(tactics_tab_state().switches.empty() && tactics_tab_state().off_line.empty(), "the switch is gone");
        CHECK(row_of("team.line_depth") && row_of("team.line_depth")->channel_off.empty(), "the row is not greyed any more");
        // Reset without the switch: Everton's row goes back to the value kept before Turbo's first write (the earlier case wrote 62); the kept
        // row names the career's club (Arsenal, the user's club in this world)
        TacticSnapshots snaps;
        std::string err;
        CHECK(load_tactic_snapshots(tactics_snapshot_path(le), snaps, &err), "the snapshot file loads: " + err);
        const DbSnapshot* kept = snaps.find("teams.defensivedepth", snapshot_row(app.bridge.state().user_team, "team:7"));
        CHECK(kept != nullptr && kept->has_written && kept->written == 62, "Everton's row is kept under the career's club, with Turbo's 62");
        CHECK(ui.click(reset_button(ui, "team.line_depth")), "Reset");
        ui.frames(3);
        if (kept)
            CHECK(app.db.get_int(*teams, app.model.team(7)->rec, "defensivedepth") == kept->value && ui.toast_contains("1 row(s) restored"),
                  fmt("Everton's row is back to %lld", (long long)kept->value));
        CHECK(ui.click("Match (both teams)##scope"), "back to the Match scope");
        app.match_setup = nullptr;
    });

    run_case("UI: Teams > Tactics: the pitch draws the core model (105:68, capped, labels without unit strings), the saved formation, the layers", [&] {
        select_team(app, ui, 1);  // Arsenal: formation 10, a saved 4-3-3
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        const TacticsTabState& st0 = tactics_tab_state();
        CHECK(st0.formation_saved && st0.formation_name == "4-3-3", "Arsenal: the saved 4-3-3 is drawn: " + st0.formation_name);
        CHECK(st0.formation_source.find("saved formation 10") != std::string::npos, "the source is named: " + st0.formation_source);
        CHECK(st0.formation_note.find("as of your last save") != std::string::npos && st0.formation_note.find("nothing on this pitch is written") != std::string::npos,
              "the pitch says it is your saved tactic as of your last save and that nothing is written: " + st0.formation_note);
        const PitchViewResult& pv0 = pitch_view_last();
        CHECK(pv0.exact_labels == std::vector<std::string>({"GK", "LB", "CB", "CB", "RB", "CDM", "CM", "CM", "LW", "ST", "RW"}),
              "the dots carry the saved formation's position names");
        CHECK(!has_part(pv0.labels, "Front line") && !has_part(pv0.labels, "Press line") && !has_part(pv0.labels, "Run arrows"),
              "a dashed layer is only drawn for a slider that is switched on");
        const int slider_free = pv0.primitives;
        show_legacy(ui, true);  // the width sliders are legacy fields, hidden by default
        for (const char* key : {"team.line_depth", "team.engagement_height", "team.width_def"}) CHECK(type_slider(ui, key, "70"), std::string("type into ") + key);
        ui.frames(3);
        const PitchViewResult& pv = pitch_view_last();
        CHECK(pv.primitives > slider_free && pv.primitives <= kPitchPrimitiveCap, fmt("primitives within the cap: %d", pv.primitives));
        CHECK(!pv.capped, "the cap was not hit");
        CHECK(std::fabs(pv.height / pv.width - 68.0f / 105.0f) < 0.001f, "105:68 aspect");
        CHECK(has(pv.labels, "Defensive line 70/100 (schematic)") && has(pv.labels, "Press line 70/100 (schematic)"), "the model's line labels are drawn");
        CHECK(has_part(pv.labels, "Width"), "the width band is drawn once a width slider is on");
        for (const std::string& l : pv.labels) {
            CHECK(!pitch_label_has_unit(l), "no unit string on a derived or modelled label: " + l);
            CHECK(l.find("(schematic)") != std::string::npos || l.find("(arbitrary)") != std::string::npos, "label wording: " + l);
        }
        CHECK(!has_part(pv.labels, "Front line"), "a line whose slider is off is not drawn");
        CHECK(pv.exact_labels.size() == 11, fmt("eleven formation dots with their position names: %zu", pv.exact_labels.size()));
        // the model is cached: frames with nothing changed build it once
        const size_t builds = tactics_tab_state().preview_builds;
        ui.frames(10);
        CHECK(tactics_tab_state().preview_builds == builds, "an unchanged frame does not rebuild the preview");
        // the modelled layers are off by default; each adds primitives, still under the cap and without units
        const int before = pv.primitives;
        CHECK(ui.click("Modelled layer (hatched, off by default)##tach"), "Modelled layer on");
        ui.frames(3);
        const PitchViewResult& pv2 = pitch_view_last();
        CHECK(pv2.primitives > before && pv2.primitives <= kPitchPrimitiveCap && pv2.heat_stride >= 1, fmt("the hatched grid adds primitives: %d -> %d", before, pv2.primitives));
        CHECK(has(pv2.labels, "Relative intensity (arbitrary)"), "the grid is labelled");
        for (const std::string& l : pv2.labels) CHECK(!pitch_label_has_unit(l), "no unit on the modelled label: " + l);
        CHECK(ui.click("Roaming range (stippled, off by default)##tacr"), "roaming rings on");
        CHECK(ui.click("Exposure hint (hatched, off by default)##tace"), "exposure hint on");
        CHECK(type_slider(ui, "team.forward_runs", "60"), "forward runs on (run arrows)");
        CHECK(type_slider(ui, "team.line_length", "55"), "compactness on (front line)");
        CHECK(type_slider(ui, "team.width_att", "65"), "attacking width on");
        ui.frames(3);
        const PitchViewResult& pv3 = pitch_view_last();
        CHECK(pv3.primitives <= kPitchPrimitiveCap && !pv3.capped, fmt("every layer on stays under the cap: %d, stride %d", pv3.primitives, pv3.heat_stride));
        CHECK(pv3.heat_stride >= 1, "the grid is still there (coarser when the cap needs it)");
        CHECK(has(pv3.labels, "Roaming range (arbitrary)") && has(pv3.labels, "Relative exposure (arbitrary)") && has_part(pv3.labels, "Run arrows"),
              "ring, exposure and arrow labels");
        for (const std::string& l : pv3.labels) CHECK(!pitch_label_has_unit(l), "no unit: " + l);
        CHECK(ui.click("Modelled layer (hatched, off by default)##tach"), "Modelled layer off again");
        CHECK(ui.click("Roaming range (stippled, off by default)##tacr"), "rings off");
        CHECK(ui.click("Exposure hint (hatched, off by default)##tace"), "exposure off");
        ui.frames(2);
        CHECK(pitch_view_last().heat_stride == 0 && !has(pitch_view_last().labels, "Relative intensity (arbitrary)"), "off again: no grid");
        // phase: without the ball draws no run arrows, with the ball no lines
        CHECK(ui.click("Without ball##tacph1"), "without the ball");
        ui.frames(2);
        CHECK(!has_part(pitch_view_last().labels, "Run arrows") && has(pitch_view_last().labels, "Defensive line 70/100 (schematic)"), "without the ball: lines, no arrows");
        CHECK(ui.click("With ball##tacph0"), "with the ball");
        ui.frames(2);
        CHECK(has_part(pitch_view_last().labels, "Run arrows") && !has_part(pitch_view_last().labels, "Defensive line"), "with the ball: arrows, no lines");
        CHECK(ui.click("Overall##tacph2"), "overall");
        ui.frames(2);
        // clicking a dot selects its slot: the position sliders then count on the pitch's strip
        const ItemRec* pitch = ui.find("##tacpitch", "##taccentre");
        CHECK(pitch != nullptr, "the pitch region");
        if (pitch) {
            const std::string strip_before = tactics_tab_state().pitch_strip;
            const float w = pitch->rect.GetWidth(), h = pitch->rect.GetHeight();
            click_at(ui, pitch->rect.Min.x + 0.85f * w, pitch->rect.Min.y + 0.5f * h);  // the striker: formation slot 9 at (x 0.5, y 0.85)
            CHECK(tactics_tab_state().selected_slot == 9, fmt("the striker is selected: %d", tactics_tab_state().selected_slot));
            CHECK(tactics_tab_state().pitch_strip != strip_before, "the position sliders now count: " + tactics_tab_state().pitch_strip);
            click_at(ui, pitch->rect.Min.x + 0.85f * w, pitch->rect.Min.y + 0.5f * h);
            CHECK(tactics_tab_state().selected_slot == -1, "a second click deselects");
        }
        // Everton: the save only has a stub row, so Turbo's own 4-4-2 is drawn and says so
        select_team(app, ui, 7);
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        const TacticsTabState& st1 = tactics_tab_state();
        CHECK(!st1.formation_saved && st1.formation_name == "4-4-2" && st1.formation_source.empty(), "Everton: the built-in 4-4-2");
        CHECK(st1.formation_note.find("Built-in shape") != std::string::npos && st1.formation_note.find("not read from your save") != std::string::npos,
              "the pitch says the shape is Turbo's: " + st1.formation_note);
        CHECK(pitch_view_last().exact_labels.size() == 11, "eleven dots");
        // a team with no formation info at all: a plain reason, no error
        select_team(app, ui, 111592);
        ui.frames(2);
        CHECK(tactics_tab_state().formation_saved && tactics_tab_state().formation_source.find("cm_mentalities") != std::string::npos, "Free Agents: found through the tactic row");
        select_team(app, ui, 1318);
        ui.frames(2);
        CHECK(tactics_tab_state().formation_saved && tactics_tab_state().formation_name == "3-5-2", "England: 3-5-2 with offsets from formationoffsets");
        // a preview of another formation changes the dots, never the save
        CHECK(ui.click("Formation (preview)##tacform"), "formation combo");
        CHECK(ui.click("4-4-2 (built-in shape)##tacfb0"), "pick the built-in 4-4-2");
        ui.frames(2);
        CHECK(tactics_tab_state().formation_name == "4-4-2" && !tactics_tab_state().formation_saved, "4-4-2 previewed");
        CHECK(tactics_tab_state().formation_note.find("nothing is written") != std::string::npos, "the preview says nothing is written: " + tactics_tab_state().formation_note);
        select_team(app, ui, 1);  // another team: back to its own formation
        ui.frames(2);
        CHECK(tactics_tab_state().formation_name == "4-3-3" && tactics_tab_state().formation_saved, "a new team shows its own formation again");
        // a Position/Role DB write: tags only (no player picked here)
        CHECK(ui.click("Position/Role##scope"), "Position scope");
        ui.frames(2);
        app.sel_player = 2001;
        ui.frames(2);
        for (const auto& r : tactics_tab_state().rows)
            if (r.status == "DB") CHECK(r.written == "database row" && r.seen == "untested" && r.tag.find("database row, untested") == 0, "DB badges on " + r.key + ": " + r.tag);
        CHECK(row_of("pos.role1") && row_of("pos.role1")->label == "Role familiarity 1 (Role+ / Role++)", "the role editor is named as the player's familiarity");
        CHECK(row_of("pos.role1") && row_of("pos.role1")->readback.find("no read-back:") == 0, "players.role1 is not in the simulated save: an honest 'no read-back': " + row_of("pos.role1")->readback);
        CHECK(ui.click("Match (both teams)##scope"), "back to the Match scope");
    });
}

// ---------------------------------------------------------------------------------------------------------------- mentality rows
static void cases_mentality(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Teams > Tactics: the team's mentality rows are found by teamid and written (the active tactic), a team without a row is reported", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        select_team(app, ui, 1);  // Arsenal: two cm_mentalities rows (the second is active), one mentalities row
        const int64_t club = app.bridge.state().user_team;  // the snapshot rows name the career's club
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        // the legacy fields are hidden until asked for
        show_legacy(ui, false);
        CHECK(tactics_tab_state().legacy_hidden >= 8 && !tactics_tab_state().show_legacy && row_of("team.width_def") == nullptr, fmt("legacy fields hidden by default: %zu", tactics_tab_state().legacy_hidden));
        show_legacy(ui, true);
        CHECK(tactics_tab_state().legacy_hidden == 0 && row_of("team.width_def") && row_of("team.width_def")->legacy && row_of("team.width_def")->seen == "legacy, likely ignored",
              "shown on request, badged legacy");
        CHECK(type_slider(ui, "team.line_depth", "62"), "defensive line 62");
        CHECK(type_slider(ui, "team.width_def", "61"), "defensive width 61");
        CHECK(type_slider(ui, "team.players_in_box_cross", "5"), "players in the box 5");
        CHECK(type_slider(ui, "team.press_intensity", "80"), "an RE slider too: it must never be written");
        ui.frames(2);
        // the receive panel lists the teams row and the legacy table; the saved tactic row (cm_mentalities) is no write target at all
        bool saw_cm = false, saw_teams = false, saw_mentalities = false, saw_re = false;
        for (const auto& l : tactics_tab_state().receive) {
            saw_cm = saw_cm || l.find("cm_mentalities") != std::string::npos;
            saw_teams = saw_teams || l == "teams.defensivedepth = 62  [database row]";
            saw_mentalities = saw_mentalities || l == "mentalities.defensivewidth = 61  [database row]";
            saw_re = saw_re || l.find("press") != std::string::npos;
        }
        CHECK(!saw_cm && saw_teams && saw_mentalities && !saw_re, "the receive list names the live tables, never the dead saved-tactic row and no RE slider");
        CHECK(!has_part(tactics_tab_state().not_written, "cm_mentalities"), "the saved tactic row is not even listed as not written: no slider names it");
        // the read-back shows the rows as they are now
        CHECK(row_of("team.line_depth") && row_of("team.line_depth")->readback.find("teams.defensivedepth now: 50") == 0, "teams read-back: " + row_of("team.line_depth")->readback);
        CHECK(row_of("team.width_def") && row_of("team.width_def")->readback == "mentalities.defensivewidth now: 50", "legacy table read-back: " + row_of("team.width_def")->readback);
        const Table* cm = app.db.table("cm_mentalities");
        const Table* m = app.db.table("mentalities");
        const Table* teams = app.db.table("teams");
        const uint64_t r1 = app.db.find(*cm, "artificialkey", 1), r2 = app.db.find(*cm, "artificialkey", 2), r3 = app.db.find(*cm, "artificialkey", 3);
        const uint64_t mr1 = app.db.find(*m, "teamid", 1), mr7 = app.db.find(*m, "teamid", 7);
        // what the other teams hold now (earlier cases wrote to Everton): Apply must leave all of it as it is
        const int64_t keep_r1 = app.db.get_int(*cm, r1, "defensivedepth"), keep_r2 = app.db.get_int(*cm, r2, "defensivedepth"), keep_r3 = app.db.get_int(*cm, r3, "defensivedepth");
        const int64_t keep_ev = app.db.get_int(*teams, app.model.team(7)->rec, "defensivedepth"), keep_mr7 = app.db.get_int(*m, mr7, "defensivewidth");
        const int64_t keep_ars = app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth"), keep_mr1 = app.db.get_int(*m, mr1, "defensivewidth");
        const int64_t keep_box1 = app.db.get_int(*m, mr1, "playersinboxcross");
        // the attacking width (a legacy field) is still switched on at 65 from the pitch-layers case: this Apply writes it too
        const int64_t keep_ow1 = app.db.get_int(*m, mr1, "offensivewidth");
        // Apply asks first: these settings are untested or legacy
        CHECK(ui.click("Apply##tac"), "Apply");
        ui.frames(2);
        const TacticsTabState& st = tactics_tab_state();
        CHECK(st.confirm_open && !st.confirm_lines.empty(), fmt("the confirmation is open with %zu line(s)", st.confirm_lines.size()));
        CHECK(has_part(st.confirm_lines, "Defensive line depth (teams.defensivedepth = 62): untested") && has_part(st.confirm_lines, "Defensive width (mentalities.defensivewidth = 61): legacy, likely ignored"),
              "it names the untested and the legacy writes");
        CHECK(app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth") == keep_ars && app.db.get_int(*m, mr1, "defensivewidth") == keep_mr1, "nothing is written while it asks");
        CHECK(ui.click("Cancel##tacconfirmno", "Apply untested settings?"), "Cancel");
        ui.frames(2);
        CHECK(!tactics_tab_state().confirm_open && app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth") == keep_ars, "cancelled: nothing written");
        CHECK(apply_confirmed(ui), "Apply, then Apply anyway");
        ui.frames(3);
        CHECK(ui.toast_contains("write(s) sent to the game"), "toast");
        CHECK(!ui.toast_contains("refused"), "nothing was refused");
        CHECK(app.db.get_int(*cm, r2, "defensivedepth") == keep_r2 && keep_r2 == 55, "the saved tactic row (cm_mentalities, dead) is NOT written");
        CHECK(app.db.get_int(*cm, r1, "defensivedepth") == keep_r1 && keep_r1 == 40, "the other tactic of the team is untouched");
        CHECK(app.db.get_int(*cm, r3, "defensivedepth") == keep_r3, "another team's row is untouched");
        CHECK(app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth") == 62, "teams.defensivedepth of Arsenal");
        CHECK(app.db.get_int(*teams, app.model.team(7)->rec, "defensivedepth") == keep_ev, "Everton's team row is untouched");
        CHECK(app.db.get_int(*m, mr1, "defensivewidth") == 61 && app.db.get_int(*m, mr1, "playersinboxcross") == 5, "mentalities row of Arsenal: width and box");
        CHECK(app.db.get_int(*m, mr1, "offensivewidth") == 65, fmt("and the attacking width still on from the pitch-layers case (65): %lld",
                                                                   (long long)app.db.get_int(*m, mr1, "offensivewidth")));
        CHECK(app.db.get_int(*m, mr7, "defensivewidth") == keep_mr7, "Everton's mentalities row is untouched");
        CHECK(has_part(st.writes_done, "mentalities.defensivewidth = 61: row 1 of 1 of Arsenal (the active tactic)"), "the log names the row used: " + (st.writes_done.empty() ? std::string("none") : st.writes_done[0]));
        CHECK(has_part(st.writes_done, "teams.defensivedepth = 62"), "and the teams write");
        CHECK(!has_part(st.writes_done, "cm_mentalities"), "and never the dead row");
        for (const auto& kv : fake->sets) CHECK(gv::known(kv.first) != nullptr && kv.first.find("DEPTH") == std::string::npos, "a DB write never becomes a game variable: " + kv.first);
        // the snapshot store kept the values before Turbo's first write; the read-back shows both
        CHECK(fs::exists(le / "turbo_output" / "tactics_snapshot.json"), "the snapshot file was written");
        {
            TacticSnapshots snaps;
            std::string err;
            CHECK(load_tactic_snapshots(tactics_snapshot_path(le), snaps, &err), "it loads: " + err);
            const DbSnapshot* s = snaps.find("teams.defensivedepth", snapshot_row(club, "team:1"));
            CHECK(s && s->value == keep_ars && s->label == "Arsenal" && s->has_written && s->written == 62,
                  fmt("teams.defensivedepth of Arsenal kept as %lld under the career's club", s ? (long long)s->value : -1LL));
            CHECK(snaps.find("mentalities.defensivewidth", snapshot_row(club, "mentalities:1")) != nullptr, "the legacy row is kept by its mentalityid");
            CHECK(snaps.find("teams.defensivedepth", "team:1") == nullptr, "never without the club");
        }
        CHECK(row_of("team.line_depth") && row_of("team.line_depth")->readback == "teams.defensivedepth now: 62 (before Turbo: " + std::to_string(keep_ars) + ")",
              "read-back after the write: " + row_of("team.line_depth")->readback);
        // Apply again with the same values: written again, and the kept value stays the one from before Turbo's FIRST write
        CHECK(apply_confirmed(ui), "Apply again");
        ui.frames(2);
        CHECK(ui.toast_contains("write(s) sent to the game") && app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth") == 62, "written again");
        {
            TacticSnapshots snaps;
            std::string err;
            CHECK(load_tactic_snapshots(tactics_snapshot_path(le), snaps, &err), "it loads: " + err);
            const DbSnapshot* s = snaps.find("teams.defensivedepth", snapshot_row(club, "team:1"));
            CHECK(s && s->value == keep_ars, "the kept value is still the original, not Turbo's own 62");
        }
        // a team with no row in the mentality tables: its team row is written, the rest is reported (not silently dropped)
        select_team(app, ui, 241);  // Inter: no cm_mentalities and no mentalities row
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        CHECK(type_slider(ui, "team.line_depth", "44"), "defensive line 44");
        CHECK(type_slider(ui, "team.width_def", "33"), "width 33");
        const int64_t keep_inter = app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth");
        CHECK(apply_confirmed(ui), "Apply");
        ui.frames(3);
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == 44, "Inter's team row was written");
        CHECK(ui.toast_contains("not written") && ui.toast_contains("has no row in"), "the toast reports the missing rows");
        CHECK(app.db.get_int(*m, mr1, "defensivewidth") == 61 && app.db.get_int(*cm, r2, "defensivedepth") == keep_r2, "other teams' rows are untouched");
        // Reset to default puts the value before Turbo's first write back and forgets the snapshot
        CHECK(ui.click(reset_button(ui, "team.line_depth")), "Reset on Inter's defensive line");
        ui.frames(3);
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == keep_inter, fmt("Inter's row is back to %lld", (long long)keep_inter));
        CHECK(ui.toast_contains("1 row(s) restored to the value before Turbo's first write"), "the toast says what was restored");
        CHECK(row_of("team.line_depth") && !row_of("team.line_depth")->enabled && row_of("team.line_depth")->readback == "teams.defensivedepth now: " + std::to_string(keep_inter),
              "the row is off and reads the original back: " + row_of("team.line_depth")->readback);
        {
            TacticSnapshots snaps;
            std::string err;
            CHECK(load_tactic_snapshots(tactics_snapshot_path(le), snaps, &err) && !snaps.find("teams.defensivedepth", snapshot_row(club, "team:241")) &&
                      snaps.find("teams.defensivedepth", snapshot_row(club, "team:1")),
                  "Inter's snapshot is gone, Arsenal's stays");
        }
        // switching a slider off and applying restores it too
        CHECK(type_slider(ui, "team.line_depth", "44"), "defensive line 44 again");
        CHECK(apply_confirmed(ui), "Apply");
        ui.frames(2);
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == 44, "written again");
        CHECK(ui.click(slider_item(ui, "##on", "team.line_depth")), "switch the row off");
        ui.frames(2);
        CHECK(apply_confirmed(ui), "Apply with the row off");
        ui.frames(2);
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == keep_inter && ui.toast_contains("1 row(s) restored"), "off + Apply: the original value is back");
        // a row that changed since Turbo wrote it (another save, the game, the user) is left alone by Reset (a new value: typing the value a
        // switched-off row still holds does not switch it on)
        CHECK(type_slider(ui, "team.line_depth", "45"), "defensive line 45");
        CHECK(apply_confirmed(ui), "Apply");
        ui.frames(2);
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == 45, "Turbo wrote 45");
        CHECK(app.db.set_int(*teams, app.model.team(241)->rec, "defensivedepth", 47), "someone else sets the row to 47");
        ++app.gen;
        ui.frames(2);
        CHECK(ui.click(reset_button(ui, "team.line_depth")), "Reset");
        ui.frames(3);
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == 47, "the row keeps 47: Turbo undoes only its own write");
        CHECK(ui.toast_contains("changed since Turbo wrote it") && ui.toast_contains("Turbo wrote 45"), "the toast says why nothing was restored");
        CHECK(app.db.set_int(*teams, app.model.team(241)->rec, "defensivedepth", keep_inter), "put the simulated row back for the next cases");
        ++app.gen;
        // a slot-offset slider cannot be written by this build: it is listed as not written, never sent
        CHECK(ui.click("Position/Role##scope"), "Position scope");
        ui.frames(2);
        CHECK(type_slider(ui, "pos.formation_x", "40"), "slot offset 40");
        ui.frames(2);
        CHECK(has_part(tactics_tab_state().not_written, "formations.offset{n}x"), "the slot offset is listed as not written");
        bool listed = false;
        for (const auto& l : tactics_tab_state().receive) listed = listed || l.find("offset{n}") != std::string::npos;
        CHECK(!listed, "and is not in the list of what the game will receive");
        // tactics_off.txt appearing puts back every database row Turbo wrote in this career, not only the selected club's (Inter is selected,
        // Arsenal's rows go back), and leaves a row that changed since Turbo wrote it (Inter's) as it is (P0-9)
        fs::create_directories(le / "turbo_output");
        {
            std::ofstream f((le / "turbo_output" / "tactics_off.txt").string(), std::ios::binary);
            f << "off";
        }
        ui.frames(130);  // the panel looks at the switches every 2 s
        CHECK(tactics_tab_state().off, "the master switch is on");
        CHECK(app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth") == keep_ars, fmt("Arsenal's teams row is back to %lld", (long long)keep_ars));
        CHECK(app.db.get_int(*m, mr1, "defensivewidth") == keep_mr1 && app.db.get_int(*m, mr1, "playersinboxcross") == keep_box1 &&
                  app.db.get_int(*m, mr1, "offensivewidth") == keep_ow1,
              "Arsenal's legacy row is back (both widths and the box count)");
        CHECK(app.db.get_int(*teams, app.model.team(241)->rec, "defensivedepth") == keep_inter, "Inter's row (changed since Turbo wrote 45) is left as it is");
        CHECK(app.db.get_int(*cm, r2, "defensivedepth") == keep_r2, "the saved tactic row was never touched");
        // 4 = Arsenal's rows Turbo wrote in this case: teams.defensivedepth, mentalities.defensivewidth, mentalities.playersinboxcross and
        // mentalities.offensivewidth (team.width_att = 65, switched on in the pitch-layers case and written by this case's Apply)
        CHECK(ui.toast_contains("4 database row(s) put back to the value before Turbo's first write") && ui.toast_contains("1 row(s) changed since Turbo wrote them left as they are"),
              "the toast says what went back and what did not");
        {
            TacticSnapshots snaps;
            std::string err;
            CHECK(load_tactic_snapshots(tactics_snapshot_path(le), snaps, &err), "it loads: " + err);
            CHECK(!snaps.find("teams.defensivedepth", snapshot_row(club, "team:1")) && !snaps.find("mentalities.defensivewidth", snapshot_row(club, "mentalities:1")) &&
                      !snaps.find("mentalities.playersinboxcross", snapshot_row(club, "mentalities:1")) &&
                      !snaps.find("mentalities.offensivewidth", snapshot_row(club, "mentalities:1")),
                  "the restored rows are forgotten");
            CHECK(snaps.find("teams.defensivedepth", snapshot_row(club, "team:241")) != nullptr, "the row that changed since stays kept");
        }
        // the switch staying there restores nothing more (one restore when the file appears, not one every 2 s)
        app.toasts.clear();
        ui.frames(130);
        CHECK(!ui.toast_contains("put back to the value before"), "no second restore while the file stays");
        fs::remove(le / "turbo_output" / "tactics_off.txt");
        ui.frames(130);
        CHECK(!tactics_tab_state().off, "the switch is gone");
        CHECK(app.db.get_int(*teams, app.model.team(1)->rec, "defensivedepth") == keep_ars, "removing the switch writes nothing back");
        CHECK(ui.click("Match (both teams)##scope"), "back to the Match scope");
        app.match_setup = nullptr;
    });
}

// ---------------------------------------------------------------------------------------------------------------- presets
static void cases_presets(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Teams > Tactics: presets - save, load, compare as 'was X', rename, duplicate, delete with a confirmation", [&] {
        select_team(app, ui, 1);
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        // the built-ins are listed first and cannot be renamed or deleted
        const auto& names0 = tactics_tab_state().preset_names;
        CHECK(!names0.empty(), "built-in team presets are listed");
        const std::vector<const Profile*> team_list = app.tactic_profiles.list(ProfileCategory::Team);
        CHECK(!team_list.empty() && team_list[0]->builtin, "the first preset is a built-in");
        const Profile bi = *team_list[0];
        CHECK(ui.click(bi.name + " (built-in)##" + bi.id, "##tacleft"), "load a built-in");
        CHECK(tactics_tab_state().selected_profile == bi.id, "built-in loaded");
        CHECK(ui.click("Rename##tacren", "##tacleft"), "Rename (disabled for a built-in)");
        ui.frames(2);
        CHECK(ui.find("##tacrenname", "Rename preset") == nullptr, "no rename box opens for a built-in");
        CHECK(ui.click("Delete##tacdel", "##tacleft"), "Delete (disabled for a built-in)");
        ui.frames(2);
        CHECK(ui.find("Delete##tacdelok", "Delete preset") == nullptr, "no delete dialog for a built-in");
        // a built-in that seeds an untested field loads it switched OFF
        CHECK(ui.toast_contains("loaded switched OFF") || !bi.sliders.is_enabled("team.line_depth"), "a built-in's untested settings come in switched off");
        CHECK(!tactics_tab_state().rows.empty(), "rows drawn");
        for (const auto& r : tactics_tab_state().rows)
            if (r.enabled && bi.sliders.is_enabled(r.key)) CHECK(!r.untested && !r.legacy, "a preset never switches on an untested or legacy setting by itself: " + r.key);
        for (const auto& kv : bi.sliders.values)
            if (bi.sliders.is_enabled(kv.first) && find_slider(kv.first) && untested_write(tactic_status(*find_slider(kv.first))))
                CHECK(!tactics_tab_state().rows.empty() && (!row_of(kv.first.c_str()) || !row_of(kv.first.c_str())->enabled), "the built-in's untested setting is off: " + kv.first);
        // a preset of my own: values 62 / 70
        show_legacy(ui, true);  // attacking width is a legacy field
        CHECK(type_slider(ui, "team.line_depth", "62"), "defensive line 62");
        CHECK(type_slider(ui, "team.width_att", "55"), "attacking width 55");
        CHECK(ui.type_into(ui.find("##tacname", "##tacright"), "My press"), "preset name");
        CHECK(ui.click("Save as preset##tac"), "Save as preset");
        ui.frames(2);
        const Profile* mine = profile_named(app, "My press", ProfileCategory::Team);
        CHECK(mine != nullptr, "the preset is in the store");
        if (!mine) return;
        const std::string id = mine->id;
        CHECK(mine->formation == "4-3-3" && mine->sliders.is_enabled("team.line_depth") && mine->sliders.value_or_default("team.line_depth") == 62,
              "it keeps the formation shown and the switched-on sliders");
        CHECK(fs::exists(le / "turbo_output" / "tactic_profiles.json"), "the preset file was written");
        CHECK(tactics_tab_state().selected_profile == id, "the new preset is the loaded one");
        // change a slider after loading it: compare shows the preset's value as 'was'
        CHECK(ui.click("My press##" + id, "##tacleft"), "load it");
        CHECK(row_of("team.line_depth") && !row_of("team.line_depth")->enabled && row_of("team.line_depth")->value == 62, "my preset's untested slider loads with its value, switched off");
        CHECK(ui.toast_contains("loaded switched OFF"), "the toast says so");
        CHECK(row_of("team.width_att") && !row_of("team.width_att")->enabled && row_of("team.width_att")->value == 55, "the legacy width loads with its value, switched off");
        CHECK(ui.click(slider_item(ui, "##on", "team.width_att")), "switch the legacy width on again (the preset's value 55 is kept)");
        ui.frames(2);
        CHECK(row_of("team.width_att") && row_of("team.width_att")->enabled && row_of("team.width_att")->value == 55, "on again with 55");
        CHECK(type_slider(ui, "team.line_depth", "70"), "change the defensive line to 70");
        ui.frames(2);
        CHECK(!tactics_tab_state().compare && row_of("team.line_depth") && row_of("team.line_depth")->was.find("was") == 0, "before compare 'was' is against the last Apply");
        CHECK(ui.click("Compare with the loaded preset##taccmp", "##tacleft"), "compare on");
        ui.frames(2);
        const TacticsTabState& st = tactics_tab_state();
        // the preset's other untested / legacy settings were loaded switched off: each of them is a "now the game decides" line; the rest is the depth
        auto held_lines = [&]() {
            size_t n = 0;
            for (const std::string& l : tactics_tab_state().compare_lines)
                if (l.find(", now the game decides") != std::string::npos) ++n;
            return n;
        };
        const size_t held = held_lines();
        CHECK(st.compare && has(st.compare_lines, "Defensive line depth: was 62, now 70") && st.compare_lines.size() == held + 1,
              fmt("the diff: the depth line plus %zu held-off line(s) (%zu lines)", held, st.compare_lines.size()));
        CHECK(row_of("team.line_depth") && row_of("team.line_depth")->was == "was 62", "the row says 'was 62'");
        CHECK(row_of("team.width_att") && row_of("team.width_att")->was.empty(), "an unchanged slider says nothing");
        CHECK(row_of("team.tempo") && row_of("team.tempo")->was.empty(), "an untouched slider says nothing");
        CHECK(type_slider(ui, "team.width_att", "60"), "attacking width to 60");
        CHECK(type_slider(ui, "team.cross_freq", "40"), "cross frequency on (the preset has it off)");
        ui.frames(2);
        CHECK(tactics_tab_state().compare_lines.size() == held + 3, fmt("three differences plus the held-off lines: %zu", tactics_tab_state().compare_lines.size()));
        CHECK(has(tactics_tab_state().compare_lines, "Cross frequency: was the game decides, now 40"), "a slider the preset leaves to the game");
        CHECK(row_of("team.cross_freq")->was == "was: game decides", "its row says so");
        CHECK(ui.click("Compare with the loaded preset##taccmp", "##tacleft"), "compare off");
        ui.frames(2);
        CHECK(!tactics_tab_state().compare && tactics_tab_state().compare_lines.empty(), "compare off");
        // rename
        CHECK(ui.click("Rename##tacren", "##tacleft"), "Rename");
        const ItemRec* box = ui.find("##tacrenname", "Rename preset");
        CHECK(box != nullptr, "the rename box");
        if (box) CHECK(ui.type_into(box, "Pressing trap"), "new name + Enter");
        ui.frames(2);
        CHECK(profile_named(app, "Pressing trap", ProfileCategory::Team) != nullptr && profile_named(app, "My press", ProfileCategory::Team) == nullptr, "renamed in the store");
        CHECK(has(tactics_tab_state().preset_names, "Pressing trap") && !has(tactics_tab_state().preset_names, "My press"), "the list shows the new name");
        {
            TacticProfileStore on_disk;
            std::string err;
            CHECK(load_tactic_profiles(tactic_profiles_path(le), on_disk, &err) && on_disk.find_by_name(ProfileCategory::Team, "Pressing trap"), "saved to the file: " + err);
        }
        // a clash: renaming to a name another preset has is refused
        CHECK(ui.click("Rename##tacren", "##tacleft"), "Rename again");
        box = ui.find("##tacrenname", "Rename preset");
        if (box) CHECK(ui.type_into(box, bi.name), "a built-in's name");
        ui.frames(2);
        CHECK(ui.toast_contains("Not renamed"), "a taken name is refused with a reason");
        CHECK(ui.click("Cancel##tacrenno", "Rename preset"), "cancel the dialog");
        ui.frames(2);
        // duplicate
        CHECK(ui.click("Duplicate##tacdup", "##tacleft"), "Duplicate");
        ui.frames(2);
        CHECK(profile_named(app, "Pressing trap copy", ProfileCategory::Team) != nullptr, "the copy exists");
        CHECK(tactics_tab_state().selected_profile == profile_named(app, "Pressing trap copy", ProfileCategory::Team)->id, "the copy is the loaded one");
        // delete with a confirmation: Cancel keeps it, Delete removes it
        CHECK(ui.click("Delete##tacdel", "##tacleft"), "Delete");
        CHECK(ui.find("Delete##tacdelok", "Delete preset") != nullptr, "the confirmation asks first");
        CHECK(ui.click("Cancel##tacdelno", "Delete preset"), "Cancel");
        ui.frames(2);
        CHECK(profile_named(app, "Pressing trap copy", ProfileCategory::Team) != nullptr, "cancelled: still there");
        CHECK(ui.click("Delete##tacdel", "##tacleft"), "Delete again");
        CHECK(ui.click("Delete##tacdelok", "Delete preset"), "confirm");
        ui.frames(3);
        CHECK(profile_named(app, "Pressing trap copy", ProfileCategory::Team) == nullptr, "deleted");
        CHECK(profile_named(app, "Pressing trap", ProfileCategory::Team) != nullptr, "the original is untouched");
        CHECK(tactics_tab_state().selected_profile.empty(), "nothing is loaded after a delete");
        {
            TacticProfileStore on_disk;
            std::string err;
            CHECK(load_tactic_profiles(tactic_profiles_path(le), on_disk, &err) && !on_disk.find_by_name(ProfileCategory::Team, "Pressing trap copy"), "deleted from the file too");
        }
        CHECK(ui.click("Match (both teams)##scope"), "back to the Match scope");
    });

    run_case("UI: Teams > Tactics: export and import presets through the in-overlay picker, with a clash plan", [&] {
        select_team(app, ui, 1);
        CHECK(ui.click("My team##scope"), "My team scope");
        ui.frames(2);
        const fs::path dest = g_out / "tactics_io";
        std::error_code ec;
        fs::remove_all(dest, ec);
        fs::create_directories(dest);
        const Profile* mine = profile_named(app, "Pressing trap", ProfileCategory::Team);
        CHECK(mine != nullptr, "the preset from the last case");
        if (!mine) return;
        const std::string id = mine->id;
        CHECK(ui.click("Pressing trap##" + id, "##tacleft"), "load it");
        // export the loaded preset
        CHECK(ui.click("Export...##tacexp", "##tacleft"), "Export...");
        CHECK(ui.click("The loaded preset##tacexp0", "Export presets"), "the loaded preset");
        CHECK(ui.click("Save as...##tacexpsave", "Export presets"), "Save as...");
        CHECK(ui.find("Save##fp", "##tacexppick") != nullptr, "the picker is drawn in the overlay");
        CHECK(ui.type_into(ui.find("##fppath", "##tacexppick"), path_text(dest)), "folder");
        CHECK(ui.type_into(ui.find("File name##fpname", "##tacexppick"), "my_presets.json"), "file name");
        CHECK(ui.click("Save##fp", "##tacexppick"), "Save");
        ui.frames(2);
        const fs::path file = dest / "my_presets.json";
        CHECK(fs::exists(file), "the file was written");
        CHECK(ui.toast_contains("Exported 1 preset(s)"), "toast");
        {
            TacticProfileStore out;
            std::string err;
            std::string text;
            CHECK(read_text_file(file, text, &err) && parse_tactic_profiles_json(text, out, &err), "it is a preset file: " + err);
            CHECK(out.profiles.size() == 1 && out.profiles[0].name == "Pressing trap" && out.profiles[0].sliders.value_or_default("team.line_depth") == 62, "with this preset and its values");
        }
        CHECK(ui.find("Close##tacexpclose") == nullptr, "the export dialog closes once the file is written");
        // import it again: the same preset is already here (a clash); Skip, then Keep both
        auto open_import = [&]() {
            CHECK(ui.click("Import...##tacimp", "##tacleft"), "Import...");
            CHECK(ui.find("##fppath", "##tacimppick") != nullptr, "the open picker");
            CHECK(ui.type_into(ui.find("##fppath", "##tacimppick"), path_text(dest)), "folder");
            CHECK(ui.click("my_presets.json", "##tacimppick"), "pick the file");
            ui.frames(3);
        };
        open_import();
        CHECK(tactics_tab_state().import_open && tactics_tab_state().import_items == 1, "the plan lists the preset");
        CHECK(ui.find("##tacimpact", "Import presets") != nullptr, "a clash offers a choice");
        CHECK(ui.click("##tacimpact", "Import presets"), "the choice");
        CHECK(ui.click("Skip it##2"), "Skip it");
        const size_t before = app.tactic_profiles.profiles.size();
        CHECK(ui.click("Import##tacimpok", "Import presets"), "Import (skip)");
        ui.frames(3);
        CHECK(app.tactic_profiles.profiles.size() == before && ui.toast_contains("1 skipped"), "skipped: nothing added");
        open_import();
        CHECK(ui.click("Import##tacimpok", "Import presets"), "Import (the default keeps both)");
        ui.frames(3);
        CHECK(app.tactic_profiles.profiles.size() == before + 1 && ui.toast_contains("1 renamed"), "kept both: the copy was renamed");
        CHECK(profile_named(app, "Pressing trap (2)", ProfileCategory::Team) != nullptr, "the renamed copy exists");
        // delete both, import again: no clash, added
        for (const char* nm : {"Pressing trap (2)", "Pressing trap"}) {
            const Profile* p = profile_named(app, nm, ProfileCategory::Team);
            CHECK(p != nullptr, std::string("exists: ") + nm);
            if (!p) continue;
            const std::string pid = p->id;
            CHECK(ui.click(std::string(nm) + "##" + pid, "##tacleft"), "load");
            CHECK(ui.click("Delete##tacdel", "##tacleft"), "Delete");
            CHECK(ui.click("Delete##tacdelok", "Delete preset"), "confirm");
            ui.frames(2);
        }
        CHECK(profile_named(app, "Pressing trap", ProfileCategory::Team) == nullptr, "both gone");
        open_import();
        CHECK(tactics_tab_state().import_open && ui.find("##tacimpact", "Import presets") == nullptr, "no clash, no choice");
        CHECK(ui.click("Import##tacimpok", "Import presets"), "Import");
        ui.frames(3);
        CHECK(ui.toast_contains("1 added") && profile_named(app, "Pressing trap", ProfileCategory::Team) != nullptr, "added again");
        // a file that is not a preset file is refused with a reason, never half-imported
        {
            std::ofstream f((dest / "other.json").string());
            f << "{\"hello\": 1}";
        }
        CHECK(ui.click("Import...##tacimp", "##tacleft"), "Import...");
        CHECK(ui.type_into(ui.find("##fppath", "##tacimppick"), path_text(dest)), "folder");
        CHECK(ui.click("other.json", "##tacimppick"), "pick the other file");
        ui.frames(3);
        CHECK(ui.toast_contains("not a Turbo preset file") && !tactics_tab_state().import_open, "refused with a reason");
        // my presets of this kind: the whole category in one file
        CHECK(ui.click("Export...##tacexp", "##tacleft"), "Export...");
        CHECK(ui.click("All my presets of this kind##tacexp1", "Export presets"), "all of this kind");
        CHECK(ui.click("Save as...##tacexpsave", "Export presets"), "Save as...");
        CHECK(ui.type_into(ui.find("##fppath", "##tacexppick"), path_text(dest)), "folder");
        CHECK(ui.type_into(ui.find("File name##fpname", "##tacexppick"), "all_team.json"), "file name");
        CHECK(ui.click("Save##fp", "##tacexppick"), "Save");
        ui.frames(2);
        CHECK(fs::exists(dest / "all_team.json"), "the category file was written");
        CHECK(ui.find("Close##tacexpclose") == nullptr, "closed");
        CHECK(ui.click("Match (both teams)##scope"), "back to the Match scope");
    });
}

// ---------------------------------------------------------------------------------------------------------------- opposition card
static void cases_opposition(App& app, Ui& ui, const fs::path& le) {
    run_case("UI: Teams > Tactics: the opposition card - facts from the save, a seeded profile, explain, nothing written without Apply", [&] {
        auto fake = std::make_shared<FakeMatchSetup>();
        app.match_setup = fake;
        std::error_code ec;
        fs::remove(le / "turbo_output" / "tactics_off.txt", ec);
        ui.frames(130);
        CHECK(app.bridge.state().user_team == 1, "the user's club is Arsenal");
        select_team(app, ui, 7);  // the opponent: Everton
        go_scope(ui, "Opposition");
        const TacticsTabState::OppCard& c = tactics_tab_state().opp;
        CHECK(c.shown && c.active && c.note.empty(), "the card is drawn with a profile: " + c.note);
        // the facts come from the model: the mean of the best 14 players of each squad
        auto band = [&](int64_t team) {
            std::vector<int> v;
            for (const LinkRow& l : app.model.links_of_team(team))
                if (const PlayerRow* p = app.model.player(l.playerid)) v.push_back(p->overall);
            std::sort(v.begin(), v.end(), std::greater<int>());
            if (v.size() > 14) v.resize(14);
            double s = 0;
            for (int x : v) s += x;
            return v.empty() ? 0.0 : s / double(v.size());
        };
        CHECK(std::fabs(c.user_ovr - band(1)) < 1e-9 && std::fabs(c.opp_ovr - band(7)) < 1e-9, fmt("squad ratings %.2f / %.2f", c.user_ovr, c.opp_ovr));
        CHECK(std::fabs(c.ratio - band(7) / band(1)) < 1e-9, fmt("strength ratio %.3f = opponent / you", c.ratio));
        CHECK(c.fixture_line.find("Arsenal") != std::string::npos && c.fixture_line.find("Everton") != std::string::npos && c.fixture_line.find("at home") != std::string::npos,
              "the fixture line: " + c.fixture_line);
        CHECK(c.n_def + c.n_mid + c.n_att == 10 && c.n_def == 4 && c.n_att == 2, fmt("the opponent's formation shape %d-%d-%d (Turbo's 4-4-2)", c.n_def, c.n_mid, c.n_att));
        CHECK(c.facts_line.find("Not read yet") != std::string::npos && c.facts_line.find("pace") != std::string::npos, "it says which facts are not read: " + c.facts_line);
        CHECK(!c.blend.empty() && c.seed_text.find("Seed ") == 0 && c.seed != 0, "style blend and a seed: " + c.seed_text);
        int sum = 0;
        for (const std::string& b : c.blend) sum += std::atoi(b.substr(b.rfind(' ') + 1).c_str());
        CHECK(sum >= 97 && sum <= 103, fmt("the blend sums to about 100%%: %d", sum));
        CHECK(!c.explain.empty() && c.explain.find("Style blend") != std::string::npos, "the explain text: " + c.explain);
        CHECK(c.live.empty() && !c.can_apply, "no built-in rule makes an injury or difficulty offset: nothing to send");
        CHECK(has_part(c.changes, "preview only, not sent to the game") || c.changes.empty(), "changes are labelled preview only");
        for (const std::string& l : c.changes) CHECK(l.find("preview only") != std::string::npos, "every change says so: " + l);
        // determinism: the same fixture and settings give the same result; the home flag does not change the seed
        const std::string seed1 = c.seed_text;
        const std::vector<std::string> changes1 = c.changes, blend1 = c.blend;
        CHECK(ui.click("Opponent plays at home##tacopphome", "##taccentre"), "away");
        ui.frames(3);
        CHECK(tactics_tab_state().opp.seed_text == seed1 && tactics_tab_state().opp.fixture_line.find("away") != std::string::npos, "the seed does not depend on the venue");
        CHECK(ui.click("Opponent plays at home##tacopphome", "##taccentre"), "home again");
        ui.frames(3);
        CHECK(tactics_tab_state().opp.changes == changes1 && tactics_tab_state().opp.blend == blend1 && tactics_tab_state().opp.seed_text == seed1, "the same inputs give the same profile");
        CHECK(ui.click("Re-roll##tacopproll", "##taccentre"), "Re-roll");
        ui.frames(3);
        CHECK(tactics_tab_state().opp.seed_text != seed1, "a re-roll gives another seed: " + tactics_tab_state().opp.seed_text);
        // the ghost pitch: the opponent's shape with the solver's changes, in the same honesty colours
        CHECK(tactics_tab_state().formation_note.find("Ghost view") != std::string::npos, "the pitch says it is a ghost view: " + tactics_tab_state().formation_note);
        CHECK(!pitch_view_last().exact_labels.empty() && pitch_view_last().exact_labels.size() == 11, "the opponent's eleven");
        for (const std::string& l : pitch_view_last().labels) CHECK(!pitch_label_has_unit(l), "no unit: " + l);
        CHECK(fake->sets.empty(), "nothing was written to the game");
        // a user rule that gives injury and difficulty offsets: now there is something Apply could send
        std::string err;
        size_t dropped = 0;
        RuleSet user;
        const char* rule_json =
            "{\"turbo_opp_rules\":1,\"rules\":[{\"id\":\"test_injuries\",\"priority\":10,\"when\":{\"all\":[{\"fact\":\"strength_ratio\",\"op\":\">\",\"value\":0}]},"
            "\"then\":[{\"op\":\"add\",\"target\":\"opp.injury_frequency_offset\",\"value\":8},{\"op\":\"add\",\"target\":\"opp.difficulty_offset\",\"value\":1}]}]}";
        CHECK(parse_opp_rules_json(rule_json, user, &err, &dropped) && user.rules.size() == 1, "the rule file parses: " + err);
        app.opp_user_rules = user;
        ui.frames(3);
        const TacticsTabState::OppCard& c2 = tactics_tab_state().opp;
        CHECK(c2.live.size() == 2 && c2.can_apply, fmt("two offsets to send, Apply is on (%zu)", c2.live.size()));
        CHECK(has(c2.live, "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI = 58  [game variable]  (base 50 the middle: Turbo cannot read the game's own number + offset +8)"),
              "the injury line shows its base and offset: " + (c2.live.empty() ? std::string("none") : c2.live[0]));
        CHECK(has_part(c2.live, "OVERRIDE_MATCH_DIFFICULTY = 4  [game variable]  (base 3"), "the difficulty line (base 3 + 1)");
        CHECK(c2.explain.find("test_injuries") != std::string::npos, "explain lists the rule that fired");
        CHECK(fake->sets.empty(), "still nothing written: only the button sends");
        // the base comes from the Match scope's own CPU setting when it is switched on
        go_scope(ui, "Match (both teams)");
        CHECK(type_slider(ui, "match.injury_frequency_cpu", "70"), "the Match scope's CPU injury frequency 70");
        go_scope(ui, "Opposition");
        CHECK(has_part(tactics_tab_state().opp.live, "INJURY_FREQUENCY_CPUAI = 78  [game variable]  (base 70 from your Match setting + offset +8)"),
              "the base is now 70: " + (tactics_tab_state().opp.live.empty() ? std::string("none") : tactics_tab_state().opp.live[0]));
        // dry run first, then Apply
        CHECK(ui.click("Dry run##tacdry", "##tacright"), "dry run on");
        CHECK(ui.click("Apply live offsets to the game##tacoppapply", "##taccentre"), "Apply (dry run)");
        ui.frames(2);
        CHECK(fake->sets.empty() && ui.toast_contains("Opposition dry run"), "a dry run sends nothing");
        CHECK(ui.click("Dry run##tacdry", "##tacright"), "dry run off");
        CHECK(ui.click("Apply live offsets to the game##tacoppapply", "##taccentre"), "Apply");
        ui.frames(2);
        bool freq = false, diff = false;
        for (const auto& kv : fake->sets) {
            freq = freq || (kv.first == "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_CPUAI" && kv.second == 78);
            diff = diff || (kv.first == "OVERRIDE_MATCH_DIFFICULTY" && kv.second == 4);
        }
        CHECK(fake->sets.size() == 2 && freq && diff, fmt("only the two game-variable offsets were sent (%zu)", fake->sets.size()));
        CHECK(ui.toast_contains("Opposition: 2 game variable(s) set for the match against Everton"), "toast");
        // the kill switch blocks the card's Apply too
        fake->sets.clear();
        fs::create_directories(le / "turbo_output");
        {
            std::ofstream f((le / "turbo_output" / "tactics_off.txt").string(), std::ios::binary);
            f << "off";
        }
        ui.frames(130);
        CHECK(!tactics_tab_state().opp.can_apply, "Apply is off while tactics_off.txt exists");
        CHECK(ui.click("Apply live offsets to the game##tacoppapply", "##taccentre"), "click the disabled button");
        ui.frames(2);
        CHECK(fake->sets.empty(), "no write with the kill switch present");
        fs::remove(le / "turbo_output" / "tactics_off.txt");
        ui.frames(130);
        // the none mode: an empty profile, nothing to send
        CHECK(ui.click("Opposition variety on##tacoppon", "##taccentre"), "variety off");
        ui.frames(3);
        CHECK(!tactics_tab_state().opp.active && tactics_tab_state().opp.explain.find("is off") != std::string::npos && !tactics_tab_state().opp.can_apply &&
                  tactics_tab_state().opp.live.empty() && tactics_tab_state().opp.changes.empty(),
              "off: no profile, nothing to send: " + tactics_tab_state().opp.explain);
        CHECK(ui.click("Opposition variety on##tacoppon", "##taccentre"), "variety on");
        ui.frames(3);
        CHECK(tactics_tab_state().opp.active, "on again");
        // your own club is not an opponent
        select_team(app, ui, 1);
        ui.frames(3);
        CHECK(tactics_tab_state().opp.note.find("your own club") != std::string::npos && !tactics_tab_state().opp.active, "the user's own club: " + tactics_tab_state().opp.note);
        // clean up: the user's rule, the Match scope value, the fake service
        app.opp_user_rules = RuleSet();
        go_scope(ui, "Match (both teams)");
        app.match_setup = nullptr;
    });
}

}  // namespace tactics_tab_test

static void ui_cases_tactics_tab(App& app, Ui& ui, const fs::path& le) {
    tactics_tab_test::cases_db(app);
    tactics_tab_test::cases_panel(app, ui, le);
    tactics_tab_test::cases_mentality(app, ui, le);
    tactics_tab_test::cases_presets(app, ui, le);
    tactics_tab_test::cases_opposition(app, ui, le);
}
