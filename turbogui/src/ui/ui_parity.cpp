// FC 27 LE Turbo GUI - FC 26 Live Editor parity panels (see ui_parity.h)
#include "ui_parity.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "app.h"
#include "core/scouts.h"
#include "core/tactic_profiles.h"  // read_text_file
#include "imgui.h"
#include "table_sort.h"

namespace turbo {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------------- transfer columns
namespace {
struct XferState {
    bool on = false;
    bool loaded = false;           // the export was read at least once (Reload reads it again)
    int gen = -1;                  // app.gen of the database part
    std::vector<xfer::Move> export_moves, moves;
    std::unordered_map<int64_t, xfer::TeamStats> stats;
    std::string export_name;       // file name of the export read ("" = none)
    int skipped = 0;
    size_t db_rows = 0;
    std::string line;
};
XferState g_x;

void xfer_read_export(App& app) {
    g_x.export_moves.clear();
    g_x.export_name.clear();
    g_x.skipped = 0;
    const fs::path p = xfer::newest_export(app.bridge.dir());
    if (!p.empty()) {
        std::string text, err;
        if (read_text_file(p, text, &err)) {
            g_x.export_moves = xfer::parse_export_csv(text, &g_x.skipped);
            g_x.export_name = p.filename().string();
        }
    }
    g_x.loaded = true;
    g_x.gen = -1;  // merge again
}

void xfer_refresh(App& app) {
    if (!g_x.loaded) xfer_read_export(app);
    if (g_x.gen == app.gen) return;
    g_x.gen = app.gen;
    const std::vector<xfer::Move> dbm = app.db.ready() ? xfer::read_db_transfers(app.db) : std::vector<xfer::Move>{};
    g_x.db_rows = dbm.size();
    g_x.moves = xfer::merge(g_x.export_moves, dbm);
    g_x.stats = xfer::per_team(g_x.moves);
    g_x.line = "Transfers: " + std::to_string(g_x.moves.size()) + " moves (" +
               (g_x.export_name.empty() ? std::string("no export yet") : "export " + g_x.export_name) + ", database " +
               std::to_string(g_x.db_rows) + ")";
    if (g_x.skipped > 0) g_x.line += ", " + std::to_string(g_x.skipped) + " unreadable rows skipped";
    if (g_x.skipped < 0) g_x.line += ", the export's columns were not recognised";
}

void export_button(App& app) {
    const bool cm = app.bridge.state().in_cm;
    const bool disabled = app.busy() || !app.mailbox || !cm;
    if (disabled) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Export now##xfexp"))
        app.send({{"op", "run"}, {"module", "export_transfer_history"}, {"overrides", nlohmann::json::object()}}, "Transfer history");
    if (disabled) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(cm ? "Writes this season's moves from the game to turbo_output (Turbo Tools > Exports > Transfer history);\n"
                               "press Reload when it is done"
                             : "Needs a loaded career save");
}
}  // namespace

bool transfer_columns_on() { return g_x.on; }
const char* transfer_source_line() { return g_x.line.c_str(); }

void transfer_columns_controls(App& app) {
    ImGui::Checkbox("Transfers##xfon", &g_x.on);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Adds In / Out / Spent / Received / Net columns: this season's moves from Turbo's transfer history export\n"
                          "(Turbo Tools > Exports) plus the database's transfers table. Read only.");
    if (!g_x.on) return;
    xfer_refresh(app);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload##xfre")) xfer_read_export(app), xfer_refresh(app);
    ImGui::SameLine();
    export_button(app);
    ImGui::TextDisabled("%s", g_x.line.c_str());
}

const xfer::TeamStats* transfer_stats_of(App& app, int64_t teamid) {
    xfer_refresh(app);
    auto it = g_x.stats.find(teamid);
    return it == g_x.stats.end() ? nullptr : &it->second;
}

