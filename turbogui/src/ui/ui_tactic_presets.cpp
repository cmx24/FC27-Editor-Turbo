// FC 27 LE Turbo GUI - the Tactic presets panel (see ui_tactic_presets.h, core/tactic_presets.h).
//
// Frame-loop rules as in the other views: never throws; the presets folder is read at the first look and after a change; the live tactic is
// read at most once a second (the card's own read); nothing is written by drawing. A Load or Undo writes only through
// live_tactic_submit (the Tactics card's guarded game-thread job, every gate G1..G6) and game_settings_stage / _unstage.
#include "ui_tactic_presets.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>

#include "app.h"
#include "core/game_settings.h"
#include "core/tactic_presets.h"
#include "imgui.h"
#include "nlohmann/json.hpp"
#include "ui_game_settings.h"
#include "ui_live_tactic.h"
#include "ui_shell.h"
#include "ui_tactics_pages.h"
#include "ui_theme.h"

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
    char filter[64] = "";  // 2.13: the select panel's search box
    bool include_settings = true;
    bool include_unverified = true;  // 2.12.1 test build: the table-mapped sliders are staged too (flag saved in the preset)
    bool include_setpieces = true;
    tpreset::SetPieceBlock sp_current;  // turbo_output\tactic_setpieces_current.json (Lua's read-out)
    bool sp_have = false;
    double next_sp = -1.0;
    std::string sp_why;
    bool confirm_setpieces = false, job_setpieces = false;
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

// ------------------------------------------------------------------------------------------------ set pieces (through Lua: features/tactic_setpieces.lua)
// Lua writes turbo_output\tactic_setpieces_current.json when it runs mode "get"; this reads it (never the database itself)
void read_setpieces_current(App& app) {
    g.sp_have = false;
    g.sp_why.clear();
    std::vector<uint8_t> b;
    const fs::path f = out_dir(app) / tpreset::setpieces_current_file();
    if (!gsettings::read_bytes(f, b, 256 * 1024)) {
        g.sp_why = "not read yet: press \"Read set pieces\" (Turbo's Lua side reads them at its next career event)";
        return;
    }
    tpreset::SetPieceBlock sp;
    std::string err;
    if (!tpreset::setpieces_parse_current(std::string(b.begin(), b.end()), sp, &err)) {
        g.sp_why = std::string(tpreset::setpieces_current_file()) + " is not usable: " + err;
        return;
    }
    if (g.have_live && g.live.at.user > 0 && sp.team != g.live.at.user) {
        g.sp_why = "the read-out is of team " + std::to_string(sp.team) + ", your club is team " + std::to_string(g.live.at.user) + ": press \"Read set pieces\" again";
        return;
    }
    g.sp_current = sp;
    g.sp_have = true;
}

