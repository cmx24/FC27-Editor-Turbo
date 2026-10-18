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
#include "core/season_pack.h"
#include "file_picker.h"
#include "imgui.h"
#include "move_rules.h"
#include "table_registry.h"
#include "ui_career_page.h"
#include "ui_presets.h"
#include "ui_shell.h"
#include "ui_theme.h"

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
void run_job_body(const std::shared_ptr<CmtJob>& job, json& preset, const fs::path& out_dir, const fs::path& cache_dir,
                  const std::string& base, int64_t id, int db_version, bool want_face) {
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
            bool downloaded = false;
            if (fs::is_regular_file(png, ec)) {
                std::ifstream in(png, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            } else {
                HttpResult r = http_get(url, kMaxPicture);
                if (r.status == 200 && !r.body.empty()) {
                    bytes = std::move(r.body);
                    downloaded = true;   // cached below once it decodes (an error page is never kept as the picture)
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
                    if (downloaded) write_file(png, bytes.data(), bytes.size());
                    std::vector<uint8_t> dds = encode_dds_dxt5(frame_image(img, 256, Framing{}));
                    fs::path dds_file = out_dir / (base + ".dds");
                    if (!dds.empty() && write_file(dds_file, dds.data(), dds.size())) {
                        preset["miniface"] = base + ".dds";
                        note = "miniface downloaded";
                    } else note = "miniface could not be written";
                } else {
                    if (!downloaded) fs::remove(png, ec);   // a bad cached file: the next create downloads it again
                    note = "miniface picture unreadable (" + err + ")";
                }
            }
        }
    }
    fs::path jf = out_dir / (base + ".json");
    // the CSV's text is not checked for UTF-8 (an Excel "CSV" save is ANSI): replace bad bytes instead of throwing
    std::string text = preset.dump(1, ' ', false, json::error_handler_t::replace);
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

// The thread's entry: an exception escaping a detached thread would end the game (std::terminate)
void run_job(std::shared_ptr<CmtJob> job, json preset, fs::path out_dir, fs::path cache_dir, std::string base, int64_t id,
             int db_version, bool want_face) {
    try {
        run_job_body(job, preset, out_dir, cache_dir, base, id, db_version, want_face);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(job->m);
        job->error = std::string("internal error: ") + e.what();
        job->state = 2;
    } catch (...) {
        std::lock_guard<std::mutex> lk(job->m);
        job->error = "internal error";
        job->state = 2;
    }
}

const char* kHowTo =
    "No CMTracker players CSV found yet.\n"
    "1. On cmtracker.net open Players, set the filters you want (league, club, nation, ...).\n"
    "2. Click the site's CSV button; save the file into the folder above (or use Add CSV files...).\n"
    "3. Repeat for the next pages: every file is added, players already there are updated.\n"
    "Then press Reload.";

}  // namespace

