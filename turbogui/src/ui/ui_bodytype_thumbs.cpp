// FC 27 LE Turbo GUI - the body-type picture maker (see ui_bodytype_thumbs.h)
#include "ui_bodytype_thumbs.h"

#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

#include "app.h"
#include "core/bodytype_thumbs.h"
#include "imgui.h"
#include "ui_bodytypes.h"
#include "ui_sections.h"
#include "ui_theme.h"

namespace turbo {

namespace bt = bodytype_thumbs;

namespace {

struct Maker {
    std::unique_ptr<bt::Generator> gen;
    std::string message;  // last refusal or note for the section
    bool message_error = false;
    int source = 0;  // 0 the full-body camera (or source.json when there is one), 1 another game camera
    int camera = capture::kCameraBody;
    int max_steps = 0;
    std::string proven;  // source.json in words, read at most every 3 s while the section is open
    double proven_at = -1e9;
};
Maker g;

std::string stamp_now() {
    char when[32];
    const std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::localtime(&t);  // render thread only
    if (!tmv || std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tmv) == 0) std::snprintf(when, sizeof(when), "%lld", static_cast<long long>(t));
    return when;
}

// his record in the players table, alive (a career reload frees the old one)
bool player_record(App& app, int32_t pid, const Table*& t, uint64_t& rec, std::string* err) {
    t = app.db.table("players");
    const PlayerRow* p = app.model.player(pid);
    if (!t || !p || !t->has("bodytypecode")) {
        if (err) *err = "player " + std::to_string(pid) + " is not in this database";
        return false;
    }
    rec = p->rec;
    if (!app.db.table_alive(*t, rec)) {
        if (err) *err = "his record moved (the database was reloaded)";
        return false;
    }
    return true;
}

bt::GeneratorEnv make_env(App& app) {
    bt::GeneratorEnv env;
    env.turbo_output = app.turbo_output();
    env.svc = app.capture.get();
    App* a = &app;
    env.read_code = [a](int32_t pid, int64_t* code, std::string* err) {
        const Table* t = nullptr;
        uint64_t rec = 0;
        if (!player_record(*a, pid, t, rec, err)) return false;
        *code = a->db.get_int(*t, rec, "bodytypecode", -1);
        return *code >= 0;
    };
    // RAW: range check, write, read back. No undo step and no kept edit (a crash must not leave a row that writes a test body
    // again at the next career load); the run's manifest keeps his own body type instead.
    env.write_code = [a](int32_t pid, int64_t code, std::string* err) {
        const Table* t = nullptr;
        uint64_t rec = 0;
        if (!player_record(*a, pid, t, rec, err)) return false;
        const Field* f = t->field("bodytypecode");
        if (!f || code < f->min || code > f->max()) {
            if (err) *err = "body type " + std::to_string(code) + " is outside the range this game accepts";
            return false;
        }
        std::string e;
        if (!a->db.set_int(*t, rec, "bodytypecode", code, &e)) {
            if (err) *err = "the database refused it: " + e;
            return false;
        }
        if (a->db.get_int(*t, rec, "bodytypecode", -1) != code) {
            if (err) *err = "the value did not stay";
            return false;
        }
        a->model.refresh_player(pid, a->today());
        ++a->gen;
        return true;
    };
    env.career = [a]() { return a->reapply.career; };
    env.clock = [a]() { return a->now; };
    env.stamp = stamp_now;
    env.log = [a](const std::string& s) {
        a->log(s);
        if (a->log_hook) a->log_hook(s);
    };
    return env;
}

bt::Generator& generator(App& app) {
    if (!g.gen) g.gen = std::make_unique<bt::Generator>(make_env(app));
    return *g.gen;
}

bool reference_of(App& app, int32_t pid, bt::Reference& ref, std::string* err) {
    const Table* t = nullptr;
    uint64_t rec = 0;
    if (!player_record(app, pid, t, rec, err)) return false;
    const PlayerRow* p = app.model.player(pid);
    ref.player = pid;
    ref.team = p->club > 0 ? static_cast<int32_t>(p->club) : -1;
    ref.gender = p->gender;
    ref.head_class = t->has("headclasscode") ? static_cast<int>(app.db.get_int(*t, rec, "headclasscode", -1)) : -1;
    ref.name = p->name;
    return true;
}

void start_run(App& app, const bt::StartRequest& rq, const char* from) {
    std::string err;
    if (!app.connected()) {
        err = "the game's database is not connected";
    } else {
        const int32_t pid = rq.player > 0 ? rq.player : static_cast<int32_t>(app.sel_player);
        bt::Reference ref;
        if (pid <= 0) err = "pick a reference player first";
        else if (reference_of(app, pid, ref, &err)) {
            bt::Generator& gen = generator(app);
            if (gen.start(bodytype_catalog(app), ref, rq, &err)) {
                g.message = std::string("Started (") + from + "): " + gen.status();
                g.message_error = false;
                return;
            }
        }
    }
    g.message = std::string("Not started (") + from + "): " + err;
    g.message_error = true;
    bt::append_log(app.turbo_output(), stamp_now(), "request refused: " + err);
    app.log("body-type pictures: request refused: " + err);
    if (app.log_hook) app.log_hook("body-type pictures: request refused: " + err);
}

}  // namespace