bool send_setpieces(App& app, const std::string& mode, const tpreset::SetPieceBlock& want, const std::string& preset, const std::string& label) {
    const nlohmann::json cmd = nlohmann::json::parse(tpreset::setpieces_command_json(mode, want, preset), nullptr, false);
    if (cmd.is_discarded()) return false;
    return app.send(cmd, label);
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
    bool have_sp = false;
    if (g.include_setpieces) {
        read_setpieces_current(app);
        have_sp = g.sp_have;
    }
    if (!g.have_live && !have_sliders && !have_sp) {
        say(app, "Not saved: the live tactic cannot be read (" + g.live_why + ")" + (g.include_settings ? " and neither can the settings file (" + swhy + ")" : ""), true);
        return;
    }
    const std::string name = tpreset::unique_name(g.presets, wanted);
    tpreset::Preset p = tpreset::make_preset(name, "", stamp(), g.have_live ? &g.live : nullptr, have_sliders ? &sliders : nullptr, layout, map_build,
                                             have_sp ? &g.sp_current : nullptr, g.include_unverified);
    std::string err;
    if (!tpreset::write_preset(tpreset::presets_dir(out_dir(app)), p, &err)) {
        say(app, "The preset could not be written: " + err, true);
        return;
    }
    std::string s = "Saved \"" + p.name + "\" to turbo_output\\" + tpreset::dir_name() + "\\" + p.file.filename().u8string() + ": " + tpreset::summary(p) + ".";
    if (!g.have_live) s += " The tactic part is missing: the live tactic could not be read (" + g.live_why + ").";
    if (g.include_settings && !have_sliders) s += " The settings part is missing: " + swhy + ".";
    if (g.include_setpieces && !have_sp) s += " The set-piece part is missing: " + g.sp_why + ".";
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
        int writable = 0, unverified = 0;
        std::vector<std::string> skipped;
        std::string why;
        if (!game_settings_plan(app, p->settings.values, writable, skipped, why, p->settings.include_unverified, &unverified)) {
            lines.push_back("sliders: cannot be staged now: " + why);
            settings = false;
        } else if (writable == 0) {
            lines.push_back("sliders: none of the preset's " + std::to_string(p->settings.values.size()) + " is mapped and allowed: nothing is staged");
            settings = false;
        } else {
            lines.push_back("sliders: " + std::to_string(writable - unverified) + " screen-proven + " + std::to_string(unverified) +
                            " table-mapped (UNVERIFIED: not seen on the EA Custom screen yet) slider(s) are staged in turbo_output\\" + gsettings::pending_file() + ", " +
                            std::to_string(skipped.size()) + " left out; they take effect only after you close FC 27, run turbo\\turbo_settings_apply.exe "
                            "--apply and start the game with Gameplay Type CUSTOM");
        }
    }
    bool setpieces = false;
    if (p->setpieces.present) {
        read_setpieces_current(app);
        if (g.have_live && g.live.at.user > 0 && p->setpieces.team != g.live.at.user) {
            lines.push_back("set pieces: the preset's players belong to team " + std::to_string(p->setpieces.team) + ", your club is team " + std::to_string(g.live.at.user) +
                            ": not loaded");
        } else {
            setpieces = true;
            std::vector<std::string> d = g.sp_have ? tpreset::setpiece_diff(p->setpieces, g.sp_current) : std::vector<std::string>();
            lines.push_back("set pieces: " + std::to_string(p->setpieces.values.size()) + " team sheet column(s) are written through Turbo's Lua side (cm_teamsheets), read back, "
                            "undo journal turbo_output\\tactic_setpieces_undo.json; players not on your sheet refuse the whole write; the running game may show them only "
                            "after a career save and load" +
                            (g.sp_have ? std::string() : std::string(" (current takers not read: press Read set pieces to see the differences)")));
            for (size_t i = 0; i < d.size() && i < 12; ++i) lines.push_back("  " + d[i]);
        }
    }
    if (!tactic && !settings && !setpieces) {
        say(app, "Nothing to load: " + (lines.empty() ? std::string("the preset holds nothing to write") : lines.front()));
        return;
    }
    g.confirm_req = req;
    g.confirm_tactic = tactic;
    g.confirm_card_undo = false;
    g.confirm_settings = settings;
    g.confirm_setpieces = setpieces;
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
    g.confirm_setpieces = g.last.setpieces_sent;
    if (g.last.setpieces_sent)
        lines.push_back("set pieces: the old takers come back from turbo_output\\tactic_setpieces_undo.json (only fields that still hold what Turbo wrote)");
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
    g.confirm_setpieces = false;
    g.confirm_last = g.last;
    ask(JobKind::Reapply, "Re-apply preset \"" + g.last.preset + "\"?##tpconfirm", lines);
}

