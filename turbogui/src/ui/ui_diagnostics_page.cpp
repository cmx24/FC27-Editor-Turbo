// FC 27 LE Turbo GUI - Advanced > Diagnostics & logs and Advanced > Data export (see ui_diagnostics_page.h).
//
// The page is the Settings page's old "Health", "Technical details", "Game hooks" and "Log" parts in one place, with the switch
// files of turbo_output added (the files that turn a feature off or on). Every fact on it is gathered once a second at most into
// a snapshot and drawn from there: no file is read, no folder listed, no hook report built in the frames in between. That is the
// gui/perf rule (Cassio's cursor lag): the Status page it replaces used to ask for the hook report and the three catalog switch files
// in every frame.
//
// Developer words (hooks, Lua, file names, paths) are on purpose here: this is the troubleshooting page of the Advanced area.
#include "ui_diagnostics_page.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "app.h"
#include "core/callnames.h"
#include "core/player_capture.h"
#include "core/user_text.h"
#include "file_picker.h"
#include "imgui.h"
#include "ui_career_page.h"
#include "ui_faces.h"
#include "ui_sections.h"
#include "ui_shell.h"
#include "ui_theme.h"

namespace turbo {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- the snapshot
enum Tone : unsigned char { kPlain = 0, kGood, kWarn, kMuted };
struct Line {
    std::string text;
    Tone tone = kPlain;
};

struct DiagSnapshot {
    double at = -1.0;                // App::now when it was gathered (< 0: never)
    fs::path root;                   // the Live Editor folder the snapshot is about (turbo_output is under it)
    std::vector<Line> link;          // the Game link card
    std::vector<Line> hooks;         // signatures, hooks, dispatcher, game calls
    std::vector<Line> details;       // connection details and the service lines
    std::vector<DiagSwitch> switches;
    bool switch_dir_unreadable = false;
    bool catalog_stale = false;
};
DiagSnapshot g_snap;
DiagnosticsState g_state;

std::string sfmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

bool ends_with(const std::string& s, const char* suffix) {
    const std::string x = suffix;
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool starts_with(const std::string& s, const char* prefix) {
    const std::string x = prefix;
    return s.size() >= x.size() && s.compare(0, x.size(), x) == 0;
}

// ---------------------------------------------------------------- switch files: plain words
struct SwitchInfo {
    const char* file;
    const char* what;
};
// Sources: the files named in core/ and win/ (each "kill switch" / "opt-in" comment). "_off" = the file turns the feature off,
// "_on" = the file switches an opt-in feature on.
const SwitchInfo kSwitchInfo[] = {
    {"game_hooks_off.txt", "Turns off every game hook: Turbo changes nothing inside the game's own code"},
    {"lua_trigger_off.txt", "No prompt Lua commands: Turbo waits for the game's own career events"},
    {"signature_adapt_off.txt", "Does not adapt the hook table to a game build Turbo does not know"},
    {"reapply_off.txt", "Kept edits are not put back when a career loads"},
    {"role_reapply_off.txt", "Squad roles are not put back when a career loads"},
    {"plan_sync_off.txt", "Skill edits are not copied into the development plan"},
    {"overall_follow_off.txt", "The overall no longer follows skill edits"},
    {"scouts_off.txt", "Scout changes are refused"},
    {"live_standings_write_off.txt", "League table edits are refused (Undo as well)"},
    {"kits3d_on.txt", "Opt-in: 3D kits on the pitch are allowed"},
    {"kits3d_off.txt", "3D kits on the pitch are switched off at once"},
    {"faces3d_on.txt", "Opt-in: the 3D hair and beard hook is installed at game start"},
    {"faces3d_off.txt", "The 3D hair and beard hook is switched off"},
    {"lua_loader_on.txt", "Diagnostic: one log line for every script the game loads"},
    {"attrib_ebx_off.txt", "Game data patches (tuning tables) are not delivered"},
    {"tuning_off.txt", "Every gameplay tuning write is refused"},
    {"tuning_live_off.txt", "Gameplay tuning writes during a match are refused"},
    {"tuning_profile_off.txt", "The gameplay tuning profile's automatic writes are off"},
    {"tactics_off.txt", "Every tactics write is refused (and the effect layers of the pitch)"},
    {"tactics_gamevar_off.txt", "The Tactics tab's game-variable rows are not written"},
    {"tactics_db_off.txt", "Tactics rows of the career database are not written"},
    {"call_team_sheet_on.txt", "Opt-in: Turbo may read and change your club's live tactic"},
    {"call_team_sheet_off.txt", "Your club's live tactic is not written"},
    {"channel_e_db_off.txt", "The database row of your live tactic is left out"},
    {"bench_keep_on.txt", "Opt-in: your bench order is kept after Team Management"},
    {"bench_keep_off.txt", "Keeping your bench order is refused"},
    {"callname_voice_off.txt", "Callname voice swaps are off"},
    {"callnames_lang_detect_off.txt", "The commentary language is not detected from the game"},
    {"team_names_hook_off.txt", "Live club names in matches are off"},
    {"asset_catalog_off.txt", "The game asset catalog is not loaded"},
    {"asset_hidden_off.txt", "Hidden pictures and ids are not offered"},
    {"edit_unlock_hook_off.txt", "The unlock of the game's own edit screens is off"},
    {"player_capture_off.txt", "Pictures from the game's 3D model are off"},
    {"create_player_typed_id_off.txt", "Creating a player with a typed ID is refused"},
};

}  // namespace

std::string diagnostics_switch_what(const std::string& file) {
    for (const SwitchInfo& i : kSwitchInfo)
        if (file == i.file) return i.what;
    if (starts_with(file, "hook_") && ends_with(file, "_off.txt") && file.size() > 13)
        return "Turns off the game hook \"" + file.substr(5, file.size() - 5 - 8) + "\" (read when the game starts)";
    if (starts_with(file, "call_") && ends_with(file, "_off.txt") && file.size() > 13)
        return "Turns off the game call \"" + file.substr(5, file.size() - 5 - 8) + "\"";
    if (ends_with(file, "_log_on.txt")) return "Diagnostic: writes extra lines to a log";
    return std::string();
}

namespace {

// *_off.txt and *_on.txt in turbo_output (one non-recursive listing; the caller runs it at most once a second)
std::vector<DiagSwitch> list_switch_files(const fs::path& out, bool& unreadable) {
    std::vector<DiagSwitch> v;
    unreadable = false;
    std::error_code ec;
    fs::directory_iterator it(out, ec);
    if (ec) {
        unreadable = true;
        return v;
    }
    for (const fs::directory_iterator end{}; it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        const bool off = ends_with(name, "_off.txt"), on = ends_with(name, "_on.txt");
        if (!off && !on) continue;
        DiagSwitch s;
        s.file = name;
        s.what = diagnostics_switch_what(name);
        s.opt_in = on;
        v.push_back(std::move(s));
        if (v.size() >= 300) break;
    }
    std::sort(v.begin(), v.end(), [](const DiagSwitch& a, const DiagSwitch& b) { return a.file < b.file; });
    return v;
}

// ---------------------------------------------------------------- gathering
void gather_connection(App& app, std::vector<Line>& d) {
    const auto& st = app.bridge.state();
    d.push_back({"Live Editor folder: " + app.bridge.root().string(), kPlain});
    d.push_back({"Bridge files: " + app.bridge.dir().string(), kPlain});
    d.push_back({"Database meta: " + (app.bridge.meta_loaded() ? std::to_string(app.bridge.meta().tables.size()) + " tables"
                                                                : (app.bridge.meta_error().empty() ? std::string("not received") : app.bridge.meta_error())),
                 kPlain});
    d.push_back({std::string("Lua state: ") + (st.loaded ? "received" : "not received"), kPlain});
    if (st.loaded) {
        d.push_back({sfmt("  LE %s | session %s | update #%lld", st.le_version.c_str(), st.session.c_str(), static_cast<long long>(st.seq)), kPlain});
        d.push_back({sfmt("  DB service %s | career loaded: %s | your club: %s (%lld)", hex_addr(st.db_service).c_str(), st.in_cm ? "yes" : "no",
                          app.model.team_name(st.user_team).c_str(), static_cast<long long>(st.user_team)),
                     kPlain});
        if (st.date.valid()) d.push_back({sfmt("  In-game date: %04d-%02d-%02d", st.date.year, st.date.month, st.date.day), kPlain});
        if (!st.unavailable.empty()) {
            d.push_back({sfmt("  Not possible with this Live Editor build (%zu):", st.unavailable.size()), kWarn});
            for (const auto& u : st.unavailable) d.push_back({"    " + u.first + ": " + u.second, kPlain});
        }
    }
    d.push_back({"Database: " + (app.db.ready() ? std::to_string(app.db.table_names().size()) + " tables found"
                                                 : (app.db_error.empty() ? std::string("not connected") : app.db_error)),
                 kPlain});
    if (app.model.built()) d.push_back({"Player names from: " + app.model.name_source(), kPlain});
    if (app.mailbox)
        d.push_back({sfmt("Command channel: %s | Lua %s", hex_addr(app.mailbox->addr()).c_str(),
                          app.lua_alive() ? "answering" : "not seen yet (needs a career-mode event)"),
                     kPlain});
    else
        d.push_back({"Command channel: unavailable", kPlain});
    if (app.voice_available()) d.push_back({app.voice_status_line(), kPlain});
    else d.push_back({app.voice_status_line(), kMuted});
    if (!app.voice_error.empty()) d.push_back({"  " + app.voice_error, kWarn});
    if (app.team_names_live()) d.push_back({app.team_names_status_line(), kPlain});
    else d.push_back({app.team_names_status_line(), kMuted});
    if (!app.team_names_error.empty()) d.push_back({"  " + app.team_names_error, kWarn});
}

void gather_hooks(App& app, const HookReport& r, bool have, std::vector<Line>& h, size_t& rows) {
    rows = 0;
    if (!have) {
        h.push_back({"The game hooks are not part of this build.", kMuted});
        return;
    }
    h.push_back({sfmt("Game build: %s | signature table: %s | hooks: %s", r.build.empty() ? "unknown" : r.build.c_str(),
                      r.table_source.empty() ? "none" : r.table_source.c_str(), r.enabled ? "allowed" : "off"),
                 kPlain});
    if (!r.note.empty()) h.push_back({"  " + r.note, kPlain});
    int found = 0;
    for (const auto& s : r.signatures)
        if (s.state == SigState::Found) ++found;
    h.push_back({sfmt("Signatures: %d of %zu found", found, r.signatures.size()), kPlain});
    for (const auto& s : r.signatures) {
        if (s.state == SigState::Found) h.push_back({sfmt("  %s: found at 0x%llX", s.name.c_str(), static_cast<unsigned long long>(s.address)), kPlain});
        else h.push_back({sfmt("  %s: %s (%s)", s.name.c_str(), sig_state_name(s.state), s.error.c_str()), kWarn});
        ++rows;
    }
    h.push_back({sfmt("Hooks active: %zu", r.hooks.size()), kPlain});
    for (const auto& k : r.hooks) {
        h.push_back({sfmt("  %s: %s at 0x%llX | calls %lld | errors %lld%s%s", k.name.c_str(),
                          k.killed ? "SWITCHED OFF (hook_<name>_off.txt)" : k.active ? "active" : "not active",
                          static_cast<unsigned long long>(k.target), k.calls, k.errors, k.note.empty() ? "" : " | ", k.note.c_str()),
                     k.killed ? kWarn : kPlain});
        ++rows;
    }
    h.push_back({sfmt("Game-thread dispatcher: %s | ticks %lld | Lua pumps %lld | jobs run %lld | failed %lld | dropped %lld | queued %zu",
                      r.dispatcher_hooked ? "prompt (game_tick hook)" : "on career-mode events (Lua pump)", r.dispatcher_ticks, r.dispatcher_pumps,
                      r.dispatcher_ran, r.dispatcher_failed, r.dispatcher_dropped, r.queued),
                 kPlain});
    h.push_back({sfmt("  threads: tick %lu | Lua pump %lu | last drain %lu", static_cast<unsigned long>(r.tick_thread_id),
                      static_cast<unsigned long>(r.pump_thread_id), static_cast<unsigned long>(r.game_thread_id)),
                 kMuted});
    h.push_back({sfmt("Prompt Lua commands (synthetic career event): %s | sent %lld | Lua pumped %lld",
                      r.lua_trigger.empty() ? "not available" : r.lua_trigger.c_str(), r.lua_triggers, r.lua_trigger_pumps),
                 kPlain});
    if (app.capture) {
        const capture::Status cs = app.capture->status();
        h.push_back({sfmt("3D-model capture: %s | %s | game captures seen %d (descriptor %s) | Turbo renders %d ok, %d failed%s%s",
                          cs.installed ? "installed" : "off", cs.busy ? ("busy: " + cs.busy_label).c_str() : cs.reason.c_str(), cs.seen,
                          cs.learned ? "learned" : "not learned", cs.done, cs.failed, cs.last_format.empty() ? "" : " | last picture ",
                          cs.last_format.c_str()),
                     kPlain});
        h.push_back({sfmt("  heads per request: %d | %s", cs.max_batch, faces3d_status().c_str()), kMuted});
    }
    if (!r.calls.empty()) {
        h.push_back({"Game calls (job offers, live standings, callnames):", kPlain});
        for (const auto& c : r.calls) h.push_back({"  " + c, kPlain});
    }
    if (app.callnames.refreshed) {
        h.push_back({"  " + app.callnames.log_line(), kPlain});
        const CallnamePopulation pop = callname_population(app.callnames.bank, app.callnames.masters, app.callnames.index);
        h.push_back({sfmt("  callnames %s: %s%s", app.callnames.lang.c_str(), callname_population_line(pop).c_str(),
                          app.callnames.index.built ? "" : " (open the callname page for the database counts)"),
                     kPlain});
    }
    if (app.commentary_audio) {
        if (app.callnames.refreshed && app.callnames.spoken.verified)
            h.push_back({sfmt("  spoken callnames (%s): %s", app.callnames.lang.c_str(), app.callnames.spoken.source.c_str()), kPlain});
        else if (!app.spoken_watch_line().empty())
            h.push_back({sfmt("  spoken callnames (%s): %s | probes %lld", app.callnames.lang.empty() ? "no language" : app.callnames.lang.c_str(),
                              app.spoken_watch_line().c_str(), static_cast<long long>(app.spoken_watch.probes())),
                         kPlain});
    }
}

void gather(App& app) {
    const fs::path out = app.turbo_output();
    DiagSnapshot s;
    s.at = app.now;
    s.root = app.bridge.root();

    // the game-hook report: one per snapshot
    HookReport r;
    const bool have = static_cast<bool>(app.hook_report);
    if (have) r = app.hook_report();

    // ---- Game link
    const auto& st = app.bridge.state();
    if (app.db.ready()) {
        g_state.database_line = sfmt("Connected to the game's database (%zu tables)", app.db.table_names().size());
        s.link.push_back({g_state.database_line, kGood});
    } else {
        g_state.database_line = "Waiting for your career: " + (app.db_error.empty() ? std::string("enter a career, or wait a moment") : app.db_error);
        s.link.push_back({g_state.database_line, kWarn});
    }
    {
        std::string line;
        bool good = true;
        if (have) {
            int found = 0, active = 0, killed = 0;
            for (const auto& sg : r.signatures)
                if (sg.state == SigState::Found) ++found;
            for (const auto& k : r.hooks) {
                if (k.active) ++active;
                if (k.killed) ++killed;
            }
            const bool dispatcher = r.dispatcher_hooked || r.dispatcher_pumps > 0 || r.dispatcher_ticks > 0;
            line = usertext::health_line(found, static_cast<int>(r.signatures.size()), active, killed, dispatcher, app.lua_alive());
            good = line.rfind("Everything", 0) == 0;
        } else {
            line = app.lua_alive() ? "Live Editor's Lua side is answering; the game hooks are not part of this build"
                                   : "Live Editor's Lua side is not answering yet (it needs a career-mode event)";
            good = app.lua_alive();
        }
        g_state.health_good = good;
        g_state.health_line = line;
        s.link.push_back({line, good ? kGood : kWarn});
        if (!st.unavailable.empty())
            s.link.push_back({sfmt("%zu features are not possible with this Live Editor build (listed under Connection details)", st.unavailable.size()), kWarn});
    }
    {
        const auto cat = app.asset_catalog();
        const assets::State cs = cat->state();
        const std::string line = "Game asset catalog: " + (app.asset_loading() && cs == assets::State::NotLoaded ? std::string("loading...") : cat->status_line());
        g_state.catalog_line = line;
        s.link.push_back({line, cs == assets::State::Current ? kGood : (cs == assets::State::Stale || cs == assets::State::Error) ? kWarn : kMuted});
        s.catalog_stale = cs == assets::State::Stale;
        if (s.catalog_stale) s.link.push_back({"  Hidden ids cannot be written until the catalog is rebuilt for this game build.", kWarn});
        std::error_code ec;
        s.link.push_back({std::string("  Script gate file: ") + (fs::exists(assets::lua_gate_path(out), ec) ? "written" : "none"), kMuted});
    }

    gather_hooks(app, r, have, s.hooks, g_state.hook_rows);
    gather_connection(app, s.details);
    s.switches = list_switch_files(out, s.switch_dir_unreadable);

    g_state.switches = s.switches;
    g_state.refreshed_at = app.now;
    ++g_state.refreshes;
    g_snap = std::move(s);
}

// At most one snapshot per second of App::now (and at once when the folder changed or the clock went back)
void refresh(App& app) {
    if (g_snap.at >= 0.0 && app.now >= g_snap.at && app.now - g_snap.at < 1.0 && g_snap.root == app.bridge.root()) return;
    gather(app);
}

// ---------------------------------------------------------------- drawing
void draw_line(const Line& l) {
    const char* t = l.text.c_str();
    switch (l.tone) {
        case kGood: theme::ok_text("%s", t); break;
        case kWarn: theme::warn_text("%s", t); break;
        case kMuted: theme::muted_text("%s", t); break;
        default: ImGui::TextUnformatted(t); break;
    }
}

// A bordered box with a horizontal scroll bar; only the lines in view are laid out
void draw_lines_box(const char* id, const std::vector<Line>& v, float height_px) {
    ImGui::BeginChild(id, ImVec2(0, S(height_px)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(v.size()));
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) draw_line(v[static_cast<size_t>(i)]);
    ImGui::EndChild();
}

void card_game_link(App& app) {
    for (const Line& l : g_snap.link) draw_line(l);
    ImGui::BeginDisabled(app.asset_loading());
    if (ImGui::SmallButton("Reload catalog##diagasset")) app.asset_reload("reload button");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open catalog folder##diagasset")) {
        std::error_code ec;
        const fs::path dir = assets::catalog_dir(app.turbo_output());
        fs::create_directories(dir, ec);
        if (!open_in_explorer(dir)) app.notify("Explorer did not start.", true);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy rebuild command##diagasset")) {
        ImGui::SetClipboardText(assets::rebuild_command().c_str());
        app.notify("Copied: " + assets::rebuild_command() + " (" + assets::rebuild_where() + ")");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n(%s; read-only on the game)", assets::rebuild_command().c_str(), assets::rebuild_where().c_str());
    if (!app.asset_load_note.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Last load: %s", app.asset_load_note.c_str());
    }
}

void card_switches(App& app) {
    theme::muted_text("A switch is an empty file in the Turbo output folder. Restart the game after changing one: most are read at start-up.");
    if (g_snap.switch_dir_unreadable) {
        theme::warn_text("The Turbo output folder cannot be read.");
    } else if (g_snap.switches.empty()) {
        theme::ok_text("No switch file is set: everything runs as normal.");
    } else {
        // plain rows (no ImGui table: the table registry of src/ui is for resizable tables): state, file, what it does
        const float x_file = S(52.0f), x_what = S(330.0f);
        theme::muted_text("Now");
        ImGui::SameLine(x_file);
        theme::muted_text("Switch file");
        ImGui::SameLine(x_what);
        theme::muted_text("What it does");
        for (const DiagSwitch& sw : g_snap.switches) {
            if (sw.opt_in) theme::ok_text("on");
            else theme::warn_text("off");
            ImGui::SameLine(x_file);
            ImGui::TextUnformatted(sw.file.c_str());
            ImGui::SameLine(x_what);
            if (sw.what.empty()) theme::muted_text("No description for this file");
            else ImGui::TextUnformatted(sw.what.c_str());
        }
    }
    if (ImGui::SmallButton("Open the folder##diagswitch")) {
        std::error_code ec;
        const fs::path dir = app.turbo_output();
        if (!fs::is_directory(dir, ec)) app.notify("The Turbo output folder does not exist yet.", true);
        else if (!open_in_explorer(dir)) app.notify("Explorer did not start.", true);
    }
}

void card_log(App& app) {
    g_state.log_lines = app.log_lines.size();
    ImGui::AlignTextToFramePadding();
    theme::muted_text("%zu lines (the last 300 are kept)", app.log_lines.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy log##diaglog")) {
        std::string all;
        for (const std::string& l : app.log_lines) {
            all += l;
            all += '\n';
        }
        ImGui::SetClipboardText(all.c_str());
        app.notify(sfmt("Log copied (%zu lines)", app.log_lines.size()));
    }
    ImGui::BeginChild("##diag_log", ImVec2(0, S(260.0f)), ImGuiChildFlags_Borders);
    ImGuiListClipper clip;  // only the lines in view are laid out
    clip.Begin(static_cast<int>(app.log_lines.size()));
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ImGui::TextUnformatted(app.log_lines[static_cast<size_t>(i)].c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void editor_diagnostics(App& app, void*) {
    refresh(app);
    ImGui::TextUnformatted("Diagnostics & logs");
    theme::muted_text("For troubleshooting. You never need this page for normal editing.");
    ImGui::Spacing();
    if (collapsing_section(app, "diag.link", "Game link###diaglink", true)) {
        card_game_link(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "diag.switches", "Switches###diagswitches", true)) {
        card_switches(app);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "diag.hooks", "Game hooks###diaghooks", false)) {
        draw_lines_box("##diag_hooks", g_snap.hooks, 300.0f);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "diag.details", "Connection details###diagdetails", false)) {
        draw_lines_box("##diag_details", g_snap.details, 240.0f);
        ImGui::Spacing();
    }
    if (collapsing_section(app, "diag.log", "Log###diaglogcard", true)) card_log(app);
}

}  // namespace

// ---------------------------------------------------------------- the pages
void draw_diagnostics_page(App& app) { shell::page::frame(app, nullptr, editor_diagnostics, nullptr, nullptr); }

void draw_data_export_page(App& app) {
    g_state.export_page_drawn = true;
    ImGui::TextUnformatted("Data export");
    theme::muted_text("Writes CSV files (spreadsheets) of your career data. You never need this for normal editing.");
    if (ImGui::SmallButton("Open the export folder##dataexport")) {
        std::error_code ec;
        const fs::path dir = app.turbo_output();
        if (!fs::is_directory(dir, ec)) app.notify("The export folder does not exist yet.", true);
        else if (!open_in_explorer(dir)) app.notify("Explorer did not start.", true);
    }
    ImGui::Spacing();
    draw_career_tools_page(app, shell::Page::Exports);  // the existing Exports page (ui_tools.cpp): same buttons, same Lua tools
}

const DiagnosticsState& diagnostics_state() { return g_state; }

void diagnostics_reset_for_tests() {
    g_snap = DiagSnapshot{};
    g_state = DiagnosticsState{};
}

}  // namespace turbo
