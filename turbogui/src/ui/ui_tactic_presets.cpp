// FC 27 LE Turbo GUI - the Tactic presets panel (see ui_tactic_presets.h, core/tactic_presets.h).
//
// Frame-loop rules as in the other views: never throws; the presets folder is read at the first look and after a change; the live tactic is
// read at most once a second (the card's own read); nothing is written by drawing. A Load or Undo writes only through
// live_tactic_submit (the Tactics card's guarded game-thread job, every gate G1..G6) and game_settings_stage / _unstage.
#include "ui_tactic_presets.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>

#include "app.h"
#include "core/game_settings.h"
#include "core/tactic_presets.h"
#include "imgui.h"
#include "ui_game_settings.h"
#include "ui_live_tactic.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;

const char* kBanner =
    "EA's own tactic/settings; takes effect at next match / needs Gameplay Type CUSTOM for sliders. Not proven in a played match yet: "
    "nobody has shown the match plays these values. Limits: the tactic part loads live in the career hub with Team Management closed; the "
    "sliders part is applied with the game closed (turbo_settings_apply.exe) and needs Gameplay Type CUSTOM; EA's Edit Tactic screen may still show "
    "the old wording; after each career load, load the tactic again (the panel offers Re-apply); Build-Up Style 2/3 and Defensive Approach 3 are "
    "never written; the formation, width and the match setup are not part of a preset.";
const char* kSafety =
    "Team Management (Edit Tactic) must be closed. If you later open Edit Tactic and answer Yes to 'save your changes to your team sheet', "
    "the game commits its screen values and AUTOSAVES your career.";

enum class JobKind { None, Load, Undo, Reapply };

struct Pv {
    bool loaded = false;
    std::vector<tpreset::Preset> presets;
    std::vector<std::string> errors;
    int sel = -1;
    char name[64] = "";
    bool include_settings = true;
    bool delete_armed = false;
    std::string status;
    // the live tactic
    bool have_live = false;
    tsheet::LiveTactic live;
    std::string live_why;
    double next_read = -1.0;
    // the latest load
    bool have_last = false;
    tpreset::LastLoad last;
    std::string last_err;
    // the confirmation
    bool confirm_request = false;
    JobKind confirm_kind = JobKind::None;
    std::string confirm_title;
    std::vector<std::string> confirm_lines;
    tsheet::Request confirm_req;
    bool confirm_tactic = false, confirm_settings = false, confirm_card_undo = false;
    tpreset::LastLoad confirm_last;     // what the Yes will write
    tpreset::Preset confirm_preset;     // the preset being loaded (its settings block)
    // the job in flight
    JobKind job = JobKind::None;
    long long job_seq0 = 0;
    double job_t0 = 0.0;
    bool job_tactic = false, job_settings = false;
    tpreset::LastLoad job_last;
    tpreset::Preset job_preset;
};
Pv g;
TacticPresetsState g_state;

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }

std::string stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &tm);
    return b;
}

void log_line(App& app, const std::string& s) {
    app.log(s);
    if (app.log_hook) app.log_hook(s);
}

void load_presets_now(App& app) {
    g.errors.clear();
    g.presets = tpreset::load_presets(tpreset::presets_dir(out_dir(app)), &g.errors);
    if (g.sel >= int(g.presets.size())) g.sel = -1;
}

void load_last_now(App& app) {
    g.last_err.clear();
    g.have_last = tpreset::load_last(tpreset::last_load_path(out_dir(app)), g.last, &g.last_err);
}

void first_look(App& app) {
    if (g.loaded) return;
    g.loaded = true;
    g.include_settings = true;
    load_presets_now(app);
    load_last_now(app);
}

void read_live(App& app, bool force) {
    if (!force && app.now < g.next_read) return;
    g.next_read = app.now + 1.0;
    std::string why;
    tsheet::LiveTactic lt;
    g.have_live = live_tactic_read(app, lt, why);
    if (g.have_live) g.live = lt;
    g.live_why = why;
}

void save_last(App& app, const tpreset::LastLoad& l) {
    std::string err;
    if (!tpreset::save_last(tpreset::last_load_path(out_dir(app)), l, &err)) log_line(app, "tactic presets: " + std::string(tpreset::last_load_file()) + " not written: " + err);
    g.last = l;
    g.have_last = true;
}

std::string trimmed_name() {
    std::string n = g.name;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    return n;
}