// The Yes
void confirmed(App& app) {
    const JobKind kind = g.confirm_kind;
    g.job_last = g.confirm_last;
    g.job_preset = g.confirm_preset;
    g.job_settings = g.confirm_settings;
    g.job_setpieces = g.confirm_setpieces;
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
            const bool ok = game_settings_stage(app, g.job_preset.name, g.job_preset.settings.values, msg, staged, skipped, g.job_preset.settings.include_unverified);
            l.settings_staged = ok;
            l.settings_preset = ok ? g.job_preset.name : "";
            l.settings_skipped = skipped;
            smsg = " Sliders: " + msg;
        }
        if (g.job_setpieces) {
            const bool sent = send_setpieces(app, "set", g.job_preset.setpieces, g.job_preset.name, "Tactic preset set pieces: " + g.job_preset.name);
            l.setpieces_sent = sent;
            smsg += sent ? " Set pieces: sent to Turbo's Lua side (it writes them at its next career event and reads them back: see turbo_output\\" +
                               std::string(tpreset::setpieces_result_file()) + ")."
                         : " Set pieces: NOT sent (the command channel refused).";
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
        if (g.job_setpieces) {
            const bool sent = send_setpieces(app, "undo", tpreset::SetPieceBlock(), l.preset, "Tactic preset set pieces: undo");
            smsg += sent ? " Set pieces: undo sent to Turbo's Lua side." : " Set pieces: undo NOT sent (the command channel refused).";
            l.setpieces_sent = !sent;
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

// ---- 2.13 revamp: the page pattern of ui_shell.h ([select | editor] + ONE primary action). Chrome only: every button below calls the same
// functions as before (save_current, start_load, start_undo, start_reapply, delete_selected ...); the ids (##tp*) are unchanged.
bool contains_nocase(const std::string& hay, const char* needle) {
    std::string h = hay, n = needle;
    for (char& c : h) c = char(std::tolower((unsigned char)c));
    for (char& c : n) c = char(std::tolower((unsigned char)c));
    return h.find(n) != std::string::npos;
}

// Everything the page shows that is computed, not drawn: the live tactic line, the latest load, what a Load would do now.
void update_state(App& app) {
    g_state.banner = kBanner;
    g_state.live_ok = g.have_live;
    if (g.have_live)
        g_state.live_line = "Your club's live tactic (read now): \"" + (g.live.name.empty() ? std::string("?") : g.live.name) + "\", Defensive Approach " +
                            tsheet::approach_text(g.live.approach) + ", Line Height " + std::to_string(g.live.line_height) + ", Build-Up Style " +
                            tsheet::build_up_text(g.live.build_up);
    else
        g_state.live_line = "Your club's live tactic: not read (" + g.live_why + ")";
    g_state.opt_in = live_tactic_opt_in(app);
    g_state.include_settings = g.include_settings;
    g_state.include_unverified = g.include_unverified;
    g_state.include_setpieces = g.include_setpieces;
    g_state.busy = g.job != JobKind::None || live_tactic_busy();
    g_state.last_line.clear();
    g_state.reapply.clear();
    if (g.have_last) {
        g_state.last_line = "Last load: \"" + g.last.preset + "\" (" + g.last.state + ", " + g.last.when + ")" + (g.last.note.empty() ? "" : ": " + g.last.note);
        if (g.have_live && !g_state.busy) {
            const BridgeState& st = app.bridge.state();
            g_state.reapply = tpreset::reapply_prompt(g.last, g.live, st.session, st.load_gen);
        }
    }
    g_state.names.clear();
    g_state.summaries.clear();
    for (const tpreset::Preset& pr : g.presets) {
        g_state.names.push_back(pr.name);
        g_state.summaries.push_back(tpreset::summary(pr));
    }
    g_state.selected = g.sel;
    g_state.errors = g.errors;
    g_state.status = g.status;
    const tpreset::Preset* p = selected();
    g_state.gate.clear();
    g_state.notes.clear();
    if (p && p->tactic.present && g.have_live) {
        tsheet::Request req;
        const std::string rwhy = tpreset::request_for(p->tactic, g.live, req, g_state.notes);
        g_state.gate = rwhy.empty() ? live_tactic_gate(app, req) : rwhy;
    } else if (p && p->tactic.present) {
        g_state.gate = g.live_why.empty() ? "the live tactic is not read" : g.live_why;
    }
    g_state.can_save = !trimmed_name().empty();
    g_state.can_load = p != nullptr && !g_state.busy;
    g_state.can_undo = g.have_last && g.last.state == "loaded" && !g_state.busy;
}

// "Undo last load" on the right of the title line
void undo_button_right(App& app, const char* label_with_id, const char* shown) {
    const float bw = ImGui::CalcTextSize(shown).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x - bw;
    if (room > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
    ImGui::BeginDisabled(!g_state.can_undo);
    if (ImGui::Button(label_with_id)) start_undo(app);
    ImGui::EndDisabled();
}

void presets_select_panel(App& app, void*) {
    ImGui::TextUnformatted("Presets");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##tprefresh")) {  // a preset file edited or copied outside the game
        load_presets_now(app);
        load_last_now(app);
        g.status = "The presets folder was read again";
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##tpfilter", "Search", g.filter, sizeof(g.filter));
    for (size_t i = 0; i < g.presets.size() && i < g_state.summaries.size(); ++i) {
        const tpreset::Preset& p = g.presets[i];
        if (g.filter[0] && !contains_nocase(p.name, g.filter)) continue;
        const std::string label = p.name + "  -  " + g_state.summaries[i] + "##tpsel" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), g.sel == int(i), 0, ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            g.sel = int(i);
            g.delete_armed = false;
        }
    }
    if (g.presets.empty()) theme::muted_text("No preset yet: save your live tactic from the editor.");
    for (const std::string& e : g.errors) theme::muted_text("Skipped %s", e.c_str());
}

void presets_editor_panel(App& app, void*) {
    const tpreset::Preset* p = selected();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(p ? p->name.c_str() : "Tactic presets");
    ImGui::PopFont();
    undo_button_right(app, "Undo last load##tpundo", "Undo last load");
    if (p && g.sel >= 0 && size_t(g.sel) < g_state.summaries.size()) theme::muted_text("%s", g_state.summaries[size_t(g.sel)].c_str());
    theme::muted_text("The game applies the values itself. The effect in a played match is not measured yet; limits are listed below.");
    if (!g.status.empty()) ImGui::TextWrapped("%s", g.status.c_str());
    if (g_state.last_line.empty() && !g.last_err.empty())
        theme::muted_text("turbo_output\\%s is not used: %s", tpreset::last_load_file(), g.last_err.c_str());
    if (!g_state.reapply.empty()) {
        theme::warn_text("%s", g_state.reapply.c_str());
        if (ImGui::Button("Re-apply##tpreapply")) start_reapply(app);
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss##tpdismiss")) dismiss_prompt(app);
    }

    if (ImGui::CollapsingHeader("Live now##tpsec_live", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("%s", g_state.live_line.c_str());
        bool on = g_state.opt_in;
        if (checkbox_right("Let Turbo change my tactic###tpopt", &on)) {
            live_tactic_set_opt_in(app, on);
            read_live(app, true);
            g_state.opt_in = on;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off by default. Turn it on so Turbo can read your club's tactic and apply presets to it.");
        if (ImGui::SmallButton("Read live##tpreadlive")) read_live(app, true);
    }
    if (ImGui::CollapsingHeader("Selected preset##tpsec_sel", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!p) theme::muted_text("Pick a preset on the left.");
        for (const std::string& n : g_state.notes) theme::muted_text("%s", n.c_str());
        if (p && !g_state.gate.empty()) theme::muted_text("Load now: %s", g_state.gate.c_str());
        if (!g_state.last_line.empty()) theme::muted_text("%s", g_state.last_line.c_str());
    }
    if (ImGui::CollapsingHeader("Save current as a new preset##tpsec_save", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
        ImGui::InputTextWithHint("##tpname", "Preset name", g.name, sizeof(g.name));
        ImGui::SameLine();
        ImGui::Checkbox("Include gameplay sliders###tpwithsliders", &g.include_settings);
        g_state.include_settings = g.include_settings;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Also stores the Game Settings sliders (read from the settings file the game saved last). Loading stages the 3 screen-proven sliders, "
                              "and the other table-mapped ones when 'Extra sliders' is ticked.");
        g_state.can_save = !trimmed_name().empty();
        ImGui::BeginDisabled(!g_state.can_save);
        ImGui::SameLine();
        if (ImGui::Button("Save current##tpsave")) save_current(app);
        ImGui::EndDisabled();
        ImGui::Checkbox("Extra sliders (experimental)###tpunver", &g.include_unverified);
        g_state.include_unverified = g.include_unverified;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Also install about 120 more sliders that the game's own table lists but the EA Custom screen has not shown yet. Experimental. The choice is saved in the "
                              "preset. Untick for the 3 screen-proven sliders only.");
        ImGui::SameLine();
        ImGui::Checkbox("with the set pieces##tpwithsp", &g.include_setpieces);
        g_state.include_setpieces = g.include_setpieces;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Captain, penalty / free-kick / corner / throw-in takers, corner supports and pre-set substitutions of your club's team sheet. Read through "
                              "Turbo's Lua side: press Read set pieces first. Loading writes the database columns (guarded, read back, undo); the running game may show "
                              "them only after a career save and load.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Read set pieces##tpreadsp")) {
            send_setpieces(app, "get", tpreset::SetPieceBlock(), "", "Tactic preset: read set pieces");
            g.status = "Asked Turbo's Lua side to read the set pieces (it runs at the next career event); press Save current afterwards";
        }
        if (app.now >= g.next_sp) {  // the small file is read at most once a second
            g.next_sp = app.now + 1.0;
            read_setpieces_current(app);
        }
        g_state.setpieces_line = g.sp_have ? "Set pieces read: team " + std::to_string(g.sp_current.team) + ", " + std::to_string(g.sp_current.values.size()) + " fields"
                                           : "Set pieces: " + g.sp_why;
        theme::muted_text("%s", g_state.setpieces_line.c_str());
    }
    if (ImGui::CollapsingHeader("Limits and safety##tpsec_limits")) {
        theme::warn_text("Limits");
        ImGui::TextWrapped("%s", g_state.banner.c_str());
        ImGui::TextWrapped("%s", kSafety);
    }
    if (ImGui::CollapsingHeader("More##tpmore")) {
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
        theme::muted_text("Files: turbo_output\\%s\\<name>.json. The tactic goes through the Tactics card's guarded job (Undo in channel_e_undo.json too); "
                          "reapply_edits.json is never touched.",
                          tpreset::dir_name());
    }
}

