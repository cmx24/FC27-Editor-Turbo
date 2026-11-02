// FC 27 LE Turbo GUI - X 2.0.2 club identity scope: where a club NAME / CREST / KIT edit lives.
//
// Cassio's rule (2026-10-18, restated 2026-11-01): an edit made with no career loaded belongs to the squads file and is GLOBAL (every
// save, Kick Off, the main menu); an edit made in a career lives in THAT career only. Example: the Italian clubs' names, crests and
// kits are global; every other name + crest + kit is save-specific.
//
//   name   : turbo_output\team_names.json entry scope "global" / "career:<id>" (the live team-names hook shows the ones in effect);
//            a global name also goes into Live Editor's custom_team_names.csv, a career name is taken out of it (else Live
//            Editor would show it in every save)
//   crest  : a global crest is the file in mods\legacy; a career crest is kept in turbo_output\careers\<id>\crest...\ and copied into
//            mods\legacy only while that career is identified (core/career_faces.h, the face pictures' route)
//   minikit: as the crest (careers\<id>\kits\...)
//   kit    : the kept kit colours / settings rows (reapply_edits.json) with the same scope; a 3D kit (kit swap) carries it in
//            swap_state.json and is in the table only in its career (or everywhere when global)
//
// Panel (Club & crest > Identity, Turbo settings > Club edits): every club with an identity edit, where each part lives, Global /
// This career, and "Italian clubs: global, the rest: this career".
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

#include "app.h"
#include "core/legacy.h"
#include "core/reapply.h"
#include "core/teamnames.h"
#include "imgui.h"
#include "table_registry.h"

