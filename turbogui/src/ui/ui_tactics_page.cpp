// FC 27 LE Turbo GUI - Tactics section pages (see ui_tactics_page.h): Tactic presets, Team sheet & bench, Game sliders.
//
// Chrome plus the thinnest glue, no new game logic: every write goes through an API that already exists (the Tactic presets panel for
// tactic presets, game_settings_stage / _unstage for the sliders, the bench-keep switch file for the bench toggle). Reads are throttled
// to once a second and never block a frame. Never throws. Honesty (plan 5.1 R8): a tactic preset is written and read back, its effect in
// a played match is not tested; the bench option is new and not tested in game; sliders are prepared here and installed later.
#include "ui_tactics_page.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "app.h"
#include "core/bench_probe.h"
#include "core/game_settings.h"
#include "core/tactic_presets.h"
#include "core/team_sheet.h"
#include "imgui.h"
#include "nlohmann/json.hpp"
#include "ui_game_settings.h"
#include "ui_live_tactic.h"
#include "ui_page.h"
#include "ui_shell.h"
#include "ui_tactic_presets.h"
#include "ui_tactics_pages.h"
#include "ui_theme.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;

fs::path out_dir(App& app) { return app.bridge.root() / "turbo_output"; }

void log_line(App& app, const std::string& s) {
    app.log(s);
    if (app.log_hook) app.log_hook(s);
}

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

// A short coloured tag in brackets ("Not tested yet")
void tag(const std::string& text, const ImVec4& col) { ImGui::TextColored(col, "[%s]", text.c_str()); }

// "433" -> "4-3-3"; anything else as the game spells it
std::string shape_text(const std::string& name) {
    if (name.size() < 3 || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) return name.empty() ? std::string("unknown") : name;
    std::string o;
    for (size_t i = 0; i < name.size(); ++i) {
        if (i) o += '-';
        o += name[i];
    }
    return o;
}

std::string approach_label(int v) {
    const tsheet::EnumLabel* e = tsheet::defensive_approach(v);
    return e ? std::string(e->label) : std::string("unknown");
}
std::string build_up_label(int v) {
    const tsheet::EnumLabel* e = tsheet::build_up_style(v);
    return e ? std::string(e->label) : std::string("unknown");
}

std::string player_name(App& app, int64_t pid) {
    if (pid < 0) return "";
    const PlayerRow* p = app.model.player(pid);
    return p && !p->name.empty() ? p->name : std::string("Unknown player");
}

// One read of the live tactic and sheet per second (the Tactic presets panel reads the same way)
struct LiveRead {
    double next = -1.0;
    bool ok = false;
    tsheet::LiveTactic lt;
    std::string why;
};
void read_live(App& app, LiveRead& r, bool force = false) {
    if (!force && app.now < r.next) return;
    r.next = app.now + 1.0;
    tsheet::LiveTactic lt;
    std::string why;
    r.ok = live_tactic_read(app, lt, why);
    if (r.ok) r.lt = lt;
    r.why = why;
}

// ---------------------------------------------------------------------------------------------- Tactic presets
TacticsPresetsPageState s1;
LiveRead live1;