void draw_body(App& app) {
    first_look(app);
    read_live(app, false);
    update_state(app);
    shell::page::Primary primary;
    primary.label = "Apply preset##tpload";
    primary.enabled = g_state.can_load;
    shell::page::frame(app, presets_select_panel, presets_editor_panel, nullptr, &primary);
    if (primary.clicked) start_load(app);
    draw_confirm(app);
}

// ---- Gameplay presets: the presets that carry a Custom sliders block (community slider presets), applied with the same start_load
GameplayPageState g_gp;

std::vector<int> gameplay_indices() {
    std::vector<int> v;
    for (size_t i = 0; i < g.presets.size(); ++i)
        if (g.presets[i].settings.present) v.push_back(int(i));
    return v;
}

void gameplay_select_panel(App& app, void*) {
    ImGui::TextUnformatted("Slider presets");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh##gprefresh")) {
        load_presets_now(app);
        g.status = "The presets folder was read again";
    }
    const std::vector<int> idx = gameplay_indices();
    g_gp.names.clear();
    g_gp.selected = -1;
    for (size_t k = 0; k < idx.size(); ++k) {
        const tpreset::Preset& p = g.presets[size_t(idx[k])];
        g_gp.names.push_back(p.name);
        if (g.sel == idx[k]) g_gp.selected = int(k);
        const std::string label = p.name + "  -  " + std::to_string(p.settings.values.size()) + " sliders##gpsel" + std::to_string(k);
        if (ImGui::Selectable(label.c_str(), g.sel == idx[k], 0, ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            g.sel = idx[k];
            g.delete_armed = false;
        }
    }
    if (idx.empty()) theme::muted_text("No slider preset yet. Save one on Tactic presets with the Custom sliders block ticked.");
}

