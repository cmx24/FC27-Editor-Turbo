// FC 27 LE Turbo GUI - Turbo X: the gear picture maker (see ui_gear_thumbs.h)
#include "ui_gear_thumbs.h"

#include <cstdio>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app.h"
#include "core/bodytype_thumbs.h"
#include "core/gear_thumbs.h"
#include "core/t3db.h"
#include "core/hairbeard_thumbs.h"
#include "imgui.h"
#include "ui_bodytype_thumbs.h"
#include "ui_faces.h"
#include "ui_faces3d_gate.h"
#include "ui_theme.h"

namespace turbo {

// Turbo X: a step with field "tattoo" takes the body area the save's tattoo table names for that id (arms and legs first: a shirt
// hides the chest and back); a tattoo id the table does not know keeps "tattoo" and fails at start with the reason.
static void resolve_tattoo_steps(App& app, gear_thumbs::Request& rq) {
    bool any = false;
    for (const gear_thumbs::Step& s : rq.steps) any = any || s.field == "tattoo";
    if (!any) return;
    const Table* t = app.db.table("tattoo");
    if (!t || !t->has("tattooid")) return;
    Snapshot snap;
    if (!snap.load(app.db.memory(), *t)) return;
    static const char* kOrder[] = {"tattooleftarm", "tattoorightarm", "tattooleftleg", "tattoorightleg", "tattoohead", "tattoofront", "tattooback"};
    std::map<int64_t, std::string> area;
    const Field* idf = t->field("tattooid");
    for (uint32_t idx : snap.valid) {
        const int64_t id = snap.get_int(idx, *idf);
        if (id <= 0 || area.count(id)) continue;
        for (const char* a : kOrder) {
            const Field* af = t->field(a);
            if (af && snap.get_int(idx, *af) != 0) {
                area[id] = a;
                break;
            }
        }
    }
    for (gear_thumbs::Step& s : rq.steps)
        if (s.field == "tattoo") {
            const auto it = area.find(s.id);
            if (it != area.end()) s.field = it->second;
        }
}


namespace gt = gear_thumbs;

namespace {

struct Maker {
    std::unique_ptr<gt::Generator> gen;
    capture::CaptureService* svc = nullptr;
    std::string message;
    bool message_error = false;
    double polled_at = -1e9;
};
Maker g;

std::string stamp_now() {
    char when[32];
    const std::time_t t = std::time(nullptr);
    const std::tm* tmv = std::localtime(&t);  // render thread only
    if (!tmv || std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tmv) == 0) std::snprintf(when, sizeof(when), "%lld", static_cast<long long>(t));
    return when;
}

// his record in the players table, alive (a career reload frees the old one), with the column
bool player_record(App& app, int32_t pid, const std::string& field, const Table*& t, uint64_t& rec, std::string* err) {
    t = app.db.table("players");
    const PlayerRow* p = app.model.player(pid);
    if (!t || !p) {
        if (err) *err = "player " + std::to_string(pid) + " is not in this database";
        return false;
    }
    if (!field.empty() && !t->has(field)) {
        if (err) *err = "the players table has no " + field;
        return false;
    }
    rec = p->rec;
    if (!app.db.table_alive(*t, rec)) {
        if (err) *err = "his record moved (the database was reloaded)";
        return false;
    }
    return true;
}

gt::GeneratorEnv make_env(App& app) {
    gt::GeneratorEnv env;
    env.turbo_output = app.turbo_output();
    env.svc = app.capture.get();
    App* a = &app;
    env.read_field = [a](int32_t pid, const std::string& field, int64_t* v, std::string* err) {
        const Table* t = nullptr;
        uint64_t rec = 0;
        if (!player_record(*a, pid, field, t, rec, err)) return false;
        *v = a->db.get_int(*t, rec, field, -1);
        return *v >= 0;
    };
    // RAW like the body-type run: range check, write, read back; no undo step and no kept edit (own.json keeps his own values)
    env.write_field = [a](int32_t pid, const std::string& field, int64_t v, std::string* err) {
        const Table* t = nullptr;
        uint64_t rec = 0;
        if (!player_record(*a, pid, field, t, rec, err)) return false;
        const Field* f = t->field(field);
        if (!f || v < f->min || v > f->max()) {
            if (err) *err = std::to_string(v) + " is outside the range of " + field;
            return false;
        }
        std::string e;
        if (!a->db.set_int(*t, rec, field, v, &e)) {
            if (err) *err = "the database refused it: " + e;
            return false;
        }
        if (a->db.get_int(*t, rec, field, -1) != v) {
            if (err) *err = "the value did not stay";
            return false;
        }
        a->model.refresh_player(pid, a->today());
        ++a->gen;
        return true;
    };
    env.check = [a](const gt::FieldSpec& fs_, int64_t id, std::string* why) {
        const Table* t = a->db.table("players");
        const Field* f = t ? t->field(fs_.db) : nullptr;
        if (!f) {
            if (why) *why = std::string("the players table has no ") + fs_.db;
            return false;
        }
        if (id < f->min || id > f->max()) {
            if (why) *why = "outside the range this game accepts (" + std::to_string(f->min) + ".." + std::to_string(f->max()) + ")";
            return false;
        }
        return true;
    };
    env.clock = [a]() { return a->now; };
    env.stamp = stamp_now;
    env.log = [a](const std::string& s) {
        a->log(s);
        if (a->log_hook) a->log_hook(s);
    };
    return env;
}

gt::Generator& generator(App& app) {
    if (g.gen && !g.gen->running() && g.svc != app.capture.get()) g.gen.reset();
    if (!g.gen) {
        g.svc = app.capture.get();
        g.gen = std::make_unique<gt::Generator>(make_env(app));
    }
    return *g.gen;
}

bool capture_in_flight(App& app) {
    if (faces3d_preview_pending() || faces3d_running()) return true;
    if (bodytype_thumbs::generator_running() || hairbeard_thumbs::generator_running()) return true;
    return app.capture && app.capture->status().busy;
}

void say(App& app, const std::string& m, bool error) {
    g.message = m;
    g.message_error = error;
    if (error) gt::append_log(app.turbo_output(), stamp_now(), m);
    app.log("gear pictures: " + m);
    if (app.log_hook) app.log_hook("gear pictures: " + m);
}

void start_run(App& app, const gt::Request& rq, const std::vector<std::string>& bad, const char* from) {
    for (const std::string& b : bad) gt::append_log(app.turbo_output(), stamp_now(), "failed: " + b);
    std::string err;
    if (!app.connected() || app.reapply.career.empty()) err = "no career is loaded";
    else if (!rq.career.empty() && rq.career != app.reapply.career) err = "the request is for career \"" + rq.career + "\", this one is \"" + app.reapply.career + "\"";
    if (err.empty()) {
        std::vector<int32_t> pids = rq.players;
        if (pids.empty()) pids.push_back(rq.player > 0 ? rq.player : static_cast<int32_t>(app.sel_player));
        std::vector<gt::Reference> refs;
        for (int32_t pid : pids) {
            const Table* t = nullptr;
            uint64_t rec = 0;
            if (pid <= 0) {
                err = "pick a reference player first";
                break;
            }
            if (!player_record(app, pid, std::string(), t, rec, &err)) break;
            const PlayerRow* p = app.model.player(pid);
            gt::Reference ref;
            ref.player = pid;
            ref.team = p->club > 0 ? static_cast<int32_t>(p->club) : -1;
            ref.head_class = t->has("headclasscode") ? static_cast<int>(app.db.get_int(*t, rec, "headclasscode", -1)) : -1;
            ref.name = p->name;
            refs.push_back(ref);
        }
        if (err.empty()) {
            gt::Generator& gen = generator(app);
            if (gen.start(rq, refs, &err)) {
                say(app, std::string("started (") + from + "): " + gen.status(), false);
                return;
            }
        }
    }
    say(app, std::string("not started (") + from + "): " + err, true);
}

}  // namespace

