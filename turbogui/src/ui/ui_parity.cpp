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
    std::string session;           // the save it was read for (session, load_gen, db_gen): another save drops it
    long long load_gen = -2, db_gen = -2;
    int gen = -1;                  // app.gen of the database part
    int today = -1;                // game date of the merge (the season filter follows it)
    std::vector<xfer::Move> export_moves, moves;
    std::unordered_map<int64_t, xfer::TeamStats> stats;
    std::string export_name;       // file name of the export read ("" = none)
    int export_date = 0;           // game date in its name (YYYYMMDD)
    int skipped = 0;
    size_t db_rows = 0;
    std::string line;
};
XferState g_x;

// Another save than the export was read for: a career loaded, entered or left (load_gen), another database (db_gen), a new
// game session. Called per Teams row: no allocation.
bool other_save(App& app) {
    const BridgeState& st = app.bridge.state();
    return st.load_gen != g_x.load_gen || st.db_gen != g_x.db_gen || st.session != g_x.session;
}

// The career's game date, invalid outside a career (then no season filter, and the newest export by time)
GameDate career_today(App& app) {
    const BridgeState& st = app.bridge.state();
    return st.in_cm && st.date.valid() ? st.date : GameDate{};
}

void xfer_read_export(App& app) {
    g_x.export_moves.clear();
    g_x.export_name.clear();
    g_x.export_date = 0;
    g_x.skipped = 0;
    const fs::path p = xfer::newest_export(app.bridge.dir(), career_today(app));
    if (!p.empty()) {
        std::string text, err;
        if (read_text_file(p, text, &err)) {
            g_x.export_moves = xfer::parse_export_csv(text, &g_x.skipped);
            g_x.export_name = p.filename().string();
            g_x.export_date = xfer::export_date(p);
        }
    }
    g_x.loaded = true;
    const BridgeState& st = app.bridge.state();
    g_x.session = st.session;
    g_x.load_gen = st.load_gen;
    g_x.db_gen = st.db_gen;
    g_x.gen = -1;  // merge again
}

void xfer_refresh(App& app) {
    // a file name holds only a game date: when another save or career is loaded, the export picked for the last one is dropped
    if (!g_x.loaded || other_save(app)) xfer_read_export(app);
    const GameDate today = career_today(app);
    const int today_int = today.valid() ? today.as_int() : 0;
    if (g_x.gen == app.gen && g_x.today == today_int) return;
    g_x.gen = app.gen;
    g_x.today = today_int;
    const std::vector<xfer::Move> dbm = app.db.ready() ? xfer::read_db_transfers(app.db) : std::vector<xfer::Move>{};
    g_x.db_rows = dbm.size();
    g_x.moves = xfer::this_season(xfer::merge(g_x.export_moves, dbm), today);
    g_x.stats = xfer::per_team(g_x.moves);
    char season[24] = "";
    if (const int from = xfer::season_start(today)) std::snprintf(season, sizeof(season), " in %d/%02d", from / 10000, (from / 10000 + 1) % 100);
    g_x.line = "Transfers: " + std::to_string(g_x.moves.size()) + " moves" + season + " (" +
               (g_x.export_name.empty() ? std::string("no export yet") : "export " + g_x.export_name) + ", database " +
               std::to_string(g_x.db_rows) + ")";
    if (!g_x.export_name.empty() && today_int && g_x.export_date != today_int)
        g_x.line += ". The export is from " + xfer::date_text(g_x.export_date) + ", not today: Export now, then Reload";
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
        ImGui::SetTooltip("Adds In / Out / Spent / Received / Net columns: this season's moves (from 1 July) from Turbo's transfer\n"
                          "history export (Turbo Tools > Exports) plus the database's transfers table. Read only.");
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
    int stars = scouts::kBestLevel + 1;  // the slider shows stars; the level written is stars - 1 (0..4)
    std::string status;
    bool status_error = false;
    double kill_checked = -100.0;
    bool killed = false;
    int backed_up = 0;  // backup entries of your club
};
ScoutState g_s;