void gameplay_editor_panel(App& app, void*) {
    const tpreset::Preset* p = selected();
    const bool have = p != nullptr && p->settings.present;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(have ? p->name.c_str() : "Gameplay presets");
    ImGui::PopFont();
    undo_button_right(app, "Undo last load##gpundo", "Undo last load");
    g_gp.note = "Community slider presets. Write + EA screen read-back proven, on-pitch effect not measured.";
    theme::muted_text("%s", g_gp.note.c_str());
    g_gp.read_back_ok = false;
    if (have) {
        ImGui::TextWrapped("%zu sliders%s. Applying stages them; they are written to the game's settings file when FC 27 is closed "
                           "(turbo_settings_apply.exe), then show on the EA Custom screen.",
                           p->settings.values.size(), p->settings.include_unverified ? ", including sliders the EA screen has not shown yet" : "");
        g_gp.read_back_ok = g.have_last && g.last.preset == p->name && g.last.state == "loaded";
        if (g_gp.read_back_ok) theme::ok_text("Prepared (%s): close the game to install", g.last.when.c_str());
        else if (g.have_last && g.last.preset == p->name) theme::muted_text("Last apply: %s", g.last.state.c_str());
    } else {
        theme::muted_text("Pick a slider preset on the left.");
    }
    if (!g.status.empty()) ImGui::TextWrapped("%s", g.status.c_str());
    if (!g_state.reapply.empty()) {
        theme::warn_text("%s", g_state.reapply.c_str());
        if (ImGui::Button("Re-apply##gpreapply")) start_reapply(app);
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss##gpdismiss")) dismiss_prompt(app);
    }
    // the game's settings file and its own slider presets (was the Game settings tab): the Advanced view only
    g_gp.advanced_section = false;
    if (app.prefs.advanced_mode && ImGui::CollapsingHeader("Game settings file and slider values (Advanced)##gpadv")) {
        g_gp.advanced_section = true;
        draw_game_settings(app);
    }
}