const char* gear_thumbs_busy_text() { return gt::generator_running() ? "Making pictures..." : ""; }

void tick_gear_thumbs(App& app) {
    if (g.gen && g.gen->running()) {
        g.gen->tick();
        return;
    }
    if (app.now - g.polled_at < 1.0) return;
    g.polled_at = app.now;
    if (!app.connected() || app.reapply.career.empty() || capture_in_flight(app)) return;
    const auto t = app.turbo_output();
    std::error_code ec;
    if (!std::filesystem::exists(gt::request_file(t), ec)) return;
    gt::Request rq;
    std::string err;
    std::vector<std::string> bad;
    if (!gt::take_request(t, rq, &err, &bad)) {
        say(app, "request refused: " + err, true);
        return;
    }
    resolve_tattoo_steps(app, rq);
    start_run(app, rq, bad, "request file");
}


void gear_thumbs_button(App& app, int32_t player) {
    gt::Generator& gen = generator(app);
    if (gen.running()) {
        theme::muted_text("%s", gen.status().c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel###gearthumbscancel")) gen.cancel();
        return;
    }
    const PlayerRow* p = player > 0 ? app.model.player(player) : nullptr;
    const bool busy = capture_in_flight(app);
    std::error_code ec;
    const bool have_want = std::filesystem::exists(gt::want_file(app.turbo_output()), ec);
    ImGui::BeginDisabled(!p || busy || !have_want);
    if (ImGui::SmallButton("Make missing pictures###gearthumbsgo")) ImGui::OpenPopup("Make missing pictures?###gearthumbsconfirm");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (busy) ImGui::SetTooltip("Still drawing the last picture...");
        else if (!have_want) ImGui::SetTooltip("No list of missing pictures yet.");
        else if (!p) ImGui::SetTooltip("Pick a player first: the game draws him once per item.");
        else ImGui::SetTooltip("The game draws %s once per item without a picture. Use a throwaway career.", p->name.c_str());
    }
    if (ImGui::BeginPopupModal("Make missing pictures?###gearthumbsconfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string career = !app.career_label.empty() ? app.career_label
                                   : (!app.reapply.career.empty() ? app.reapply.career : std::string("no career identified"));
        ImGui::Text("Career: %s", career.c_str());
        ImGui::Text("Player: %s", p ? p->name.c_str() : "none picked");
        ImGui::TextUnformatted("His look changes once per item, then goes back. Throwaway career only.");
        theme::muted_text("The list's own players are used when it names them.");
        ImGui::BeginDisabled(!p || app.reapply.career.empty());
        if (ImGui::Button("Yes, make them###gearthumbsyes")) {
            gt::Request rq;
            std::string err;
            std::vector<std::string> bad;
            if (gt::read_want(app.turbo_output(), rq, &err, &bad)) {
                rq.player = p ? static_cast<int32_t>(p->playerid) : 0;  // want.json's "players" (if any) still wins: batches of up to 6
                start_run(app, rq, bad, "button");
            } else {
                say(app, "not started: " + err, true);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel###gearthumbsno")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!g.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        if (g.message_error) theme::warn_text("%s", g.message.c_str());
        else theme::muted_text("%s", g.message.c_str());
        ImGui::PopTextWrapPos();
    }
}

}  // namespace turbo