namespace {

// The CMTracker library UI state: one set for the "From CMTracker..." dialog of the player page and for Career tools > CMTracker
struct CmtUi {
    CmtLibrary lib;
    bool loaded = false;
    char query[96] = "";
    int64_t selected = 0;
    int teamid = move_rules::kFreeAgents;
    int jersey = 0;
    bool want_face = true, want_real = true;
    FilePicker picker;
    std::shared_ptr<CmtJob> job;
    std::string status;
};

CmtUi& cmt_ui() {
    static CmtUi u;
    return u;
}

// a finished job: hand the preset to Lua's create_player
void cmt_poll_job(App& app) {
    std::shared_ptr<CmtJob>& job = cmt_ui().job;
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
}

// The library list, the search box and the selected player's details: the body of the dialog and of the page
void cmt_body(App& app) {
    CmtUi& u = cmt_ui();
    CmtLibrary& lib = u.lib;
    bool& loaded = u.loaded;
    char(&query)[96] = u.query;
    int64_t& selected = u.selected;
    int& teamid = u.teamid;
    int& jersey = u.jersey;
    bool& want_face = u.want_face;
    bool& want_real = u.want_real;
    FilePicker& picker = u.picker;
    std::string& status = u.status;
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
        for (const auto& pr : lib.problems()) theme::warn_text("skipped %s", pr.c_str());

        if (lib.size() == 0) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", kHowTo);
        } else {
            ImGui::SetNextItemWidth(S(320.0f));
            ImGui::InputTextWithHint("##cmtq", "search: name, club, nation or player id", query, sizeof(query));
            std::vector<const CmtPlayer*> hits = lib.search(query, 120);
            ImGui::BeginChild("##cmtlist", ImVec2(-FLT_MIN, S(150.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
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
                club_picker(app, teamid);
                club_notes(app, teamid);
                form_label("Shirt number", "0 = the first free number at the club");
                ImGui::SetNextItemWidth(S(110.0f));
                ImGui::InputInt("##jersey", &jersey, 0);
                jersey = std::max(0, std::min(jersey, 99));
                checkbox_right("Download the miniface from CMTracker", &want_face);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("One picture request to cmtracker.fra1.cdn.digitaloceanspaces.com for this player; kept in turbo_output\\cmtracker");
                checkbox_right("Use the real face when the game has this player's head", &want_real);
                ImGui::TextDisabled("Hair style, facial hair, boots and tattoos are not in the CSV: change them in Players > Appearance.");
            }
        }
}

// "Create player" is possible: a player of the library is selected, the club exists and no job is running
bool cmt_can_create(App& app) {
    CmtUi& u = cmt_ui();
    const CmtPlayer* sel = u.lib.size() ? u.lib.find(u.selected) : nullptr;
    const bool running = u.job && u.job->state == 1;
    return sel && app.model.team(u.teamid) != nullptr && !running;
}

// Starts the CMTracker job for the selected player (the picture request runs on its own thread; cmt_poll_job hands the
// result to Lua). Call only when cmt_can_create.
void cmt_create(App& app) {
    CmtUi& u = cmt_ui();
    const CmtPlayer* sel = u.lib.find(u.selected);
    if (!sel) return;
    CmtPresetOptions o;
    o.teamid = u.teamid;
    o.jersey = u.jersey;
    o.real_face = u.want_real;
    json preset = cmt_to_preset(*sel, o);
    std::string base = preset_safe_name(sel->name) + "_" + std::to_string(sel->id) + "_cmt";
    u.job = std::make_shared<CmtJob>();
    u.job->state = 1;
    u.job->label = sel->name;
    u.job->teamid = u.teamid;
    u.job->jersey = u.jersey;
    std::thread(run_job, u.job, std::move(preset), players_dir(app), heads_cache(app), base, sel->id, sel->db_version, u.want_face).detach();
}

}  // namespace

void cmtracker_dialog(App& app) {
    cmt_poll_job(app);

    // a window that always fits the screen: the body scrolls, the buttons stay in a footer
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(S(640.0f), disp.x * 0.92f), std::min(S(690.0f), disp.y * 0.9f)), ImGuiCond_Appearing);
    }
    if (ImGui::BeginPopupModal("##pcmt", nullptr)) {
        const float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + S(4.0f);
        ImGui::BeginChild("##cmtbody", ImVec2(0, -footer_h));
        cmt_body(app);
        ImGui::EndChild();
        {
            const bool can = cmt_can_create(app);
            if (!can) ImGui::BeginDisabled();
            if (ImGui::Button("Create player")) {
                cmt_create(app);
                ImGui::CloseCurrentPopup();
            }
            if (!can) ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Season pack...")) ImGui::OpenPopup("##pseasonpack");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Season update pack: transfers and manager changes from a folder, previewed row by row, applied in pages of 50, with Undo "
                              "(not proven in game yet)");
        ImGui::SameLine();
        season_pack_dialog(app);   // a popup of this one: it is drawn here so the button above opens it
        if (ImGui::Button("Close##pcmt")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- Season update pack (core/season_pack.h)
// "Season pack..." (Players tab, next to "From CMTracker..."): pick a pack folder, see every row checked against the live
// career (accepted rows and rejected rows with the reason), apply it one page of at most 50 rows per click (each click asks
// first), and undo a recorded pack. The commands are the ones the Players and Managers tabs already send (player_moves,
// manager_move), one at a time as Lua answers (App::pack_queue). Nothing here touches the network or a save.

namespace {

const char* const kPackNotProven =
    "Not proven in game yet. This importer passed offline tests only (reading the pack, the preview, paging, the undo record). "
    "Whether the game accepts these moves, shows them in the squad hub and takes them back with Undo has not been tried in a "
    "career. Use a throwaway career, back up your saves first, and save nothing you want to keep.";

struct PackUi {
    FilePicker picker;
    fs::path folder;
    season_pack::Pack pack;
    season_pack::Preview pv;
    bool have = false;
    std::string error;
    uint64_t model_version = ~0ull;
    uint64_t sent_at_version = ~0ull;   // model version when the last page went out: the next page waits for a refresh
    bool waiting_refresh = false;
    bool open_apply = false, open_undo = false;
    std::string undo_pack;              // the set the Undo confirmation is about
    season_pack::UndoPlan undo_plan;
};

PackUi& pack_ui() {
    static PackUi p;
    return p;
}

fs::path undo_file(App& app) { return season_pack::undo_store_path(app.bridge.root()); }

void pack_refresh(App& app, PackUi& u) {
    if (!u.have) return;
    u.pv = season_pack::preview(u.pack, season_pack::snapshot_from_model(app.model, app.bridge.state().user_team));
    u.model_version = app.model.version();
}

bool pack_idle(App& app) { return app.mailbox && !app.busy() && app.pack_queue.empty(); }

// Sends the next queued command; a refused send ends the run
void pack_send_next(App& app) {
    if (app.pack_queue.empty()) return;
    json cmd = app.pack_queue.front();
    app.pack_queue.pop_front();
    const std::string label = "Season pack: " + app.pack_kind + " (command " + std::to_string(app.pack_sent + 1) + " of " +
                              std::to_string(app.pack_total) + ")";
    if (!app.send(cmd, label)) {
        app.pack_queue.clear();
        app.pack_failed = true;
        app.pack_status = "Stopped: a command could not be sent (" + std::to_string(app.pack_sent) + " of " + std::to_string(app.pack_total) +
                          " done). Rows already sent are in the undo record.";
    }
}

void pack_start(App& app, const std::string& kind, const std::string& pack_name, const std::vector<json>& sends) {
    app.pack_queue.assign(sends.begin(), sends.end());
    app.pack_kind = kind;
    app.pack_pack = pack_name;
    app.pack_sent = 0;
    app.pack_total = static_cast<int>(sends.size());
    app.pack_failed = false;
    app.pack_status = kind + " running: " + std::to_string(sends.size()) + " command(s)";
    pack_send_next(app);
}

}  // namespace

void season_pack_on_result(App& app, bool ok, const std::string& result) {
    if (!ok) {
        const size_t done = static_cast<size_t>(app.pack_sent);
        app.pack_queue.clear();
        app.pack_failed = true;
        app.pack_status = "Failed after " + std::to_string(done) + " of " + std::to_string(app.pack_total) + " command(s): " +
                          (result.empty() ? std::string("failed") : result.substr(0, result.find('\n'))) +
                          ". Rows already sent are in the undo record; Undo reverses what is still where the pack put it.";
        return;
    }
    ++app.pack_sent;
    if (!app.pack_queue.empty()) {
        pack_send_next(app);
        return;
    }
    app.pack_status = app.pack_kind + " done (" + std::to_string(app.pack_total) + " command(s)): " + result.substr(0, result.find('\n'));
    if (app.pack_kind == "undo") {   // the whole undo ran: the record is spent
        season_pack::UndoStore st;
        std::string err;
        if (season_pack::load_undo_store(undo_file(app), st, &err) && st.remove(app.pack_pack)) season_pack::save_undo_store(undo_file(app), st, &err);
    }
}

bool season_pack_open_folder(App& app, const fs::path& folder, std::string* err) {
    PackUi& u = pack_ui();
    season_pack::Pack pack;
    std::string e;
    if (!season_pack::read_pack(folder, pack, &e)) {
        u.have = false;
        u.error = e;
        if (err) *err = e;
        return false;
    }
    u.folder = folder;
    u.pack = pack;
    u.have = true;
    u.error.clear();
    u.waiting_refresh = false;
    pack_refresh(app, u);
    return true;
}

void season_pack_dialog(App& app) {
    PackUi& u = pack_ui();
    const BridgeState& st = app.bridge.state();
    // after a page the model is rebuilt (Lua changed the database): check the rows again against the new state
    if (u.have && app.model.built() && app.model.version() != u.model_version) pack_refresh(app, u);
    if (u.waiting_refresh && (app.model.version() != u.sent_at_version || app.pack_failed)) u.waiting_refresh = false;

    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(S(760.0f), disp.x * 0.94f), std::min(S(700.0f), disp.y * 0.92f)), ImGuiCond_Appearing);
    }
    if (!ImGui::BeginPopupModal("##pseasonpack", nullptr)) return;
    const float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + S(4.0f);
    ImGui::BeginChild("##packbody", ImVec2(0, -footer_h));
    ImGui::TextUnformatted("Season update pack");
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", kPackNotProven);
    ImGui::TextWrapped("A pack is a folder: pack.json (name, season, source), transfers.csv (playerid, from, to), managers.csv (managerid, to) and "
                       "optional kits\\*.zip. Fees are ignored. Packs apply in pages of %zu rows; every page asks first. Rows that touch your own club "
                       "are left out.",
                       season_pack::kPageRows);

    const bool career = st.in_cm && app.model.built() && st.user_team > 0;
    if (!career) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Load a career first: the rows are checked against it.");
    if (ImGui::Button("Choose pack folder...##packchoose")) {
        u.picker.mode = PickMode::Folder;
        u.picker.title = "Choose a season pack folder (it holds transfers.csv and/or managers.csv)";
        u.picker.exts.clear();
        u.picker.key = "season_pack";
        u.picker.start = app.bridge.root() / "turbo_cmtracker";
        ImGui::OpenPopup("##packbrowse");
    }
    fs::path chosen;
    if (file_picker_modal("##packbrowse", u.picker, chosen)) season_pack_open_folder(app, chosen, nullptr);
    if (u.have) {
        ImGui::SameLine();
        if (ImGui::Button("Check again##packrecheck")) {
            std::string e;
            season_pack_open_folder(app, u.folder, &e);   // re-reads the files too
        }
    }
    if (!u.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", u.error.c_str());

    if (u.have) {
        ImGui::SeparatorText(u.pv.info.name.c_str());
        ImGui::TextDisabled("Folder: %s", path_text(u.folder).c_str());
        if (!u.pv.info.season.empty() || !u.pv.info.source.empty())
            ImGui::TextDisabled("Season: %s   Source: %s", u.pv.info.season.c_str(), u.pv.info.source.c_str());
        for (const auto& n : u.pv.notes) ImGui::TextWrapped("- %s", n.c_str());
        ImGui::Text("%zu row(s) can be applied (%zu page(s)), %zu rejected.", u.pv.accepted, u.pv.pages(), u.pv.rejected);
        if (ImGui::BeginTable("##packrows", 4, tbl::kData | ImGuiTableFlags_ScrollY, ImVec2(0, S(230.0f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, S(110.0f));
            ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, S(40.0f));
            ImGui::TableSetupColumn("Row", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(u.pv.rows.size()));
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                    const season_pack::PreviewRow& r = u.pv.rows[static_cast<size_t>(i)];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.file.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", r.line);
                    ImGui::TableNextColumn();
                    if (r.ok) ImGui::TextUnformatted(r.text.c_str());
                    else ImGui::TextDisabled("%s", r.subject.empty() ? "(row not readable)" : r.subject.c_str());
                    ImGui::TableNextColumn();
                    if (r.ok) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "will apply");
                    else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "rejected: %s", r.reason.c_str());
                }
            }
            ImGui::EndTable();
        }
        const bool can = career && pack_idle(app) && !u.waiting_refresh && u.pv.accepted > 0;
        const size_t this_page = std::min(u.pv.accepted, season_pack::kPageRows);
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Apply next page##packapply")) u.open_apply = true;
        if (!can) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(!career ? "Load a career first" : u.pv.accepted == 0 ? "No row can be applied"
                              : !pack_idle(app) || u.waiting_refresh ? "Wait for the last command to finish" : "Sends the next page of the pack");
        ImGui::SameLine();
        ImGui::TextDisabled("next page: %zu row(s) of %zu left", this_page, u.pv.accepted);
    }

    // recorded packs: Undo
    season_pack::UndoStore store;
    std::string uerr;
    season_pack::load_undo_store(undo_file(app), store, &uerr);
    ImGui::SeparatorText("Undo");
    ImGui::TextDisabled("Undo restores club links and manager teams only, not the join date, previous team or contract the move wrote.");
    if (!uerr.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", uerr.c_str());
    if (store.sets.empty()) ImGui::TextDisabled("No pack applied yet (nothing to undo).");
    for (size_t k = 0; k < store.sets.size(); ++k) {
        const season_pack::UndoSet& s = store.sets[k];
        const bool can = career && pack_idle(app);
        if (!can) ImGui::BeginDisabled();
        const std::string lbl = "Undo this pack##packundo" + std::to_string(k);
        if (ImGui::Button(lbl.c_str())) {
            u.undo_pack = s.pack;
            u.undo_plan = season_pack::make_undo(s, season_pack::snapshot_from_model(app.model, st.user_team));
            u.open_undo = true;
        }
        if (!can) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%s: %zu row(s) sent %s", s.pack.c_str(), s.rows.size(), s.when.c_str());
    }
    if (!app.pack_status.empty()) {
        ImGui::Separator();
        ImGui::TextColored(app.pack_failed ? ImVec4(1.0f, 0.5f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", app.pack_status.c_str());
    }
    ImGui::EndChild();

    // confirmations
    if (u.open_apply) { ImGui::OpenPopup("Apply season pack page?"); u.open_apply = false; }
    if (u.open_undo) { ImGui::OpenPopup("Undo season pack?"); u.open_undo = false; }
    if (ImGui::BeginPopupModal("Apply season pack page?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto pages = season_pack::make_pages(u.pv);
        if (pages.empty()) {
            ImGui::TextUnformatted("Nothing to apply.");
        } else {
            const season_pack::Page& pg = pages[0];
            size_t transfers = 0, managers = 0;
            for (size_t i : pg.rows) (u.pv.rows[i].kind == season_pack::Kind::Manager ? managers : transfers)++;
            ImGui::TextWrapped("Send %zu transfer(s) and %zu manager move(s) of \"%s\" to the career now?", transfers, managers, u.pv.info.name.c_str());
            ImGui::TextWrapped("The moves change the live career database. Nothing is saved by Turbo; the game saves with your career. "
                               "The rows are recorded first so Undo can reverse them.");
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Not proven in game yet.");
            if (ImGui::Button("Apply##packconfirm")) {
                season_pack::UndoStore s2;
                std::string e;
                const fs::path file = undo_file(app);
                if (!season_pack::load_undo_store(file, s2, &e)) {
                    app.notify("Season pack: the undo record could not be read, nothing sent: " + e, true);
                } else {
                    s2.add(season_pack::undo_rows_of(u.pv, pg));
                    if (!season_pack::save_undo_store(file, s2, &e)) {
                        app.notify("Season pack: the undo record could not be written, nothing sent: " + e, true);
                    } else {
                        u.sent_at_version = app.model.version();
                        u.waiting_refresh = true;
                        pack_start(app, "apply", u.pv.info.name, pg.sends);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel##packcancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Undo season pack?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Undo \"%s\": %zu row(s) go back, %zu stay (they moved since).", u.undo_pack.c_str(), u.undo_plan.done.size(), u.undo_plan.skipped.size());
        for (size_t i = 0; i < u.undo_plan.skipped.size() && i < 8; ++i) ImGui::TextDisabled("stays: %s", u.undo_plan.skipped[i].c_str());
        if (u.undo_plan.skipped.size() > 8) ImGui::TextDisabled("... and %zu more", u.undo_plan.skipped.size() - 8);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Not proven in game yet.");
        const bool any = !u.undo_plan.pages.empty();
        if (!any) ImGui::BeginDisabled();
        if (ImGui::Button("Undo##packundoconfirm")) {
            std::vector<json> sends;
            for (const auto& pg : u.undo_plan.pages) sends.insert(sends.end(), pg.sends.begin(), pg.sends.end());
            u.sent_at_version = app.model.version();
            u.waiting_refresh = true;
            pack_start(app, "undo", u.undo_pack, sends);
            ImGui::CloseCurrentPopup();
        }
        if (!any) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##packundocancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::Button("Close##packclose")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}


// ---------------------------------------------------------------- Career tools > CMTracker page (GUI revamp 2.13)
// The library body of the "From CMTracker..." dialog as a page: the same list, search and details, the same one-row-at-a-time
// Create player (the 120-hit list and the CSV-library-only rule are unchanged: nothing here reaches the network except the
// one picture request the checkbox already described). One primary action: Create player. Season pack... opens the importer.
namespace {
void editor_cmtracker(App& app, void*) { cmt_body(app); }
}  // namespace

void draw_cmtracker_page(App& app) {
    cmt_poll_job(app);
    if (ImGui::Button("Season pack...##page")) ImGui::OpenPopup("##pseasonpack");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Season update pack: transfers and manager changes from a folder, previewed row by row, applied in pages of 50, with Undo "
                          "(not tested in a game yet)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Not tested yet");  // 2.16 step 3: the honest tag (plan R8)
    season_pack_dialog(app);  // the importer is a popup: drawn here so the button above opens it
    shell::page::Primary pr;
    pr.label = "Create player";
    pr.enabled = cmt_can_create(app);
    shell::page::frame(app, nullptr, editor_cmtracker, nullptr, &pr);
    if (pr.clicked) cmt_create(app);
}

TURBO_REGISTER_PAGE(shell::Page::CMTracker, draw_cmtracker_page);

}  // namespace turbo