void draw_gameplay_body(App& app) {
    first_look(app);
    read_live(app, false);
    update_state(app);
    const tpreset::Preset* p = selected();
    g_gp.can_apply = p != nullptr && p->settings.present && !g_state.busy;
    g_gp.can_undo = g_state.can_undo;
    shell::page::Primary primary;
    primary.label = "Apply preset##gpload";
    primary.enabled = g_gp.can_apply;
    shell::page::frame(app, gameplay_select_panel, gameplay_editor_panel, nullptr, &primary);
    if (primary.clicked) start_load(app);
    draw_confirm(app);
}

}  // namespace

const TacticPresetsState& tactic_presets_state() { return g_state; }
const GameplayPageState& gameplay_page_state() { return g_gp; }

void tactic_presets_reset() {
    g = Pv();
    g_state = TacticPresetsState();
    g_gp = GameplayPageState();
}

void draw_tactic_presets(App& app) {
    g_state.drawn = true;
    try {
        draw_body(app);
    } catch (const std::exception& e) {
        theme::err_text("Tactic presets error: %s", e.what());
    } catch (...) {
        theme::err_text("Tactic presets error");
    }
}

void draw_gameplay_presets(App& app) {
    g_gp.drawn = true;
    try {
        draw_gameplay_body(app);
    } catch (const std::exception& e) {
        theme::err_text("Gameplay presets error: %s", e.what());
    } catch (...) {
        theme::err_text("Gameplay presets error");
    }
}

// The two pages of the Tactics & Gameplay area that live here (ui_shell.h). Registered at static init; the flag lets a test see it
// after another test cleared the registry.
TURBO_REGISTER_PAGE(shell::Page::TacticPresets, draw_tactic_presets);
TURBO_REGISTER_PAGE(shell::Page::GameplayPresets, draw_gameplay_presets);
static const bool g_pages_registered_at_start =
    shell::registered_page(shell::Page::TacticPresets) == draw_tactic_presets && shell::registered_page(shell::Page::GameplayPresets) == draw_gameplay_presets;
bool tactic_pages_registered_at_start() { return g_pages_registered_at_start; }

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