void say(App& app, const std::string& s, bool error = false) {
    g.status = s;
    log_line(app, "tactic presets: " + s);
    if (error) app.notify(s, true);
}

// ------------------------------------------------------------------------------------------------ Save current
void save_current(App& app) {
    const std::string wanted = trimmed_name();
    if (wanted.empty()) {
        app.notify("Type a name for the preset first", true);
        return;
    }
    read_live(app, true);
    std::map<std::string, int> sliders;
    std::string layout, map_build, swhy;
    bool have_sliders = false;
    if (g.include_settings) have_sliders = game_settings_current(app, sliders, layout, map_build, swhy);
    if (!g.have_live && !have_sliders) {
        say(app, "Not saved: the live tactic cannot be read (" + g.live_why + ")" + (g.include_settings ? " and neither can the settings file (" + swhy + ")" : ""), true);
        return;
    }
    const std::string name = tpreset::unique_name(g.presets, wanted);
    tpreset::Preset p = tpreset::make_preset(name, "", stamp(), g.have_live ? &g.live : nullptr, have_sliders ? &sliders : nullptr, layout, map_build);
    std::string err;
    if (!tpreset::write_preset(tpreset::presets_dir(out_dir(app)), p, &err)) {
        say(app, "The preset could not be written: " + err, true);
        return;
    }
    std::string s = "Saved \"" + p.name + "\" to turbo_output\\" + tpreset::dir_name() + "\\" + p.file.filename().u8string() + ": " + tpreset::summary(p) + ".";
    if (!g.have_live) s += " The tactic part is missing: the live tactic could not be read (" + g.live_why + ").";
    if (g.include_settings && !have_sliders) s += " The settings part is missing: " + swhy + ".";
    if (name != wanted) s += " (the name was taken: saved as \"" + name + "\")";
    say(app, s);
    load_presets_now(app);
    g.sel = -1;
    for (size_t i = 0; i < g.presets.size(); ++i)
        if (g.presets[i].name == p.name) g.sel = int(i);
    g.delete_armed = false;
}

// ------------------------------------------------------------------------------------------------ Load / Undo / Re-apply: the confirmation
const tpreset::Preset* selected() { return g.sel >= 0 && g.sel < int(g.presets.size()) ? &g.presets[size_t(g.sel)] : nullptr; }

void ask(JobKind kind, const std::string& title, std::vector<std::string> lines) {
    g.confirm_kind = kind;
    g.confirm_title = title;
    g.confirm_lines = std::move(lines);
    g.confirm_request = true;
}

void start_load(App& app) {
    const tpreset::Preset* p = selected();
    if (!p) return;
    read_live(app, true);
    if (p->tactic.present && !g.have_live) {
        say(app, "Not loaded: your live tactic cannot be read (" + g.live_why + ")", true);
        return;
    }
    tsheet::Request req;
    std::vector<std::string> notes;
    bool tactic = false;
    std::vector<std::string> lines;
    if (p->tactic.present) {
        const std::string why = tpreset::request_for(p->tactic, g.live, req, notes);
        if (!why.empty()) {
            say(app, "Not loaded: " + why, true);
            return;
        }
        const std::string gate = live_tactic_gate(app, req);
        if (gate.empty()) {
            tactic = true;
            for (const std::string& d : tpreset::diff_lines(p->tactic, g.live)) lines.push_back(d);
        } else if (gate.find("G6 values: nothing changed") == 0) {
            lines.push_back("the tactic already holds the preset's values: nothing to write there");
        } else {
            say(app, "Not loaded: " + gate, true);
            return;
        }
        for (const std::string& n : notes) lines.push_back("note: " + n);
    }
    bool settings = false;
    if (p->settings.present) {
        settings = true;
        int writable = 0;
        std::vector<std::string> skipped;
        std::string why;
        if (!game_settings_plan(app, p->settings.values, writable, skipped, why)) {
            lines.push_back("sliders: cannot be staged now: " + why);
            settings = false;
        } else if (writable == 0) {
            lines.push_back("sliders: none of the preset's " + std::to_string(p->settings.values.size()) + " is verified in game yet: nothing is staged");
            settings = false;
        } else {
            lines.push_back("sliders: " + std::to_string(writable) + " verified slider(s) are staged in turbo_output\\" + gsettings::pending_file() + ", " +
                            std::to_string(skipped.size()) + " not verified (left out); they take effect only after you close FC 27, run turbo\turbo_settings_apply.exe "
                            "--apply and start the game with Gameplay Type CUSTOM");
        }
    }
    if (!tactic && !settings) {
        say(app, "Nothing to load: " + (lines.empty() ? std::string("the preset holds nothing to write") : lines.front()));
        return;
    }
    g.confirm_req = req;
    g.confirm_tactic = tactic;
    g.confirm_card_undo = false;
    g.confirm_settings = settings;
    g.confirm_preset = *p;
    // what the Yes records: the values before, and the values the load asks for
    tpreset::LastLoad l;
    l.preset = p->name;
    l.when = stamp();
    l.state = "pending";
    const BridgeState& st = app.bridge.state();
    l.session = st.session;
    l.load_gen = st.load_gen;
    if (g.have_live) {
        l.team = g.live.at.user;
        l.before = tpreset::block_from_live(g.live);
        l.after = l.before;
        if (tactic) {
            l.after.line_height = req.line_height;
            l.after.build_up = req.build_up;
            l.after.approach = req.approach;
            for (const tsheet::SlotChange& c : req.slots) {
                l.after.slots[size_t(c.slot)].role_id = c.role_id;
                l.after.slots[size_t(c.slot)].focus = c.focus;
            }
        }
    }
    g.confirm_last = l;
    ask(JobKind::Load, "Load tactic preset \"" + p->name + "\"?##tpconfirm", lines);
}

