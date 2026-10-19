// FC 27 LE Turbo GUI - the "My team" preset (see team_preset.h).
#include "team_preset.h"

#include <algorithm>
#include <cstdio>

#include "ui/app.h"

namespace turbo {
namespace teampreset {

using json = nlohmann::json;

const std::array<Field, kFieldCount>& fields() {
    // The order is the order team_mass runs the Lua actions in (contract first, the game-side states last); the jersey fit is the window's
    static const std::array<Field, kFieldCount> f = {{
        {"jersey_fit", "Tight shirts", ""},
        {"contract", "Contract extended by 60 months", "long_contract"},
        {"squad_role", "Squad roles: Rotation, or Prospect for under 23s below the club median", "squad_roles"},
        {"morale", "Very happy morale", "morale"},
        {"block_offers", "Offers for your players blocked", "block_offers"},
    }};
    return f;
}

bool growth_default_extra_xp(const json&) { return kExtraXpDefault; }
bool growth_default_no_decline(const json&) { return kNoDeclineDefault; }

double median_ovr(const std::vector<int>& ovrs) {
    if (ovrs.empty()) return 0.0;
    std::vector<int> v = ovrs;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    if (n % 2 == 1) return static_cast<double>(v[n / 2]);
    return (static_cast<double>(v[n / 2 - 1]) + static_cast<double>(v[n / 2])) / 2.0;
}

int squad_role_for(int age, int ovr, double median) {
    if (static_cast<double>(ovr) >= median) return kRoleRotation;  // at or above the median
    if (age < 0) return kRoleLeaveAlone;                           // below it, age unknown: the rule cannot be judged
    return age < kProspectBelowAge ? kRoleProspect : kRoleRotation;
}

json role_pins(const std::vector<SquadPlayer>& squad) {
    std::vector<int> ovrs;
    ovrs.reserve(squad.size());
    for (const SquadPlayer& p : squad) ovrs.push_back(p.ovr);
    const double med = median_ovr(ovrs);
    json pins = json::object();
    for (const SquadPlayer& p : squad)
        if (p.playerid > 0) pins[std::to_string(p.playerid)] = squad_role_for(p.age, p.ovr, med);
    return pins;
}

json team_mass_overrides(int64_t teamid, const std::vector<SquadPlayer>& squad) {
    json actions = json::array();
    for (const Field& f : fields())
        if (f.action[0]) actions.push_back(f.action);
    json o = {{"teamid", teamid}, {"actions", actions}, {"role_pins", role_pins(squad)}, {"role_save", false}};
    return o;
}

}  // namespace teampreset

bool team_preset_apply(App& app, int64_t teamid) {
    using namespace teampreset;
    const BridgeState& st = app.bridge.state();
    auto refuse = [&](const std::string& why) {
        app.notify("My team preset: " + why, true);
        return false;
    };
    if (!app.connected()) return refuse("the game's database is not loaded");
    if (!st.in_cm) return refuse("needs a loaded Manager Career");
    if (!app.mailbox) return refuse("Turbo's command channel is not available");
    if (teamid <= 0 || teamid != st.user_team) return refuse("it is for your own club only (squad roles and blocked offers exist for your club alone)");
    if (!app.model.team(teamid)) return refuse("your club is not in the database");
    if (app.busy()) return refuse("another command is still running");
    const Table* pt = app.db.table("players");
    const turbo::Field* jf = pt ? pt->field("jerseyfit") : nullptr;

    std::vector<SquadPlayer> squad;
    std::vector<std::pair<int64_t, uint64_t>> recs;  // playerid, players record
    for (const LinkRow& l : app.model.links_of_team(teamid)) {
        const PlayerRow* p = app.model.player(l.playerid);
        if (!p) continue;
        squad.push_back({p->playerid, p->overall, p->age});
        recs.emplace_back(p->playerid, p->rec);
    }
    if (squad.empty()) return refuse("your club has no players");
    const json overrides = team_mass_overrides(teamid, squad);
    const json cmd = {{"op", "run"}, {"module", "team_mass"}, {"overrides", overrides}};
    if (cmd.dump().size() > kMaxCommandChars) return refuse("too many players for one command");

    // 1. the window's own field: tight shirts, through the editor write path (range check, one undo step per player)
    int tight = 0, changed = 0, failed = 0;
    if (!jf || jf->type != FieldType::Int) {
        app.notify("My team preset: this database has no jersey fit field; the other four go ahead", true);
        failed = static_cast<int>(recs.size());
    } else {
        for (const auto& r : recs) {
            if (!app.db.table_alive(*pt, r.second)) {
                ++failed;
                continue;
            }
            if (app.db.get_int(*pt, r.second, "jerseyfit", -1) != kJerseyFitTight) {
                if (app.edit_field(*pt, r.second, *jf, Value::of_int(kJerseyFitTight))) ++changed;
                else ++failed;
            }
            if (app.db.get_int(*pt, r.second, "jerseyfit", -1) == kJerseyFitTight) ++tight;  // read back
        }
    }
    // the lines the Tester's live check reads: App::log only reaches the in-app Log, log_hook reaches turbo_output\turbo_gui.log
    auto say = [&](const std::string& line) {
        app.log(line);
        if (app.log_hook) app.log_hook(line);
    };
    say("My team preset: jersey fit tight on " + std::to_string(tight) + " of " + std::to_string(recs.size()) + " players (" + std::to_string(changed) +
        " changed, " + std::to_string(failed) + " not written)");

    // 2. the four game-side fields: one team_mass command with exactly their actions (the result lands in Mass actions; the Lua side
    //    logs "team_mass done: ..." in Live Editor's log)
    if (!app.send(cmd, label())) return false;
    std::string acts;
    for (const auto& a : overrides["actions"]) acts += (acts.empty() ? "" : ",") + a.get<std::string>();
    int prospects = 0, rotation = 0, alone = 0;
    for (auto it = overrides["role_pins"].begin(); it != overrides["role_pins"].end(); ++it) {
        const int r = it.value().get<int>();
        prospects += r == kRoleProspect;
        rotation += r == kRoleRotation;
        alone += r == kRoleLeaveAlone;
    }
    say("My team preset: sent team_mass for team " + std::to_string(teamid) + " actions=" + acts + " role_save=false role_pins=" +
        std::to_string(overrides["role_pins"].size()) + " (prospect " + std::to_string(prospects) + ", rotation " + std::to_string(rotation) +
        ", left alone " + std::to_string(alone) + ")");
    return true;
}

}  // namespace turbo
