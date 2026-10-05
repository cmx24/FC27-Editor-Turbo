// FC 27 LE Turbo GUI - "From CMTracker..." (Players tab): pick a player of the CMTracker CSV library (by name, club or id),
// pick a club, and Turbo creates the fully populated player: attributes, positions, PlayStyles, appearance codes, contract,
// names, real face when the game has the head, and the miniface downloaded from CMTracker's picture host.
//
// The library is every players CSV in <Live Editor>\turbo_cmtracker (the CSV button of cmtracker.net/players, 50 rows per
// page; any number of files, merged by player id). The only network request is the miniface picture of the one player
// created, and only when the box is ticked. The rest is Lua's create_player (validated, rolled back on failure).
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

#include "app.h"
#include "core/cmtracker.h"
#include "core/http.h"
#include "core/image.h"
#include "file_picker.h"
#include "imgui.h"
#include "move_rules.h"
#include "ui_presets.h"

namespace turbo {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A create in progress: the picture is fetched and the files are written on a worker thread, the command is sent by the
// render thread when the job is done (ImGui and the bridge are never touched from the worker)
struct CmtJob {
    std::atomic<int> state{0};   // 1 running, 2 finished
    std::mutex m;
    std::string label;           // player name
    std::string note;            // what happened to the miniface
    std::string error;           // non-empty: nothing was written
    fs::path json_file;
    int64_t teamid = 0;
    int jersey = 0;
};

constexpr size_t kMaxPicture = 4u * 1024u * 1024u;

fs::path library_dir(App& app) { return app.bridge.root() / "turbo_cmtracker"; }
fs::path players_dir(App& app) { return app.bridge.dir() / "players"; }
fs::path heads_cache(App& app) { return app.bridge.dir() / "cmtracker" / "heads"; }

bool write_file(const fs::path& p, const void* data, size_t n) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
    return static_cast<bool>(out);
}

// Worker: miniface (cached PNG, else download), 256 x 256 DXT5 like FC 27's own, then the preset JSON
void run_job(std::shared_ptr<CmtJob> job, json preset, fs::path out_dir, fs::path cache_dir, std::string base, int64_t id,
             int db_version, bool want_face) {
    std::string note;
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        std::lock_guard<std::mutex> lk(job->m);
        job->error = "cannot create " + path_text(out_dir);
        job->state = 2;
        return;
    }
    if (want_face) {
        std::string url = cmt_head_url(id, db_version);
        if (url.empty()) {
            note = "no miniface address in the CSV";
        } else {
            fs::create_directories(cache_dir, ec);
            fs::path png = cache_dir / ("p" + std::to_string(id) + "_" + std::to_string(db_version) + ".png");
            std::vector<uint8_t> bytes;
            if (fs::is_regular_file(png, ec)) {
                std::ifstream in(png, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            } else {
                HttpResult r = http_get(url, kMaxPicture);
                if (r.status == 200 && !r.body.empty()) {
                    bytes = std::move(r.body);
                    write_file(png, bytes.data(), bytes.size());
                } else if (r.status == 403 || r.status == 404) {
                    note = "CMTracker has no miniface picture for this player";
                } else {
                    note = "miniface not downloaded (" + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error) + ")";
                }
            }
            if (!bytes.empty()) {
                Rgba img;
                std::string err;
                if (decode_image(bytes, img, &err)) {
                    std::vector<uint8_t> dds = encode_dds_dxt5(frame_image(img, 256, Framing{}));
                    fs::path dds_file = out_dir / (base + ".dds");
                    if (!dds.empty() && write_file(dds_file, dds.data(), dds.size())) {
                        preset["miniface"] = base + ".dds";
                        note = "miniface downloaded";
                    } else note = "miniface could not be written";
                } else note = "miniface picture unreadable (" + err + ")";
            }
        }
    }
    fs::path jf = out_dir / (base + ".json");
    std::string text = preset.dump(1);
    if (!write_file(jf, text.data(), text.size())) {
        std::lock_guard<std::mutex> lk(job->m);
        job->error = "cannot write " + path_text(jf);
        job->state = 2;
        return;
    }
    std::lock_guard<std::mutex> lk(job->m);
    job->json_file = jf;
    job->note = note;
    job->state = 2;
}