void start_undo(App& app) {
    if (!g.have_last || g.last.state != "loaded") return;
    read_live(app, true);
    std::vector<std::string> lines, notes;
    tsheet::Request req;
    bool tactic = false, card = false;
    if (g.last.before.present) {
        if (!g.have_live) {
            say(app, "Nothing put back: your live tactic cannot be read (" + g.live_why + ")", true);
            return;
        }
        if (live_tactic_undo_ready(app)) {
            // the card's own Undo: every value Turbo wrote in this career load goes back, judged by the write history (no role allow-list: the values
            // are the game's own from before the write)
            tactic = true;
            card = true;
            lines = tpreset::diff_lines(g.last.before, g.live);
            if (lines.empty()) lines.push_back("the values Turbo wrote in this career load (channel_e_undo.json)");
            lines.push_back("note: this is the Tactics card's Undo: it puts back everything Turbo wrote into your live tactic in this career load");
        } else {
            const std::string why = tpreset::undo_request(g.last, g.live, req, notes);
            if (!why.empty()) {
                say(app, "Nothing put back: " + why, true);
                return;
            }
            const std::string gate = live_tactic_gate(app, req);
            if (gate.empty()) {
                tactic = true;
                for (const std::string& d : tpreset::diff_lines(g.last.before, g.live)) lines.push_back(d);
            } else if (gate.find("G6 values: nothing changed") == 0) {
                lines.push_back("the tactic already holds the values from before the load");
            } else {
                say(app, "Nothing put back: " + gate, true);
                return;
            }
            for (const std::string& n : notes) lines.push_back("note: " + n);
        }
    }
    if (g.last.settings_staged) lines.push_back("staged sliders: discarded if turbo_settings_apply.exe has not applied them yet (else only its --undo puts the file back)");
    g.confirm_req = req;
    g.confirm_tactic = tactic;
    g.confirm_card_undo = card;
    g.confirm_settings = g.last.settings_staged;
    g.confirm_last = g.last;
    ask(JobKind::Undo, "Undo the last preset load?##tpconfirm", lines);
}

void start_reapply(App& app) {
    if (!g.have_last || g.last.state != "loaded") return;
    read_live(app, true);
    if (!g.have_live) {
        say(app, "Not re-applied: your live tactic cannot be read (" + g.live_why + ")", true);
        return;
    }
    tsheet::Request req;
    std::vector<std::string> notes;
    const std::string why = tpreset::request_for(g.last.after, g.live, req, notes);
    if (!why.empty()) {
        say(app, "Not re-applied: " + why, true);
        return;
    }
    const std::string gate = live_tactic_gate(app, req);
    if (!gate.empty()) {
        say(app, "Not re-applied: " + gate, true);
        return;
    }
    std::vector<std::string> lines = tpreset::diff_lines(g.last.after, g.live);
    for (const std::string& n : notes) lines.push_back("note: " + n);
    g.confirm_req = req;
    g.confirm_tactic = true;
    g.confirm_card_undo = false;
    g.confirm_settings = false;
    g.confirm_last = g.last;
    ask(JobKind::Reapply, "Re-apply preset \"" + g.last.preset + "\"?##tpconfirm", lines);
}

