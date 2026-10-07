// FC 27 LE Turbo GUI - Teams > Tactics (see ui_tactics.h).
//
// Frame-loop rules (plan section 9): an exception in App::draw disables the overlay for the session, so the tab body is wrapped
// (inline error, never a throw); the pitch view is cached by app.gen and the slider values; no file I/O in the frame path (the kill
// switch is looked at every 2 s, and again at once when a write is asked for); the pitch is capped at kPitchPrimitiveCap primitives.
#include "ui_tactics.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>

#include "app.h"
#include "core/sliders.h"
#include "core/tactic_profiles.h"
#include "pitch_view.h"

namespace turbo {

namespace {

namespace fs = std::filesystem;

// ---- the built-in fallback formation table (used when the schema has no formation to show; the dots are Turbo's, not saved data)
struct FormationShape {
    const char* name;
    std::vector<PitchDot> dots;
};

PitchDot D(float x, float y, const char* l) {
    PitchDot d;
    d.x = x;
    d.y = y;
    d.label = l;
    return d;
}

const std::vector<FormationShape>& formations() {
    static const std::vector<FormationShape> f = {
        {"4-4-2",
         {D(0.04f, 0.5f, "GK"), D(0.2f, 0.12f, "LB"), D(0.2f, 0.38f, "CB"), D(0.2f, 0.62f, "CB"), D(0.2f, 0.88f, "RB"), D(0.46f, 0.1f, "LM"),
          D(0.46f, 0.38f, "CM"), D(0.46f, 0.62f, "CM"), D(0.46f, 0.9f, "RM"), D(0.72f, 0.38f, "ST"), D(0.72f, 0.62f, "ST")}},
        {"4-3-3",
         {D(0.04f, 0.5f, "GK"), D(0.2f, 0.12f, "LB"), D(0.2f, 0.38f, "CB"), D(0.2f, 0.62f, "CB"), D(0.2f, 0.88f, "RB"), D(0.42f, 0.5f, "CDM"),
          D(0.5f, 0.28f, "CM"), D(0.5f, 0.72f, "CM"), D(0.74f, 0.14f, "LW"), D(0.78f, 0.5f, "ST"), D(0.74f, 0.86f, "RW")}},
        {"4-2-3-1",
         {D(0.04f, 0.5f, "GK"), D(0.2f, 0.12f, "LB"), D(0.2f, 0.38f, "CB"), D(0.2f, 0.62f, "CB"), D(0.2f, 0.88f, "RB"), D(0.4f, 0.38f, "CDM"),
          D(0.4f, 0.62f, "CDM"), D(0.6f, 0.15f, "LM"), D(0.6f, 0.5f, "CAM"), D(0.6f, 0.85f, "RM"), D(0.8f, 0.5f, "ST")}},
        {"3-5-2",
         {D(0.04f, 0.5f, "GK"), D(0.2f, 0.25f, "CB"), D(0.2f, 0.5f, "CB"), D(0.2f, 0.75f, "CB"), D(0.44f, 0.08f, "LWB"), D(0.4f, 0.5f, "CDM"),
          D(0.5f, 0.3f, "CM"), D(0.5f, 0.7f, "CM"), D(0.44f, 0.92f, "RWB"), D(0.74f, 0.38f, "ST"), D(0.74f, 0.62f, "ST")}},
        {"5-3-2",
         {D(0.04f, 0.5f, "GK"), D(0.22f, 0.08f, "LWB"), D(0.18f, 0.3f, "CB"), D(0.18f, 0.5f, "CB"), D(0.18f, 0.7f, "CB"), D(0.22f, 0.92f, "RWB"),
          D(0.46f, 0.3f, "CM"), D(0.44f, 0.5f, "CM"), D(0.46f, 0.7f, "CM"), D(0.74f, 0.38f, "ST"), D(0.74f, 0.62f, "ST")}},
    };
    return f;
}

// ---- panel state
struct TacState {
    int scope = 0;  // SliderScope index
    SliderSet working;   // what the sliders hold now
    SliderSet applied;   // what was last applied (the base for "N changed" and Revert)
    bool dry_run = false;
    bool show_derived = true, show_heat = false;
    int formation = 0;
    std::string selected_profile;  // id of the preset last loaded
    char preset_name[48] = "";
    std::string error;   // inline error of the exception-proof draw
    bool off = false;    // tactics_off.txt (looked at every 2 s)
    double next_check = -1.0;
    int live_written = 0;  // game variables this panel set (cleared when Turbo unloads)
    // the pitch cache
    int cache_gen = -1;
    std::string cache_sig;
    PitchView cache;
};
TacState g_tac;
TacticsTabState g_state;

const char* const kScopeLabels[4] = {"Match (both teams)", "My team", "Position/Role", "Opposition"};
const SliderScope kScopes[4] = {SliderScope::Match, SliderScope::Team, SliderScope::Position, SliderScope::Opposition};

fs::path off_file(App& app) { return app.bridge.root() / "turbo_output" / "tactics_off.txt"; }

ProfileCategory category_of(SliderScope s) {
    switch (s) {
        case SliderScope::Match: return ProfileCategory::Match;
        case SliderScope::Team: return ProfileCategory::Team;
        case SliderScope::Position: return ProfileCategory::Position;
        case SliderScope::Opposition: return ProfileCategory::Opposition;
    }
    return ProfileCategory::Team;
}

// The kill switch, looked at every 2 s (the frame path does no file I/O otherwise)
void refresh_off(App& app) {
    if (app.now < g_tac.next_check) return;
    g_tac.next_check = app.now + 2.0;
    std::error_code ec;
    g_tac.off = fs::exists(off_file(app), ec);
}

std::string name_of(const SliderDef& d, int v) {
    const long idx = long(v) - long(d.min);
    if (idx >= 0 && size_t(idx) < d.labels.size() && !d.labels[size_t(idx)].empty()) return d.labels[size_t(idx)];
    return std::to_string(v);
}

std::vector<int> legal_values(const SliderDef& d) {
    std::vector<int> v;
    if (!d.allowed.empty()) return d.allowed;
    for (int i = d.min; i <= d.max && v.size() < 64; i += std::max(1, d.step)) v.push_back(i);
    return v;
}

bool value_changed(const SliderDef& d) {
    const SliderSet& w = g_tac.working;
    const SliderSet& a = g_tac.applied;
    const bool we = w.is_enabled(d.key), ae = a.is_enabled(d.key);
    if (we != ae) return true;
    return we && w.value_or_default(d.key) != a.value_or_default(d.key);
}

// ---- the pitch's preview model: a plain monotonic mapping of the sliders that are switched on (never metres)
int layer_value(const std::vector<const char*>& keys) {
    for (const char* k : keys)
        if (find_slider(k) && g_tac.working.is_enabled(k)) return g_tac.working.value_or_default(k);
    return -1;
}

std::string preview_sig() {
    std::string s = std::to_string(g_tac.formation) + (g_tac.show_derived ? "d" : "-") + (g_tac.show_heat ? "h" : "-") + (g_tac.off ? "o" : "-");
    for (const char* k : {"team.line_depth", "team.defensive_depth", "team.engagement_height", "team.width_def", "team.defensive_width", "team.tempo"})
        s += "|" + std::to_string(layer_value({k}));
    return s;
}

void rebuild_preview(PitchView& v) {
    v = PitchView();
    const FormationShape& f = formations()[size_t(std::clamp(g_tac.formation, 0, int(formations().size()) - 1))];
    v.dots = f.dots;
    v.dots_note = "Built-in shape (Turbo's fallback table), not read from your save.";
    v.show_derived = g_tac.show_derived;
    v.show_heat = g_tac.show_heat;
    v.greyed = g_tac.off;
    v.def_line = {layer_value({"team.line_depth", "team.defensive_depth"}), "Defensive line"};
    v.press_line = {layer_value({"team.engagement_height"}), "Press line"};
    v.width_band = {layer_value({"team.width_def", "team.defensive_width"}), "Width"};
    const int focus = v.press_line.value >= 0 ? v.press_line.value : v.def_line.value;  // the line the grid is centred on
    if (v.show_heat && focus >= 0) {
        // relative intensity (arbitrary): a smooth ramp around the press (else defensive) line, narrower with a narrower width; deterministic
        v.heat.assign(size_t(kHeatCols * kHeatRows), 0.0f);
        const float px = 0.30f + 0.62f * float(focus) / 100.0f;
        const float half = v.width_band.value >= 0 ? 0.18f + 0.30f * float(v.width_band.value) / 100.0f : 0.5f;
        for (int r = 0; r < kHeatRows; ++r)
            for (int q = 0; q < kHeatCols; ++q) {
                const float cx = (float(q) + 0.5f) / kHeatCols, cy = (float(r) + 0.5f) / kHeatRows;
                const float dx = std::fabs(cx - px), dy = std::fabs(cy - 0.5f);
                float val = 1.0f - dx * 2.0f;
                if (dy > half) val *= 0.3f;
                v.heat[size_t(r * kHeatCols + q)] = std::clamp(val, 0.0f, 1.0f);
            }
    }
}

// ---- writes
std::string tag_of(const SliderDef& d) {
    switch (d.status) {
        case SliderStatus::Live: return "game variable, applies in played matches";
        case SliderStatus::DB: return "saved data, in-game effect unverified";
        case SliderStatus::Local: return "Turbo only, never written to the game";
        case SliderStatus::Preview:
        case SliderStatus::RE: return "not applied to game";
    }
    return "";
}

ImVec4 badge_colour(SliderStatus s) {
    switch (s) {
        case SliderStatus::Live: return ImVec4(0.45f, 0.85f, 0.45f, 1);
        case SliderStatus::DB: return ImVec4(0.45f, 0.7f, 1.0f, 1);
        case SliderStatus::Local: return ImVec4(0.8f, 0.7f, 1.0f, 1);
        case SliderStatus::Preview: return ImVec4(0.9f, 0.8f, 0.4f, 1);
        case SliderStatus::RE: return ImVec4(0.7f, 0.7f, 0.7f, 1);
    }
    return ImVec4(1, 1, 1, 1);
}

// One DB write of "table.field" through App::edit (Database::set: range check, table_alive; undo steps for players; never saves).
// Skipped = this build cannot tell which row to write (not a failure of the write itself); Failed = the write was refused.
enum class DbOutcome { Ok, Skipped, Failed };

DbOutcome write_db(App& app, const SliderWrite& w, std::string& why) {
    const size_t dot = w.target.find('.');
    if (dot == std::string::npos) {
        why = "no field named in " + w.target;
        return DbOutcome::Failed;
    }
    const std::string table = w.target.substr(0, dot), field = w.target.substr(dot + 1);
    const Table* t = app.db.table(table);
    const Field* f = t ? t->field(field) : nullptr;
    if (!t || !f) {
        why = w.target + " is not in this game's database";
        return DbOutcome::Failed;
    }
    std::vector<uint64_t> recs;
    if (table == "teams") {
        const TeamRow* tr = app.model.team(app.sel_team);
        if (!tr) {
            why = "select a team first";
            return DbOutcome::Failed;
        }
        recs.push_back(tr->rec);
    } else if (table == "players") {
        const PlayerRow* p = app.model.player(app.sel_player);
        if (!p) {
            why = "select a player first (Position/Role writes the selected player)";
            return DbOutcome::Failed;
        }
        recs.push_back(p->rec);
    } else if (table == "referee") {
        Snapshot snap;  // every referee row (a user action, not the frame path)
        if (snap.load(app.db.memory(), *t))
            for (uint32_t i : snap.valid) recs.push_back(snap.addr(i));
        if (recs.empty()) {
            why = "the referee table has no rows";
            return DbOutcome::Failed;
        }
    } else {
        why = "which row of " + table + " to write is not resolved in this build";
        return DbOutcome::Skipped;
    }
    for (uint64_t rec : recs) {
        if (!app.db.table_alive(*t, rec)) {
            why = "the database changed (another save loaded?): press Refresh";
            return DbOutcome::Failed;
        }
        if (!app.edit(*t, rec, *f, Value::of_int(w.value))) {
            why = "the game's database refused the value";
            return DbOutcome::Failed;
        }
    }
    return DbOutcome::Ok;
}

void apply_changes(App& app) {
    // the switch is read again here: a write never trusts the 2 s cache
    {
        std::error_code ec;
        g_tac.off = fs::exists(off_file(app), ec);
        g_tac.next_check = app.now + 2.0;
    }
    if (g_tac.off) {
        app.notify("Tactics are off: turbo_output\\tactics_off.txt exists, nothing was written", true);
        return;
    }
    const std::vector<SliderWrite> writes = game_writes(g_tac.working);
    if (g_tac.dry_run) {
        app.notify("Tactics dry run: " + std::to_string(writes.size()) + " write(s) would be sent, nothing was written");
        return;
    }
    int ok = 0, failed = 0, skipped = 0;
    std::string first_error, first_skip;
    auto fail = [&](const std::string& key, const std::string& why) {
        ++failed;
        if (first_error.empty()) first_error = key + ": " + why;
    };
    // a Live slider that was applied and is now back to "the game decides": clear its variable
    for (const SliderDef& d : slider_registry()) {
        if (d.status != SliderStatus::Live || d.binding.targets.empty()) continue;
        if (g_tac.applied.is_enabled(d.key) && !g_tac.working.is_enabled(d.key) && app.match_setup) {
            std::string why;
            if (!app.match_setup->clear_var(d.binding.targets[0], why)) fail(d.key, why);
        }
    }
    for (const SliderWrite& w : writes) {
        if (w.status == SliderStatus::Live) {
            if (!app.match_setup) {
                fail(w.key, "this Turbo has no game calls");
                continue;
            }
            const gv::KnownVar* kv = gv::known(w.target);
            if (!kv) {
                fail(w.key, w.target + " is not a variable Turbo knows");
                continue;
            }
            std::string why;
            const int32_t v = std::clamp<int32_t>(w.value, kv->min, kv->max);
            if (!app.match_setup->set_var(w.target, v, why)) fail(w.key, why);
            else {
                ++ok;
                ++g_tac.live_written;
            }
        } else if (w.status == SliderStatus::DB) {
            std::string why;
            const DbOutcome o = write_db(app, w, why);
            if (o == DbOutcome::Ok) ++ok;
            else if (o == DbOutcome::Skipped) {
                ++skipped;
                if (first_skip.empty()) first_skip = w.key + ": " + why;
            } else fail(w.key, why);
        }
    }
    if (failed == 0) g_tac.applied = g_tac.working;
    std::string msg = "Tactics: " + std::to_string(ok) + " write(s) sent to the game";
    if (skipped) msg += ", " + std::to_string(skipped) + " not written (" + first_skip + ")";
    if (failed) msg += ", " + std::to_string(failed) + " refused (" + first_error + ")";
    app.notify(msg, failed > 0);
}

// ---- a slider row
void draw_row(App& app, const SliderDef& d) {
    SliderSet& w = g_tac.working;
    const bool dim = preview_only(d);
    TacticsTabState::Row row;
    row.key = d.key;
    row.status = status_name(d.status);
    row.tag = tag_of(d);
    row.enabled = w.is_enabled(d.key);
    row.dimmed = dim;
    row.value = w.value_or_default(d.key);
    g_state.rows.push_back(row);

    ImGui::PushID(d.key.c_str());
    if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.55f);
    bool on = w.is_enabled(d.key);
    if (ImGui::Checkbox("##on", &on)) w.set_enabled(d.key, on);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", on ? "Switched on: this value is used" : "Off: the game decides");
    ImGui::SameLine();
    ImGui::TextUnformatted(d.label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.description.c_str());
    ImGui::SameLine();
    ImGui::TextColored(badge_colour(d.status), "[%s]", status_name(d.status));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", status_badge_help(d.status));
    ImGui::SameLine();
    if (ImGui::SmallButton("o##reset")) w.reset(d.key);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to \"the game decides\"");
    if (!d.technical.empty()) ImGui::TextDisabled("  %s", d.technical.c_str());
    int v = w.value_or_default(d.key);
    int nv = v;
    ImGui::SetNextItemWidth(S(150.0f));
    if (d.kind == SliderKind::Toggle) {
        bool b = v != 0;
        if (ImGui::Checkbox("##v", &b)) nv = b ? 1 : 0;
    } else if (d.kind == SliderKind::Enum && (!d.labels.empty() || !d.allowed.empty())) {
        if (ImGui::BeginCombo("##v", on ? name_of(d, v).c_str() : "game decides")) {
            for (int val : legal_values(d))
                if (ImGui::Selectable((name_of(d, val) + "##" + std::to_string(val)).c_str(), on && val == v)) nv = val;
            ImGui::EndCombo();
        }
    } else {
        ImGui::SliderInt("##v", &nv, d.min, d.max, on ? "%d" : "game decides");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(64.0f));
        int typed = v;
        if (ImGui::InputInt("##typed", &typed, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) nv = typed;
    }
    if (nv != v) w.set(d.key, nv, true);
    if (value_changed(d)) {
        ImGui::SameLine();
        const bool ae = g_tac.applied.is_enabled(d.key);
        if (ae) ImGui::TextDisabled("was %s", name_of(d, g_tac.applied.value_or_default(d.key)).c_str());
        else ImGui::TextDisabled("was: game decides");
    }
    ImGui::TextDisabled("  %s", row.tag.c_str());
    if (dim) ImGui::PopStyleVar();
    ImGui::PopID();
}