// One scout's experience / knowledge: 0..4 (the game's range), written on a pick after the originals are backed up
void scout_level_cell(App& app, const Table& t, const scouts::Scout& s, const char* field, int64_t cur) {
    const Field* fl = t.field(field);
    const int64_t hi = fl ? scouts::level_max(*fl) : scouts::kMaxLevel;
    std::string preview = std::to_string(cur);
    if (cur < 0 || cur > hi) preview += " (out of range)";
    ImGui::SetNextItemWidth(S(70.0f));
    if (ImGui::BeginCombo((std::string("##") + field).c_str(), preview.c_str())) {
        for (int64_t v = 0; v <= hi; ++v) {
            const std::string item = std::to_string(v) + "##" + field + std::to_string(v);
            if (ImGui::Selectable(item.c_str(), v == cur) && v != cur) {
                std::string msg;
                if (scouts::set_level(app.db, s.rec, field, v, app.bridge.dir(), app.bridge.state().user_team, &msg)) {
                    app.log("Scout " + s.name() + ": " + field + " " + std::to_string(cur) + " -> " + std::to_string(v) +
                            "  [career_scouts." + field + " = " + std::to_string(v) + "]");
                    ++app.gen;
                    g_s.kill_checked = -100.0;
                } else {
                    app.notify(msg, true);
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0 to %lld (the game's range; INFERRED: the game shows the value + 1 stars).\n"
                          "The first change saves your scouts' originals for Restore.",
                          static_cast<long long>(hi));
}
}  // namespace

void scouts_section(App& app) {
    if (!ImGui::CollapsingHeader("Your scouts (career staff)")) return;
    const Table* t = app.db.table("career_scouts");
    if (!t) {
        ImGui::TextDisabled("No career_scouts table in this database (load a Manager Career).");
        return;
    }
    const int64_t team = app.bridge.state().user_team;
    // the kill switch and the backup file are looked at every 2 s, not every frame
    if (app.now - g_s.kill_checked > 2.0) {
        g_s.kill_checked = app.now;
        const fs::path out = app.bridge.dir();
        g_s.killed = scouts::killed(out);
        g_s.backed_up = scouts::backed_up(out, team);
    }
    if (g_s.gen != app.gen) {
        g_s.gen = app.gen;
        g_s.list = scouts::read(app.db);
    }
    ImGui::TextWrapped("FC 27 has scouts in the career database and no coaches. Edit a scout's experience and knowledge (0 to 4, "
                       "the game's range) below, or set every scout at once. Your scouts' originals are saved first, per club "
                       "(turbo_output\\turbo_scouts_backup.json); Restore puts them back.");
    if (g_s.list.empty()) {
        ImGui::TextDisabled("Your career has no scouts yet (hire one in the game's Transfers > Scouting).");
    } else if (ImGui::BeginTable("##scouts", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Experience (0-4)");
        ImGui::TableSetupColumn("Knowledge (0-4)");
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
                const int64_t cur = std::string(f) == "experience" ? s.experience : s.knowledge;
                if (!t->field(f) || !alive || g_s.killed) {
                    ImGui::Text("%lld", static_cast<long long>(cur));
                    continue;
                }
                scout_level_cell(app, *t, s, f, cur);
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
    ImGui::SliderInt("##scoutlvl", &g_s.stars, 1, scouts::kMaxLevel + 1, "%d stars");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stars = level + 1: the database stores 0 to 4 (5 stars = level 4, the best).\n"
                          "INFERRED from the game's 0..4 range; check the game's Scouting screen.");
    ImGui::SameLine();
    if (g_s.list.empty()) ImGui::BeginDisabled();
    if (ImGui::Button("Set every scout##scoutbest")) {
        std::string msg;
        const int n = scouts::make_best(app.db, g_s.stars - 1, out, team, &msg);
        g_s.status = msg;
        g_s.status_error = n == 0 && msg.rfind("Scouts:", 0) != 0;
        ++app.gen;
        g_s.kill_checked = -100.0;
        app.notify(msg, g_s.status_error);
    }
    if (g_s.list.empty()) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Experience and knowledge of every scout to this level (stars - 1, 0 to 4)");
    ImGui::SameLine();
    const bool no_backup = g_s.backed_up == 0;
    if (no_backup) ImGui::BeginDisabled();
    if (ImGui::Button("Restore originals##scoutrestore")) {
        std::string msg;
        const int n = scouts::restore(app.db, out, team, &msg);
        g_s.status = msg;
        g_s.status_error = n == 0;
        ++app.gen;
        g_s.kill_checked = -100.0;
        app.notify(msg, g_s.status_error);
    }
    if (no_backup) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (no_backup) ImGui::SetTooltip("No originals saved for this club's scouts");
        else ImGui::SetTooltip("Writes back the originals saved for this club (%d scouts)", g_s.backed_up);
    }
    if (!g_s.status.empty())
        ImGui::TextColored(g_s.status_error ? ImVec4(1.0f, 0.5f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", g_s.status.c_str());
}

// ---------------------------------------------------------------------------------------------------- bulk edit field names
namespace {
std::unordered_map<std::string, char*> g_pick_bufs;  // combo id -> the field-name box it fills (for the tests)
}  // namespace

char* bulk_field_buffer(const char* id) {
    const auto it = g_pick_bufs.find(id);
    return it == g_pick_bufs.end() ? nullptr : it->second;
}

bool bulk_field_pick(const char* id, char* buf, size_t size) {
    g_pick_bufs[id] = buf;
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