const char* bodytype_thumbs_busy_text() { return bt::generator_running() ? "Making body-type pictures..." : ""; }

// 2.17.0 (G4 blocker C2): a run starts ONLY from the section's button, after a confirmation that names the career and the player.
// No file starts it (a request file left behind could have started the writes in another career).
void tick_bodytype_thumbs(App& app) {
    bt::configure(app.turbo_output());
    if (g.gen && g.gen->running()) g.gen->tick();
}

void bodytype_thumbs_section(App& app) {
    if (!collapsing_section(app, "diag.bodythumbs", "Body-type pictures###diagbodythumbs", false)) return;
    theme::muted_text("Makes the pictures of the body-type grid: the game draws one player once per body type. Use a throwaway "
                      "career that you do not save. Not tested in the game yet.");
    bt::Generator& gen = generator(app);
    const bool running = gen.running();
    const PlayerRow* p = app.sel_player > 0 ? app.model.player(app.sel_player) : nullptr;
    if (running) {
        ImGui::TextWrapped("%s", gen.status().c_str());
        if (ImGui::Button("Stop and give him his own body type back##btthumbstop")) gen.cancel();
    } else {
        ImGui::TextWrapped("Reference player: %s", p ? p->name.c_str() : "pick one in Players first (a real face)");
        if (app.now >= g.proven_at + 3.0) {
            g.proven_at = app.now;
            bt::SourceChoice c;
            g.proven = bt::read_source_file(app.turbo_output(), c) ? c.text() : std::string("the full-body camera");
        }
        ImGui::RadioButton("Full body##btthumbsrc0", &g.source, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The game draws him head to boots: %s", g.proven.c_str());
        ImGui::SameLine();
        ImGui::RadioButton("Another camera##btthumbsrc1", &g.source, 1);
        if (g.source == 1) {
            ImGui::SetNextItemWidth(S(120.0f));
            ImGui::InputInt("Camera##btthumbcamidx", &g.camera, 1, 1);
            if (g.camera < 0) g.camera = 0;
            if (g.camera >= static_cast<int>(capture::cameras().size())) g.camera = static_cast<int>(capture::cameras().size()) - 1;
            ImGui::SameLine();
            ImGui::TextDisabled("%s", capture::cameras()[static_cast<size_t>(g.camera)].label);
        }
        ImGui::SetNextItemWidth(S(120.0f));
        ImGui::InputInt("Only the first (0 = all)##btthumbmax", &g.max_steps, 0, 0);
        if (g.max_steps < 0) g.max_steps = 0;
        ImGui::BeginDisabled(!p);
        if (ImGui::Button("Make the pictures...##btthumbgo")) ImGui::OpenPopup("Make body-type pictures?###btthumbconfirm");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Make body-type pictures?###btthumbconfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string career = !app.career_label.empty() ? app.career_label
                                       : (!app.reapply.career.empty() ? app.reapply.career : std::string("no career identified"));
            ImGui::Text("Career: %s", career.c_str());
            ImGui::Text("Player: %s", p ? p->name.c_str() : "none picked");
            ImGui::TextWrapped("His body type is changed once per body type while the game draws him, and put back at the end. Do this "
                               "only in a throwaway career that you will not save.");
            ImGui::BeginDisabled(!p || app.reapply.career.empty());
            if (ImGui::Button("Yes, make them##btthumbyes")) {
                bt::StartRequest rq;
                rq.player = p ? static_cast<int32_t>(p->playerid) : 0;
                if (g.source == 1) {
                    rq.source_given = true;
                    rq.source.kind = bt::SourceKind::Camera;
                    rq.source.camera = g.camera;
                }
                rq.max_steps = static_cast<size_t>(g.max_steps);
                start_run(app, rq, "button");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel##btthumbno")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Put his own body type back##btthumbback")) {
            std::string err;
            if (gen.put_back_leftover(&err)) {
                g.message = "His own body type is in place.";
                g.message_error = false;
            } else {
                g.message = "Not put back: " + err;
                g.message_error = true;
            }
        }
        if (gen.sequence()) ImGui::TextWrapped("%s", gen.status().c_str());
    }
    if (!g.message.empty()) {
        if (g.message_error) theme::warn_text("%s", g.message.c_str());
        else theme::muted_text("%s", g.message.c_str());
    }
}

}  // namespace turbo