void team_transfers_tab(App& app, int64_t teamid) {
    xfer_refresh(app);
    if (ImGui::SmallButton("Reload##xftre")) xfer_read_export(app), xfer_refresh(app);
    ImGui::SameLine();
    export_button(app);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_x.line.c_str());
    const auto it = g_x.stats.find(teamid);
    if (it != g_x.stats.end()) {
        const xfer::TeamStats& s = it->second;
        ImGui::Text("In %d (loans %d), out %d (loans %d). Spent %s, received %s, net %s", s.in, s.loans_in, s.out, s.loans_out,
                    xfer::money_short(s.spent).c_str(), xfer::money_short(s.received).c_str(), xfer::money_short(s.net()).c_str());
    }
    const std::vector<xfer::Move> rows = xfer::of_team(g_x.moves, teamid);
    if (rows.empty()) {
        ImGui::TextDisabled(g_x.export_name.empty() ? "No moves known. Export the transfer history (Export now), then Reload."
                                                    : "No moves of this club in the export or the database.");
        return;
    }
    if (ImGui::BeginTable("##xfrows", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, S(80.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(30.0f));
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Club");
        ImGui::TableSetupColumn("Fee", ImGuiTableColumnFlags_WidthFixed, S(60.0f));
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, S(70.0f));
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(rows.size()));
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const xfer::Move& m = rows[static_cast<size_t>(i)];
                const bool in = m.to == teamid;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(xfer::date_text(m.date).c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(in ? ImVec4(0.45f, 0.85f, 0.45f, 1.0f) : ImVec4(1.0f, 0.6f, 0.4f, 1.0f), in ? "in" : "out");
                ImGui::TableNextColumn();
                const std::string pname = !m.player.empty() ? m.player : app.model.player_name(m.playerid);
                ImGui::Text("%s (%lld)", pname.c_str(), static_cast<long long>(m.playerid));
                ImGui::TableNextColumn();
                const int64_t other = in ? m.from : m.to;
                ImGui::Text("%s %s", in ? "from" : "to",
                            other == xfer::kFreeAgents ? "free agents" : app.model.team_name(other).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(xfer::money_short(m.fee).c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", m.kind.c_str());
            }
        }
        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------------------------------- scouts
namespace {
struct ScoutState {
    int gen = -1;
    std::vector<scouts::Scout> list;
    int level = scouts::kBestLevel;
    std::string status;
    bool status_error = false;
    double kill_checked = -100.0;
    bool killed = false;
    bool has_backup = false;
};
ScoutState g_s;
}  // namespace

void scouts_section(App& app) {
    if (!ImGui::CollapsingHeader("Your scouts (career staff)")) return;
    const Table* t = app.db.table("career_scouts");
    if (!t) {
        ImGui::TextDisabled("No career_scouts table in this database (load a Manager Career).");
        return;
    }
    // the kill switch and the backup file are looked at every 2 s, not every frame
    if (app.now - g_s.kill_checked > 2.0) {
        g_s.kill_checked = app.now;
        const fs::path out = app.bridge.dir();
        g_s.killed = scouts::killed(out);
        std::error_code ec;
        g_s.has_backup = fs::exists(scouts::backup_path(out), ec);
    }
    if (g_s.gen != app.gen) {
        g_s.gen = app.gen;
        g_s.list = scouts::read(app.db);
    }
    ImGui::TextWrapped("FC 27 has scouts in the career database and no coaches. Edit a scout's experience and knowledge "
                       "below, or set every scout at once. The originals are saved first (turbo_output\\turbo_scouts_backup.json); "
                       "Restore puts them back.");
    if (g_s.list.empty()) {
        ImGui::TextDisabled("Your career has no scouts yet (hire one in the game's Transfers > Scouting).");
    } else if (ImGui::BeginTable("##scouts", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Experience");
        ImGui::TableSetupColumn("Knowledge");
        ImGui::TableSetupColumn("Region");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < g_s.list.size(); ++i) {
            const scouts::Scout& s = g_s.list[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(s.scoutid));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name().c_str());
            const bool alive = app.db.table_alive(*t, s.rec);
            for (const char* f : {"experience", "knowledge"}) {
                ImGui::TableNextColumn();
                const Field* fl = t->field(f);
                if (!fl || !alive || g_s.killed) {
                    ImGui::Text("%lld", static_cast<long long>(std::string(f) == "experience" ? s.experience : s.knowledge));
                    continue;
                }
                if (field_editor(app, *t, s.rec, *fl, (std::string("##") + f).c_str(), S(70.0f))) ++app.gen;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(s.regionid));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    const fs::path out = app.bridge.dir();
    if (g_s.killed) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Turned off: turbo_output\\scouts_off.txt exists");
        return;
    }
    ImGui::SetNextItemWidth(S(110.0f));
    ImGui::SliderInt("Level##scoutlvl", &g_s.level, 1, scouts::kBestLevel);
    ImGui::SameLine();
    if (g_s.list.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Set every scout##scoutbest")) {
        std::string msg;
        const int n = scouts::make_best(app.db, g_s.level, out, &msg);
        g_s.status = msg;
        g_s.status_error = n == 0 && msg.rfind("Scouts:", 0) != 0;
        ++app.gen;
        g_s.kill_checked = -100.0;
        app.notify(msg, g_s.status_error);
    }
    if (g_s.list.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Experience and knowledge of every scout to this level (5 = the best the game shows)");
    ImGui::SameLine();
    if (!g_s.has_backup) ImGui::BeginDisabled();
    if (ImGui::Button("Restore originals##scoutrestore")) {
        std::string msg;
        const int n = scouts::restore(app.db, out, &msg);
        g_s.status = msg;
        g_s.status_error = n == 0;
        ++app.gen;
        g_s.kill_checked = -100.0;
        app.notify(msg, g_s.status_error);
    }
    if (!g_s.has_backup) ImGui::EndDisabled();
    if (!g_s.status.empty())
        ImGui::TextColored(g_s.status_error ? ImVec4(1.0f, 0.5f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", g_s.status.c_str());
}

// ---------------------------------------------------------------------------------------------------- bulk edit field names
bool bulk_field_pick(const char* id, char* buf, size_t size) {
    static const char* const kFields[][2] = {
        {"releaseclause", "Release clause"}, {"wage", "Wage"}, {"contractvaliduntil", "Contract until (year)"},
        {"potential", "Potential"}, {"internationalrep", "International reputation (1-5)"}, {"skillmoves", "Skill moves (0-4)"},
        {"weakfootabilitytypecode", "Weak foot (1-5)"}, {"isretiring", "Retiring (0 / 1)"}, {"personality", "Personality"}};
    bool picked = false;
    ImGui::SetNextItemWidth(S(70.0f));
    if (ImGui::BeginCombo(id, "Pick", ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_HeightLarge)) {
        for (const auto& f : kFields) {
            if (ImGui::Selectable((std::string(f[1]) + "  (" + f[0] + ")").c_str())) {
                std::snprintf(buf, size, "%s", f[0]);
                picked = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a common players-table field (FC 26 LE's bulk edit fields)");
    return picked;
}

}  // namespace turbo
