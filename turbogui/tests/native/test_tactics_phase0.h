// Native tests (Turbo 2.1 Phase 0, docs/plans/tactics-2.1/PLAN.md section 7): the Tactics tab honesty layer in core/tactics_status.h:
// two badges per control (how written / what was seen), legacy hiding and the role-familiarity label, dead write targets, one kill switch
// per write channel, presets that leave untested controls off, the snapshot store behind "Reset to default" and the master kill switch's
// restore (per career club). Included by test_main.cpp after its framework (CHECK, run_case, fmt, g_out). Pure data: no game memory, no ImGui.
#pragma once
#include <algorithm>
#include <fstream>

#include "core/gameplay_sliders.h"
#include "core/match_setup.h"
#include "core/sliders.h"
#include "core/tactic_profiles.h"
#include "core/tactics_status.h"

namespace tactics_phase0_test {

using namespace turbo;

static fs::path scratch(const char* name) {
    const fs::path d = g_out / "tactics_phase0" / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

static TacticStatus status_of(const char* key) {
    const SliderDef* d = find_slider(key);
    CHECK(d != nullptr, std::string("registry has ") + key);
    return d ? tactic_status(*d) : TacticStatus();
}

static void touch(const fs::path& p) {
    std::ofstream f(p.string(), std::ios::binary);
    f << "off";
}

static void test_badges() {
    run_case("phase0 badges: how written and what was seen are two separate badges, per status, and no 'Live' badge exists", [] {
        // seen in a played match 2026-10-07 (turbo_dev\evidence\2026-10-07\RESULTS.md): the four injury sliders, weather, CPU subs
        for (const char* k : {"match.injury_frequency_user", "match.injury_frequency_cpu", "match.injury_severity_user", "match.injury_severity_cpu",
                              "match.weather", "match.cpu_subs_off"}) {
            const TacticStatus s = status_of(k);
            CHECK(s.written == WriteKind::GameVar && std::string(written_badge(s.written)) == "game variable", std::string(k) + ": written as a game variable");
            CHECK(s.seen == SeenKind::SeenInMatch && seen_badge(s) == "seen in a played match 2026-10-07", std::string(k) + ": seen badge: " + seen_badge(s));
            CHECK(!s.note.empty() && !untested_write(s) && !s.legacy_hidden, std::string(k) + ": has its evidence note, not untested, not hidden");
        }
        // accepted, no effect seen: difficulty and time of day lose the green badge
        for (const char* k : {"match.difficulty", "match.time_of_day", "opp.difficulty_offset"}) {
            const TacticStatus s = status_of(k);
            CHECK(s.written == WriteKind::GameVar && s.seen == SeenKind::NoEffectSeen && seen_badge(s) == "no effect seen 2026-10-07", std::string(k) + ": no effect seen: " + seen_badge(s));
            CHECK(untested_write(s), std::string(k) + ": counts as untested (off by default, Apply asks)");
            CHECK(status_tag(s).find("Live") == std::string::npos && status_tag(s).find("no effect seen") != std::string::npos, std::string(k) + ": tag: " + status_tag(s));
        }
        // accepted, untested: NEVER_INJURE, the referee fields, the teams style fields, the role familiarity
        for (const char* k : {"match.never_injure", "match.foul_strictness", "match.card_strictness", "team.line_depth", "team.buildup_play", "pos.role1", "pos.role9"}) {
            const TacticStatus s = status_of(k);
            CHECK(s.seen == SeenKind::Untested && seen_badge(s) == "untested" && untested_write(s), std::string(k) + ": untested: " + seen_badge(s));
            CHECK(s.written == (find_slider(k)->status == SliderStatus::Live ? WriteKind::GameVar : WriteKind::DbRow), std::string(k) + ": write badge follows the binding");
            CHECK(!s.legacy_hidden, std::string(k) + ": not hidden");
        }
        CHECK(status_of("match.never_injure").note.find("2026-10-04") != std::string::npos, "NEVER_INJURE: the note says when it was accepted");
        CHECK(status_of("team.line_depth").note.find("NOT written") != std::string::npos, "line depth: the note says the saved tactic row is not written");
        // legacy: EA removed these controls in FC 25: hidden, off, labelled
        for (const char* k : {"team.width_def", "team.width_att", "team.players_in_box_cross", "team.players_in_box_corner", "team.players_in_box_fk",
                              "team.chance_creation", "team.offensive_style", "team.defensive_style"}) {
            const TacticStatus s = status_of(k);
            CHECK(s.written == WriteKind::DbRow && s.seen == SeenKind::Legacy && seen_badge(s) == "legacy, likely ignored", std::string(k) + ": legacy badge: " + seen_badge(s));
            CHECK(s.legacy_hidden && legacy_hidden(*find_slider(k)) && untested_write(s), std::string(k) + ": hidden behind 'show legacy fields', off by default");
        }
        // the 7-step mentality: FC 27 has no such control: hidden, preview, nothing written
        {
            const TacticStatus s = status_of("team.mentality");
            CHECK(s.legacy_hidden && s.written == WriteKind::Preview && s.seen == SeenKind::NothingWritten, "mentality is legacy-hidden preview");
            CHECK(status_tag(s) == "not applied to game", "its tag: " + status_tag(s));
        }
        // the role editor is renamed in the registry (P0-5): the player's familiarity, not a slot role; still a database row with its undo
        for (int i = 1; i <= 9; ++i) {
            const std::string k = "pos.role" + std::to_string(i);
            const TacticStatus s = status_of(k.c_str());
            const SliderDef* d = find_slider(k);
            CHECK(s.label == "Role familiarity " + std::to_string(i) + " (Role+ / Role++)" && d && d->label == s.label, "role label: " + s.label);
            CHECK(d && d->group == "Player's familiarity" && d->description.find("not the role of a tactic slot") != std::string::npos,
                  k + ": group and description name it the player's familiarity");
            CHECK(d && d->status == SliderStatus::DB && d->binding.targets == std::vector<std::string>({"players.role" + std::to_string(i)}), k + ": still players.role" + std::to_string(i));
            CHECK(s.note.find("not the role of a tactic slot") != std::string::npos, "role note: " + s.note);
        }
        // preview-only sliders: nothing written, never 'seen'
        for (const char* k : {"match.sprint_speed", "match.pass_error", "team.tempo", "team.formation", "pos.roam", "opp.formation"}) {
            const TacticStatus s = status_of(k);
            CHECK(s.written == WriteKind::Preview && s.seen == SeenKind::NothingWritten && status_tag(s) == "not applied to game", std::string(k) + ": preview: " + status_tag(s));
            CHECK(!untested_write(s), std::string(k) + ": a preview is not an untested write");
        }
        // Turbo-only solver parameters
        {
            const TacticStatus s = status_of("opp.variety_strength");
            CHECK(s.written == WriteKind::TurboOnly && std::string(written_badge(s.written)) == "Turbo only" && s.seen == SeenKind::NothingWritten, "solver parameter: Turbo only");
            CHECK(status_tag(s) == "Turbo only, never written to the game", "its tag");
        }
        // the whole registry: "seen in a played match" only on the proven rows; a control without a write path is never 'seen'
        std::set<std::string> seen;
        for (const SliderDef& d : slider_registry()) {
            const TacticStatus s = tactic_status(d);
            if (s.seen == SeenKind::SeenInMatch) seen.insert(d.key);
            if (!writes_game(d)) CHECK(s.seen == SeenKind::NothingWritten, d.key + ": no write path, nothing seen");
            else CHECK(s.seen != SeenKind::NothingWritten, d.key + ": a write path has a seen badge");
            CHECK(!s.label.empty() && !seen_badge(s).empty() && written_badge(s.written)[0] != 0, d.key + ": badges are never empty");
            CHECK(seen_badge(s) != "Live" && std::string(written_badge(s.written)) != "Live" && status_tag(s).find("in-game effect unverified") == std::string::npos,
                  d.key + ": the old wording is gone");
        }
        const std::set<std::string> expect = {"match.injury_frequency_user", "match.injury_frequency_cpu", "match.injury_severity_user", "match.injury_severity_cpu",
                                              "match.weather", "match.cpu_subs_off", "opp.injury_frequency_offset", "opp.injury_severity_offset"};
        CHECK(seen == expect, fmt("exactly the proven rows carry 'seen in a played match': %zu", seen.size()));
        // the help texts exist for every badge
        for (WriteKind k : {WriteKind::GameVar, WriteKind::DbRow, WriteKind::Preview, WriteKind::TurboOnly, WriteKind::LiveSheet}) CHECK(written_help(k)[0] != 0, "written help");
        // channel E's badges (ui_live_tactic.cpp): written "live team sheet", seen "untested" until the in-game acceptance test
        for (const char* k : {"live.build_up", "live.depth", "live.role"}) {
            const TacticStatus ls = live_sheet_status(k, "x");
            CHECK(ls.written == WriteKind::LiveSheet && std::string(written_badge(ls.written)) == "live team sheet" && ls.seen == SeenKind::Untested &&
                      seen_badge(ls) == "untested" && ls.date.empty() && !ls.note.empty() && status_tag(ls).find("live team sheet, untested: ") == 0,
                  std::string(k) + ": " + status_tag(ls));
        }
        for (SeenKind k : {SeenKind::SeenInMatch, SeenKind::NoEffectSeen, SeenKind::Untested, SeenKind::Legacy, SeenKind::NothingWritten}) CHECK(seen_help(k)[0] != 0, "seen help");
    });

    run_case("phase0 x channel A: the per-side gameplay slider variables are 'game variable' + 'untested', load switched OFF, one switch blocks them", [] {
        size_t n = 0;
        for (const SliderDef& d : slider_registry()) {
            if (!d.untested) continue;
            ++n;
            const TacticStatus s = tactic_status(d);
            const gs::Slider* gsl = nullptr;
            gs::Side side = gs::Side::User;
            CHECK(d.binding.targets.size() == 1 && gs::parse_var(d.binding.targets[0], gsl, side), d.key + ": a channel A variable");
            CHECK(s.written == WriteKind::GameVar && std::string(written_badge(s.written)) == "game variable", d.key + ": written as a game variable");
            CHECK(s.seen == SeenKind::Untested && seen_badge(s) == "untested" && untested_write(s) && !s.legacy_hidden, d.key + ": untested: " + seen_badge(s));
            CHECK(s.note.find("variable store") != std::string::npos && status_tag(s).find("game variable, untested: accepted by the game's variable store") == 0,
                  d.key + ": tag: " + status_tag(s));
            CHECK(!d.binding.targets.empty() && channel_of_target(d.binding.targets[0]) == WriteChannel::GameVar, d.key + ": the game-variable channel");
        }
        CHECK(n == 66, fmt("66 channel A rows, got %zu", n));
        // the proven rows keep their own evidence (the channel A note is only for untested rows)
        CHECK(status_of("match.injury_frequency_user").seen == SeenKind::SeenInMatch && status_of("match.never_injure").note.find("variable store") == std::string::npos,
              "the classic rows keep their notes");
        // kill switches: the master and the game-variable file block channel A, the database file does not
        SwitchState sw;
        CHECK(!sw.blocks(WriteChannel::GameVar), "no file: not blocked");
        sw.present = {"tactics_db_off.txt"};
        CHECK(!sw.blocks(WriteChannel::GameVar), "tactics_db_off.txt leaves game variables alone");
        sw.present = {"tactics_gamevar_off.txt"};
        CHECK(sw.blocks(WriteChannel::GameVar) && sw.blocking_file(WriteChannel::GameVar) == "tactics_gamevar_off.txt", "tactics_gamevar_off.txt blocks them");
        sw.present = {"tactics_off.txt"};
        CHECK(sw.blocks(WriteChannel::GameVar) && sw.blocking_file(WriteChannel::GameVar) == "tactics_off.txt", "the master blocks them");
        // a preset never switches a channel A variable on by itself
        Profile p;
        p.category = ProfileCategory::Match;
        p.sliders.set("match.injury_frequency_user", 65, true);
        p.sliders.set("match.sprint_speed_cpu", 0, true);
        SliderSet w;
        const ProfileLoadResult r = load_profile_into(p, w);
        CHECK(w.is_enabled("match.injury_frequency_user") && !w.is_enabled("match.sprint_speed_cpu") && w.value_or_default("match.sprint_speed_cpu") == 0,
              "the channel A value is loaded switched off");
        CHECK(r.held_off == std::vector<std::string>({"match.sprint_speed_cpu"}), fmt("held off: %zu", r.held_off.size()));
    });

    run_case("phase0 status table: every key it names is in the registry; a 'seen' or 'no effect seen' row carries its date and its evidence", [] {
        size_t n = 0;
        for (const auto& kv : tactics_status_detail::table()) {
            ++n;
            CHECK(find_slider(kv.first) != nullptr, kv.first + ": the status table names a registry key (a typo would silently drop its badge)");
            const auto& e = kv.second;
            if (e.seen == SeenKind::SeenInMatch || e.seen == SeenKind::NoEffectSeen)
                CHECK(std::string(e.date).size() == 10 && std::string(e.note).size() > 10, kv.first + ": a dated badge needs its date and a note");
            else
                CHECK(std::string(e.date).empty(), kv.first + ": only seen / no-effect rows carry a date");
            CHECK(std::string(e.note).size() > 0, kv.first + ": every row says why");
        }
        CHECK(n >= 30, fmt("status rows: %zu", n));
        // every slider the registry shows has a note when it writes something nobody saw working (the tag says why)
        for (const SliderDef& d : slider_registry()) {
            const TacticStatus s = tactic_status(d);
            if (untested_write(s)) CHECK(!s.note.empty(), d.key + ": an untested write says why");
        }
    });

    run_case("phase0 registry: every game-variable binding is a variable Turbo knows, with a compatible range (P0-6)", [] {
        size_t checked = 0;
        for (const SliderDef& d : slider_registry()) {
            if (d.binding.kind != BindingKind::GameVar) continue;
            CHECK(!d.binding.targets.empty(), d.key + ": has a target");
            for (const std::string& t : d.binding.targets) {
                const gv::KnownVar* kv = gv::known(t);
                CHECK(kv != nullptr, d.key + ": " + t + " is a variable Turbo knows (gv::known_vars)");
                if (!kv) continue;
                ++checked;
                if (d.key.rfind("opp.", 0) == 0) continue;  // an offset: added to the Match value and clamped at Apply
                CHECK(d.min >= kv->min && d.max <= kv->max, fmt("%s: slider %d..%d inside the variable's %d..%d", d.key.c_str(), d.min, d.max, kv->min, kv->max));
            }
        }
        CHECK(checked >= 12, fmt("game-variable bindings checked: %zu", checked));
    });
}

static void test_writes() {
    run_case("phase0 dead targets: the saved tactic row (cm_mentalities) is gone from the registry and refused anyway; the teams row stays (P0-2)", [] {
        CHECK(dead_target_reason("cm_mentalities.defensivedepth") != nullptr && dead_target_reason("cm_mentalities.buildupplay") != nullptr, "cm_mentalities is dead");
        CHECK(std::string(dead_target_reason("cm_mentalities.defensivedepth")).find("overwritten") != std::string::npos, "the reason names the overwrite at save");
        for (const char* t : {"teams.defensivedepth", "teams.buildupplay", "referee.foulstrictness", "players.role1", "mentalities.defensivewidth",
                              "GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER"})
            CHECK(dead_target_reason(t) == nullptr, std::string(t) + " is not dead");
        // no slider of the registry names a dead target
        for (const SliderDef& d : slider_registry())
            for (const std::string& t : d.binding.targets) CHECK(dead_target_reason(t) == nullptr, d.key + ": " + t + " is not a dead target");
        const SliderDef* d = find_slider("team.line_depth");
        CHECK(d && d->binding.targets == std::vector<std::string>({"teams.defensivedepth"}) && d->status == SliderStatus::DB, "line depth writes teams.defensivedepth only");
        d = find_slider("team.buildup_play");
        CHECK(d && d->binding.targets == std::vector<std::string>({"teams.buildupplay"}) && d->status == SliderStatus::DB, "build-up writes teams.buildupplay only");
        CHECK(validate_registry().empty(), "the registry still validates: " + validate_registry());
    });

    run_case("phase0 snapshot rows: the career's club in every key, parsed back, the row kinds a restore can find (P0-9)", [] {
        CHECK(snapshot_row(95, "team:1") == "club:95|team:1" && snapshot_row(0, "referee:155") == "club:0|referee:155", "the row names the club");
        SnapshotRow r;
        CHECK(parse_snapshot_row("club:95|team:1", r) && r.club == 95 && r.kind == "team" && r.id == 1, "parsed: club 95, team 1");
        CHECK(parse_snapshot_row("club:1|mentalities:12", r) && r.club == 1 && r.kind == "mentalities" && r.id == 12, "parsed: mentalities 12");
        CHECK(parse_snapshot_row("club:1|player:-3", r) && r.id == -3, "a negative id is still a number");
        for (const char* bad : {"team:1", "club:x|team:1", "club:1|team", "club:1|team:", "club:1|:4", "club:1|team:1x", "club:|team:1", ""})
            CHECK(!parse_snapshot_row(bad, r), std::string("refused: '") + bad + "'");
        CHECK(std::string(snapshot_key_field("team")) == "teamid" && std::string(snapshot_key_field("player")) == "playerid" &&
                  std::string(snapshot_key_field("referee")) == "refereeid" && std::string(snapshot_key_field("mentalities")) == "mentalityid" &&
                  std::string(snapshot_key_field("cm_mentalities")).empty(),
              "the key field of each kind; the saved tactic row is not one");
        // what the master kill switch does with a kept row
        DbSnapshot s{"teams.defensivedepth", "club:1|team:1", 50, "2026-10-11 15:00", "Arsenal", 62, true};
        CHECK(restore_verdict(s, 1, 62) == RestoreVerdict::Restore, "this career, the row still holds Turbo's 62: restore");
        CHECK(restore_verdict(s, 1, 63) == RestoreVerdict::ChangedSince, "the row changed since: leave it");
        CHECK(restore_verdict(s, 95, 62) == RestoreVerdict::OtherCareer, "another career's club: never written into this one");
        s.has_written = false;
        CHECK(restore_verdict(s, 1, 50) == RestoreVerdict::NeverWritten, "Turbo's write never landed: nothing to undo");
        s.row = "team:1";
        CHECK(restore_verdict(s, 1, 50) == RestoreVerdict::NotARow, "a row without a club is not restored");
        s.row = "club:1|formation:3";
        CHECK(restore_verdict(s, 1, 50) == RestoreVerdict::NotARow, "an unknown kind is not restored");
    });

    run_case("phase0 kill switches: one file per write channel, the master blocks everything, the channel file only its rows", [] {
        const auto& ks = kill_switches();
        CHECK(ks.size() == 5 && std::string(ks.front().file) == "tactics_off.txt" && ks.front().all, "tactics_off.txt stays the master switch, first in the list");
        std::set<std::string> files;
        for (const KillSwitch& k : ks) {
            files.insert(k.file);
            CHECK(std::string(k.file).find("_off.txt") != std::string::npos && k.what[0] != 0, std::string(k.file) + ": named *_off.txt with a purpose");
        }
        CHECK(files.count("tactics_gamevar_off.txt") && files.count("tactics_db_off.txt") && files.count("tactics_legacy_off.txt") &&
                  files.count("call_team_sheet_off.txt"),
              "one switch per channel (call_team_sheet_off.txt: your club's live tactic, channel E)");
        CHECK(std::string(channel_switch_file(WriteChannel::LiveSheet)) == "call_team_sheet_off.txt" && std::string(channel_name(WriteChannel::LiveSheet)) == "live team sheet",
              "the live team sheet channel");
        CHECK(channel_of_target("GAMEPLAY_CUSTOMIZATION/INJURY_FREQUENCY_USER") == WriteChannel::GameVar && channel_of_target("OVERRIDE/WEATHER") == WriteChannel::GameVar,
              "game variables");
        CHECK(channel_of_target("teams.defensivedepth") == WriteChannel::DbRow && channel_of_target("referee.foulstrictness") == WriteChannel::DbRow &&
                  channel_of_target("players.role1") == WriteChannel::DbRow,
              "database rows");
        CHECK(channel_of_target("mentalities.defensivewidth") == WriteChannel::LegacyDb, "the old tactic table is its own channel");
        CHECK(std::string(channel_switch_file(WriteChannel::GameVar)) == "tactics_gamevar_off.txt" && std::string(channel_switch_file(WriteChannel::DbRow)) == "tactics_db_off.txt" &&
                  std::string(channel_switch_file(WriteChannel::LegacyDb)) == "tactics_legacy_off.txt",
              "channel to file");
        // every slider that writes has a channel with a switch file
        for (const SliderDef& d : slider_registry()) {
            if (!writes_game(d)) continue;
            for (const std::string& t : d.binding.targets) CHECK(channel_switch_file(channel_of_target(t))[0] != 0, d.key + ": " + t + " has a kill switch file");
        }
        SwitchState none;
        CHECK(!none.all() && !none.blocks(WriteChannel::GameVar) && !none.blocks(WriteChannel::DbRow) && none.blocking_file(WriteChannel::DbRow).empty(), "no file: nothing blocked");
        SwitchState db;
        db.present.insert("tactics_db_off.txt");
        CHECK(!db.all() && db.blocks(WriteChannel::DbRow) && !db.blocks(WriteChannel::GameVar) && !db.blocks(WriteChannel::LegacyDb), "the db file blocks only database rows");
        CHECK(db.blocking_file(WriteChannel::DbRow) == "tactics_db_off.txt" && db.blocking_file(WriteChannel::GameVar).empty(), "it names itself");
        SwitchState all;
        all.present.insert("tactics_off.txt");
        CHECK(all.all() && all.blocks(WriteChannel::GameVar) && all.blocks(WriteChannel::DbRow) && all.blocks(WriteChannel::LegacyDb), "the master blocks every channel");
        CHECK(all.blocking_file(WriteChannel::LegacyDb) == "tactics_off.txt", "and is the file named");
        // read from a directory
        const fs::path dir = scratch("switches");
        CHECK(read_switches(dir).present.empty(), "an empty directory: no switch");
        touch(dir / "tactics_gamevar_off.txt");
        touch(dir / "unrelated_off.txt");
        SwitchState s = read_switches(dir);
        CHECK(s.present == std::set<std::string>({"tactics_gamevar_off.txt"}) && s.blocks(WriteChannel::GameVar) && !s.blocks(WriteChannel::DbRow), "only the known files count");
        CHECK(s.files() == std::vector<std::string>({"tactics_gamevar_off.txt"}), "listed for the UI");
    });
}

static void test_presets() {
    run_case("phase0 presets: a preset switches on only what was seen working; untested and legacy settings load switched OFF", [] {
        Profile p;
        p.category = ProfileCategory::Bundle;
        p.sliders.set("match.injury_frequency_user", 65, true);   // seen in a played match: on
        p.sliders.set("match.foul_strictness", 2, true);          // untested database row: off
        p.sliders.set("match.difficulty", 5, true);               // no effect seen: off
        p.sliders.set("team.width_att", 85, true);                // legacy: off
        p.sliders.set("team.mentality", 5, true);                 // legacy-hidden preview: off
        p.sliders.set("team.tempo", 70, true);                    // preview only: follows the preset
        p.sliders.set("opp.variety_strength", 30, true);          // Turbo only: follows the preset
        p.sliders.set("match.weather", 3, false);                 // the preset leaves it to the game
        SliderSet w;
        w.set("match.weather", 2, true);  // was on before the load: the preset resets it
        const ProfileLoadResult r = load_profile_into(p, w);
        CHECK(w.is_enabled("match.injury_frequency_user") && w.value_or_default("match.injury_frequency_user") == 65, "the proven slider is on with its value");
        CHECK(w.is_enabled("team.tempo") && w.is_enabled("opp.variety_strength"), "preview and Turbo-only settings follow the preset");
        for (const char* k : {"match.foul_strictness", "match.difficulty", "team.width_att", "team.mentality"})
            CHECK(!w.is_enabled(k) && w.has(k), std::string(k) + ": loaded with its value but switched off");
        CHECK(w.value_or_default("match.foul_strictness") == 2 && w.value_or_default("team.width_att") == 85, "the held-off values are kept for the user to switch on");
        CHECK(!w.is_enabled("match.weather") && !w.has("match.weather"), "what the preset leaves to the game is reset");
        CHECK(r.held_off.size() == 4 && r.enabled.size() == 3, fmt("report: %zu held off, %zu enabled", r.held_off.size(), r.enabled.size()));
        CHECK(std::find(r.held_off.begin(), r.held_off.end(), "team.width_att") != r.held_off.end(), "the report names the legacy field");
        // the built-ins no longer seed a legacy field at all (P0-3: Wing play, Possession and Balanced used to set the widths)
        for (const Profile& b : builtin_profiles())
            for (const auto& kv : b.sliders.values) {
                const SliderDef* d = find_slider(kv.first);
                // (the 7-step mentality is hidden too, but it is a preview row that writes nothing: its removal is P0-4, not this phase)
                CHECK(d && (!legacy_hidden(*d) || !writes_game(*d)), b.name + ": seeds no legacy field: " + kv.first);
                CHECK(d && tactic_status(*d).seen != SeenKind::Legacy, b.name + ": no legacy-badged key: " + kv.first);
            }
        const Profile* wing = nullptr;
        for (const Profile& b : builtin_profiles())
            if (b.id == "builtin.team.wing_play") wing = &b;
        CHECK(wing && !wing->sliders.has("team.width_att") && !wing->sliders.has("team.width_def") && !wing->sliders.has("team.players_in_box_cross"),
              "Wing play stops seeding the widths and the box count");
        // every built-in preset: nothing untested or legacy comes on by itself
        for (const Profile& b : builtin_profiles()) {
            SliderSet s;
            load_profile_into(b, s);
            for (const std::string& k : s.enabled) {
                const SliderDef* d = find_slider(k);
                CHECK(d && !untested_write(tactic_status(*d)) && !legacy_hidden(*d), b.name + ": " + k + " may come on");
            }
        }
        // a preset with no enabled flag at all enables nothing
        Profile q;
        q.sliders.set("match.injury_frequency_user", 10, false);
        SliderSet e;
        CHECK(load_profile_into(q, e).enabled.empty() && !e.is_enabled("match.injury_frequency_user"), "value without the flag: off");
    });
}

static void test_snapshots() {
    run_case("phase0 snapshots: the value before Turbo's first write is kept once, found, forgotten, and survives the file", [] {
        TacticSnapshots s;
        CHECK(s.remember("teams.defensivedepth", "team:1", 50, "2026-10-11 15:00", "Arsenal"), "first write: kept");
        CHECK(!s.remember("teams.defensivedepth", "team:1", 62, "2026-10-11 15:01", "Arsenal"), "second write: the original stays");
        const DbSnapshot* f = s.find("teams.defensivedepth", "team:1");
        CHECK(f && f->value == 50 && f->when == "2026-10-11 15:00" && f->label == "Arsenal", "the first value is the one kept");
        CHECK(s.find("teams.defensivedepth", "team:7") == nullptr && s.find("teams.buildupplay", "team:1") == nullptr, "other rows and fields are not kept");
        // what Turbo wrote: a restore may only undo Turbo's own write
        CHECK(TacticSnapshots::may_restore(*f, 99), "without a written value anything may be restored");
        CHECK(s.note_written("teams.defensivedepth", "team:1", 62) && f->has_written && f->written == 62, "the written value is noted");
        CHECK(!s.note_written("teams.defensivedepth", "team:7", 1), "nothing noted for a row that is not kept");
        CHECK(TacticSnapshots::may_restore(*f, 62) && !TacticSnapshots::may_restore(*f, 63), "restore only while the row still holds 62");
        CHECK(s.note_written("teams.defensivedepth", "team:1", 70) && f->written == 70 && f->value == 50, "a second write moves 'written', never the original");
        CHECK(s.remember("referee.foulstrictness", "referee:155", 1, "2026-10-11 15:02", "40 referees") && s.remember("referee.foulstrictness", "referee:156", 2, "2026-10-11 15:02", "40 referees"),
              "every referee row keeps its own value");
        // JSON round trip
        const std::string text = s.to_json();
        TacticSnapshots back;
        std::string err;
        CHECK(TacticSnapshots::parse(text, back, &err), "parses: " + err);
        CHECK(back.items.size() == 3 && back.find("referee.foulstrictness", "referee:156") && back.find("referee.foulstrictness", "referee:156")->value == 2, "every row came back");
        CHECK(back.find("teams.defensivedepth", "team:1") && back.find("teams.defensivedepth", "team:1")->has_written && back.find("teams.defensivedepth", "team:1")->written == 70,
              "the written value survives the file");
        CHECK(back.find("referee.foulstrictness", "referee:155") && !back.find("referee.foulstrictness", "referee:155")->has_written, "a row never written stays without one");
        CHECK(back.to_json() == text, "stable text");
        CHECK(s.forget("teams.defensivedepth", "team:1") && !s.find("teams.defensivedepth", "team:1") && !s.forget("teams.defensivedepth", "team:1"), "forget once");
        // not a snapshot file, malformed rows
        TacticSnapshots bad;
        CHECK(!TacticSnapshots::parse("{\"hello\":1}", bad, &err) && !err.empty(), "another file is refused: " + err);
        CHECK(!TacticSnapshots::parse("not json", bad, &err), "not JSON is refused");
        size_t dropped = 0;
        CHECK(TacticSnapshots::parse("{\"turbo_tactics_snapshot\":1,\"rows\":[{\"target\":\"teams.x\",\"row\":\"team:1\",\"value\":3},{\"target\":1},7]}", bad, &err, &dropped) &&
                  bad.items.size() == 1 && dropped == 2,
              fmt("malformed rows are dropped and counted: %zu kept, %zu dropped", bad.items.size(), dropped));
        // files: a missing file is an empty store; save then load
        const fs::path root = scratch("snapfile");
        const fs::path p = tactics_snapshot_path(root);
        CHECK(p == root / "turbo_output" / "tactics_snapshot.json", "the file lives in turbo_output");
        TacticSnapshots loaded;
        loaded.remember("x.y", "r", 1, "", "");
        CHECK(load_tactic_snapshots(p, loaded, &err) && loaded.empty(), "missing file: empty store, no error");
        CHECK(save_tactic_snapshots(p, s, &err) && fs::exists(p), "saved: " + err);
        CHECK(load_tactic_snapshots(p, loaded, &err) && loaded.items.size() == 2 && loaded.find("referee.foulstrictness", "referee:155"), "loaded again");
        {
            std::ofstream f(p.string(), std::ios::binary | std::ios::trunc);
            f << "{\"something\":true}";
        }
        CHECK(!load_tactic_snapshots(p, loaded, &err) && !err.empty(), "a foreign file is reported, not used: " + err);
    });
}

static void run() {
    std::printf("native tactics phase 0 (honesty layer)\n");
    test_badges();
    test_writes();
    test_presets();
    test_snapshots();
}

}  // namespace tactics_phase0_test