namespace turbo {

namespace fs = std::filesystem;

// ---------------------------------------------------------------- the scope of an identity edit made now
std::string App::identity_scope(std::string* refuse) const {
    if (refuse) refuse->clear();
    if (!bridge.state().in_cm) return kScopeGlobal;  // the squads file: every save
    const std::string c = career_faces_wanted();
    if (!c.empty()) return career_scope(c);
    if (refuse)
        *refuse = "Turbo has not identified this career yet (" + (career_identity_line.empty() ? std::string("not read yet") : career_identity_line) +
                  "): a club edit made now could not be kept with it, so none was made";
    return {};
}

std::string App::identity_kept_scope() const {
    if (!bridge.state().in_cm) return kScopeGlobal;
    return reapply.edit_scope();  // this career's, or "" (not assigned) while it is not identified: as before
}

bool App::write_identity_file(const std::string& rel, const std::vector<uint8_t>& bytes, std::string* err, std::string* where, fs::path* backup) {
    if (where) where->clear();
    if (backup) backup->clear();
    std::string refuse;
    const std::string scope = identity_scope(&refuse);
    if (scope.empty()) {
        if (err) *err = refuse;
        return false;
    }
    const career_faces::Paths cp = career_faces_paths();
    if (scope == kScopeGlobal) {
        if (!career_faces::record_global(cp, rel, std::string(bytes.begin(), bytes.end()), err)) return false;
        if (!legacy.save_custom(rel, bytes, err, backup)) {
            career_faces::forget_global(cp, rel, nullptr);
            return false;
        }
        textures.forget(legacy.custom_file(rel));
        if (where) *where = "global";
        return true;
    }
    const std::string career = scope_career(scope);
    fs::path undo;
    if (!career_faces::save_overlay(cp, career, rel, bytes, err, &undo)) return false;
    if (backup) *backup = undo;
    career_faces_sync(career, "a club identity file written in this career");
    textures.forget(career_faces::mods_file(cp, rel));
    if (where) *where = "this career";
    return true;
}

bool App::undo_identity_file(const std::string& rel, const std::string& scope, const std::string& backup, std::string* err) {
    const std::string career = scope_career(scope);
    if (career.empty()) {
        if (err) *err = "not a career scope";
        return false;
    }
    const career_faces::Paths cp = career_faces_paths();
    if (!career_faces::restore_overlay(cp, career, rel, backup.empty() ? fs::path() : fs::u8path(backup), err)) return false;
    career_faces_sync(career_faces_wanted(), "undo of a club identity file");
    textures.forget(career_faces::mods_file(cp, rel));
    return true;
}

bool App::remove_identity_file(const std::string& rel, std::string* err, bool* removed) {
    if (removed) *removed = false;
    std::string refuse;
    const std::string scope = identity_scope(&refuse);
    if (scope.empty()) {
        if (err) *err = refuse;
        return false;
    }
    const career_faces::Paths cp = career_faces_paths();
    if (scope == kScopeGlobal) {
        const fs::path old = legacy.custom_file(rel);
        if (old.empty()) return true;
        if (!legacy.remove_custom(rel, err)) return false;
        career_faces::forget_global(cp, rel, nullptr);
        textures.forget(old);
        if (removed) *removed = true;
        return true;
    }
    const std::string career = scope_career(scope);
    if (career_faces::has_overlay(cp, career, rel)) {
        fs::path undo;
        if (!career_faces::remove_overlay(cp, career, rel, err, &undo)) return false;
        career_faces_sync(career, "a club identity file removed in this career");
        textures.forget(career_faces::mods_file(cp, rel));
        if (removed) *removed = true;
        return true;
    }
    if (!legacy.custom_file(rel).empty()) {
        if (err)
            *err = rel + " is a GLOBAL file (every save): remove it with no career loaded, or move it to this career first (Club edits: where they live)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- moves between scopes
// The legacy files of a club's identity: every crest variant and its minikits
static std::vector<std::string> club_files(int64_t teamid) {
    std::vector<std::string> out;
    for (const CrestVariant& v : crest_variants(teamid)) out.push_back(v.path);
    for (int type = 0; type <= 4; ++type)
        for (int n = 0; n <= 1; ++n)
            out.push_back("data/ui/imgAssets/kits/j" + std::to_string(type) + "_" + std::to_string(teamid) + "_" + std::to_string(n) + ".dds");
    return out;
}

ClubScopeRow App::club_scope_row(int64_t teamid) const {
    ClubScopeRow r;
    r.teamid = teamid;
    r.name = model.team_name(teamid);
    if (const TeamRow* t = model.team(teamid)) r.league = t->league;
    for (const auto& e : team_names.entries)
        if (e.teamid == teamid) r.name_scopes.push_back(e.scope);
    const career_faces::Paths cp = career_faces_paths();
    for (const std::string& rel : club_files(teamid)) {
        const bool crest = career_faces::is_crest_path(rel);
        // a global file: in mods\legacy and not a career's copy put there for the loaded career
        const career_faces::OwnerInfo o = career_faces::owner_of(cp, rel);
        const bool global = o.owner == career_faces::Owner::TurboGlobal || o.owner == career_faces::Owner::Foreign ||
                            (o.owner == career_faces::Owner::TurboCareer && o.base_under);
        if (global) (crest ? r.crest_global : r.kit_global) += 1;
        for (const std::string& c : career_faces::careers_with(cp, rel)) {
            auto& v = crest ? r.crest_careers : r.kit_careers;
            if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
        }
    }
    for (const auto& kv : reapply.kits)
        if (kv.second.teamtechid == teamid) {
            auto& v = r.kept_kit_scopes;
            if (std::find(v.begin(), v.end(), kv.second.scope) == v.end()) v.push_back(kv.second.scope);
        }
    if (kit_swap) r.kit3d_scopes = kit_swap->scopes_of(static_cast<int>(teamid));
    return r;
}

std::vector<int64_t> App::clubs_with_identity_edits() const {
    std::set<int64_t> ids;
    for (const auto& e : team_names.entries) ids.insert(e.teamid);
    for (const auto& kv : reapply.kits) ids.insert(kv.second.teamtechid);
    const career_faces::Paths cp = career_faces_paths();
    // crests / minikits: what mods\legacy and the careers hold (file names l<id>.dds, j<type>_<id>_<n>.dds)
    auto add_from_name = [&](const std::string& rel) {
        std::string n = fs::u8path(rel).filename().u8string();
        if (career_faces::is_crest_path(rel) && n.size() > 5) {
            ids.insert(std::atoll(n.substr(1, n.size() - 5).c_str()));
        } else if (career_faces::is_minikit_path(rel)) {
            const size_t a = n.find('_'), b = n.find('_', a == std::string::npos ? 0 : a + 1);
            if (a != std::string::npos && b != std::string::npos) ids.insert(std::atoll(n.substr(a + 1, b - a - 1).c_str()));
        }
    };
    std::error_code ec;
    const fs::path base = cp.mods_legacy / "data" / "ui" / "imgAssets";
    for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 2) {
            it.disable_recursion_pending();
            continue;
        }
        if (it.depth() == 0 && it->is_directory(ec)) {
            const std::string d = it->path().filename().u8string();
            if (d.compare(0, 5, "crest") != 0 && d != "kits") it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        add_from_name("data/ui/imgAssets/" + it->path().lexically_relative(base).generic_u8string());
    }
    for (fs::directory_iterator it(career_faces::careers_dir(cp), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string id = it->path().filename().u8string();
        if (!career_faces::is_career_id(id)) continue;
        for (const std::string& rel : career_faces::career_files(cp, id)) add_from_name(rel);
    }
    ids.erase(0);
    std::vector<int64_t> out(ids.begin(), ids.end());
    return out;
}

bool App::move_club_identity(int64_t teamid, const std::string& to, std::string* msg) {
    const ScopeKind k = scope_kind(to);
    if (k != ScopeKind::Global && k != ScopeKind::Career) {
        if (msg) *msg = "unknown scope " + to;
        return false;
    }
    const std::string to_career = scope_career(to);
    std::vector<std::string> notes;
    // Global takes every scope of the club; a career takes only the SHARED ones (global / not assigned): another career's own name,
    // crest or kit stays that career's
    auto takes = [&](const std::string& scope) {
        if (scope == to) return false;
        if (k == ScopeKind::Global) return true;
        const ScopeKind f = scope_kind(scope);
        return f == ScopeKind::Global || f == ScopeKind::NotAssigned;
    };
    // 1. the name (the one in effect here first: it is the one the user sees)
    {
        std::vector<std::string> from;
        for (const auto& e : team_names.entries)
            if (e.teamid == teamid && takes(e.scope)) {
                from.push_back(e.scope);
                // the text of a name that is dropped (the target has its own) stays in the log
                if (team_names.find(teamid, to)) log("club scope: " + std::to_string(teamid) + ": name '" + e.name() + "' of " + reapply.scope_label(e.scope) + " dropped (" + reapply.scope_label(to) + " has its own)");
            }
        std::sort(from.begin(), from.end(), [&](const std::string& a, const std::string& b) { return reapply.rank(a) > reapply.rank(b); });
        if (const tnames::Entry* have = team_names.find(teamid, to)) {
            (void)have;  // the target already has a name: it stays, the others go (one name per scope)
            for (const std::string& f : from) team_names.forget(teamid, f);
            if (!from.empty()) save_team_names(nullptr), team_names_publish();
        } else if (!from.empty()) {
            if (!move_team_name(teamid, from.front(), to)) notes.push_back("name: not moved");
            for (size_t i = 1; i < from.size(); ++i) team_names.forget(teamid, from[i]);
            if (from.size() > 1) save_team_names(nullptr), team_names_publish();
        }
        // a career name must not stay in Live Editor's custom_team_names.csv (Live Editor shows that file in every save)
        if (k == ScopeKind::Career && !from.empty()) {
            TeamNamesCsv csv;
            std::string err;
            const fs::path file = team_names_file(bridge.root());
            if (csv.load(file, &err)) {
                std::string full, a3, a10, a15;
                csv.team_names(teamid, full, a3, a10, a15);
                if (!full.empty() || !a3.empty() || !a10.empty() || !a15.empty()) {
                    csv.set_team_names(teamid, "", "", "", "");
                    if (csv.save(file, team_names_backup_dir(bridge.root()), &err))
                        notes.push_back("name taken out of Live Editor's team names file (a backup copy is kept)");
                    else
                        notes.push_back("custom_team_names.csv: " + err);
                }
            } else {
                notes.push_back("custom_team_names.csv: " + err);
            }
        }
    }
    // 2. crest and minikit files: no career copy in mods\legacy while they move, the loaded career's copied in again after
    const career_faces::Paths cp = career_faces_paths();
    const std::string here = career_faces_active_;
    career_faces_sync("", "moving a club's crest / kit files");
    int moved = 0, failed = 0;
    std::string first_err;
    for (const std::string& rel : club_files(teamid)) {
        std::string err;
        if (k == ScopeKind::Career) {
            // the global file (if any) becomes the career's; other careers' own copies stay theirs
            {
                std::error_code fe;
                if (!fs::is_regular_file(career_faces::mods_file(cp, rel), fe)) continue;
            }
            if (career_faces::has_overlay(cp, to_career, rel)) continue;  // the career's own wins; the global one stays
            if (career_faces::move_to_career(cp, to_career, rel, &err)) ++moved;
            else if (++failed == 1) first_err = rel + ": " + err;
        } else {
            // global: the copy of the career in effect here (else of the only career that has one) becomes the global file
            std::vector<std::string> cs = career_faces::careers_with(cp, rel);
            if (cs.empty()) continue;
            std::string from = std::find(cs.begin(), cs.end(), here) != cs.end() ? here : (cs.size() == 1 ? cs.front() : std::string());
            if (from.empty()) {
                if (++failed == 1) first_err = rel + ": several careers have their own copy (open the one whose crest / kit should be global, then Global)";
                continue;
            }
            if (career_faces::move_to_global(cp, from, rel, &err)) ++moved;
            else if (++failed == 1) first_err = rel + ": " + err;
        }
    }
    career_faces_sync(here, "club files moved");
    legacy.forget_folder_looks();
    if (moved) notes.push_back(std::to_string(moved) + " crest / minikit file(s) moved");
    if (failed) notes.push_back(std::to_string(failed) + " not moved (" + first_err + ")");
    // 3. kept kit rows (colours, settings) and 3D kits
    int rows = 0;
    {
        std::vector<std::string> scopes;
        for (const auto& kv : reapply.kits)
            if (kv.second.teamtechid == teamid && takes(kv.second.scope) &&
                std::find(scopes.begin(), scopes.end(), kv.second.scope) == scopes.end())
                scopes.push_back(kv.second.scope);
        for (const std::string& from : scopes) rows += move_kept_kits(teamid, from, to) ? 1 : 0;
        if (rows) notes.push_back("kept kit colours / settings moved (" + std::to_string(rows) + " scope(s))");
    }
    if (kit_swap) {
        const int n = kit_swap->set_team_scope(static_cast<int>(teamid), to);
        if (n > 0) notes.push_back(std::to_string(n) + " 3D kit(s) now " + reapply.scope_label(to));
    }
    std::string line = model.team_name(teamid) + " (" + std::to_string(teamid) + ") -> " + reapply.scope_label(to);
    for (const std::string& n : notes) line += "; " + n;
    log("club scope: " + line);
    if (log_hook) log_hook("club scope: " + line);
    if (msg) *msg = line;
    return failed == 0;
}

std::string App::owner_career_of(int64_t teamid, std::string* why) const {
    std::set<std::string> cs;
    for (const auto& e : team_names.entries)
        if (e.teamid == teamid && scope_kind(e.scope) == ScopeKind::Career) cs.insert(scope_career(e.scope));
    for (const auto& kv : reapply.kits)
        if (kv.second.teamtechid == teamid && scope_kind(kv.second.scope) == ScopeKind::Career) cs.insert(scope_career(kv.second.scope));
    const career_faces::Paths cp = career_faces_paths();
    for (const std::string& rel : club_files(teamid))
        for (const std::string& c : career_faces::careers_with(cp, rel)) cs.insert(c);
    if (cs.size() == 1) return *cs.begin();
    // several careers: the one whose own club it is (its label is "<manager>, <club>")
    std::set<std::string> names{model.team_name(teamid)};
    for (const auto& e : team_names.entries)
        if (e.teamid == teamid) names.insert(e.name());
    std::vector<std::string> own;
    const std::set<std::string>& pool = cs.empty() ? std::set<std::string>() : cs;
    for (const auto& kv : reapply.careers) {
        if (!pool.empty() && !pool.count(kv.first)) continue;
        const std::string& l = kv.second.label;
        const size_t c = l.rfind(", ");
        if (c != std::string::npos && names.count(l.substr(c + 2))) own.push_back(kv.first);
    }
    if (own.size() == 1) return own.front();
    if (why) *why = cs.empty() ? "no career holds an edit of this club" : std::to_string(cs.size()) + " careers hold edits of this club";
    return {};
}

bool is_italian_club_league(int64_t league) { return league == 31 || league == 32; }  // Serie A, Serie B (FC league ids)

// ---------------------------------------------------------------- the panel
void club_scope_body(App& app);
static std::string scope_short(const App& app, const std::string& s) {
    switch (scope_kind(s)) {
        case ScopeKind::Global: return "Global";
        case ScopeKind::NotAssigned: return "Every career (not assigned)";
        case ScopeKind::Career: {
            const std::string id = scope_career(s);
            if (id == app.reapply.career) return "This career";
            auto it = app.reapply.careers.find(id);
            return "Career " + (it != app.reapply.careers.end() && !it->second.label.empty() ? it->second.label : id);
        }
        default: return "?";
    }
}

static std::string careers_short(const App& app, const std::vector<std::string>& ids) {
    std::string s;
    for (const std::string& id : ids) s += (s.empty() ? "" : ", ") + scope_short(app, career_scope(id));
    return s;
}

void club_scope_section(App& app) {
    if (!ImGui::CollapsingHeader("Where club edits live (Global / this career)###clubscope")) return;
    ImGui::PushID("clubscope");
    club_scope_body(app);
    ImGui::PopID();
}

void club_scope_body(App& app) {
    static std::vector<int64_t> clubs;
    static double next = 0.0;
    static std::string last_msg;
    if (app.now >= next || clubs.empty()) {
        clubs = app.clubs_with_identity_edits();
        next = app.now + 3.0;
    }
    ImGui::TextWrapped(
        "Where each club edit lives. Global = every save and the main menu (like the squads file). This career = only the career it was "
        "made in. Edits made with no career loaded are Global; edits made in a career belong to it.");
    std::string refuse;
    const std::string here = app.identity_scope(&refuse);
    const bool in_career = scope_kind(here) == ScopeKind::Career;
    ImGui::TextDisabled("Edits made now: %s", here.empty() ? refuse.c_str() : scope_short(app, here).c_str());
    if (clubs.empty()) {
        ImGui::TextDisabled("No club has a name, crest or kit edit.");
        return;
    }
    // the one-click sort the owner asked for: the Italian clubs global, every other club to the career it belongs to (the one career that
    // holds its edits, or the career whose own club it is); a club with no such career is listed and left as it is
    struct Plan {
        int64_t team;
        std::string to, why;
    };
    static std::vector<Plan> plan;
    ImGui::BeginDisabled(!app.connected());  // the clubs' leagues come from the game's database
    const bool sort_clicked = ImGui::Button("Sort club edits: Italian clubs Global, every other club to its own career###cs_split");
    ImGui::EndDisabled();
    if (!app.connected()) ImGui::TextDisabled("Sorting needs the game's database: load a career (or connect Turbo at the main menu) first.");
    if (sort_clicked) {
        plan.clear();
        for (int64_t t : clubs) {
            Plan p{t, {}, {}};
            if (is_italian_club_league(app.model.team(t) ? app.model.team(t)->league : -1)) p.to = kScopeGlobal;
            else if (const std::string c = app.owner_career_of(t, &p.why); !c.empty()) p.to = career_scope(c);
            plan.push_back(p);
        }
        ImGui::OpenPopup("##cs_split_confirm");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Serie A / Serie B clubs: name, crest and kits Global (every save). Every other club: its shared edits go to the career "
                          "they belong to. You see the list before anything moves.");
    if (ImGui::BeginPopup("##cs_split_confirm")) {
        int moves = 0;
        ImGui::TextUnformatted("These moves will be made:");
        ImGui::BeginChild("##cs_plan", ImVec2(S(560.0f), S(260.0f)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY);
        for (const Plan& p : plan) {
            const std::string name = app.model.team_name(p.team);
            if (p.to.empty()) {
                ImGui::TextDisabled("%s: left as it is (%s)", name.c_str(), p.why.c_str());
            } else {
                ++moves;
                ImGui::Text("%s -> %s", name.c_str(), scope_short(app, p.to).c_str());
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Move###cs_split_go")) {
            int ok = 0, bad = 0;
            for (const Plan& p : plan) {
                if (p.to.empty()) continue;
                std::string m;
                (app.move_club_identity(p.team, p.to, &m) ? ok : bad) += 1;
            }
            last_msg = std::to_string(ok) + " club(s) moved" + (bad ? ", " + std::to_string(bad) + " with a problem (see the log)" : "") + "; " +
                       std::to_string(static_cast<int>(plan.size()) - moves) + " left as they were";
            app.notify("Club edits: " + last_msg, bad > 0);
            next = 0.0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel###cs_split_no")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!last_msg.empty()) ImGui::TextDisabled("%s", last_msg.c_str());
    if (ImGui::BeginTable("##club_scope", 6, tbl::find("##club_scope")->flags, ImVec2(0, S(320.0f)))) {
        ImGui::TableSetupColumn("Club");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Crest");
        ImGui::TableSetupColumn("Kits");
        ImGui::TableSetupColumn("3D kits");
        ImGui::TableSetupColumn("Move", ImGuiTableColumnFlags_WidthFixed, S(190.0f));
        ImGui::TableHeadersRow();
        for (int64_t t : clubs) {
            const ClubScopeRow r = app.club_scope_row(t);
            ImGui::PushID(static_cast<int>(t));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", r.name.c_str(), is_italian_club_league(r.league) ? "  (ITA)" : "");
            ImGui::TableNextColumn();
            std::string n;
            for (const std::string& s : r.name_scopes) n += (n.empty() ? "" : ", ") + scope_short(app, s);
            ImGui::TextUnformatted(n.empty() ? "-" : n.c_str());
            ImGui::TableNextColumn();
            std::string c = r.crest_global ? std::string("Global") : std::string();
            if (!r.crest_careers.empty()) c += (c.empty() ? "" : ", ") + careers_short(app, r.crest_careers);
            ImGui::TextUnformatted(c.empty() ? "-" : c.c_str());
            ImGui::TableNextColumn();
            std::string k;
            for (const std::string& s : r.kept_kit_scopes) k += (k.empty() ? "" : ", ") + scope_short(app, s);
            if (r.kit_global) k += (k.empty() ? "" : ", ") + std::string("Global minikit");
            if (!r.kit_careers.empty()) k += (k.empty() ? "" : ", ") + careers_short(app, r.kit_careers) + " minikit";
            ImGui::TextUnformatted(k.empty() ? "-" : k.c_str());
            ImGui::TableNextColumn();
            std::string d;
            for (const std::string& s : r.kit3d_scopes) d += (d.empty() ? "" : ", ") + scope_short(app, s);
            ImGui::TextUnformatted(d.empty() ? "-" : d.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Global###csg")) {
                std::string m;
                const bool ok = app.move_club_identity(t, kScopeGlobal, &m);
                app.notify("Club edits: " + m, !ok);
                next = 0.0;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!in_career);
            if (ImGui::SmallButton("This career###csc")) {
                std::string m;
                const bool ok = app.move_club_identity(t, here, &m);
                app.notify("Club edits: " + m, !ok);
                next = 0.0;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

}  // namespace turbo