// The Yes
void confirmed(App& app) {
    const JobKind kind = g.confirm_kind;
    g.job_last = g.confirm_last;
    g.job_preset = g.confirm_preset;
    g.job_settings = g.confirm_settings;
    g.job_tactic = g.confirm_tactic;
    g.job_t0 = app.now;
    if (g.confirm_tactic) {
        std::string why;
        g.job_seq0 = live_tactic_result().seq;
        if (kind == JobKind::Load) save_last(app, g.job_last);  // "pending": the values before the load are on disk before anything is written
        const bool queued = kind == JobKind::Undo && g.confirm_card_undo ? live_tactic_undo(app, why) : live_tactic_submit(app, g.confirm_req, why);
        if (!queued) {
            if (kind == JobKind::Load) {
                g.job_last.state = "failed";
                g.job_last.note = why;
                save_last(app, g.job_last);
            }
            say(app, std::string(kind == JobKind::Load ? "Not loaded: " : kind == JobKind::Undo ? "Nothing put back: " : "Not re-applied: ") + why, true);
            return;
        }
        g.job = kind;
        say(app, "Queued for the game thread ...");
        return;
    }
    g.job = kind;  // no tactic part: finish at once (the next tick)
    g.job_seq0 = live_tactic_result().seq;
}

// The job came back (or there was none): the last-load file and the sliders
void finish(App& app) {
    const JobKind kind = g.job;
    g.job = JobKind::None;
    tpreset::LastLoad l = g.job_last;
    const LiveJobResult r = live_tactic_result();
    const bool fresh = g.job_tactic && r.seq > g.job_seq0;
    // an undo job counts as done when it came back ok (values that no longer hold Turbo's are left alone: nothing to write is fine)
    const bool tactic_ok = !g.job_tactic || (fresh && r.ok && (r.written || (kind == JobKind::Undo && r.undo)));
    const std::string job_line = g.job_tactic ? (fresh ? r.line : std::string("the game thread did not run the job (nothing written)")) : "";
    if (kind == JobKind::Load) {
        if (!tactic_ok) {
            l.state = "failed";
            l.note = job_line;
            save_last(app, l);
            say(app, "Not loaded: " + job_line, true);
            return;
        }
        std::string smsg = "";
        if (g.job_settings) {
            std::string msg;
            int staged = 0;
            std::vector<std::string> skipped;
            const bool ok = game_settings_stage(app, g.job_preset.name, g.job_preset.settings.values, msg, staged, skipped);
            l.settings_staged = ok;
            l.settings_preset = ok ? g.job_preset.name : "";
            l.settings_skipped = skipped;
            smsg = " Sliders: " + msg;
        }
        l.state = "loaded";
        l.when = stamp();
        save_last(app, l);
        say(app, "Loaded \"" + l.preset + "\"" + (g.job_tactic ? ": written into your live tactic and read back" : "") + "." + smsg +
                     " Takes effect at the next match; Edit Tactic may show other values until the game reloads its screen. " + kSafety);
        if (g.job_tactic) app.notify("Preset loaded into your live tactic (read back)");
    } else if (kind == JobKind::Undo) {
        if (!tactic_ok) {
            l.note = "undo: " + job_line;
            save_last(app, l);
            say(app, "Not put back: " + job_line, true);
            return;
        }
        std::string smsg;
        if (g.job_settings) {
            std::string msg;
            game_settings_unstage(app, l.settings_preset, msg);
            smsg = " Sliders: " + msg + ".";
            l.settings_staged = false;
        }
        l.state = "undone";
        l.note = "";
        save_last(app, l);
        say(app, "Put back the values from before the load of \"" + l.preset + "\" (read back)." + smsg);
    } else if (kind == JobKind::Reapply) {
        if (!tactic_ok) {
            l.note = "re-apply: " + job_line;
            save_last(app, l);
            say(app, "Not re-applied: " + job_line, true);
            return;
        }
        l.note = "re-applied " + stamp();
        const BridgeState& st = app.bridge.state();
        l.session = st.session;
        l.load_gen = st.load_gen;
        save_last(app, l);
        say(app, "Re-applied \"" + l.preset + "\" (read back).");
    }
    read_live(app, true);
}

void delete_selected(App& app) {
    const tpreset::Preset* p = selected();
    if (!p) return;
    std::string err;
    const std::string name = p->name;
    if (!tpreset::delete_preset(*p, &err)) {
        say(app, "The preset could not be deleted: " + err, true);
        return;
    }
    say(app, "Deleted \"" + name + "\"");
    load_presets_now(app);
    g.sel = -1;
    g.delete_armed = false;
}