const char* kHowTo =
    "No CMTracker players CSV found yet.\n"
    "1. On cmtracker.net open Players, set the filters you want (league, club, nation, ...).\n"
    "2. Click the site's CSV button; save the file into the folder above (or use Add CSV files...).\n"
    "3. Repeat for the next pages: every file is added, players already there are updated.\n"
    "Then press Reload.";

}  // namespace

void cmtracker_dialog(App& app) {
    static CmtLibrary lib;
    static bool loaded = false;
    static char query[96] = "";
    static int64_t selected = 0;
    static int teamid = move_rules::kFreeAgents;
    static char club_search[64] = "";
    static int jersey = 0;
    static bool want_face = true, want_real = true;
    static FilePicker picker;
    static std::shared_ptr<CmtJob> job;
    static std::string status;

    // a finished job: hand the preset to Lua's create_player
    if (job && job->state == 2) {
        std::shared_ptr<CmtJob> j = job;
        job.reset();
        std::lock_guard<std::mutex> lk(j->m);
        if (!j->error.empty()) {
            app.notify("From CMTracker: " + j->error, true);
        } else {
            json o = {{"source", {{"file", path_text(j->json_file)}}}, {"teamid", j->teamid}, {"jersey", j->jersey}};
            app.send({{"op", "run"}, {"module", "create_player"}, {"overrides", o}}, "Create player from CMTracker: " + j->label);
            if (!j->note.empty()) app.notify(j->label + ": " + j->note);
            app.log("CMTracker: " + j->label + " -> team " + std::to_string(j->teamid) + "; " + (j->note.empty() ? "no miniface requested" : j->note));
        }
    }

    // a window that always fits the screen: the body scrolls, the buttons stay in a footer
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(S(640.0f), disp.x * 0.92f), std::min(S(690.0f), disp.y * 0.9f)), ImGuiCond_Appearing);
    }
    if (ImGui::BeginPopupModal("##pcmt", nullptr)) {
        const float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + S(4.0f);
        ImGui::BeginChild("##cmtbody", ImVec2(0, -footer_h));
        fs::path dir = library_dir(app);
        if (!loaded) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            lib.load(dir);
            loaded = true;
        }
        ImGui::TextUnformatted("Create a player from the CMTracker library");
        ImGui::TextDisabled("Library folder: %s", path_text(dir).c_str());
        ImGui::TextDisabled("%zu players from %zu CSV file(s)", lib.size(), lib.files());
        ImGui::SameLine();
        if (ImGui::Button("Reload")) { lib.load(dir); selected = 0; }
        ImGui::SameLine();
        if (ImGui::Button("Add CSV files...")) {
            picker.mode = PickMode::Open;
            picker.title = "Choose a CMTracker players CSV (copied into the library folder)";
            picker.exts = {".csv"};
            picker.key = "cmtracker.csv";
            picker.start = app.bridge.root();
            ImGui::OpenPopup("##pcmtbrowse");
        }
        fs::path chosen;
        if (file_picker_modal("##pcmtbrowse", picker, chosen)) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            fs::path dest = dir / chosen.filename();
            if (fs::equivalent(chosen, dest, ec)) { ec.clear(); }
            else fs::copy_file(chosen, dest, fs::copy_options::overwrite_existing, ec);
            lib.load(dir);
            status = ec ? "Could not copy the file: " + ec.message() : "Added " + path_text(chosen.filename());
        }
        if (!status.empty()) ImGui::TextDisabled("%s", status.c_str());
        for (const auto& pr : lib.problems()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "skipped %s", pr.c_str());

        if (lib.size() == 0) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", kHowTo);
        } else {
            ImGui::SetNextItemWidth(S(320.0f));
            ImGui::InputTextWithHint("##cmtq", "search: name, club, nation or player id", query, sizeof(query));
            std::vector<const CmtPlayer*> hits = lib.search(query, 120);
            ImGui::BeginChild("##cmtlist", ImVec2(-FLT_MIN, S(150.0f)), ImGuiChildFlags_Borders);
            for (const CmtPlayer* h : hits) {
                char lbl[256];
                std::snprintf(lbl, sizeof(lbl), "%-3d %-3d %-4s  %s  -  %s  (%lld)##c%lld", h->overall, h->potential, h->position.c_str(),
                              h->name.c_str(), h->club.c_str(), static_cast<long long>(h->id), static_cast<long long>(h->id));
                if (ImGui::Selectable(lbl, selected == h->id)) {
                    selected = h->id;
                    teamid = (h->club_id > 0 && app.model.team(h->club_id) && !app.model.is_national_team(h->club_id))
                                 ? static_cast<int>(h->club_id) : move_rules::kFreeAgents;
                    jersey = 0;
                }
            }
            if (hits.empty()) ImGui::TextDisabled("no player matches");
            ImGui::EndChild();
            ImGui::TextDisabled("OVR POT POS  name - club (CMTracker id)");

            const CmtPlayer* sel = lib.find(selected);
            if (sel) {
                ImGui::SeparatorText(sel->name.c_str());
                auto c = [&](const char* k) { auto it = sel->cols.find(k); return it == sel->cols.end() ? std::string() : it->second; };
                ImGui::Text("Age %d, %s, %s cm / %s kg, %s foot, %s skill moves, %s weak foot", sel->age, sel->nation.c_str(),
                            c("info.height").c_str(), c("info.weight").c_str(), c("info.preferredfoot").c_str(),
                            c("info.skillmoves").c_str(), c("info.weafoot").c_str());
                ImGui::Text("%s  |  %s", sel->position.c_str(), c("other_positions").c_str());
                std::string ps = c("info.traits.trait1");
                if (!c("info.traits.trait2").empty()) ps += (ps.empty() ? "" : ", ") + c("info.traits.trait2");
                if (!ps.empty()) ImGui::TextWrapped("PlayStyles: %s", ps.c_str());
                ImGui::TextDisabled("Real face on CMTracker: %s. Contract to %s.", c("info.real_face").c_str(),
                                    c("info.contract.enddate").c_str());
                ImGui::SeparatorText("Club");
                club_picker(app, teamid, club_search, sizeof(club_search));
                club_notes(app, teamid);
                ImGui::SetNextItemWidth(S(110.0f));
                ImGui::InputInt("Shirt number (0 = first free)", &jersey, 0);
                jersey = std::max(0, std::min(jersey, 99));
                ImGui::Checkbox("Download the miniface from CMTracker", &want_face);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("One picture request to cmtracker.fra1.cdn.digitaloceanspaces.com for this player; kept in turbo_output\\cmtracker");
                ImGui::Checkbox("Use the real face when the game has this player's head", &want_real);
                ImGui::TextDisabled("Hair style, facial hair, boots and tattoos are not in the CSV: change them in Players > Appearance.");
            }
        }
        ImGui::EndChild();
        {
            const CmtPlayer* sel = lib.size() ? lib.find(selected) : nullptr;
            bool running = job && job->state == 1;
            bool can = sel && app.model.team(teamid) != nullptr && !running;
            if (!can) ImGui::BeginDisabled();
            if (ImGui::Button("Create player")) {
                CmtPresetOptions o;
                o.teamid = teamid;
                o.jersey = jersey;
                o.real_face = want_real;
                json preset = cmt_to_preset(*sel, o);
                std::string base = preset_safe_name(sel->name) + "_" + std::to_string(sel->id) + "_cmt";
                job = std::make_shared<CmtJob>();
                job->state = 1;
                job->label = sel->name;
                job->teamid = teamid;
                job->jersey = jersey;
                std::thread(run_job, job, std::move(preset), players_dir(app), heads_cache(app), base, sel->id, sel->db_version, want_face)
                    .detach();
                ImGui::CloseCurrentPopup();
            }
            if (!can) ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Close##pcmt")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace turbo