void presets_frame(App& app) {
    read_live(app, live1);
    s1.opt_in = live_tactic_opt_in(app);
    s1.tactic_read = live1.ok;
    s1.tag = "Not tested yet";
    s1.proof_line = "Written to the game and read back. How it changes matches: not tested yet.";
    s1.store_line = "One list of presets, shared with Game sliders.";
    if (live1.ok) {
        s1.tactic_now = approach_label(live1.lt.approach) + ", line height " + std::to_string(live1.lt.line_height) + ", build-up " +
                        build_up_label(live1.lt.build_up) + " - shape " + shape_text(live1.lt.name);
    } else if (!s1.opt_in) {
        s1.tactic_now = "not read. Turn on \"Let Turbo change my tactic\" below so Turbo can look at it.";
    } else {
        // 2.20: the real reason in plain words (until 2.20 every failure said "go to the hub", also in the hub; brief 2.20-T-tactics cause B)
        s1.tactic_now = "not read. " + tpreset::live_why_plain(live1.why.empty() ? std::string("not read yet") : live1.why);
    }
    tag(s1.tag, theme::warn());
    ImGui::SameLine();
    theme::muted_text("%s", s1.proof_line.c_str());
    theme::muted_text("%s", s1.store_line.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A preset can hold your tactic and, if you tick them, your set pieces and gameplay sliders. Game sliders installs the sliders part.");
    theme::muted_text("Tactic now: %s", s1.tactic_now.c_str());
    if (app.prefs.advanced_mode && !live1.ok && ImGui::IsItemHovered() && !live1.why.empty()) ImGui::SetTooltip("%s", live1.why.c_str());
    if (live1.ok) {
        ImGui::SameLine();
        theme::muted_text("Turbo cannot change the shape.");
    }
}

// ---------------------------------------------------------------------------------------------- Team sheet & bench
TeamSheetPageState s2;
LiveRead live2;
struct Switches {
    double next = -1.0;
    bool on_file = false, kill = false;
    std::string session_off;
} sw2;
struct SetPieces {
    double next = -1.0;
    bool have = false;
    tpreset::SetPieceBlock block;
    std::string why;
    std::string asked;
} sp2;

void read_switches(App& app) {
    if (app.now < sw2.next) return;
    sw2.next = app.now + 1.0;
    std::error_code ec;
    const fs::path o = out_dir(app);
    sw2.on_file = fs::exists(o / bench::keep_on_file(), ec);
    sw2.kill = fs::exists(o / tsheet::master_switch_file(), ec) || fs::exists(o / tsheet::kill_switch_file(), ec) || fs::exists(o / bench::keep_off_file(), ec);
    sw2.session_off = app.team_sheet ? app.team_sheet->bench_off() : std::string();
}

void read_setpieces(App& app) {
    if (app.now < sp2.next) return;
    sp2.next = app.now + 1.0;
    sp2.have = false;
    sp2.why.clear();
    std::vector<uint8_t> b;
    if (!gsettings::read_bytes(out_dir(app) / tpreset::setpieces_current_file(), b, 256 * 1024)) {
        sp2.why = "not read yet. Press \"Read from the game\".";
        return;
    }
    tpreset::SetPieceBlock sp;
    std::string err;
    if (!tpreset::setpieces_parse_current(std::string(b.begin(), b.end()), sp, &err)) {
        sp2.why = "the last read could not be used. Press \"Read from the game\" again.";
        return;
    }
    if (live2.ok && live2.lt.at.user > 0 && sp.team != live2.lt.at.user) {
        sp2.why = "these belong to another club. Press \"Read from the game\" again.";
        return;
    }
    sp2.block = sp;
    sp2.have = true;
}

void update_team_sheet_state(App& app) {
    read_live(app, live2);
    read_switches(app);
    read_setpieces(app);
    s2.tag = "New, not tested yet";
    s2.bench_on = sw2.on_file;
    s2.bench_hint.clear();
    if (sw2.kill)
        s2.bench_status = "Switched off by a safety switch. See Advanced > Diagnostics & logs.";
    else if (!sw2.session_off.empty())
        s2.bench_status = "Switched off for this session because a check failed. Restart the game to try again.";
    else if (sw2.on_file)
        s2.bench_status = "On. Each time Team Management opens, Turbo puts your saved bench order back. The starting XI is never touched.";
    else
        s2.bench_status = "Off. The game decides your bench order.";
    if (sw2.on_file && !live_tactic_opt_in(app)) s2.bench_hint = "Also turn on \"Let Turbo change my tactic\" on Tactic presets, so Turbo can find your team sheet.";

    s2.sheet_read = live2.ok;
    s2.sheet_why = live2.ok ? std::string()
                            : (live_tactic_opt_in(app) ? tpreset::live_why_plain(live2.why.empty() ? std::string("not read yet") : live2.why)
                                                       : "Turn on \"Let Turbo change my tactic\" on Tactic presets first.");
    s2.starting.clear();
    s2.bench.clear();
    if (live2.ok) {
        for (int k = 0; k < tsheet::kSheetPidCount; ++k) {
            const int32_t pid = live2.lt.sheet_ids[size_t(k)];
            if (pid < 0) continue;
            (k < bench::kStarters ? s2.starting : s2.bench).push_back(player_name(app, pid));
        }
    }
    s2.setpieces_read = sp2.have;
    s2.setpieces_line = sp2.have ? std::string("Read from the game.") : "Captain and takers: " + sp2.why;
    s2.takers.clear();
    if (sp2.have) {
        int n = 0;
        for (const tpreset::SetPieceField& f : tpreset::setpiece_fields()) {
            if (++n > 9) break;  // captain, penalty, free kicks, corners, throw-ins: the supports stay out of sight
            auto it = sp2.block.values.find(f.name);
            if (it == sp2.block.values.end()) continue;
            const std::string nm = player_name(app, it->second);
            s2.takers.emplace_back(f.label, nm.empty() ? std::string("Nobody set") : nm);
        }
    }
}

void bench_card(App& app) {
    ImGui::TextUnformatted("Bench order");
    ImGui::SameLine();
    tag(s2.tag, theme::warn());
    ImGui::Separator();
    bool on = s2.bench_on;
    if (checkbox_right("Keep my bench order###tsbench", &on)) team_sheet_set_bench_keep(app, on);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The game re-orders your bench every time Team Management opens. With this on, Turbo puts your saved order back. "
                          "Only the bench and reserves are changed, never the starting XI and never a player added or removed.");
    theme::muted_text("%s", s2.bench_status.c_str());
    if (!s2.bench_hint.empty()) theme::warn_text("%s", s2.bench_hint.c_str());
    ImGui::Spacing();
    theme::muted_text("Your saved order");
    if (!s2.sheet_read) {
        theme::muted_text("Can't read your team sheet right now. %s", s2.sheet_why.c_str());
        if (app.prefs.advanced_mode && ImGui::IsItemHovered() && !live2.why.empty()) ImGui::SetTooltip("%s", live2.why.c_str());
        return;
    }
    std::string xi;
    for (const std::string& n : s2.starting) xi += (xi.empty() ? "" : ", ") + n;
    ImGui::TextWrapped("Starting XI: %s", xi.c_str());
    theme::muted_text("Bench and reserves, in order");
    ImGui::BeginChild("##tsbenchlist", ImVec2(0, S(150.0f)), ImGuiChildFlags_Borders);
    int i = 0;
    for (const std::string& n : s2.bench) ImGui::Text("%d. %s", ++i, n.c_str());
    if (s2.bench.empty()) theme::muted_text("Nobody on the bench.");
    ImGui::EndChild();
}