void dismiss_prompt(App& app) {
    if (!g.have_last) return;
    tpreset::LastLoad l = g.last;
    l.state = "dismissed";
    save_last(app, l);
    say(app, "Dismissed: Turbo will not offer to re-apply \"" + l.preset + "\"");
}

// ------------------------------------------------------------------------------------------------ drawing
void draw_confirm(App& app) {
    if (g.confirm_request) {
        ImGui::OpenPopup(g.confirm_title.c_str());
        g.confirm_request = false;
    }
    g_state.confirm_open = false;
    if (ImGui::BeginPopupModal(g.confirm_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        g_state.confirm_open = true;
        g_state.confirm_title = g.confirm_title;
        g_state.confirm_lines = g.confirm_lines;
        ImGui::TextUnformatted(g.confirm_kind == JobKind::Undo ? "Turbo puts these values back:" : "Turbo writes these values:");
        for (const std::string& l : g.confirm_lines) ImGui::BulletText("%s", l.c_str());
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1), "Not proven in a played match yet. The values before go to turbo_output\\%s and channel_e_undo.json.",
                           tpreset::last_load_file());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
        ImGui::TextWrapped("%s", kSafety);
        ImGui::PopStyleColor();
        const char* yes = g.confirm_kind == JobKind::Undo ? "Undo##tpyes" : g.confirm_kind == JobKind::Reapply ? "Re-apply##tpyes" : "Load##tpyes";
        if (ImGui::Button(yes)) {
            ImGui::CloseCurrentPopup();
            confirmed(app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##tpno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_body(App& app) {
    first_look(app);
    read_live(app, false);
    g_state.banner = kBanner;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
    ImGui::TextWrapped("%s", g_state.banner.c_str());
    ImGui::PopStyleColor();

    // the live tactic
    g_state.live_ok = g.have_live;
    if (g.have_live)
        g_state.live_line = "Your club's live tactic (read now): \"" + (g.live.name.empty() ? std::string("?") : g.live.name) + "\", Defensive Approach " +
                            tsheet::approach_text(g.live.approach) + ", Line Height " + std::to_string(g.live.line_height) + ", Build-Up Style " +
                            tsheet::build_up_text(g.live.build_up);
    else
        g_state.live_line = "Your club's live tactic: not read (" + g.live_why + ")";
    ImGui::TextWrapped("%s", g_state.live_line.c_str());

    bool on = live_tactic_opt_in(app);
    g_state.opt_in = on;
    if (checkbox_right("Channel E on (turbo_output\\call_team_sheet_on.txt): needed to read and load the live tactic##tpopt", &on)) {
        live_tactic_set_opt_in(app, on);
        read_live(app, true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off by default. On creates turbo_output\\call_team_sheet_on.txt; call_team_sheet_off.txt or tactics_off.txt switch every write off.");

    // the latest load and the Re-apply prompt
    g_state.busy = g.job != JobKind::None || live_tactic_busy();
    g_state.last_line.clear();
    g_state.reapply.clear();
    if (g.have_last) {
        g_state.last_line = "Last load: \"" + g.last.preset + "\" (" + g.last.state + ", " + g.last.when + ")" + (g.last.note.empty() ? "" : ": " + g.last.note);
        ImGui::TextDisabled("%s", g_state.last_line.c_str());
        if (g.have_live && !g_state.busy) {
            const BridgeState& st = app.bridge.state();
            g_state.reapply = tpreset::reapply_prompt(g.last, g.live, st.session, st.load_gen);
        }
        if (!g_state.reapply.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.3f, 1));
            ImGui::TextWrapped("%s", g_state.reapply.c_str());
            ImGui::PopStyleColor();
            if (ImGui::Button("Re-apply##tpreapply")) start_reapply(app);
            ImGui::SameLine();
            if (ImGui::SmallButton("Dismiss##tpdismiss")) dismiss_prompt(app);
        }
    } else if (!g.last_err.empty()) {
        ImGui::TextDisabled("turbo_output\\%s is not used: %s", tpreset::last_load_file(), g.last_err.c_str());
    }

    // Save current
    ImGui::SeparatorText("Save current");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::InputTextWithHint("##tpname", "Preset name", g.name, sizeof(g.name));
    ImGui::SameLine();
    ImGui::Checkbox("with the Custom sliders block##tpwithsliders", &g.include_settings);
    g_state.include_settings = g.include_settings;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Also stores the Game Settings sliders (read from the settings file the game saved last). Loading stages only the sliders verified in game.");
    g_state.can_save = !trimmed_name().empty();
    ImGui::BeginDisabled(!g_state.can_save);
    ImGui::SameLine();
    if (ImGui::Button("Save current##tpsave")) save_current(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Refresh##tprefresh")) {  // a preset file edited or copied outside the game
        load_presets_now(app);
        load_last_now(app);
        g.status = "The presets folder was read again";
    }

    // the list
    ImGui::SeparatorText("Presets");
    g_state.names.clear();
    g_state.summaries.clear();
    ImGui::BeginChild("##tplist", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.5f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < g.presets.size(); ++i) {
        const tpreset::Preset& p = g.presets[i];
        g_state.names.push_back(p.name);
        g_state.summaries.push_back(tpreset::summary(p));
        const std::string label = p.name + "  -  " + g_state.summaries.back() + "##tpsel" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), g.sel == int(i))) {
            g.sel = int(i);
            g.delete_armed = false;
        }
    }
    if (g.presets.empty()) ImGui::TextDisabled("No preset yet: type a name and press Save current.");
    ImGui::EndChild();
    g_state.selected = g.sel;
    for (const std::string& e : g.errors) ImGui::TextDisabled("Skipped %s", e.c_str());
    g_state.errors = g.errors;

    // what a Load of the selected preset would do right now
    const tpreset::Preset* p = selected();
    g_state.gate.clear();
    g_state.notes.clear();
    if (p && p->tactic.present && g.have_live) {
        tsheet::Request req;
        const std::string rwhy = tpreset::request_for(p->tactic, g.live, req, g_state.notes);
        g_state.gate = rwhy.empty() ? live_tactic_gate(app, req) : rwhy;
        for (const std::string& n : g_state.notes) ImGui::TextDisabled("%s", n.c_str());
        if (!g_state.gate.empty()) ImGui::TextDisabled("Load now: %s", g_state.gate.c_str());
    } else if (p && p->tactic.present) {
        g_state.gate = g.live_why.empty() ? "the live tactic is not read" : g.live_why;
        ImGui::TextDisabled("Load now: %s", g_state.gate.c_str());
    }
    g_state.can_load = p != nullptr && !g_state.busy;
    g_state.can_undo = g.have_last && g.last.state == "loaded" && !g_state.busy;
    ImGui::BeginDisabled(!g_state.can_load);
    if (ImGui::Button("Load##tpload")) start_load(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_state.can_undo);
    if (ImGui::Button("Undo last load##tpundo")) start_undo(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(p == nullptr || g_state.busy);
    if (ImGui::Button((g.delete_armed ? "Really delete?##tpdel" : "Delete##tpdel"))) {
        if (g.delete_armed) delete_selected(app);
        else g.delete_armed = true;
    }
    ImGui::EndDisabled();
    if (g.delete_armed && p) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Keep##tpkeep")) g.delete_armed = false;
    }
    if (!g.status.empty()) ImGui::TextWrapped("%s", g.status.c_str());
    g_state.status = g.status;
    ImGui::TextDisabled("Files: turbo_output\\%s\\<name>.json. The tactic goes through the Tactics card's guarded job (Undo in channel_e_undo.json too); "
                        "reapply_edits.json is never touched.",
                        tpreset::dir_name());
    draw_confirm(app);
}

}  // namespace

const TacticPresetsState& tactic_presets_state() { return g_state; }

void tactic_presets_reset() {
    g = Pv();
    g_state = TacticPresetsState();
}

void draw_tactic_presets(App& app) {
    g_state.drawn = true;
    try {
        draw_body(app);
    } catch (const std::exception& e) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactic presets error: %s", e.what());
    } catch (...) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactic presets error");
    }
}

void tactic_presets_tick(App& app) {
    try {
        if (g.job == JobKind::None) return;
        const LiveJobResult r = live_tactic_result();
        const bool no_tactic = !g.job_tactic;
        const bool came_back = g.job_tactic && !live_tactic_busy() && r.seq > g.job_seq0;
        const bool lost = g.job_tactic && !live_tactic_busy() && r.seq == g.job_seq0 && app.now - g.job_t0 > 15.0;
        if (no_tactic || came_back || lost) finish(app);
    } catch (...) {
    }
}

}  // namespace turbo