// ---- columns
void draw_left(App& app) {
    ImGui::SeparatorText("Scope");
    for (int i = 0; i < 4; ++i) {
        const bool sel = g_tac.scope == i;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button((std::string(kScopeLabels[i]) + "##scope").c_str(), ImVec2(-1, 0))) g_tac.scope = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::SeparatorText("Presets");
    const SliderScope sc = kScopes[g_tac.scope];
    const std::vector<const Profile*> list = app.tactic_profiles.list(category_of(sc));
    if (list.empty()) ImGui::TextDisabled("No presets in this scope yet.");
    for (const Profile* p : list) {
        const bool sel = g_tac.selected_profile == p->id;
        const std::string label = p->name + (p->builtin ? " (built-in)" : "") + "##" + p->id;
        if (ImGui::Selectable(label.c_str(), sel)) {
            g_tac.selected_profile = p->id;
            for (const auto& kv : p->sliders.values) {
                if (p->sliders.is_enabled(kv.first)) g_tac.working.set(kv.first, kv.second, true);
                else g_tac.working.reset(kv.first);
            }
            const std::string& fname = p->formation;
            for (size_t i = 0; i < formations().size(); ++i)
                if (fname == formations()[i].name) g_tac.formation = int(i);
            app.notify("Preset loaded: " + p->name + " (not applied yet: press Apply)");
        }
        if (ImGui::IsItemHovered() && !p->note.empty()) ImGui::SetTooltip("%s", p->note.c_str());
    }
    if (!app.tactic_profiles_error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s", app.tactic_profiles_error.c_str());
}

void draw_centre(App& app) {
    ImGui::SetNextItemWidth(S(120.0f));
    if (ImGui::BeginCombo("Formation (preview)##tacform", formations()[size_t(g_tac.formation)].name)) {
        for (size_t i = 0; i < formations().size(); ++i)
            if (ImGui::Selectable(formations()[i].name, int(i) == g_tac.formation)) g_tac.formation = int(i);
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Schematic layers (dashed)##tacd", &g_tac.show_derived);
    ImGui::Checkbox("Modelled layer (hatched, off by default)##tach", &g_tac.show_heat);
    if (app.gen != g_tac.cache_gen || preview_sig() != g_tac.cache_sig) {
        g_tac.cache_gen = app.gen;
        g_tac.cache_sig = preview_sig();
        rebuild_preview(g_tac.cache);
    }
    const PitchViewResult r = draw_pitch_view("##tacpitch", g_tac.cache, 0.0f);
    g_state.chip = r.chip;
    g_state.pitch_primitives = r.primitives;
    g_state.pitch_labels = r.labels;
    if (g_tac.off) ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Effect layers are grey: tactics_off.txt is present.");
    ImGui::TextDisabled("Dashed lines are schematic (the slider value, not a distance). Nothing here is the game's own output.");
}

void draw_right(App& app) {
    const SliderScope sc = kScopes[g_tac.scope];
    const std::vector<const SliderDef*> visible = sliders_in_scope(sc);
    const size_t prev = count_preview_only(visible);
    g_state.preview_strip = std::to_string(prev) + " of " + std::to_string(visible.size()) + " visible settings are preview-only";
    ImGui::TextDisabled("%s", g_state.preview_strip.c_str());
    // the cards: consecutive groups of the registry order
    std::vector<std::string> groups;
    for (const SliderDef* d : visible)
        if (std::find(groups.begin(), groups.end(), d->group) == groups.end()) groups.push_back(d->group);
    const float footer = S(190.0f);
    ImGui::BeginChild("##tacsliders", ImVec2(0, -footer));
    for (const std::string& g : groups) {
        ImGui::PushID(g.c_str());
        if (ImGui::CollapsingHeader(g.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            for (const SliderDef* d : visible)
                if (d->group == g) draw_row(app, *d);
        ImGui::PopID();
    }
    ImGui::EndChild();

    // footer: what the game will receive, N changed, Apply / Revert / Save as preset
    ImGui::SeparatorText("What the game will receive");
    g_state.receive_title = "What the game will receive";
    const std::vector<SliderWrite> writes = game_writes(g_tac.working);
    for (const SliderWrite& w : writes) {
        const SliderDef* d = find_slider(w.key);
        const std::string line = w.target + " = " + (d ? name_of(*d, w.value) : std::to_string(w.value)) + "  [" + status_name(w.status) + "]";
        g_state.receive.push_back(line);
        ImGui::TextUnformatted(line.c_str());
    }
    if (writes.empty()) ImGui::TextDisabled("Nothing: every setting is \"the game decides\".");
    const int changed = int(diff_sliders(g_tac.working, g_tac.applied).size());
    g_state.changed = changed;
    ImGui::Text("%d changed", changed);
    ImGui::SameLine();
    ImGui::Checkbox("Dry run##tacdry", &g_tac.dry_run);
    ImGui::BeginDisabled(g_tac.off);
    if (ImGui::Button("Apply##tac")) apply_changes(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Revert##tac")) {
        g_tac.working = g_tac.applied;
        app.notify("Tactics reverted to what was last applied");
    }
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::InputTextWithHint("##tacname", "preset name", g_tac.preset_name, sizeof(g_tac.preset_name));
    ImGui::SameLine();
    if (ImGui::Button("Save as preset##tac")) {
        const std::string nm = g_tac.preset_name;
        if (nm.empty()) {
            app.notify("Type a name for the preset first", true);
        } else {
            Profile p;
            p.category = category_of(sc);
            p.name = app.tactic_profiles.unique_name(p.category, nm);
            for (const SliderDef* d : visible)
                if (g_tac.working.has(d->key)) p.sliders.set(d->key, g_tac.working.value_or_default(d->key), g_tac.working.is_enabled(d->key));
            if (sc == SliderScope::Team) p.formation = formations()[size_t(g_tac.formation)].name;
            std::time_t tt = std::time(nullptr);
            char buf[32] = "";
            if (const std::tm* tm = std::localtime(&tt)) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
            p.created = buf;
            const std::string id = app.tactic_profiles.add(p);
            std::string err;
            if (id.empty()) {
                app.notify("Preset not saved: the preset file is read-only (written by a newer Turbo)", true);
            } else if (!save_tactic_profiles(tactic_profiles_path(app.bridge.root()), app.tactic_profiles, &err, app.tactic_profiles_unreadable)) {
                app.notify("Preset kept for this session only: " + err, true);
            } else {
                app.tactic_profiles_unreadable = false;
                g_tac.selected_profile = id;
                g_tac.preset_name[0] = 0;
                app.notify("Preset saved: " + p.name);
            }
        }
    }
}

void draw_live_banner(App& app) {
    int active = 0;
    if (app.match_setup) {
        for (const msetup::VarLine& l : app.match_setup->vars())
            if (l.active) ++active;
    }
    if (active > 0) {
        g_state.banner = "Game overrides are active (" + std::to_string(active) + "): they last until the game closes and apply to every match you play.";
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", g_state.banner.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear overrides##tac")) {
            std::string why;
            if (!app.match_setup->clear_all(why)) app.notify("Clear overrides: " + why, true);
            else {
                g_tac.live_written = 0;
                g_tac.applied = SliderSet();
                app.notify("Game overrides cleared: the game decides again");
            }
        }
    }
}

void draw_body(App& app) {
    g_state = TacticsTabState();
    const std::string shown_error = g_tac.error;  // an error stays shown while it keeps happening, then goes
    g_tac.error.clear();
    g_state.scope = g_tac.scope;
    g_state.dry_run = g_tac.dry_run;
    refresh_off(app);
    g_state.off = g_tac.off;
    if (g_tac.off) {
        g_state.off_line = "Tactics are off: turbo_output\\tactics_off.txt exists. Nothing is written and the effect layers are grey.";
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", g_state.off_line.c_str());
    }
    draw_live_banner(app);
    if (!shown_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error: %s", shown_error.c_str());
    const float total = ImGui::GetContentRegionAvail().x;
    const ImGuiChildFlags cf = ImGuiChildFlags_Borders;
    ImGui::BeginChild("##tacleft", ImVec2(total * 0.22f, 0), cf);
    try {
        draw_left(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##taccentre", ImVec2(total * 0.46f, 0), cf);
    try {
        draw_centre(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##tacright", ImVec2(0, 0), cf);
    try {
        draw_right(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
    } catch (...) {
        g_tac.error = "unknown error";
    }
    ImGui::EndChild();
    g_state.error = g_tac.error.empty() ? shown_error : g_tac.error;
}

}  // namespace

const TacticsTabState& tactics_tab_state() { return g_state; }

bool tactics_off_now(App& app) {
    std::error_code ec;
    return fs::exists(off_file(app), ec);
}

void draw_tactics(App& app) {
    try {
        draw_body(app);
    } catch (const std::exception& e) {
        g_tac.error = e.what();
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error: %s", e.what());
    } catch (...) {
        g_tac.error = "unknown error";
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "Tactics error");
    }
}

void tactics_clear_on_exit(App& app) {
    try {
        if (g_tac.live_written > 0 && app.match_setup) {
            std::string why;
            app.match_setup->clear_all(why);
            g_tac.live_written = 0;
        }
    } catch (...) {
    }
}

}  // namespace turbo