void setpieces_card(App& app) {
    ImGui::SeparatorText("Captain & takers");
    if (ImGui::SmallButton("Read from the game###tsreadsp")) {
        const nlohmann::json cmd = nlohmann::json::parse(tpreset::setpieces_command_json("get", tpreset::SetPieceBlock(), ""), nullptr, false);
        if (!cmd.is_discarded() && app.send(cmd, "Read captain and takers")) sp2.asked = "Asked the game. They appear here after you open another career menu.";
        else sp2.asked = "Couldn't ask the game right now.";
        sp2.next = -1.0;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only reads. Nothing is changed.");
    if (!sp2.asked.empty()) theme::muted_text("%s", sp2.asked.c_str());
    if (!sp2.have) {
        theme::muted_text("%s", s2.setpieces_line.c_str());
        return;
    }
    for (const auto& kv : s2.takers) {
        theme::muted_text("%s", kv.first.c_str());
        ImGui::SameLine(S(210.0f));
        ImGui::TextUnformatted(kv.second.c_str());
    }
    theme::muted_text("To change them, set them in the game. Tactic presets can save them and put them back.");
}

// ---------------------------------------------------------------------------------------------- Game sliders
GameSlidersPageState s3;
struct Sliders {
    bool loaded = false;
    std::vector<tpreset::Preset> presets;
    std::vector<int> idx;  // into presets: those with a sliders block
    int sel = -1;          // into idx
    int plan_for = -2;
    bool plan_ok = false;
    int writable = 0, unverified = 0;
    size_t skipped = 0;
    std::string plan_why;
    double next_pending = -1.0;
    bool have_pending = false;
    std::string pending_name;
    std::string raw_message;
    std::vector<std::string> errors;  // 2.20: the files load_presets skipped (shown, never dropped)
    bool sliders_ok = false;          // 2.20: the Custom sliders block the game saved last (first look + Refresh)
    std::map<std::string, int> sliders;
    std::string sliders_why;
    int refreshes = 0;
} sl;

const tpreset::Preset* selected_preset() {
    if (sl.sel < 0 || sl.sel >= int(sl.idx.size())) return nullptr;
    return &sl.presets[size_t(sl.idx[size_t(sl.sel)])];
}

void read_sliders_now(App& app) {
    std::string layout, map_build;
    sl.sliders.clear();
    sl.sliders_ok = game_settings_current(app, sl.sliders, layout, map_build, sl.sliders_why);
    if (sl.sliders_ok) sl.sliders_why.clear();
}

std::string sliders_now_line() {
    if (!sl.sliders_ok) return "Custom sliders now: not read (" + sl.sliders_why + ")";
    auto val = [](const char* k) {
        auto it = sl.sliders.find(k);
        return it == sl.sliders.end() ? std::string("?") : std::to_string(it->second);
    };
    return "Custom sliders now: " + std::to_string(sl.sliders.size()) + " values (sprint speed you " + val("user.sprint_speed") + " / CPU " +
           val("cpu.sprint_speed") + ", acceleration you " + val("user.acceleration") + ") - " + gsettings::kSlidersSource;
}

void load_presets_now(App& app) {
    std::vector<std::string>& errors = sl.errors;
    errors.clear();
    const fs::path dir = tpreset::presets_dir(out_dir(app));
    sl.presets = tpreset::load_presets(dir, &errors);
    log_line(app, "game sliders: " + tpreset::scan_line(sl.presets.size(), errors, dir.u8string()));
    sl.idx.clear();
    for (size_t i = 0; i < sl.presets.size(); ++i)
        if (sl.presets[i].settings.present) sl.idx.push_back(int(i));
    if (sl.sel >= int(sl.idx.size())) sl.sel = -1;
    sl.plan_for = -2;
    sl.loaded = true;
}

void read_pending(App& app, bool force = false) {
    if (!force && app.now < sl.next_pending) return;
    sl.next_pending = app.now + 1.0;
    sl.have_pending = false;
    sl.pending_name.clear();
    std::vector<uint8_t> b;
    std::error_code ec;
    const fs::path f = out_dir(app) / gsettings::pending_file();
    if (!fs::exists(f, ec) || !gsettings::read_bytes(f, b, 1u << 20)) return;
    gsettings::Pending pd;
    std::string err;
    if (!gsettings::Pending::parse(std::string(b.begin(), b.end()), pd, &err)) return;
    sl.have_pending = true;
    sl.pending_name = pd.preset;
}

void plan_selected(App& app) {
    const tpreset::Preset* p = selected_preset();
    if (!p) {
        sl.plan_for = -2;
        return;
    }
    if (sl.plan_for == sl.sel) return;  // once per selection (and after Refresh), not per frame
    sl.plan_for = sl.sel;
    std::vector<std::string> skipped;
    sl.writable = sl.unverified = 0;
    sl.plan_why.clear();
    sl.plan_ok = game_settings_plan(app, p->settings.values, sl.writable, skipped, sl.plan_why, p->settings.include_unverified, &sl.unverified);
    sl.skipped = skipped.size();
}

void update_sliders_state(App& app) {
    if (!sl.loaded) {
        load_presets_now(app);
        read_sliders_now(app);
    }
    read_pending(app);
    plan_selected(app);
    s3.names.clear();
    for (int i : sl.idx) s3.names.push_back(sl.presets[size_t(i)].name);
    s3.selected = sl.sel;
    s3.skipped = sl.errors;
    s3.sliders_now = sliders_now_line();
    s3.refreshes = sl.refreshes;
    s3.tag = "Effect in matches: not tested yet";
    const tpreset::Preset* p = selected_preset();
    s3.counts_line.clear();
    s3.status.clear();
    s3.can_prepare = s3.can_discard = s3.prepared = false;
    s3.steps = "1. Press Prepare.\n2. Close the game, then run turbo\\turbo_settings_apply.exe --apply (the settings tool in the Turbo folder).\n3. Start the game and choose Gameplay Type: Custom.";
    if (!p) return;
    if (!sl.plan_ok) {
        s3.counts_line = "Can't check these sliders now. Turbo has no slider list for this version of the game.";
    } else {
        const int proven = sl.writable - sl.unverified;
        s3.counts_line = std::to_string(proven) + " slider" + (proven == 1 ? "" : "s") + " confirmed on the Custom screen";
        if (sl.unverified > 0) s3.counts_line += ", " + std::to_string(sl.unverified) + " experimental (not seen on that screen yet)";
        if (sl.skipped > 0) s3.counts_line += ", " + std::to_string(sl.skipped) + " left out";
        s3.counts_line += ".";
    }
    s3.prepared = sl.have_pending && sl.pending_name == p->name;
    if (s3.prepared) s3.status = "Prepared. Close the game, then run turbo\\turbo_settings_apply.exe --apply to finish.";
    else if (sl.have_pending) s3.status = "Another preset is prepared (\"" + sl.pending_name + "\"). Preparing this one replaces it.";
    else s3.status = "Not prepared yet.";
    s3.can_prepare = sl.plan_ok && sl.writable > 0;
    s3.can_discard = s3.prepared;
}

void prepare_selected(App& app) {
    const tpreset::Preset* p = selected_preset();
    if (!p) return;
    std::string msg;
    int staged = 0;
    std::vector<std::string> skipped;
    const bool ok = game_settings_stage(app, p->name, p->settings.values, msg, staged, skipped, p->settings.include_unverified);
    sl.raw_message = msg;
    s3.message = ok ? "Prepared " + std::to_string(staged) + " slider" + (staged == 1 ? "" : "s") + " from \"" + p->name +
                          "\". Close the game and run turbo\\turbo_settings_apply.exe --apply, then start with Gameplay Type: Custom."
                    : "Couldn't prepare \"" + p->name + "\". None of its sliders can be installed with this version of the game yet.";
    log_line(app, "game sliders: " + (ok ? std::string("prepared ") : std::string("not prepared ")) + p->name);
    read_pending(app, true);
}

void discard_selected(App& app) {
    const tpreset::Preset* p = selected_preset();
    if (!p) return;
    std::string msg;
    const bool ok = game_settings_unstage(app, p->name, msg);
    sl.raw_message = msg;
    s3.message = ok ? "Discarded. Nothing will be installed." : "Nothing to discard.";
    log_line(app, "game sliders: " + std::string(ok ? "discarded " : "nothing to discard for ") + p->name);
    read_pending(app, true);
}

void sliders_select_panel(App& app, void*) {
    ImGui::TextUnformatted("Slider presets");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh###gsrefresh")) {
        // 2.20: the folder, the prepared file and the Custom sliders, all read again (until 2.20 only the folder)
        load_presets_now(app);
        read_pending(app, true);
        read_sliders_now(app);
        ++sl.refreshes;
        log_line(app, "game sliders: refresh: " + std::to_string(sl.idx.size()) + " slider preset(s) listed, " + std::to_string(sl.errors.size()) +
                          " file(s) skipped; custom sliders " + (sl.sliders_ok ? "read: " + std::to_string(sl.sliders.size()) + " values" : "not read: " + sl.sliders_why));
        s3.message = "Read again: " + std::to_string(sl.idx.size()) + " slider preset" + (sl.idx.size() == 1 ? "" : "s") +
                     (sl.errors.empty() ? std::string() : ", " + std::to_string(sl.errors.size()) + " file(s) could not be used") + ".";
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reads the presets and the Custom sliders again");
    for (size_t k = 0; k < sl.idx.size(); ++k) {
        const tpreset::Preset& p = sl.presets[size_t(sl.idx[k])];
        const std::string label = p.name + "  -  " + std::to_string(p.settings.values.size()) + " sliders###gsel" + std::to_string(k);
        if (ImGui::Selectable(label.c_str(), sl.sel == int(k), 0, ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            if (sl.sel != int(k)) log_line(app, "game sliders: picked \"" + p.name + "\": " + tpreset::summary(p));
            sl.sel = int(k);
            sl.plan_for = -2;
            s3.message.clear();
        }
    }
    if (sl.idx.empty() && sl.errors.empty()) theme::muted_text("No slider presets yet. Save one on Tactic presets with \"Include gameplay sliders\" ticked.");
    if (!sl.errors.empty()) theme::warn_text("%zu preset file(s) could not be used:", sl.errors.size());
    for (const std::string& e : sl.errors) ImGui::TextWrapped("Skipped %s", e.c_str());
}

void sliders_editor_panel(App& app, void*) {
    const tpreset::Preset* p = selected_preset();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * theme::kFontTitle);
    ImGui::TextUnformatted(p ? p->name.c_str() : "Game sliders");
    ImGui::PopFont();
    tag(s3.tag, theme::warn());
    theme::muted_text("%s", s3.sliders_now.c_str());
    if (!p) {
        theme::muted_text("Pick a slider preset on the left.");
        return;
    }
    ImGui::TextWrapped("%s", s3.counts_line.c_str());
    if (s3.prepared) theme::ok_text("%s", s3.status.c_str());
    else theme::muted_text("%s", s3.status.c_str());
    ImGui::Spacing();
    theme::muted_text("How it works");
    ImGui::TextWrapped("%s", s3.steps.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(!s3.can_discard);
    if (ImGui::Button("Discard###gsdiscard")) discard_selected(app);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Takes the prepared sliders back. Nothing will be installed.");
    if (!s3.message.empty()) {
        ImGui::TextWrapped("%s", s3.message.c_str());
        if (app.prefs.advanced_mode && ImGui::IsItemHovered() && !sl.raw_message.empty()) ImGui::SetTooltip("%s", sl.raw_message.c_str());
    }
    if (app.prefs.advanced_mode && ImGui::CollapsingHeader("Game settings file and slider values (Advanced)###gsadv")) draw_game_settings(app);
}

}  // namespace

const TacticsPresetsPageState& tactics_presets_page_state() { return s1; }
const TeamSheetPageState& team_sheet_page_state() { return s2; }
const GameSlidersPageState& game_sliders_page_state() { return s3; }

void tactics_pages_reset() {
    s1 = TacticsPresetsPageState();
    live1 = LiveRead();
    s2 = TeamSheetPageState();
    live2 = LiveRead();
    sw2 = Switches();
    sp2 = SetPieces();
    s3 = GameSlidersPageState();
    sl = Sliders();
}

bool team_sheet_set_bench_keep(App& app, bool on) {
    std::error_code ec;
    const fs::path p = out_dir(app) / bench::keep_on_file();
    if (on) {
        fs::create_directories(out_dir(app), ec);
        std::ofstream(p, std::ios::binary | std::ios::trunc) << "Turbo bench keep on: Turbo puts your saved bench / reserves order back when the game "
                                                                "reorders it after Team Management (switched on in the Turbo window "
                                                             << stamp() << ")\n";
    } else {
        fs::remove(p, ec);
    }
    const bool now_on = fs::exists(p, ec);
    sw2.next = -1.0;  // read the switches again at once
    log_line(app, std::string("bench keep: ") + (now_on ? "switched on" : "switched off") + " in the Turbo window (Team sheet & bench)");
    return now_on == on;
}

// 4.0 S10: the three pages sit in page::Frame (title, one help line, scrolling body, status line, bar Undo / Back). No page has an
// Apply in the bar: Apply preset and Prepare stay inside the panels they were proven in. The page bodies are the same calls as before.
void draw_tactics_presets_page(App& app) {
    s1.drawn = true;
    page::Frame frame(app, {"tactics_presets", "Presets", "Save your tactic as a preset and load it back. Set pieces and sliders can ride along.", nullptr});
    try {
        presets_frame(app);
        draw_tactic_presets(app);  // the list, the editor, Apply, Undo: all of it stays where it was proven
    } catch (const std::exception& e) {
        theme::err_text("Tactic presets error: %s", e.what());
    } catch (...) {
        theme::err_text("Tactic presets error");
    }
    frame.end();
}

void draw_team_sheet_page(App& app) {
    s2.drawn = true;
    page::Frame frame(app, {"team_sheet", "Team sheet & bench", "Your starting eleven and bench, and who is captain and takes the set pieces.", nullptr});
    try {
        update_team_sheet_state(app);
        bench_card(app);
        ImGui::Spacing();
        setpieces_card(app);  // the existing read-out of the captain and the takers (reading only; see its last line)
    } catch (const std::exception& e) {
        theme::err_text("Team sheet error: %s", e.what());
    } catch (...) {
        theme::err_text("Team sheet error");
    }
    frame.end();
}

const char* const kOfflineSlidersHeading = "Offline slider file (experimental)";
const char* const kOfflineSlidersLabel =
    "No effect seen in game on 2026-10-24: the game rewrote its Settings file at boot, before the Main Menu (GUI30K). Kept for diagnosis.";

// 4.0 S10: Tactics > Game sliders draws the offline slider section (below) in the page frame, the same function the Lab page calls, with
// its heading and its honest "no effect seen in game" label. It replaces the 3.0.1 (GUI30K) pointer page ("moved to the Lab").
void draw_game_sliders_page(App& app) {
    s3.drawn = true;
    page::Frame frame(app, {"game_sliders", "Game sliders", "Prepare a slider preset here and install it later. No effect in the game has been seen yet.", nullptr});
    draw_offline_slider_section(app);  // has its own try / catch
    frame.end();
}

// The section the Tactics area had until 3.0.0 (the list, Prepare / Discard, the steps, the Advanced Game settings view), unchanged,
// under its heading and label. Its own ID scope; its body fills the rest of the Lab page's bottom region (##labtacoff), so the list's
// Refresh (top of ##select) and Prepare (footer) are both in view: a fixed 480 px body overflowed the up-to-320 px region and hid Refresh
// (GUI30K FAIL on 0f5d3b2, test_nav_220.h:196); the list and the editor scroll inside their own panels.
void draw_offline_slider_section(App& app) {
    s3.drawn = true;
    s3.lab_heading = kOfflineSlidersHeading;
    s3.lab_label = kOfflineSlidersLabel;
    ImGui::PushID("##offsl");
    ImGui::SeparatorText(kOfflineSlidersHeading);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::warn());
    ImGui::TextWrapped("%s", kOfflineSlidersLabel);
    ImGui::PopStyleColor();
    ImGui::BeginChild("##offslbody", ImVec2(0, 0), ImGuiChildFlags_None);
    try {
        update_sliders_state(app);
        shell::page::Primary primary;
        primary.label = "Prepare###gsprepare";
        primary.enabled = s3.can_prepare;
        shell::page::frame(app, sliders_select_panel, sliders_editor_panel, nullptr, &primary);
        if (primary.clicked) prepare_selected(app);
    } catch (const std::exception& e) {
        theme::err_text("Game sliders error: %s", e.what());
    } catch (...) {
        theme::err_text("Game sliders error");
    }
    ImGui::EndChild();
    ImGui::PopID();
}

// Lab > Tactics (experimental): the old page draw_tactics_page (ui_tactics.cpp: amber line, ##ttac child, draw_tactics) called as it is,
// in a region that leaves the bottom part of the page free, then the offline slider file section in that bottom part, below the
// live-slider block. Calling draw_tactics_page itself (not a copy of its body) keeps it reached by the default navigation (NAVCAP,
// tools/nav_capability_check.py: "Advanced > Tactics (experimental): draw_tactics_page"); with the old pages on it still answers alone.
void draw_lab_tactics_page(App& app) {
    const float avail = ImGui::GetContentRegionAvail().y;
    const float off_h = std::max(1.0f, std::min(S(320.0f), avail * 0.4f));
    ImGui::BeginChild("##labtaclive", ImVec2(0, -off_h), ImGuiChildFlags_None);  // negative height: all but the bottom off_h
    draw_tactics_page(app);
    ImGui::EndChild();
    ImGui::BeginChild("##labtacoff", ImVec2(0, 0), ImGuiChildFlags_None);
    draw_offline_slider_section(app);
    ImGui::EndChild();
}
TURBO_REGISTER_NEW_PAGE(shell::Page::AdvTactics, draw_lab_tactics_page);

}  // namespace turbo
